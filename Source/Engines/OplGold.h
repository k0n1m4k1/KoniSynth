// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

#include "../ThirdParty/TDA8425_emu/TDA8425_emu.h"
#if defined (_MSC_VER)
 #pragma warning (push)
 #pragma warning (disable: 4310 5054) // avisos de la cabecera de terceros (sin modificar), no de KoniSynth
#endif
#include "../ThirdParty/YM7128B_emu/YM7128B_emu.h"
#if defined (_MSC_VER)
 #pragma warning (pop)
#endif

//==============================================================================
// AdLib Gold 1000 (Ad Lib Inc., 1992): lo que la tarjeta pone detrás del OPL3. Fuentes: Ad Lib Gold Developer Toolkit 1.01
// (registros del chip de control 04h-08h y 18h; edición OCR en slartibardfast.github.io/adlib_gold), hojas de datos del
// TDA8425 y del YM7128B a través de los emuladores de A. Zoppi (TDA8425_emu 0.2.0 e YM7128B_emu 0.1.3, BSD-2-Clause, sin
// decap: Source/ThirdParty) y la cadena de DOSBox Staging (adlib_gold.cpp, GPL-2.0+; oplmode = opl3gold). Ver
// User/OPL/HARDWARE.md.
//  - Orden de la señal (el de DOSBox Staging): el módulo de surround SP2 (YM7128B) recibe L + R y su salida húmeda se suma a
//    la seca; la suma entra en el procesador estéreo TDA8425 (graves, agudos, modo estéreo).
//  - TDA8425: el modelo del emulador de Zoppi, de la hoja de datos: shelving de primer orden a 300 Hz (graves, -12..+15 dB)
//    y 4,5 kHz (agudos, -12..+12 dB) en pasos de 3 dB; pseudoestéreo = dos pasa-todo en el canal izquierdo (C1 = C2 = 15 nF:
//    los condensadores de la Gold no constan, supuesto); espacial = 52 % de diafonía; filtro T apagado (uso no documentado);
//    corte de continua a 10 Hz (condensadores de acoplo, supuesto). DOSBox Staging usa otros valores, ajustados de oído contra
//    grabaciones que no publica (400 Hz / 2,5 kHz, +3 dB de agudos): aquí se sigue la hoja de datos.
//  - YM7128B: el motor Fixed del emulador (aritmética de 16 bits con saturación, sobremuestreo ×2 de la salida) a su propia
//    tasa: reloj de 7,159 MHz / 304 = 23 549,6 Hz (calculado; el manual de la Gold dice 23,6 y 28,6 kHz en dos sitios). Se
//    alimenta con la salida del chip OPL3 (49 716 Hz) remuestreada, y su salida (47 099 Hz) se remuestrea a la del host.
//  - Nivel de la señal húmeda: ×1,8 sobre el chip ideal, el ajuste de DOSBox Staging contra grabaciones del hardware real (la
//    única referencia con hardware que hay); el sobremuestreo ×2 del motor Fixed rellena con ceros y su núcleo suma 1, así
//    que su salida queda a la mitad (-6 dB, calculado): se compensa con ×2.
//  - Latencia: la señal seca no se retrasa (filtros IIR). La húmeda llega 0,64 ms más tarde que en el aparato (calculado:
//    colchón de 32 muestras a 49 716 Hz; los remuestreadores son de fase cero, solo piden muestras por delante: ~16 muestras
//    del chip la rama húmeda frente a 8 la seca), menos que un paso de toma (3,2 ms).
//  - Programas de surround: los juegos (Dune, 1992) escribían los suyos y no se conoce ninguna tabla original de Ad Lib. Los
//    de aquí son propios (no del aparato) y así se dice en la ayuda.
struct OplGold
{
    static constexpr double kSurroundRate = 7159090.0 / 304.0; // YM7128B_Input_Rate sin redondear
    static constexpr int numPrograms = 5;                      // Off + 4 propios

    void prepare (double chipRate, double hostRate, float chipGain)
    {
        gain = chipGain;
        down.prepare (chipRate, kSurroundRate);
        up.prepare (2.0 * kSurroundRate, hostRate);
        TDA8425_Chip_Ctor (&tda);
        TDA8425_Chip_Setup (&tda, (TDA8425_Float) hostRate, TDA8425_Pseudo_C1_Table[0], TDA8425_Pseudo_C2_Table[0],
                            TDA8425_Tfilter_Mode_Disabled);
        YM7128B_ChipFixed_Ctor (&ym);
        reset();
    }

    void reset()
    {
        TDA8425_Chip_Reset (&tda);
        TDA8425_Chip_Start (&tda);
        YM7128B_ChipFixed_Reset (&ym);
        YM7128B_ChipFixed_Start (&ym);
        down.reset();
        up.reset();
        clearFifo();
        pendingOut = false;
        bass = treble = mode = program = level = -1; // se reescriben todos los registros en el siguiente apply
    }

    // Registros a partir de los parámetros (solo los que cambian). bassDb -12..+15, trebleDb -12..+12 (pasos de 3 dB),
    // stereo 0 mono forzado, 1 lineal, 2 pseudo, 3 espacial; surround 0 Off, 1-4 programas; levelDb -60..0 (pasos de 2 dB)
    void apply (int bassDb, int trebleDb, int stereo, int surround, int levelDb)
    {
        const auto b = juce::jlimit (0, 11, 6 + bassDb / 3), t = juce::jlimit (0, 10, 6 + trebleDb / 3);
        if (b != bass)   { bass = b;   TDA8425_Chip_Write (&tda, TDA8425_Reg_BA, (TDA8425_Register) b); }
        if (t != treble) { treble = t; TDA8425_Chip_Write (&tda, TDA8425_Reg_TR, (TDA8425_Register) t); }
        if (stereo != mode)
        {
            mode = stereo;
            // SF: IS/ML = 110 (Stereo 1), STL.EFL = modo, bit 6 = 0 (corte de continua), bit 7 = 1 (sin filtro T)
            TDA8425_Chip_Write (&tda, TDA8425_Reg_SF, (TDA8425_Register) (0x80 | ((stereo & 3) << 3) | TDA8425_Selector_Stereo_1));
            TDA8425_Chip_Write (&tda, TDA8425_Reg_VL, 60); // 0 dB (TDA8425_Volume_Data_Unity)
            TDA8425_Chip_Write (&tda, TDA8425_Reg_VR, 60);
        }
        const auto p = juce::jlimit (0, numPrograms - 1, surround);
        const auto l = juce::jlimit (0, 30, -levelDb / 2);
        if (p != program || l != level)
        {
            if ((p == 0) != (program <= 0)) { clearFifo(); down.reset(); up.reset(); pendingOut = false; }
            program = p;
            level = l;
            writeProgram (p, 0x20 | (31 - l)); // VL / VR en fase
        }
    }

    bool surroundOn() const { return program > 0; }

    // Hilo de audio, por muestra del chip OPL3 (49 716 Hz) mientras suena el surround: entrada del YM7128B (L + R)
    void push (float l, float r)
    {
        fifo[(size_t) (writeIndex & kFifoMask)] = l + r;
        ++writeIndex;
    }

    // Por muestra del host: seca + húmeda, por el TDA8425
    std::array<float, 2> process (std::array<float, 2> dry)
    {
        TDA8425_Chip_Process_Data data {};
        data.inputs[0][0] = dry[0];
        data.inputs[0][1] = dry[1];
        if (program > 0)
        {
            const auto wet = up.nextStereo ([this] { return surroundFrame(); });
            data.inputs[0][0] += wet[0];
            data.inputs[0][1] += wet[1];
        }
        TDA8425_Chip_Process (&tda, &data);
        return { (float) data.outputs[0], (float) data.outputs[1] };
    }

    // Nombres de los programas (UI)
    static const char* programName (int p)
    {
        static const char* names[numPrograms] = { "Off", "Narrow", "Wide", "Room", "Hall" };
        return names[juce::jlimit (0, numPrograms - 1, p)];
    }

private:
    static constexpr int kFifoSize = 512, kFifoMask = kFifoSize - 1, kFifoPrefill = 32;

    void clearFifo()
    {
        fifo.fill (0.0f);
        readIndex = 0;
        writeIndex = kFifoPrefill; // colchón: los remuestreadores de la rama húmeda piden muestras por delante de la seca
    }

    float pop()
    {
        if (readIndex >= writeIndex) return 0.0f; // no debería pasar con el colchón
        return fifo[(size_t) (readIndex++ & kFifoMask)];
    }

    // Una muestra de salida del YM7128B a 47 099 Hz (dos por cada muestra de entrada)
    std::array<float, 2> surroundFrame()
    {
        if (pendingOut) { pendingOut = false; return pending; }
        const auto in = down.next ([this] { return pop(); });
        YM7128B_ChipFixed_Process_Data data {};
        data.inputs[0] = (YM7128B_Fixed) juce::jlimit (-32767, 32767, juce::roundToInt (in / gain)); // a la escala del chip
        YM7128B_ChipFixed_Process (&ym, &data);
        const auto k = gain * kWetGain;
        pending = { k * (float) data.outputs[0][1], k * (float) data.outputs[1][1] };
        pendingOut = true;
        return { k * (float) data.outputs[0][0], k * (float) data.outputs[1][0] };
    }

    // Programas propios (no del aparato). Registros del YM7128B: ganancias GL1-8 / GR1-8, VM, VC, VL, VR en 6 bits (bit 5 =
    // en fase, 0x1F = 0 dB, -2 dB por paso, 0 = silencio); C0 / C1 del FIR de realimentación (6 bits altos de un
    // coeficiente de 12 bits con signo: 0x10 = +0,5); tomas T0-T8 en códigos de 3,2 ms (0-31 = 0-100 ms). T0 alimenta la
    // realimentación (C0, C1, VC); T1-T8 van a las salidas
    void writeProgram (int p, int outputLevel)
    {
        struct Program { std::uint8_t gl[8], gr[8], vc, taps[9]; };
        static const Program programs[numPrograms] = {
            { {}, {}, 0, {} },
            // Narrow: dos ecos de Haas, 3,2 ms a la izquierda y 6,5 ms a la derecha
            { { 0x3F }, { 0, 0x3F }, 0, { 0, 1, 2 } },
            // Wide: cuatro ecos de 9,7 a 29 ms, alternando lado y polaridad (0x1B: -8 dB en contrafase)
            { { 0x3D, 0, 0x1B }, { 0, 0x3D, 0, 0x1B }, 0, { 0, 3, 5, 7, 9 } },
            // Room: 8 ecos de 6,5 a 74 ms y realimentación de -18 dB desde 25,8 ms
            { { 0x3E, 0, 0x1A, 0, 0x37, 0, 0x14, 0 }, { 0, 0x1D, 0, 0x39, 0, 0x16, 0, 0x33 }, 0x36, { 8, 2, 4, 7, 10, 13, 16, 19, 23 } },
            // Hall: 8 ecos de 16 a 100 ms y realimentación de -10 dB desde 64,5 ms
            { { 0x3D, 0, 0x19, 0, 0x35, 0, 0x12, 0 }, { 0, 0x1C, 0, 0x37, 0, 0x14, 0, 0x31 }, 0x3A, { 20, 5, 9, 13, 17, 21, 25, 28, 31 } },
        };
        const auto& g = programs[(size_t) p];
        for (int i = 0; i < 8; ++i)
        {
            YM7128B_ChipFixed_Write (&ym, (YM7128B_Address) (YM7128B_Reg_GL1 + i), g.gl[i]);
            YM7128B_ChipFixed_Write (&ym, (YM7128B_Address) (YM7128B_Reg_GR1 + i), g.gr[i]);
        }
        // VM -12 dB: la entrada es L + R (×2 con un sonido centrado) y la salida lleva ×3,6 (ver kWetGain); así cada toma a
        // 0 dB suena a 0,9 veces la señal seca. Con VM a 0 dB, Narrow medía +12,9 dB sobre la seca (KoniBench)
        YM7128B_ChipFixed_Write (&ym, YM7128B_Reg_VM, p > 0 ? 0x39 : 0);
        YM7128B_ChipFixed_Write (&ym, YM7128B_Reg_VC, g.vc);
        YM7128B_ChipFixed_Write (&ym, YM7128B_Reg_VL, (YM7128B_Register) (p > 0 ? outputLevel : 0));
        YM7128B_ChipFixed_Write (&ym, YM7128B_Reg_VR, (YM7128B_Register) (p > 0 ? outputLevel : 0));
        YM7128B_ChipFixed_Write (&ym, YM7128B_Reg_C0, 0x10);
        YM7128B_ChipFixed_Write (&ym, YM7128B_Reg_C1, 0x10);
        for (int i = 0; i < 9; ++i)
            YM7128B_ChipFixed_Write (&ym, (YM7128B_Address) (YM7128B_Reg_T0 + i), g.taps[i]);
    }

    static constexpr float kWetGain = 1.8f * 2.0f; // ajuste de DOSBox Staging × compensación del sobremuestreo (ver arriba)

    TDA8425_Chip tda {};
    YM7128B_ChipFixed ym {};
    SincResampler down, up;
    std::array<float, kFifoSize> fifo {};
    juce::int64 readIndex = 0, writeIndex = 0;
    std::array<float, 2> pending {};
    bool pendingOut = false;
    float gain = 1.0f;
    int bass = -1, treble = -1, mode = -1, program = -1, level = -1;
};

// Motor Gravis UltraSound (1992): síntesis wavetable del GF1 con los patches .PAT del usuario (no se incluyen: el juego de
// Gravis no es redistribuible). Extraído como cabecera: se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación
// (usa DataSlot, ZohResampler y Params).
//
// Fuente primaria: "Low Level Toolkit/Source for UltraSound", GUS SDK v2.01 (Advanced Gravis / Forte, 1993; ULTRADOC\LOWSRC.DOC,
// ULTRASRC\VOCFREQ.C, ULTRASRC\PATCH.H). Se implementa a partir de sus datos, sin copiar su código ni sus tablas (su licencia
// no permite redistribuirlos). Lo verificado en el SDK:
// - El GF1 es un procesador en cadena: atiende una voz cada 1,6 µs y recorre las voces activas (14-32; menos de 14 se fuerza a
//   14). Cada voz se actualiza a fs = 1 000 000 / (1,619695497 · n) Hz: 44 100 Hz con 14 voces, 19 293 Hz con 32 (§1.5).
// - Frecuencia (reg. 1): bits 15-10 entero y 9-1 fracción; es lo que avanza la posición por actualización. La fracción se
//   interpola linealmente entre las dos muestras vecinas, a 16 bits también con datos de 8 bits (§1.5, §2.6.2.2). Un paso
//   mayor que 1 salta muestras sin filtrar: "could cause some distortion or aliasing" (§1.5). El driver calcula el paso como
//   round(f · 512 / fs) (VOCFREQ.C): el tono queda cuantizado a 1/512 de muestra por actualización.
// - Volumen actual (reg. 9): 12 bits, exponente de 4 y mantisa de 8 (más 4 de precisión durante las rampas); inicio y fin de
//   rampa, 8 bits (exponente 4, mantisa 4). Rampa (reg. 6): incremento de 1 a 63 aplicado cada 1, 8, 64 o 512
//   actualizaciones (§1.6): con 14 voces, 0-4095 en 1,4 ms (rate 0, 63) a 47 s (rate 3, 1); con 32 voces, 2,3 veces más.
// - Paneo (reg. C): 4 bits, 0 = izquierda, 15 = derecha (16 posiciones, sin centro exacto) (§2.6.2.13).
// - Bucles hacia delante, bidireccionales y hacia atrás (reg. 0). Todas las voces activas se suman siempre, también las
//   paradas (su dato actual por su volumen) (§1.7.5).
// - Patch .PAT (PATCH.H, "GF1PATCH110"): cabecera de 129 bytes, instrumento de 63, capa de 47, y por muestra 96 bytes de
//   cabecera seguidos de sus datos.
// El driver (envolventes, velocidad, volumen y pan por canal, pitch bend, vibrato, trémolo, reparto de voces) sigue la
// biblioteca del mismo SDK, leída en su código máquina enlazado en ULTRA3D\PATCH.EXE: cada regla cita su función y dirección.
// Que ULTRAMID (el driver MIDI residente de Gravis) use la misma biblioteca no está verificado.
// Estimaciones (marcadas donde se usan): la ley de ganancia del paneo (potencia constante, como DOSBox Staging), la etapa
// analógica de salida (paso bajo de primer orden a 8 kHz, ajuste de DOSBox Staging a grabaciones de la Classic), la saturación
// del sumador a 16 bits, el orden de búsqueda de la muestra de un patch y la gestión de la RAM.
#pragma once

//==============================================================================
// Un patch .PAT ya leído: muestras a 16 bits con signo, bucles y parámetros de cada muestra
struct GusPatch
{
    struct Sample
    {
        std::vector<juce::int16> data;   // datos a 16 bits con signo (los de 8 bits, desplazados 8 bits: el GF1 interpola a 16)
        juce::int64 loopStart = 0, loopEnd = 0; // en 1/512 de muestra (posición del GF1: 9 bits de fracción)
        int sampleRate = 44100;
        juce::uint32 lowMilli = 0, highMilli = 0xFFFFFFFFu, rootMilli = 261626; // frecuencias en milésimas de Hz, como en el .PAT
        int balance = 7;                  // 0-15
        std::array<juce::uint8, 6> envRate {}, envOffset {};
        int tremSweep = 0, tremRate = 0, tremDepth = 0, vibSweep = 0, vibRate = 0, vibDepth = 0;
        juce::uint8 modes = 0;
        int scaleNote = 60, scaleFactor = 1024; // afinación por tecla: 1024 = un semitono por tecla; 0 = tono fijo (percusión)
        bool looping() const     { return (modes & 0x04) != 0; }
        bool bidir() const       { return (modes & 0x08) != 0; }
        bool sustain() const     { return (modes & 0x20) != 0; }
        bool envelope() const    { return (modes & 0x40) != 0; }
        bool fastRelease() const { return (modes & 0x80) != 0; } // PATCH.H no lo documenta; la biblioteca lo usa (ver noteOff)
    };
    juce::String name;
    std::vector<Sample> samples;
    juce::int64 bytes = 0; // memoria que ocupa en la DRAM de la tarjeta (los datos, como los cargaba el driver)

    // Muestra para una frecuencia (mHz), como _GF1_HERTZ_ON (16EE:1CDC): se queda la ranura actual si low < f < high
    // (estricto); si no, recorre las ranuras 0-15 y elige la primera que contenga f (low <= f <= high) salvo que la siguiente
    // también lo contenga (con rangos solapados gana la última del tramo). Sin ninguna, -1: la nota no suena. Una voz tiene 16
    // ranuras: las muestras de más no se usan
    int sampleSlot (juce::uint32 milli, int current) const
    {
        const auto slots = juce::jmin (16, (int) samples.size());
        const auto inside = [&] (int k) { return milli >= samples[(size_t) k].lowMilli && milli <= samples[(size_t) k].highMilli; };
        if (juce::isPositiveAndBelow (current, slots))
            if (const auto& s = samples[(size_t) current]; s.highMilli > milli && s.lowMilli < milli)
                return current;
        for (int k = 0; k < slots; ++k)
            if (inside (k) && ! (k + 1 < slots && inside (k + 1)))
                return k;
        return -1;
    }

    static std::unique_ptr<GusPatch> load (const juce::File& file, juce::String& error)
    {
        juce::MemoryBlock block;
        if (! file.loadFileAsData (block)) { error = "cannot read " + file.getFileName(); return nullptr; }
        const auto* d = static_cast<const juce::uint8*> (block.getData());
        const auto size = (juce::int64) block.getSize();
        if (size < 239 || std::memcmp (d, "GF1PATCH1", 9) != 0) { error = file.getFileName() + " is not a GF1 patch"; return nullptr; }
        const auto u16 = [d] (juce::int64 o) { return (int) d[o] | ((int) d[o + 1] << 8); };
        const auto s16 = [&u16] (juce::int64 o) { return (int) (juce::int16) u16 (o); };
        const auto u32 = [d] (juce::int64 o) { return (juce::uint32) d[o] | ((juce::uint32) d[o + 1] << 8) | ((juce::uint32) d[o + 2] << 16) | ((juce::uint32) d[o + 3] << 24); };
        auto patch = std::make_unique<GusPatch>();
        patch->name = file.getFileNameWithoutExtension();
        const auto count = (int) d[198]; // muestras de la (única) capa: LAYERDATA.samples
        juce::int64 at = 239;
        for (int k = 0; k < count && at + 96 <= size; ++k)
        {
            Sample s;
            const auto fractions = d[at + 7];
            const auto waveBytes = (juce::int64) u32 (at + 8);
            const auto loopStartBytes = (juce::int64) u32 (at + 12), loopEndBytes = (juce::int64) u32 (at + 16);
            s.sampleRate = juce::jmax (1, u16 (at + 20));
            s.lowMilli = u32 (at + 22);   // frecuencias en milésimas de Hz
            s.highMilli = u32 (at + 26);
            s.rootMilli = juce::jmax ((juce::uint32) 1, u32 (at + 30));
            s.balance = juce::jlimit (0, 15, (int) d[at + 36]);
            for (int e = 0; e < 6; ++e) { s.envRate[(size_t) e] = d[at + 37 + e]; s.envOffset[(size_t) e] = d[at + 43 + e]; }
            // PATCHDATA de PATCH.H: tremolo_sweep/rate/depth (49-51) y vibrato_sweep/rate/depth (52-54)
            s.tremSweep = d[at + 49]; s.tremRate = d[at + 50]; s.tremDepth = d[at + 51];
            s.vibSweep = d[at + 52];  s.vibRate = d[at + 53];  s.vibDepth = d[at + 54];
            s.modes = d[at + 55];
            s.scaleNote = u16 (at + 56);
            s.scaleFactor = u16 (at + 58);
            at += 96;
            if (at + waveBytes > size) { error = file.getFileName() + ": truncated sample data"; return nullptr; }
            const auto is16 = (s.modes & 0x01) != 0, isUnsigned = (s.modes & 0x02) != 0;
            const auto bytesPerSample = is16 ? 2 : 1;
            const auto n = waveBytes / bytesPerSample;
            s.data.resize ((size_t) n + 1); // + 1: la vecina de la última muestra (interpolación)
            for (juce::int64 i = 0; i < n; ++i)
            {
                int v = is16 ? (isUnsigned ? u16 (at + 2 * i) - 32768 : s16 (at + 2 * i))
                             : (isUnsigned ? (int) d[at + i] - 128 : (int) (juce::int8) d[at + i]) * 256;
                s.data[(size_t) i] = (juce::int16) v;
            }
            s.data[(size_t) n] = n > 0 ? s.data[(size_t) n - 1] : 0;
            // Bucle en muestras con su fracción (fractions: nibble bajo, inicio; alto, fin; en 1/16) y en 1/512 (posición del GF1)
            s.loopStart = (loopStartBytes / bytesPerSample) * 512 + (fractions & 0x0F) * 32;
            s.loopEnd = (loopEndBytes / bytesPerSample) * 512 + (fractions >> 4) * 32;
            s.loopEnd = juce::jlimit ((juce::int64) 0, n * 512, s.loopEnd);
            s.loopStart = juce::jlimit ((juce::int64) 0, s.loopEnd, s.loopStart);
            if ((s.modes & 0x10) != 0) // "looping backward": se invierte (como TiMidity)
            {
                std::reverse (s.data.begin(), s.data.begin() + (std::ptrdiff_t) n);
                const auto a = n * 512 - s.loopEnd, b = n * 512 - s.loopStart;
                s.loopStart = a; s.loopEnd = b;
            }
            patch->bytes += waveBytes;
            patch->samples.push_back (std::move (s));
            at += waveBytes;
        }
        if (patch->samples.empty()) { error = file.getFileName() + ": no samples"; return nullptr; }
        return patch;
    }
};

//==============================================================================
// Juego de patches: la carpeta del usuario y su mapa de programas (timidity.cfg, o DEFAULT.CFG de Gravis). Formato de las líneas
// de mapa: "<número> <fichero> [opciones]" bajo "bank 0" (melódicos) o "drumset 0" (percusión: el número es la tecla). Un
// número de 128 a 255 fuera de drumset es percusión (tecla = número - 128), como los programas de percusión del driver de Gravis.
// Se construye en el hilo de mensajes y se publica por DataSlot: inmutable después
struct GusBank
{
    std::vector<std::unique_ptr<GusPatch>> patches;
    std::array<const GusPatch*, 128> melodic {}, drums {};
    std::array<int, 128> drumNote {}; // tecla a la que suena cada percusión ("note=" de timidity.cfg; -1 = la tocada)
    juce::String description;

    static std::unique_ptr<GusBank> load (const juce::File& folder, juce::String& report)
    {
        auto bank = std::make_unique<GusBank>();
        bank->drumNote.fill (-1);
        juce::File cfg;
        for (const auto& name : { "timidity.cfg", "default.cfg", "gravis.cfg" })
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.cfg"))
                if (f.getFileName().equalsIgnoreCase (name) && cfg == juce::File())
                    cfg = f;
        if (cfg == juce::File())
        {
            report = "No timidity.cfg or DEFAULT.CFG in " + folder.getFullPathName();
            return nullptr;
        }
        std::map<juce::String, const GusPatch*> loaded;
        int errors = 0;
        const auto patchFor = [&] (juce::String file) -> const GusPatch*
        {
            if (! file.endsWithIgnoreCase (".pat")) file << ".pat";
            const auto key = file.toLowerCase();
            if (const auto it = loaded.find (key); it != loaded.end()) return it->second;
            juce::File f = folder.getChildFile (file);
            if (! f.existsAsFile())
                for (const auto& c : folder.findChildFiles (juce::File::findFiles, true, "*.pat"))
                    if (c.getFileName().equalsIgnoreCase (juce::File::createFileWithoutCheckingPath (file).getFileName())) { f = c; break; }
            juce::String error;
            auto p = f.existsAsFile() ? GusPatch::load (f, error) : nullptr;
            const auto* raw = p.get();
            if (p != nullptr) bank->patches.push_back (std::move (p));
            else ++errors;
            loaded[key] = raw;
            return raw;
        };
        bool drumset = false, skipBank = false;
        for (auto line : juce::StringArray::fromLines (cfg.loadFileAsString()))
        {
            line = line.upToFirstOccurrenceOf ("#", false, false).trim();
            if (line.isEmpty()) continue;
            juce::StringArray t;
            t.addTokens (line, " \t", "\"");
            t.removeEmptyStrings();
            if (t[0].equalsIgnoreCase ("bank"))    { drumset = false; skipBank = t[1].getIntValue() != 0; continue; }
            if (t[0].equalsIgnoreCase ("drumset")) { drumset = true;  skipBank = t[1].getIntValue() != 0; continue; }
            if (skipBank || t.size() < 2 || ! t[0].containsOnly ("0123456789")) continue;
            const auto number = t[0].getIntValue();
            const auto isDrum = drumset || (number >= 128 && number < 256);
            const auto index = isDrum && ! drumset ? number - 128 : number;
            if (! juce::isPositiveAndBelow (index, 128)) continue;
            const auto* patch = patchFor (t[1].unquoted());
            (isDrum ? bank->drums : bank->melodic)[(size_t) index] = patch;
            for (int k = 2; k < t.size(); ++k)
                if (isDrum && t[k].startsWithIgnoreCase ("note="))
                    bank->drumNote[(size_t) index] = juce::jlimit (0, 127, t[k].fromFirstOccurrenceOf ("=", false, false).getIntValue());
        }
        if (bank->patches.empty())
        {
            report = "No usable .PAT files for " + cfg.getFileName() + " in " + folder.getFullPathName();
            return nullptr;
        }
        juce::int64 total = 0;
        for (const auto& p : bank->patches) total += p->bytes;
        bank->description = juce::String ((int) bank->patches.size()) + " patches (" + juce::String (total / 1024) + " KB) from " + cfg.getFileName()
                            + (errors > 0 ? ", " + juce::String (errors) + " missing" : juce::String());
        report = bank->description;
        return bank;
    }
};

using GusSlot = DataSlot<GusBank>;

//==============================================================================
struct GusEngine
{
    static constexpr int maxVoices = 32;
    GusSlot slot;

    void prepare (double sampleRate, int)
    {
        hostRate = sampleRate;
        outLpf = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 8000.0 / sampleRate);
        setVoices (activeVoices, true);
    }

    void reset()
    {
        for (auto& v : voices) v = Voice {};
        for (auto& c : channels) c = Channel {};
        resident.clear();
        residentBytes = 0;
        resampler.reset();
        lpState = { 0.0f, 0.0f };
        nextVoice = 0;
        timerElapsed = 0.0;
    }

    //==========================================================================
    // Teclado: Keys (canal 1 con el programa del panel) o Rhythm (percusión). Multi: los 16 canales MIDI como un módulo GM
    // (percusión en el canal 10), para ficheros .mid y para un DAW
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        const auto* bank = slot.current.load (std::memory_order_acquire);
        if (bank != lastBank) { lastBank = bank; reset(); }
        setVoices (juce::jlimit (14, 32, juce::roundToInt (p.gusVoices->load())), false);
        // RAM: la de la perilla, acotada por el modelo (Wikipedia, Gravis_Ultrasound): Classic 256 KB-1 MB, MAX 512 KB-1 MB,
        // Extreme 1 MB soldado
        static const juce::int64 ramSizes[] = { 256 * 1024, 512 * 1024, 768 * 1024, 1024 * 1024 };
        const auto model = juce::jlimit (0, 2, juce::roundToInt (p.gusModel->load()));
        const auto ramIndex = model == 2 ? 3 : juce::jlimit (model == 1 ? 1 : 0, 3, juce::roundToInt (p.gusRam->load()));
        if (const auto ram = ramSizes[ramIndex]; ram != ramBytes)
        {
            ramBytes = ram;
            resident.clear(); // otra memoria: el driver vuelve a cargar los patches
            residentBytes = 0;
        }
        const auto multi = p.gusMulti->load() > 0.5f;
        const auto mode = juce::jlimit (0, 1, juce::roundToInt (p.gusMode->load()));
        const auto keysProgram = juce::jlimit (0, 127, juce::roundToInt (p.gusProgram->load()));
        const auto n = out.getNumSamples();
        int at = 0;
        for (const auto meta : midi)
        {
            renderTo (out, at, juce::jlimit (0, n, meta.samplePosition));
            at = juce::jlimit (0, n, meta.samplePosition);
            const auto msg = meta.getMessage();
            if (msg.getRawDataSize() > 3 || msg.getRawDataSize() < 2) continue;
            const auto channel = multi ? msg.getChannel() - 1 : (mode == 1 ? 9 : 0);
            if (! multi && mode == 0) channels[0].program = keysProgram;
            handle (bank, msg, channel);
        }
        renderTo (out, at, n);
    }

private:
    //==========================================================================
    // GF1: una voz, y lo que la biblioteca de Gravis guarda de ella
    struct Voice
    {
        const GusPatch::Sample* s = nullptr;
        const GusPatch* patch = nullptr;
        juce::int64 pos = 0;        // posición en 1/512 de muestra
        int fc = 0;                 // paso calculado por el driver (_GF1_FC_REGISTER), en 1/512 de muestra por actualización
        int inc = 0;                // paso que usa el GF1: fc más el vibrato (registro de frecuencia >> 1)
        bool backwards = false, stopped = true, loopOff = false;
        // Rampa de volumen (regs. 6-9 y 0D): volumen actual en 1/16 de paso de los 12 bits; límites de 8 bits, << 8
        int vol = 0, rampLow = 0, rampHigh = 0, rampStep = 0, rampEvery = 1, rampCount = 0;
        bool ramping = false, rampDown = false, rampLoop = false, rampIrq = true;
        int pan = 7;
        // driver
        int channel = -1, note = -1, stage = 0, pendingStage = -1;
        int velocity = 127, chanVolume = 127, att = 255; // _GF1_VELOCITY, _GF1_CHAN_VOLUME, _GF1_ATTENUATION
        juce::uint32 freqMilli = 0; // frecuencia de la nota sin pitch bend (tabla de notas), mHz
        bool released = false, pedalHeld = false;
        int priority = 0;           // prioridad de robo de la capa MIDI: 10 sonando, +5 sostenida por el pedal, +20 soltada
        juce::uint64 age = 0;
        int vibCount = 1, vibTicker = 0, vibQuarter = 0, vibSweepLeft = 0, vibOffset = 0; // _VIBE_COUNT/TICKER/QUARTER/SWEEP/FC_OFFSET
        int tremStart = 0, tremEnd = 0, tremRate = 0;                                    // _TREM_START/END/VOLINC
    };
    struct Channel
    {
        int program = 0, volume = 127, expression = 127, pan = 16, bendRange = 2, rpnMsb = 0, rpnLsb = 0;
        int bend1000 = 1000;        // relación del pitch bend en milésimas
        bool sustain = false;
    };

    // Volumen del GF1 (12 bits: exponente e, mantisa m) a amplitud lineal: ((256 + m) / 512) · 2^(e - 15). 4095: 0 dB;
    // 0: -96 dB (calculado: mantisa a la mitad y 15 octavas)
    static float amplitudeOf (int vol12)
    {
        static const auto table = []
        {
            std::array<float, 4096> t {};
            for (int v = 0; v < 4096; ++v)
                t[(size_t) v] = (float) ((256 + (v & 0xFF)) / 512.0 * std::ldexp (1.0, (v >> 8) - 15));
            return t;
        }();
        return table[(size_t) juce::jlimit (0, 4095, vol12)];
    }

    //==========================================================================
    // Driver: la biblioteca del GUS SDK v2.01, leída en su código enlazado en ULTRA3D\PATCH.EXE (con los símbolos de depuración
    // de Borland que trae el ejecutable). Cada regla cita la función y su dirección (segmento:desplazamiento de la imagen).
    // Las tablas se calculan (coinciden entrada a entrada con las de la biblioteca: 127/127 y 12/12, comprobado)

    // _SCALE_TABLE: floor(440 000 · 2^((n - 69) / 12)) mHz para las notas 0-126 (error <= 0,19 cents); la nota 127 vale 0:
    // no suena, como con la biblioteca
    static juce::uint32 noteMilli (int n)
    {
        static const auto t = []
        {
            std::array<juce::uint32, 129> a {};
            for (int k = 0; k < 127; ++k) a[(size_t) k] = (juce::uint32) std::floor (440000.0 * std::exp2 ((k - 69) / 12.0) + 1.0e-6);
            return a;
        }();
        return t[(size_t) juce::jlimit (0, 128, n)];
    }

    // Semitonos en milésimas: round(1000 · 2^(k / 12)), k = 0-11 (tabla de la biblioteca en DS:2D4D)
    static juce::int64 semitone1000 (int k)
    {
        static const auto t = []
        {
            std::array<juce::int64, 12> a {};
            for (int i = 0; i < 12; ++i) a[(size_t) i] = (juce::int64) std::floor (1000.0 * std::exp2 (i / 12.0) + 0.5);
            return a;
        }();
        return t[(size_t) juce::jlimit (0, 11, k)];
    }

    // _GF1_ATTENUATION (_GF1_VOICE_VELOCITY 16EE:179D, _GF1_CHANNEL_VOLUME 16EE:1815): (M + 64)(vel + 128)(canal + 128) / 48 705,
    // con M = 127 (volumen maestro que fija _GF1_ACTIVE_VOICES), vel y canal acotados a 1-127. Escala los niveles de la
    // envolvente: 255 = sin atenuar; velocidad 1 con canal 127 da 129
    static int attenuation (int velocity, int chanVolume)
    {
        return (127 + 64) * (juce::jlimit (1, 127, velocity) + 128) * (juce::jlimit (1, 127, chanVolume) + 128) / 48705;
    }

    // Nivel de 8 bits de un punto de la envolvente: offset · atenuación / 255, mínimo 8 (_PROCESS_STATUS 2018:0A45)
    static int envTarget (const Voice& v, int stage)
    {
        return juce::jmax (8, v.s->envOffset[(size_t) stage] * v.att / 255);
    }

    // Paso del GF1 para una frecuencia (mHz), como _GF1_CALC_FC (16EE:2478): relación = round(fs · 512 / tasa de la muestra),
    // con fs entera = 617 400 / voces (reset_ultra 16EE:0646); t = round(f · 512 / raíz); paso = round(t · 512 / relación). En
    // aritmética de 32 bits sin signo, como la biblioteca: f · 512 desborda por encima de 8 388 Hz y esas notas suenan
    // desafinadas. El registro guarda el paso << 1 en 16 bits: queda en 0-32 767
    int fcFor (const GusPatch::Sample& s, juce::uint32 milli) const
    {
        const auto chip = (juce::uint32) (617400 / activeVoices);
        const auto rate = (juce::uint32) juce::jmax (1, s.sampleRate);
        const auto ratio = juce::jmax ((juce::uint32) 1, ((chip << 9) + (rate >> 1)) / rate);
        const auto root = juce::jmax ((juce::uint32) 1, s.rootMilli);
        const auto t = ((milli << 9) + (root >> 1)) / root;
        return (int) ((((t << 9) + (ratio >> 1)) / ratio) & 0x7FFF);
    }

    // Escala por tecla del patch (calculate_scaled_frequency 16EE:1A15), si scale_factor != 1024: la nota de la tabla justo por
    // debajo de f, (nota - scale_frequency) · scale_factor en 16 bits (se queda la palabra baja), parte entera >> 10 más
    // scale_frequency y fracción de 10 bits interpolada entre dos notas de la tabla. Desde la nota 95 la biblioteca resta la
    // nota de abajo ya dividida entre 32 de la de arriba sin dividir: se reproduce tal cual
    static juce::uint32 scaledMilli (const GusPatch::Sample& s, juce::uint32 milli)
    {
        int n = 1;
        while (n < 127 && noteMilli (n) <= milli) ++n;
        --n;
        const auto scaled = (juce::int16) (juce::uint16) ((n - s.scaleNote) * s.scaleFactor);
        const auto frac = (juce::int64) ((juce::uint16) scaled & 0x3FF);
        auto k = (scaled >> 10) + s.scaleNote;
        if (k < 0) k = 0;
        if (k >= 128) return noteMilli (127);
        const auto t0 = (juce::int32) noteMilli (k), t1 = (juce::int32) noteMilli (k + 1);
        const auto wrap = [] (juce::int64 x) { return (juce::int32) (juce::uint32) (juce::uint64) x; };
        if (k >= 95)
        {
            const auto a = t0 >> 5;
            return (juce::uint32) (wrap ((juce::int64) (wrap ((juce::int64) (t1 - a) * frac) >> 10) + a) << 5);
        }
        return (juce::uint32) wrap ((juce::int64) (wrap ((juce::int64) (t1 - t0) * frac) >> 10) + t0);
    }

    // Frecuencia con pitch bend (_GF1_MIDI_NOTE_ON 08C2:0D8A): (f · bend + 500) / 1000, bend en milésimas
    static juce::uint32 bent (juce::uint32 milli, int bend1000)
    {
        return bend1000 == 1000 ? milli : (juce::uint32) (((juce::uint64) milli * (juce::uint32) bend1000 + 500) / 1000);
    }

    // Pitch bend a milésimas (_GF1_MIDI_PITCH_BEND 08C2:0551): (valor - 8192) · rango en 1/8192 de semitono; semitonos
    // enteros y fracción interpolada entre dos entradas de la tabla de semitonos; hacia abajo, 1 000 000 / subida. Rango 0:
    // sin bend
    static int bendTo1000 (int value14, int range)
    {
        if (range == 0) return 1000;
        auto p = (juce::int64) (value14 - 8192) * range;
        const auto down = p < 0;
        if (down) p = -p;
        const auto semi = (int) (p >> 13);
        const auto frac = p % 8192;
        const auto lo = semitone1000 (semi % 12) << (semi / 12), hi = semitone1000 ((semi + 1) % 12) << ((semi + 1) / 12);
        const auto up = (int) (((hi - lo) * frac >> 13) + lo);
        return down ? (int) (1000000 / juce::jmax (1, up)) : up;
    }

    void setVoices (int count, bool force)
    {
        if (count == activeVoices && ! force) return;
        activeVoices = count;
        chipRate = 617400.0 / count; // §1.5: 1 000 000 / (1,619695497 · voces) = 617 400 / voces
        if (force) resampler.prepare (chipRate, hostRate);
        else       resampler.setRates (chipRate, hostRate); // hilo de audio: sin reservar memoria
        // Otra fs: el driver vuelve a calcular el paso (en la tarjeta las voces se fijan al iniciar; aquí, al mover el mando)
        for (auto& v : voices)
            if (v.s != nullptr && ! v.stopped)
            {
                setPitch (v);
                computeVibrato (v, false);
            }
    }

    // Paso de la voz para su nota y el pitch bend de su canal (_GF1_HERTZ_ON y _GF1_HERTZ_BEND 16EE:1B74)
    void setPitch (Voice& v)
    {
        auto milli = bent (v.freqMilli, channels[(size_t) juce::jlimit (0, 15, v.channel)].bend1000);
        if (v.s->scaleFactor != 1024) milli = scaledMilli (*v.s, milli);
        v.fc = fcFor (*v.s, milli);
        v.inc = v.fc;
    }

    // calculate_vibrato (16EE:1075). Rate: f = (rate · 2334 + 5000) / 100 mHz (0,05-6,0 Hz); ticks del Timer 2 por cuarto de
    // ciclo = round(45 289 / (4 f)), mínimo 1 (45 289 mHz = el Timer 2 a 320 µs con 69 cuentas: _GF1_ENABLE_VIBRATO carga
    // 0xBB en el reg. 47h). Profundidad: 21 unidades por semitono; la desviación es (fc · r - fc) / 2 con r de la tabla de
    // semitonos interpolada. Sin retrigger (pitch bend de una voz que suena) se conserva la fase y el sweep
    void computeVibrato (Voice& v, bool retrigger)
    {
        const auto depth = v.s->vibDepth;
        const auto x = (juce::uint32) ((v.s->vibRate * 2334 + 5000) / 100);
        const auto d4 = x << 2;
        auto count = (45289u / d4 + ((45289u % d4) > (x << 1) ? 1u : 0u)) & 0xFFu;
        v.vibCount = (int) juce::jmax (1u, count);
        const auto semi = depth / 21;
        const auto lo = semitone1000 (semi % 12) << (semi / 12), hi = semitone1000 ((semi + 1) % 12) << ((semi + 1) / 12);
        const auto r = lo + (juce::int64) ((juce::uint32) ((hi - lo) * (depth % 21)) / 21u);
        const auto m = (juce::uint32) (r * v.fc) / 1000u;
        v.vibOffset = (int) (juce::uint16) ((m - (juce::uint32) v.fc) >> 1);
        if (retrigger) { v.vibSweepLeft = v.s->vibSweep; v.vibTicker = 0; v.vibQuarter = 0; }
    }

    // Un tick del Timer 2 (45,29 Hz) para el vibrato de cada voz que suena (_PROCESS_STATUS 2018:0EF2): triángulo de 4 cuartos
    // de vibCount ticks (0 → +d, +d → 0, 0 → -d, -d → 0, en escalera); el sweep lo hace crecer de 0 a la profundidad en
    // vibrato_sweep ticks (22,08 ms cada uno)
    void vibratoTick()
    {
        for (int i = 0; i < activeVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.s == nullptr || v.stopped || v.s->vibDepth == 0) continue;
            auto inc = (int) (juce::int16) (juce::uint16) ((juce::uint32) (v.vibOffset * v.vibTicker) / (juce::uint32) v.vibCount);
            switch (v.vibQuarter)
            {
                case 1:  inc = v.vibOffset - inc; break;
                case 2:  inc = -inc; break;
                case 3:  inc = -(v.vibOffset - inc); break;
                default: break;
            }
            if (v.vibSweepLeft != 0 && v.s->vibSweep != 0)
            {
                const auto left = v.vibSweepLeft--;
                inc = inc * (v.s->vibSweep - left) / v.s->vibSweep;
            }
            v.inc = (v.fc + inc) & 0x7FFF;
            if (++v.vibTicker >= v.vibCount) { v.vibQuarter = (v.vibQuarter + 1) % 4; v.vibTicker = 0; }
        }
    }

    // calculate_tremolo (16EE:12C1): una rampa de volumen en bucle bidireccional alrededor del nivel de sostenido
    // (offset 2 · atenuación / 255), de amplitud (depth + 1) >> 3 unidades de 8 bits (acotada a 8-246); su velocidad sale de
    // la del LFO (la misma fórmula que el vibrato) y del número de voces, porque la rampa avanza con cada actualización. Solo
    // suena mientras la nota está sostenida (punto 3 con sustain). El tremolo_sweep del patch no se usa
    void computeTremolo (Voice& v)
    {
        const auto amp = (v.s->tremDepth + 1) >> 3, half = amp >> 1, odd = amp & 1;
        auto level = (juce::int64) ((juce::uint32) (v.att * v.s->envOffset[2]) / 255u);
        if (level - half >= 0 && level - half < 8) level = half + 8;
        if (level + half + odd > 0xF6) level = 0xF6 - half - odd;
        v.tremStart = (int) (level - half);
        v.tremEnd = (int) (level + half + odd);
        auto a = (juce::uint32) ((juce::uint16) ((v.tremEnd - v.tremStart) << 4)) * (juce::uint32) (activeVoices << 4);
        const auto q = (juce::uint32) (5000000 / (v.s->tremRate * 2334 + 5000)) * 100000u;
        int i = 0;
        for (; i <= 3; ++i)
        {
            if (a > q) break;
            a *= 8u;
        }
        i = juce::jmin (i, 3);
        // round(A / Q); la biblioteca acota a 63 la palabra baja del cociente (16EE:14B2)
        const auto quotient = (juce::uint16) ((a + (q >> 1)) / juce::jmax ((juce::uint32) 1, q));
        v.tremRate = juce::jmin ((int) quotient, 63) | (i << 6);
    }

    // Programa una rampa (regs. 6-8 y 0D): hacia abajo hasta el límite inferior, hacia arriba hasta el superior
    static void rampTo (Voice& v, int target8, int rate, bool down)
    {
        if (down) v.rampLow = target8 << 8;
        else      v.rampHigh = target8 << 8;
        v.rampDown = down;
        v.rampLoop = false;
        v.rampStep = juce::jmax (1, rate & 0x3F) << 4; // incremento 0: el SDK da 1-63; aquí, 1 (estimación)
        v.rampEvery = 1 << (3 * ((rate >> 6) & 3));    // 1, 8, 64, 512 actualizaciones (§1.6)
        v.rampCount = 0;
        v.ramping = true;
    }

    // Note on de la biblioteca (_GF1_HERTZ_ON 16EE:1FF0): punto 0 de la envolvente, rampa hacia arriba desde 8
    void startEnvelope (Voice& v)
    {
        v.stage = 0;
        v.pendingStage = -1;
        v.rampIrq = true;
        if (! v.s->envelope())
        {
            // Sin envolvente la biblioteca no escribe el volumen (queda el anterior): estimación, el nivel de la atenuación
            v.vol = v.att << 8;
            v.ramping = false;
            return;
        }
        v.rampLow = 8 << 8;
        rampTo (v, envTarget (v, 0), v.s->envRate[0], false);
    }

    // Fin de una rampa (IRQ de volumen; _PROCESS_STATUS 2018:07F3): siguiente punto, o el pendiente tras un cambio de volumen.
    // En el 3, con sustain y sin soltar, se queda (y arranca el trémolo); desde el 6, el bucle de la muestra se apaga y la voz
    // acaba al final de sus datos. La dirección sale de comparar el offset anterior con el nuevo (para el punto 0 la
    // biblioteca lee el rate 5: se conserva)
    void rampDone (Voice& v)
    {
        if (! v.s->envelope()) return;
        if (v.pendingStage >= 0) { v.stage = v.pendingStage; v.pendingStage = -1; }
        else                     ++v.stage;
        if (v.stage >= 6) { v.loopOff = true; return; }
        if (v.stage == 3 && v.s->sustain() && ! v.released)
        {
            if (v.s->tremDepth != 0)
            {
                v.rampLow = (v.tremStart & 0xFF) << 8;
                v.rampHigh = (v.tremEnd & 0xFF) << 8;
                // La biblioteca compara TREM_START con los 8 bits bajos del volumen de 12 bits (2018:0956): se conserva
                v.rampDown = (juce::uint16) v.tremStart < ((v.vol >> 4) & 0xFF);
                v.rampStep = juce::jmax (1, v.tremRate & 0x3F) << 4;
                v.rampEvery = 1 << (3 * ((v.tremRate >> 6) & 3));
                v.rampCount = 0;
                v.rampLoop = true;
                v.ramping = true;
            }
            return;
        }
        const auto stage = (size_t) v.stage;
        const auto previous = v.stage > 0 ? v.s->envOffset[stage - 1] : v.s->envRate[5];
        rampTo (v, envTarget (v, v.stage), v.s->envRate[stage], previous > v.s->envOffset[stage]);
    }

    // Note off de la biblioteca (_GF1_HERTZ_OFF 16EE:213F). Sin envolvente no hace nada: la muestra sigue (fiel). Con el bit 7 de
    // modes, salto al punto 5 bajando a 8. Con sustain, al punto 3 (si no había llegado) con el rate 3 hacia el offset 3. Sin
    // sustain, la envolvente sigue sola
    void release (Voice& v)
    {
        if (v.stopped || v.released) return;
        v.released = true;
        v.pedalHeld = false;
        v.priority += 20;
        if (! v.s->envelope()) return;
        v.rampIrq = true;
        if (v.s->fastRelease())
        {
            v.stage = 5;
            rampTo (v, 8, v.s->envRate[5], true);
            return;
        }
        if (! v.s->sustain()) return;
        if (v.stage < 3) v.stage = 3;
        const auto current8 = v.vol >> 8, target = v.s->envOffset[3] * v.att / 255;
        if (v.s->envOffset[3] > current8) rampTo (v, target, v.s->envRate[3], false);
        else                              rampTo (v, juce::jmax (8, target), v.s->envRate[3], true);
    }

    // _GF1_CHANGE_VOLUME (16EE:14FD): nueva atenuación y rampa al nivel nuevo con rate 0x44 (el actual escalado, o con trémolo
    // el offset del punto anterior), acotado a 0x80-0xF60; al acabar, la envolvente retoma su punto. La IRQ de esa rampa solo se
    // activa con sustain: sin él, un cambio de volumen congela la envolvente (fiel)
    void changeVolume (Voice& v, int chanVolume)
    {
        const auto old = v.att;
        v.chanVolume = juce::jlimit (1, 127, chanVolume);
        v.att = juce::jmax (1, attenuation (v.velocity, v.chanVolume));
        if (v.stopped) return;
        const auto current12 = v.vol >> 4;
        int target12;
        if (v.s->tremDepth != 0)
        {
            computeTremolo (v);
            target12 = (v.s->envOffset[(size_t) juce::jmax (0, v.stage - 1)] * v.att / 255) << 4;
        }
        else
            target12 = current12 * v.att / juce::jmax (1, old);
        target12 = juce::jlimit (0x80, 0xF60, target12);
        rampTo (v, target12 >> 4, 0x44, target12 < current12);
        v.pendingStage = v.stage;
        v.rampIrq = v.s->sustain();
    }

    //==========================================================================
    // Patch residente en la DRAM: el driver carga los que caben en la RAM elegida; si uno no cabe, suena otro ya cargado de la
    // misma familia GM (8 programas) o, si no hay, el primero cargado. ULTRAMID sustituía según ULTRAMID.INI: estimación
    const GusPatch* residentPatch (const GusPatch* p, int family)
    {
        if (p == nullptr) return nullptr;
        for (const auto& r : resident) if (r.patch == p) return p;
        if (residentBytes + p->bytes <= ramBytes)
        {
            resident.push_back ({ p, family });
            residentBytes += p->bytes;
            return p;
        }
        for (const auto& r : resident) if (r.family == family) return r.patch;
        return resident.empty() ? nullptr : resident.front().patch;
    }

    // Capa MIDI de la biblioteca (_GF1_MIDI_PARAMETER 08C2:07C1 y vecinas). Valores iniciales (_GF1_MIDI_INIT_HARDWARE): volumen
    // y expresión 127, pan 16 (= el balance del patch), rango de bend 2, RPN 0/0
    void handle (const GusBank* bank, const juce::MidiMessage& m, int ch)
    {
        auto& c = channels[(size_t) ch];
        if (m.isNoteOn())
        {
            if (bank != nullptr) noteOn (*bank, ch, m.getNoteNumber(), m.getVelocity());
        }
        else if (m.isNoteOff())
            noteOff (ch, m.getNoteNumber());
        else if (m.isProgramChange())
            c.program = m.getProgramChangeNumber();
        else if (m.isPitchWheel())
        {
            c.bend1000 = bendTo1000 (m.getPitchWheelValue(), c.bendRange);
            for (auto& v : voices)
                if (v.channel == ch && ! v.stopped) { setPitch (v); computeVibrato (v, false); }
        }
        else if (m.isController())
        {
            const auto cc = m.getControllerNumber(), val = m.getControllerValue();
            switch (cc)
            {
                case 7: case 11:
                    (cc == 7 ? c.volume : c.expression) = val;
                    for (auto& v : voices)
                        if (v.channel == ch && ! v.released && ! v.stopped) changeVolume (v, c.volume * c.expression / 127);
                    break;
                case 10: // 4 bits; se aplica también a las notas ya soltadas
                    c.pan = val >> 3;
                    for (auto& v : voices) if (v.channel == ch && ! v.stopped) v.pan = c.pan;
                    break;
                case 64:
                    c.sustain = val >= 64;
                    if (! c.sustain)
                        for (auto& v : voices) if (v.channel == ch && v.pedalHeld) release (v);
                    break;
                case 100: c.rpnLsb = val; break;
                case 101: c.rpnMsb = val; break;
                case 6:   if (c.rpnMsb == 0 && c.rpnLsb == 0) c.bendRange = val; break; // sin acotar, como la biblioteca
                case 121: c.bend1000 = 1000; c.expression = 127; c.pan = 16; break; // no toca volumen, pedal ni RPN
                case 123: case 124: case 125: case 126: case 127: // note off de todas las notas de todos los canales
                    for (auto& v : voices)
                        if (v.channel >= 0 && ! v.stopped) noteOffVoice (v);
                    break;
                default: break; // CC 1 (modulación), 38, 43, 96, 97 y el resto: sin efecto en la biblioteca
            }
        }
    }

    // _GF1_MIDI_NOTE_ON (08C2:0EED y 08C2:0C0D): frecuencia de la tabla de notas con el bend del canal, voz por turno rotatorio
    // (o robada), velocidad y volumen del canal (volumen · expresión / 127), pan del canal si no es 16
    void noteOn (const GusBank& bank, int ch, int note, int velocity)
    {
        const auto drum = ch == 9;
        const auto& c = channels[(size_t) ch];
        const auto* patch = residentPatch (drum ? bank.drums[(size_t) note] : bank.melodic[(size_t) c.program], drum ? 16 : c.program / 8);
        if (patch == nullptr || patch->samples.empty()) return;
        const auto playNote = drum && bank.drumNote[(size_t) note] >= 0 ? bank.drumNote[(size_t) note] : note;
        // La muestra se elige con la frecuencia ya escalada por la de la ranura actual (_GF1_HERTZ_ON 16EE:1CAD). La ranura
        // actual es la última asignada con _GF1_ASSIGN_WAVEFORM, y eso lo decide el driver: estimación, las muestras se
        // asignan en orden y queda la última
        const auto current = juce::jmin (16, (int) patch->samples.size()) - 1;
        auto milli = bent (noteMilli (playNote), c.bend1000);
        if (patch->samples[(size_t) current].scaleFactor != 1024) milli = scaledMilli (patch->samples[(size_t) current], milli);
        const auto chosen = patch->sampleSlot (milli, current);
        if (chosen < 0) return;
        const auto* s = &patch->samples[(size_t) chosen];
        auto& v = allocate();
        const auto vol = v.s != nullptr && ! v.stopped ? 8 << 8 : 0; // voz robada: _GF1_STOP_HERTZ la baja a 8 antes (instantáneo aquí)
        v = Voice {};
        v.s = s; v.patch = patch; v.channel = ch; v.note = note; v.stopped = false; v.age = ++clock;
        v.priority = 10;
        v.freqMilli = noteMilli (playNote);
        v.velocity = juce::jlimit (1, 127, velocity);
        v.chanVolume = juce::jlimit (1, 127, c.volume * c.expression / 127);
        v.att = juce::jmax (1, attenuation (v.velocity, v.chanVolume));
        v.vol = vol;
        setPitch (v);
        v.pan = c.pan != 16 ? juce::jlimit (0, 15, c.pan) : s->balance;
        startEnvelope (v);
        computeVibrato (v, true);
        if (s->tremDepth != 0) computeTremolo (v);
    }

    // _GF1_MIDI_NOTE_OFF (08C2:0F4E): suelta la primera voz del canal con esa nota aún no soltada; con el pedal pisado solo la
    // marca (+5 de prioridad) y la suelta al levantarlo
    void noteOff (int ch, int note)
    {
        for (int i = 0; i < activeVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.channel == ch && v.note == note && ! v.released && ! v.pedalHeld && ! v.stopped)
            {
                noteOffVoice (v);
                return;
            }
        }
    }

    void noteOffVoice (Voice& v)
    {
        if (v.released || v.pedalHeld) return;
        if (channels[(size_t) v.channel].sustain) { v.pedalHeld = true; v.priority += 5; }
        else release (v);
    }

    static void silence (Voice& v) { v.stopped = true; v.ramping = false; v.vol = 0; v.channel = -1; v.note = -1; }

    // Reparto de voces (08C2:0C0D): turno rotatorio desde la siguiente a la última asignada; una libre si la hay; si no, la de
    // mayor prioridad (soltada 30 > sostenida por el pedal 15 > sonando 10) y, a igualdad, la más antigua
    Voice& allocate()
    {
        Voice* best = nullptr;
        for (int k = 0; k < activeVoices; ++k)
        {
            const auto i = (nextVoice + k) % activeVoices;
            auto& v = voices[(size_t) i];
            if (v.s == nullptr || v.stopped) { nextVoice = (i + 1) % activeVoices; return v; }
            if (best == nullptr || v.priority > best->priority || (v.priority == best->priority && v.age < best->age)) best = &v;
        }
        nextVoice = (int) ((best - voices.data()) + 1) % activeVoices;
        return *best;
    }

    //==========================================================================
    // Una actualización del GF1: todas las voces activas, sumadas (también las paradas: su dato actual por su volumen)
    std::array<float, 2> tick()
    {
        // Timer 2 del GF1 (vibrato): 69 cuentas de 320 µs = 22,08 ms
        timerElapsed += 1.0 / chipRate;
        if (timerElapsed >= 69 * 320.0e-6) { timerElapsed -= 69 * 320.0e-6; vibratoTick(); }
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < activeVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.s == nullptr) continue;
            const auto& data = v.s->data;
            const auto last = (juce::int64) data.size() - 1;
            const auto idx = juce::jlimit ((juce::int64) 0, last, v.pos >> 9);
            const auto frac = (int) (v.pos & 511);
            const auto a = (int) data[(size_t) idx], b = (int) data[(size_t) juce::jmin (last, idx + 1)];
            const auto sample = (float) (a + ((b - a) * frac) / 512) * (1.0f / 32768.0f); // interpolación lineal a 16 bits
            const auto amp = sample * amplitudeOf (v.vol >> 4);
            l += amp * panGain[(size_t) v.pan][0];
            r += amp * panGain[(size_t) v.pan][1];
            if (v.ramping && ++v.rampCount >= v.rampEvery)
            {
                v.rampCount = 0;
                bool atBound;
                if (v.rampDown) { const auto before = v.vol; v.vol -= v.rampStep; atBound = v.vol <= v.rampLow; if (atBound) v.vol = juce::jmin (before, v.rampLow); }
                else            { const auto before = v.vol; v.vol += v.rampStep; atBound = v.vol >= v.rampHigh; if (atBound) v.vol = juce::jmax (before, v.rampHigh); }
                if (atBound)
                {
                    if (v.rampLoop) v.rampDown = ! v.rampDown; // trémolo: bucle bidireccional
                    else
                    {
                        v.ramping = false;
                        if (v.rampIrq && ! v.stopped) rampDone (v);
                    }
                }
            }
            if (v.stopped) continue;
            // Avance y bucles (reg. 0): hacia delante, bidireccional; sin bucle (o apagado al acabar la envolvente), para al final
            v.pos += v.backwards ? -v.inc : v.inc;
            const auto looping = v.s->looping() && ! v.loopOff;
            const auto end = looping ? v.s->loopEnd : last * 512;
            if (! v.backwards && v.pos >= end)
            {
                if (! looping) { v.pos = end; v.stopped = true; }
                else if (v.s->bidir()) { v.pos = 2 * end - v.pos; v.backwards = true; }
                else v.pos -= juce::jmax ((juce::int64) 512, v.s->loopEnd - v.s->loopStart);
            }
            else if (v.backwards && (! looping || v.pos <= v.s->loopStart))
            {
                if (! looping) { v.backwards = false; } // bucle apagado: sigue hacia delante hasta el final
                else { v.pos = 2 * v.s->loopStart - v.pos; v.backwards = false; }
            }
        }
        // Sumador de 16 bits: satura (estimación)
        return { juce::jlimit (-1.0f, 1.0f, l), juce::jlimit (-1.0f, 1.0f, r) };
    }

    void renderTo (juce::AudioBuffer<float>& out, int from, int to)
    {
        auto* L = out.getWritePointer (0);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        for (int i = from; i < to; ++i)
        {
            auto y = resampler.nextStereo ([this] { return tick(); });
            // Etapa de salida: paso bajo de primer orden a 8 kHz (estimación: el ajuste de DOSBox Staging a la Classic)
            for (int c = 0; c < 2; ++c) { lpState[(size_t) c] += outLpf * (y[(size_t) c] - lpState[(size_t) c]); y[(size_t) c] = lpState[(size_t) c]; }
            if (R != nullptr) { L[i] += y[0]; R[i] += y[1]; }
            else L[i] += 0.5f * (y[0] + y[1]);
        }
    }

    // Paneo de 16 posiciones con potencia constante (estimación: el SDK no da la ley; DOSBox Staging la usa)
    static inline const std::array<std::array<float, 2>, 16> panGain = []
    {
        std::array<std::array<float, 2>, 16> g {};
        for (int k = 0; k < 16; ++k)
        {
            const auto a = k / 15.0 * juce::MathConstants<double>::halfPi;
            g[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }
        return g;
    }();

    struct Resident { const GusPatch* patch; int family; };
    std::array<Voice, maxVoices> voices {};
    std::array<Channel, 16> channels {};
    std::vector<Resident> resident = [] { std::vector<Resident> r; r.reserve (512); return r; }();
    juce::int64 residentBytes = 0, ramBytes = 1024 * 1024;
    const GusBank* lastBank = nullptr;
    ZohResampler resampler;
    double hostRate = 48000.0, chipRate = 44100.0;
    int activeVoices = 24, nextVoice = 0;
    double timerElapsed = 0.0; // tiempo del Timer 2 desde su último tick (s)
    juce::uint64 clock = 0;
    float outLpf = 0.65f;
    std::array<float, 2> lpState { 0.0f, 0.0f };
};

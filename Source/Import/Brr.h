// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// BRR (Bit Rate Reduction) del S-DSP de la SNES (fullsnes "BRR Samples"): bloques de 9 bytes = cabecera (shift 4 bits,
// filtro 2 bits, loop, end) + 16 nibbles con signo. Decodificación exacta del chip, compartida por el motor SNES y por la
// importación de ficheros .brr y .spc.
namespace Brr
{
    // Una muestra: nibble con signo, shift 0-12 (13-15: solo el signo, como el chip), filtro 0-3 sobre las dos anteriores
    inline int decodeSample (int nib, int shift, int filter, int old, int older)
    {
        auto s = shift <= 12 ? (nib * (1 << shift)) >> 1 : ((nib >> 3) * (1 << 12)) >> 1;
        switch (filter)
        {
            case 1: s += old + ((-old) >> 4); break;
            case 2: s += 2 * old + ((-3 * old) >> 5) - older + (older >> 4); break;
            case 3: s += 2 * old + ((-13 * old) >> 6) - older + ((3 * older) >> 4); break;
            default: break;
        }
        s = juce::jlimit (-32768, 32767, s);
        return (int) (int16_t) (uint16_t) (s * 2) >> 1; // recorte a 15 bits: +4000h..+7FFFh pasa a negativo, como el chip
    }

    // Número de bloques de una cadena BRR que empieza en 'data' (hasta el bloque con End, incluido). 0 si no termina dentro de
    // 'size' bytes o si algún bloque usa shift > 12 (los codificadores no lo generan: indica que no es un sample).
    inline int chainLength (const uint8_t* data, size_t size, bool strict)
    {
        for (size_t b = 0; (b + 1) * 9 <= size; ++b)
        {
            const auto header = data[b * 9];
            if (strict && (header >> 4) > 12)
                return 0;
            if (header & 1)
                return (int) b + 1;
        }
        return 0;
    }

    // Decodifica 'numBlocks' bloques a PCM de 16 bits (el S-DSP trabaja con 15 bits × 2). Sin bucle: estado inicial a cero.
    inline std::vector<int> decode (const uint8_t* data, int numBlocks)
    {
        std::vector<int> pcm ((size_t) numBlocks * 16);
        int old = 0, older = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            const auto* block = data + (size_t) b * 9;
            for (int k = 0; k < 16; ++k)
            {
                const auto byte = block[1 + k / 2];
                const auto nib = (int) (int8_t) (uint8_t) ((k & 1) ? (byte << 4) : (byte & 0xf0)) >> 4;
                const auto s = decodeSample (nib, block[0] >> 4, (block[0] >> 2) & 3, old, older);
                older = old;
                old = s;
                pcm[(size_t) b * 16 + (size_t) k] = s * 2;
            }
        }
        return pcm;
    }
}

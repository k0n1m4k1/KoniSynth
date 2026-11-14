// HLE del núcleo de operadores del firmware de fábrica del FM-1 (ver Fm1Hle.h). Las operaciones de los kernels son las de
// EngineMkI.cpp de Synth_Dexed (codeberg.org/dcoredump/Synth_Dexed, e3f524c; msfa Apache-2.0 con cambios de Dexed GPL-3.0),
// verificadas contra el firmware en User/FM1/SOUND-ENGINE.md; la disposición de memoria es la del firmware.
#include "Fm1Hle.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fm1
{
namespace
{
constexpr int kN = 64;
constexpr int32_t kEnvMax = 1 << 14;            // ENV_MAX de EngineMkI
constexpr int32_t kLevelThresh = kEnvMax - 100; // 16284, el umbral del firmware (0x02085ABE)
constexpr uint16_t kNegative = 0x8000;

// Tablas de EngineMkI con sus mismas expresiones (iguales a las del firmware en la RAM de la V13: 0 de 1024 distintas)
struct MkiTables
{
    uint16_t sinLog[1024], sinExp[1024];
    MkiTables()
    {
        float bitReso = 1024;
        for (int32_t i = 0; i < 1024; i++)
        {
            float x1 = (float) std::sin (((0.5 + i) / bitReso) * 3.14159265358979323846 / 2.0);
            sinLog[i] = (uint16_t) std::round (-1024 * std::log2 (x1));
        }
        for (int32_t i = 0; i < 1024; i++)
        {
            float x1 = (float) ((std::pow (2, float (i) / bitReso) - 1) * 4096);
            sinExp[i] = (uint16_t) std::round (x1);
        }
    }
};
const MkiTables& tables() { static const MkiTables t; return t; }

inline uint16_t sinLog (const MkiTables& t, uint16_t phi) noexcept
{
    const uint16_t index = phi & 1023;
    switch (phi & 3072)
    {
        case 0:    return t.sinLog[index];
        case 1024: return t.sinLog[index ^ 1023];
        case 2048: return (uint16_t) (t.sinLog[index] | kNegative);
        default:   return (uint16_t) (t.sinLog[index ^ 1023] | kNegative);
    }
}

inline int32_t mkiSin (const MkiTables& t, int32_t phase, uint16_t env) noexcept
{
    uint16_t expVal = (uint16_t) (sinLog (t, (uint16_t) (phase >> 12)) + env);
    const bool isSigned = (expVal & kNegative) != 0;
    expVal &= (uint16_t) ~kNegative;
    uint16_t result = (uint16_t) (4096 + t.sinExp[(expVal & 0x3FF) ^ 0x3FF]);
    result = (uint16_t) (result >> (expVal >> 10));
    return isSigned ? (int32_t) ((uint32_t) (-(int32_t) result - 1) << 13) : (int32_t) ((uint32_t) result << 13);
}

// Estado de un operador como lo usan los kernels; gainOut/level son +0/+4 del firmware
struct Op { int32_t gainOut, level, freq, phase; };

void compute (const MkiTables& t, int32_t* out, const int32_t* in, int32_t phase0, int32_t freq, int32_t g1, int32_t g2, bool add)
{
    const int32_t dgain = (g2 - g1 + (kN >> 1)) >> 6;
    int32_t gain = g1, phase = phase0;
    for (int i = 0; i < kN; i++)
    {
        gain += dgain;
        const int32_t y = mkiSin (t, phase + in[i], (uint16_t) gain);
        out[i] = y + (add ? out[i] : 0);
        phase += freq;
    }
}

void computePure (const MkiTables& t, int32_t* out, int32_t phase0, int32_t freq, int32_t g1, int32_t g2, bool add)
{
    const int32_t dgain = (g2 - g1 + (kN >> 1)) >> 6;
    int32_t gain = g1, phase = phase0;
    for (int i = 0; i < kN; i++)
    {
        gain += dgain;
        const int32_t y = mkiSin (t, phase, (uint16_t) gain);
        out[i] = y + (add ? out[i] : 0);
        phase += freq;
    }
}

void computeFb (const MkiTables& t, int32_t* out, int32_t phase0, int32_t freq, int32_t g1, int32_t g2, int32_t* fb, int32_t shift, bool add)
{
    const int32_t dgain = (g2 - g1 + (kN >> 1)) >> 6;
    int32_t gain = g1, phase = phase0, y0 = fb[0], y = fb[1];
    for (int i = 0; i < kN; i++)
    {
        gain += dgain;
        const int32_t scaled = (y0 + y) >> (shift + 1);
        y0 = y;
        y = mkiSin (t, phase + scaled, (uint16_t) gain);
        out[i] = y + (add ? out[i] : 0);
        phase += freq;
    }
    fb[0] = y0;
    fb[1] = y;
}

// Ganancia inicial y paso por muestra de un operador interior de las cadenas (escribe la ganancia nueva en +4).
// V13 (readsGain): lee +0 (una ganancia) como nivel y, como compute_fb3 de Synth_Dexed, la deja fija en el bloque: mudo.
// V15: ganancia anterior (+0) → nueva (del nivel en +4) con la rampa de los demás operadores
inline void chainRamp (Op& o, bool readsGain, int32_t& gain, int32_t& dg) noexcept
{
    if (readsGain)
    {
        o.level = kEnvMax - (o.gainOut >> 14);
        gain = o.level == 0 ? kEnvMax - 1 : o.level;
        dg = o.level - gain;
        return;
    }
    const int32_t g1 = o.gainOut == 0 ? kEnvMax - 1 : o.gainOut;
    const int32_t g2 = kEnvMax - (o.level >> 14);
    o.level = g2;
    gain = g1;
    dg = (g2 - g1 + (kN >> 1)) >> 6;
}

// compute_fb2 (algoritmo 6 con realimentación): operadores op y op + 1
void computeFb2 (const MkiTables& t, int32_t* out, Op* p, int32_t g01, int32_t g02, int32_t* fb, int32_t shift, bool readsGain)
{
    int32_t y0 = fb[0], y = fb[1];
    int32_t ph0 = p[0].phase, ph1 = p[1].phase;
    int32_t gain0 = g01, gain1, dg1;
    chainRamp (p[1], readsGain, gain1, dg1);
    const int32_t dg0 = (g02 - g01 + (kN >> 1)) >> 6;
    for (int i = 0; i < kN; i++)
    {
        const int32_t scaled = (y0 + y) >> (shift + 1);
        gain0 += dg0;
        y0 = y;
        y = mkiSin (t, ph0 + scaled, (uint16_t) gain0);
        ph0 += p[0].freq;
        gain1 += dg1;
        y = mkiSin (t, ph1 + y, (uint16_t) gain1);
        ph1 += p[1].freq;
        out[i] = y;
    }
    fb[0] = y0;
    fb[1] = y;
}

// compute_fb3 (algoritmo 4 con realimentación): operadores op, op + 1 y op + 2
void computeFb3 (const MkiTables& t, int32_t* out, Op* p, int32_t g01, int32_t g02, int32_t* fb, int32_t shift, bool readsGain)
{
    int32_t y0 = fb[0], y = fb[1];
    int32_t ph0 = p[0].phase, ph1 = p[1].phase, ph2 = p[2].phase;
    int32_t gain0 = g01, gain1, dg1, gain2, dg2;
    chainRamp (p[1], readsGain, gain1, dg1);
    chainRamp (p[2], readsGain, gain2, dg2);
    const int32_t dg0 = (g02 - g01 + (kN >> 1)) >> 6;
    for (int i = 0; i < kN; i++)
    {
        const int32_t scaled = (y0 + y) >> (shift + 1);
        gain0 += dg0;
        y0 = y;
        y = mkiSin (t, ph0 + scaled, (uint16_t) gain0);
        ph0 += p[0].freq;
        gain1 += dg1;
        y = mkiSin (t, ph1 + y, (uint16_t) gain1);
        ph1 += p[1].freq;
        gain2 += dg2;
        y = mkiSin (t, ph2 + y, (uint16_t) gain2);
        ph2 += p[2].freq;
        out[i] = y;
    }
    fb[0] = y0;
    fb[1] = y;
}

inline uint32_t rd32 (const uint8_t* p) noexcept { uint32_t v; std::memcpy (&v, p, 4); return v; }
inline void wr32 (uint8_t* p, uint32_t v) noexcept { std::memcpy (p, &v, 4); }
} // namespace

HleRenderSite findFmCoreRender (const uint8_t* ram, uint32_t ramBase, size_t ramSize) noexcept
{
    // prólogo: [--sp] = {rets, r15-r4}; sp += -68; r5 = r2 # [sp+20] = r3 (igual en V13 y V15)
    static const uint8_t sig[] = { 0x7f, 0x04, 0xe2, 0x8f, 0x25, 0xd6, 0x83, 0x25 };
    static const uint8_t alg1[] = { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }; // algoritmo 1 de FmCore::algorithms
    const size_t limit = std::min<size_t> (ramSize, 0x10000);
    for (size_t i = 0; i + 0x64 <= limit; i += 2)
    {
        if (std::memcmp (ram + i, sig, sizeof sig) != 0)
            continue;
        // la constante de la tabla: "r1 = imm32" (c1 ff xx xx xx xx) en +0x5C
        const uint8_t* c = ram + i + 0x5c;
        if (c[0] != 0xc1 || c[1] != 0xff)
            continue;
        const uint32_t table = rd32 (c + 2);
        if (table < ramBase || table - ramBase + 32 * 6 > ramSize || std::memcmp (ram + (table - ramBase), alg1, sizeof alg1) != 0)
            continue;
        HleRenderSite s { ramBase + (uint32_t) i, table };
        // el kernel de la cadena: el prólogo 7f 04 e2 9a más cercano antes de la rutina (V13 y V15: 0x204 bytes antes)
        for (size_t k = i >= 2 ? i - 2 : 0; k + 2 > 0 && i - k < 0x800; k -= 2)
        {
            if (ram[k] == 0x7f && ram[k + 1] == 0x04 && ram[k + 2] == 0xe2 && ram[k + 3] == 0x9a)
            {
                for (size_t j = k; j < k + 32; j += 2)
                    if (ram[j] == 0x14 && ram[j + 1] == 0x64)  // r4 = [r1+16]
                        s.chainReadsGain = true;
                break;
            }
            if (k == 0)
                break;
        }
        return s;
    }
    return {};
}

bool hleFmCoreRender (uint8_t* ram, uint32_t ramBase, size_t ramSize, const HleRenderArgs& a) noexcept
{
    const auto inRam = [&] (uint32_t addr, size_t len) { return addr >= ramBase && (size_t) (addr - ramBase) + len <= ramSize; };
    if (a.n != kN || a.algo > 31 || ! inRam (a.out, kN * 4) || ! inRam (a.params, 6 * 16) || ! inRam (a.fb, 8)
        || ! inRam (a.algTable, 32 * 6))
        return false;
    const MkiTables& t = tables();
    uint8_t* pp = ram + (a.params - ramBase);
    Op p[6];
    for (int op = 0; op < 6; op++)
        p[op] = { (int32_t) rd32 (pp + 16 * op), (int32_t) rd32 (pp + 16 * op + 4), (int32_t) rd32 (pp + 16 * op + 8),
                  (int32_t) rd32 (pp + 16 * op + 12) };
    int32_t fb[2] = { (int32_t) rd32 (ram + (a.fb - ramBase)), (int32_t) rd32 (ram + (a.fb - ramBase) + 4) };
    const int32_t fbShift = (int32_t) a.fbShift;
    const bool fbOn = fbShift < 16;
    uint8_t* alg = ram + (a.algTable - ramBase) + 6 * a.algo;
    if ((a.algo == 3 || a.algo == 5) && fbOn)
        alg[0] = 0xc4; // en la tabla global, como el firmware
    int32_t out[kN] = {}, bus[2][kN] = {};
    int32_t* const outPtrs[3] = { out, bus[0], bus[1] };
    bool hasContents[3] = { true, false, false };
    for (int op = 0; op < 6; op++)
    {
        const int32_t flags = alg[op];
        bool add = (flags & 4) != 0;
        Op& param = p[op];
        const int32_t inbus = (flags >> 4) & 3, outbus = flags & 3;
        int32_t* outptr = outPtrs[outbus];
        const int32_t gain1 = param.gainOut == 0 ? kEnvMax - 1 : param.gainOut;
        const int32_t gain2 = kEnvMax - (param.level >> 14);
        param.level = gain2;
        if (gain1 <= kLevelThresh || gain2 <= kLevelThresh)
        {
            if (! hasContents[outbus])
                add = false;
            if (inbus == 0 || ! hasContents[inbus])
            {
                if ((flags & 0xc0) == 0xc0 && fbOn)
                {
                    switch (a.algo)
                    {
                        case 3:
                            computeFb3 (t, outptr, &p[op], gain1, gain2, fb, std::min (fbShift + 2, 16), a.chainReadsGain);
                            p[op + 1].phase += p[op + 1].freq << 6;
                            p[op + 2].phase += p[op + 2].freq << 6;
                            op += 2;
                            break;
                        case 5:
                            computeFb2 (t, outptr, &p[op], gain1, gain2, fb, std::min (fbShift + 2, 16), a.chainReadsGain);
                            p[op + 1].phase += p[op + 1].freq << 6;
                            op += 1;
                            break;
                        case 31:
                            computeFb (t, outptr, param.phase, param.freq, gain1, gain2, fb, std::min (fbShift + 2, 16), add);
                            break;
                        default:
                            computeFb (t, outptr, param.phase, param.freq, gain1, gain2, fb, fbShift, add);
                            break;
                    }
                }
                else
                    computePure (t, outptr, param.phase, param.freq, gain1, gain2, add);
            }
            else
                compute (t, outptr, outPtrs[inbus], param.phase, param.freq, gain1, gain2, add);
            hasContents[outbus] = true;
        }
        else if (! add)
            hasContents[outbus] = false;
        param.phase += param.freq << 6;
    }
    uint8_t* po = ram + (a.out - ramBase);
    for (int i = 0; i < kN; i++)
        wr32 (po + 4 * i, (uint32_t) out[i]);
    for (int op = 0; op < 6; op++)
    {
        wr32 (pp + 16 * op + 4, (uint32_t) p[op].level);
        wr32 (pp + 16 * op + 12, (uint32_t) p[op].phase);
    }
    wr32 (ram + (a.fb - ramBase), (uint32_t) fb[0]);
    wr32 (ram + (a.fb - ramBase) + 4, (uint32_t) fb[1]);
    return true;
}
} // namespace fm1

// HLE del núcleo de operadores del firmware de fábrica del FM-1 (ver Fm1Hle.h). Las operaciones de los kernels son las de
// EngineMkI.cpp de Synth_Dexed (codeberg.org/dcoredump/Synth_Dexed, e3f524c; msfa Apache-2.0 con cambios de Dexed GPL-3.0),
// verificadas contra el firmware en User/FM1/SOUND-ENGINE.md; la disposición de memoria es la del firmware.
#include "Fm1Hle.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>

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

// Ganancia inicial y paso por muestra de un operador interior de las cadenas (escribe la ganancia nueva en +4): de la
// ganancia anterior (+0) a la nueva (del nivel en +4) con la rampa de los demás operadores (V15)
inline void chainRamp (Op& o, int32_t& gain, int32_t& dg) noexcept
{
    const int32_t g1 = o.gainOut == 0 ? kEnvMax - 1 : o.gainOut;
    const int32_t g2 = kEnvMax - (o.level >> 14);
    o.level = g2;
    gain = g1;
    dg = (g2 - g1 + (kN >> 1)) >> 6;
}

// compute_fb2 (algoritmo 6 con realimentación): operadores op y op + 1
void computeFb2 (const MkiTables& t, int32_t* out, Op* p, int32_t g01, int32_t g02, int32_t* fb, int32_t shift)
{
    int32_t y0 = fb[0], y = fb[1];
    int32_t ph0 = p[0].phase, ph1 = p[1].phase;
    int32_t gain0 = g01, gain1, dg1;
    chainRamp (p[1], gain1, dg1);
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
void computeFb3 (const MkiTables& t, int32_t* out, Op* p, int32_t g01, int32_t g02, int32_t* fb, int32_t shift)
{
    int32_t y0 = fb[0], y = fb[1];
    int32_t ph0 = p[0].phase, ph1 = p[1].phase, ph2 = p[2].phase;
    int32_t gain0 = g01, gain1, dg1, gain2, dg2;
    chainRamp (p[1], gain1, dg1);
    chainRamp (p[2], gain2, dg2);
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
    // prólogo: [--sp] = {rets, r15-r4}; sp += -68; r5 = r2 # [sp+20] = r3 (V15)
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
        return { ramBase + (uint32_t) i, table };
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
                            computeFb3 (t, outptr, &p[op], gain1, gain2, fb, std::min (fbShift + 2, 16));
                            p[op + 1].phase += p[op + 1].freq << 6;
                            p[op + 2].phase += p[op + 2].freq << 6;
                            op += 2;
                            break;
                        case 5:
                            computeFb2 (t, outptr, &p[op], gain1, gain2, fb, std::min (fbShift + 2, 16));
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

// ---- V16 ------------------------------------------------------------------------------------------------------------------
namespace
{
// El seno de los kernels de la V16 (0x01C0BFA4..0x01C0BFD8 en la V16 de fábrica), del sextra al << 13; mismos registros
// en todos: r0 fase, r1 ganancia, r6 / r7 las tablas
const uint8_t kV16Sin[] = { 0xb2, 0xe1, 0x2d, 0x06, 0xab, 0xbf, 0x3a, 0x19, 0xd8, 0xed, 0x68, 0x22, 0x80, 0xa8, 0x60, 0xe1,
                            0x00, 0x0c, 0x20, 0x19, 0x08, 0x1c, 0x43, 0xe0, 0xff, 0x03, 0x90, 0xe1, 0x33, 0x10, 0xd8, 0xed,
                            0x78, 0x11, 0xb2, 0xe1, 0x14, 0x05, 0x11, 0xe1, 0x00, 0x10, 0xa1, 0x1a, 0xb0, 0xe1, 0x85, 0x07,
                            0x18, 0x19, 0x00, 0xad };

bool at (const uint8_t* ram, size_t size, size_t o, std::initializer_list<uint8_t> b) noexcept
{
    if (o + b.size() > size)
        return false;
    size_t i = 0;
    for (auto x : b)
        if (ram[o + i++] != x)
            return false;
    return true;
}
} // namespace

int findV16Kernels (const uint8_t* ram, uint32_t ramBase, size_t ramSize, HleKernel* out, int max) noexcept
{
    int n = 0;
    const auto* end = ram + ramSize;
    for (const auto* p = std::search (ram, end, std::begin (kV16Sin), std::end (kV16Sin)); p != end && n < max;
         p = std::search (p + 1, end, std::begin (kV16Sin), std::end (kV16Sin)))
    {
        const auto c = (size_t) (p - ram);
        // la carga de la primera tabla (r6 = #imm32, c6 ff) entre el prólogo y el seno: el gancho
        size_t hook = 0;
        for (size_t o = c >= 0x40 ? c - 0x40 : 0; o + 12 <= c; o += 2)
            if (at (ram, ramSize, o, { 0xc6, 0xff }) && at (ram, ramSize, o + 6, { 0xc7, 0xff }))
                hook = o;  // (la última antes del seno)
        if (hook == 0)
            continue;
        HleKernel k;
        k.hook = ramBase + (uint32_t) hook;
        k.tLog = rd32 (ram + hook + 2);
        k.tExp = rd32 (ram + hook + 8);
        static const uint8_t asrR14[] = { 0xc8, 0xe1, 0x03, 0x0e };  // r0 = r0 >>> r14
        const bool fb = std::search (ram + hook, ram + c, std::begin (asrR14), std::end (asrR14)) != ram + c;
        if (fb)
            k.kind = HleKernel::Fb;
        else
        {
            if (! at (ram, ramSize, hook + 12, { 0x4c, 0xe0, 0x40, 0x00 }))  // r12 = 64
                continue;
            k.kind = at (ram, ramSize, hook + 18, { 0x50, 0x05 }) ? HleKernel::Input : HleKernel::Pure;  // r0 = [r5++=4]
        }
        // tras el seno: con la suma, r1 = [r4+0] (41 60); el epílogo {pc, R*n} = [sp++] (5n 04)
        for (size_t o = c + sizeof kV16Sin; o < c + sizeof kV16Sin + 8; o += 2)
            if (at (ram, ramSize, o, { 0x41, 0x60 }))
                k.add = true;
        for (size_t o = c + sizeof kV16Sin; o + 2 <= ramSize && o < c + sizeof kV16Sin + 0x20; o += 2)
            if ((ram[o] & 0xf0) == 0x50 && ram[o + 1] == 0x04)
            {
                k.exit = ramBase + (uint32_t) o;
                break;
            }
        const auto inRam = [&] (uint32_t a, uint32_t len) { return a >= ramBase && (size_t) (a - ramBase) + len <= ramSize; };
        if (k.exit == 0 || ! inRam (k.tLog, 2048) || ! inRam (k.tExp, 2048))
            continue;
        out[n++] = k;
    }
    return n;
}

bool hleV16Kernel (uint8_t* ram, uint32_t ramBase, size_t ramSize, const HleKernel& k, uint32_t regs[16]) noexcept
{
    const auto inRam = [&] (uint32_t a, uint32_t len) { return a >= ramBase && (size_t) (a - ramBase) + len <= ramSize; };
    const uint32_t outA = regs[4], inA = regs[5], fbA = regs[6];
    if (! inRam (outA, 256) || (k.kind == HleKernel::Input && ! inRam (inA, 256)) || (k.kind == HleKernel::Fb && ! inRam (fbA, 8))
        || ! inRam (k.tLog, 2048) || ! inRam (k.tExp, 2048))
        return false;
    const auto* tLog = ram + (k.tLog - ramBase);
    const auto* tExp = ram + (k.tExp - ramBase);
    const auto h = [] (const uint8_t* t, uint32_t i) { return (uint32_t) (t[2 * i] | (t[2 * i + 1] << 8)); };
    uint8_t* o = ram + (outA - ramBase);
    const uint8_t* in = k.kind == HleKernel::Input ? ram + (inA - ramBase) : nullptr;
    uint32_t gain = regs[8], phase = regs[10], y0 = regs[12], y = regs[13];
    const uint32_t dgain = regs[9], freq = regs[11], sh1 = regs[14];
    uint32_t r0 = 0, r1 = 0, r2 = 0;
    for (int i = 0; i < kN; ++i)
    {
        gain += dgain;
        uint32_t x = phase;
        if (in != nullptr)
            x += rd32 (in + 4 * i);
        else if (k.kind == HleKernel::Fb)
        {
            // r0 = r12 + r13; r0 >>>= r14; r12 = r13
            x += (uint32_t) ((int32_t) (y0 + y) >> std::min<uint32_t> (sh1 & 31, 31));
            y0 = y;
        }
        // el seno (kV16Sin), instrucción a instrucción
        auto a = (uint32_t) ((int32_t) ((x >> 12) << 21) >> 21);       // sextra (p 12, l 11)
        a ^= (uint32_t) ((int32_t) a >> 31);
        const auto e = (((x >> 8) & 0x8000u) | h (tLog, a & 0xffffu)) + gain;  // (a: 0..1023)
        r2 = (e >> 10) & 31;                                             // uextra (p 10, l 5)
        r1 = (h (tExp, 1023u & ~e) + 4096u) >> r2;
        r0 = ((uint32_t) -(int32_t) ((e >> 15) & 1) ^ r1) << 13;        // sextra (p 15, l 1), ^, << 13
        if (k.kind == HleKernel::Fb)
            y = r0;
        if (k.add)
        {
            r1 = rd32 (o + 4 * i);
            r0 += r1;
        }
        wr32 (o + 4 * i, r0);
        phase += freq;
    }
    regs[0] = r0;
    regs[1] = r1;
    regs[2] = r2;
    regs[3] = 1023;
    regs[4] = outA + 256;
    regs[7] = k.tExp;
    regs[8] = gain;
    regs[10] = phase;
    if (k.kind == HleKernel::Fb)
    {
        // r6 = fb_buf; [r6++=4] = r12; [r6] = r13
        wr32 (ram + (fbA - ramBase), y0);
        wr32 (ram + (fbA - ramBase) + 4, y);
        regs[6] = fbA + 4;
        regs[12] = y0;
        regs[13] = y;
    }
    else
    {
        if (k.kind == HleKernel::Input)
            regs[5] = inA + 256;
        regs[6] = k.tLog;
        regs[12] = 0;
    }
    return true;
}
} // namespace fm1

// Port a C++ del intérprete de referencia de fm1-emu (crate fm1-core, lib.rs; Joakim Eriksson, licencia MIT). Ver
// Fm1Cpu.h. Los comentarios de campos y de muestras del corpus resumen los del original; las cifras de muestras son
// las que da fm1-emu, no medidas nuestras.
#include "Fm1Cpu.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace fm1
{
namespace
{
using sv = std::string_view;

constexpr uint16_t kRouteSyntax = 30, kRouteFpu = 31;
constexpr uint8_t kRawPopRetsMask = 0x80 | 27;  // la forma de 4 bytes de la ruta 27 (d1 e8 <máscara>)

bool has (sv h, sv n) noexcept { return h.find (n) != sv::npos; }

// split_once para patrones cortos
bool split2 (sv h, sv n, sv& a, sv& b) noexcept
{
    const auto i = h.find (n);
    if (i == sv::npos)
        return false;
    a = h.substr (0, i);
    b = h.substr (i + n.size());
    return true;
}

bool stripPrefix (sv& s, sv p) noexcept
{
    if (! s.starts_with (p)) return false;
    s.remove_prefix (p.size());
    return true;
}

bool stripSuffix (sv& s, sv p) noexcept
{
    if (! s.ends_with (p)) return false;
    s.remove_suffix (p.size());
    return true;
}

uint32_t sx8 (uint32_t x) noexcept { return (uint32_t) (int32_t) (int8_t) (uint8_t) x; }
uint32_t sx16 (uint32_t x) noexcept { return (uint32_t) (int32_t) (int16_t) (uint16_t) x; }
uint32_t bswap32 (uint32_t x) noexcept
{
    return (x >> 24) | ((x >> 8) & 0xff00u) | ((x << 8) & 0xff0000u) | (x << 24);
}
uint32_t brev32 (uint32_t x) noexcept
{
    uint32_t r = 0;
    for (int i = 0; i < 32; ++i)
        r |= ((x >> i) & 1u) << (31 - i);
    return r;
}

// a + b + cin con las banderas (resta: a + ~b + 1; con préstamo: a + ~b + C)
uint32_t addFlags (uint32_t a, uint32_t b, uint32_t cin, uint32_t& f) noexcept
{
    const uint64_t w = (uint64_t) a + b + cin;
    const auto r = (uint32_t) w;
    f = (r >> 31) * kFlagN | (uint32_t) (w >> 32) * kFlagC | (uint32_t) (r == 0) * kFlagZ
      | (((a ^ r) & (b ^ r)) >> 31) * kFlagV;
    return r;
}

uint32_t alu (Fm1Pre::AluOp op, uint32_t a, uint32_t b) noexcept
{
    switch (op)
    {
        case Fm1Pre::Add:    return a + b;
        case Fm1Pre::Sub:    return a - b;
        case Fm1Pre::RSub:   return b - a;
        case Fm1Pre::And:    return a & b;
        case Fm1Pre::AndNot: return a & ~b;
        case Fm1Pre::Or:     return a | b;
        case Fm1Pre::Xor:    return a ^ b;
        case Fm1Pre::Mul:    return a * b;
        case Fm1Pre::Shl:    return a << (b & 31);
        case Fm1Pre::Shr:    return a >> (b & 31);
        case Fm1Pre::Sar:    return (uint32_t) ((int32_t) a >> (b & 31));
    }
    return 0;
}

// <<<, __builtin_pi32v2_qasl: desplazamiento a la izquierda saturado al rango con signo de 32 bits
uint32_t qasl (uint32_t x, uint32_t n) noexcept
{
    const auto v = (int64_t) (int32_t) x;
    if (v == 0) return 0;
    if (n >= 32) return v < 0 ? 0x80000000u : 0x7fffffffu;
    return (uint32_t) (int32_t) std::clamp<int64_t> (v << n, INT32_MIN, INT32_MAX);
}

// ---- comparaciones -----------------------------------------------------------------------------------------------
enum class Cond { Eq, Ne, LtU, GtU, LeU, GeU, LtS, GtS, LeS, GeS, FEq, FUne, FLt, FUge, FLe, FUgt, FGe, FUlt, FGt, FUle };

Cond condOfFam (uint64_t fam, bool alt) noexcept
{
    switch (fam)
    {
        case 0:  return alt ? Cond::Ne : Cond::Eq;
        case 1:  return alt ? Cond::LtU : Cond::GeU;
        case 4:  return alt ? Cond::LeU : Cond::GtU;
        case 5:  return alt ? Cond::LtS : Cond::GeS;
        case 8:  return alt ? Cond::FUne : Cond::FEq;
        case 9:  return alt ? Cond::FLt : Cond::FUge;
        case 12: return alt ? Cond::FLe : Cond::FUgt;
        case 13: return alt ? Cond::FUlt : Cond::FGe;
        case 14: return alt ? Cond::FUle : Cond::FGt;
        default: return alt ? Cond::LeS : Cond::GtS;
    }
}

// Comparación en coma flotante sobre los bits de los registros (NaN: las ordenadas dan falso, las no ordenadas cierto)
bool fcond (Cond c, uint32_t l, uint32_t r) noexcept
{
    const auto x = std::bit_cast<float> (l), y = std::bit_cast<float> (r);
    switch (c)
    {
        case Cond::FEq:  return x == y;
        case Cond::FUne: return ! (x == y);
        case Cond::FLt:  return x < y;
        case Cond::FUge: return ! (x < y);
        case Cond::FLe:  return x <= y;
        case Cond::FUgt: return ! (x <= y);
        case Cond::FGe:  return x >= y;
        case Cond::FUlt: return ! (x >= y);
        case Cond::FGt:  return x > y;
        default:         return ! (x > y);
    }
}

bool condEval (Cond c, uint32_t l, uint32_t r) noexcept
{
    switch (c)
    {
        case Cond::Eq:  return l == r;
        case Cond::Ne:  return l != r;
        case Cond::LtU: return l < r;
        case Cond::GtU: return l > r;
        case Cond::LeU: return l <= r;
        case Cond::GeU: return l >= r;
        case Cond::LtS: return (int32_t) l < (int32_t) r;
        case Cond::GtS: return (int32_t) l > (int32_t) r;
        case Cond::LeS: return (int32_t) l <= (int32_t) r;
        case Cond::GeS: return (int32_t) l >= (int32_t) r;
        default:        return fcond (c, l, r);
    }
}

// Condición común a todas las familias if: fam = byte1 & 7 de las formas de 4 bytes (e8 ==, e9 >= u, ec > u,
// ed >= s, ee > s), también el código de familia de las de inmediato (f8/f9/fc/fd/fe) y de 6 bytes (4x ff); alt
// invierte a !=, <, <=. fam >= 8: la comparación en coma flotante (iff).
bool cmpCond (uint64_t fam, bool alt, uint32_t a, uint32_t b) noexcept
{
    if (fam >= 8)
        return fcond (condOfFam (fam, alt), a, b);
    bool base;
    switch (fam)
    {
        case 0:  base = a == b; break;
        case 1:  base = a >= b; break;
        case 4:  base = a > b; break;
        case 5:  base = (int32_t) a >= (int32_t) b; break;
        default: base = (int32_t) a > (int32_t) b; break;
    }
    return base != alt;
}

// ---- FPU (3f e5) -------------------------------------------------------------------------------------------------
// Conversiones saturadas como "as" de Rust: NaN -> 0, fuera de rango -> el extremo
int32_t f2i (float x) noexcept
{
    if (std::isnan (x)) return 0;
    if (x >= 2147483648.0f) return INT32_MAX;
    if (x < -2147483648.0f) return INT32_MIN;
    return (int32_t) x;
}
uint32_t f2u (float x) noexcept
{
    if (std::isnan (x) || x <= 0.0f) return 0;
    if (x >= 4294967296.0f) return UINT32_MAX;
    return (uint32_t) x;
}
// Redondeo al par más cercano sin depender del modo de redondeo del hilo
float roundTiesEven (float x) noexcept
{
    const float f = std::floor (x);
    const float d = x - f;
    if (d > 0.5f) return f + 1.0f;
    if (d < 0.5f) return f;
    return std::fmod (f, 2.0f) == 0.0f ? f : f + 1.0f;
}

// Operación binaria 0 +, 1 -, 2 *, 3 /, 5 fmin, 6 fmax sobre (a, b); unaria "K f" sobre a: K 0..3 ftoi, 4..7 ftou,
// c..f ftof (un float entero), redondeo por K & 3 (0 al par más cercano, 1 truncar, 2 techo, 3 suelo); 0x8f itof,
// 0x9f utof. Las operaciones 7 y 8 son d += a * b y d -= a * b sin fusionar (el producto se redondea antes de sumar).
// Supuesto (fm1-emu): el redondeo y los subnormales del hardware no se han medido. Si el hilo tiene FTZ/DAZ activos
// (ScopedNoDenormals), los subnormales se pierden aquí y no en fm1-emu.
uint32_t fpuD (uint8_t op, uint32_t d, uint32_t a, uint32_t b) noexcept
{
    const auto x = std::bit_cast<float> (a), y = std::bit_cast<float> (b);
    const auto rnd = [x] (uint8_t k) -> float
    {
        switch (k & 3)
        {
            case 0:  return roundTiesEven (x);
            case 1:  return std::trunc (x);
            case 2:  return std::ceil (x);
            default: return std::floor (x);
        }
    };
    switch (op)
    {
        case 0: return std::bit_cast<uint32_t> (x + y);
        case 1: return std::bit_cast<uint32_t> (x - y);
        case 2: return std::bit_cast<uint32_t> (x * y);
        case 3: return std::bit_cast<uint32_t> (x / y);
        case 5: return std::bit_cast<uint32_t> (std::fmin (x, y));
        case 6: return std::bit_cast<uint32_t> (std::fmax (x, y));
        case 7: { const float p = x * y; return std::bit_cast<uint32_t> (std::bit_cast<float> (d) + p); }
        case 8: { const float p = x * y; return std::bit_cast<uint32_t> (std::bit_cast<float> (d) - p); }
        case 0x8f: return std::bit_cast<uint32_t> ((float) (int32_t) a);
        case 0x9f: return std::bit_cast<uint32_t> ((float) a);
        default:
        {
            const uint8_t k = op >> 4;
            if (k <= 3) return (uint32_t) f2i (rnd (k));
            if (k <= 7) return f2u (rnd (k));
            return std::bit_cast<uint32_t> (rnd (k));
        }
    }
}

// (op, d, a, b) de una instrucción FPU
void decodeFpu (uint64_t raw, uint8_t& op, uint8_t& d, uint8_t& a, uint8_t& b) noexcept
{
    const auto b2 = (uint8_t) (raw >> 16), b3 = (uint8_t) (raw >> 24);
    if ((b2 & 0xf) == 0xf) { op = b2; d = b3 >> 4; a = b3 & 0xf; b = 0; }
    else                   { op = b2 & 0xf; d = b3 >> 4; a = b2 >> 4; b = b3 & 0xf; }
}

// ---- familias decodificadas por sus campos -------------------------------------------------------------------------
using Rhs = Fm1Pre::Rhs;
using AluOp = Fm1Pre::AluOp;

struct Alu4 { bool dstMem; uint8_t dst; int32_t memOff; uint8_t src; AluOp op; Rhs rhs; };

int32_t sext6x4 (uint64_t v) noexcept
{
    const auto x = (int32_t) (v & 0x3f);
    return 4 * ((x & 0x20) != 0 ? x - 0x40 : x);
}

// Las ALU de 4 bytes con byte1 = e0/e1/e8/eb/ef (verificadas por fm1-emu en todas sus muestras de V13, isa-notes):
//   e0 b4 / e1 90 / e1 f0   R = R op R: d = bits(28,31) a = bits(20,23) b = bits(24,27), subop bits(16,19):
//                           b4 {0 +, 2 -}, 90 {0 |, 1 ^, 2 &}, f0 {0 *}
//   e1 op4 0..3             R = R + sext14(bits(16,27) | op4 << 12)
//   e1 op4 4/5/6/7, e       R = R |, ^, &, &~, * compuesto;  e0 op4 e/f: R +, - compuesto
//   e0 op4 a                R = compuesto - R          (d = bits(0,3), s = bits(28,31))
//   e8 64 / 68              [R+off] op= R: base = bits(28,31) src = bits(24,27) off = 4·sext6(bits(18,23))
//   eb / ef                 [R+off] op= imm: base = bits(28,31) off = 4·sext6(bits(0,5)), bits(6,7) eligen la op
bool decodeAlu4 (uint64_t raw, Alu4& out) noexcept
{
    const auto b0 = (uint32_t) (raw & 0xff);
    auto b1 = (uint32_t) ((raw >> 8) & 0xff);
    if (b1 == 0xf0) b1 = 0xe0;  // f0/f1: las gemelas que fijan banderas (impresas con " #")
    else if (b1 == 0xf1) b1 = 0xe1;
    const auto op4 = b0 >> 4;
    const auto code = (uint32_t) ((raw >> 24) & 0xf);
    const auto m = (uint32_t) ((raw >> 16) & 0xff);
    const auto imm12 = (uint32_t) ((raw >> 16) & 0xfff);
    const auto comp = composedImm (code, m);
    const auto dLo = (uint8_t) (b0 & 0xf);
    const auto sHi = (uint8_t) ((raw >> 28) & 0xf);
    const auto regDst = [&] (AluOp op, Rhs rhs) { out = { false, dLo, 0, sHi, op, rhs }; return true; };
    const auto imm = [] (uint32_t x) { return Rhs { Fm1Pre::RhsImm, x }; };

    if ((b0 == 0xb4 && b1 == 0xe0) || ((b0 == 0x90 || b0 == 0xc8 || b0 == 0x94 || b0 == 0xf0) && b1 == 0xe1))
    {
        // formas de tres registros; c8 = desplazamientos por registro, 94 = R op (1 << R). (b4 e1 es el extractor
        // de campo de bits, no una suma.)
        const auto sub = (uint32_t) ((raw >> 16) & 0xf);
        AluOp op;
        if (b0 == 0xb4 && sub == 0) op = Fm1Pre::Add;
        else if (b0 == 0xb4 && sub == 2) op = Fm1Pre::Sub;
        else if ((b0 == 0x90 || b0 == 0x94) && sub == 0) op = Fm1Pre::Or;
        else if ((b0 == 0x90 || b0 == 0x94) && sub == 1) op = Fm1Pre::Xor;
        else if ((b0 == 0x90 || b0 == 0x94) && sub == 2) op = Fm1Pre::And;
        else if (b0 == 0x94 && sub == 3) op = Fm1Pre::AndNot;  // 94 e1 A3 DB: rD = rA & ~(1 << rB)
        else if (b0 == 0xf0 && sub == 0) op = Fm1Pre::Mul;
        else if (b0 == 0xc8 && sub == 0) op = Fm1Pre::Shl;
        else if (b0 == 0xc8 && sub == 2) op = Fm1Pre::Shr;
        else if (b0 == 0xc8 && sub == 3) op = Fm1Pre::Sar;
        else return false;
        const auto rr = (uint32_t) ((raw >> 24) & 0xf);
        out = { false, sHi, 0, (uint8_t) ((raw >> 20) & 0xf), op,
                Rhs { b0 == 0x94 ? Fm1Pre::RhsBit : Fm1Pre::RhsReg, rr } };
        return true;
    }
    if (b1 == 0xe0)
    {
        if (op4 == 0xe) return regDst (Fm1Pre::Add, imm (comp));
        if (op4 == 0xf) return regDst (Fm1Pre::Sub, imm (comp));
        if (op4 == 0xa) return regDst (Fm1Pre::RSub, imm (comp));
        return false;
    }
    if (b1 == 0xe1)
    {
        if (op4 <= 3)
        {
            auto x = imm12 | (op4 << 12);
            if ((x & 0x2000) != 0) x |= ~0x3fffu;
            return regDst (Fm1Pre::Add, imm (x));
        }
        switch (op4)
        {
            case 4:   return regDst (Fm1Pre::Or, imm (comp));
            case 5:   return regDst (Fm1Pre::Xor, imm (comp));
            case 6:   return regDst (Fm1Pre::And, imm (comp));
            case 7:   return regDst (Fm1Pre::AndNot, imm (comp));
            case 0xe: return regDst (Fm1Pre::Mul, imm (comp));
            default:  return false;
        }
    }
    if (b1 == 0xe8 && (b0 == 0x6c || b0 == 0x6d))
    {
        // [R+#i] <<= #n (6c e8): base bits 28-31, desplazamiento en palabras bits 18-23, cuenta bits 24-27, op bits
        // 16-17 como los de e1; 6d e8: lo mismo con 16 más en la cuenta
        AluOp op;
        switch ((raw >> 16) & 3)
        {
            case 0: op = Fm1Pre::Shl; break;
            case 2: op = Fm1Pre::Shr; break;
            case 3: op = Fm1Pre::Sar; break;
            default: return false;
        }
        out = { true, 0, sext6x4 (raw >> 18), sHi, op, imm ((uint32_t) ((raw >> 24) & 0xf) + 16 * (b0 & 1)) };
        return true;
    }
    if (b1 == 0xe8 && b0 == 0x66)
    {
        // [R+#i] op= 1 << R (66 e8): el formato de 64 e8 con operando de bit; op {0 |, 1 ^, 2 &, 3 &~}
        static constexpr AluOp ops[] = { Fm1Pre::Or, Fm1Pre::Xor, Fm1Pre::And, Fm1Pre::AndNot };
        out = { true, 0, sext6x4 (raw >> 18), sHi, ops[(raw >> 16) & 3], Rhs { Fm1Pre::RhsBit, (uint32_t) ((raw >> 24) & 0xf) } };
        return true;
    }
    if (b1 == 0xe8 && (b0 == 0x64 || b0 == 0x68 || b0 == 0x6e))
    {
        // 6e e8: [R+#i] <<= R / >>= R / >>>= R, la gemela por registro de 6c e8 (1 es el <<<= saturado, no se toma)
        const auto sub = (raw >> 16) & 3;
        AluOp op;
        if (b0 == 0x64 && sub == 0) op = Fm1Pre::Or;
        else if (b0 == 0x64 && sub == 1) op = Fm1Pre::Xor;
        else if (b0 == 0x64 && sub == 2) op = Fm1Pre::And;
        else if (b0 == 0x68 && sub == 0) op = Fm1Pre::Add;
        else if (b0 == 0x68 && sub == 2) op = Fm1Pre::Sub;
        else if (b0 == 0x6e && sub == 0) op = Fm1Pre::Shl;
        else if (b0 == 0x6e && sub == 2) op = Fm1Pre::Shr;
        else if (b0 == 0x6e && sub == 3) op = Fm1Pre::Sar;
        else return false;
        out = { true, 0, sext6x4 (raw >> 18), sHi, op, Rhs { Fm1Pre::RhsReg, (uint32_t) ((raw >> 24) & 0xf) } };
        return true;
    }
    if (b1 == 0xeb || b1 == 0xef)
    {
        const auto sel = (b0 >> 6) & 3;
        AluOp op;
        uint32_t x;
        if (b1 == 0xef) { static constexpr AluOp ops[] = { Fm1Pre::Or, Fm1Pre::Xor, Fm1Pre::And, Fm1Pre::AndNot }; op = ops[sel]; x = comp; }
        else if (sel == 2) { op = Fm1Pre::Add; x = comp; }
        else if (sel == 3) { op = Fm1Pre::Add; x = (imm12 & 0x800) != 0 ? (imm12 | ~0xfffu) : imm12; }
        else return false;
        out = { true, 0, sext6x4 (raw), sHi, op, imm (x) };
        return true;
    }
    return false;
}

struct BitTest { uint8_t reg; Rhs mask; bool ne; int64_t off; };

int64_t sextN (uint64_t v, unsigned bits) noexcept
{
    const auto x = (int64_t) (v & ((1ull << bits) - 1));
    return (x & ((int64_t) 1 << (bits - 1))) != 0 ? x - ((int64_t) 1 << bits) : x;
}

// if ((rN & mask) ==/!= 0) goto #off, tres codificaciones (1358 muestras de V13 según fm1-emu):
//   6 bytes 60/61 ff: reg = bits(28,31), bit0 = !=, mask compuesta (bits(24,27), bits(16,23)), off = 2·sext16(bits(32,47))
//   4 bytes 5x e8:    reg = bits(0,3), mask = 1 << bits(27,31), bit25 = !=, off = 2·sext9(bits(16,23) | bit24 << 8)
//   4 bytes fa/fb:    reg = bits(0,3), mask = r[bits(4,7)], byte1 fb = !=, off = 2·sext16(bits(16,31))
bool decodeBitTest (uint64_t raw, uint8_t len, BitTest& out) noexcept
{
    const auto b0 = (uint32_t) (raw & 0xff), b1 = (uint32_t) ((raw >> 8) & 0xff);
    if (len == 6 && b1 == 0xff && (b0 >> 1) == 0x30)
    {
        out = { (uint8_t) ((raw >> 28) & 0xf), Rhs { Fm1Pre::RhsImm, composedImm ((uint32_t) ((raw >> 24) & 0xf), (uint32_t) ((raw >> 16) & 0xff)) },
                (b0 & 1) != 0, 2 * sextN (raw >> 32, 16) };
        return true;
    }
    if (len == 4 && b1 == 0xe8 && (b0 >> 4) == 5)
    {
        out = { (uint8_t) (raw & 0xf), Rhs { Fm1Pre::RhsImm, 1u << ((raw >> 27) & 0x1f) }, ((raw >> 25) & 1) != 0,
                2 * sextN (((raw >> 16) & 0xff) | (((raw >> 24) & 1) << 8), 9) };
        return true;
    }
    if (len == 4 && (b1 == 0xfa || b1 == 0xfb))
    {
        out = { (uint8_t) (raw & 0xf), Rhs { Fm1Pre::RhsReg, (uint32_t) ((raw >> 4) & 0xf) }, (b1 & 1) != 0,
                2 * sextN (raw >> 16, 16) };
        return true;
    }
    return false;
}

// if[s] (rA <op> rB) goto #off: 4 bytes (byte1 e8/e9/ec/ed/ee, bit7 alt, A = bits(28,31), B = bits(0,3),
// off = 2·sext9(bits(16,23) | bit24 << 8)) y 6 bytes (4x ff: fam = bits(1,3), bit0 alt, A = bits(28,31),
// B = bits(24,27), off = 2·sext16(bits(32,47))). Bit 27 / bit 23: la comparación en coma flotante (familias 8..).
bool decodeCmpRrBranch (uint64_t raw, uint8_t len, uint64_t& fam, bool& alt, int& a, int& b, int64_t& off) noexcept
{
    const auto b0 = raw & 0xff, b1 = (raw >> 8) & 0xff;
    if (len == 4 && (b1 == 0xe8 || b1 == 0xe9 || b1 == 0xec || b1 == 0xed || b1 == 0xee) && (b0 & 0x70) == 0)
    {
        const auto f = (int64_t) (((raw >> 16) & 0xff) | (((raw >> 24) & 1) << 8));
        off = 2 * ((f & 0x100) != 0 ? f - 0x200 : f);
        fam = (b1 & 7) | (((raw >> 27) & 1) << 3);
        alt = (b0 & 0x80) != 0;
        a = (int) ((raw >> 28) & 0xf);
        b = (int) (raw & 0xf);
        return true;
    }
    if (len == 6 && b1 == 0xff && (b0 >> 4) == 4)
    {
        fam = ((b0 >> 1) & 7) | (((raw >> 23) & 1) << 3);
        alt = (b0 & 1) != 0;
        a = (int) ((raw >> 28) & 0xf);
        b = (int) ((raw >> 24) & 0xf);
        off = 2 * (int64_t) (int16_t) (uint16_t) (raw >> 32);
        return true;
    }
    return false;
}

// Acceso de 64 bits a un par de registros rH_rL = d[...] / d[...] = rH_rL (el bajo en la dirección menor).
//   5x ec: par = bits(29,31), store = bit16, base = bits(20,23),
//          off = 4·sext9(bits(18,19) | bits(24,27) << 2 | bit0 << 6 | bit1 << 7 | bit2 << 8); el modo:
//          50..57, bit17 a 0 d[R+#], a 1 d[++R=#]; 58..5f, bit17 a 0 d[R++=#]; a 1: índice bits(24,27), bits(1,2)
//          el modo (58 d[R+R], 5c d[++R=R], 5e d[R++=R]).
//   d0 e9: relativo a sp, off = bits(17,27) << 1.
struct PairMem { int pair; bool store; int base; int32_t off; int index; bool pre, post; };  // base/index -1 = ninguno

PairMem decodePairMem (uint64_t raw) noexcept
{
    const auto pair = (int) ((raw >> 29) & 7);
    const bool store = ((raw >> 16) & 1) != 0;
    if (((raw >> 8) & 0xff) != 0xec)
        return { pair, store, -1, (int32_t) (((raw >> 17) & 0x7ff) << 1), -1, false, false };
    const auto base = (int) ((raw >> 20) & 0xf);
    const auto f = (int32_t) (((raw >> 18) & 3) | (((raw >> 24) & 0xf) << 2) | ((raw & 1) << 6) | (((raw >> 1) & 1) << 7)
                              | (((raw >> 2) & 1) << 8));
    const auto off = 4 * ((f & 0x100) != 0 ? f - 0x200 : f);
    const bool bit17 = ((raw >> 17) & 1) != 0;
    if ((raw & 8) == 0) return { pair, store, base, off, -1, bit17, false };
    if (! bit17) return { pair, store, base, off, -1, false, true };
    return { pair, store, base, 0, (int) ((raw >> 24) & 0xf), (raw & 6) == 4, (raw & 6) == 6 };
}

// ¿Un salto a pc desde dentro de una parte then que empieza en ts salió de la construcción hacia atrás? No una
// llamada (vuelve a la construcción): una llamada pone rets en su propio siguiente pc, y nada más lo hace.
bool predLeftBack (uint32_t pc, uint32_t ts, uint32_t nextPc, uint32_t rets) noexcept { return pc < ts && rets != nextPc; }

struct IfBlock { uint64_t fam; bool alt; int reg; Rhs rhs; uint32_t thenUnits, elseUnits; };

// Bloque predicado if[s] (rA <op> x) { (4 bytes, byte1 e8/e9/ea/ec/ed/ee): rA = bits(0,3); nibble alto h de byte0:
// h & 3 = 1 registro (rB = bits(24,27)), 2 inmediato compuesto, 3 sext12(bits(16,27)); bit7 invierte (familia ea de
// prueba de bits: h = 2 == 0, h = 3 != 0, forma registro bit 23 = != 0). Tamaños en bits(28,31): then = bits(30,31) + 1
// instrucciones, else = bits(28,29) (2057 bloques del corpus según fm1-emu).
bool decodeIfBlock (uint64_t raw, IfBlock& out) noexcept
{
    const auto b0 = raw & 0xff, b1 = (raw >> 8) & 0xff;
    if (! (b1 == 0xe8 || b1 == 0xe9 || b1 == 0xea || b1 == 0xec || b1 == 0xed || b1 == 0xee))
        return false;
    const auto h = b0 >> 4;
    auto fam = b1 & 7;
    Rhs rhs;
    bool alt;
    const auto comp = [raw] { return composedImm ((uint32_t) ((raw >> 24) & 0xf), (uint32_t) ((raw >> 16) & 0xff)); };
    if (fam == 2 && (h & 7) == 1)                      { rhs = { Fm1Pre::RhsReg, (uint32_t) ((raw >> 24) & 0xf) }; alt = ((raw >> 23) & 1) != 0; }
    else if (fam == 2 && ((h & 7) == 2 || (h & 7) == 3)) { rhs = { Fm1Pre::RhsImm, comp() }; alt = (h & 1) != 0; }
    else if ((h & 7) == 1)                             { rhs = { Fm1Pre::RhsReg, (uint32_t) ((raw >> 24) & 0xf) }; alt = (h & 8) != 0; }
    else if ((h & 7) == 2)                             { rhs = { Fm1Pre::RhsImm, comp() }; alt = (h & 8) != 0; }
    else if ((h & 7) == 3)
    {
        // sext12, salvo las familias sin signo (e9 >=, ec >), que extienden con ceros
        auto x = (uint32_t) ((raw >> 16) & 0xfff);
        if ((x & 0x800) != 0 && ! (fam == 1 || fam == 4)) x |= ~0xfffu;
        rhs = { Fm1Pre::RhsImm, x };
        alt = (h & 8) != 0;
    }
    else return false;
    // bit 23 de la forma registro (no en la familia de prueba de bits): la comparación en coma flotante
    if (fam != 2 && (h & 7) == 1 && ((raw >> 23) & 1) != 0)
        fam |= 8;
    out = { fam, alt, (int) (raw & 0xf), rhs, (uint32_t) ((raw >> 30) & 3) + 1, (uint32_t) ((raw >> 28) & 3) };
    return true;
}

// dc [++rB=rI] / de [rB++=rI] (byte1 ec palabra, ed media, ee byte): dato bits(28,31), base bits(20,23), índice
// bits(24,27); bits(16,19): bit16 store, bit17 carga con signo (ec: bit17 a 1 = esta forma por registro), en un store
// de media palabra bit17 = la mitad alta.
struct IdxInc { bool pre; uint8_t w; bool store, sign, high; int d, b, i; };

bool idxInc (uint64_t raw, IdxInc& out) noexcept
{
    bool pre;
    switch (raw & 0xff) { case 0xdc: pre = true; break; case 0xde: pre = false; break; default: return false; }
    uint8_t w;
    switch ((raw >> 8) & 0xff) { case 0xec: w = 4; break; case 0xed: w = 2; break; case 0xee: w = 1; break; default: return false; }
    const auto sub = (raw >> 16) & 0xf;
    const bool store = (sub & 1) != 0, bit17 = (sub & 2) != 0;
    if ((sub & 8) != 0 || (w == 4 && ! bit17) || (w == 1 && store && bit17))
        return false;
    out = { pre, w, store, ! store && w != 4 && bit17, store && w == 2 && bit17,
            (int) ((raw >> 28) & 0xf), (int) ((raw >> 20) & 0xf), (int) ((raw >> 24) & 0xf) };
    return true;
}

// d8|d9|dc|dd e9 OO RX: accesos de byte / media palabra relativos a sp, R = bits(28,31), off12 = X << 8 | OO:
//   dc rR = b[sp+off] (u), dd (s); d9 rR = h[sp+off] (s) (off impar no es instrucción);
//   d8 la media palabra en off & ~1, bit 0 = store h[sp+off] = rR, si no carga (u).
struct SpMem { uint8_t d; uint32_t off; uint8_t w; bool sign, store; };

bool spMemRaw (uint64_t raw, SpMem& out) noexcept
{
    if (((raw >> 8) & 0xff) != 0xe9)
        return false;
    const auto b2 = (raw >> 16) & 0xff, b3 = (raw >> 24) & 0xff;
    const auto d = (uint8_t) (b3 >> 4);
    const auto off = (uint32_t) (((b3 & 0xf) << 8) | b2);
    switch (raw & 0xff)
    {
        case 0xdc: out = { d, off, 1, false, false }; return true;
        case 0xdd: out = { d, off, 1, true, false }; return true;
        case 0xd9: if ((off & 1) != 0) return false; out = { d, off, 2, true, false }; return true;
        case 0xd8: out = { d, off & ~1u, 2, false, (off & 1) != 0 }; return true;
        default:   return false;
    }
}

// Rutas 18 y 19, accesos de 4 bytes con incremento: dato bits(28,31), base bits(20,23), f = bits(16,19) | bits(24,27) << 4;
// el ancho por byte1 (ec palabra, ed media, ee byte). Detalle de campos: decode_inc_mem de fm1-core.
struct IncMem { int d, b; uint8_t w; bool store, sign; int32_t inc; bool pre, post; };

IncMem decodeIncMem (uint8_t route, uint64_t raw) noexcept
{
    const uint8_t w = ((raw >> 8) & 0xff) == 0xec ? 4 : ((raw >> 8) & 0xff) == 0xed ? 2 : 1;
    const auto d = (int) ((raw >> 28) & 0xf), b = (int) ((raw >> 20) & 0xf);
    const auto f = (uint32_t) (((raw >> 16) & 0xf) | (((raw >> 24) & 0xf) << 4));
    bool store, sign;
    int32_t inc;
    if (w == 1)
    {
        inc = (raw & 1) != 0 ? (int32_t) f - 256 : (int32_t) f;
        store = ((raw >> 1) & 1) != 0;
        sign = ((raw >> 2) & 1) != 0;
    }
    else if (w == 4 && route == 18)
    {
        const auto o = (int32_t) (((raw >> 18) & 3) | (((raw >> 24) & 0xf) << 2) | ((raw & 1) << 6) | (((raw >> 1) & 1) << 7)
                                  | (((raw >> 2) & 1) << 8));
        store = ((raw >> 16) & 1) != 0;
        inc = 4 * ((o & 0x100) != 0 ? o - 0x200 : o);
        sign = false;
    }
    else if (w == 4)
    {
        const auto x = (f & ~1u) | ((uint32_t) (raw & 7) << 8);
        inc = (x & 0x400) != 0 ? (int32_t) x - 0x800 : (int32_t) x;
        store = ((raw >> 16) & 1) != 0;
        sign = false;
    }
    else
    {
        const auto x = (f & ~1u) | ((uint32_t) (raw & 1) << 8) | ((uint32_t) ((raw >> 1) & 1) << 9);
        inc = (x & 0x200) != 0 ? (int32_t) x - 0x400 : (int32_t) x;
        store = ((raw >> 16) & 1) != 0;
        sign = ((raw >> 2) & 1) != 0;
    }
    const bool wordOff = route == 18 && w == 4 && ((raw >> 17) & 1) == 0;
    return { d, b, w, store, sign, inc, route == 18 && ! wordOff, route == 19 };
}

// 5x ed: medias palabras; dato bits(28,31), base bits(20,23), bit16 store, bit 2 de byte0 con signo (carga) / la mitad
// alta (store), bit 3 preincremento; off = sext10(byte0 & 3 << 8 | bits(24,27) << 4 | bits(17,19) << 1) en bytes
struct HalfMem { int d, b; bool store, flag, pre; int32_t off; };

bool halfMem (uint64_t raw, HalfMem& out) noexcept
{
    if (((raw >> 8) & 0xff) != 0xed || (raw & 0xf0) != 0x50)
        return false;
    const auto b0 = raw & 0xff, b2 = (raw >> 16) & 0xff, b3 = (raw >> 24) & 0xff;
    const auto x = (int32_t) (((b0 & 3) << 8) | ((b3 & 15) << 4) | (b2 & 14));
    out = { (int) (b3 >> 4), (int) (b2 >> 4), (b2 & 1) != 0, (b0 & 4) != 0, (b0 & 8) != 0, (x & 512) != 0 ? x - 1024 : x };
    return true;
}

// 78..7b e0 0S DS: rD = sat16(rS), S 1 con signo, 0 sin signo
bool sat16Raw (uint64_t raw, uint8_t& d, uint8_t& s, bool& sign) noexcept
{
    const auto b1 = (raw >> 8) & 0xff;
    if (! (b1 == 0xe0 || b1 == 0xf0) || (raw & 0xfc) != 0x78 || ((raw >> 16) & 0xfe) != 0)
        return false;
    d = (uint8_t) ((raw >> 28) & 0xf);
    s = (uint8_t) ((raw >> 24) & 0xf);
    sign = ((raw >> 16) & 1) != 0;
    return true;
}

// Formas DSP de los builtins del compilador (ruta 41); d = bits(28,31)
enum class Dsp { None, QaslImm, QaslReg, Cls, MinMax, BrevAdd };

Dsp dspRaw (uint64_t raw) noexcept
{
    const auto b1r = (raw >> 8) & 0xff;
    const bool pair = b1r == 0xf0 || b1r == 0xf1 || b1r == 0xf4;
    const auto b1 = pair ? (b1r ^ 0x10) : b1r;
    const auto b0 = raw & 0xff;
    if (b0 == 0xc0 && b1 == 0xe1 && ((raw >> 25) & 7) == 2) return Dsp::QaslImm;
    if (b0 == 0xc8 && b1 == 0xe1 && ((raw >> 16) & 0xf) == 1) return Dsp::QaslReg;
    if (b0 == 0x80 && b1 == 0xe1 && ((raw >> 16) & 0xff) == 1) return Dsp::Cls;
    if (b0 == 0xb0 && b1 == 0xe0 && ((raw >> 16) & 0xff) == 0) return Dsp::BrevAdd;
    if ((b0 == 0x34 || b0 == 0x35) && b1 == 0xe4 && ((raw >> 17) & 7) == 0) return Dsp::MinMax;
    return Dsp::None;
}

// Movimientos de bloque con base registro: "[R+] = {..}" / "{..} = [R+]" y las formas R++, -R, --R.
// (store, actualiza la base, hacia abajo)
bool blockForm (sv s, bool& store, bool& wb, bool& down) noexcept
{
    sv m, a, b;
    if (s.starts_with ('['))
    {
        if (! split2 (s.substr (1), "] = {", a, b)) return false;
        store = true;
        m = a;
    }
    else if (s.starts_with ('{'))
    {
        if (! split2 (s, "} = [", a, b)) return false;
        if (! stripSuffix (b, "]")) return false;
        store = false;
        m = b;
    }
    else return false;
    if (m == "R+")  { wb = false; down = false; return true; }
    if (m == "R++") { wb = true;  down = false; return true; }
    if (m == "-R")  { wb = false; down = true;  return true; }
    if (m == "--R") { wb = true;  down = true;  return true; }
    return false;
}

// ---- entradas sin tabla (las de decode_any) --------------------------------------------------------------------
#define FM1_RAW(id, nm, syn, can, ln) \
    const Fm1IsaEntry id { nm, syn, can, ln, 0u, 0ull, 0ull, nullptr, 0, nullptr, 0, nullptr, 0 };

const Fm1SlotSpec kSregSlots[] = { { 0, 1, { { 28, 31 } }, 0, 0, 0, 0 } };
const Fm1SlotSpec kCarrySlots[] = {
    { 0, 1, { { 28, 31 } }, 0, 0, 0, 0 },
    { 0, 1, { { 20, 23 } }, 0, 0, 0, 0 },
    { 0, 1, { { 24, 27 } }, 0, 0, 0, 0 },
};

FM1_RAW (kPairOpRaw, "pair_op_raw", "r1_r0 pair op (raw)", "r1_r0 pair op (raw)", 4)
FM1_RAW (kPairOpPairRaw, "pair_op_raw", "r1_r0 pair op (raw)  #", "r1_r0 pair op (raw)", 4)
FM1_RAW (kDspCraw, "dsp_craw", "R = dsp(R, R) (raw)", "R = dsp(R, R) (raw)", 4)
FM1_RAW (kDspPairCraw, "dsp_craw", "R = dsp(R, R) (raw)  #", "R = dsp(R, R) (raw)", 4)
FM1_RAW (kSsatCraw, "ssat_craw", "R = R +|- R (ssat)", "R = R +|- R (ssat)", 4)
FM1_RAW (kSsatPairCraw, "ssat_craw", "R = R +|- R (ssat)  #", "R = R +|- R (ssat)", 4)
FM1_RAW (kRotCraw, "rot_craw", "R = R <> #i", "R = R <> #i", 4)
FM1_RAW (kRotPairCraw, "rot_pair_craw", "R = R <> #i  #", "R = R <> #i", 4)
FM1_RAW (kSat16Craw, "sat16_craw", "R = sat16(R) (raw)", "R = sat16(R) (raw)", 4)
FM1_RAW (kSat16PairCraw, "sat16_craw", "R = sat16(R) (raw)  #", "R = sat16(R) (raw)", 4)
FM1_RAW (kByteSetPairCraw, "byte_set_craw", "R.b# = #i (raw)  #", "R.b# = #i (raw)", 4)
FM1_RAW (kByteSetCraw, "byte_set_craw", "R.b# = #i (raw)", "R.b# = #i (raw)", 4)
FM1_RAW (kPairSpCraw, "pair_sp_craw", "d[sp+#i] = R_R (raw)", "d[sp+#i] = R_R (raw)", 4)
FM1_RAW (kRev8Craw, "rev8_craw", "R = rev8(R)", "R = rev8(R)", 4)
FM1_RAW (kRev8PairCraw, "rev8_craw", "R = rev8(R)  #", "R = rev8(R)", 4)
FM1_RAW (kRev16Craw, "rev8_craw", "R = rev16(R)", "R = rev16(R)", 4)
FM1_RAW (kRev16PairCraw, "rev8_craw", "R = rev16(R)  #", "R = rev16(R)", 4)
FM1_RAW (kPopcntCraw, "rev8_craw", "R = popcnt(R)", "R = popcnt(R)", 4)
FM1_RAW (kPopcntPairCraw, "rev8_craw", "R = popcnt(R)  #", "R = popcnt(R)", 4)
FM1_RAW (kCmpRrBranch6Craw, "if_r_op_r_goto_i_craw", "if (R op R) goto #i (raw)", "if (R op R) goto #i (raw)", 6)
FM1_RAW (kIdxIncCraw, "idx_inc_craw", "[R++=R] = R (raw)", "[R++=R] = R (raw)", 4)
FM1_RAW (kBitTestEqCraw, "if_bit_test_craw", "if ((R & #h) == 0) goto #i", "if ((R & #h) == 0) goto #i", 6)
FM1_RAW (kBitTestNeCraw, "if_bit_test_craw", "if ((R & #h) != 0) goto #i", "if ((R & #h) != 0) goto #i", 6)
FM1_RAW (kIfBlockCraw, "if_block_craw", "if (R op x) { (raw)", "if (R op x) { (raw)", 4)
FM1_RAW (kAlu4Craw, "alu4_craw", "R op= (alu4 raw)", "R op= (alu4 raw)", 4)
FM1_RAW (kPostIncCraw, "post_inc_craw", "[R++=#i] = R (raw)", "[R++=#i] = R (raw)", 4)
FM1_RAW (kWordMemCraw, "word_mem_craw", "[R+#i] = R (word raw)", "[R+#i] = R (word raw)", 4)
FM1_RAW (kRetsPushMaskCraw, "sp_mov_rets_mask_craw", "[--sp] = {rets, R*4}", "[--sp] = {rets, R*4}", 4)
FM1_RAW (kPcPopMaskCraw, "pc_mask_mov_sp_craw", "{pc, R*4} = [sp++]", "{pc, R*4} = [sp++]", 4)
FM1_RAW (kFpuMacCraw, "fpu_mac_craw", "R += R * R (f)", "R += R * R (f)", 4)
FM1_RAW (kFpuMacPairCraw, "fpu_mac_craw", "R += R * R (f) #", "R += R * R (f)", 4)
FM1_RAW (kHalfMemCraw, "half_mem_craw", "h[R+#i] (raw)", "h[R+#i] (raw)", 4)
FM1_RAW (kSpLoadCraw, "sp_load_craw", "R = b|h[sp+#i] (raw)", "R = b|h[sp+#i] (raw)", 4)
FM1_RAW (kMov2Craw, "mov2_craw", "R_R = R_R (raw)", "R_R = R_R (raw)", 2)
FM1_RAW (kMov2PairCraw, "mov2_craw", "R_R = R_R  #", "R_R = R_R", 2)
FM1_RAW (kClzCraw, "clz_pair_craw", "R = clz(R) (raw)", "R = clz(R) (raw)", 4)
FM1_RAW (kClzPairCraw, "clz_pair_craw", "R = clz(R)  #", "R = clz(R)", 4)
FM1_RAW (kSpWordCraw, "sp_word_craw", "R = [sp+#i] (raw)", "R = [sp+#i] (raw)", 4)
FM1_RAW (kRetsPopMaskCraw, "rets_mask_mov_sp_craw", "{rets, R*4} = [sp++]", "{rets, R*4} = [sp++]", 4)
#undef FM1_RAW

#define FM1_SLOTTED(nm, syn, can, slots) \
    { nm, syn, can, 4, 0u, 0ull, 0ull, nullptr, 0, nullptr, 0, slots, (uint8_t) (sizeof (slots) / sizeof (slots[0])) }

const Fm1IsaEntry kAdcCraw = FM1_SLOTTED ("adc_craw", "R = R + R + c", "R = R + R + c", kCarrySlots);
const Fm1IsaEntry kAdcPairCraw = FM1_SLOTTED ("adc_pair_craw", "R = R + R + c #", "R = R + R + c", kCarrySlots);
const Fm1IsaEntry kSbcCraw = FM1_SLOTTED ("sbc_craw", "R = R - R - !c", "R = R - R - !c", kCarrySlots);
const Fm1IsaEntry kSbcPairCraw = FM1_SLOTTED ("sbc_pair_craw", "R = R - R - !c #", "R = R - R - !c", kCarrySlots);

// 64 e0 00 XY rX = Y y 64 e0 80 XY Y = rX, Y registro especial: 0 reti, 1 rete, 3 rets, 5 psr, 6 cnum, b icfg, c usp,
// d ssp, e sp (cnum no se escribe nunca)
#define FM1_SREG(syn, can) FM1_SLOTTED ("sreg_mov_craw", syn, can, kSregSlots)
const Fm1IsaEntry kSregRead[9] = {
    FM1_SREG ("R = reti", "R = reti"), FM1_SREG ("R = rete", "R = rete"), FM1_SREG ("R = rets", "R = rets"),
    FM1_SREG ("R = psr", "R = psr"), FM1_SREG ("R = cnum", "R = cnum"), FM1_SREG ("R = icfg", "R = icfg"),
    FM1_SREG ("R = usp", "R = usp"), FM1_SREG ("R = ssp", "R = ssp"), FM1_SREG ("R = sp", "R = sp") };
const Fm1IsaEntry kSregWrite[9] = {
    FM1_SREG ("reti = R", "reti = R"), FM1_SREG ("rete = R", "rete = R"), FM1_SREG ("rets = R", "rets = R"),
    FM1_SREG ("psr = R", "psr = R"), FM1_SREG ("cnum = R", "cnum = R"), FM1_SREG ("icfg = R", "icfg = R"),
    FM1_SREG ("usp = R", "usp = R"), FM1_SREG ("ssp = R", "ssp = R"), FM1_SREG ("sp = R", "sp = R") };
const Fm1IsaEntry kSregReadPair[9] = {
    FM1_SREG ("R = reti  #", "R = reti"), FM1_SREG ("R = rete  #", "R = rete"), FM1_SREG ("R = rets  #", "R = rets"),
    FM1_SREG ("R = psr  #", "R = psr"), FM1_SREG ("R = cnum  #", "R = cnum"), FM1_SREG ("R = icfg  #", "R = icfg"),
    FM1_SREG ("R = usp  #", "R = usp"), FM1_SREG ("R = ssp  #", "R = ssp"), FM1_SREG ("R = sp  #", "R = sp") };
const Fm1IsaEntry kSregWritePair[9] = {
    FM1_SREG ("reti = R  #", "reti = R"), FM1_SREG ("rete = R  #", "rete = R"), FM1_SREG ("rets = R  #", "rets = R"),
    FM1_SREG ("psr = R  #", "psr = R"), FM1_SREG ("cnum = R  #", "cnum = R"), FM1_SREG ("icfg = R  #", "icfg = R"),
    FM1_SREG ("usp = R  #", "usp = R"), FM1_SREG ("ssp = R  #", "ssp = R"), FM1_SREG ("sp = R  #", "sp = R") };
#undef FM1_SREG
#undef FM1_SLOTTED

const Fm1IsaEntry* sregMoveRaw (uint64_t win) noexcept
{
    bool pair;
    switch (win & 0xffff) { case 0xe064: pair = false; break; case 0xf064: pair = true; break; default: return nullptr; }
    bool write;
    switch ((win >> 16) & 0xff) { case 0x00: write = false; break; case 0x80: write = true; break; default: return nullptr; }
    int i;
    switch ((win >> 24) & 0xf)
    {
        case 0: i = 0; break;
        case 1: i = 1; break;
        case 3: i = 2; break;
        case 5: i = 3; break;
        case 6: if (write) return nullptr; i = 4; break;
        case 0xb: i = 5; break;
        case 0xc: i = 6; break;
        case 0xd: i = 7; break;
        case 0xe: i = 8; break;
        default: return nullptr;
    }
    return write ? (pair ? &kSregWritePair[i] : &kSregWrite[i]) : (pair ? &kSregReadPair[i] : &kSregRead[i]);
}

// b8 e0 / b8 f0 con bits(16,19) = 0: rD = rA + rB + c; 2: rD = rA - rB - !c (rD bits(28,31), rA bits(20,23),
// rB bits(24,27)); las clases de la tabla solo encajaban con los registros del corpus
const Fm1IsaEntry* carryOpRaw (uint64_t win) noexcept
{
    if ((win & 0xff) != 0xb8) return nullptr;
    bool pair;
    switch ((win >> 8) & 0xff) { case 0xe0: pair = false; break; case 0xf0: pair = true; break; default: return nullptr; }
    switch ((win >> 16) & 0xf)
    {
        case 0:  return pair ? &kAdcPairCraw : &kAdcCraw;
        case 2:  return pair ? &kSbcPairCraw : &kSbcCraw;
        default: return nullptr;
    }
}

// Las formas DSP cuyos formatos fijó el ensamblador del fabricante para todo registro: antes que la tabla
const Fm1IsaEntry* preTableRaw (uint64_t raw) noexcept
{
    const auto b1r = (raw >> 8) & 0xff;
    const bool head = b1r == 0xf0 || b1r == 0xf1 || b1r == 0xf4;
    const auto b1 = head ? (b1r ^ 0x10) : b1r;
    const auto pick = [head] (const Fm1IsaEntry& plain, const Fm1IsaEntry& pair) { return head ? &pair : &plain; };
    const auto b0 = raw & 0xff;
    if (b0 == 0x39 && b1 == 0xe4) return pick (kSsatCraw, kSsatPairCraw);
    if (b0 == 0xde && b1 == 0xe1 && (raw & 0x04100000) == 0) return pick (kPairOpRaw, kPairOpPairRaw);
    if (b0 == 0xdf && b1 == 0xe1 && (raw & 0x001e0000) == 0) return pick (kPairOpRaw, kPairOpPairRaw);
    if (b0 == 0xd4 && b1 == 0xe1 && (raw & 0x1cf00000) == 0) return pick (kPairOpRaw, kPairOpPairRaw);
    if (b0 == 0xdc && b1 == 0xe1 && (raw & 0x10ff0000) == 0) return pick (kPairOpRaw, kPairOpPairRaw);
    if (b0 == 0x20 && b1 == 0xe0 && ((raw >> 24) & 3) == 0) return pick (kByteSetCraw, kByteSetPairCraw);
    if (b0 == 0x70 && b1 == 0xe0 && ((raw >> 16) & 0xff) == 0x80) return pick (kRev8Craw, kRev8PairCraw);
    if (b0 >= 0x78 && b0 <= 0x7b && b1 == 0xe0 && ((raw >> 16) & 0xfe) == 0) return pick (kSat16Craw, kSat16PairCraw);
    if (dspRaw (raw) != Dsp::None) return pick (kDspCraw, kDspPairCraw);
    return nullptr;
}
} // namespace

// ---- decode_any ------------------------------------------------------------------------------------------------------
static const Fm1IsaEntry* decodeAnyUncached (uint64_t win, uint64_t& raw) noexcept
{
    const auto w32 = win & 0xffffffffull;
    raw = w32;
    SpMem spm;
    if (auto* e = carryOpRaw (win)) return e;
    if (spMemRaw (win, spm)) return &kSpLoadCraw;
    if (auto* e = sregMoveRaw (win)) return e;
    if (auto* e = preTableRaw (w32)) return e;
    if (auto* e = decodeWin (win)) { raw = win; return e; }

    const auto b0 = win & 0xff, b1 = (win >> 8) & 0xff, lo16 = win & 0xffff, lo24 = win & 0xffffff;
    if ((b1 == 0xe1 || b1 == 0xf1) && (b0 == 0xd0 || b0 == 0xd8 || b0 == 0xf8 || b0 == 0xfc || b0 == 0xfe || b0 == 0xf6))
    {
        raw = win;  // 64 bits de pares que el corpus nunca mostró (p. ej. r3_r2 >>>= 39)
        return b1 == 0xf1 ? &kPairOpPairRaw : &kPairOpRaw;
    }
    if (lo16 == 0xe8d9) return &kRetsPushMaskCraw;
    if (lo16 == 0xe8d1) return &kRetsPopMaskCraw;
    if (lo16 == 0xe8d5) return &kPcPopMaskCraw;
    if (lo16 == 0xe9d4) return &kSpWordCraw;
    HalfMem hm;
    if (halfMem (w32, hm)) return &kHalfMemCraw;
    if (lo24 == 0x00e180) return &kClzCraw;
    if (lo24 == 0x00f180) return &kClzPairCraw;
    if ((b1 == 0x15 || b1 == 0xd5) && (win & 0x11) == 0) return (win & 0x8000) != 0 ? &kMov2PairCraw : &kMov2Craw;
    if ((lo16 == 0xe53f || lo16 == 0xf53f) && (((win >> 16) & 0xf) == 7 || ((win >> 16) & 0xf) == 8))
        return lo16 == 0xf53f ? &kFpuMacPairCraw : &kFpuMacCraw;
    Alu4 a4;
    if (decodeAlu4 (w32, a4)) return &kAlu4Craw;
    if (b0 >= 0xd0 && b0 <= 0xd7 && b1 == 0xec) return &kWordMemCraw;
    if ((b1 == 0xed && b0 >= 0xd0 && b0 <= 0xd7) || (b1 == 0xee && b0 >= 0xd0 && b0 <= 0xd5)
        || (b1 == 0xec && b0 >= 0xd8 && b0 <= 0xdf && ((win >> 17) & 1) == 0))
        return &kPostIncCraw;
    if (auto* e = sregMoveRaw (win)) return e;
    if ((win & 0xfe) == 0x60 && b1 == 0xff)
    {
        raw = win & 0xffffffffffffull;
        return (win & 1) == 0 ? &kBitTestEqCraw : &kBitTestNeCraw;
    }
    IfBlock ib;
    if (decodeIfBlock (w32, ib) && (b1 != 0xea || (win & 0x80) == 0)) return &kIfBlockCraw;
    if (lo16 == 0xe9d0) return &kPairSpCraw;
    if (lo24 == 0x00e070) return &kRev8Craw;
    if (lo24 == 0x00f070) return &kRev8PairCraw;
    if (lo24 == 0x40e070) return &kRev16Craw;
    if (lo24 == 0x40f070) return &kRev16PairCraw;
    if (lo24 == 0xc0e070) return &kPopcntCraw;
    if (lo24 == 0xc0f070) return &kPopcntPairCraw;
    IdxInc ii;
    if (idxInc (w32, ii)) return &kIdxIncCraw;
    if (b1 == 0xff && (b0 >> 4) == 4 && ((win >> 16) & 0xff) == 0)
    {
        raw = win & 0xffffffffffffull;
        return &kCmpRrBranch6Craw;
    }
    if ((lo16 == 0xe1c4 || lo16 == 0xf1c4) && (win & 0x0e000000) == 0)
        return lo16 == 0xf1c4 ? &kRotPairCraw : &kRotCraw;
    return nullptr;
}

// decode_any con memoria: es una función pura de la ventana, así que guardar el resultado por ventana da lo mismo. La tabla
// se recorre entera en cada fallo (2916 clases), y blockEnd vuelve a decodificar el cuerpo de cada if {} predicado cada vez
// que se ejecuta: sin esta memoria era el 42 % del tiempo del emulador (medido con KoniFm1Run --hostprofile). Mapeo directo de
// 2^12 ventanas por hilo (96 KiB); una colisión solo vuelve a decodificar.
const Fm1IsaEntry* decodeAny (uint64_t win, uint64_t& raw) noexcept
{
    struct Memo { uint64_t win; const Fm1IsaEntry* e; uint64_t raw; };
    constexpr unsigned kBits = 12;
    thread_local Memo memo[1u << kBits] {};
    auto& m = memo[(win * 0x9e3779b97f4a7c15ull) >> (64 - kBits)];
    if (m.e != nullptr && m.win == win)
    {
        raw = m.raw;
        return m.e;
    }
    const auto* e = decodeAnyUncached (win, raw);
    if (e != nullptr)
        m = { win, e, raw };
    return e;
}

// ---- route_of ----------------------------------------------------------------------------------------------------------
static uint16_t routeOf (const Fm1IsaEntry& e, uint64_t raw, bool alu4, bool bittest) noexcept
{
    const sv name (e.name), syn (e.syntax);
    const auto len = e.len;
    const auto b1 = (raw >> 8) & 0xff;
    IdxInc ii;
    uint8_t d8, s8;
    bool sg;
    if (len == 4 && idxInc (raw, ii)) return 32;
    if (len == 4 && sat16Raw (raw, d8, s8, sg)) return 34;
    if (bittest) return 0;
    if (alu4) return 1;
    if (name == "nop" || name == "csync" || name == "sti_r" || name == "cli_r") return 2;
    if (name == "lockclr" || name == "lockset") return 3;
    if (name == "r_mov_i_l2") return 4;
    if (name == "sp_mov_i") return 5;
    if (name == "ssp_mov_i") return 6;
    if (name == "usp_mov_i") return 7;
    if (name == "r_mov_h") return 8;
    if (name == "r_i_or_r" || name == "r_i_and_r") return 9;
    if (name == "r_mov_r_lsl_i" || name == "r_mov_r_lsr_i" || name == "r_mov_r_asr_i") return 10;
    if (len == 4 && name.starts_with ("if_r_") && has (name, "_i_goto_i")
        && (b1 == 0xf8 || b1 == 0xf9 || b1 == 0xfc || b1 == 0xfd || b1 == 0xfe))
        return 11;
    if (len == 6 && name.starts_with ("if") && has (name, "_i_goto_i") && b1 == 0xff && (raw & 0xc0) == 0) return 12;
    {
        uint64_t fam; bool alt; int a, b; int64_t off;
        if (name.starts_with ("if") && has (name, "_r_goto_i") && decodeCmpRrBranch (raw, len, fam, alt, a, b, off)) return 13;
    }
    IfBlock ib;
    if (name.starts_with ("if") && ! has (name, "goto") && len == 4 && decodeIfBlock (raw, ib)) return 14;
    if (len == 2 && b1 == 0x17) return 15;
    if (len == 4 && has (syn, "d[") && ((raw & 0xfff0) == 0xec50 || (raw & 0xffff) == 0xe9d0)) return 16;
    if (len == 4 && (raw & 0xf8) == 0x50 && (b1 == 0xec || b1 == 0xed || b1 == 0xee) && has (syn, "[R+") && ! has (syn, "[R+R"))
        return 17;
    // accesos de palabra con desplazamiento negativo (d4..d7 ec)
    if (len == 4 && (raw & 0xfc) == 0xd4 && b1 == 0xec && has (syn, "[")) return 18;
    if (name == "word_mem_craw") return 18;
    if (len == 4 && (has (syn, "[++R=#i]") || has (syn, "[++R=-#i]")) && (b1 == 0xec || b1 == 0xed || b1 == 0xee)
        && ((raw & 0xf8) == 0x58 || (raw & 0xff) == 0xd0))
        return 18;
    if (len == 4 && (raw & 0xf0) == 0xd0 && (b1 == 0xec || b1 == 0xed || b1 == 0xee) && has (syn, "++=")) return 19;
    if (len == 4 && (raw & 0xff) == 0xd8 && (b1 == 0xec || b1 == 0xed || b1 == 0xee)) return 20;
    {
        const auto b0 = raw & 0xff;
        if (len == 4 && (b1 == 0xe1 || b1 == 0xf1) && (b0 == 0xd0 || b0 == 0xd8 || b0 == 0xf8 || b0 == 0xfc || b0 == 0xfe || b0 == 0xf6)
            && syn.starts_with ('r') && has (syn, "_r"))
            return 21;
    }
    if (name == "pair_op_raw") return 21;
    // (como en fm1-core: la condición de f070 no exige longitud 4)
    if ((len == 4 && (raw & 0xffff) == 0xe070) || (raw & 0xffff) == 0xf070) return 22;
    if (name == "testset_b_r" || name == "testset_r") return 23;
    if (name == "ifeq_goto_i" || name == "ifne_goto_i") return 24;
    if (name == "if_ret_eq_i_goto_i" || name == "if_ret_ne_i_goto_i") return 25;
    if (name == "sp_mov_rets_r_1" || name == "sp_mov_rets_r_2" || name == "sp_mov_rets_r_3" || name == "sp_mov_rets_r_4"
        || name == "sp_mov_rets_r_6" || name == "sp_mov_rets_r_7" || name == "sp_mov_rets_mask_craw")
        return 26;
    if (name == "rets_r_1_mov_sp" || name == "rets_r_2_mov_sp" || name == "rets_r_3_mov_sp" || name == "rets_r_4_mov_sp"
        || name == "rets_r_6_mov_sp" || name == "rets_r_8_mov_sp" || name == "rets_mask_mov_sp_craw")
        return 27;
    if (name == "pc_r_1_mov_sp" || name == "pc_r_2_mov_sp" || name == "pc_r_3_mov_sp" || name == "pc_r_4_mov_sp"
        || name == "pc_r_mov_sp" || name == "pc_mask_mov_sp_craw")
        return 28;
    if (name == "sp_word_craw") return 33;
    if (name == "half_mem_craw") return 35;
    if (name == "clz_pair_craw") return 36;
    if (name == "sp_load_craw") return 37;
    if (name == "mov2_craw") return 38;
    if (name == "ssat_craw") return 39;
    if (name == "dsp_craw") return 41;
    if (name == "byte_set_craw") return 40;
    if (name == "rep_i_r" || name == "rep_i_i") return 29;
    if (name.starts_with ("fpu_")) return kRouteFpu;
    return kRouteSyntax;
}

static void computeFast (const Fm1IsaEntry& e, uint64_t raw, Fm1Pre& p) noexcept;

void computePre (const Fm1IsaEntry& e, uint64_t raw, Fm1Pre& pre) noexcept
{
    pre = {};
    Alu4 a4;
    if (e.len == 4 && decodeAlu4 (raw, a4))
    {
        pre.hasAlu4 = true;
        pre.aluDstMem = a4.dstMem;
        pre.aluDst = a4.dst;
        pre.aluSrc = a4.src;
        pre.aluOp = a4.op;
        pre.aluRhs = a4.rhs;
        pre.aluMemOff = a4.memOff;
    }
    BitTest bt;
    if (sv (e.name).starts_with ("if") && decodeBitTest (raw, e.len, bt))
    {
        pre.hasBitTest = true;
        pre.btReg = bt.reg;
        pre.btMask = bt.mask;
        pre.btNe = bt.ne;
        pre.btOff = bt.off;
    }
    pre.route = routeOf (e, raw, pre.hasAlu4, pre.hasBitTest);
    sv s (e.syntax);
    while (! s.empty() && s.back() == ' ') s.remove_suffix (1);
    pre.pairHead = s.ends_with ('#');
    if (pre.route == kRouteSyntax)
        computeFast (e, raw, pre);
}

// ---- Cpu ----------------------------------------------------------------------------------------------------------------
// La caché de instrucciones es de correspondencia directa sobre el índice de media palabra: 256 K líneas cubren 512 KiB
// de código XIP sin solaparse; el código por debajo de la XIP (RAM) tiene 64 K líneas propias detrás (fm1-core).
static constexpr uint32_t kIcacheBits = 18, kIcacheRamBits = 16;
static constexpr size_t kIcacheLines = ((size_t) 1 << kIcacheBits) + ((size_t) 1 << kIcacheRamBits);

static size_t icacheSlot (uint32_t pc) noexcept
{
    if (pc < 0x02000000u)
        return ((size_t) 1 << kIcacheBits) + ((pc >> 1) & ((1u << kIcacheRamBits) - 1));
    return (pc >> 1) & ((1u << kIcacheBits) - 1);
}

Cpu::Cpu (uint32_t entry) : pc (entry)
{
    predSkips.reserve (kMaxPred);
    irqLevels.reserve (32);
}

bool Cpu::irqReady (uint8_t prio) const noexcept
{
    return ie && (sregStore[0] & 0x100) != 0 && repEnd == 0 && predSkips.empty() && ! pairPending && holdIrq == 0
        && (irqLevels.empty() || (prio > irqLevels.back() && nestOk));
}

void Cpu::interrupt (uint32_t handler, uint8_t prio)
{
    sregStore[1] = pc;
    // Desde usuario la ISR corre en ssp (el sp de usuario espera en usp); desde supervisor sigue en la misma pila
    const auto depth = std::min<size_t> (irqLevels.size(), 31);
    if (user)
    {
        userIrq |= 1u << depth;
        usp = sp;
        sp = ssp;
        user = false;
    }
    else
        userIrq &= ~(1u << depth);
    irqLevels.push_back (prio);
    nestOk = false;
    pc = handler;
}

bool Cpu::plainState (PlainState& o) const noexcept
{
    if (pairPending || ! predSkips.empty() || repEnd != 0 || holdIrq != 0 || ! irqLevels.empty())
        return false;
    o.pc = pc;
    std::memcpy (o.regs, regs, sizeof regs);
    o.sp = sp; o.ssp = ssp; o.usp = usp;
    o.user = user; o.userIrq = userIrq; o.rets = rets;
    o.repStart = repStart; o.repCount = repCount;
    o.cc = cc; o.flags = flags;
    std::memcpy (o.sregStore, sregStore, sizeof sregStore);
    o.ie = ie; o.nestOk = nestOk;
    return true;
}

void Cpu::setPlainState (const PlainState& s) noexcept
{
    pc = s.pc;
    std::memcpy (regs, s.regs, sizeof regs);
    sp = s.sp; ssp = s.ssp; usp = s.usp;
    user = s.user; userIrq = s.userIrq; rets = s.rets;
    repStart = s.repStart; repEnd = 0; repCount = s.repCount;
    predSkips.clear();
    cc = s.cc; flags = s.flags;
    std::memcpy (sregStore, s.sregStore, sizeof sregStore);
    pairPending = false;
    ie = s.ie;
    irqLevels.clear();
    nestOk = s.nestOk;
    holdIrq = 0;
}

int64_t Cpu::skipCountedLoop (Bus& bus, uint32_t room)
{
    if (! fastPath || repEnd != 0 || ! predSkips.empty() || pairPending)
        return 0;
    const auto head = pc;
    // Segunda forma: contador volátil en la RAM, "[A] += 1; R = [A]; if (R < L) goto cabeza" (el delay de GHOULBOX:
    // for (volatile uint32_t i = 0; i < n; i++)). Ni la suma en memoria ni la carga tocan las banderas
    {
        const Line* l0 = fetch (bus, head);
        const Line* l1 = l0 != nullptr ? fetch (bus, head + l0->len) : nullptr;
        const Line* l2 = l1 != nullptr ? fetch (bus, head + l0->len + l1->len) : nullptr;
        if (l0 == nullptr || l1 == nullptr || l2 == nullptr)
        {
            fault = Fault::None;
            return -1;
        }
        const auto &a = l0->pre, &b = l1->pre, &c = l2->pre;
        // la suma: micro-op de almacenamiento (FastStX) o ALU de 4 bytes con destino en memoria (ruta 1; alu() sin banderas)
        int baseReg = -1;  // -1: sp
        uint32_t off = 0;
        bool addOne = false;
        if (a.fast == Fm1Pre::FastStX)
        {
            addOne = a.fcond == Fm1Pre::OpAdd && a.fsrc == Fm1Pre::SrcImm && a.foff == 1 && a.fw == 4
                && (a.fmode == Fm1Pre::MemRImm || a.fmode == Fm1Pre::MemSpImm || a.fmode == Fm1Pre::MemSp);
            baseReg = a.fmode == Fm1Pre::MemRImm ? a.fa : -1;
            off = a.fmode == Fm1Pre::MemSp ? 0 : a.fimm;
        }
        else if (a.fast == Fm1Pre::FastNone && a.route == 1)
        {
            addOne = a.aluDstMem && a.aluOp == Fm1Pre::Add && a.aluRhs.kind == Fm1Pre::RhsImm && a.aluRhs.v == 1;
            baseReg = a.aluSrc;
            off = (uint32_t) a.aluMemOff;
        }
        if (addOne && ! a.pairHead && ! b.pairHead && ! c.pairHead)
        {
            const auto end = head + l0->len + l1->len + l2->len;
            const bool load = b.fast == Fm1Pre::FastLd && b.fw == 4 && ! b.fidx;
            // el salto: micro-op (FastBrR) o comparación entre registros de la ruta 13
            Cond cond = Cond::Eq;
            int ra = -1, rb = -1;
            int64_t boff = 0;
            if (c.fast == Fm1Pre::FastBrR)
            {
                cond = (Cond) c.fcond;
                ra = c.fa;
                rb = c.fb;
                boff = c.foff;
            }
            else if (c.fast == Fm1Pre::FastNone && c.route == 13)
            {
                uint64_t fam;
                bool alt;
                if (! decodeCmpRrBranch (l2->raw, l2->len, fam, alt, ra, rb, boff))
                    return -1;
                cond = condOfFam (fam, alt);
            }
            const bool less = (cond == Cond::LtU || cond == Cond::LtS) && ra == b.fd && rb != b.fd && rb >= 0
                && (uint32_t) ((int64_t) end + boff) == head;
            if (! load || ! less || baseReg == b.fd || (! b.fsp && b.fa == b.fd))
                return -1;
            const auto addr = (baseReg >= 0 ? regs[baseReg] : sp) + off;
            const auto addrLd = (b.fsp ? sp : regs[b.fa]) + b.fimm;
            // solo la RAM de datos (0x01C00000, 1 MiB: kRamBase de Fm1Soc.h): sin efectos al leerla ni al escribirla
            if (addr != addrLd || (addr & 3) != 0 || addr < 0x01c00000u || addr >= 0x01d00000u)
                return -1;
            // vueltas que quedan desde la cabeza: el menor m >= 1 con !(i + m < L), con i el contador actual
            const auto i = bus.read32 (addr), lim = regs[rb];
            const bool sgn = cond == Cond::LtS;
            const int64_t iv = sgn ? (int64_t) (int32_t) i : (int64_t) i, lv = sgn ? (int64_t) (int32_t) lim : (int64_t) lim;
            const uint64_t m = iv + 1 < lv ? (uint64_t) (lv - iv) : 1;
            constexpr uint64_t len3 = 3;
            const auto rounds = std::min<uint64_t> (m - 1, room / len3);  // al menos la última vuelta, en el intérprete
            if (rounds == 0)
                return 0;
            const auto v = i + (uint32_t) rounds;
            bus.write32 (addr, v);
            regs[b.fd] = v;
            insnCount += rounds * len3;
            holdIrq = 0;
            return (int64_t) (rounds * len3);
        }
    }
    int reg = -1;
    uint32_t d = 0, len = 0, at = head;
    for (int i = 0; i < 4; ++i)
    {
        const Line* l = fetch (bus, at);
        if (l == nullptr)
        {
            fault = Fault::None;  // (la instrucción fallará igual al ejecutarla)
            return -1;
        }
        const auto& p = l->pre;
        if (p.pairHead)
            return -1;
        ++len;
        const auto next = at + l->len;
        if (p.fast == Fm1Pre::FastAluI && p.fcond == Fm1Pre::Add && p.fd == p.fa)
        {
            if (reg >= 0)
                return -1;  // una sola suma
            reg = p.fd;
            d = p.fimm;
        }
        else if (p.fast == Fm1Pre::FastBrI && (Cond) p.fcond == Cond::Ne)
        {
            if (reg < 0 || p.fa != reg || (uint32_t) ((int64_t) next + p.foff) != head || d == 0)
                return -1;
            // Vueltas hasta salir desde la cabeza: el menor m >= 1 con R + m·d == C (mod 2^32). Con d = 2^t·u (u impar):
            // hace falta que 2^t divida C - R, y m = ((C - R) >> t)·u^-1 mod 2^(32-t)
            const auto diff = p.fimm - regs[reg];
            const auto t = (unsigned) std::countr_zero (d);
            if (diff == 0 || (diff & ((1u << t) - 1)) != 0)
                return 0;  // no sale en esta vuelta de 2^32 (o ya está en C): el intérprete
            const uint32_t u = d >> t;
            uint32_t inv = u;  // inverso de u módulo 2^32 (Newton: cada paso dobla los bits correctos)
            for (int k = 0; k < 5; ++k)
                inv *= 2u - u * inv;
            const uint64_t mask = (1ull << (32 - t)) - 1;
            const auto m = ((uint64_t) (diff >> t) * inv) & mask;
            const auto rounds = std::min<uint64_t> (m - 1, room / len);  // al menos la última vuelta, en el intérprete
            if (rounds == 0)
                return 0;
            regs[reg] += (uint32_t) (rounds - 1) * d;
            regs[reg] = aluF (Fm1Pre::Add, regs[reg], d);  // la última suma saltada, con sus banderas
            insnCount += rounds * len;
            holdIrq = 0;
            return (int64_t) (rounds * len);
        }
        else if (! (p.route == 2 && p.fast == Fm1Pre::FastNone))
            return -1;  // solo nop / csync en el cuerpo
        at = next;
    }
    return -1;
}

bool Cpu::pcEnter (uint32_t at) noexcept
{
    const bool outside = hasPcWin && ! (pcWin[0][0] <= at && at <= pcWin[0][1]) && ! (pcWin[1][0] <= at && at <= pcWin[1][1]);
    const bool trip = outside && ! pcOut;
    pcOut = outside;
    return trip;
}

// icfg al leerlo: byte bajo, las prioridades de ISR en curso; bit 9, la habilitación global de sti / cli
uint32_t Cpu::icfg() const noexcept
{
    uint32_t active = 0;
    for (auto p : irqLevels)
        active |= 1u << (p & 31);
    return (sregStore[0] & ~0x2ffu) | active | (ie ? 0x200u : 0u);
}

static int sregIndex (sv n) noexcept
{
    if (n == "icfg") return 0;
    if (n == "reti") return 1;
    if (n == "retx") return 2;
    if (n == "rete") return 3;
    if (n == "sspn") return 4;
    if (n == "sr4") return 6;
    return 5;
}

uint32_t Cpu::sreg (const char* name) const noexcept
{
    const sv n (name);
    if (n == "cnum") return coreId;
    if (n == "icfg") return icfg();
    if (n == "psr") return (sregStore[5] & ~0xfu) | flags;
    return sregStore[sregIndex (n)];
}

uint32_t Cpu::sregNamed (const char* n, size_t len) const noexcept
{
    const sv s (n, len);
    if (s == "rets") return rets;
    if (s == "sp") return sp;
    if (s == "ssp") return ssp;
    if (s == "usp") return usp;
    if (s == "cnum") return coreId;
    if (s == "icfg") return icfg();
    if (s == "psr") return (sregStore[5] & ~0xfu) | flags;
    return sregStore[sregIndex (s)];
}

void Cpu::setSreg (const char* n, size_t len, uint32_t val) noexcept
{
    const sv s (n, len);
    if (s == "psr")
        flags = val & 0xf;
    if (s == "icfg")
        return setIcfg (val);  // icfg = R: el bit 9 es la habilitación global, como al leerlo
    sregStore[sregIndex (s)] = val;
}

void Cpu::setSregNamed (const char* n, size_t len, uint32_t val) noexcept
{
    const sv s (n, len);
    if (s == "rets") rets = val;
    else if (s == "sp") sp = val;
    else if (s == "ssp") ssp = val;
    else if (s == "usp") usp = val;
    else setSreg (n, len, val);
}

uint32_t Cpu::aluF (Fm1Pre::AluOp op, uint32_t a, uint32_t b) noexcept
{
    switch (op)
    {
        case Fm1Pre::Add:  return addFlags (a, b, 0, flags);
        case Fm1Pre::Sub:  return addFlags (a, ~b, 1, flags);
        case Fm1Pre::RSub: return addFlags (b, ~a, 1, flags);
        default:           return alu (op, a, b);
    }
}

int64_t Cpu::slot (const Line& l, int i)
{
    if (i >= l.entry->slotCount)
    {
        missingSlot = true;
        return 0;
    }
    return slotValue (l.entry->slots[i], l.raw);
}

bool Cpu::fail (Fault f, const Line* l)
{
    fault = f;
    faultPc = pc;
    faultName = l != nullptr && l->entry != nullptr ? l->entry->name : nullptr;
    faultRaw = l != nullptr ? l->raw : 0;
    return false;
}

Cpu::Line* Cpu::fetch (Bus& bus, uint32_t at)
{
    if (icache.empty())
        icache.resize (kIcacheLines);
    auto& line = icache[icacheSlot (at)];
    const bool hit = line.pc == at && line.entry != nullptr;
    if (hit && at >= 0x02000000u && at < 0x04000000u)
        return &line;  // la XIP no cambia
    const uint64_t win = ((uint64_t) bus.fetch32 (at + 4) << 32) | bus.fetch32 (at);
    if (hit)
    {
        // código en RAM: la línea vale mientras sus propios bytes no cambien
        const uint64_t mask = line.len >= 8 ? ~0ull : (1ull << (8 * line.len)) - 1;
        if (((line.win ^ win) & mask) == 0)
            return &line;
    }
    ++decodes;
    uint64_t raw = 0;
    const auto* e = decodeAny (win, raw);
    if (e == nullptr)
    {
        line.pc = 0xffffffffu;
        line.entry = nullptr;
        fault = Fault::Decode;
        faultPc = at;
        faultName = nullptr;
        faultRaw = win;
        return nullptr;
    }
    line.pc = at;
    line.len = e->len;
    line.win = win;
    line.raw = raw;
    line.entry = e;
    line.ifKnown = false;
    computePre (*e, raw, line.pre);
    return &line;
}

bool Cpu::step (Bus& bus)
{
    fault = Fault::None;
    const auto* l = fetch (bus, pc);
    if (l == nullptr)
        return false;
    // (sin copia: solo fetch reemplaza líneas de la caché, y execute no llama a fetch; blockEnd decodifica aparte)
    const auto& line = *l;
    if (hist != nullptr)
        ++(*hist)[line.entry];
    if (pcHist != nullptr)
        ++(*pcHist)[pc];
    const auto nextPc = pc + line.len;
    if (line.pre.pairHead)
        std::memcpy (pairBefore, regs, sizeof regs);  // solo la cabeza de un par necesita los registros de antes
    const bool pending = pairPending;
    pairPending = false;
    const bool ok = execute (bus, line, nextPc);
    if (pending)
    {
        // confirma los resultados de la cabeza del par que la segunda instrucción no reemplazó
        for (int i = 0; i < 16; ++i)
            if (pairNew[i] != pairOld[i] && regs[i] == pairOld[i])
                regs[i] = pairNew[i];
    }
    if (line.pre.pairHead && ok)
    {
        std::memcpy (pairOld, pairBefore, sizeof regs);
        std::memcpy (pairNew, regs, sizeof regs);
        std::memcpy (regs, pairBefore, sizeof regs);
        pairPending = true;
    }
    return ok;
}

// Dirección justo después de units instrucciones desde pc; un if predicado cuenta como una unidad con sus partes y un
// rep incluye su bloque (como cuentan los nibbles de tamaño de "if (...) {", 2057 bloques del corpus según fm1-emu)
uint32_t Cpu::blockEnd (Bus& bus, uint32_t at, uint32_t units) const
{
    for (uint32_t u = 0; u < units; ++u)
    {
        uint64_t raw = 0;
        const auto* e = decodeAny (((uint64_t) bus.fetch32 (at + 4) << 32) | bus.fetch32 (at), raw);
        if (e == nullptr)
            return at;
        at += e->len;
        sv syn (e->syntax);
        while (! syn.empty() && syn.back() == ' ') syn.remove_suffix (1);
        if (syn.ends_with ('#'))
        {
            // un par de emisión doble (cabeza " #" + la siguiente) es una unidad
            uint64_t raw2 = 0;
            if (const auto* second = decodeAny (((uint64_t) bus.fetch32 (at + 4) << 32) | bus.fetch32 (at), raw2))
                at += second->len;
            continue;
        }
        const sv name (e->name);
        IfBlock b;
        if (e->len == 4 && name.starts_with ("if") && ! has (name, "goto"))
        {
            if (decodeIfBlock (raw, b))
            {
                at = blockEnd (bus, at, b.thenUnits);
                at = blockEnd (bus, at, b.elseUnits);
            }
        }
        else if (name == "rep_i_r" || name == "rep_i_i")
            at += 2 * ((uint32_t) ((raw >> 4) & 0xf) + 1);
    }
    return at;
}

static uint32_t ldW (Bus& bus, uint8_t w, bool sign, uint32_t a)
{
    if (w == 1) return sign ? sx8 (bus.read8 (a)) : bus.read8 (a);
    if (w == 2) return sign ? sx16 (bus.read16 (a)) : bus.read16 (a);
    return bus.read32 (a);
}

static void stW (Bus& bus, uint8_t w, uint32_t a, uint32_t val)
{
    if (w == 1) bus.write8 (a, (uint8_t) val);
    else if (w == 2) bus.write16 (a, (uint16_t) val);
    else bus.write32 (a, val);
}

bool Cpu::execute (Bus& bus, const Line& l, uint32_t nextPc)
{
    const auto raw = l.raw;
    const auto& pre = l.pre;
    bool jumped = false;
    uint32_t target = 0;
    const auto jumpTo = [&] (uint32_t t) { jumped = true; target = t; };
    const auto rhsVal = [this] (Rhs x) -> uint32_t
    {
        switch (x.kind)
        {
            case Fm1Pre::RhsReg: return regs[x.v & 15];
            case Fm1Pre::RhsImm: return x.v;
            default:             return 1u << (regs[x.v & 15] & 31);
        }
    };

    // Ruta rápida: las formas comunes de exec_syntax con sus operandos ya resueltos (misma semántica, ver computeFast)
    if (fastPath && pre.fast != Fm1Pre::FastNone)
    {
        switch (pre.fast)
        {
            case Fm1Pre::FastLd:
            {
                const auto addr = pre.fsp ? sp + pre.fimm : pre.fidx ? regs[pre.fa] + (regs[pre.fb] << pre.fsh) : regs[pre.fa] + pre.fimm;
                regs[pre.fd] = ldW (bus, pre.fw, pre.fsign, addr);
                break;
            }
            case Fm1Pre::FastSt:
            {
                const auto addr = pre.fsp ? sp + pre.fimm : pre.fidx ? regs[pre.fa] + (regs[pre.fb] << pre.fsh) : regs[pre.fa] + pre.fimm;
                stW (bus, pre.fw, addr, regs[pre.fd]);
                break;
            }
            case Fm1Pre::FastBrI:
                if (condEval ((Cond) pre.fcond, regs[pre.fa], pre.fimm))
                {
                    jumpTo ((uint32_t) ((int64_t) nextPc + pre.foff));
                    if (pre.foff < 0 && (Cond) pre.fcond == Cond::Ne)
                        loopHint = target;  // quizá un bucle de retardo contado (skipCountedLoop)
                }
                break;
            case Fm1Pre::FastBrR:
                if (condEval ((Cond) pre.fcond, regs[pre.fa], regs[pre.fb]))
                {
                    jumpTo ((uint32_t) ((int64_t) nextPc + pre.foff));
                    // un salto atrás corto con "<": quizá un retardo con el contador en memoria (skipCountedLoop). Solo
                    // cuerpos de hasta 16 B, para no mirar en cada vuelta los bucles de DSP
                    if (pre.foff < 0 && pre.foff >= -16 && ((Cond) pre.fcond == Cond::LtU || (Cond) pre.fcond == Cond::LtS))
                        loopHint = target;
                }
                break;
            case Fm1Pre::FastAluI: regs[pre.fd] = aluF ((Fm1Pre::AluOp) pre.fcond, regs[pre.fa], pre.fimm); break;
            case Fm1Pre::FastAluR: regs[pre.fd] = aluF ((Fm1Pre::AluOp) pre.fcond, regs[pre.fa], regs[pre.fb]); break;
            case Fm1Pre::FastMov: regs[pre.fd] = regs[pre.fa]; break;
            case Fm1Pre::FastMovI: regs[pre.fd] = pre.fimm; break;
            case Fm1Pre::FastGoto: jumpTo ((uint32_t) ((int64_t) nextPc + pre.foff)); break;
            case Fm1Pre::FastCall: rets = nextPc; jumpTo ((uint32_t) ((int64_t) nextPc + pre.foff)); break;
            case Fm1Pre::FastRts: jumpTo (rets); break;
            case Fm1Pre::FastPart:
            {
                const auto x = regs[pre.fa];
                uint32_t y;
                switch (pre.fsh)
                {
                    case 0:  y = x & 0xff; break;
                    case 1:  y = sx8 (x); break;
                    case 2:  y = x & 0xffff; break;
                    case 3:  y = sx16 (x); break;
                    case 4:  y = x >> 16; break;
                    case 5:  y = sx16 (x >> 16); break;
                    case 6:  y = (x >> 8) & 0xff; break;
                    case 7:  y = (x >> 16) & 0xff; break;
                    default: y = x >> 24; break;
                }
                regs[pre.fd] = y;
                break;
            }
            case Fm1Pre::FastLdX:
            {
                uint32_t addr, inc = 0;
                bool post = false;
                switch (pre.fmode)
                {
                    case Fm1Pre::MemPostImm: addr = regs[pre.fa]; inc = pre.fimm; post = true; break;
                    case Fm1Pre::MemPostR:   addr = regs[pre.fa]; inc = regs[pre.fb]; post = true; break;
                    case Fm1Pre::MemPreImm:  regs[pre.fa] += pre.fimm; addr = regs[pre.fa]; break;
                    default:                 regs[pre.fa] += regs[pre.fb]; addr = regs[pre.fa]; break;
                }
                regs[pre.fd] = ldW (bus, pre.fw, pre.fsign, addr);
                if (post)
                    regs[pre.fa] += inc;
                break;
            }
            case Fm1Pre::FastStX:
            {
                uint32_t addr, inc = 0;
                bool post = false;
                switch (pre.fmode)
                {
                    case Fm1Pre::MemRImm:    addr = regs[pre.fa] + pre.fimm; break;
                    case Fm1Pre::MemSpImm:   addr = sp + pre.fimm; break;
                    case Fm1Pre::MemSp:      addr = sp; break;
                    case Fm1Pre::MemRR:
                    case Fm1Pre::MemRRShl:   addr = regs[pre.fa] + (regs[pre.fb] << pre.fsh); break;
                    case Fm1Pre::MemPostImm: addr = regs[pre.fa]; inc = pre.fimm; post = true; break;
                    case Fm1Pre::MemPostR:   addr = regs[pre.fa]; inc = regs[pre.fb]; post = true; break;
                    case Fm1Pre::MemPreImm:  regs[pre.fa] += pre.fimm; addr = regs[pre.fa]; break;
                    case Fm1Pre::MemPreR:    regs[pre.fa] += regs[pre.fb]; addr = regs[pre.fa]; break;
                    default:                 sp -= 4; addr = sp; break;
                }
                const uint32_t val = pre.fsrc == Fm1Pre::SrcReg ? regs[pre.fd] : pre.fsrc == Fm1Pre::SrcRets ? rets : (uint32_t) pre.foff;
                uint32_t nv;
                switch (pre.fcond)
                {
                    case Fm1Pre::OpSet: nv = val; break;
                    case Fm1Pre::OpOr:  nv = ldW (bus, pre.fw, false, addr) | val; break;
                    case Fm1Pre::OpAnd: nv = ldW (bus, pre.fw, false, addr) & val; break;
                    case Fm1Pre::OpXor: nv = ldW (bus, pre.fw, false, addr) ^ val; break;
                    case Fm1Pre::OpAdd: nv = ldW (bus, pre.fw, false, addr) + val; break;
                    default:            nv = ldW (bus, pre.fw, false, addr) - val; break;
                }
                stW (bus, pre.fw, addr, nv);
                if (post)
                    regs[pre.fa] += inc;
                break;
            }
            case Fm1Pre::FastSpAdd: sp += pre.fimm; break;
            case Fm1Pre::FastSpPlus: regs[pre.fd] = sp + pre.fimm; break;
            case Fm1Pre::FastCnum: regs[pre.fd] = coreId; break;
            case Fm1Pre::FastField:
            {
                const auto src = regs[pre.fa];
                const uint32_t p = pre.fsh, ln = pre.fw;
                const auto mask = ln == 0 ? 0xffffffffu : (1u << ln) - 1;
                if (pre.fcond == 0) regs[pre.fd] = (src >> p) & mask;
                else if (pre.fcond == 1) regs[pre.fd] = ln == 0 ? (src >> p) : (uint32_t) ((int32_t) ((src >> p) << (32 - ln)) >> (32 - ln));
                else regs[pre.fd] = (regs[pre.fd] & ~(mask << p)) | ((src & mask) << p);
                break;
            }
            case Fm1Pre::FastAdc: regs[pre.fd] = addFlags (regs[pre.fa], regs[pre.fb], (flags & kFlagC) != 0 ? 1u : 0u, flags); break;
            case Fm1Pre::FastIe: ie = pre.fimm != 0; break;
            case Fm1Pre::FastIcfgRd: regs[pre.fd] = icfg(); break;
            case Fm1Pre::FastIcfgWr: setIcfg (regs[pre.fd]); break;
            case Fm1Pre::FastAndNot: regs[pre.fd] = regs[pre.fa] & ~regs[pre.fb]; break;
            case Fm1Pre::FastShift:
            {
                const auto x = regs[pre.fa];
                const uint32_t c = pre.fidx ? (regs[pre.fb] & 31) : pre.fimm;
                uint32_t y;
                switch (pre.fcond)
                {
                    case 0:  y = c >= 32 ? 0 : x << c; break;
                    case 1:  y = c >= 32 ? 0 : x >> c; break;
                    case 2:  y = c >= 32 ? (uint32_t) ((int32_t) x >> 31) : (uint32_t) ((int32_t) x >> c); break;
                    default: y = qasl (x, c); break;
                }
                regs[pre.fd] = y;
                break;
            }
            case Fm1Pre::FastDecBr:
                regs[pre.fa] -= 1;
                if (condEval ((Cond) pre.fcond, regs[pre.fa], pre.fimm))
                    jumpTo ((uint32_t) ((int64_t) nextPc + pre.foff));
                break;
            case Fm1Pre::FastPopPc:
            {
                const auto t = bus.read32 (sp);
                sp += 4;
                jumpTo (t);
                break;
            }
            case Fm1Pre::FastNone: break;
        }
        finish (jumped, target, nextPc);
        return true;
    }

    // el nombre solo lo miran unas pocas rutas: strlen en cada instrucción era el 9 % del tiempo (--hostprofile)
    const auto name = [&l] { return sv (l.entry->name); };
    switch (pre.route)
    {
        case 0:
            if (((regs[pre.btReg] & rhsVal (pre.btMask)) != 0) == pre.btNe)
                jumpTo ((uint32_t) ((int64_t) nextPc + pre.btOff));
            break;
        case 1:
        {
            const auto rhs = rhsVal (pre.aluRhs);
            const auto lhs = regs[pre.aluSrc];
            if (! pre.aluDstMem)
            {
                const bool arith = pre.aluOp == Fm1Pre::Add || pre.aluOp == Fm1Pre::Sub || pre.aluOp == Fm1Pre::RSub;
                regs[pre.aluDst] = arith ? aluF (pre.aluOp, lhs, rhs) : alu (pre.aluOp, lhs, rhs);
            }
            else
            {
                const auto addr = (uint32_t) ((int64_t) regs[pre.aluSrc] + pre.aluMemOff);
                bus.write32 (addr, alu (pre.aluOp, bus.read32 (addr), rhs));
            }
            break;
        }
        case 2: break;
        case 3:
            // lockset / lockclr: el cerrojo hardware que comparten los núcleos; si lo tiene el otro, se reintenta
            if (! bus.hwLock (coreId, name() == "lockset"))
                jumpTo (pc);
            break;
        case 4:
        {
            // reg = bits(0,2) | bit7 << 3; imm = concat((8,12), (3,5)); la familia c0 14 es "rN = 0" según el corpus
            const auto rr = (int) ((raw & 7) | (((raw >> 7) & 1) << 3));
            uint32_t imm = 0;
            if (! ((raw & 0xc0) == 0xc0 && ((raw >> 8) & 0xff) == 0x14))
                imm = (uint32_t) ((raw >> 8) & 0x1f) | ((uint32_t) ((raw >> 3) & 7) << 5);
            regs[rr] = imm;
            break;
        }
        case 5: sp = (uint32_t) (raw >> 16); break;
        case 6: ssp = (uint32_t) (raw >> 16); break;
        case 7: usp = (uint32_t) (raw >> 16); break;
        case 8: regs[(raw >> 28) & 0xf] = composedImm ((uint32_t) ((raw >> 24) & 0xf), (uint32_t) ((raw >> 16) & 0xff)); break;
        case 9:
        {
            // [R(base)+off] |=/&= R(src): base = b3 >> 4; src = b3 & 0xf; off = b1
            const auto addr = regs[(raw >> 28) & 0xf] + (uint32_t) ((raw >> 8) & 0xff);
            const auto cur = bus.read32 (addr);
            const auto src = regs[(raw >> 24) & 0xf];
            bus.write32 (addr, has (name(), "and") ? (cur & src) : (cur | src));
            break;
        }
        case 10:
        {
            // R = R <op> #i de 2 bytes (máscara e088): dst (0,2), src (4,6), cuenta (8,12), 0 = 32
            const auto d = (int) (raw & 7);
            auto shc = (uint32_t) ((raw >> 8) & 0x1f);
            if (shc == 0) shc = 32;
            const auto x = regs[(raw >> 4) & 7];
            if (has (name(), "lsr")) regs[d] = shc >= 32 ? 0 : x >> shc;
            else if (has (name(), "asr")) regs[d] = (uint32_t) ((int32_t) x >> std::min<uint32_t> (shc, 31));
            else regs[d] = shc >= 32 ? 0 : x << shc;
            break;
        }
        case 11:
        {
            // if[s] (rN <op> #imm) goto #off de 4 bytes (5302 muestras según fm1-emu): reg bits(0,3); byte1 la familia,
            // bit7 la op; imm = bits(25,31) | bits(4,6) << 7, 10 bits, con signo en ==/!= y en las familias con signo;
            // las sin signo comparan con extensión de ceros (supuesto (fm1-emu): imm >= 512); off = 2·sext9 desde pc + 4
            const auto rv = regs[raw & 0xf];
            const auto fam = (raw >> 8) & 0xff;
            const bool alt = (raw & 0x80) != 0;
            const auto imm10 = (uint32_t) (((raw >> 25) & 0x7f) | (((raw >> 4) & 7) << 7));
            const auto immS = (imm10 & 0x200) != 0 ? (imm10 | ~0x3ffu) : imm10;
            const auto f = (int64_t) (((raw >> 16) & 0xff) | (((raw >> 24) & 1) << 8));
            const auto off = 2 * ((f & 0x100) != 0 ? f - 0x200 : f);
            bool taken;
            if (fam == 0xf8) taken = alt ? rv != immS : rv == immS;
            else if (fam == 0xf9) taken = alt ? rv < imm10 : rv >= imm10;
            else if (fam == 0xfc) taken = alt ? rv <= imm10 : rv > imm10;
            else if (fam == 0xfd) taken = alt ? (int32_t) rv < (int32_t) immS : (int32_t) rv >= (int32_t) immS;
            else if (fam == 0xfe && ! alt) taken = (int32_t) rv > (int32_t) immS;
            else taken = (int32_t) rv <= (int32_t) immS;
            if (taken)
                jumpTo ((uint32_t) ((int64_t) pc + 4 + off));
            break;
        }
        case 12:
        {
            // if[s] (rN <op> #imm) goto #off de 6 bytes (xx ff): bit0 de byte0 = alt, bits(1,3) = familia, bit5 = tipo de
            // inmediato: 0 sext12(bits(16,27)) (ceros en las familias sin signo), 1 compuesto; reg = bits(28,31);
            // off = 2·sext16(bits(32,47))
            const auto b0 = raw & 0xff;
            const auto fam = (b0 >> 1) & 7;
            uint32_t imm;
            if ((b0 & 0x20) != 0)
                imm = composedImm ((uint32_t) ((raw >> 24) & 0xf), (uint32_t) ((raw >> 16) & 0xff));
            else
            {
                imm = (uint32_t) ((raw >> 16) & 0xfff);
                if ((imm & 0x800) != 0 && ! (fam == 1 || fam == 4)) imm |= ~0xfffu;
            }
            if (cmpCond (fam, (b0 & 1) != 0, regs[(raw >> 28) & 0xf], imm))
                jumpTo ((uint32_t) ((int64_t) nextPc + 2 * (int64_t) (int16_t) (uint16_t) (raw >> 32)));
            break;
        }
        case 13:
        {
            uint64_t fam; bool alt; int a, b; int64_t off;
            decodeCmpRrBranch (raw, l.len, fam, alt, a, b, off);
            if (cmpCond (fam, alt, regs[a], regs[b]))
            {
                jumpTo ((uint32_t) ((int64_t) nextPc + off));
                // salto atrás corto con "<": quizá un retardo con el contador en memoria (skipCountedLoop); solo cuerpos de
                // hasta 16 B, para no mirar en cada vuelta los bucles de DSP
                if (off < 0 && off >= -16 && (fam == 1 || fam == 5) && alt)
                    loopHint = target;
            }
            break;
        }
        case 14:
        {
            // bloque predicado if (rA <op> x) { then... } [else { ... }]
            IfBlock blk;
            decodeIfBlock (raw, blk);
            const auto a = regs[blk.reg];
            const auto b = rhsVal (blk.rhs);
            const bool taken = blk.fam == 2 ? (((a & b) != 0) == blk.alt) : cmpCond (blk.fam, blk.alt, a, b);
            uint32_t thenEnd, elseEnd;
            if (l.ifKnown)
            {
                thenEnd = l.ifThenEnd;
                elseEnd = l.ifElseEnd;
            }
            else
            {
                thenEnd = blockEnd (bus, nextPc, blk.thenUnits);
                elseEnd = blockEnd (bus, thenEnd, blk.elseUnits);
                if (l.pc >= 0x02000000u && l.pc < 0x04000000u)  // (en la XIP el código no cambia, como supone fetch)
                {
                    l.ifThenEnd = thenEnd;
                    l.ifElseEnd = elseEnd;
                    l.ifKnown = true;
                }
            }
            if (taken)
            {
                if (blk.elseUnits > 0)
                {
                    std::erase_if (predSkips, [thenEnd] (const PredSkip& p) { return p.thenEnd == thenEnd; });
                    if (predSkips.size() >= kMaxPred)
                        return fail (Fault::PredOverflow, &l);
                    predSkips.push_back ({ thenEnd, elseEnd, nextPc });
                }
            }
            else
                jumpTo (thenEnd);
            break;
        }
        case 15:
        {
            // R = R.b0|.l (u|s) (xx 17): d = bits(0,2), s = bits(4,6), bit3 = con signo, bit7 = media palabra
            const auto x = regs[(raw >> 4) & 7];
            const bool sign = (raw & 8) != 0;
            if ((raw & 0x80) == 0) regs[raw & 7] = sign ? sx8 (x) : (x & 0xff);
            else regs[raw & 7] = sign ? sx16 (x) : (x & 0xffff);
            break;
        }
        case 16: case 18: case 19: case 21: case 22: case 32: case 33: case 34: case 35: case 36: case 37: case 38:
        case 39: case 40: case 41:
            execRaw (bus, (uint8_t) pre.route, (uint32_t) raw);
            break;
        case 27:
            execRaw (bus, l.len == 4 ? kRawPopRetsMask : (uint8_t) 27, (uint32_t) raw);
            break;
        case 17:
        {
            // 50..57 ec/ed/ee: R = [rB+#i] / [rB+#i] = R con variantes b/h y sin actualizar la base. Palabra/media:
            // store = bit16, off = sext10(f & ~1 | bit0 << 8 | bit1 << 9), bit2 = con signo; byte: store = bit1,
            // con signo = bit2, off = bit0 ? sext8(f) : f
            const uint8_t w = ((raw >> 8) & 0xff) == 0xec ? 4 : ((raw >> 8) & 0xff) == 0xed ? 2 : 1;
            const auto d = (int) ((raw >> 28) & 0xf), b = (int) ((raw >> 20) & 0xf);
            const auto f = (uint32_t) (((raw >> 16) & 0xf) | (((raw >> 24) & 0xf) << 4));
            bool store, sign;
            int32_t off;
            if (w == 1)
            {
                off = (raw & 1) != 0 ? (int32_t) f - 256 : (int32_t) f;
                store = ((raw >> 1) & 1) != 0;
                sign = ((raw >> 2) & 1) != 0;
            }
            else
            {
                const auto x = (f & ~1u) | ((uint32_t) (raw & 1) << 8) | ((uint32_t) ((raw >> 1) & 1) << 9);
                off = (x & 0x200) != 0 ? (int32_t) x - 0x400 : (int32_t) x;
                store = ((raw >> 16) & 1) != 0;
                sign = w == 2 && ((raw >> 2) & 1) != 0;
            }
            const auto addr = regs[b] + (uint32_t) off;
            if (store) stW (bus, w, addr, regs[d]);
            else regs[d] = ldW (bus, w, sign, addr);
            break;
        }
        case 20:
        {
            // [rB + rI << s] indexados (d8 ec/ed/ee): dato bits(28,31), base bits(20,23), índice bits(24,27); subop
            // bits(16,19): ec 2 ld, 3 st, a/b con << 2; ed 0 ld u, 1 st, 2 ld s, 8/9/a con << 1; ee 0 ld u, 1 st, 2 ld s
            const auto kind = (raw >> 8) & 0xff;
            const auto sub = (raw >> 16) & 0xf;
            const auto d = (int) ((raw >> 28) & 0xf);
            const auto shift = (sub & 8) != 0 ? (kind == 0xec ? 2 : 1) : 0;
            const auto addr = regs[(raw >> 20) & 0xf] + (regs[(raw >> 24) & 0xf] << shift);
            const auto op = sub & 3;
            if (kind == 0xec && op == 2) regs[d] = bus.read32 (addr);
            else if (kind == 0xec && op == 3) bus.write32 (addr, regs[d]);
            else if (kind == 0xed && op == 0) regs[d] = bus.read16 (addr);
            else if (kind == 0xed && op == 1) bus.write16 (addr, (uint16_t) regs[d]);
            else if (kind == 0xed && op == 2) regs[d] = sx16 (bus.read16 (addr));
            else if (kind == 0xee && op == 0) regs[d] = bus.read8 (addr);
            else if (kind == 0xee && op == 1) bus.write8 (addr, (uint8_t) regs[d]);
            else if (kind == 0xee && op == 2) regs[d] = sx8 (bus.read8 (addr));
            else return fail (Fault::Unsupported, &l);
            break;
        }
        case 23:
        {
            // testset b[rN]: prueba y marca atómica de un byte. Supuesto (fm1-emu): cc = (viejo != 0) ("ocupado"),
            // byte |= 0x80; el SDK espera con "testset; ifeq goto reintento"
            const auto addr = regs[raw & 0xf];
            const auto old = bus.read8 (addr);
            cc = old != 0;
            bus.write8 (addr, (uint8_t) (old | 0x80));
            if (old != 0 && bus.fetch32 (nextPc) == 0xfffde840u)  // 40 e8 fd ff: el cerrojo de byte del SDK
                bus.spin (coreId, addr);
            break;
        }
        case 24:
            // ifeq/ifne goto #off (40/41 e8): off = 2·sext16(bits(16,31))
            if (cc == ((raw & 1) == 0))
                jumpTo ((uint32_t) ((int64_t) nextPc + 2 * (int64_t) (int16_t) (uint16_t) (raw >> 16)));
            break;
        case 25:
        {
            const auto val = (uint32_t) slot (l, 0);
            const bool cond = has (name(), "ne") ? rets != val : rets == val;
            if (cond)
            {
                const auto off = slot (l, 1);
                jumpTo ((uint32_t) ((int64_t) pc + l.len + off));
            }
            break;
        }
        case 26:
            if (l.len == 4)
            {
                // d9 e8 <máscara>: [--sp] = {rets, registros de la máscara}, rets primero (dirección más alta)
                const auto mask = (uint32_t) ((raw >> 16) & 0xffff);
                sp -= 4;
                bus.write32 (sp, rets);
                for (int i = 15; i >= 0; --i)
                    if (((mask >> i) & 1) != 0)
                    {
                        sp -= 4;
                        bus.write32 (sp, regs[i]);
                    }
            }
            else
            {
                // [--sp] = {rets, rN..rB}: n = bits(0,3); lista = rets, [max(n,3)..min(n,4)] descendente
                const auto n = (int) (raw & 0xf);
                const int top = std::max (n, 3), bot = std::min (n, 4);
                sp -= 4;
                bus.write32 (sp, rets);
                for (int i = top; i >= bot; --i)
                {
                    sp -= 4;
                    bus.write32 (sp, regs[i]);
                }
            }
            break;
        case 28:
            if (l.len == 4)
            {
                // d5 e8 <máscara>: {pc, registros} = [sp++], los registros de menor a mayor, luego pc
                const auto mask = (uint32_t) ((raw >> 16) & 0xffff);
                for (int i = 0; i < 16; ++i)
                    if (((mask >> i) & 1) != 0)
                    {
                        regs[i] = bus.read32 (sp);
                        sp += 4;
                    }
            }
            else
            {
                // {pc, rN..rB} = [sp++]: [min(n,4)..max(n,3)] ascendente y luego pc
                const auto n = (int) (raw & 0xf);
                for (int i = std::min (n, 4); i <= std::max (n, 3); ++i)
                {
                    regs[i] = bus.read32 (sp);
                    sp += 4;
                }
            }
            jumpTo (bus.read32 (sp));
            sp += 4;
            break;
        case 29:
        {
            // rep N rR { (03 xx): reg = bits(0,3), N = 2·(bits(4,7) + 1); rep N #k { (8x xx): k = bits(8,12) + 1.
            // El bloque son los N bytes tras la instrucción. La cuenta se fija aquí: la forma registro deja el
            // registro a 0. Una cuenta 0 se salta el bloque (supuesto (fm1-emu), pendiente de confirmar en el aparato).
            const auto n = 2 * ((uint32_t) ((raw >> 4) & 0xf) + 1);
            repStart = nextPc;
            repEnd = nextPc + n;
            if (name() == "rep_i_r")
            {
                repCount = regs[raw & 0xf];
                regs[raw & 0xf] = 0;
            }
            else
                repCount = (uint32_t) ((raw >> 8) & 0x1f) + 1;
            if (repCount == 0)
            {
                jumpTo (repEnd);
                repEnd = 0;
            }
            break;
        }
        case kRouteFpu:
        {
            uint8_t op, d, a, b;
            decodeFpu (raw, op, d, a, b);
            regs[d] = fpuD (op, regs[d], regs[a], regs[b]);
            break;
        }
        case kRouteSyntax:
        {
            if (synHist != nullptr)
                ++(*synHist)[l.entry];
            missingSlot = false;
            bool hasJump = false;
            uint32_t j = 0;
            const bool ok = execSyntax (bus, l, nextPc, hasJump, j);
            if (! ok || missingSlot)
                return fail (Fault::Unsupported, &l);
            if (hasJump)
                jumpTo (j);
            break;
        }
        default:
            return fail (Fault::Unsupported, &l);
    }
    if (missingSlot)
        return fail (Fault::Unsupported, &l);
    finish (jumped, target, nextPc);
    return true;
}

void Cpu::execRaw (Bus& bus, uint8_t route, uint32_t raw32)
{
    const auto raw = (uint64_t) raw32;
    switch (route)
    {
        case 16:
        {
            // pares de 64 bits rH_rL = d[...] / d[...] = rH_rL (decode_pair_mem); el bajo en la dirección menor
            const auto m = decodePairMem (raw);
            const int hi = 2 * m.pair + 1, lo = 2 * m.pair;
            const auto base = m.base >= 0 ? regs[m.base] : sp;
            const auto step = m.index >= 0 ? regs[m.index] : (uint32_t) m.off;
            const auto addr = m.post ? base : base + step;
            if (m.base >= 0 && (m.pre || m.post))
                regs[m.base] = base + step;  // la actualización lee el índice antes de que una carga lo pise
            if (m.store)
            {
                bus.write32 (addr, regs[lo]);
                bus.write32 (addr + 4, regs[hi]);
            }
            else
            {
                regs[lo] = bus.read32 (addr);
                regs[hi] = bus.read32 (addr + 4);
            }
            break;
        }
        case 18: case 19:
        {
            // un registro con actualización de la base: el preincremento antes del acceso, el postincremento después
            const auto m = decodeIncMem (route, raw);
            const auto base = regs[m.b];
            const auto next = base + (uint32_t) m.inc;
            const auto a = m.post ? base : next;
            if (m.pre) regs[m.b] = next;
            if (m.store) stW (bus, m.w, a, regs[m.d]);
            else regs[m.d] = ldW (bus, m.w, m.sign, a);
            if (m.post) regs[m.b] = next;
            break;
        }
        case 35:
        {
            HalfMem m;
            halfMem (raw, m);
            const auto a = regs[m.b] + (uint32_t) m.off;
            if (m.pre) regs[m.b] = a;
            if (m.store) bus.write16 (a, (uint16_t) (regs[m.d] >> (m.flag ? 16 : 0)));
            else
            {
                const auto x = bus.read16 (a);
                regs[m.d] = m.flag ? sx16 (x) : x;
            }
            break;
        }
        case 36: regs[(raw >> 28) & 0xf] = (uint32_t) std::countl_zero (regs[(raw >> 24) & 0xf]); break;
        case 37:
        {
            SpMem m;
            spMemRaw (raw, m);
            const auto a = sp + m.off;
            if (m.store) bus.write16 (a, (uint16_t) regs[m.d]);
            else regs[m.d] = ldW (bus, m.w, m.sign, a);
            break;
        }
        case 38:
        {
            const auto d = (int) (raw & 0xe), s = (int) ((raw >> 4) & 0xe);
            const auto lo = regs[s], hi = regs[s + 1];
            regs[d] = lo;
            regs[d + 1] = hi;
            break;
        }
        case 34:
        {
            uint8_t d, s; bool sign;
            sat16Raw (raw, d, s, sign);
            const auto x = (int32_t) regs[s];
            regs[d] = (uint32_t) (sign ? std::clamp (x, -32768, 32767) : std::clamp (x, 0, 65535));
            break;
        }
        case 33:
        {
            // d4 e9: una palabra en sp + off, carga o store
            const auto rr = (int) ((raw >> 28) & 0xf);
            const auto addr = sp + (uint32_t) ((raw >> 16) & 0xffe);
            if ((raw & 0x10000) != 0) bus.write32 (addr, regs[rr]);
            else regs[rr] = bus.read32 (addr);
            break;
        }
        case 32:
        {
            // [++rB=rI] / [rB++=rI]; una carga en el propio registro base deja la base incrementada (como la ruta 19)
            IdxInc m;
            if (! idxInc (raw, m)) return;
            const auto base = regs[m.b];
            const auto next = base + regs[m.i];
            const auto addr = m.pre ? next : base;
            if (m.store) stW (bus, m.w, addr, m.high ? regs[m.d] >> 16 : regs[m.d]);
            else regs[m.d] = ldW (bus, m.w, m.sign, addr);
            regs[m.b] = next;
            break;
        }
        case 21:
        {
            // operaciones de pares de 64 bits (campos de fm1-core, exec_raw ruta 21): d0/d8 desplazamientos, d4/dc
            // rotaciones, de/df desplazamiento saturado a un registro, f8/fc/fe productos (=, +=, -=), f6 división
            const auto p = (int) ((raw >> 29) & 7);
            const int hi = 2 * p + 1, lo = 2 * p;
            const bool sign = ((raw >> 28) & 1) != 0;
            const auto pairOf = [this] (int q) { return ((uint64_t) regs[2 * q + 1] << 32) | regs[2 * q]; };
            const auto setPair = [this, hi, lo] (uint64_t y) { regs[lo] = (uint32_t) y; regs[hi] = (uint32_t) (y >> 32); };
            const auto nImm = (uint32_t) (((raw >> 16) & 0xf) | (((raw >> 24) & 3) << 4));
            const auto sat = [] (uint64_t x, uint32_t n, bool s) -> uint32_t
            {
                if (s)
                    return (uint32_t) (int32_t) std::clamp<int64_t> ((int64_t) x >> n, INT32_MIN, INT32_MAX);
                return (uint32_t) std::min<uint64_t> (x >> n, UINT32_MAX);
            };
            switch (raw & 0xff)
            {
                case 0xd4: case 0xdc:
                {
                    const auto n = (raw & 0xff) == 0xdc ? (regs[(raw >> 24) & 0xf] & 63) : nImm;
                    setPair (std::rotr (pairOf (p), (int) n));
                    break;
                }
                case 0xdf:
                {
                    const auto x = pairOf ((int) ((raw >> 21) & 7));
                    regs[(raw >> 28) & 0xf] = sat (x, regs[(raw >> 24) & 0xf] & 63, (raw & 0x10000) != 0);
                    break;
                }
                case 0xde:
                    // saturado: solo lo usa el ensamblador DSP (FM1 Open: r3 = r7_r6 >> 29 (s))
                    regs[(raw >> 28) & 0xf] = sat (pairOf ((int) ((raw >> 21) & 7)), nImm, (raw & 0x08000000) != 0);
                    break;
                case 0xd0: case 0xd8:
                {
                    const bool byReg = (raw & 0xff) == 0xd8;
                    const auto n = byReg ? (regs[(raw >> 24) & 0xf] & 63) : nImm;
                    const auto op = byReg ? ((raw >> 16) & 3) : ((raw >> 26) & 3);
                    const auto x = pairOf (p);
                    setPair (op <= 1 ? (x << n) : op == 2 ? (x >> n) : (uint64_t) ((int64_t) x >> n));
                    break;
                }
                case 0xf8: case 0xfc: case 0xfe:
                {
                    const auto a = regs[(raw >> 20) & 0xf], b = regs[(raw >> 24) & 0xf];
                    auto y = sign ? (uint64_t) ((int64_t) (int32_t) a * (int64_t) (int32_t) b) : (uint64_t) a * b;
                    const auto acc = pairOf (p);
                    if ((raw & 0xff) == 0xfc) y = acc + y;
                    else if ((raw & 0xff) == 0xfe) y = acc - y;
                    setPair (y);
                    break;
                }
                default:
                {
                    const auto x = pairOf ((int) ((raw >> 21) & 7));
                    const auto b = regs[(raw >> 24) & 0xf];
                    uint64_t y = 0;
                    if (b != 0)
                    {
                        if (sign)
                        {
                            const auto xs = (int64_t) x, bs = (int64_t) (int32_t) b;
                            y = (xs == INT64_MIN && bs == -1) ? (uint64_t) xs : (uint64_t) (xs / bs);  // wrapping_div
                        }
                        else
                            y = x / b;
                    }
                    setPair (y);
                    break;
                }
            }
            break;
        }
        case 22:
        {
            // 70 e0 00 rev8; 40 rev16 (bytes de cada mitad); 80 revs(rS.l) con signo; c0 popcnt
            const auto x = regs[(raw >> 24) & 0xf];
            uint32_t y;
            switch ((raw >> 22) & 3)
            {
                case 0:  y = bswap32 (x); break;
                case 1:  y = ((x & 0x00ff00ffu) << 8) | ((x >> 8) & 0x00ff00ffu); break;
                case 2:  y = sx16 ((uint32_t) (((x & 0xff) << 8) | ((x >> 8) & 0xff))); break;
                default: y = (uint32_t) std::popcount (x); break;
            }
            regs[(raw >> 28) & 0xf] = y;
            break;
        }
        case 27:
        {
            // {rets, rN..rB} = [sp++] (3n 04): como los pops de pc pero la última restaura rets
            const auto n = (int) (raw & 0xf);
            for (int i = std::min (n, 4); i <= std::max (n, 3); ++i)
            {
                regs[i] = bus.read32 (sp);
                sp += 4;
            }
            rets = bus.read32 (sp);
            sp += 4;
            break;
        }
        case 41:
        {
            const auto d = (int) ((raw >> 28) & 0xf), a = (int) ((raw >> 20) & 0xf), b = (int) ((raw >> 24) & 0xf);
            switch (dspRaw (raw))
            {
                case Dsp::QaslImm:
                {
                    auto n = (uint32_t) (((raw >> 16) & 0xf) | (((raw >> 24) & 1) << 4));
                    regs[d] = qasl (regs[a], n == 0 ? 32 : n);
                    break;
                }
                case Dsp::QaslReg: regs[d] = qasl (regs[a], regs[b] & 31); break;
                case Dsp::Cls:
                {
                    // supuesto (fm1-emu): cls cuenta los bits iguales al signo sin contar el propio signo (clrsb)
                    const auto x = regs[b];
                    regs[d] = (uint32_t) std::countl_zero ((int32_t) x < 0 ? ~x : x) - 1;
                    break;
                }
                case Dsp::MinMax:
                {
                    const auto x = regs[a], y = regs[b];
                    const bool isMin = (raw & 1) != 0, sgn = ((raw >> 16) & 1) != 0;
                    if (! isMin) regs[d] = sgn ? (uint32_t) std::max ((int32_t) x, (int32_t) y) : std::max (x, y);
                    else regs[d] = sgn ? (uint32_t) std::min ((int32_t) x, (int32_t) y) : std::min (x, y);
                    break;
                }
                case Dsp::BrevAdd:
                    // supuesto (fm1-emu): suma con el acarreo de bit 31 a bit 0 (direccionamiento de FFT)
                    regs[d] = brev32 (brev32 (regs[d]) + brev32 (regs[b]));
                    break;
                case Dsp::None: break;
            }
            break;
        }
        case 39:
        {
            // 39 e4|f4 AO DB: rD = rA +|- rB (ssat), resta con bit 19
            const auto a = (int64_t) (int32_t) regs[(raw >> 20) & 0xf], b = (int64_t) (int32_t) regs[(raw >> 24) & 0xf];
            regs[(raw >> 28) & 0xf] = (uint32_t) (int32_t) std::clamp<int64_t> ((raw & 0x80000) != 0 ? a - b : a + b, INT32_MIN, INT32_MAX);
            break;
        }
        case 40:
        {
            // 20 e0 II DN: el byte N (bits 26,27) de rD (bits 28,31) = II
            const auto d = (int) ((raw >> 28) & 0xf);
            const auto k = 8u * (uint32_t) ((raw >> 26) & 3);
            regs[d] = (regs[d] & ~(0xffu << k)) | ((uint32_t) ((raw >> 16) & 0xff) << k);
            break;
        }
        case kRawPopRetsMask:
        {
            // d1 e8 <máscara>: {rets, registros} = [sp++], los registros de menor a mayor y luego rets
            const auto mask = (uint32_t) ((raw >> 16) & 0xffff);
            for (int i = 0; i < 16; ++i)
                if (((mask >> i) & 1) != 0)
                {
                    regs[i] = bus.read32 (sp);
                    sp += 4;
                }
            rets = bus.read32 (sp);
            sp += 4;
            break;
        }
        default: break;
    }
}

// Retira una instrucción: pc siguiente, saltos de bloques predicados y bucles rep
void Cpu::finish (bool hasJump, uint32_t jump, uint32_t nextPc)
{
    ++insnCount;
    holdIrq = 0;
    const auto at = pc;
    pc = hasJump ? jump : nextPc;
    if (! predSkips.empty())
    {
        for (size_t i = 0; i < predSkips.size(); ++i)
            if (predSkips[i].thenEnd == pc)
            {
                pc = predSkips[i].elseEnd;
                predSkips.erase (predSkips.begin() + (std::ptrdiff_t) i);
                break;
            }
        // un salto fuera de la construcción abandona su salto pendiente: más allá de su parte then, o un goto desde
        // dentro de ella hacia atrás y fuera (predLeftBack)
        const auto npc = pc;
        const auto rr = rets;
        std::erase_if (predSkips, [&] (const PredSkip& p)
        {
            return ! (npc <= p.thenEnd && ! (hasJump && p.thenStart <= at && at < p.thenEnd && predLeftBack (npc, p.thenStart, nextPc, rr)));
        });
    }
    if (repEnd != 0 && ! hasJump && pc == repEnd)
    {
        // salió por el final de un bloque rep: descuenta, repite o termina
        --repCount;
        if (repCount != 0)
            pc = repStart;
        else
            repEnd = 0;
    }
}

// ---- exec_syntax --------------------------------------------------------------------------------------------------------
namespace
{
bool isNamedSreg (sv t) noexcept
{
    return t == "psr" || t == "rets" || t == "reti" || t == "retx" || t == "rete" || t == "sr4" || t == "icfg" || t == "sp"
        || t == "ssp" || t == "usp";
}

// "{a, b, ..}" de registros especiales con nombre (hasta 10)
int namedList (sv list, sv out[10]) noexcept
{
    if (! stripPrefix (list, "{") || ! stripSuffix (list, "}"))
        return -1;
    int n = 0;
    while (true)
    {
        const auto i = list.find (", ");
        const auto t = list.substr (0, i);
        if (n == 10 || ! isNamedSreg (t))
            return -1;
        out[n++] = t;
        if (i == sv::npos)
            break;
        list.remove_prefix (i + 2);
    }
    return n;
}

// "rH_rL" literal (r1_r0, r3_r2...)
bool pairOf (sv t, int& h, int& l) noexcept
{
    if (! stripPrefix (t, "r"))
        return false;
    sv a, b;
    if (! split2 (t, "_r", a, b))
        return false;
    const auto num = [] (sv s, int& v)
    {
        if (s.empty() || s.size() > 2) return false;
        v = 0;
        for (char c : s)
        {
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        return v < 16;
    };
    return num (a, h) && num (b, l);
}

bool widthOf (sv t, uint8_t& w, sv& rest) noexcept
{
    rest = t;
    if (stripPrefix (rest, "b[")) { w = 1; return true; }
    if (stripPrefix (rest, "h[")) { w = 2; return true; }
    if (stripPrefix (rest, "[")) { w = 4; return true; }
    return false;
}

bool isSregGet (sv n) noexcept
{
    return n == "rets" || n == "sp" || n == "ssp" || n == "usp" || n == "psr" || n == "icfg" || n == "reti" || n == "retx"
        || n == "rete" || n == "sspn" || n == "cnum";
}
} // namespace

bool Cpu::execSyntax (Bus& bus, const Line& l, uint32_t nextPc, bool& hasJump, uint32_t& jump)
{
    const sv s (l.entry->canon);
    const auto raw = l.raw;
    const auto len = l.len;
    const auto J = [&] (uint32_t t) { hasJump = true; jump = t; return true; };

    // ---- registros especiales a / desde la pila (envoltorios de ISR, prólogos, cambios de contexto): se apilan en el
    // orden impreso (el primero en la dirección más alta) y se desapilan en ascendente asignando la lista al revés
    {
        sv list = s, names[10];
        if (stripPrefix (list, "[--sp] = "))
        {
            const int n = namedList (list, names);
            if (n > 0)
            {
                for (int i = 0; i < n; ++i)
                    if (names[i] == "reti") nestOk = true;
                for (int i = 0; i < n; ++i)
                {
                    const auto val = sregNamed (names[i].data(), names[i].size());
                    sp -= 4;
                    bus.write32 (sp, val);
                }
                return true;
            }
        }
        list = s;
        if (stripSuffix (list, " = [sp++]"))
        {
            const int n = namedList (list, names);
            if (n > 0)
            {
                for (int i = 0; i < n; ++i)
                    if (names[i] == "reti") nestOk = false;
                for (int i = n - 1; i >= 0; --i)
                {
                    const auto val = bus.read32 (sp);
                    sp += 4;
                    setSregNamed (names[i].data(), names[i].size(), val);
                }
                return true;
            }
        }
    }

    // ---- saltos: if[s] (<cond>) goto #i
    if ((s.starts_with ("if (") || s.starts_with ("ifs (")) && s.ends_with (") goto #i"))
    {
        const bool sgn = s.starts_with ("ifs");
        const auto open = s.find ('(');
        sv cond = s.substr (open + 1, s.size() - sv (") goto #i").size() - (open + 1));
        uint32_t lhs, rhs;
        sv op;
        int nslots;
        sv rest = cond;
        if (stripPrefix (rest, "(R & "))
        {
            // (R & x) OP #i: ranuras r, máscara, comparación, desplazamiento
            sv mask, opRhs;
            if (! split2 (rest, ") ", mask, opRhs)) { missingSlot = true; return true; }
            const auto m = mask == "R" ? regs[r (l, 1)] : v (l, 1);
            op = opRhs.substr (0, opRhs.find (' '));
            lhs = regs[r (l, 0)] & m;
            rhs = v (l, 2);
            nslots = 4;
        }
        else if (rest = cond, stripPrefix (rest, "--R "))
        {
            const auto rr = r (l, 0);
            regs[rr] -= 1;
            op = rest.substr (0, rest.find (' '));
            lhs = regs[rr];
            rhs = v (l, 1);
            nslots = 3;
        }
        else if (rest = cond, stripPrefix (rest, "R "))
        {
            const auto sp1 = rest.find (' ');
            op = rest.substr (0, sp1);
            const sv rhsTxt = sp1 == sv::npos ? sv() : rest.substr (sp1 + 1);
            rhs = rhsTxt == "R" ? regs[r (l, 1)] : v (l, 1);
            lhs = regs[r (l, 0)];
            nslots = 3;
        }
        else
            return false;
        bool taken;
        if (op == "==") taken = lhs == rhs;
        else if (op == "!=") taken = lhs != rhs;
        else if (op == "<") taken = sgn ? (int32_t) lhs < (int32_t) rhs : lhs < rhs;
        else if (op == ">") taken = sgn ? (int32_t) lhs > (int32_t) rhs : lhs > rhs;
        else if (op == "<=") taken = sgn ? (int32_t) lhs <= (int32_t) rhs : lhs <= rhs;
        else if (op == ">=") taken = sgn ? (int32_t) lhs >= (int32_t) rhs : lhs >= rhs;
        else return false;
        const auto off = slot (l, nslots - 1);
        if (taken)
            return J ((uint32_t) ((int64_t) nextPc + off));
        return true;
    }

    // ---- cargas: R = <mem> [(u)|(s)]
    {
        sv rhsS = s;
        if (stripPrefix (rhsS, "R = "))
        {
            bool sgn = false;
            if (stripSuffix (rhsS, " (s)")) sgn = true;
            else stripSuffix (rhsS, " (u)");
            uint8_t w;
            sv inner;
            if (widthOf (rhsS, w, inner))
            {
                const auto d = r (l, 0);
                if (! stripSuffix (inner, "]")) { missingSlot = true; return true; }
                uint32_t addr;
                int postB = -1;
                uint32_t postInc = 0;
                if (inner == "R+#i") addr = regs[r (l, 1)] + v (l, 2);
                else if (inner == "sp+#i") addr = sp + v (l, 1);
                else if (inner == "sp") addr = sp;
                else if (inner == "R+R") addr = regs[r (l, 1)] + regs[r (l, 2)];
                else if (inner == "R+R<<#i") addr = regs[r (l, 1)] + (regs[r (l, 2)] << (v (l, 3) & 31));
                else if (inner == "R++=#i") { postB = r (l, 1); addr = regs[postB]; postInc = v (l, 2); }
                else if (inner == "R++=R") { postB = r (l, 1); addr = regs[postB]; postInc = regs[r (l, 2)]; }
                else if (inner == "++R=#i") { const auto b = r (l, 1); regs[b] += v (l, 2); addr = regs[b]; }
                else if (inner == "++R=R") { const auto b = r (l, 1); regs[b] += regs[r (l, 2)]; addr = regs[b]; }
                else return false;
                if (missingSlot) return true;
                regs[d] = ldW (bus, w, sgn, addr);
                if (postB >= 0)
                    regs[postB] += postInc;
                return true;
            }
        }
    }

    // ---- stores / lectura-modificación-escritura: <mem> op= x
    {
        uint8_t w;
        sv inner;
        if (widthOf (s, w, inner))
        {
            sv addrTxt, rest;
            if (split2 (inner, "] ", addrTxt, rest))
            {
                const auto spc = rest.find (' ');
                if (spc == sv::npos) { missingSlot = true; return true; }
                const sv op = rest.substr (0, spc), src = rest.substr (spc + 1);
                // las direcciones desconocidas (movimientos de bloque [R+]...) siguen a los manejadores de abajo
                const bool known = (addrTxt == "R+#i" || addrTxt == "sp+#i" || addrTxt == "sp" || addrTxt == "R+R"
                                    || addrTxt == "R+R<<#i" || addrTxt == "R++=#i" || addrTxt == "R++=R" || addrTxt == "++R=#i"
                                    || addrTxt == "++R=R" || addrTxt == "--sp")
                                && (src == "R" || src == "#i" || src == "#h" || src == "rets");
                if (known)
                {
                    int n = 0, postB = -1;
                    uint32_t addr, postInc = 0;
                    if (addrTxt == "R+#i") { n = 2; addr = regs[r (l, 0)] + v (l, 1); }
                    else if (addrTxt == "sp+#i") { n = 1; addr = sp + v (l, 0); }
                    else if (addrTxt == "sp") addr = sp;
                    else if (addrTxt == "R+R") { n = 2; addr = regs[r (l, 0)] + regs[r (l, 1)]; }
                    else if (addrTxt == "R+R<<#i") { n = 3; addr = regs[r (l, 0)] + (regs[r (l, 1)] << (v (l, 2) & 31)); }
                    else if (addrTxt == "R++=#i") { n = 2; postB = r (l, 0); addr = regs[postB]; postInc = v (l, 1); }
                    else if (addrTxt == "R++=R") { n = 2; postB = r (l, 0); addr = regs[postB]; postInc = regs[r (l, 1)]; }
                    else if (addrTxt == "++R=#i") { n = 2; const auto b = r (l, 0); regs[b] += v (l, 1); addr = regs[b]; }
                    else if (addrTxt == "++R=R") { n = 2; const auto b = r (l, 0); regs[b] += regs[r (l, 1)]; addr = regs[b]; }
                    else { sp -= 4; addr = sp; }
                    uint32_t val;
                    if (src == "R") val = regs[r (l, n)];
                    else if (src == "rets") val = rets;
                    else val = v (l, n);
                    if (missingSlot) return true;
                    uint32_t newv;
                    if (op == "=") newv = val;
                    else if (op == "|=") newv = ldW (bus, w, false, addr) | val;
                    else if (op == "&=") newv = ldW (bus, w, false, addr) & val;
                    else if (op == "^=") newv = ldW (bus, w, false, addr) ^ val;
                    else if (op == "+=") newv = ldW (bus, w, false, addr) + val;
                    else if (op == "-=") newv = ldW (bus, w, false, addr) - val;
                    else return false;
                    stW (bus, w, addr, newv);
                    if (postB >= 0)
                        regs[postB] += postInc;
                    return true;
                }
            }
        }
    }

    // ---- pares de registros: rH_rL = d[...] / d[...] = rH_rL (el bajo en la dirección menor)
    {
        sv lhs, rhs;
        if (split2 (s, " = ", lhs, rhs))
        {
            int hi = 0, lo = 0;
            bool isPair = false, isLoad = false;
            sv mem;
            if (pairOf (lhs, hi, lo)) { isPair = true; isLoad = true; mem = rhs; }
            else if (pairOf (rhs, hi, lo)) { isPair = true; mem = lhs; }
            sv inner = mem;
            if (isPair && stripPrefix (inner, "d[") && stripSuffix (inner, "]"))
            {
                uint32_t addr;
                int postB = -1;
                uint32_t postInc = 0;
                if (inner == "R+#i") addr = regs[r (l, 0)] + v (l, 1);
                else if (inner == "sp+#i") addr = sp + v (l, 0);
                else if (inner == "sp") addr = sp;
                else if (inner == "++R=#i") { const auto b = r (l, 0); regs[b] += v (l, 1); addr = regs[b]; }
                else if (inner == "R++=#i") { postB = r (l, 0); addr = regs[postB]; postInc = v (l, 1); }
                else return false;
                if (missingSlot) return true;
                if (isLoad)
                {
                    regs[lo] = bus.read32 (addr);
                    regs[hi] = bus.read32 (addr + 4);
                }
                else
                {
                    bus.write32 (addr, regs[lo]);
                    bus.write32 (addr + 4, regs[hi]);
                }
                if (postB >= 0)
                    regs[postB] += postInc;
                return true;
            }
        }
    }

    // ---- movimientos de pares / aritmética de 64 bits
    {
        sv lhs, rhs;
        int dh, dl;
        if (split2 (s, " = ", lhs, rhs) && pairOf (lhs, dh, dl))
        {
            int sh, sl;
            if (pairOf (rhs, sh, sl))
            {
                const auto a = regs[sh], b = regs[sl];
                regs[dh] = a;
                regs[dl] = b;
                return true;
            }
            if (rhs == "#i")
            {
                // operandos impresos rH, rL, #i: el inmediato es la ranura 2
                const auto imm = slot (l, 2);
                regs[dl] = (uint32_t) imm;
                regs[dh] = (uint32_t) (imm >> 32);
                return true;
            }
            if (rhs == "R * R (s)" || rhs == "R * R (u)")
            {
                const auto a = regs[r (l, 0)], b = regs[r (l, 1)];
                const auto p = rhs.ends_with ("(s)") ? (uint64_t) ((int64_t) (int32_t) a * (int64_t) (int32_t) b) : (uint64_t) a * b;
                regs[dl] = (uint32_t) p;
                regs[dh] = (uint32_t) (p >> 32);
                return true;
            }
        }
        if (split2 (s, " += ", lhs, rhs) && pairOf (lhs, dh, dl) && (rhs == "R * R (s)" || rhs == "R * R (u)"))
        {
            const auto a = regs[r (l, 0)], b = regs[r (l, 1)];
            const auto p = rhs.ends_with ("(s)") ? (uint64_t) ((int64_t) (int32_t) a * (int64_t) (int32_t) b) : (uint64_t) a * b;
            const auto acc = (((uint64_t) regs[dh] << 32) | regs[dl]) + p;
            regs[dl] = (uint32_t) acc;
            regs[dh] = (uint32_t) (acc >> 32);
            return true;
        }
        // Desplazamiento de un par de 64 bits (">>=" tomado como lógico: pendiente de probar en el aparato según
        // fm1-emu). Se conserva tal cual el emparejamiento de fm1-core, que asocia " >>= #i" al desplazamiento a la
        // izquierda: candidato a nuestras pruebas (las formas d0/d8 e1 van por la ruta 21, no por aquí).
        for (int k = 0; k < 2; ++k)
        {
            sv lh = s;
            const bool left = k == 0;
            if (stripSuffix (lh, k == 0 ? sv (" >>= #i") : sv (" <<= #i")) && pairOf (lh, dh, dl))
            {
                const auto n = v (l, 0) & 63;
                const auto x = ((uint64_t) regs[dh] << 32) | regs[dl];
                const auto y = n == 0 ? x : left ? (x << n) : (x >> n);
                regs[dl] = (uint32_t) y;
                regs[dh] = (uint32_t) (y >> 32);
                return true;
            }
        }
    }

    // ---- movimientos de bloque con base registro (eb 00 / eb 20 ...): base = bits(0,3), máscara de 16 registros en
    // bits(16,31); el registro menor en la dirección menor. [rN++] avanza la base; [-rN] / [--rN]: el bloque acaba en
    // la base. Que [rN+] y [-rN] no actualicen la base: supuesto (fm1-emu).
    {
        bool store, wb, down;
        if (len == 4 && ((raw >> 8) & 0xff) == 0xeb && blockForm (s, store, wb, down))
        {
            const auto base = (int) (raw & 0xf);
            int list[16], n = 0;
            for (int i = 0; i < 16; ++i)
                if (((raw >> (16 + i)) & 1) != 0)
                    list[n++] = i;
            const auto start = down ? regs[base] - 4u * (uint32_t) n : regs[base];
            auto addr = start;
            for (int k = 0; k < n; ++k)
            {
                if (store) bus.write32 (addr, regs[list[k]]);
                else regs[list[k]] = bus.read32 (addr);
                addr += 4;
            }
            if (wb)
                regs[base] = down ? start : addr;
            return true;
        }
    }

    // ---- rotación a la derecha R = R <> #i (c4 e1): d = bits(28,31), s = bits(20,23), n = bits(16,19) | bit24 << 4
    if (s == "R = R <> #i" && len == 4)
    {
        const auto n = (int) (((raw >> 16) & 0xf) | (((raw >> 24) & 1) << 4));
        regs[(raw >> 28) & 0xf] = std::rotr (regs[(raw >> 20) & 0xf], n);
        return true;
    }

    // ---- saltos por tabla, push/pop de bloques sin rets, operaciones de caché
    if (s == "tbb [R]" || s == "tbh [R]")
    {
        // tabla justo después de la instrucción: pc = nextPc + 2·tabla[R] (tbb: bytes, R = índice; tbh: medias
        // palabras, R = 2·índice)
        const auto idx = regs[r (l, 0)];
        const auto e = s.starts_with ("tbb") ? (uint32_t) bus.read8 (nextPc + idx) : (uint32_t) bus.read16 (nextPc + idx);
        return J (nextPc + 2 * e);
    }
    if (s == "[--sp] = {R*2}" || s == "{R*2} = [sp++]" || s == "[--sp] = {R*1}" || s == "{R*1} = [sp++]")
    {
        // 2 bytes 6n 04 / 4n 04 con la lista [max(n,3) .. min(n,4)]; 4 bytes d8 e8 / d4 e8 con máscara en bits 16-31
        int list[16], n = 0;
        if (len == 2)
        {
            const auto m = (int) (raw & 0xf);
            for (int i = std::max (m, 3); i >= std::min (m, 4); --i)
                list[n++] = i;
        }
        else
            for (int i = 15; i >= 0; --i)
                if (((raw >> (16 + i)) & 1) != 0)
                    list[n++] = i;
        if (s.starts_with ("[--sp]"))
            for (int k = 0; k < n; ++k)
            {
                sp -= 4;
                bus.write32 (sp, regs[list[k]]);
            }
        else
            for (int k = n - 1; k >= 0; --k)
            {
                regs[list[k]] = bus.read32 (sp);
                sp += 4;
            }
        return true;
    }
    if (s == "ssync" || s == "btbclr" || s == "iflush [R]" || s == "flush [R]" || s == "flushinv [R]")
        return true;
    if (s == "cc = #i") { cc = v (l, 0) != 0; return true; }
    if (s == "callns R") { rets = nextPc; return J (regs[r (l, 0)]); }
    if (s == "rti")
    {
        // vuelve al modo del que vino la interrupción; sin ninguna en curso, a usuario
        bool toUser = true;
        if (! irqLevels.empty())
        {
            irqLevels.pop_back();
            toUser = ((userIrq >> std::min<size_t> (irqLevels.size(), 31)) & 1) != 0;
        }
        if (toUser && ! user)
        {
            ssp = sp;
            sp = usp;
            user = true;
        }
        nestOk = ! irqLevels.empty();  // de vuelta en una ISR externa: se interrumpió con reti en su pila
        return J (sreg ("reti"));
    }
    if (s == "R = sextra(R, p:#i, l:#i)")
    {
        const auto d = r (l, 0);
        const auto src = regs[r (l, 1)];
        const auto p = v (l, 2) & 31, ln = v (l, 3) & 31;
        regs[d] = ln == 0 ? (src >> p) : (uint32_t) ((int32_t) ((src >> p) << (32 - ln)) >> (32 - ln));
        return true;
    }
    if (s == "R.l = #i" || s == "R.h = #i")
    {
        const auto d = r (l, 0);
        const auto imm = v (l, 1) & 0xffff;
        regs[d] = s.starts_with ("R.l") ? ((regs[d] & 0xffff0000u) | imm) : ((regs[d] & 0xffffu) | (imm << 16));
        return true;
    }
    if (s == "R <<= R" || s == "R >>= R" || s == "R >>>= R" || s == "R <<= #i" || s == "R >>= #i" || s == "R >>>= #i")
    {
        const auto d = r (l, 0);
        const auto n = s.ends_with ('R') ? (regs[r (l, 1)] & 31) : v (l, 1);
        const auto kind = s.substr (2, s.find ('=') - 2);
        const auto x = regs[d];
        if (kind == "<<") regs[d] = n >= 32 ? 0 : x << n;
        else if (kind == ">>") regs[d] = n >= 32 ? 0 : x >> n;
        else regs[d] = (uint32_t) ((int32_t) x >> std::min<uint32_t> (n, 31));
        return true;
    }
    if (s == "R = R & ~R") { const auto d = r (l, 0); regs[d] = regs[r (l, 1)] & ~regs[r (l, 2)]; return true; }
    if (s == "R = R & ~(#i << R)" || s == "R = R | (#i << R)")
    {
        const auto d = r (l, 0);
        const auto a = regs[r (l, 1)];
        const auto m = v (l, 2) << (regs[r (l, 3)] & 31);
        regs[d] = has (s, "~") ? (a & ~m) : (a | m);
        return true;
    }
    if (s == "[R+#i] &= ~(#i << R)" || s == "[R+#i] |= #i << R" || s == "[R+#i] &= ~R" || s == "[R+#i] |= R << #i")
    {
        const auto addr = regs[r (l, 0)] + v (l, 1);
        uint32_t m;
        if (s == "[R+#i] &= ~R") m = regs[r (l, 2)];
        else if (s == "[R+#i] |= R << #i") m = regs[r (l, 2)] << (v (l, 3) & 31);
        else m = v (l, 2) << (regs[r (l, 3)] & 31);
        if (missingSlot) return true;
        const auto cur = bus.read32 (addr);
        bus.write32 (addr, has (s, "&=") ? (cur & ~m) : (cur | m));
        return true;
    }

    // ---- registros especiales: psr (bits bajos = banderas), icfg / reti / retx / rete almacenamiento simple
    {
        sv lhs, rhs;
        if (split2 (s, " = ", lhs, rhs))
        {
            if (lhs == "R" && rhs.find (' ') == sv::npos && rhs != "R" && ! rhs.starts_with ('#') && isSregGet (rhs))
            {
                regs[r (l, 0)] = sregNamed (rhs.data(), rhs.size());
                return true;
            }
            if (lhs != "R" && lhs.find_first_of ("[{._") == sv::npos
                && (rhs == "R" || rhs == "#i" || rhs == "#h" || isSregGet (rhs)))
            {
                uint32_t val;
                if (rhs == "R") val = regs[r (l, 0)];
                else if (rhs.starts_with ('#') && len == 6 && ((raw >> 8) & 0xff) == 0xff)
                    val = (uint32_t) (raw >> 16);  // <sreg> = #imm32 de 6 bytes (e0..ef ff <imm32-le>)
                else if (rhs.starts_with ('#')) val = v (l, 0);
                else val = sregNamed (rhs.data(), rhs.size());
                if (lhs == "rets") rets = val;
                else if (lhs == "sp") sp = val;
                else if (lhs == "ssp") ssp = val;
                else if (lhs == "usp") usp = val;
                else if (lhs == "psr" || lhs == "icfg" || lhs == "reti" || lhs == "retx" || lhs == "rete" || lhs == "sspn")
                    setSreg (lhs.data(), lhs.size(), val);
                else return false;
                return true;
            }
        }
    }

    // ---- campos de bits, min/max, unarias
    if (s == "R <= insert(R, p:#i, l:#i)")
    {
        const auto d = r (l, 0);
        const auto src = regs[r (l, 1)];
        const auto p = v (l, 2) & 31, ln = v (l, 3) & 31;
        const auto mask = ln == 0 ? 0xffffffffu : (1u << ln) - 1;
        regs[d] = (regs[d] & ~(mask << p)) | ((src & mask) << p);
        return true;
    }
    if (s == "R = uextra(R, p:#i, l:#i)")
    {
        const auto d = r (l, 0);
        const auto src = regs[r (l, 1)];
        const auto p = v (l, 2) & 31, ln = v (l, 3) & 31;
        const auto mask = ln == 0 ? 0xffffffffu : (1u << ln) - 1;
        regs[d] = (src >> p) & mask;
        return true;
    }
    if (s == "R = smin(R, R)" || s == "R = smax(R, R)" || s == "R = umin(R, R)" || s == "R = umax(R, R)")
    {
        // e4 y su gemela f4: d bits 28-31, a bits 20-23, b bits 24-27
        int d;
        uint32_t a, b;
        if (len == 4 && (((raw >> 8) & 0xff) == 0xe4 || ((raw >> 8) & 0xff) == 0xf4))
        {
            d = (int) ((raw >> 28) & 0xf);
            a = regs[(raw >> 20) & 0xf];
            b = regs[(raw >> 24) & 0xf];
        }
        else
        {
            d = r (l, 0);
            a = regs[r (l, 1)];
            b = regs[r (l, 2)];
        }
        const auto f = s.substr (4, 4);
        if (f == "smin") regs[d] = (uint32_t) std::min ((int32_t) a, (int32_t) b);
        else if (f == "smax") regs[d] = (uint32_t) std::max ((int32_t) a, (int32_t) b);
        else if (f == "umin") regs[d] = std::min (a, b);
        else regs[d] = std::max (a, b);
        return true;
    }
    if (s == "R = abs(R)")
    {
        const auto d = r (l, 0);
        const auto x = regs[r (l, 1)];
        regs[d] = (int32_t) x < 0 ? 0u - x : x;  // wrapping_abs
        return true;
    }
    if (s == "R = clz(R)") { const auto d = r (l, 0); regs[d] = (uint32_t) std::countl_zero (regs[r (l, 1)]); return true; }
    if (s == "R = rev8(R)") { const auto d = r (l, 0); regs[d] = bswap32 (regs[r (l, 1)]); return true; }
    if (s == "R = R + R + c")
    {
        const auto d = r (l, 0);
        regs[d] = addFlags (regs[r (l, 1)], regs[r (l, 2)], (flags & kFlagC) != 0 ? 1u : 0u, flags);
        return true;
    }
    if (s == "R = R - R - !c")
    {
        const auto d = r (l, 0);
        regs[d] = addFlags (regs[r (l, 1)], ~regs[r (l, 2)], (flags & kFlagC) != 0 ? 1u : 0u, flags);
        return true;
    }
    if (s == "R = R + R (ssat)")
    {
        const auto d = r (l, 0);
        const auto sum = (int64_t) (int32_t) regs[r (l, 1)] + (int64_t) (int32_t) regs[r (l, 2)];
        regs[d] = (uint32_t) (int32_t) std::clamp<int64_t> (sum, INT32_MIN, INT32_MAX);
        return true;
    }

    // ---- todo lo demás
    const auto sh = [] (uint32_t x, uint32_t n, sv kind) -> uint32_t
    {
        if (kind == "<<") return n >= 32 ? 0 : x << n;
        if (kind == "<<<") return qasl (x, n);
        if (kind == ">>") return n >= 32 ? 0 : x >> n;
        return n >= 32 ? (uint32_t) ((int32_t) x >> 31) : (uint32_t) ((int32_t) x >> n);
    };
    if (s == "nop" || s == "csync" || s == "pfetch [R]" || s == "sti R" || s == "cli R" || s == "trigger")
        return true;  // trigger congela el historial de PC de la unidad de traza: aquí no hay traza
    if (s == "idle") { bus.idle (coreId); return true; }
    if (s == "sti") { ie = true; return true; }
    if (s == "cli") { ie = false; return true; }
    if (s == "R = R") { const auto d = r (l, 0); regs[d] = regs[r (l, 1)]; return true; }
    if (s == "R = #i" || s == "R = #h") { const auto d = r (l, 0); regs[d] = v (l, 1); return true; }
    if (s == "R = cnum") { regs[r (l, 0)] = coreId; return true; }
    if (s == "R = sp") { regs[r (l, 0)] = sp; return true; }
    if (s == "R = sp + #i") { const auto d = r (l, 0); regs[d] = sp + v (l, 1); return true; }
    if (s == "sp += #i")
    {
        // f0 e8 <imm> de 4 bytes: sext13(bits(16,28))
        uint32_t imm;
        if (len == 4 && (raw & 0xffff) == 0xe8f0)
        {
            const auto f = (uint32_t) ((raw >> 16) & 0x1fff);
            imm = (f & 0x1000) != 0 ? (f | ~0x1fffu) : f;
        }
        else
            imm = v (l, 0);
        sp += imm;
        return true;
    }
    if (s == "sp = R") { sp = regs[r (l, 0)]; return true; }
    if (s == "R = R + #i" || s == "R = R + #h" || s == "R = R - #i" || s == "R = R & #h" || s == "R = R | #h"
        || s == "R = R ^ #h" || s == "R = R * #h" || s == "R = R & #i" || s == "R = R | #i" || s == "R = R ^ #i")
    {
        const auto d = r (l, 0);
        const auto a = regs[r (l, 1)], b = v (l, 2);
        switch (s[6])
        {
            case '+': regs[d] = aluF (Fm1Pre::Add, a, b); break;
            case '-': regs[d] = aluF (Fm1Pre::Sub, a, b); break;
            case '&': regs[d] = a & b; break;
            case '|': regs[d] = a | b; break;
            case '^': regs[d] = a ^ b; break;
            default:  regs[d] = a * b; break;
        }
        return true;
    }
    if (s == "R = #h - R" || s == "R = #i - R")
    {
        const auto d = r (l, 0);
        const auto a = v (l, 1), b = regs[r (l, 2)];
        regs[d] = aluF (Fm1Pre::Sub, a, b);
        return true;
    }
    if (s == "R = R + R" || s == "R = R - R" || s == "R = R & R" || s == "R = R | R" || s == "R = R ^ R" || s == "R = R * R"
        || s == "R = R / R (s)" || s == "R = R / R (u)" || s == "R = R % R (s)" || s == "R = R % R (u)")
    {
        const auto d = r (l, 0);
        const auto a = regs[r (l, 1)], b = regs[r (l, 2)];
        const bool sgn = s.ends_with ("(s)");
        // división entre 0 = 0 (como fm1-emu); INT32_MIN / -1 da INT32_MIN (wrapping_div) y su resto 0
        const auto sdiv = [] (uint32_t x, uint32_t y) { return ((int32_t) x == INT32_MIN && (int32_t) y == -1) ? x : (uint32_t) ((int32_t) x / (int32_t) y); };
        const auto srem = [] (uint32_t x, uint32_t y) { return ((int32_t) x == INT32_MIN && (int32_t) y == -1) ? 0u : (uint32_t) ((int32_t) x % (int32_t) y); };
        switch (s[6])
        {
            case '+': regs[d] = aluF (Fm1Pre::Add, a, b); break;
            case '-': regs[d] = aluF (Fm1Pre::Sub, a, b); break;
            case '&': regs[d] = a & b; break;
            case '|': regs[d] = a | b; break;
            case '^': regs[d] = a ^ b; break;
            case '*': regs[d] = a * b; break;
            case '/': regs[d] = b == 0 ? 0 : sgn ? sdiv (a, b) : a / b; break;
            default:  regs[d] = b == 0 ? 0 : sgn ? srem (a, b) : a % b; break;
        }
        return true;
    }
    if (s == "R += #i" || s == "R -= #i" || s == "R |= #i" || s == "R &= #i" || s == "R ^= #i" || s == "R *= #i"
        || s == "R += R" || s == "R -= R" || s == "R |= R" || s == "R &= R" || s == "R ^= R" || s == "R *= R")
    {
        const auto d = r (l, 0);
        const auto b = s.ends_with ('R') ? regs[r (l, 1)] : v (l, 1);
        const auto a = regs[d];
        switch (s[2])
        {
            case '+': regs[d] = aluF (Fm1Pre::Add, a, b); break;
            case '-': regs[d] = aluF (Fm1Pre::Sub, a, b); break;
            case '|': regs[d] = a | b; break;
            case '&': regs[d] = a & b; break;
            case '^': regs[d] = a ^ b; break;
            default:  regs[d] = a * b; break;
        }
        return true;
    }
    if (s == "R = R << #i" || s == "R = R >> #i" || s == "R = R >>> #i" || s == "R = R <<< #i" || s == "R = R << R"
        || s == "R = R >> R" || s == "R = R >>> R")
    {
        const auto d = r (l, 0);
        const auto a = regs[r (l, 1)];
        // una cuenta inmediata va de 1 a 32, 32 codificado como 0 (el rango del ensamblador del fabricante)
        uint32_t n;
        if (s.ends_with ('R')) n = regs[r (l, 2)] & 31;
        else { n = v (l, 2); if (n == 0) n = 32; }
        const auto tail = s.substr (6);
        regs[d] = sh (a, n, tail.substr (0, tail.find (' ')));
        return true;
    }
    if (s == "R <<<= R" && len == 2)
    {
        // SD 1a con bit 3: rD <<<= rS, d = bits(0,2), s = bits(4,7)
        const auto d = (int) (raw & 7);
        regs[d] = qasl (regs[d], regs[(raw >> 4) & 0xf] & 31);
        return true;
    }
    if (s == "R = -R") { const auto d = r (l, 0); regs[d] = 0u - regs[r (l, 1)]; return true; }
    if (s == "R = ~R") { const auto d = r (l, 0); regs[d] = ~regs[r (l, 1)]; return true; }
    if (s == "R = R.b0 (u)" || s == "R = R.b0 (s)" || s == "R = R.l (u)" || s == "R = R.l (s)" || s == "R = R.h (u)"
        || s == "R = R.h (s)" || s == "R = R.b1 (u)" || s == "R = R.b2 (u)" || s == "R = R.b3 (u)")
    {
        const auto d = r (l, 0);
        const auto x = regs[r (l, 1)];
        const bool sgn = s.ends_with ("(s)");
        const auto part = s.substr (6, 2);
        if (part == "b0") regs[d] = sgn ? sx8 (x) : (x & 0xff);
        else if (part == "b1") regs[d] = (x >> 8) & 0xff;
        else if (part == "b2") regs[d] = (x >> 16) & 0xff;
        else if (part == "b3") regs[d] = x >> 24;
        else if (part == "l ") regs[d] = sgn ? sx16 (x) : (x & 0xffff);
        else regs[d] = sgn ? sx16 (x >> 16) : (x >> 16);
        return true;
    }
    if (s == "goto #i") return J ((uint32_t) ((int64_t) nextPc + slot (l, 0)));
    if (s == "goto R") return J (regs[r (l, 0)]);
    if (s == "call #i") { rets = nextPc; return J ((uint32_t) ((int64_t) nextPc + slot (l, 0))); }
    if (s == "call R") { const auto t = regs[r (l, 0)]; rets = nextPc; return J (t); }
    if (s == "rts") return J (rets);
    if (s == "pc = [sp++]") { const auto t = bus.read32 (sp); sp += 4; return J (t); }
    if (s == "rets = [sp++]") { rets = bus.read32 (sp); sp += 4; return true; }
    return false;
}

// ---- micro-ops (ruta rápida) --------------------------------------------------------------------------------------------
// Las formas más ejecutadas que van por exec_syntax (perfil de la V15: cargas R = [R+#i], comparar y saltar, sumas), con
// los mismos operandos de las mismas ranuras que exec_syntax y la misma semántica. Una forma sin las ranuras que necesita
// se queda en exec_syntax (que la marca como no soportada). La prueba diferencial (KoniFm1Run --reference) compara la
// ejecución con y sin esta ruta.
static void computeFast (const Fm1IsaEntry& e, uint64_t raw, Fm1Pre& p) noexcept
{
    const sv s (e.canon);
    const int n = e.slotCount;
    const auto sl = [&] (int i) { return slotValue (e.slots[i], raw); };
    const auto rg = [&] (int i) { return (uint8_t) (sl (i) & 0xf); };

    // cargas: R = <mem> [(u)|(s)]
    {
        sv rest = s;
        if (stripPrefix (rest, "R = "))
        {
            bool sgn = false;
            if (stripSuffix (rest, " (s)")) sgn = true;
            else stripSuffix (rest, " (u)");
            uint8_t w;
            sv inner;
            if (widthOf (rest, w, inner) && stripSuffix (inner, "]"))
            {
                if (inner == "R+#i" && n >= 3) { p.fd = rg (0); p.fa = rg (1); p.fimm = (uint32_t) sl (2); }
                else if (inner == "sp+#i" && n >= 2) { p.fd = rg (0); p.fsp = true; p.fimm = (uint32_t) sl (1); }
                else if (inner == "sp" && n >= 1) { p.fd = rg (0); p.fsp = true; p.fimm = 0; }
                else if (inner == "R+R" && n >= 3) { p.fd = rg (0); p.fidx = true; p.fa = rg (1); p.fb = rg (2); p.fsh = 0; }
                else if (inner == "R+R<<#i" && n >= 4) { p.fd = rg (0); p.fidx = true; p.fa = rg (1); p.fb = rg (2); p.fsh = (uint8_t) (sl (3) & 31); }
                else if (n >= 3 && (inner == "R++=#i" || inner == "R++=R" || inner == "++R=#i" || inner == "++R=R"))
                {
                    // con incremento de la base: R++= después de leer (con el incremento de antes), ++R= antes
                    p.fd = rg (0);
                    p.fa = rg (1);
                    if (inner.ends_with ('R')) p.fb = rg (2);
                    else p.fimm = (uint32_t) sl (2);
                    p.fmode = inner == "R++=#i" ? Fm1Pre::MemPostImm : inner == "R++=R" ? Fm1Pre::MemPostR
                            : inner == "++R=#i" ? Fm1Pre::MemPreImm : Fm1Pre::MemPreR;
                    p.fw = w;
                    p.fsign = sgn;
                    p.fast = Fm1Pre::FastLdX;
                    return;
                }
                else return;
                p.fw = w;
                p.fsign = sgn;
                p.fast = Fm1Pre::FastLd;
                return;
            }
        }
    }
    // stores simples: <mem> = R
    {
        uint8_t w;
        sv inner, addrTxt, rest;
        // (otros direccionamientos: el bloque general de abajo)
        if (widthOf (s, w, inner) && split2 (inner, "] ", addrTxt, rest) && rest == "= R"
            && (addrTxt == "R+#i" || addrTxt == "sp+#i" || addrTxt == "sp" || addrTxt == "R+R" || addrTxt == "R+R<<#i"))
        {
            if (addrTxt == "R+#i" && n >= 3) { p.fa = rg (0); p.fimm = (uint32_t) sl (1); p.fd = rg (2); }
            else if (addrTxt == "sp+#i" && n >= 2) { p.fsp = true; p.fimm = (uint32_t) sl (0); p.fd = rg (1); }
            else if (addrTxt == "sp" && n >= 1) { p.fsp = true; p.fimm = 0; p.fd = rg (0); }
            else if (addrTxt == "R+R" && n >= 3) { p.fidx = true; p.fa = rg (0); p.fb = rg (1); p.fsh = 0; p.fd = rg (2); }
            else if (addrTxt == "R+R<<#i" && n >= 4) { p.fidx = true; p.fa = rg (0); p.fb = rg (1); p.fsh = (uint8_t) (sl (2) & 31); p.fd = rg (3); }
            else return;  // (le falta una ranura)
            p.fw = w;
            p.fast = Fm1Pre::FastSt;
            return;
        }
    }
    // el resto de <mem> op= x: los direccionamientos y fuentes que exec_syntax conoce ("known"), en su mismo orden
    {
        uint8_t w;
        sv inner, addrTxt, rest;
        if (widthOf (s, w, inner) && split2 (inner, "] ", addrTxt, rest))
        {
            const auto spc = rest.find (' ');
            if (spc == sv::npos)
                return;
            const sv op = rest.substr (0, spc), src = rest.substr (spc + 1);
            int k;  // ranuras de la dirección
            Fm1Pre::MemMode mode;
            if (addrTxt == "R+#i") { k = 2; mode = Fm1Pre::MemRImm; }
            else if (addrTxt == "sp+#i") { k = 1; mode = Fm1Pre::MemSpImm; }
            else if (addrTxt == "sp") { k = 0; mode = Fm1Pre::MemSp; }
            else if (addrTxt == "R+R") { k = 2; mode = Fm1Pre::MemRR; }
            else if (addrTxt == "R+R<<#i") { k = 3; mode = Fm1Pre::MemRRShl; }
            else if (addrTxt == "R++=#i") { k = 2; mode = Fm1Pre::MemPostImm; }
            else if (addrTxt == "R++=R") { k = 2; mode = Fm1Pre::MemPostR; }
            else if (addrTxt == "++R=#i") { k = 2; mode = Fm1Pre::MemPreImm; }
            else if (addrTxt == "++R=R") { k = 2; mode = Fm1Pre::MemPreR; }
            else if (addrTxt == "--sp") { k = 0; mode = Fm1Pre::MemPushSp; }
            else return;
            Fm1Pre::MemSrc from;
            if (src == "R") from = Fm1Pre::SrcReg;
            else if (src == "#i" || src == "#h") from = Fm1Pre::SrcImm;
            else if (src == "rets") from = Fm1Pre::SrcRets;
            else return;
            Fm1Pre::MemOp mop;
            if (op == "=") mop = Fm1Pre::OpSet;
            else if (op == "|=") mop = Fm1Pre::OpOr;
            else if (op == "&=") mop = Fm1Pre::OpAnd;
            else if (op == "^=") mop = Fm1Pre::OpXor;
            else if (op == "+=") mop = Fm1Pre::OpAdd;
            else if (op == "-=") mop = Fm1Pre::OpSub;
            else return;
            if (n < k + (from == Fm1Pre::SrcRets ? 0 : 1))
                return;  // (le falta una ranura: exec_syntax falla)
            switch (mode)
            {
                case Fm1Pre::MemRImm:    p.fa = rg (0); p.fimm = (uint32_t) sl (1); break;
                case Fm1Pre::MemSpImm:   p.fimm = (uint32_t) sl (0); break;
                case Fm1Pre::MemRR:      p.fa = rg (0); p.fb = rg (1); p.fsh = 0; break;
                case Fm1Pre::MemRRShl:   p.fa = rg (0); p.fb = rg (1); p.fsh = (uint8_t) (sl (2) & 31); break;
                case Fm1Pre::MemPostImm:
                case Fm1Pre::MemPreImm:  p.fa = rg (0); p.fimm = (uint32_t) sl (1); break;
                case Fm1Pre::MemPostR:
                case Fm1Pre::MemPreR:    p.fa = rg (0); p.fb = rg (1); break;
                default: break;
            }
            if (from == Fm1Pre::SrcReg) p.fd = rg (k);
            else if (from == Fm1Pre::SrcImm) p.foff = (int64_t) (uint32_t) sl (k);
            p.fmode = mode;
            p.fsrc = from;
            p.fcond = mop;
            p.fw = w;
            p.fast = Fm1Pre::FastStX;
            return;
        }
    }
    // salto con decremento: if[s] (--R <op> #i) goto #i (el registro baja antes de comparar)
    if ((s.starts_with ("if (--R ") || s.starts_with ("ifs (--R ")) && s.ends_with (") goto #i") && n >= 3)
    {
        const bool sgn = s.starts_with ("ifs");
        sv cond = s.substr (s.find ("--R ") + 4);
        cond.remove_suffix (sv (") goto #i").size());
        const sv op = cond.substr (0, cond.find (' '));
        if (cond.substr (op.size()) != " #i")
            return;
        Cond c;
        if (op == "==") c = Cond::Eq;
        else if (op == "!=") c = Cond::Ne;
        else if (op == "<") c = sgn ? Cond::LtS : Cond::LtU;
        else if (op == ">") c = sgn ? Cond::GtS : Cond::GtU;
        else if (op == "<=") c = sgn ? Cond::LeS : Cond::LeU;
        else if (op == ">=") c = sgn ? Cond::GeS : Cond::GeU;
        else return;
        p.fcond = (uint8_t) c;
        p.fa = rg (0);
        p.fimm = (uint32_t) sl (1);
        p.foff = sl (2);
        p.fast = Fm1Pre::FastDecBr;
        return;
    }
    // saltos: if[s] (R <op> R|#x) goto #i
    if ((s.starts_with ("if (R ") || s.starts_with ("ifs (R ")) && s.ends_with (") goto #i") && n >= 3)
    {
        const bool sgn = s.starts_with ("ifs");
        sv cond = s.substr (s.find ('(') + 3, s.size() - sv (") goto #i").size() - (s.find ('(') + 3));
        const auto sp1 = cond.find (' ');
        if (sp1 == sv::npos)
            return;
        const sv op = cond.substr (0, sp1), rhs = cond.substr (sp1 + 1);
        Cond c;
        if (op == "==") c = Cond::Eq;
        else if (op == "!=") c = Cond::Ne;
        else if (op == "<") c = sgn ? Cond::LtS : Cond::LtU;
        else if (op == ">") c = sgn ? Cond::GtS : Cond::GtU;
        else if (op == "<=") c = sgn ? Cond::LeS : Cond::LeU;
        else if (op == ">=") c = sgn ? Cond::GeS : Cond::GeU;
        else return;
        p.fcond = (uint8_t) c;
        p.fa = rg (0);
        if (rhs == "R") { p.fb = rg (1); p.fast = Fm1Pre::FastBrR; }
        else { p.fimm = (uint32_t) sl (1); p.fast = Fm1Pre::FastBrI; }
        p.foff = sl (2);
        return;
    }
    const auto aluOf = [] (char c, Fm1Pre::AluOp& op)
    {
        switch (c)
        {
            case '+': op = Fm1Pre::Add; return true;
            case '-': op = Fm1Pre::Sub; return true;
            case '&': op = Fm1Pre::And; return true;
            case '|': op = Fm1Pre::Or; return true;
            case '^': op = Fm1Pre::Xor; return true;
            case '*': op = Fm1Pre::Mul; return true;
            default: return false;
        }
    };
    Fm1Pre::AluOp op;
    // R op= #i / R op= R: exactamente las formas de exec_syntax (+= -= |= &= ^= *=, sin #h)
    if ((s.size() == 6 || s.size() == 7) && s.starts_with ("R ") && s[3] == '=' && s[4] == ' ' && aluOf (s[2], op) && n >= 2)
    {
        const sv rhs = s.substr (5);
        if (rhs == "#i") { p.fd = p.fa = rg (0); p.fimm = (uint32_t) sl (1); p.fast = Fm1Pre::FastAluI; }
        else if (rhs == "R") { p.fd = p.fa = rg (0); p.fb = rg (1); p.fast = Fm1Pre::FastAluR; }
        else return;
        p.fcond = (uint8_t) op;
        return;
    }
    // R = R op #x: la lista de exec_syntax
    static const char* const aluImm[] = { "R = R + #i", "R = R + #h", "R = R - #i", "R = R & #h", "R = R | #h", "R = R ^ #h",
                                          "R = R * #h", "R = R & #i", "R = R | #i", "R = R ^ #i" };
    for (const auto* f : aluImm)
        if (s == f && n >= 3 && aluOf (s[6], op))
        {
            p.fd = rg (0);
            p.fa = rg (1);
            p.fimm = (uint32_t) sl (2);
            p.fcond = (uint8_t) op;
            p.fast = Fm1Pre::FastAluI;
            return;
        }
    // R = R op R (sin divisiones ni restos)
    static const char* const aluReg[] = { "R = R + R", "R = R - R", "R = R & R", "R = R | R", "R = R ^ R", "R = R * R" };
    for (const auto* f : aluReg)
        if (s == f && n >= 3 && aluOf (s[6], op))
        {
            p.fd = rg (0);
            p.fa = rg (1);
            p.fb = rg (2);
            p.fcond = (uint8_t) op;
            p.fast = Fm1Pre::FastAluR;
            return;
        }
    if (s == "R = R" && n >= 2) { p.fd = rg (0); p.fa = rg (1); p.fast = Fm1Pre::FastMov; return; }
    if ((s == "R = #i" || s == "R = #h") && n >= 2) { p.fd = rg (0); p.fimm = (uint32_t) sl (1); p.fast = Fm1Pre::FastMovI; return; }
    if (s == "goto #i" && n >= 1) { p.foff = sl (0); p.fast = Fm1Pre::FastGoto; return; }
    if (s == "call #i" && n >= 1) { p.foff = sl (0); p.fast = Fm1Pre::FastCall; return; }
    if (s == "rts") { p.fast = Fm1Pre::FastRts; return; }
    static const char* const parts[] = { "R = R.b0 (u)", "R = R.b0 (s)", "R = R.l (u)", "R = R.l (s)", "R = R.h (u)", "R = R.h (s)",
                                         "R = R.b1 (u)", "R = R.b2 (u)", "R = R.b3 (u)" };
    for (uint8_t k = 0; k < 9; ++k)
        if (s == parts[k] && n >= 2)
        {
            p.fd = rg (0);
            p.fa = rg (1);
            p.fsh = k;
            p.fast = Fm1Pre::FastPart;
            return;
        }
    // segunda tanda: la cola de exec_syntax con la V15 (sp += #i, R = cnum, campos de bits, desplazamientos...)
    if (s == "R = cnum" && n >= 1) { p.fd = rg (0); p.fast = Fm1Pre::FastCnum; return; }
    // icfg: la guarda de interrupciones de GHOULBOX (motion_guard) la lee y la escribe millones de veces por segundo
    if (s == "R = icfg" && n >= 1) { p.fd = rg (0); p.fast = Fm1Pre::FastIcfgRd; return; }
    if (s == "icfg = R" && n >= 1) { p.fd = rg (0); p.fast = Fm1Pre::FastIcfgWr; return; }
    if (s == "sp += #i")
    {
        if (e.len == 4 && (raw & 0xffff) == 0xe8f0)
        {
            const auto f = (uint32_t) ((raw >> 16) & 0x1fff);  // f0 e8 <imm> de 4 bytes: sext13(bits(16,28))
            p.fimm = (f & 0x1000) != 0 ? (f | ~0x1fffu) : f;
        }
        else if (n >= 1)
            p.fimm = (uint32_t) sl (0);
        else
            return;
        p.fast = Fm1Pre::FastSpAdd;
        return;
    }
    if (s == "R = sp + #i" && n >= 2) { p.fd = rg (0); p.fimm = (uint32_t) sl (1); p.fast = Fm1Pre::FastSpPlus; return; }
    if ((s == "R = uextra(R, p:#i, l:#i)" || s == "R = sextra(R, p:#i, l:#i)" || s == "R <= insert(R, p:#i, l:#i)") && n >= 4)
    {
        p.fd = rg (0);
        p.fa = rg (1);
        p.fsh = (uint8_t) (sl (2) & 31);
        p.fw = (uint8_t) (sl (3) & 31);
        p.fcond = s[4] == 'u' ? 0 : s[4] == 's' ? 1 : 2;
        p.fast = Fm1Pre::FastField;
        return;
    }
    if (s == "R = R + R + c" && n >= 3) { p.fd = rg (0); p.fa = rg (1); p.fb = rg (2); p.fast = Fm1Pre::FastAdc; return; }
    if (s == "sti" || s == "cli") { p.fimm = s == "sti" ? 1u : 0u; p.fast = Fm1Pre::FastIe; return; }
    if (s == "R = R & ~R" && n >= 3) { p.fd = rg (0); p.fa = rg (1); p.fb = rg (2); p.fast = Fm1Pre::FastAndNot; return; }
    if ((s == "R = R << #i" || s == "R = R >> #i" || s == "R = R >>> #i" || s == "R = R <<< #i" || s == "R = R << R"
         || s == "R = R >> R" || s == "R = R >>> R") && n >= 3)
    {
        p.fd = rg (0);
        p.fa = rg (1);
        if (s.ends_with ('R')) { p.fidx = true; p.fb = rg (2); }
        else { p.fimm = (uint32_t) sl (2); if (p.fimm == 0) p.fimm = 32; }  // la cuenta inmediata 0 es 32
        const sv kind = s.substr (6, s.find (' ', 6) - 6);
        p.fcond = kind == "<<" ? 0 : kind == ">>" ? 1 : kind == ">>>" ? 2 : 3;
        p.fast = Fm1Pre::FastShift;
        return;
    }
    if (s == "pc = [sp++]") { p.fast = Fm1Pre::FastPopPc; return; }
}
} // namespace fm1

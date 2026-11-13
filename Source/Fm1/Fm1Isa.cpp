// Port a C++ del decodificador de fm1-emu (crate fm1-isa, lib.rs; Joakim Eriksson, licencia MIT). Ver Fm1Isa.h.
#include "Fm1Isa.h"

#include <cstring>

namespace fm1
{
#include "Fm1IsaTable.inc"

template <typename T, size_t N> static constexpr size_t countOf (const T (&)[N]) noexcept { return N; }

uint32_t composedImm (uint32_t code, uint32_t m) noexcept
{
    switch (code)
    {
        case 0: return m;
        case 1: return m * 0x00010001u;
        case 2: return (m << 8) * 0x00010001u;
        case 3: return m * 0x01010101u;
        default:
        {
            const auto e = (code << 1) | (m >> 7);
            return (0x80u | (m & 0x7fu)) << (32 - e);
        }
    }
}

const Fm1SlotSpec& slotOf (const Fm1IsaEntry& e, int i) noexcept { return e.slots[i]; }
const char* altOf (const Fm1IsaEntry& e, int i) noexcept { return e.alts[i]; }

uint32_t slotConcat (const Fm1SlotSpec& s, uint64_t raw) noexcept
{
    uint32_t r = 0;
    int bits = 0;
    for (int p = 0; p < s.numPieces; ++p)
    {
        const int lsb = s.pieces[p][0], w = s.pieces[p][1] - lsb + 1;
        r |= (uint32_t) ((raw >> lsb) & ((1ull << w) - 1)) << bits;
        bits += w;
    }
    return r;
}

int slotBits (const Fm1SlotSpec& s) noexcept
{
    int b = 0;
    for (int p = 0; p < s.numPieces; ++p)
        b += s.pieces[p][1] - s.pieces[p][0] + 1;
    return b;
}

// Extensión de signo de los bits bajos (bits de 1 a 32)
static int64_t sext (uint32_t v, int bits) noexcept
{
    v &= bits >= 32 ? 0xffffffffu : ((1u << bits) - 1);
    if (bits < 32 && (v & (1u << (bits - 1))) != 0)
        return (int64_t) v - ((int64_t) 1 << bits);
    return (int64_t) v;
}

int64_t slotValue (const Fm1SlotSpec& s, uint64_t raw) noexcept
{
    const auto c = slotConcat (s, raw);
    switch ((SlotKind) s.kind)
    {
        case SlotKind::Imm:
        case SlotKind::Blk:       return (int64_t) c;
        case SlotKind::Adj:       return (int64_t) c - s.adj;
        case SlotKind::Scaled:    return (int64_t) s.mult * (int64_t) c - s.adj;
        case SlotKind::Const:     return s.adj;
        case SlotKind::Zero32:    return c == 0 ? 32 : (int64_t) c;
        case SlotKind::Bit:       return (int64_t) 1 << (c & 31);
        case SlotKind::Composed:  return (int64_t) composedImm ((uint32_t) (raw >> 24) & 0xf, (uint32_t) (raw >> 16) & 0xff);
        case SlotKind::NComposed: return (int64_t) (uint32_t) ~composedImm ((uint32_t) (raw >> 24) & 0xf, (uint32_t) (raw >> 16) & 0xff);
        case SlotKind::NBit:      return (int64_t) (uint32_t) ~(1u << (c & 31));
        case SlotKind::Rel:
        {
            // Como fm1-emu: máscara de min(bits, 31) bits y extensión de signo sobre bits
            const auto w = slotBits (s);
            const uint32_t m = w >= 31 ? 0x7fffffffu : (1u << w) - 1;
            return (int64_t) s.mult * sext (c & m, w);
        }
    }
    return 0;
}

int slotBlockRegs (const Fm1SlotSpec& s, uint64_t raw, uint8_t out[32]) noexcept
{
    const auto n = slotConcat (s, raw);
    const auto top = n > s.hi ? n : (uint32_t) s.hi;
    const auto bottom = n < s.lo ? n : (uint32_t) s.lo;
    if (top < bottom)
        return 0;
    int k = 0;
    for (auto r = top; r + 1 > bottom && k < 32; --r)
    {
        out[k++] = (uint8_t) r;
        if (r == 0) break;
    }
    return k;
}

bool hasSyntax (const Fm1IsaEntry& e, const char* form) noexcept
{
    if (std::strcmp (e.syntax, form) == 0)
        return true;
    for (int i = 0; i < e.altCount; ++i)
        if (std::strcmp (altOf (e, i), form) == 0)
            return true;
    return false;
}

const Fm1IsaEntry* decodeWin (uint64_t win) noexcept
{
    for (const auto& e : kFpu)
        if ((win & e.mask) == e.match)
            return &e;
    const Fm1IsaEntry* best = nullptr;
    for (const auto& e : kIsa)
    {
        if ((win & e.mask) != e.match)
            continue;
        // El desempate por muestra compara solo los bytes de la longitud de la entrada (las muestras se guardan como
        // enteros de len bytes; la ventana es siempre de 6)
        const auto obs = win & ((1ull << (e.len * 8u)) - 1);
        for (uint32_t i = 0; i < e.sampleCount; ++i)
            if (e.samples[i] == obs)
                return &e;
        if (best == nullptr)
            best = &e;
    }
    if (best == nullptr)
        for (const auto& e : kFallback)
            if ((win & e.mask) == e.match)
                return &e;
    return best;
}

const Fm1IsaEntry* decodeBytes (const uint8_t* bytes, size_t n) noexcept
{
    uint64_t win = 0;
    for (size_t i = 0; i < n && i < 6; ++i)
        win |= (uint64_t) bytes[i] << (8 * i);
    return decodeWin (win);
}

size_t entryCount() noexcept { return countOf (kIsa); }

const Fm1IsaEntry* findEntry (const char* name) noexcept
{
    for (const auto& e : kFpu)      if (std::strcmp (e.name, name) == 0) return &e;
    for (const auto& e : kIsa)      if (std::strcmp (e.name, name) == 0) return &e;
    for (const auto& e : kFallback) if (std::strcmp (e.name, name) == 0) return &e;
    return nullptr;
}
} // namespace fm1

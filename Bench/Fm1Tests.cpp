// Pruebas del núcleo pi32v2 del motor FM-1 (Source/Fm1). Sin JUCE.
//
// Uso: KoniFm1Tests [--listing <objdump.txt> ...] [--fwsc <paquete.fwsc> [--app <app.bin>] ...]
//   sin argumentos: las pruebas de ejecución, portadas de fm1-emu (crate fm1-core, módulo tests; MIT) con sus mismos
//   bytes y resultados esperados; solo las que van por step (la ruta de referencia). Las de micro-ops, bloques, JIT y
//   costes de ciclo no aplican aquí.
//   --listing: además decodifica cada instrucción de un listado del objdump de JieLi (formato "  27c:\t40 f0 10 27 \t...")
//   y compara la longitud con la del listado (lo mismo que tools/fm1/check_isa.py, ahora con el decodificador C++).
#include "../Source/Fm1/Fm1Cpu.h"
#include "../Source/Fm1/Fm1Fwsc.h"
#include "../Source/Fm1/Fm1Devices.h"
#include "../Source/Fm1/Fm1Periph.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace fm1;

namespace
{
int failures = 0, checks = 0;
const char* currentTest = "";

#define CHECK(cond, msg)                                                                                  \
    do {                                                                                                  \
        ++checks;                                                                                         \
        if (! (cond)) { ++failures; std::printf ("  FALLA %s: %s (línea %d)\n", currentTest, msg, __LINE__); } \
    } while (0)

#define CHECK_EQ(a, b, msg)                                                                               \
    do {                                                                                                  \
        ++checks;                                                                                         \
        const auto va_ = (unsigned long long) (a), vb_ = (unsigned long long) (b);                        \
        if (va_ != vb_) { ++failures; std::printf ("  FALLA %s: %s: 0x%llx != 0x%llx (línea %d)\n", currentTest, msg, va_, vb_, __LINE__); } \
    } while (0)

// Memoria plana (FlatRam de las pruebas de fm1-core): lecturas fuera de rango = 0, escrituras fuera se ignoran
struct FlatRam : Bus
{
    std::vector<uint8_t> mem;
    explicit FlatRam (size_t n) : mem (n, 0) {}
    void put (size_t at, std::initializer_list<uint8_t> b) { std::memcpy (mem.data() + at, b.begin(), b.size()); }
    void put32 (size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) mem[at + (size_t) i] = (uint8_t) (v >> (8 * i)); }
    uint8_t read8 (uint32_t a) override { return a < mem.size() ? mem[a] : 0; }
    uint16_t read16 (uint32_t a) override { return (uint16_t) (read8 (a) | (read8 (a + 1) << 8)); }
    uint32_t read32 (uint32_t a) override { return read16 (a) | ((uint32_t) read16 (a + 2) << 16); }
    void write8 (uint32_t a, uint8_t v) override { if (a < mem.size()) mem[a] = v; }
    void write16 (uint32_t a, uint16_t v) override { write8 (a, (uint8_t) v); write8 (a + 1, (uint8_t) (v >> 8)); }
    void write32 (uint32_t a, uint32_t v) override { write16 (a, (uint16_t) v); write16 (a + 2, (uint16_t) (v >> 16)); }
};

void step (Cpu& c, Bus& b)
{
    ++checks;
    if (! c.step (b))
    {
        ++failures;
        std::printf ("  FALLA %s: step en pc 0x%x: fallo %d (%s, raw 0x%llx)\n", currentTest, c.faultPc, (int) c.fault,
                     c.faultName != nullptr ? c.faultName : "?", (unsigned long long) c.faultRaw);
    }
}

// Prueba diferencial de las micro-ops: cada muestra del corpus de cada clase de la tabla cuya forma tiene micro-op se
// ejecuta con estados aleatorios (sembrados: reproducible) por la ruta rápida y por exec_syntax, y el estado visible debe
// coincidir entero: registros, sp, pc, banderas, rets y la memoria. Cubre todas las clases, no solo las que usa un firmware
void fastPathMatchesExecSyntax()
{
    constexpr uint32_t kPc = 0x100, kMem = 1u << 16;
    uint64_t rng = 0x5eed5eed12345678ull;
    const auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
    size_t classes = 0, runs = 0, bad = 0;
    for (size_t ei = 0; ei < entryCount(); ++ei)
    {
        const auto& e = entryAt (ei);
        bool counted = false;
        for (uint32_t si = 0; si < std::min<uint32_t> (e.sampleCount, 8); ++si)
        {
            uint64_t win = e.samples[si], raw = 0;
            const auto* d = decodeAny (win, raw);
            if (d == nullptr)
                continue;
            Fm1Pre pre;
            computePre (*d, raw, pre);
            if (pre.fast == Fm1Pre::FastNone)
                continue;
            if (! counted) { ++classes; counted = true; }
            for (int trial = 0; trial < 4; ++trial)
            {
                FlatRam ma (kMem), mb (kMem);
                for (auto& b : ma.mem) b = (uint8_t) next();
                for (int k = 0; k < 8; ++k) ma.mem[kPc + (uint32_t) k] = (uint8_t) (win >> (8 * k));
                for (int k = 8; k < 16; ++k) ma.mem[kPc + (uint32_t) k] = 0;  // nop detrás (la segunda de un par)
                mb.mem = ma.mem;
                Cpu a (kPc), b (kPc);
                for (int r = 0; r < 16; ++r)
                    a.regs[r] = (trial & 1) != 0 ? (uint32_t) next() : 0x4000u + ((uint32_t) next() & 0x7ffcu);
                a.sp = 0x8000u + ((uint32_t) next() & 0x3ffcu);
                a.flags = (uint32_t) next() & 15;
                a.rets = 0x2000u + ((uint32_t) next() & 0xffeu);
                a.coreId = (uint32_t) (trial >> 1) & 1;  // (R = cnum)
                a.ie = (next() & 1) != 0;
                std::memcpy (b.regs, a.regs, sizeof a.regs);
                b.sp = a.sp; b.flags = a.flags; b.rets = a.rets; b.coreId = a.coreId; b.ie = a.ie;
                b.fastPath = false;
                const bool oa = a.step (ma), ob = b.step (mb);
                ++runs;
                const bool same = oa == ob && a.pc == b.pc && a.sp == b.sp && a.flags == b.flags && a.rets == b.rets && a.ie == b.ie
                               && std::memcmp (a.regs, b.regs, sizeof a.regs) == 0 && ma.mem == mb.mem;
                if (! same && bad++ < 8)
                    std::printf ("  FALLA %s: %s (\"%s\", bytes 0x%llx, ensayo %d): ok %d/%d pc 0x%x/0x%x flags %x/%x\n",
                                 currentTest, d->name, d->syntax, (unsigned long long) win, trial, oa, ob, a.pc, b.pc, a.flags, b.flags);
            }
        }
    }
    std::printf ("  micro-ops: %zu clases, %zu ejecuciones comparadas, %zu distintas\n", classes, runs, bad);
    CHECK (classes > 0, "hay clases con micro-op");
    CHECK_EQ (bad, 0, "ejecuciones distintas entre la ruta rápida y exec_syntax");
}

void nopRunsAtEntry()
{
    FlatRam ram (16);
    Cpu cpu (0);
    step (cpu, ram);
    CHECK_EQ (cpu.insnCount, 1, "insnCount");
    CHECK_EQ (cpu.pc, 2, "pc");
}

void gotoRelJumps()
{
    FlatRam ram (12);
    ram.put (0, { 0x04, 0x81 });  // goto +2
    Cpu cpu (0);
    step (cpu, ram);
    CHECK_EQ (cpu.pc, 4, "2 + 2");
}

void movImmL2()
{
    FlatRam ram (8);
    ram.put (0, { 0x40, 0x2a });  // r0 = 10
    Cpu cpu (0);
    step (cpu, ram);
    CHECK_EQ (cpu.regs[0], 10, "r0 = 10");
    FlatRam ram2 (8);
    ram2.put (0, { 0x45, 0x24 });  // r5 = 4
    Cpu cpu2 (0);
    step (cpu2, ram2);
    CHECK_EQ (cpu2.regs[5], 4, "r5 = 4");
}

void callL6()
{
    FlatRam ram (16);
    ram.put (0, { 0x80, 0xff, 0xa0, 0x5a, 0xc0, 0xff });  // call -4171104
    Cpu cpu (0);
    step (cpu, ram);
    CHECK_EQ (cpu.rets, 6, "rets");
    CHECK_EQ (cpu.pc, (uint32_t) (6 - 4171104), "pc");
}

void unsupportedHalts()
{
    FlatRam ram (8);
    ram.put (0, { 0xa0, 0x00 });  // swi 0: sin semántica, debe parar
    Cpu cpu (0);
    ++checks;
    const bool ok = cpu.step (ram);
    CHECK (! ok && cpu.fault == Fault::Unsupported, "Unsupported");
    CHECK (cpu.faultName != nullptr && std::strcmp (cpu.faultName, "swi_i") == 0, "nombre swi_i");
}

void locksetRetries()
{
    struct LockRam : FlatRam
    {
        bool grant = false;
        std::vector<std::pair<uint32_t, bool>> calls;
        LockRam() : FlatRam (8) {}
        bool hwLock (uint32_t core, bool set) override { calls.push_back ({ core, set }); return ! set || grant; }
    } ram;
    ram.put (0, { 0x41, 0x00, 0x40, 0x00 });  // lockset; lockclr
    Cpu cpu (0);
    cpu.coreId = 1;
    step (cpu, ram);
    CHECK_EQ (cpu.pc, 0, "lo tiene el otro núcleo: lockset reintenta");
    ram.grant = true;
    step (cpu, ram);
    CHECK_EQ (cpu.pc, 2, "concedido");
    step (cpu, ram);
    CHECK_EQ (cpu.pc, 4, "lockclr");
    CHECK (ram.calls.size() == 3 && ram.calls[0] == std::make_pair (1u, true) && ram.calls[1] == std::make_pair (1u, true)
               && ram.calls[2] == std::make_pair (1u, false),
           "llamadas al cerrojo");
}

void composedMatchesObservedTable()
{
    // MOV_H_TABLE de fm1-core: valores de R = #h observados en los cinco corpus del objdump
    static const struct { uint8_t code, m; uint32_t v; } t[] = {
        { 0x2, 0x64, 0x64006400 }, { 0x3, 0x05, 0x05050505 }, { 0x3, 0x30, 0x30303030 }, { 0x3, 0x32, 0x32323232 },
        { 0x3, 0xc1, 0xc1c1c1c1 }, { 0x3, 0xcc, 0xcccccccc }, { 0x3, 0xff, 0xffffffff }, { 0x4, 0x00, 0x80000000 },
        { 0x4, 0x08, 0x88000000 }, { 0x4, 0x40, 0xc0000000 }, { 0x4, 0x60, 0xe0000000 }, { 0x4, 0x70, 0xf0000000 },
        { 0x4, 0x83, 0x41800000 }, { 0x4, 0x86, 0x43000000 }, { 0x4, 0x87, 0x43800000 }, { 0x4, 0x88, 0x44000000 },
        { 0x4, 0x9f, 0x4f800000 }, { 0x4, 0xf0, 0x78000000 }, { 0x5, 0x42, 0x30800000 }, { 0x5, 0x50, 0x34000000 },
        { 0x5, 0x60, 0x38000000 }, { 0x5, 0x7a, 0x3e800000 }, { 0x5, 0x7c, 0x3f000000 }, { 0x5, 0x7e, 0x3f800000 },
        { 0x5, 0xc0, 0x18000000 }, { 0x6, 0x00, 0x08000000 }, { 0x6, 0x80, 0x04000000 }, { 0x6, 0xb0, 0x05800000 },
        { 0x6, 0xef, 0x07780000 }, { 0x7, 0x00, 0x02000000 }, { 0x7, 0x80, 0x01000000 }, { 0x7, 0xf8, 0x01f00000 },
        { 0x8, 0x00, 0x00800000 }, { 0x9, 0x00, 0x00200000 }, { 0x9, 0x10, 0x00240000 }, { 0x9, 0x40, 0x00300000 },
        { 0x9, 0x80, 0x00100000 }, { 0xa, 0x10, 0x00090000 }, { 0xa, 0x30, 0x000b0000 }, { 0xa, 0x70, 0x000f0000 },
        { 0xa, 0x80, 0x00040000 }, { 0xa, 0xa0, 0x00050000 }, { 0xa, 0xa2, 0x00051000 }, { 0xb, 0x00, 0x00020000 },
        { 0xb, 0x20, 0x00028000 }, { 0xb, 0x3f, 0x0002fc00 }, { 0xb, 0x80, 0x00010000 }, { 0xb, 0x81, 0x00010200 },
        { 0xb, 0x82, 0x00010400 }, { 0xb, 0x84, 0x00010800 }, { 0xb, 0x8c, 0x00011800 }, { 0xb, 0x8e, 0x00011c00 },
        { 0xb, 0x8f, 0x00011e00 }, { 0xb, 0x90, 0x00012000 }, { 0xb, 0x97, 0x00012e00 }, { 0xb, 0x98, 0x00013000 },
        { 0xb, 0x9a, 0x00013400 }, { 0xb, 0x9b, 0x00013600 }, { 0xb, 0x9f, 0x00013e00 }, { 0xb, 0xa0, 0x00014000 },
        { 0xb, 0xa9, 0x00015200 }, { 0xb, 0xb0, 0x00016000 }, { 0xb, 0xb4, 0x00016800 }, { 0xb, 0xb5, 0x00016a00 },
        { 0xb, 0xf8, 0x0001f000 }, { 0xc, 0x00, 0x00008000 }, { 0xc, 0x1c, 0x00009c00 }, { 0xc, 0x40, 0x0000c000 },
        { 0xc, 0x7e, 0x0000fe00 }, { 0xc, 0x7f, 0x0000ff00 },
    };
    for (const auto& x : t)
        CHECK_EQ (composedImm (x.code, x.m), x.v, "composedImm");
    CHECK_EQ (composedImm (0xe, 0x01), 0x810, "r0 | 0x810");
    CHECK_EQ (composedImm (0xf, 0x7f), 0x3fc, "r0 & 0x3fc");
    CHECK_EQ (composedImm (0x1, 0x5f), 0x5f005f, "0x5f005f");
    CHECK_EQ (composedImm (0x3, 0xff), 0xffffffff, "0xffffffff");
}

void alu4Shapes()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x90, 0xe1, 0x20, 0x01, 0x36, 0xe1, 0xf0, 0x4f, 0xc0, 0xef, 0x40, 0x0f, 0x64, 0xe8, 0x0a, 0x02 });
    ram.put32 (0x40, 0xffffffff);
    ram.put32 (0x48, 0x0000f0f0);
    Cpu cpu (0);
    cpu.regs[1] = 0x10; cpu.regs[2] = 0x0f; cpu.regs[4] = 100;
    step (cpu, ram);
    CHECK_EQ (cpu.regs[0], 0x1f, "r0 = r2 | r1");
    step (cpu, ram);
    CHECK_EQ (cpu.regs[6], 84, "r6 = r4 + -16");
    cpu.regs[0] = 0x40;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x40), 0xfffffcff, "[r0+0] &= 0xfffffcff");
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x48), 0x0000f0f0 & 0x0f, "[r0+8] &= r2");
    CHECK_EQ (cpu.pc, 16, "pc");
}

void memShiftByRegister()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x6e, 0xe8, 0x0a, 0x01, 0x6e, 0xe8, 0x00, 0x05, 0x6e, 0xe8, 0x0f, 0x71 });
    ram.put32 (0x40, 0x00000081);
    ram.put32 (0x48, 0x80000f00);
    ram.put32 (0x5c, 0xffffff00);
    Cpu cpu (0);
    cpu.regs[0] = 0x40; cpu.regs[7] = 0x50; cpu.regs[1] = 4; cpu.regs[5] = 33;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x48), 0x080000f0, "[r0+8] >>= r1: lógico");
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x40), 0x00000102, "[r0+0] <<= r5 (módulo 32)");
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x5c), 0xfffffff0, "[r7+12] >>>= r1: aritmético");
    CHECK_EQ (cpu.pc, 12, "pc");
}

void carryFlags64()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x24, 0x1c, 0xb8, 0xe0, 0x20, 0x46, 0x84, 0x1e, 0xb8, 0xe0, 0x12, 0x53, 0x84, 0x1e, 0x64, 0xe0, 0x00, 0x05 });
    Cpu cpu (0);
    cpu.regs[2] = 0xffffffff; cpu.regs[0] = 1;
    step (cpu, ram);
    CHECK_EQ (cpu.regs[4], 0, "r4 = r2 + r0");
    CHECK ((cpu.flags & kFlagC) != 0, "acarreo de la palabra baja");
    cpu.regs[2] = 5; cpu.regs[6] = 7;
    step (cpu, ram);
    CHECK_EQ (cpu.regs[4], 13, "5 + 7 + acarreo");
    cpu.regs[0] = 0; cpu.regs[2] = 1;
    step (cpu, ram);
    cpu.regs[1] = 1; cpu.regs[3] = 0;
    step (cpu, ram);
    CHECK (cpu.regs[4] == 0xffffffff && cpu.regs[5] == 0, "la palabra alta toma el préstamo");
    cpu.regs[0] = 9; cpu.regs[2] = 9;
    step (cpu, ram);
    step (cpu, ram);
    CHECK_EQ (cpu.regs[0] & (kFlagC | kFlagZ), kFlagC | kFlagZ, "psr: C y Z");
}

void pairHeadAndNot()
{
    {
        FlatRam ram (0x100);
        ram.put (0, { 0x94, 0xf1, 0x13, 0x13, 0x00, 0x00 });
        Cpu cpu (0);
        cpu.regs[1] = 0xff; cpu.regs[3] = 3;
        step (cpu, ram);
        step (cpu, ram);
        CHECK_EQ (cpu.regs[1], 0xf7, "r1 &= ~(1 << r3)");
    }
    {
        FlatRam ram (0x100);
        ram.put (0, { 0x94, 0xf1, 0x63, 0x10, 0x00, 0x00 });
        Cpu cpu (0);
        cpu.regs[0] = 2; cpu.regs[1] = 0; cpu.regs[2] = 0x55; cpu.regs[6] = 0xff;
        step (cpu, ram);
        step (cpu, ram);
        CHECK (cpu.regs[1] == 0xfb && cpu.regs[2] == 0x55, "r1 = r6 & ~(1 << r0), r2 intacto");
    }
}

void andNotAboveR7()
{
    FlatRam ram (0x200);
    ram.put (0, { 0x64, 0xe8, 0x5f, 0xbc });
    ram.put32 (0x15c, 0xffffffff);
    Cpu cpu (0);
    cpu.regs[11] = 0x100; cpu.regs[12] = 0x0000ff00;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x15c), 0xffff00ff, "[r11+92] &= ~r12");
}

void corpusUnseenOffsets()
{
    FlatRam ram (0x400);
    ram.put (0, { 0x6c, 0xe8, 0x18, 0x01, 0x64, 0xe8, 0xfe, 0x10, 0xd1, 0xed, 0x41, 0x0e });
    ram.put32 (0x118, 0x40000001);
    ram.put32 (0x1fc, 0xffff00ff);
    Cpu cpu (0);
    cpu.regs[0] = 0x100;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x118), 0x80000002, "[r0+24] <<= 1");
    cpu.regs[1] = 0x200; cpu.regs[0] = 0x0f0f0f0f;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x1fc), 0x0f0f000f, "[r1+-4] &= r0");
    cpu.regs[4] = 0x300; cpu.regs[0] = 0xbeef;
    step (cpu, ram);
    CHECK_EQ (ram.read16 (0x300), 0xbeef, "h[r4++=480] = r0");
    CHECK_EQ (cpu.regs[4], 0x300 + 480, "postincremento de 480");
}

void wordNegativeOffsets()
{
    FlatRam ram (0x800);
    ram.put (0, { 0xd7, 0xec, 0x51, 0x38, 0xd5, 0xec, 0x10, 0x2e, 0xd4, 0xec, 0xeb, 0xef });
    ram.put32 (0x500, 0x12345678);
    Cpu cpu (0);
    cpu.regs[5] = 0x480; cpu.regs[3] = 0xcafef00d;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x400), 0xcafef00d, "[r5+-128] = r3");
    CHECK_EQ (cpu.regs[5], 0x480, "sin escribir la base");
    cpu.regs[1] = 0x720;
    step (cpu, ram);
    CHECK_EQ (cpu.regs[2], 0x12345678, "r2 = [r1+-544]");
    cpu.regs[14] = 0x708;
    step (cpu, ram);
    CHECK_EQ (cpu.regs[14], 0x400, "preincremento de -776");
}

void sregMovesAnyRegister()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x64, 0xe0, 0x80, 0x40, 0x64, 0xe0, 0x00, 0x20, 0x64, 0xe0, 0x80, 0x13 });
    Cpu cpu (0);
    cpu.regs[4] = 0x02012345; cpu.regs[1] = 0x02000abc;
    for (int i = 0; i < 3; ++i) step (cpu, ram);
    CHECK_EQ (cpu.regs[2], 0x02012345, "r2 = reti");
    CHECK_EQ (cpu.rets, 0x02000abc, "rets = r1");
}

void icfgRestoresEnable()
{
    for (bool on : { true, false })
    {
        FlatRam ram (0x100);
        ram.put (0, { 0x64, 0xe0, 0x00, 0x1b, 0x60, 0x00, 0x64, 0xe0, 0x80, 0x1b });  // r1 = icfg; cli; icfg = r1
        Cpu cpu (0);
        cpu.ie = on;
        for (int i = 0; i < 3; ++i) step (cpu, ram);
        CHECK_EQ (cpu.pc, 10, "pc");
        CHECK (cpu.ie == on, "ie restaurada por icfg = r1");
    }
}

void ifBlockWithElse()
{
    FlatRam base (0x100);
    base.put (0, { 0x1d, 0xea, 0x80, 0xa6 });  // if ((r13 & r6) != 0) { 3 } else { 2 }
    for (uint8_t r = 0; r < 6; ++r)
        base.put (4 + 2u * r, { (uint8_t) (0xc0 | r), 0x21 });  // rN += 1
    const struct { uint32_t r13; uint32_t want[6]; } cases[] = { { 0x10, { 1, 1, 1, 0, 0, 1 } }, { 0x01, { 0, 0, 0, 1, 1, 1 } } };
    for (const auto& c : cases)
    {
        FlatRam ram = base;
        Cpu cpu (0);
        cpu.regs[13] = c.r13; cpu.regs[6] = 0x30;
        for (int guard = 0; cpu.pc != 16 && guard < 32; ++guard) step (cpu, ram);
        for (int i = 0; i < 6; ++i)
            CHECK_EQ (cpu.regs[i], c.want[i], "registros tras el bloque");
    }
}

// Programas de then_part_programs (fm1-core): bloque if-else en 0x80, f en 0x10 (goto o call a f desde la parte then)
FlatRam thenPartProgram (bool call)
{
    const auto off = (uint16_t) (int16_t) ((0x10 - 0x88) / 2);
    FlatRam ram (0x100);
    ram.put (0x10, { 0xc5, 0x21, (uint8_t) (call ? 0x80 : 0xc5), (uint8_t) (call ? 0x00 : 0x21) });
    ram.put (0x80, { 0xb0, 0xe8, 0x00, 0x10 });
    ram.put (0x84, { (uint8_t) (call ? 0xbf : 0xff), 0xea, (uint8_t) (off & 0xff), (uint8_t) (off >> 8) });
    ram.put (0x88, { 0x40, 0x20 });
    ram.put (0x8a, { 0xc6, 0x21 });
    ram.put (0x14, { 0xff, 0xea, 0xfe, 0xff });
    ram.put (0x8c, { 0xff, 0xea, 0xfe, 0xff });
    return ram;
}

void gotoBackOutOfThenPart()
{
    for (bool call : { false, true })
    {
        auto ram = thenPartProgram (call);
        Cpu cpu (0x80);
        cpu.regs[0] = 1;
        cpu.ie = true;
        cpu.sregStore[0] |= 0x100;
        step (cpu, ram);
        CHECK (! cpu.predSkips.empty() && ! cpu.irqReady (4), "en la parte then, sin interrupción");
        step (cpu, ram);
        CHECK_EQ (cpu.pc, 0x10, "a f");
        CHECK (cpu.predSkips.empty() == ! call, "un goto sale de la construcción, una llamada no");
        CHECK (cpu.irqReady (4) == ! call, "interrupciones");
        if (call)
        {
            for (int i = 0; i < 3; ++i) step (cpu, ram);
            CHECK (cpu.pc == 0x8c && cpu.regs[0] == 1 && cpu.regs[5] == 1 && cpu.regs[6] == 1, "la parte else saltada");
            CHECK (cpu.predSkips.empty(), "sin saltos pendientes");
        }
    }
}

void registerIncrementAccesses()
{
    FlatRam ram (0x100);
    ram.put (0, { 0xde, 0xee, 0x21, 0x10, 0xdc, 0xec, 0x02, 0x11, 0xde, 0xed, 0x93, 0x00 });
    ram.put32 (0x68, 0x12345678);
    Cpu cpu (0);
    cpu.regs[0] = 3; cpu.regs[1] = 0xab; cpu.regs[2] = 0x40;
    step (cpu, ram);
    CHECK (ram.read8 (0x40) == 0xab && cpu.regs[2] == 0x43, "store de byte con postincremento");
    cpu.regs[0] = 0x50; cpu.regs[1] = 0x18;
    step (cpu, ram);
    CHECK (cpu.regs[1] == 0x12345678 && cpu.regs[0] == 0x68, "carga de palabra con preincremento");
    cpu.regs[0] = 0x00020000; cpu.regs[9] = 0x70;
    step (cpu, ram);
    CHECK (ram.read16 (0x70) == 2 && cpu.regs[9] == 0x20070, "store de la mitad alta");
}

void spPairStoreAndRev8()
{
    FlatRam ram (0x600);
    ram.put (0, { 0xd0, 0xe9, 0x15, 0xc4, 0x70, 0xe0, 0x00, 0x80 });
    Cpu cpu (0);
    cpu.sp = 0x100;
    cpu.regs[12] = 0x11111111; cpu.regs[13] = 0x22222222; cpu.regs[0] = 0x11223344;
    step (cpu, ram);
    CHECK (ram.read32 (0x514) == 0x11111111 && ram.read32 (0x518) == 0x22222222, "d[sp+1044] = r13_r12");
    step (cpu, ram);
    CHECK_EQ (cpu.regs[8], 0x44332211, "r8 = rev8(r0)");
}

void pairMultiplyAccumulate()
{
    FlatRam ram (0x100);
    ram.put (0, { 0xfc, 0xe1, 0x30, 0x07 });  // r1_r0 += r3 * r7 (u)
    Cpu cpu (0);
    cpu.regs[0] = 0xffffffff; cpu.regs[1] = 1; cpu.regs[3] = 0x10000; cpu.regs[7] = 0x10001;
    step (cpu, ram);
    const uint64_t want = 0x1ffffffffull + 0x10000ull * 0x10001ull;
    CHECK (cpu.regs[0] == (uint32_t) (want & 0xffffffffu) && cpu.regs[1] == (uint32_t) (want >> 32), "r1_r0 += r3 * r7");
}

uint64_t pairOfRegs (const Cpu& c, int p) { return ((uint64_t) c.regs[2 * p + 1] << 32) | c.regs[2 * p]; }

Cpu runOne (std::initializer_list<uint8_t> code, std::initializer_list<std::pair<int, uint32_t>> regs)
{
    FlatRam ram (0x100);
    ram.put (0, code);
    Cpu cpu (0);
    for (const auto& [r, v] : regs) cpu.regs[r] = v;
    step (cpu, ram);
    CHECK_EQ (cpu.pc, 4, "pc");
    return cpu;
}

void pairMultiplySubtract()
{
    auto c = runOne ({ 0xfe, 0xe1, 0x40, 0x61 }, { { 6, 5 }, { 7, 1 }, { 4, 0xffffffff }, { 1, 3 } });
    CHECK_EQ (pairOfRegs (c, 3), 0x100000005ull - 0xffffffffull * 3, "sin signo, préstamo de la palabra alta");
    c = runOne ({ 0xfe, 0xe1, 0x40, 0x71 }, { { 6, 10 }, { 7, 0 }, { 4, (uint32_t) -2 }, { 1, 0x7fffffff } });
    CHECK_EQ (pairOfRegs (c, 3), (uint64_t) (10ll - (-2ll) * 0x7fffffffll), "con signo");
    c = runOne ({ 0xfe, 0xe1, 0x40, 0x61 }, { { 6, 10 }, { 7, 0 }, { 4, (uint32_t) -2 }, { 1, 0x7fffffff } });
    CHECK_EQ (pairOfRegs (c, 3), 10ull - 0xfffffffeull * 0x7fffffffull, "los mismos sin signo");
    c = runOne ({ 0xfe, 0xe1, 0xf0, 0x1e }, { { 0, 0 }, { 1, 0 }, { 15, (uint32_t) -7 }, { 14, 9 } });
    CHECK_EQ (pairOfRegs (c, 0), 63, "r1_r0 -= r15 * r14 (s)");
    c = runOne ({ 0xfe, 0xe1, 0x00, 0xed }, { { 14, 1 }, { 15, 0 }, { 0, 0x80000000 }, { 13, 4 } });
    CHECK_EQ (pairOfRegs (c, 7), 1ull - 0x200000000ull, "r15_r14 -= r0 * r13 (u)");
    c = runOne ({ 0xfe, 0xe1, 0x60, 0x67 }, { { 6, 3 }, { 7, 2 } });
    CHECK_EQ (pairOfRegs (c, 3), 0x200000003ull - 6, "un factor dentro del par lee su valor viejo");
}

void pairMultiplySubtractDualIssue()
{
    FlatRam ram (0x200);
    ram.put (0, { 0xfe, 0xf1, 0x40, 0x61, 0x87, 0x20 });
    Cpu cpu (0);
    cpu.sp = 0x100;
    cpu.regs[6] = 0; cpu.regs[7] = 0x10; cpu.regs[4] = 0x1000; cpu.regs[1] = 0x1000;
    for (int g = 0; cpu.pc < 6 && g < 8; ++g) step (cpu, ram);
    CHECK (cpu.regs[6] == 0xff000000 && cpu.regs[7] == 0xf, "r7_r6 -= r4 * r1");
    CHECK_EQ (ram.read32 (0x100), 0x10, "el store ve el r7 viejo");
}

void multiplyImmediateDualIssue()
{
    FlatRam ram (0x200);
    ram.put (0, { 0xea, 0xf1, 0x16, 0x40, 0x87, 0x20, 0xe3, 0xf1, 0xff, 0x25, 0x85, 0x21 });
    Cpu cpu (0);
    cpu.sp = 0x100;
    cpu.regs[4] = 3; cpu.regs[7] = 0x55; cpu.regs[2] = 5; cpu.regs[5] = 0x66;
    for (int g = 0; cpu.pc < 12 && g < 8; ++g) step (cpu, ram);
    CHECK_EQ (cpu.regs[10], 66, "r10 = r4 * 0x16");
    CHECK_EQ (cpu.regs[3], 5u * 0x1fe00000u, "r3 = r2 * 0x1fe00000");
    CHECK (ram.read32 (0x100) == 0x55 && ram.read32 (0x104) == 0x66, "stores del par");
}

void pairStoreAndLoad()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x50, 0xec, 0x35, 0x40, 0x50, 0xec, 0x00, 0x20 });
    Cpu cpu (0);
    cpu.regs[3] = 0x40; cpu.regs[4] = 0x1000; cpu.regs[5] = 0x2000;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x44), 0x1000, "el bajo en la dirección menor");
    CHECK_EQ (ram.read32 (0x48), 0x2000, "el alto");
    cpu.regs[0] = 0x44;
    step (cpu, ram);
    CHECK (cpu.regs[2] == 0x1000 && cpu.regs[3] == 0x2000, "r3_r2 = d[r0+0]");
}

void pairHeadByteExtract()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x00, 0xd7, 0xc8, 0x4b });  // r0 = r0.b0 (u) #; b[r4+11] = r0
    Cpu cpu (0);
    cpu.regs[0] = 0xdeadbeef; cpu.regs[4] = 0x40;
    step (cpu, ram);
    step (cpu, ram);
    CHECK_EQ (ram.read8 (0x4b), 0xef, "el store ve el r0 viejo (su byte bajo)");
    CHECK_EQ (cpu.regs[0], 0xef, "r0 = byte bajo");
}

void shiftImmHighSource()
{
    FlatRam ram (0x100);
    ram.put (0, { 0xc0, 0xe1, 0xc8, 0x10, 0xc0, 0xe1, 0xc1, 0x38 });
    Cpu cpu (0);
    cpu.regs[12] = 3;
    step (cpu, ram);
    step (cpu, ram);
    CHECK_EQ (cpu.regs[1], 0x300, "r1 = r12 << 8");
    CHECK_EQ (cpu.regs[3], 1, "r3 = r12 >> 1");
}

void spPairStoreLargeOffset()
{
    FlatRam ram (0x800);
    ram.put (0, { 0xd0, 0xe9, 0x21, 0xa4, 0xd0, 0xe9, 0xc0, 0x01 });
    Cpu cpu (0);
    cpu.sp = 0x100;
    cpu.regs[10] = 0x11223344; cpu.regs[11] = 0x55667788;
    step (cpu, ram);
    CHECK_EQ (ram.read32 (0x100 + 1056), 0x11223344, "d[sp+1056] bajo");
    CHECK_EQ (ram.read32 (0x100 + 1060), 0x55667788, "d[sp+1056] alto");
    cpu.sp = 0x100 + 1056 - 448;
    step (cpu, ram);
    CHECK (cpu.regs[0] == 0x11223344 && cpu.regs[1] == 0x55667788, "r1_r0 = d[sp+448]");
}

void pairAccessModes()
{
    FlatRam ram (0x400);
    ram.put (0, { 0x5c, 0xec, 0x13, 0x40, 0x57, 0xec, 0x19, 0x8f, 0x58, 0xec, 0x09, 0x20, 0x58, 0xec, 0x13, 0x82 });
    Cpu cpu (0);
    cpu.regs[0] = 0x40; cpu.regs[1] = 0x100;
    cpu.regs[4] = 0x11; cpu.regs[5] = 0x22; cpu.regs[8] = 0x33; cpu.regs[9] = 0x44; cpu.regs[2] = 0x55; cpu.regs[3] = 0x66;
    step (cpu, ram);
    CHECK (cpu.regs[1] == 0x140 && ram.read32 (0x140) == 0x11 && ram.read32 (0x144) == 0x22, "d[++r1=r0] = r5_r4");
    step (cpu, ram);
    CHECK (ram.read32 (0x138) == 0x33 && ram.read32 (0x13c) == 0x44, "d[r1+-8] = r9_r8");
    step (cpu, ram);
    CHECK (cpu.regs[0] == 0x48 && ram.read32 (0x40) == 0x55 && ram.read32 (0x44) == 0x66, "d[r0++=8] = r3_r2");
    cpu.regs[2] = 0x80;
    step (cpu, ram);
    CHECK (ram.read32 (0x1c0) == 0x33 && ram.read32 (0x1c4) == 0x44, "d[r1+r2] = r9_r8");
}

void bitfieldExtractIsNotAnAdd()
{
    FlatRam ram (0x100);
    ram.put (0, { 0xb4, 0xe1, 0xa0, 0x4b });  // r4 = uextra(r4, p:23, l:8)
    Cpu cpu (0);
    cpu.regs[4] = std::bit_cast<uint32_t> (1e-5f);
    step (cpu, ram);
    CHECK_EQ (cpu.regs[4], 0x6e, "exponente de 1e-5");
}

void fpuOpsAndFloatCompare()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x3f, 0xe5, 0x30, 0x57, 0x3f, 0xe5, 0x1f, 0x65, 0x3f, 0xe5, 0x8f, 0x26, 0x12, 0xe9, 0x80, 0x01, 0x40, 0x20 });
    Cpu cpu (0);
    cpu.regs[3] = std::bit_cast<uint32_t> (2.5f);
    cpu.regs[7] = std::bit_cast<uint32_t> (-7.75f);
    cpu.regs[1] = std::bit_cast<uint32_t> (0.0f);
    cpu.regs[0] = 1;
    for (int i = 0; i < 4; ++i) step (cpu, ram);
    CHECK (std::bit_cast<float> (cpu.regs[5]) == -5.25f, "r5 = r3 + r7 (f)");
    CHECK_EQ ((int32_t) cpu.regs[6], -5, "r6 = ftoi(r5)");
    CHECK (std::bit_cast<float> (cpu.regs[2]) == -5.0f, "r2 = itof(r6)");
    CHECK (cpu.pc == 18 && cpu.regs[0] == 1, "-5.0 u>= 0.0 es falso: bloque saltado");
}

void shortShiftBy32()
{
    FlatRam ram (0x100);
    ram.put (0, { 0xdf, 0xa0, 0x32, 0xa0 });
    Cpu cpu (0);
    cpu.regs[5] = 0x80001234; cpu.regs[3] = 0xffffffff;
    step (cpu, ram);
    step (cpu, ram);
    CHECK_EQ (cpu.regs[7], 0xffffffff, "r7 = r5 >>> 32");
    CHECK_EQ (cpu.regs[2], 0, "r2 = r3 << 32");
}

void pushPopRegisterMask()
{
    FlatRam ram (0x200);
    ram.put (0, { 0xd9, 0xe8, 0xf0, 0x0d, 0xd5, 0xe8, 0xf0, 0x0d });
    Cpu cpu (0);
    cpu.sp = 0x200;
    cpu.rets = 0x44;
    for (int r = 0; r < 16; ++r) cpu.regs[r] = 0x100u + (uint32_t) r;
    step (cpu, ram);
    CHECK_EQ (cpu.sp, 0x200 - 4 * 8, "sp tras el push");
    CHECK_EQ (ram.read32 (0x1fc), 0x44, "rets arriba");
    CHECK_EQ (ram.read32 (0x1f8), 0x10b, "luego r11");
    CHECK_EQ (ram.read32 (0x200 - 32), 0x104, "r4 abajo");
    for (auto& r : cpu.regs) r = 0;
    step (cpu, ram);
    CHECK_EQ (cpu.pc, 0x44, "pc");
    CHECK_EQ (cpu.sp, 0x200, "sp");
    CHECK (cpu.regs[4] == 0x104 && cpu.regs[8] == 0x108 && cpu.regs[9] == 0 && cpu.regs[10] == 0x10a && cpu.regs[11] == 0x10b, "registros");
}

void retsMaskPopAndPairs()
{
    FlatRam ram (0x200);
    ram.put (0, { 0xd9, 0xe8, 0xf0, 0x0d, 0xd1, 0xe8, 0xf0, 0x0d, 0x40, 0x15, 0x10, 0x04 });
    Cpu cpu (0);
    cpu.sp = 0x200;
    cpu.rets = 0x44;
    for (int r = 0; r < 16; ++r) cpu.regs[r] = 0x100u + (uint32_t) r;
    step (cpu, ram);
    for (auto& r : cpu.regs) r = 0;
    cpu.rets = 0;
    step (cpu, ram);
    CHECK (cpu.pc == 8 && cpu.sp == 0x200 && cpu.rets == 0x44, "pop con rets, sin salto");
    CHECK (cpu.regs[4] == 0x104 && cpu.regs[8] == 0x108 && cpu.regs[9] == 0 && cpu.regs[10] == 0x10a && cpu.regs[11] == 0x10b, "registros");
    cpu.regs[5] = 0x55;
    step (cpu, ram);
    CHECK (cpu.regs[1] == 0x55 && cpu.regs[0] == 0x104, "r1_r0 = r5_r4");
    step (cpu, ram);
    CHECK (cpu.sp == 0x1fc && ram.read32 (0x1fc) == 0x44, "[--sp] = rets");
}

void shiftAndAddFields()
{
    FlatRam ram (12);
    ram.put (0, { 0xa2, 0xa2, 0x9a, 0xa2, 0x93, 0x1c });
    Cpu cpu (0);
    cpu.regs[1] = 0xfffffff0; cpu.regs[2] = 0x9e7c;
    step (cpu, ram);
    CHECK_EQ (cpu.regs[2], 0x9e7c >> 2, "r2 = r2 >> 2");
    step (cpu, ram);
    CHECK_EQ (cpu.regs[2], 0xfffffffc, "r2 = r1 >>> 2");
    step (cpu, ram);
    CHECK_EQ (cpu.regs[3], 0xfffffff0u + 0xfffffffcu, "r3 = r1 + r2");
}

void repBlockRegisterCount()
{
    FlatRam ram (0x100);
    ram.put (0, { 0x12, 0x03, 0x13, 0x05, 0xc3, 0x05, 0x80, 0x00 });  // rep 4 r2 { r3 = [r1++=4]; [r4++=4] = r3 }; rts
    for (uint32_t i = 0; i < 3; ++i) ram.put32 (0x40 + 4 * i, 0x1111 * (i + 1));
    Cpu cpu (0);
    cpu.regs[1] = 0x40; cpu.regs[2] = 3; cpu.regs[4] = 0x80;
    for (int i = 0; i < 7; ++i) step (cpu, ram);
    CHECK_EQ (cpu.pc, 6, "el bloque corrió 3 veces y siguió");
    CHECK_EQ (cpu.regs[2], 0, "r2");
    CHECK_EQ (cpu.regs[1], 0x4c, "r1");
    CHECK_EQ (cpu.regs[4], 0x8c, "r4");
    CHECK_EQ (ram.read32 (0x88), 0x3333, "copia");
    FlatRam ram2 (10);
    ram2.put (0, { 0x02, 0x03, 0xb1, 0x05, 0x80, 0x00 });
    Cpu cpu2 (0);
    cpu2.regs[2] = 0;
    step (cpu2, ram2);
    CHECK_EQ (cpu2.pc, 4, "cuenta 0: salta el bloque");
}

// run1 / run_regs de fm1-core: una instrucción en 0, r0..r15 = 0x1000 + 0x100·n, sp = 0x2000
struct Run1 { Cpu cpu; FlatRam ram; };
Run1 run1 (std::initializer_list<uint8_t> bytes, std::initializer_list<std::pair<size_t, uint32_t>> pokes)
{
    Run1 x { Cpu (0), FlatRam (0x4000) };
    for (size_t a = 0x40; a < 0x4000; a += 4) x.ram.put32 (a, (uint32_t) a);
    for (const auto& [a, v] : pokes) x.ram.put32 (a, v);
    x.ram.put (0, bytes);
    for (int n = 0; n < 16; ++n) x.cpu.regs[n] = 0x1000u + 0x100u * (uint32_t) n;
    x.cpu.sp = 0x2000;
    step (x.cpu, x.ram);
    return x;
}

Cpu runRegs (std::initializer_list<uint8_t> bytes, std::initializer_list<std::pair<int, uint32_t>> regs)
{
    FlatRam ram (0x4000);
    ram.put (0, bytes);
    Cpu cpu (0);
    for (int n = 0; n < 16; ++n) cpu.regs[n] = 0x1000u + 0x100u * (uint32_t) n;
    for (const auto& [r, v] : regs) cpu.regs[r] = v;
    cpu.sp = 0x2000;
    step (cpu, ram);
    return cpu;
}

void pairHeadSregMoves()
{
    const auto run = [] (std::initializer_list<uint8_t> b, auto set)
    {
        FlatRam ram (0x100);
        ram.put (0, b);
        Cpu cpu (0);
        set (cpu);
        step (cpu, ram);
        step (cpu, ram);
        return cpu;
    };
    CHECK_EQ (run ({ 0x64, 0xf0, 0x00, 0x23 }, [] (Cpu& c) { c.rets = 0x02012345; }).regs[2], 0x02012345, "r2 = rets #");
    CHECK_EQ (run ({ 0x64, 0xf0, 0x80, 0xa3 }, [] (Cpu& c) { c.regs[10] = 0x02001000; }).rets, 0x02001000, "rets = r10 #");
    CHECK_EQ (run ({ 0x64, 0xf0, 0x00, 0xae }, [] (Cpu& c) { c.sp = 0x1f80; }).regs[10], 0x1f80, "r10 = sp #");
}

void bitTestBothWays()
{
    const auto run = [] (std::initializer_list<uint8_t> b, int r, uint32_t v)
    {
        FlatRam ram (0x100);
        ram.put (0x80, b);
        Cpu cpu (0x80);
        cpu.regs[r] = v;
        step (cpu, ram);
        return cpu.pc;
    };
    CHECK_EQ (run ({ 0x60, 0xff, 0x01, 0x00, 0xfe, 0xff }, 0, 2), 0x82, "== 0 tomado");
    CHECK_EQ (run ({ 0x60, 0xff, 0x01, 0x00, 0xfe, 0xff }, 0, 1), 0x86, "== 0 no tomado");
    CHECK_EQ (run ({ 0x61, 0xff, 0x03, 0x50, 0x00, 0x80 }, 5, 2), 0x86u - 0x10000u, "!= 0 tomado hacia atrás");
    CHECK_EQ (run ({ 0x61, 0xff, 0x03, 0x50, 0x00, 0x80 }, 5, 4), 0x86, "!= 0 no tomado");
}

void dspEncodings()
{
    CHECK_EQ (runRegs ({ 0xde, 0xe1, 0x6d, 0x39 }, { { 7, 1 }, { 6, 0 } }).regs[3], 8, "r3 = r7_r6 >> 29 (s)");
    CHECK_EQ (runRegs ({ 0xde, 0xe1, 0x6d, 0x39 }, { { 7, 0x40000000 }, { 6, 0 } }).regs[3], 0x7fffffff, "saturado +");
    CHECK_EQ (runRegs ({ 0xde, 0xe1, 0x6d, 0x39 }, { { 7, 0xffffffff }, { 6, 0xe0000000 } }).regs[3], 0xffffffff, "-1");
    CHECK_EQ (runRegs ({ 0xde, 0xe1, 0x6d, 0x39 }, { { 7, 0x80000000 }, { 6, 0 } }).regs[3], 0x80000000, "saturado -");
    CHECK_EQ (runRegs ({ 0xde, 0xe1, 0x01, 0x02 }, { { 1, 0xfffffffe }, { 0, 5 } }).regs[0], 0x7fffffff, "r0 = r1_r0 >> 33 (u)");
    {
        // d4 f1 05 eb: la rotación con acarreo (bit 27) no es la rotación de par: se para
        FlatRam ram (0x4000);
        ram.put (0, { 0xd4, 0xf1, 0x05, 0xeb });
        Cpu cpu (0);
        ++checks;
        if (cpu.step (ram)) { ++failures; std::printf ("  FALLA %s: d4 f1 05 eb debía parar\n", currentTest); }
    }
    CHECK_EQ (runRegs ({ 0x39, 0xe4, 0x20, 0x13 }, { { 2, 0x7ffffff0 }, { 3, 0x100 } }).regs[1], 0x7fffffff, "r1 = r2 + r3 (ssat)");
    CHECK_EQ (runRegs ({ 0x39, 0xe4, 0x20, 0x13 }, { { 2, 5 }, { 3, 0xfffffff9u } }).regs[1], 0xfffffffeu, "5 + -7");
    CHECK_EQ (runRegs ({ 0x39, 0xe4, 0x28, 0x13 }, { { 2, 0x80000010 }, { 3, 0x100 } }).regs[1], 0x80000000, "r1 = r2 - r3 (ssat)");
    CHECK_EQ (runRegs ({ 0x39, 0xe4, 0xe0, 0xf9 }, { { 14, 3 }, { 9, 4 } }).regs[15], 7, "r15 = r14 + r9 (ssat)");
    CHECK_EQ (runRegs ({ 0xc4, 0xe1, 0x73, 0x61 }, { { 7, 0x00080001 } }).regs[6], std::rotr (0x00080001u, 19), "r6 = r7 <> 19");
    CHECK_EQ (runRegs ({ 0xc4, 0xe1, 0xaf, 0x91 }, { { 10, 1 } }).regs[9], 2, "r9 = r10 <> 31");
    CHECK_EQ (runRegs ({ 0x78, 0xe0, 0x01, 0x35 }, { { 5, 0x12345 } }).regs[3], 32767, "sat16 (s)");
    CHECK_EQ (runRegs ({ 0x78, 0xe0, 0x00, 0x35 }, { { 5, 0xfffffffbu } }).regs[3], 0, "sat16 (u)");
    CHECK_EQ (runRegs ({ 0x20, 0xe0, 0xd0, 0xec }, { { 14, 0x12345678 } }).regs[14], 0xd0345678, "r14.b3 = 208");
    CHECK_EQ (runRegs ({ 0x20, 0xe0, 0xff, 0x24 }, { { 2, 0x12345678 } }).regs[2], 0x1234ff78, "r2.b1 = 255");
    CHECK_EQ (runRegs ({ 0x94, 0xe1, 0x13, 0x16 }, { { 1, 0xff }, { 6, 3 } }).regs[1], 0xf7, "r1 & ~(1 << r6)");
}

void builtinEncodings()
{
    CHECK_EQ (runRegs ({ 0x08, 0xa1 }, { { 0, 0x7fffffff } }).regs[0], 0x7fffffff, "<<< satura");
    CHECK_EQ (runRegs ({ 0x08, 0xa1 }, { { 0, 0x30000000 } }).regs[0], 0x60000000, "<<< 1");
    CHECK_EQ (runRegs ({ 0x18, 0x1a }, { { 0, 0xfffffffdu }, { 1, 31 } }).regs[0], 0x80000000, "r0 <<<= r1");
    CHECK_EQ (runRegs ({ 0xc0, 0xe1, 0xa9, 0x14 }, { { 10, 0x00400000 } }).regs[1], 0x7fffffff, "r1 = r10 <<< 9 satura");
    CHECK_EQ (runRegs ({ 0xc0, 0xe1, 0xa9, 0x14 }, { { 10, 3 } }).regs[1], 3 << 9, "r1 = r10 <<< 9");
    CHECK_EQ (runRegs ({ 0xc8, 0xe1, 0x11, 0x1a }, { { 1, 0xffffffffu }, { 10, 4 } }).regs[1], 0xfffffff0u, "r1 <<< r10");
    CHECK_EQ (runRegs ({ 0x70, 0xe0, 0x80, 0x14 }, { { 4, 0x12340080 } }).regs[1], 0xffff8000, "revs(r4.l)");
    auto c = runRegs ({ 0xd4, 0xe1, 0x01, 0x41 }, { { 5, 0 }, { 4, 1 } });
    CHECK (c.regs[5] == 0x8000 && c.regs[4] == 0, "r5_r4 <>= 17");
    c = runRegs ({ 0xdc, 0xe1, 0x00, 0x43 }, { { 5, 0 }, { 4, 1 }, { 3, 33 } });
    CHECK (c.regs[5] == 0 && c.regs[4] == 0x80000000, "r5_r4 <>= r3");
    CHECK_EQ (runRegs ({ 0xdf, 0xe1, 0xc1, 0x95 }, { { 13, 0xfffffff0 }, { 12, 0 }, { 5, 4 } }).regs[9], 0x80000000, "r9 = r13_r12 >> r5 (s)");
    CHECK_EQ (runRegs ({ 0xdf, 0xe1, 0x20, 0x05 }, { { 3, 1 }, { 2, 0x10 }, { 5, 4 } }).regs[0], 0x10000001, "r0 = r3_r2 >> r5 (u)");
    CHECK_EQ (runRegs ({ 0x80, 0xe1, 0x01, 0x14 }, { { 4, 0xffff00ff } }).regs[1], 15, "cls");
    CHECK_EQ (runRegs ({ 0x80, 0xe1, 0x01, 0x14 }, { { 4, 1 } }).regs[1], 30, "cls(1)");
    CHECK_EQ (runRegs ({ 0xb0, 0xe0, 0x00, 0x14 }, { { 1, 0x80000000 }, { 4, 0x80000000 } }).regs[1], 0x40000000, "brev add");
}

void vendorEncodings()
{
    auto x = run1 ({ 0xdc, 0xec, 0xcc, 0xbc }, {});
    CHECK (x.cpu.regs[11] == 0x1c00 && x.cpu.regs[12] == 0x1c00 - 820, "r11 = [r12++=-820]");
    x = run1 ({ 0xdf, 0xec, 0x00, 0x4f }, {});
    CHECK (x.cpu.regs[4] == 0x1000 && x.cpu.regs[0] == 0x1000 - 16, "r4 = [r0++=-16]");
    x = run1 ({ 0xd8, 0xec, 0x05, 0x1f }, {});
    CHECK (x.ram.read32 (0x1000) == 0x1100 && x.cpu.regs[0] == 0x1000 + 244, "[r0++=244] = r1");
    x = run1 ({ 0x5e, 0xec, 0x02, 0x21 }, {});
    CHECK (x.cpu.regs[2] == 0x1000 && x.cpu.regs[3] == 0x1004 && x.cpu.regs[0] == 0x2100, "r3_r2 = d[r0++=r1]");
    x = run1 ({ 0xdc, 0xe9, 0x00, 0x40 }, {});
    CHECK (x.cpu.regs[4] == 0 && x.cpu.regs[8] == 0x1800, "r4 = b[sp] (u)");
    x = run1 ({ 0xd8, 0xe9, 0x00, 0xf4 }, { { 0x2400, 0x9234 } });
    CHECK_EQ (x.cpu.regs[15], 0x9234, "r15 = h[sp+1024] (u)");
    x = run1 ({ 0xd9, 0xe9, 0x00, 0xf4 }, { { 0x2400, 0x9234 } });
    CHECK_EQ (x.cpu.regs[15], 0xffff9234, "r15 = h[sp+1024] (s)");
    x = run1 ({ 0xd8, 0xe9, 0x03, 0x40 }, {});
    CHECK_EQ (x.ram.read32 (0x2000), (0x1400u << 16) | 0x2000u, "h[sp+2] = r4");
    x = run1 ({ 0xd0, 0xed, 0x02, 0x39 }, { { 0x1000, 0x8001 } });
    CHECK (x.cpu.regs[3] == 0x8001 && x.cpu.regs[0] == 0x1000 + 146, "r3 = h[r0++=146] (u)");
    x = run1 ({ 0xd4, 0xed, 0x02, 0x39 }, { { 0x1000, 0x8001 } });
    CHECK (x.cpu.regs[3] == 0xffff8001 && x.cpu.regs[0] == 0x1000 + 146, "r3 = h[r0++=146] (s)");
}

// Una ventana XIP: las direcciones se reducen a 64 KiB (Xip de la prueba de fm1-core)
struct XipRam : FlatRam
{
    XipRam() : FlatRam (0x10000) {}
    uint8_t read8 (uint32_t a) override { return FlatRam::read8 (a & 0xffff); }
    void write8 (uint32_t a, uint8_t v) override { FlatRam::write8 (a & 0xffff, v); }
};

void blockMovesUpAndDown()
{
    const struct { uint8_t op; uint32_t at, base; } cases[] = {
        { 0x00, 0x1000, 0x1000 }, { 0x10, 0x1000, 0x1008 }, { 0x20, 0x1000, 0x1000 }, { 0x30, 0x1000, 0x1008 },
        { 0x40, 0x0ff8, 0x1000 }, { 0x50, 0x0ff8, 0x0ff8 }, { 0x60, 0x0ff8, 0x1000 }, { 0x70, 0x0ff8, 0x0ff8 } };
    for (const auto& c : cases)
    {
        const bool store = (c.op & 0x20) != 0;
        XipRam bus;
        bus.put (0, { c.op, 0xeb, 0x06, 0x00 });
        const uint32_t words[] = { 0x0ff8, 0x0ffc, 0x1000, 0x1004 };
        for (uint32_t k = 0; k < 4; ++k) bus.put32 (words[k], 0xa0 + k);
        Cpu cpu (0x02000000);
        cpu.regs[0] = 0x1000; cpu.regs[1] = 0x11; cpu.regs[2] = 0x22;
        step (cpu, bus);
        const auto lo = bus.read32 (c.at), hi = bus.read32 (c.at + 4);
        if (store) CHECK (lo == 0x11 && hi == 0x22, "guardado");
        else CHECK (cpu.regs[1] == lo && cpu.regs[2] == hi && lo != 0, "cargado");
        CHECK_EQ (cpu.regs[0], c.base, "la base");
    }
    // 41 eb 0a 00: {r1, r3} = [-r1]; la base, también en la lista, toma su palabra cargada
    XipRam bus;
    bus.put (0, { 0x41, 0xeb, 0x0a, 0x00 });
    bus.put (0x0ff8, { 1, 0, 0, 0, 2, 0, 0, 0 });
    Cpu cpu (0x02000000);
    cpu.regs[1] = 0x1000; cpu.regs[3] = 0x33;
    step (cpu, bus);
    CHECK (cpu.regs[1] == 1 && cpu.regs[3] == 2, "{r1, r3} = [-r1]");
}

// ---- paquetes .fwsc (pruebas de fm1-soc/src/fwsc.rs) -------------------------------------------------------------------
std::vector<uint8_t> fwscHeader (const char* id, size_t n)
{
    std::vector<uint8_t> f (n * 0x30 + 0x100, 0);
    const auto len = std::strlen (id);
    for (size_t i = 0; i < n; ++i)
        f[i * 0x30 + 0x2f] = i < len ? (uint8_t) ((uint8_t) id[i] + (uint8_t) (i + 1)) : (uint8_t) 0x7d;
    f[n * 0x30 + 0x2f] = 0x12;
    return f;
}

void fwscContainerIdentity()
{
    std::string id;
    CHECK_EQ (fwscContainer (fwscHeader ("FM-1_015", 20), id), 20, "bloques FM-1");
    CHECK (id == "FM-1_015", "identidad FM-1_015");
    CHECK_EQ (fwscContainer (fwscHeader ("SMK-37 Pro_016", 36), id), 36, "bloques SMK-37");
    CHECK (id == "SMK-37 Pro_016", "identidad SMK-37 Pro_016");
}

void fwscDefaultBank()
{
    const auto allEq = [] (const std::vector<uint8_t>& v, size_t a, size_t b, uint8_t x)
    { return std::all_of (v.begin() + (std::ptrdiff_t) a, v.begin() + (std::ptrdiff_t) b, [x] (uint8_t c) { return c == x; }); };
    std::vector<uint8_t> fm (1 << 20, 0xff);
    size_t usr = 0xea000;
    CHECK (usrFillDefaults (fm, usr, UsrLayout::fm1), "banco por defecto FM-1");
    CHECK (std::memcmp (&fm[usr + 0x4000 + 118], "EMU E.PNO ", 10) == 0, "primera voz");
    CHECK (allEq (fm, usr + 0x10000, usr + 0x10000 + 128 * 59, 0), "ajustes a 0");
    CHECK (! usrFillDefaults (fm, usr, UsrLayout::fm1), "un banco escrito se queda");

    std::vector<uint8_t> smk (1 << 20, 0xff);
    usr = 0xf4000;
    CHECK (usrFillDefaults (smk, usr, UsrLayout::smk37), "banco por defecto SMK-37");
    CHECK (std::memcmp (&smk[usr + 118], "EMU E.PNO ", 10) == 0, "voz 0");
    CHECK (std::memcmp (&smk[usr + 128 + 118], "EMU BASS  ", 10) == 0, "voz 1");
    CHECK (std::memcmp (&smk[usr + 0x1000 + 118], "INIT VOICE", 10) == 0, "banco 1, patch 0");
    CHECK (allEq (smk, usr + 0x4000, usr + 0x9200, 0), "registros de 163 bytes y el bloque siguiente");
    CHECK (allEq (smk, usr + 0x9200, usr + 0xa000, 0xff), "el resto intacto");
    CHECK (allEq (smk, 0, usr, 0xff) && allEq (smk, usr + 0xa000, smk.size(), 0xff), "fuera del área intacto");
    std::string err;
    const std::vector<uint8_t> bank (4096, 0x23);
    CHECK_EQ (usrLoadSyx (smk, usr, UsrLayout::smk37, bank, 0, err), 32, "32 voces");
    CHECK (allEq (smk, usr, usr + 4096, 0x23), "voces cargadas");
    CHECK (std::memcmp (&smk[usr + 4096 + 118], "INIT VOICE", 10) == 0, "la ranura 32 se conserva");
}

std::vector<uint8_t> readFile (const char* path)
{
    std::ifstream in (path, std::ios::binary);
    return std::vector<uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

// Desempaqueta un .fwsc; si se da un app.bin extraído por otra herramienta (jl-misctools), lo compara byte a byte
void checkFwsc (const char* path, const char* appBin)
{
    const auto file = readFile (path);
    FwscUnpacked u;
    std::string err;
    ++checks;
    if (! fwscUnpack (file, u, err))
    {
        ++failures;
        std::printf ("  FALLA %s: %s\n", path, err.c_str());
        return;
    }
    std::printf ("  %s: identidad \"%s\", flash %zu B, área 0x%zx (+%zu B), fin protegido 0x%zx, clave 0x%04x\n", path,
                 u.identity.c_str(), u.raw.size(), u.area, u.size, u.end, u.key);
    for (const auto& h : u.dir)
        std::printf ("    %-14s off 0x%06zx  size %7zu  flags 0x%02x\n", h.name.c_str(), h.offset, h.size, h.flags);
    if (appBin == nullptr)
        return;
    const auto ref = readFile (appBin);
    const auto* app = u.entry ("app.bin");
    ++checks;
    if (app == nullptr || app->offset + ref.size() > u.xip.size()
        || std::memcmp (u.xip.data() + app->offset, ref.data(), ref.size()) != 0 || app->size != ref.size())
    {
        ++failures;
        std::printf ("  FALLA %s: app.bin distinto de %s (%zu B)\n", path, appBin, ref.size());
    }
    else
        std::printf ("    app.bin idéntico a %s (%zu B)\n", appBin, ref.size());
}

// ---- dispositivos y periféricos (pruebas de fm1-soc: ram.rs, lcd.rs, p33.rs, periph.rs) ----------------------------------
void ramRoundtrip()
{
    Ram ram (64);
    ram.write32 (0, 0xdeadbeef);
    CHECK_EQ (ram.read32 (0), 0xdeadbeef, "read32");
    CHECK_EQ (ram.read16 (0), 0xbeef, "read16");
    CHECK_EQ (ram.read8 (1), 0xbe, "read8");
    ram.write16 (8, 0x1234);
    CHECK (ram.read8 (8) == 0x34 && ram.read8 (9) == 0x12, "write16 little-endian");
}

void lcdRevision()
{
    Lcd lcd;
    lcd.feed (0x2c, false);
    lcd.feed (0x12, true);
    CHECK_EQ (lcd.revision, 0, "medio píxel");
    lcd.feed (0x34, true);
    CHECK_EQ (lcd.revision, 1, "un píxel");
    CHECK_EQ (lcd.frames, 0, "una ventana parcial también llega a la pantalla");
    lcd.feed (0x2c, false);
    lcd.feed (0x12, true);
    lcd.feed (0x34, true);
    CHECK_EQ (lcd.revision, 1, "redibujo idéntico");
    lcd.feed (0x01, false);
    CHECK_EQ (lcd.revision, 2, "reset");
    CHECK_EQ (lcd.fb[0], 0, "framebuffer borrado");
}

uint8_t p33Access (P33& p, uint8_t cmd, uint8_t addr, uint8_t data, bool rtc)
{
    const uint32_t cs = 1 | (rtc ? 0x100u : 0u);
    p.write (P33::kCon, cs);
    uint8_t last = 0;
    for (uint8_t b : { cmd, addr, data })
    {
        p.write (P33::kDat, b);
        p.write (P33::kCon, p.read (P33::kCon) | 0x10);
        CHECK_EQ (p.read (P33::kCon) & 2, 0, "nunca ocupado");
        last = (uint8_t) p.read (P33::kDat);
    }
    p.write (P33::kCon, p.read (P33::kCon) & ~0x101u);
    return last;
}

void p33ReadsBack()
{
    P33 p;
    CHECK_EQ (p33Access (p, 0x80, 0x05, 0x5e, false), 0, "0 al encender");
    p33Access (p, 0x00, 0x05, 0x40, false);
    p33Access (p, 0x20, 0x05, 0x04, false);
    CHECK_EQ (p33Access (p, 0x80, 0x05, 0x5e, false), 0x44, "escribir y or");
    p33Access (p, 0x40, 0x05, 0xfb, false);
    p33Access (p, 0x60, 0x05, 0x30, false);
    CHECK_EQ (p33Access (p, 0x80, 0x05, 0x5e, false), 0x70, "and y xor");
    p33Access (p, 0x01, 0x05, 0x11, false);
    p33Access (p, 0x00, 0x05, 0x22, true);
    CHECK (p.reg (0x105, false) == 0x11 && p.reg (0x05, false) == 0x70 && p.reg (0x05, true) == 0x22, "bits altos y banco RTC");
}
} // namespace

namespace fm1
{
// Acceso al estado interno de Periph para las pruebas (amiga de la clase)
struct PeriphTest
{
    static uint16_t& srLatched (Periph& p) { return p.srLatched; }
    static uint32_t& paOut (Periph& p) { return p.paOut; }
    static uint64_t& ledLast (Periph& p) { return p.ledLast; }
    static std::array<uint64_t, 41>& ledAcc (Periph& p) { return p.ledAcc; }
    static uint64_t nextEvent (const Periph& p) { return p.nextEvent(); }
    static uint64_t midiByteTime (const Periph& p) { return p.midiByteTime(); }
};
} // namespace fm1

namespace
{
uint32_t adcConvert (Periph& p, uint32_t ch)
{
    p.write (0x13100, 0x40 | ch << 8 | 0x0e);
    p.write (0x13100, 0x40 | 0x10 | ch << 8 | 0x0e);
    return *p.read (0x13104);
}

void periphRowsReadPill()
{
    Periph p;
    const auto ms = p.hz / 1000;
    p.write (0x50008, 1 << 7);  // fila PA7 de entrada con su pull-up, columna 0 fijada: la tecla 9 (id 23)
    p.write (0x50010, 1 << 7);
    PeriphTest::srLatched (p) = (uint16_t) ~1;
    CHECK_EQ (adcConvert (p, 0), 1023, "abierta");
    p.apply ({ PanelAction::KeyVel, 0, 127 });
    p.apply ({ PanelAction::Key, 23, 1 });
    p.cycles += 2 * ms;
    auto v = adcConvert (p, 0);
    CHECK (v >= 193 && v <= 196, "a fondo con pull-up: el diodo y poco más");
    p.write (0x50008, 0);
    PeriphTest::paOut (p) |= 1 << 7;
    p.write (0x50018, 1 << 7);
    p.write (0x5001c, 1 << 7);
    v = adcConvert (p, 0);
    CHECK (v >= 804 && v <= 808, "conducida alta con el driver más fuerte: ~806");
    p.apply ({ PanelAction::Key, 23, 0 });
    p.cycles += 10 * ms;
    CHECK_EQ (adcConvert (p, 0), 1023, "levantada");
    p.apply ({ PanelAction::KeyVel, 0, 1 });
    p.apply ({ PanelAction::Key, 23, 1 });
    p.cycles += 2 * ms;
    v = adcConvert (p, 0);
    CHECK (v >= 1004 && v <= 1012, "una pulsación ligera lee más alto");
    p.apply ({ PanelAction::Pressure, 23, 127 });
    p.cycles += 25 * ms;
    CHECK (adcConvert (p, 0) < 810, "apretada más fuerte");
    PeriphTest::srLatched (p) = (uint16_t) ~2;
    CHECK_EQ (adcConvert (p, 0), 1023, "otra columna: su tecla está abierta");
    PeriphTest::paOut (p) &= ~(1u << 7);
    CHECK_EQ (adcConvert (p, 0), 0, "conducida baja");
    CHECK_EQ (adcConvert (p, 4), 900, "el potenciómetro MASTER");
}

void runTo (Periph& p, uint64_t cycle, std::vector<std::pair<uint32_t, uint8_t>>& got)
{
    while (p.cycles < cycle)
    {
        const auto stepN = std::max (std::min (PeriphTest::nextEvent (p), cycle), p.cycles + 1) - p.cycles;
        if (const auto w = p.advance (stepN).uartRx)
            got.push_back (*w);
    }
}

void periphUartMidiIn()
{
    Periph p;
    p.write (0x1211c, 0x1000);  // la configuración de Felucca con un anillo de 4 bytes y las IRQ de RX y timeout
    p.write (0x12120, 0x1004);
    p.write (0x12124, 4);
    p.write (0x12110, 48'000);  // 1 ms de silencio
    p.write (0x12100, 0x40 | 0x08 | 0x20 | 1);
    for (int b : { 0x90, 0x3c, 0x64, 0x80, 0x3c })
        p.apply ({ PanelAction::Midi, 0, b });
    const auto byte = PeriphTest::midiByteTime (p);
    std::vector<std::pair<uint32_t, uint8_t>> got;
    runTo (p, 3 * byte, got);
    CHECK (got.size() == 3 && got[0] == std::make_pair (0x1000u, (uint8_t) 0x90) && got[1] == std::make_pair (0x1001u, (uint8_t) 0x3c)
               && got[2] == std::make_pair (0x1002u, (uint8_t) 0x64),
           "un tiempo de byte entre bytes");
    CHECK (! p.pendingIrqFor (0) && ! p.anyPending(), "IRQ 20 aún sin habilitar");
    p.icfg[kIrqUart1 >> 3] |= 1u << ((kIrqUart1 & 7) * 4);
    runTo (p, 5 * byte, got);
    CHECK (got.size() == 5 && got[3] == std::make_pair (0x1003u, (uint8_t) 0x80) && got[4] == std::make_pair (0x1000u, (uint8_t) 0x3c),
           "el anillo da la vuelta");
    CHECK_EQ (*p.read (0x12100) & 0x4000, 0x4000, "RXCNT bytes: RX pendiente");
    CHECK (p.pendingIrqFor (0) && p.pendingIrqFor (0)->first == kIrqUart1, "IRQ 20");
    auto con = *p.read (0x12100);
    p.write (0x12100, con | 0x80);
    con = *p.read (0x12100);
    p.write (0x12100, con | 0x1000);
    CHECK_EQ (*p.read (0x12128), 5, "HRXCNT");
    CHECK (! p.pendingIrqFor (0), "reconocida");
    const auto ms = p.hz / 1000;
    runTo (p, 5 * byte + ms + 1, got);
    CHECK_EQ (*p.read (0x12100) & 0x800, 0x800, "timeout tras 1 ms de silencio");
    CHECK (p.pendingIrqFor (0) && p.pendingIrqFor (0)->first == kIrqUart1, "IRQ 20 del timeout");
    con = *p.read (0x12100);
    p.write (0x12100, con | 0x80 | 0x400);
    CHECK_EQ (*p.read (0x12128), 0, "HRXCNT a 0");
    CHECK (! p.pendingIrqFor (0), "timeout reconocido");
    p.write (0x12100, 0);
    p.apply ({ PanelAction::Midi, 0, 0xf8 });
    runTo (p, 7 * byte + ms, got);
    CHECK (got.size() == 5 && p.midiDropped() == 1, "con la UART apagada el byte se descarta");
}

void periphAudioFrames()
{
    CHECK_EQ (Periph::audioFramesAt (0, kCpuHz), 0, "0");
    CHECK_EQ (Periph::audioFramesAt (5440, kCpuHz), 1, "una trama");
    CHECK_EQ (Periph::audioFramesAt (kCpuHz, kCpuHz), 44117, "44 117 tramas por segundo");
    CHECK_EQ (Periph::audioFramesAt (160'000'000, 160'000'000), 44117, "sea cual sea el reloj de CPU");
    CHECK_EQ (Periph::audioFramesAt (3600 * kCpuHz, kCpuHz), 3600ull * 44117 + 2329, "una hora");
}

void periphLedPeek()
{
    Periph p;
    p.cycles = 1000;
    PeriphTest::ledLast (p) = 1000;
    PeriphTest::ledAcc (p)[5] = 500;
    CHECK (p.ledPeek()[5] == 0.5f, "mitad del tiempo");
    CHECK (p.ledPeek()[5] == 0.5f, "mirar no reinicia la ventana");
    CHECK (p.ledTake()[5] == 0.5f, "tomar cubre todo");
    p.cycles = 2000;
    PeriphTest::ledLast (p) = 2000;
    CHECK (p.ledTake()[5] == 0.0f, "tomar reinicia");
}

// ---- decodificador contra un listado del objdump de JieLi --------------------------------------------------------------
bool parseObjdumpLine (const std::string& line, uint32_t& addr, std::vector<uint8_t>& bytes)
{
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    const auto hex = [] (char c) { return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1; };
    addr = 0;
    size_t start = i;
    while (i < line.size() && hex (line[i]) >= 0) addr = addr * 16 + (uint32_t) hex (line[i++]);
    if (i == start || i >= line.size() || line[i] != ':') return false;
    ++i;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    bytes.clear();
    // pares "xx " separados por un espacio; el texto va tras un tabulador
    while (i + 2 < line.size() && hex (line[i]) >= 0 && hex (line[i + 1]) >= 0 && line[i + 2] == ' ')
    {
        bytes.push_back ((uint8_t) (hex (line[i]) * 16 + hex (line[i + 1])));
        i += 3;
    }
    if (bytes.empty()) return false;
    while (i < line.size() && line[i] == ' ') ++i;
    return i < line.size() && line[i] == '\t';
}

void checkListing (const char* path)
{
    std::ifstream in (path);
    if (! in)
    {
        std::printf ("  no se puede abrir %s\n", path);
        ++failures;
        return;
    }
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> rows;
    std::map<uint32_t, uint8_t> mem;
    std::string line;
    uint32_t addr;
    std::vector<uint8_t> b;
    while (std::getline (in, line))
        if (parseObjdumpLine (line, addr, b))
        {
            for (size_t k = 0; k < b.size(); ++k) mem.emplace (addr + (uint32_t) k, b[k]);
            rows.emplace_back (addr, b);
        }
    size_t same = 0, diff = 0, unknown = 0;
    std::map<std::string, size_t> diffByClass;
    for (const auto& [a, bytes] : rows)
    {
        uint64_t win = 0;
        for (uint32_t k = 0; k < 8; ++k)
        {
            const auto it = mem.find (a + k);
            if (it != mem.end()) win |= (uint64_t) it->second << (8 * k);
        }
        // ventana de 6 bytes, como check_isa.py y decode_win
        const auto* e = decodeWin (win & 0xffffffffffffull);
        if (e == nullptr) ++unknown;
        else if (e->len == bytes.size()) ++same;
        else { ++diff; ++diffByClass[e->name]; }
    }
    std::printf ("  %s: %zu instrucciones, %zu misma longitud (%.3f %%), %zu distinta, %zu desconocidas\n", path, rows.size(), same,
                 100.0 * (double) same / (double) std::max<size_t> (1, rows.size()), diff, unknown);
    for (const auto& [n, c] : diffByClass)
        std::printf ("    %6zu  %s\n", c, n.c_str());
    ++checks;
    if (diff != 0 || unknown != 0 || rows.empty())
        ++failures;
}
} // namespace

int main (int argc, char** argv)
{
    const struct { const char* name; void (*fn)(); } tests[] = {
        { "nop_runs_at_entry", nopRunsAtEntry },
        { "goto_rel_jumps", gotoRelJumps },
        { "mov_imm_l2_loads_algebra", movImmL2 },
        { "call_l6_jumps_and_sets_rets", callL6 },
        { "unsupported_class_halts", unsupportedHalts },
        { "lockset_retries_while_the_other_core_holds_it", locksetRetries },
        { "composed_matches_observed_table", composedMatchesObservedTable },
        { "alu4_shapes", alu4Shapes },
        { "mem_shift_by_register", memShiftByRegister },
        { "carry_flags_64bit_add_sub", carryFlags64 },
        { "pair_head_and_not_shifted_runs", pairHeadAndNot },
        { "and_not_takes_a_register_above_r7", andNotAboveR7 },
        { "corpus_unseen_offsets_decode", corpusUnseenOffsets },
        { "word_negative_offsets", wordNegativeOffsets },
        { "sreg_moves_any_register", sregMovesAnyRegister },
        { "icfg_restores_the_global_enable (step)", icfgRestoresEnable },
        { "if_block_with_else", ifBlockWithElse },
        { "a_goto_back_out_of_a_then_part_ends_the_construct", gotoBackOutOfThenPart },
        { "register_increment_accesses", registerIncrementAccesses },
        { "sp_pair_store_and_rev8", spPairStoreAndRev8 },
        { "pair_multiply_accumulate", pairMultiplyAccumulate },
        { "pair_multiply_subtract", pairMultiplySubtract },
        { "pair_multiply_subtract_dual_issue", pairMultiplySubtractDualIssue },
        { "multiply_immediate_dual_issue", multiplyImmediateDualIssue },
        { "pair_store_and_load", pairStoreAndLoad },
        { "pair_head_byte_extract", pairHeadByteExtract },
        { "shift_imm_high_source_register", shiftImmHighSource },
        { "sp_pair_store_large_offset", spPairStoreLargeOffset },
        { "pair_access_modes", pairAccessModes },
        { "bitfield_extract_is_not_an_add", bitfieldExtractIsNotAnAdd },
        { "fpu_ops_and_float_compare", fpuOpsAndFloatCompare },
        { "short_shift_by_32", shortShiftBy32 },
        { "push_pop_register_mask", pushPopRegisterMask },
        { "rets_mask_pop_and_pair_micro_ops (step)", retsMaskPopAndPairs },
        { "shift_and_add_fields", shiftAndAddFields },
        { "rep_block_runs_register_count_times", repBlockRegisterCount },
        { "pair_head_special_register_moves", pairHeadSregMoves },
        { "bit_test_branches_both_ways", bitTestBothWays },
        { "dsp_encodings", dspEncodings },
        { "builtin_encodings", builtinEncodings },
        { "vendor_encodings", vendorEncodings },
        { "block_moves_up_and_down (step)", blockMovesUpAndDown },
        { "fwsc container_counts_blocks_and_reads_the_identity", fwscContainerIdentity },
        { "fwsc default_bank_in_each_boards_usr_layout", fwscDefaultBank },
        { "ram roundtrip_all_widths", ramRoundtrip },
        { "lcd revision_tracks_partial_updates_and_reset", lcdRevision },
        { "p33 reads_back_what_was_written_and_combined", p33ReadsBack },
        { "periph the_rows_read_a_keys_pill", periphRowsReadPill },
        { "periph uart_midi_in_dma_latch_and_timeout", periphUartMidiIn },
        { "periph audio_frames_follow_the_dac_clock", periphAudioFrames },
        { "periph led_peek_reads_the_fractions_without_taking_them", periphLedPeek },
        { "fast_path_matches_exec_syntax (todas las clases)", fastPathMatchesExecSyntax },
    };
    std::printf ("Tabla pi32v2: %zu clases\n", entryCount());
    for (const auto& t : tests)
    {
        currentTest = t.name;
        const int before = failures;
        t.fn();
        std::printf ("%s %s\n", failures == before ? "ok   " : "FALLA", t.name);
    }
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp (argv[i], "--listing") == 0)
        {
            currentTest = argv[i + 1];
            checkListing (argv[++i]);
        }
        else if (std::strcmp (argv[i], "--fwsc") == 0)
        {
            currentTest = argv[i + 1];
            const char* app = (i + 3 < argc && std::strcmp (argv[i + 2], "--app") == 0) ? argv[i + 3] : nullptr;
            checkFwsc (argv[i + 1], app);
            i += app != nullptr ? 3 : 1;
        }
    std::printf ("\n%d comprobaciones, %d fallos\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

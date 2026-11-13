// Pruebas del núcleo pi32v2 del motor FM-1 (Source/Fm1). Sin JUCE.
//
// Uso: KoniFm1Tests [--listing <objdump.txt> ...]
//   sin argumentos: las pruebas de ejecución, portadas de fm1-emu (crate fm1-core, módulo tests; MIT) con sus mismos
//   bytes y resultados esperados; solo las que van por step (la ruta de referencia). Las de micro-ops, bloques, JIT y
//   costes de ciclo no aplican aquí.
//   --listing: además decodifica cada instrucción de un listado del objdump de JieLi (formato "  27c:\t40 f0 10 27 \t...")
//   y compara la longitud con la del listado (lo mismo que tools/fm1/check_isa.py, ahora con el decodificador C++).
#include "../Source/Fm1/Fm1Cpu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
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
    ram.put (0x84, { (uint8_t) (call ? 0xbf : 0xff), 0xea, (uint8_t) off, (uint8_t) (off >> 8) });
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
    CHECK (cpu.regs[0] == (uint32_t) want && cpu.regs[1] == (uint32_t) (want >> 32), "r1_r0 += r3 * r7");
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
    std::printf ("\n%d comprobaciones, %d fallos\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

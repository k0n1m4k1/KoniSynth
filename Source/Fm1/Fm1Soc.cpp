// Port a C++ de fm1-emu, crates/fm1-soc/src/lib.rs y machine.rs (MIT), placa FM-1, ruta de referencia. Ver Fm1Soc.h.
#include "Fm1Soc.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fm1
{
namespace
{
constexpr uint32_t kSfrLo = 0x10000, kSfrHi = 0x14000;
constexpr uint32_t kNullPage = 0x1000;     // por debajo nada responde: el acceso es inválido para el bus
constexpr uint32_t kSfrLoEnd = 0x60000;    // las palabras SFR alineadas por debajo van en un vector
constexpr uint32_t kPll0Con0 = 0x119a0, kPll0Con1 = 0x119a4;
constexpr uint32_t kPll0Boot0 = 0x45400203, kPll0Boot1 = 0x3f503026;  // PLL0 como lo deja el cargador (felucca-bsp)
} // namespace

Soc::Soc (size_t ramSize, std::vector<uint8_t> x)
    : xip (std::move (x)), ram (ramSize), ovl (512 * 1024), sram2 (256 * 1024), sfrLo (kSfrLoEnd / 4, 0)
{
    unknownLog.reserve (16384);
    // PLL0 como lo deja el cargador: el driver de audio de la BSP lo comprueba antes de arrancar ALNK0
    sfrSet (kPll0Con0, kPll0Boot0);
    sfrSet (kPll0Con1, kPll0Boot1);
}

uint32_t Soc::sfrGet (uint32_t addr) const
{
    if (addr < kSfrLoEnd && (addr & 3) == 0)
        return sfrLo[addr >> 2];
    const auto it = sfr.find (addr);
    return it == sfr.end() ? 0 : it->second;
}

void Soc::sfrSet (uint32_t addr, uint32_t v)
{
    if (addr < kSfrLoEnd && (addr & 3) == 0)
        sfrLo[addr >> 2] = v;
    else
        sfr[addr] = v;
}

Soc::Region Soc::region (uint32_t addr) const noexcept
{
    if (addr >= kSfrLo && addr < kSfrHi)
    {
        // tabla de periféricos (celdas de 8 bytes en fm1-emu; aquí por rangos, el mismo resultado)
        if (addr >= 0x12e00 && addr <= 0x12e3f) return Region::Alnk;
        if (addr >= 0x10000 && addr <= 0x1001f) return Region::Clk;
        if (addr >= 0x10400 && addr <= 0x1091f) return Region::Timer;
        if (addr >= 0x11c00 && addr <= 0x11c1f) return Region::Spi0;
        if (addr >= 0x11d00 && addr <= 0x11d1f) return Region::Spi1;
        if (addr >= 0x11e00 && addr <= 0x11e1f) return Region::Spi2;
        if (addr >= 0x11800 && addr <= 0x1183f) return Region::Usb;
        if (addr >= 0x12100 && addr <= 0x1212f) return Region::Uart1;
        if (addr >= 0x13100 && addr <= 0x1311f) return Region::Adc;
        if (addr >= 0x13500 && addr <= 0x1350f) return Region::Crc;
        if (addr >= 0x13b00 && addr <= 0x13b07) return Region::Rand;
        if (addr >= 0x11900 && addr <= 0x119ff) return Region::Ana;
        if (addr >= 0x13e08 && addr <= 0x13e0f) return Region::P33;
        return Region::Unknown;
    }
    if (addr < 0x01000000)
    {
        if (addr >= 0x50000 && addr <= 0x501ff) return Region::Gpio;
        if (addr >= 0x51000 && addr <= 0x5103f) return Region::Iomap;
        if (addr >= 0x40300 && addr <= 0x4030f) return Region::Sfcenc;
        if (addr >= 0x20000 && addr <= 0x2ffff) return Region::Bt;
        return Region::Unknown;  // (EQ y SRC por hardware: aún no portados)
    }
    if (addr >= kXipBase && addr - kXipBase < xip.size()) return Region::Xip;
    // más allá de la imagen cargada la ventana XIP sigue mostrando el chip de flash
    if (xipFlash && addr >= kXipBase && (size_t) (addr - kXipBase) + kXipFlashOffset < SpiFlash::kSize) return Region::XipFlash;
    if (addr >= kRamBase && addr - kRamBase < ram.size()) return Region::Ram;
    if (addr >= kOvlBase && addr - kOvlBase < ovl.size()) return Region::Ovl;
    if (addr >= kSram2Base && addr - kSram2Base < sram2.size()) return Region::Sram2;
    if (addr >= 0x1eee000 && addr <= 0x1eee0ff) return Region::Irqc;
    if (addr >= 0x1eef000 && addr <= 0x1eef3ff) return Region::Irqc2;
    if (addr >= 0x1eee240 && addr <= 0x1eee24f) return Region::Dbg;
    if (addr >= 0x1eee250 && addr <= 0x1eee3ff) return Region::Guard;
    return Region::Unknown;  // (la unidad FFT por hardware: aún no portada)
}

// ---- XIP y descifrado del SFC ---------------------------------------------------------------------------------------------
// Tras un borrado o programa, la ventana XIP muestra el chip de flash, también donde su contenido viene del paquete
void Soc::syncXip()
{
    if (! flash.changed)
        return;
    const auto [a, b] = *flash.changed;
    flash.changed.reset();
    if (! xipFlash)
        return;
    if (sfcKey)
    {
        // bloques enteros, como los descifra el SFC
        for (size_t blk = std::max (a, kXipFlashOffset) & ~(size_t) 31; blk < b && blk - kXipFlashOffset < xip.size(); blk += 32)
        {
            const auto i = (blk - kXipFlashOffset) / 32;
            if (sfcFromFlash.size() <= i) sfcFromFlash.resize (i + 1, false);
            sfcFromFlash[i] = true;
            xipBlock (blk);
        }
        return;
    }
    for (size_t o = std::max (a, kXipFlashOffset); o < b && o - kXipFlashOffset < xip.size(); ++o)
        xip[o - kXipFlashOffset] = flash.mem[o];
}

// El offset de flash o se lee a través del descifrado del SFC: CON bit 0, fuera de la ventana en claro si el bit 1 la pone
bool Soc::sfcScrambled (size_t o) const noexcept
{
    const auto x = kXipBase + (uint32_t) (o - kXipFlashOffset);
    return (sfcCon & 1) != 0 && ! ((sfcCon & 2) != 0 && x >= sfcUnenc0 && x <= sfcUnenc1);
}

void Soc::xipBlock (size_t blk)
{
    const auto i = blk - kXipFlashOffset;
    const auto n = std::min ({ (size_t) 32, xip.size() - i, flash.mem.size() - blk });
    uint8_t b[32];
    std::memcpy (b, &flash.mem[blk], n);
    if (sfcKey && sfcScrambled (blk))
        fwscUnmask (b, n, (uint16_t) (*sfcKey ^ (uint16_t) (i >> 2)));
    std::memcpy (&xip[i], b, n);
}

void Soc::sfcReread()
{
    if (! sfcKey)
        return;
    for (size_t i = 0; i < sfcFromFlash.size(); ++i)
        if (sfcFromFlash[i])
            xipBlock (kXipFlashOffset + 32 * i);
}

// El arranque de un paquete: la clave del chip y SFCENC como lo deja el cargador (descifrado hasta appEnd)
void Soc::sfcBoot (uint16_t key, size_t appEnd)
{
    sfcKey = key;
    write32 (0x4030c, kXipBase + (uint32_t) (appEnd - kXipFlashOffset));
    write32 (0x40308, 0x07ffffff);
    write32 (0x40300, 3);
}

void Soc::restoreFlash (const std::vector<uint8_t>& image)
{
    const auto n = std::min (image.size(), SpiFlash::kSize);
    for (size_t at = 0; at < n; at += 4096)
    {
        const auto end = std::min (at + 4096, n);
        if (std::memcmp (&flash.mem[at], &image[at], end - at) != 0)
        {
            std::memcpy (&flash.mem[at], &image[at], end - at);
            flash.changed = std::make_pair (at, end);
            syncXip();
            ++flash.revision;
        }
    }
}

// ---- arranque y reset ---------------------------------------------------------------------------------------------------
void Soc::takeBoot (const Cpu& cpu)
{
    if (boot || cpu.insnCount != 0)
        return;
    auto b = std::make_unique<Boot>();
    if (! cpu.plainState (b->cpu))
        return;
    constexpr uint32_t kInfo = 24 + 32;  // el bloque de writeBootInfo
    const auto r0 = cpu.regs[0];
    if (r0 >= kRamBase && (size_t) (r0 - kRamBase) + kInfo <= ram.size())
    {
        b->hasInfo = true;
        b->infoAt = r0;
        for (uint32_t i = 0; i < kInfo; ++i)
            b->info.push_back (ram.read8 (r0 - kRamBase + i));
    }
    b->sfrLo = sfrLo;
    b->sfr = sfr;
    b->sfcCon = sfcCon;
    b->sfcUnenc0 = sfcUnenc0;
    b->sfcUnenc1 = sfcUnenc1;
    b->btWindow = btWindow;
    boot = std::move (b);
}

// El reset pendiente: cpu0 donde la arrancó el cargador, los registros del chip como los dejó, el núcleo 1 retenido, los
// parámetros de arranque escritos de nuevo, la causa en RST_SRC. Lo que no es del chip se queda: RAM, flash, pantalla
void Soc::chipReset (Cpu& cpu)
{
    if (! resetReq || ! boot)
    {
        resetReq.reset();
        return;
    }
    const auto cause = *resetReq;
    resetReq.reset();
    ++resets;
    periph.chipReset();
    p33.chipReset (cause);
    paused[0] = paused[1] = false;
    lockOwner.reset();
    cpu1Release = cpu1ReleasedOnce = false;
    crc = 0;
    cpu1Idle = false;
    cpu1Fault.reset();
    cpu1Running = false;
    waits[0].reset();
    waits[1].reset();
    flash.chipReset();
    cpu.setPlainState (boot->cpu);
    dbgCause = emuCause = 0;
    cpu.hasPcWin = false;
    cpu.pcOut = false;
    pcGuardDirty = true;
    sfrLo = boot->sfrLo;
    sfr = boot->sfr;
    sfcCon = boot->sfcCon;
    sfcUnenc0 = boot->sfcUnenc0;
    sfcUnenc1 = boot->sfcUnenc1;
    btWindow = boot->btWindow;
    if (boot->hasInfo)
        for (size_t i = 0; i < boot->info.size(); ++i)
            write8 (boot->infoAt + (uint32_t) i, boot->info[i]);
    sfcReread();
}

// Un acceso a P33 terminado: lo que pide al chip. El perro guardián cuenta desde que se enciende o se alimenta
void Soc::p33Event()
{
    const auto e = p33.takeEvent();
    if (! e)
        return;
    if (e->kind == P33::Event::Reset)
    {
        if (! resetReq) resetReq = P33::kRstSoft;
        periph.nextDue = 0;
        return;
    }
    const auto con = p33.reg (P33::kWdtCon, false);
    auto& pd = periph;
    const auto before = pd.wdtAt;
    if ((con & 0x10) == 0)
        pd.wdtAt = kNever;
    else if (e->fed || pd.wdtAt == kNever)
        pd.wdtAt = pd.cycles + std::max<uint64_t> (pd.cycleOfMs (P33::wdtMs (con)), 1);
    if (pd.wdtAt < before)
        pd.nextDue = 0;
    else
        pd.nextDue = std::min (pd.nextDue, pd.wdtAt);
}

std::optional<uint8_t> Soc::ramByte (uint32_t a) const noexcept
{
    if (a >= kRamBase && a - kRamBase < ram.size()) return ram.read8 (a - kRamBase);
    if (a >= kOvlBase && a - kOvlBase < ovl.size()) return ovl.read8 (a - kOvlBase);
    if (a >= kSram2Base && a - kSram2Base < sram2.size()) return sram2.read8 (a - kSram2Base);
    return std::nullopt;
}

bool Soc::stillWaits (uint32_t core, const Wait& w) const noexcept
{
    if (w.kind == Wait::Lock)
        return lockOwner && *lockOwner != core;
    const auto b = ramByte (w.addr);
    return ! (b && *b == 0);
}

void Soc::guardTrip (uint32_t bit)
{
    dbgCause |= 1u << bit;
    raiseException();
}

// La excepción de CPU (IRQ 1): pendiente hasta que cpu0 la tome, si el firmware la tiene habilitada; si no, se descarta
void Soc::raiseException()
{
    if (periph.irqEnabled (0, 1))
    {
        periph.excPending = true;
        periph.nextDue = 0;
    }
}

void Soc::busInvalid (uint32_t bit)
{
    if ((sfrGet (0x1eee340) & (1u << bit)) != 0)
        guardTrip (bit);
}

// Los límites de PC (0x1EEE380 / 84: ventana 0 alto / bajo, 88 / 8C: ventana 1)
bool Soc::pcWindows (uint32_t out[2][2])
{
    out[0][0] = sfrGet (0x1eee384);
    out[0][1] = sfrGet (0x1eee380);
    out[1][0] = sfrGet (0x1eee38c);
    out[1][1] = sfrGet (0x1eee388);
    return out[0][1] != 0 || out[1][1] != 0;
}

void Soc::logUnknown (uint32_t addr, uint8_t width, bool isWrite, uint32_t value)
{
    // se guardan los últimos 8192..16384
    if (unknownLog.size() >= 16384)
        unknownLog.erase (unknownLog.begin(), unknownLog.begin() + 8192);
    unknownLog.push_back ({ addr, width, isWrite, value });
}

// Avanza el reloj de periféricos n ciclos de CPU y saca de la RAM la mitad de audio acabada (int32 L/R con una muestra de
// 24 bits en los bits bajos: Felucca escribe Q15 << 7, así que >> 8 da 16 bits)
void Soc::advance (uint64_t n)
{
    const auto due = periph.advance (n);
    if (due.wdt && ! resetReq)
        resetReq = P33::kRstWdt;
    if (due.uartRx)
        write8 (due.uartRx->first, due.uartRx->second);
    if (due.audioHalf)
    {
        const auto [adr, words] = *due.audioHalf;
        for (uint32_t i = 0; i < words; ++i)
        {
            const auto w = (int32_t) read32 (adr + i * 4);
            periph.audio.push_back ((int16_t) std::clamp (w >> 8, -32768, 32767));
        }
    }
}

// ---- Bus --------------------------------------------------------------------------------------------------------------------
void Soc::idle (uint32_t core)
{
    if (core == 0) periph.idle();
    else cpu1Idle = true;
}

void Soc::spin (uint32_t core, uint32_t addr)
{
    if (cpu1Running)
        waits[core & 1] = Wait { Wait::Byte, addr };
}

bool Soc::hwLock (uint32_t core, bool set)
{
    if (set)
    {
        if (lockOwner && *lockOwner != core)
        {
            if (cpu1Running)
                waits[core & 1] = Wait { Wait::Lock, 0 };
            return false;
        }
        lockOwner = core;
        return true;
    }
    if (lockOwner && *lockOwner == core)
        lockOwner.reset();
    return true;
}

uint8_t Soc::read8 (uint32_t addr)
{
    const auto o = addr - kRamBase;
    if (o < ram.size())
        return ram.read8 (o);
    return (uint8_t) (read32 (addr & ~3u) >> (8 * (addr & 3)));
}

// las medias palabras están en los offsets 0 y 2 de la palabra
uint16_t Soc::read16 (uint32_t addr) { return (uint16_t) (read32 (addr & ~3u) >> ((addr & 2) * 8)); }

uint32_t Soc::read32 (uint32_t addr)
{
    const auto o = (size_t) (addr - kRamBase);
    if (o + 4 <= ram.size())
        return ram.read32 ((uint32_t) o);
    const auto x = (size_t) (addr - kXipBase);
    if (x + 4 <= xip.size())
    {
        uint32_t v;
        std::memcpy (&v, &xip[x], 4);
        return v;
    }
    return read32Slow (addr);
}

uint32_t Soc::fetch32 (uint32_t addr)
{
    const auto o = (size_t) (addr - kRamBase);
    if (o + 4 <= ram.size())
        return ram.read32 ((uint32_t) o);
    return read32 (addr);
}

void Soc::write8 (uint32_t addr, uint8_t v)
{
    const auto o = addr - kRamBase;
    if (o < ram.size())
        return ram.write8 (o, v);
    const auto cur = read32 (addr & ~3u);
    const auto sh = (addr & 3) * 8;
    write32 (addr & ~3u, (cur & ~(0xffu << sh)) | ((uint32_t) v << sh));
}

void Soc::write16 (uint32_t addr, uint16_t v)
{
    const auto o = (size_t) ((addr & ~1u) - kRamBase);
    if (o + 2 <= ram.size() && (addr & 1) == 0)
        return ram.write16 ((uint32_t) o, v);
    const auto cur = read32 (addr & ~3u);
    const auto sh = (addr & 2) * 8;
    write32 (addr & ~3u, (cur & ~(0xffffu << sh)) | ((uint32_t) v << sh));
}

void Soc::write32 (uint32_t addr, uint32_t v)
{
    const auto o = (size_t) (addr - kRamBase);
    if (o + 4 <= ram.size())
        return ram.write32 ((uint32_t) o, v);
    write32Slow (addr, v);
}

uint32_t Soc::timerRead (uint32_t addr)
{
    // una base de tiempo para que los bucles de espera avancen: los contadores devuelven un valor que se mueve
    ++ticks;
    if ((addr & 0xff) == 0x04)
        return (uint32_t) (ticks * 7);
    return sfrGet (addr);
}

uint32_t Soc::read32Slow (uint32_t addr)
{
    switch (region (addr))
    {
        case Region::Xip:
        {
            const auto o = (size_t) (addr - kXipBase);
            uint32_t v = 0;
            for (size_t i = 0; i < 4; ++i)
                v |= (uint32_t) (o + i < xip.size() ? xip[o + i] : 0) << (8 * i);
            return v;
        }
        case Region::Ram: return ram.read32 (addr - kRamBase);
        case Region::Sfcenc: return sfrGet (addr);
        case Region::XipFlash:
        {
            const auto o = (size_t) (addr - kXipBase) + kXipFlashOffset;
            uint32_t v = 0;
            for (size_t i = 0; i < 4; ++i)
                v |= (uint32_t) (o + i < flash.mem.size() ? flash.mem[o + i] : 0xff) << (8 * i);
            return v;
        }
        case Region::Ovl: return ovl.read32 (addr - kOvlBase);
        case Region::Sram2: return sram2.read32 (addr - kSram2Base);
        case Region::Irqc:
        {
            // 0x1eee008 bit 14: controlador listo (la inicialización temprana lo espera); C0_CON / C1_CON: bit 4 pausado
            const auto v = sfrGet (addr);
            if (addr == 0x1eee008) return v | 0x4000;
            if (addr == 0x1eee000 || addr == 0x1eee004)
                return (v & ~0x14u) | (paused[(addr >> 2) & 1] ? 0x10u : 0u);
            return v;
        }
        case Region::Iomap: case Region::Usb: case Region::Clk: return sfrGet (addr);
        case Region::Ana:
        {
            // JL_ANA: 0x11978 es la medida del ajuste del DAC: hecho (bit 5), valor a media escala, "este paso es el
            // bueno" (bit 18); así la calibración de audio de fábrica acaba al instante
            const auto v = sfrGet (addr);
            return addr == 0x11978 ? ((v & ~0xff00u) | 0x40000u | 0x8000u | 0x20u) : v;
        }
        // la banda base Bluetooth (sin radio): lee 0, salvo 0x2FC44, la base de su ventana en RAM
        case Region::Bt: return addr == 0x2fc44 ? btWindow : 0;
        case Region::Gpio: case Region::Adc: case Region::Alnk: case Region::Irqc2: case Region::Uart1:
        {
            if (addr == 0x1eef0d4) return emuCause;  // EMU_MSG: solo sus causas
            const auto v = periph.read (addr);
            return v ? *v : sfrGet (addr);
        }
        case Region::Timer:
        {
            const auto v = periph.read (addr);
            return v ? *v : timerRead (addr);
        }
        case Region::Spi0: return flash.read (addr);
        case Region::Spi2: return sfrGet (addr) | (addr == 0x11e00 ? 0x8000u : 0u);  // acaba al instante
        case Region::Spi1:
            if (addr == 0x11d00)
            {
                const bool busy = lcdTimed && periph.cycles < periph.spi1DoneAt;
                return sfrGet (addr) | (busy ? 0u : 0x8000u);
            }
            return sfrGet (addr);
        case Region::Dbg: return addr == 0x1eee244 ? dbgCause : sfrGet (addr);
        case Region::Guard: return sfrGet (addr);
        case Region::Crc: return addr == 0x13504 ? crc : 0u;
        case Region::Rand:
        {
            // JL_RAND: un número nuevo en cada lectura, de una semilla fija para que las ejecuciones se repitan
            auto x = rand;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            rand = x;
            return (uint32_t) (addr == 0x13b00 ? x : x >> 32);
        }
        case Region::P33: return p33.read (addr);
        case Region::HwDsp: case Region::HwEq: case Region::HwSrc: case Region::Unknown:
        default:
            if (addr < kNullPage)
                busInvalid (4);  // lectura de la página NULL (DBG_MSG bit 4)
            logUnknown (addr, 4, false, 0);
            return 0;
    }
}

void Soc::spi1Write (uint32_t addr, uint32_t v)
{
    switch (addr)
    {
        case 0x11d00:
            // CON: bit14 borra el hecho pendiente (y su IRQ 16), bit13 habilita la IRQ
            if ((v & 0x4000) != 0) periph.spi1Pending = false;
            if ((v & 0x2000) == 0) periph.spi1IrqOff();
            sfrSet (addr, v & ~0xc000u);
            break;
        case 0x11d08:
            // BUF: un byte, D/C = estado de PC8
            lcd.feed ((uint8_t) v, (sfrGet (0x50080) & 0x100) != 0);
            sfrSet (addr, v);
            spi1Done (1);
            break;
        case 0x11d10:
        {
            // CNT: DMA de v bytes desde ADR (una dirección de bus: RAM, SRAM o XIP) al panel
            const auto adr = sfrGet (0x11d0c);
            const auto n = std::min<uint32_t> (v, 0x40000);
            const bool dc = (sfrGet (0x50080) & 0x100) != 0;
            for (uint32_t i = 0; i < n; ++i)
                lcd.feed (read8 (adr + i), dc);
            sfrSet (addr, 0);
            spi1Done (n);
            break;
        }
        default: sfrSet (addr, v); break;
    }
}

// Una transferencia de SPI1 tarda su tiempo en el cable con lcdTimed o con su interrupción habilitada (CON bit 13)
void Soc::spi1Done (uint32_t bytes)
{
    const bool irq = (sfrGet (0x11d00) & 0x2000) != 0;
    if (irq || lcdTimed)
        periph.spi1Start (bytes, sfrGet (0x11d04), irq);
}

// SPI2: el firmware de fábrica mete con él el patrón de columna de la matriz en los dos 74HC595
void Soc::spi2Write (uint32_t addr, uint32_t v)
{
    const auto started = [this] (uint32_t bytes)
    { periph.spi2Start (bytes, sfrGet (0x11e04), (sfrGet (0x11e00) & 0x2000) != 0); };
    switch (addr)
    {
        case 0x11e00:
            if ((v & 0x4000) != 0) periph.spi2Pending = false;
            sfrSet (addr, v & ~0xc000u);
            break;
        case 0x11e08:
            periph.srShiftByte ((uint8_t) v);
            sfrSet (addr, v);
            started (1);
            break;
        case 0x11e10:
        {
            const auto adr = sfrGet (0x11e0c);
            for (uint32_t i = 0; i < std::min<uint32_t> (v, 64); ++i)
                periph.srShiftByte (read8 (adr + i));
            sfrSet (addr, 0);
            started (v);
            break;
        }
        default: sfrSet (addr, v); break;
    }
}

void Soc::write32Slow (uint32_t addr, uint32_t v)
{
    switch (region (addr))
    {
        case Region::Xip: case Region::XipFlash: logUnknown (addr, 4, true, v); break;  // la flash se escribe por SPI0
        case Region::Ram: ram.write32 (addr - kRamBase, v); break;
        case Region::Ovl: ovl.write32 (addr - kOvlBase, v); break;
        case Region::Sram2: sram2.write32 (addr - kSram2Base, v); break;
        case Region::Irqc:
            if (addr == 0x1eee000 || addr == 0x1eee004)
            {
                // C0_CON / C1_CON: bit 2 pausa ese núcleo; bit 3 sin bit 2 lo reanuda. El primer |= 8 del núcleo 1 es su
                // liberación en el SDK de FM1 Open (el firmware de fábrica lo libera en 0x10008)
                const auto core = (addr >> 2) & 1;
                if ((v & 4) != 0)
                    paused[core] = true;
                else if ((v & 8) != 0)
                {
                    paused[core] = false;
                    if (core == 1 && ! cpu1ReleasedOnce)
                    {
                        cpu1ReleasedOnce = true;
                        cpu1Release = true;
                        periph.nextDue = 0;
                    }
                }
                sfrSet (addr, v & ~0x14u);
            }
            else
                sfrSet (addr, v);
            break;
        case Region::Gpio:
            // PD0 es el chip select de la flash; PA1/PA3/PA4 mueven los registros de la matriz
            if (addr == 0x500c0)
            {
                flash.setCs ((v & 1) == 0);
                syncXip();
            }
            periph.write (addr, v);
            sfrSet (addr, v);
            break;
        case Region::Alnk: case Region::Irqc2:
            if (addr == 0x1eef0d4)
            {
                emuCause &= ~v;  // EMU_MSG: escribir borra las causas con 1 y no se guarda
                return;
            }
            if (! periph.write (addr, v)) sfrSet (addr, v);
            break;
        case Region::Spi0: flash.write (addr, v); syncXip(); break;
        case Region::Iomap: case Region::Ana: sfrSet (addr, v); break;
        case Region::P33: p33.write (addr, v); p33Event(); break;
        case Region::Bt: if (addr == 0x2fc44) btWindow = v; break;
        case Region::Clk:
            if (addr == 0x10008 && (v & 8) != 0 && (sfrGet (addr) & 8) == 0 && ! cpu1ReleasedOnce)
            {
                cpu1ReleasedOnce = true;  // el firmware de fábrica libera el núcleo 1 con 0x10008 bit 3
                cpu1Release = true;
                periph.nextDue = 0;
            }
            sfrSet (addr, v);
            break;
        case Region::Usb: sfrSet (addr, v); break;  // (USB aún no portado)
        case Region::Uart1: case Region::Adc: case Region::Timer:
            if (! periph.write (addr, v)) sfrSet (addr, v);
            break;
        case Region::Spi1: spi1Write (addr, v); break;
        case Region::Spi2: spi2Write (addr, v); break;
        case Region::Rand: break;
        case Region::Crc:
            // la unidad CRC-16 (CCITT 0x1021, primero el bit alto): [+0] mete un byte, [+4] el registro
            if (addr == 0x13500)
            {
                auto c = (uint16_t) (crc ^ ((v & 0xff) << 8));
                for (int k = 0; k < 8; ++k)
                    c = (uint16_t) ((c & 0x8000) != 0 ? (c << 1) ^ 0x1021 : c << 1);
                crc = c;
            }
            else if (addr == 0x13504)
                crc = (uint16_t) v;
            break;
        case Region::Dbg:
            if ((addr & ~3u) == 0x1eee244)
                for (int i = 0; i < 4; ++i) console.push_back ((uint8_t) (v >> (8 * i)));  // DBG_MSG: a la consola
            else if ((addr & ~3u) == 0x1eee248)
                dbgCause &= ~v;
            sfrSet (addr, v);
            break;
        case Region::Guard:
            if (addr >= 0x1eee380 && addr <= 0x1eee38f)
            {
                pcGuardDirty = true;
                periph.nextDue = 0;
            }
            sfrSet (addr, v);
            break;
        case Region::Sfcenc:
            if (addr == 0x40300) sfcCon = v & 0xff;
            else if (addr == 0x40308) sfcUnenc1 = v;
            else if (addr == 0x4030c) sfcUnenc0 = v;
            sfrSet (addr, v);
            sfcReread();
            break;
        case Region::HwDsp: case Region::HwEq: case Region::HwSrc: case Region::Unknown:
        default:
            if (addr < kNullPage)
                busInvalid (5);  // escritura en la página NULL (DBG_MSG bit 5)
            logUnknown (addr, 4, true, v);
            break;
    }
}

// ---- paquetes ---------------------------------------------------------------------------------------------------------------
bool stockFromPackage (const std::vector<uint8_t>& pkg, const std::vector<uint8_t>* syx, Stock& out, std::string& error)
{
    FwscUnpacked u;
    if (! fwscUnpack (pkg, u, error))
        return false;
    out.flash = fwscFullFlash (u.raw, SpiFlash::kSize);
    // el banco de patches que los aparatos llevan de fábrica: cualquier paquete con entrada USR lo recibe
    if (const auto* usr = u.entry ("USR"))
    {
        out.usr = usr->offset;
        usrFillDefaults (out.flash, usr->offset, UsrLayout::fm1);
        if (syx != nullptr && usrLoadSyx (out.flash, usr->offset, UsrLayout::fm1, *syx, 0, error) < 0)
            return false;
    }
    const auto* app = u.entry ("app.bin");
    out.entry = kXipBase + (uint32_t) (app != nullptr ? app->offset : 0x120);
    out.xip = std::move (u.xip);
    out.identity = u.identity;
    out.key = u.key;
    out.appEnd = u.end;
    return true;
}

uint32_t stockBoot (Soc& soc, const std::vector<uint8_t>& flash, uint32_t params)
{
    soc.flash.load (flash.data(), flash.size());
    soc.xipFlash = true;
    return writeBootInfo (soc, params, flash);
}

// El bloque de parámetros que el SPL (uboot.boot) pasa a la aplicación en r0 (BOOT_DEVICE_INFO del SDK): fs_info (-> la
// cabecera de flash copiada tras la estructura), sfc_base_addr 0x4000, app_addr 0x02000000, chip_id, trim_value, una MAC
// de 8 bytes y la cabecera de 32 bytes desenmascarada
uint32_t writeBootInfo (Soc& soc, uint32_t at, const std::vector<uint8_t>& flash)
{
    uint8_t head[32];
    std::memcpy (head, flash.data(), 32);
    fwscUnmask (head, 32, 0xffff);
    const uint32_t words[6] = { at + 24, 0x4000, kXipBase, 0, 0, 0 };
    for (uint32_t i = 0; i < 6; ++i)
        soc.write32 (at + 4 * i, words[i]);
    for (uint32_t i = 0; i < 32; ++i)
        soc.write8 (at + 24 + i, head[i]);
    return at;
}

// ---- el ejecutor ------------------------------------------------------------------------------------------------------------
namespace
{
// Lo que ve el núcleo 1 del SoC: sin reloj propio; el ejecutor le da un presupuesto. stop = la tanda acaba tras esta
// instrucción (un reset pedido, un lockclr con cpu0 esperando el cerrojo, idle)
struct Core1Bus : Bus
{
    Soc& soc;
    std::optional<Wait> until;
    bool stop = false;
    Core1Bus (Soc& s, std::optional<Wait> u) : soc (s), until (u) {}
    uint8_t read8 (uint32_t a) override { return soc.read8 (a); }
    uint16_t read16 (uint32_t a) override { return soc.read16 (a); }
    uint32_t read32 (uint32_t a) override { return soc.read32 (a); }
    uint32_t fetch32 (uint32_t a) override { return soc.fetch32 (a); }
    void write8 (uint32_t a, uint8_t v) override { soc.write8 (a, v); stop |= soc.resetDue(); }
    void write16 (uint32_t a, uint16_t v) override { soc.write16 (a, v); stop |= soc.resetDue(); }
    void write32 (uint32_t a, uint32_t v) override { soc.write32 (a, v); stop |= soc.resetDue(); }
    bool hwLock (uint32_t core, bool set) override
    {
        const bool got = soc.hwLock (core, set);
        if (! set && until && until->kind == Wait::Lock)
            stop = true;
        return got;
    }
    void spin (uint32_t core, uint32_t addr) override { soc.spin (core, addr); }
    void idle (uint32_t) override { soc.cpu1Idle = true; stop = true; }
};

std::string faultText (const Cpu& c, const char* who)
{
    char buf[160];
    std::snprintf (buf, sizeof buf, "%s: fallo %d (%s, raw 0x%llx) en pc 0x%08x", who, (int) c.fault,
                   c.faultName != nullptr ? c.faultName : "?", (unsigned long long) c.faultRaw, c.faultPc);
    return buf;
}

constexpr uint32_t kSoloChunk = 4096;
} // namespace

void Machine::cpu0Between()
{
    if (soc.pcGuardDirty)
    {
        soc.pcGuardDirty = false;
        cpu0.hasPcWin = soc.pcWindows (cpu0.pcWin);
    }
    soc.periph.inIsr = ! cpu0.irqLevels.empty();
    if (! soc.periph.anyPending())
        return;
    if (const auto p = soc.periph.pendingIrqFor (0); p && cpu0.irqReady (p->second))
    {
        const auto handler = soc.read32 (kVecBase + 4u * p->first);
        cpu0.interrupt (handler, p->second);
        soc.periph.inIsr = true;
        if (p->first == 1)
            soc.periph.excPending = false;  // la excepción: se toma una vez
    }
}

// El bucle de cpu0 (de referencia: una instrucción cada vez, el SoC atendido cuando vence un tick). Devuelve en done las
// instrucciones ejecutadas: n, o menos si cpu0 liberó a cpu1, el chip se reseteó o encontró un cerrojo de cpu1
bool Machine::runBlocks (uint32_t n, uint32_t& done, std::string& error)
{
    soc.takeBoot (cpu0);
    done = 0;
    if (soc.resetDue())
    {
        soc.chipReset (cpu0);
        return true;
    }
    bool ticked = pendingTick;
    pendingTick = false;
    while (done < n)
    {
        if (ticked || soc.periph.tick())
            soc.advance (0);
        ticked = false;
        if (soc.resetDue())
        {
            soc.chipReset (cpu0);
            break;
        }
        cpu0Between();
        const bool trip = cpu0.pcEnter (cpu0.pc);
        if (! cpu0.step (soc))
        {
            error = faultText (cpu0, "cpu0");
            return false;
        }
        ++done;
        if (trip)
            soc.guardTrip (12);  // la primera instrucción fuera de los límites corre, luego la excepción
        if (soc.cpu1Release || soc.waits[0])
            break;
    }
    return true;
}

// cpu1 cuando cpu0 la libera: su entrada es la palabra de 0x01C7FFF8 (imágenes antiguas sin ella: el bucle de trabajos en
// RAM de 0x01C022B6). Empieza en modo supervisor
std::unique_ptr<Cpu> Machine::releasedCpu1()
{
    if (soc.cpu1Fault)
        return nullptr;
    const bool latched = soc.cpu1Release;
    soc.cpu1Release = false;
    if (! latched && (soc.read32 (0x10008) & 8) == 0)
        return nullptr;
    soc.cpu1Idle = false;
    const auto entry = soc.read32 (0x01c7fff8);
    const bool inCode = (entry >= 0x02000000 && entry < 0x03000000) || (entry >= kRamBase && entry < kRamBase + soc.ram.size());
    auto c1 = std::make_unique<Cpu> (inCode ? entry : kRamBase + 0x22b6);
    c1->coreId = 1;
    c1->usp = kRamBase + 0x15eb4;
    c1->sp = kRamBase + 0x16eb4;
    return c1;
}

// cpu1 durante hasta `cycles` instrucciones: interrupciones de su banco y pasos; de vuelta donde empezó sin cambios es que
// está ociosa. Para también si encuentra un cerrojo de cpu0 o, con until, cuando cpu0 ya lo obtendría.
// (fm1-emu comprueba la vuelta al inicio al final de cada bloque; aquí, tras cada instrucción)
bool Machine::runCpu1 (uint64_t cycles, std::optional<Wait> until, uint64_t& ran, std::string& error)
{
    ran = 0;
    auto& c1 = *cpu1;
    if (soc.corePaused (1) || soc.resetDue() || soc.cpu1Fault)
        return true;
    if (soc.cpu1Idle)
    {
        // dormida desde su idle: solo despierta con una interrupción de su banco pendiente
        if (! (soc.periph.anyPending() && soc.periph.pendingIrqFor (1)))
            return true;
        soc.cpu1Idle = false;
    }
    const auto probePc = c1.pc, probeSp = c1.sp;
    uint32_t probeRegs[16];
    std::memcpy (probeRegs, c1.regs, sizeof probeRegs);
    const uint64_t budget = std::min<uint64_t> (cycles, 1u << 20);
    uint64_t left = budget;
    while (left > 0)
    {
        if (soc.periph.anyPending())
            if (const auto p = soc.periph.pendingIrqFor (1); p && c1.irqReady (p->second))
                c1.interrupt (soc.read32 (kVecBase + 4u * p->first), p->second);
        Core1Bus bus (soc, until);
        if (! c1.step (bus))
        {
            error = faultText (c1, "cpu1");
            return false;
        }
        --left;
        if (soc.waits[1])
        {
            soc.waits[1].reset();
            break;  // espera un cerrojo de cpu0: toca el tramo de cpu0
        }
        if (soc.resetDue() || soc.cpu1Idle || bus.stop)
            break;
        if (c1.pc == probePc && c1.sp == probeSp && std::memcmp (c1.regs, probeRegs, sizeof probeRegs) == 0)
            break;  // sondeando sin nada que hacer
        if (until && ! soc.stillWaits (0, *until))
            break;  // cpu0 ya obtendría lo que espera
    }
    ran = budget - left;
    return true;
}

bool Machine::runCpu1On (uint64_t cycles, std::optional<Wait> until, uint64_t& ran, std::string& error)
{
    if (runCpu1 (cycles, until, ran, error))
        return true;
    if (! soc.cpu1Survives)
        return false;
    // el núcleo 1 se para solo y cpu0 sigue; se suelta el cerrojo hardware que tuviera
    soc.cpu1Fault = error;
    error.clear();
    if (soc.lockOwner && *soc.lockOwner == 1)
        soc.lockOwner.reset();
    soc.waits[0].reset();
    soc.waits[1].reset();
    ran = 0;
    return true;
}

bool Machine::runDualFrom (uint32_t& done, uint32_t n, std::string& error)
{
    const auto resets = soc.resets;
    soc.cpu1Running = cpu1 != nullptr;
    while (! cpu1)
    {
        cpu1 = releasedCpu1();
        if (cpu1 || done >= n)
            break;
        const auto k = n - done;
        soc.periph.roomCap = k;
        uint32_t ran = 0;
        if (! runBlocks (k, ran, error))
            return false;
        if (soc.resets != resets)
        {
            done += ran;
            return true;
        }
        // cpu0 sola: lo que cede al liberar a cpu1 llega hasta el siguiente múltiplo de kSoloChunk (como fm1-emu)
        done = ran < k ? std::min (n, (done + ran + kSoloChunk - 1) / kSoloChunk * kSoloChunk) : n;
    }
    if (! cpu1)
        return true;
    soc.periph.roomCap = kSlice;
    soc.cpu1Running = true;
    while (done < n)
    {
        // un tramo de cpu0, luego cpu1 durante los ciclos que tomó
        auto k = std::min (n - done, kSlice);
        while (k > 0)
        {
            const auto c0 = soc.periph.cycles;
            uint32_t ran = 0;
            if (soc.corePaused (0) && ! soc.resetDue())
            {
                soc.advance (k);  // retenida por cpu1 (C0_CON bit 2): el tiempo sigue
                if (soc.resetDue())
                {
                    uint32_t r0 = 0;
                    if (! runBlocks (0, r0, error)) return false;
                }
                ran = k;
            }
            else if (! runBlocks (k, ran, error))
                return false;
            if (soc.resets != resets)
            {
                done += ran;
                return true;
            }
            done += ran;
            k -= ran;
            const auto dt = soc.periph.cycles - c0;
            uint64_t r1 = 0;
            if (! runCpu1On (dt, std::nullopt, r1, error))
                return false;
            const auto wait = soc.waits[0];
            soc.waits[0].reset();
            if (k == 0)
                break;
            if (! wait)
            {
                done += k;  // corta sin cerrojo que esperar (una liberación retenida): el resto del tramo se pierde
                break;
            }
            uint64_t spun = 0;
            if (soc.stillWaits (0, *wait) && ! runCpu1On (k, wait, spun, error))
                return false;
            if (soc.stillWaits (0, *wait))
                spun = k;  // cpu1 paró con el cerrojo aún suyo: el resto del tramo también giraría
            if (spun > 0)
            {
                soc.advance (spun);
                done += (uint32_t) spun;
                k -= (uint32_t) spun;
                if (soc.resetDue())
                {
                    uint32_t r0 = 0;
                    if (! runBlocks (0, r0, error)) return false;
                    if (soc.resets != resets)
                        return true;
                }
            }
        }
    }
    return true;
}

bool Machine::run (uint32_t n, std::string& error)
{
    uint32_t done = 0;
    // un reset del chip retiene de nuevo al núcleo 1: cpu0 sola hasta que lo libere otra vez
    while (done < n)
    {
        const auto resets = soc.resets;
        if (! runDualFrom (done, n, error))
            return false;
        if (soc.resets == resets)
            break;
        cpu1.reset();
    }
    return true;
}
} // namespace fm1

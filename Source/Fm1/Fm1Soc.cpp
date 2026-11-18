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

bool Soc::pureRead (uint32_t addr) const noexcept
{
    switch (region (addr))
    {
        case Region::Xip: case Region::XipFlash: case Region::Ram: case Region::Ovl: case Region::Sram2: return true;
        case Region::Usb: return true;  // (USB sin portar: almacenamiento simple)
        case Region::Irqc: case Region::Iomap: case Region::Clk: case Region::Ana: case Region::Bt: case Region::Dbg: case Region::Guard:
            return true;
        case Region::Spi2: case Region::Crc: case Region::P33: case Region::Alnk: case Region::Uart1: case Region::Sfcenc: return true;
        case Region::Irqc2: return addr != 0x1eef0f0;  // la palabra del contador del tick cuenta el reloj
        case Region::Timer:
            // las palabras de control y periodo; los contadores cuentan el reloj
            return addr == 0x10800 || addr == 0x10900 || addr == 0x10908 || (addr & ~0x300u) == 0x10400 || (addr & ~0x300u) == 0x10408;
        case Region::Gpio: case Region::Adc: return true;
        case Region::Spi1: return ! lcdTimed;
        default: return false;
    }
}

bool Soc::loopWriteOk (uint32_t addr) const noexcept
{
    const auto r = region (addr);
    return r == Region::P33 || (r == Region::Adc && (addr == 0x13100 || addr == 0x13104));
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
    ++advances;
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
    ++sideEffects;
    ++stopEffects;
    if (ioHist != nullptr) ++(*ioHist)[0xfffffff0u];
    obsImpure += observing ? 1 : 0;
    if (core == 0) periph.idle();
    else cpu1Idle = true;
}

void Soc::spin (uint32_t core, uint32_t addr)
{
    ++sideEffects;
    ++stopEffects;
    if (ioHist != nullptr) ++(*ioHist)[0xfffffff4u];
    obsImpure += observing ? 1 : 0;
    if (cpu1Running)
        waits[core & 1] = Wait { Wait::Byte, addr };
}

bool Soc::hwLock (uint32_t core, bool set)
{
    ++sideEffects;
    ++stopEffects;
    if (ioHist != nullptr) ++(*ioHist)[0xfffffff8u];
    obsImpure += observing ? 1 : 0;
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
    ++sideEffects;
    // Sin efectos para la ráfaga: el P33 lee su estado, el ADC su CON / RES y los GPIO sus pines (nada cambia), y el
    // contador del TIMER4 (0x10804) si no salta tiempo (skipIdle: adelanta cycles y pide reevaluar con nextDue = 0). El
    // resto, siempre. Melodee con 12 notas lo lee 3 millones de veces por segundo: el 45 % de los accesos con efectos.
    // También, medido con 12 notas en 4 s: ANA (sfrGet; FoMni 969 000, HYP-1 723 000) y USB (almacenamiento simple; ~480 000
    // en todos), el contador del TIMER5 (0x10904, calculado de cycles; V15 2,4 M) y SPI0, la flash (sin interrupciones;
    // Melodee 4,1 M), con nextDue y cycles intactos
    const auto r0 = region (addr);
    // (--ioprofile cuenta los que paran)
    struct StopAfterRead
    {
        Soc& soc;
        bool always;
        uint64_t due0, cyc0;
        uint32_t key;
        ~StopAfterRead()
        {
            if (always || soc.periph.nextDue != due0 || soc.periph.cycles != cyc0)
            {
                ++soc.stopEffects;
                if (soc.ioHist != nullptr) ++(*soc.ioHist)[key];
            }
        }
    } stopAfter { *this, ! (r0 == Region::P33 || r0 == Region::Gpio || r0 == Region::Ana || r0 == Region::Usb || r0 == Region::Spi0
                            || addr == 0x13100 || addr == 0x13104 || addr == 0x10804 || addr == 0x10904),
                  periph.nextDue, periph.cycles, addr & ~3u };
    if (observing) [[unlikely]]
    {
        if (addr == 0x10804) ++obsT4;
        else if (! pureRead (addr)) ++obsImpure;
    }
    switch (r0)
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
    ++sideEffects;
    const auto rg = region (addr);  // (una vez: no se queda en línea, y eran tres llamadas)
    // ADC, GPIO (salvo PD0), P33, SPI0 (la flash, sin interrupciones; Melodee con 12 notas: 3,4 M en 4 s) y el contador del
    // TIMER5 (escribirlo no hace nada; V15: 1,2 M): sin efectos para la ráfaga si el periférico no pidió reevaluar (nextDue intacto; una
    // IRQ del ADC lo pide, y en el P33 un reset o un perro guardián que vence antes: p33Event). El resto, siempre. Se decide
    // al salir (las ramas de abajo vuelven por varios sitios). Medido con 12 notas: HYP-1 y DUB-1 alimentan el perro
    // guardián 117 000-181 000 veces por segundo, ~24 accesos al P33 cada vez
    struct StopAfter
    {
        Soc& soc;
        bool quiet;
        uint64_t due0;
        uint32_t key;
        ~StopAfter()
        {
            if (! quiet || soc.periph.nextDue != due0)
            {
                ++soc.stopEffects;
                if (soc.ioHist != nullptr) ++(*soc.ioHist)[key];  // (--ioprofile cuenta los que paran)
            }
        }
    } stopAfter { *this, (rg == Region::Gpio && addr != 0x500c0) || rg == Region::P33 || rg == Region::Spi0 || addr == 0x13100
                             || addr == 0x13104 || addr == 0x10904,
                  periph.nextDue, (addr & ~3u) | 1u };
    if (observing && rg != Region::Ram) [[unlikely]]
        ++obsImpure;
    switch (rg)
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
    bool ramView (RamView& v) override { return soc.ramView (v); }
    bool xipView (XipView& v) override { return soc.xipView (v); }
    void markCode (uint32_t lo, uint32_t hi) override { soc.markCode (lo, hi); }
    uint64_t codeWrites() override { return soc.codeWrites(); }
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

// ---- salto de bucles de sondeo (probe_loop de fm1-emu) ---------------------------------------------------------------------
namespace
{
constexpr uint32_t kProbeMin = 1024;  // solo con al menos estas instrucciones y ciclos por delante
constexpr uint32_t kProbeKnown = 64;  // lo mismo en un pc con bucle conocido (un sondeo de un bucle corto cuesta ~10-20)
constexpr int64_t kCreditMax = 1 << 16;
constexpr size_t kVisits = 8;         // visitas al pc inicial que se guardan para comparar
constexpr uint32_t kT4Cnt = 0x10804;  // el contador del TIMER4 (24 MHz, libre)
constexpr uint32_t kDeadStack = 0x4000;  // pila muerta: palabras hasta 16 KiB por debajo de sp
constexpr uint32_t kMinTimedRound = 64;
constexpr uint32_t kMinObserved = 256;  // vueltas que se observan en lugar de sondearse: las largas  // vueltas con contador: al menos esto (Periph: dos lecturas a < 64 ciclos son racha)

// cpu0 tal como la ve un sondeo: un acceso que una vuelta saltada no podría repetir no se hace (una carga lee 0) y marca el
// sondeo como malo; las escrituras a RAM se apuntan con lo que había
struct ProbeBus : Bus
{
    Soc& soc;
    bool bad = false;
    uint32_t badAddr = 0;  // el acceso que lo hizo malo (diagnóstico)
    uint64_t* tReads = nullptr;  // != nullptr: se admite leer el contador del TIMER4 (palabra entera) y se cuentan las lecturas
    uint32_t altDelta = 0;       // lo que se suma a esas lecturas (la vuelta en sombra)
    std::vector<std::pair<uint32_t, uint32_t>>& log;
    std::optional<P33>& p33Was;
    ProbeBus (Soc& s, std::vector<std::pair<uint32_t, uint32_t>>& l, std::optional<P33>& p) : soc (s), log (l), p33Was (p) {}

    bool load (uint32_t a, bool word = true)
    {
        if (bad) return false;
        if ((size_t) (a - kRamBase) < soc.ram.size() || soc.pureRead (a)) return true;
        if (tReads != nullptr && word && a == kT4Cnt)
        {
            ++*tReads;
            return true;
        }
        bad = true;
        badAddr = a;
        return false;
    }
    bool store (uint32_t a, uint32_t width, uint32_t word)
    {
        if (bad) return false;
        const auto o = a - kRamBase;
        if ((size_t) o < soc.ram.size())
        {
            // no en código (sus escrituras cuentan) ni más allá del final de la RAM
            if ((size_t) o + width > soc.ram.size() || soc.ram.isCode (o))
            {
                bad = true;
                badAddr = a;
                return false;
            }
            const auto w = o & ~3u;
            log.push_back ({ w, soc.ram.read32 (w) });
            if ((o & 3) + width > 4)  // un store no alineado llega a la palabra siguiente
                log.push_back ({ w + 4, soc.ram.read32 (w + 4) });
            return true;
        }
        if (! soc.loopWriteOk (word))
        {
            bad = true;
            badAddr = a;
            return false;
        }
        if (! p33Was && word - P33::kCon < 8)
            p33Was = soc.p33;
        return true;
    }
    uint8_t read8 (uint32_t a) override { return load (a & ~3u, false) ? soc.read8 (a) : 0; }
    uint16_t read16 (uint32_t a) override { return load (a & ~3u, false) ? soc.read16 (a) : 0; }
    uint32_t read32 (uint32_t a) override { return load (a) ? soc.read32 (a) + (a == kT4Cnt ? altDelta : 0u) : 0; }
    uint32_t fetch32 (uint32_t a) override { return load (a) ? soc.fetch32 (a) : 0; }
    void write8 (uint32_t a, uint8_t v) override { if (store (a, 1, a & ~3u)) soc.write8 (a, v); }
    void write16 (uint32_t a, uint16_t v) override { if (store (a, 2, a & ~3u)) soc.write16 (a, v); }
    void write32 (uint32_t a, uint32_t v) override { if (store (a, 4, a)) soc.write32 (a, v); }
    bool hwLock (uint32_t, bool) override { bad = true; badAddr = 1; return true; }
    void spin (uint32_t, uint32_t) override { bad = true; badAddr = 2; }  // la vuelta de un cerrojo de byte no es un bucle de sondeo
    void idle (uint32_t) override { bad = true; badAddr = 3; }
};
} // namespace

bool Machine::LoopSkip::mayProbe (uint64_t now, uint32_t pc, uint32_t room)
{
    if (now < nextTry)
        return false;
    if (loopAt (pc) == pc ? room < kProbeKnown : (room < kProbeMin || now < backoffUntil))
        return false;
    const auto earned = (int64_t) std::min<uint64_t> ((now > creditAt ? now - creditAt : 0) / 2048, (uint64_t) kCreditMax);
    credit = std::min (credit + earned, kCreditMax);
    creditAt = now;
    // sin crédito, ni sondeo ni paradas de la ráfaga para buscarlo hasta que vuelva (1 por cada 2048 ciclos): si no, la
    // ráfaga se para en cada instrucción (burstStop) para nada y todo va por la ruta de arriba
    if (credit <= 0)
        nextTry = now + (uint64_t) (1 - credit) * 2048;
    return credit > 0;
}

// Sin sondeos un tiempo (doblando hasta ~17 ms a 240 MHz), salvo en los pc con bucle conocido
void Machine::LoopSkip::backOff (uint64_t now)
{
    backoff = std::clamp<uint64_t> (backoff * 2, 1u << 11, 1u << 22);
    backoffUntil = now + backoff;
}

// Busca un bucle de sondeo en el pc de cpu0 con el tick de esa instrucción pagado. Devuelve las instrucciones ejecutadas,
// vueltas saltadas incluidas (como mucho budget; el tick de la siguiente sin pagar), o 0: no hay bucle y nada cambió
uint32_t Machine::probeLoop (uint32_t budget)
{
    Cpu::PlainState start;
    if (! cpu0.plainState (start))
        return 0;
    const auto pc0 = start.pc;
    const auto cycles0 = soc.periph.cycles, due0 = soc.periph.nextDue, wdt0 = soc.periph.wdtAt;
    const auto adc0 = soc.periph.adcState();
    const auto insns0 = cpu0.insnCount;
    const auto lastCnt0 = soc.periph.lastCntRead, idleSkipped0 = soc.periph.skipped;
    const auto streak0 = soc.periph.cntStreak;
    auto& ls = loopSkip;
    ls.tReads = 0;
    {
        const auto room = due0 > cycles0 ? due0 - cycles0 : 0;
        ++ls.roomHist[room < 2048 ? 0 : room < 8192 ? 1 : room < 32768 ? 2 : room < 131072 ? 3 : 4];
    }
    const bool timerOk = ls.timerOk && ! soc.cpu1Running && ! cpu1;
    ++ls.probes;
    ls.log.clear();
    ls.seen.clear();
    ls.pcs.clear();
    std::optional<P33> p33Was;
    uint32_t i = 0;
    size_t visits = 0;
    int result = 0;  // 1 = vuelta encontrada (len en roundLen); -1 = no; -2 = un evento vence (mirar después de él);
                     // -3 = interrupción o reset pendiente (sin espera)
    uint32_t roundLen = 0;
    size_t found = 0;  // la visita con la que cerró la vuelta
    while (true)
    {
        Cpu::PlainState core;
        if (cpu0.pc == pc0 && cpu0.plainState (core))
        {
            const auto snap = soc.p33.snap();
            const auto adc = soc.periph.adcState();
            const LoopSkip::Visit visit { core, snap, adc, i, ls.log.size(), ls.tReads, soc.periph.cycles, soc.periph.nextDue, soc.periph.wdtAt,
                                          soc.periph.lastCntRead, soc.sideEffects, soc.periph.cntStreak };
            // (con lecturas del contador en medio, los registros de datos y las banderas pueden diferir: restos de la vuelta
            // anterior que la siguiente reescribe antes de leerlos; lo comprueba probeTimerAlt)
            auto sameCore = [this, &core] (const LoopSkip::Visit& s)
            {
                if (s.core == core)
                    return true;
                if (loopSkip.tReads <= s.tReads)
                    return false;
                auto c = s.core;
                std::copy (std::begin (core.regs), std::end (core.regs), std::begin (c.regs));
                c.flags = core.flags;
                return c == core;
            };
            size_t j = 0;
            while (j < ls.seen.size() && ! (sameCore (ls.seen[j]) && ls.seen[j].p33 == snap && ls.seen[j].adc == adc))
                ++j;
            if (j < ls.seen.size())
            {
                // cada palabra que la vuelta escribió conserva lo que tenía al empezarla (el primer valor apuntado)
                ls.scratch.assign (ls.log.begin() + (std::ptrdiff_t) ls.seen[j].log, ls.log.end());
                std::stable_sort (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
                ls.scratch.erase (std::unique (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first == b.first; }),
                                  ls.scratch.end());
                // (con lecturas del contador, la pila muerta puede guardar su valor: ver LoopSkip::timerOk)
                const bool timed = ls.tReads > ls.seen[j].tReads;
                const auto sp = core.sp;
                // (con lecturas del contador, cualquier palabra puede guardar su valor o algo hecho con él: lo juzgan las
                // vueltas en sombra de probeTimerAlt, que exigen que esté muerta al empezar)
                // (y entonces la vuelta debe ser un periodo: la visita j cerraba otra vuelta igual, mismo recorrido de pc. Si
                // no, el pc se visita varias veces por periodo y el tramo siguiente puede ser otro: no es una vuelta)
                const auto len = i - ls.seen[j].at;
                const bool period = ! timed || std::any_of (ls.seen.begin(), ls.seen.end(), [&] (const LoopSkip::Visit& p)
                {
                    return p.at + len == ls.seen[j].at
                        && std::equal (ls.pcs.begin() + p.at, ls.pcs.begin() + ls.seen[j].at, ls.pcs.begin() + ls.seen[j].at);
                });
                if (period && (timed || std::all_of (ls.scratch.begin(), ls.scratch.end(), [this, timed, sp] (const auto& e)
                    {
                        const auto a = kRamBase + e.first;
                        return soc.ram.read32 (e.first) == e.second || (timed && a < sp && a + kDeadStack >= sp);
                    })))
                {
                    roundLen = i - ls.seen[j].at;
                    found = j;
                    result = 1;
                    break;
                }
                if (ls.tReads > ls.seen[j].tReads && ls.seen.size() < kVisits)
                    ls.seen.push_back (visit);  // (con contador se guarda la historia: el periodo se mira en dos vueltas)
                else
                    ls.seen[j] = visit;  // (una primera vuelta que preparó algo: comparar con esta en adelante)
            }
            else if (ls.seen.size() < kVisits)
                ls.seen.push_back (visit);
            else
                ls.seen[visits % kVisits] = visit;
            ++visits;
        }
        if (i >= budget || i >= ls.probeMax)
        {
            ++ls.failNoRound;
            result = -1;
            break;
        }
        // (el tick de la primera instrucción está pagado; con algo pendiente sigue el bucle normal)
        if (i > 0 && soc.periph.tick())
        {
            ++ls.failEvent;
            result = -2;
            break;
        }
        ls.pcs.push_back (cpu0.pc);
        ProbeBus bus (soc, ls.log, p33Was);
        if (timerOk)
            bus.tReads = &ls.tReads;
        const bool ok = cpu0.step (bus);
        // (un reset pedido: llega tras esta vuelta, no tras las saltadas)
        if (! ok || bus.bad)
        {
            ++ls.failBad;
            if (ls.badAt != nullptr && bus.bad)
                ++(*ls.badAt)[bus.badAddr];
            result = -1;
            break;
        }
        if (soc.periph.anyPending() || soc.resetDue())
        {
            ++ls.failIrq;
            result = -3;  // la cortó una interrupción o un reset: no dice nada de si hay bucle
            break;
        }
        ++i;
    }
    if (result == 1)
    {
        // vueltas enteras cuyos ticks caen antes del siguiente pendiente, y dentro del presupuesto
        const uint64_t l = roundLen;
        const auto c = soc.periph.cycles;
        const auto due = soc.periph.nextDue;
        const auto byEvent = (due > c + 1 ? due - (c + 1) : 0) / l;
        const auto byBudget = (uint64_t) (budget - i) / l;
        auto k = std::min (byEvent, byBudget);
        if (const auto roundReads = ls.tReads - ls.seen[found].tReads; roundReads > 0)
        {
            // con contador: la vuelta debe poder repetirse con otro valor, y la última se ejecuta de verdad (ver timerOk)
            const bool fine = l >= kMinTimedRound && ! p33Was && soc.periph.cntStreak == 0 && soc.periph.skipped == idleSkipped0
                              && probeTimerAlt (ls.seen[found], roundLen, roundReads);
            k = fine && k > 0 && ls.timerSkip ? k - 1 : 0;
            if (fine && k > 0)
                ++ls.timerHits;
            // comprobada: desde ahora se observa (Observe) en lugar de sondearla
            if (fine && observe.enabled && l >= kMinObserved && observe.pcAt (pc0) == 0xffffffffu && observe.known.size() < 8)
            {
                Observe::Known kn { pc0, roundLen, (uint32_t) roundReads, 0, 0, 0, false };
                for (size_t e = ls.seen[found].log; e < ls.log.size(); ++e)
                {
                    ++kn.writes;
                    kn.addrHash += (uint64_t) (ls.log[e].first + 0x9e3779b9u) * 0x9e3779b97f4a7c15ull;
                }
                Cpu::PlainState endCore;
                cpu0.plainState (endCore);
                for (int r = 0; r < 16; ++r)
                    if (endCore.regs[r] != ls.seen[found].core.regs[r])
                        kn.deadRegs |= 1u << r;
                kn.deadFlags = endCore.flags != ls.seen[found].core.flags;
                ls.scratch.assign (ls.log.begin() + (std::ptrdiff_t) ls.seen[found].log, ls.log.end());
                std::stable_sort (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
                ls.scratch.erase (std::unique (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first == b.first; }),
                                  ls.scratch.end());
                for (const auto& [w, startVal] : ls.scratch)
                    if (soc.ram.read32 (w) != startVal)
                        kn.deadWords.push_back (w);
                observe.known.push_back (std::move (kn));
            }
        }
        if (ls.hitsAt != nullptr)
        {
            auto& e = (*ls.hitsAt)[{ pc0, roundLen, (uint32_t) (ls.tReads - ls.seen[found].tReads) }];
            ++e.first;
            e.second += k;
        }
        soc.periph.cycles += k * l;
        cpu0.insnCount += k * l;
        ls.credit = std::min<int64_t> (ls.credit + (int64_t) (k * l / 4) - (int64_t) i, kCreditMax);
        ++ls.hits;
        ls.skipped += k * l;
        // (solo las vueltas sin contador: las que lo leen son largas, las lleva Observe, y sus pc llenarían la tabla de 64:
        // la ráfaga pararía en ellos y el JIT no encadenaría)
        if (ls.tReads == ls.seen[found].tReads)
        {
            for (size_t q = i - roundLen; q < i; ++q)  // todos los pc de la vuelta: un tramo puede empezar en cualquiera
            {
                ls.loopAt (ls.pcs[q]) = ls.pcs[q];
                ls.loopsMask |= 1ull << ((ls.pcs[q] >> 1) & 63);
            }
            ls.hasLoops = true;
        }
        if (k == 0 && ls.tReads > ls.seen[found].tReads)
            ls.backOff (soc.periph.cycles);  // con contador y sin salto (corta, o sin sitio): como un fallo
        else if (k * l >= 8ull * i)
        {
            ls.backoff = 0;
            ls.backoffUntil = 0;
            ls.nextTry = 0;
        }
        else if (byEvent < byBudget)
            ls.nextTry = due;  // un evento cerca: mirar después de él
        else
            ls.backOff (soc.periph.cycles);
        return i + (uint32_t) (k * l);
    }
    // deshacerlo todo: el bucle normal sigue desde donde empezó el sondeo, como sin él
    for (auto it = ls.log.rbegin(); it != ls.log.rend(); ++it)
        soc.ram.restore32 (it->first, it->second);
    if (p33Was)
        soc.p33 = *p33Was;
    soc.periph.setAdcState (adc0);
    soc.periph.nextDue = due0;
    soc.periph.wdtAt = wdt0;
    soc.periph.lastCntRead = lastCnt0;
    soc.periph.cntStreak = streak0;
    soc.periph.skipped = idleSkipped0;
    soc.clearResetReq();
    soc.periph.cycles = cycles0;
    cpu0.setPlainState (start);
    cpu0.insnCount = insns0;
    ls.credit -= i;
    if (result == -2)
        ls.nextTry = due0;
    else if (result == -1 && ls.loopAt (pc0) != pc0)
        ls.backOff (cycles0);
    // (en un pc con bucle conocido un fallo es casi siempre cpu0 saliendo del bucle, despertada: solo cuesta crédito)
    return 0;
}

// La vuelta que acaba de cerrar el sondeo (de la visita v al pc actual, roundLen instrucciones, roundReads lecturas del
// contador del TIMER4), repetida en sombra con el contador desplazado 2^31: true si recorre los mismos pc, lee el contador
// las mismas veces y vuelve al mismo estado (registros y RAM salvo la pila muerta). Lo deja todo como al final de la vuelta
// real. Veredicto en caché por la firma de la vuelta (sus pc): un recorrido distinto (un botón pulsado...) se comprueba aparte
bool Machine::probeTimerAlt (const LoopSkip::Visit& v, uint32_t roundLen, uint64_t roundReads)
{
    // A: los registros (y banderas) que la vuelta deja distintos, alterados al empezar y el contador como estaba: el final
    // debe ser el de la vuelta real, bit a bit (están muertos al empezar). B: el contador desplazado 2^31: mismo recorrido y
    // mismo estado salvo esos registros y la pila muerta (el contador solo llega a ellos)
    return probeShadow (v, roundLen, roundReads, true) && probeShadow (v, roundLen, roundReads, false);
}

bool Machine::probeShadow (const LoopSkip::Visit& v, uint32_t roundLen, uint64_t roundReads, bool perturbDead)
{
    auto& ls = loopSkip;
    const auto from = ls.pcs.size() - roundLen;
    uint64_t h = (1469598103934665603ull ^ roundLen) + (perturbDead ? 1u : 0u);
    for (size_t q = from; q < ls.pcs.size(); ++q)
        h = (h ^ ls.pcs[q]) * 1099511628211ull;
    if (const auto it = ls.verdict.find (h); it != ls.verdict.end())
        return it->second > 0;
    ++ls.timerVerified;

    // el final de la vuelta real, para volver a él
    Cpu::PlainState endCore;
    if (! cpu0.plainState (endCore))
        return false;
    const auto insns = cpu0.insnCount;
    const auto cyc = soc.periph.cycles, due = soc.periph.nextDue, wdt = soc.periph.wdtAt;
    const auto adc = soc.periph.adcState();
    const auto lastCnt = soc.periph.lastCntRead, idleSkipped = soc.periph.skipped, side = soc.sideEffects;
    const auto streak = soc.periph.cntStreak;
    ls.realEnd.clear();
    for (size_t e = v.log; e < ls.log.size(); ++e)
        ls.realEnd.push_back ({ ls.log[e].first, soc.ram.read32 (ls.log[e].first) });
    // las palabras que escribe la vuelta real y las que deja distintas (el primer valor apuntado es el del principio)
    ls.scratch.assign (ls.log.begin() + (std::ptrdiff_t) v.log, ls.log.end());
    std::stable_sort (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    ls.scratch.erase (std::unique (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first == b.first; }),
                      ls.scratch.end());
    ls.realWords.clear();
    ls.deadWords.clear();
    for (const auto& [w, startVal] : ls.scratch)
    {
        ls.realWords.push_back (w);
        if (soc.ram.read32 (w) != startVal)
            ls.deadWords.push_back (w);
    }
    ls.credit -= (int64_t) roundLen;  // (la sombra también cuesta)
    // al principio de la vuelta
    for (size_t e = ls.log.size(); e-- > v.log;)
        soc.ram.restore32 (ls.log[e].first, ls.log[e].second);
    // los registros que difieren al final de la vuelta real (A: alterados al empezar)
    uint32_t deadRegs = 0;
    for (int r = 0; r < 16; ++r)
        if (endCore.regs[r] != v.core.regs[r])
            deadRegs |= 1u << r;
    const bool deadFlags = endCore.flags != v.core.flags;
    auto startCore = v.core;
    if (perturbDead)
    {
        for (int r = 0; r < 16; ++r)
            if ((deadRegs >> r) & 1u)
                startCore.regs[r] ^= 0xa5a5a5a5u;
        if (deadFlags)
            startCore.flags ^= 0xfu;
    }
    if (perturbDead)
        for (const auto w : ls.deadWords)
            soc.ram.restore32 (w, soc.ram.read32 (w) ^ 0xa5a5a5a5u);
    cpu0.setPlainState (startCore);
    soc.periph.cycles = v.cyc;
    soc.periph.nextDue = v.due;
    soc.periph.wdtAt = v.wdt;
    soc.periph.setAdcState (v.adc);
    soc.periph.lastCntRead = v.lastCnt;
    soc.periph.cntStreak = v.streak;
    auto* const hist = cpu0.pcHist;
    cpu0.pcHist = nullptr;

    ls.altLog.clear();
    std::optional<P33> p33x;
    uint64_t reads = 0;
    bool ok = true;
    for (uint32_t q = 0; q < roundLen && ok; ++q)
    {
        if ((v.at + q > 0 && soc.periph.tick()) || cpu0.pc != ls.pcs[from + q])
        {
            ok = false;
            break;
        }
        ProbeBus bus (soc, ls.altLog, p33x);
        bus.tReads = &reads;
        bus.altDelta = perturbDead ? 0u : 0x80000000u;
        if (! cpu0.step (bus) || bus.bad || p33x || soc.periph.anyPending() || soc.resetDue())
            ok = false;
    }
    Cpu::PlainState back {};
    ok = ok && reads == roundReads && cpu0.plainState (back);
    if (ok && perturbDead)
    {
        // A: todo como en la vuelta real (registros y cada palabra escrita, pila muerta incluida)
        ok = back == endCore;
        for (const auto& [w, val] : ls.realEnd)
            ok = ok && soc.ram.read32 (w) == val;
        for (const auto& [w, old] : ls.altLog)
            ok = ok && std::any_of (ls.realEnd.begin(), ls.realEnd.end(), [w = w] (const auto& e) { return e.first == w; });
    }
    else if (ok)
    {
        // B: el estado del principio salvo los registros muertos
        auto b = back;
        for (int r = 0; r < 16; ++r)
            if ((deadRegs >> r) & 1u)
                b.regs[r] = v.core.regs[r];
        if (deadFlags)
            b.flags = v.core.flags;
        ok = b == v.core;
    }
    if (ok && ! perturbDead)
    {
        ls.scratch = ls.altLog;
        std::stable_sort (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        ls.scratch.erase (std::unique (ls.scratch.begin(), ls.scratch.end(), [] (const auto& a, const auto& b) { return a.first == b.first; }),
                          ls.scratch.end());
        const auto sp = v.core.sp;
        ok = ls.scratch.size() == ls.realWords.size()
             && std::all_of (ls.scratch.begin(), ls.scratch.end(), [this, sp, q = size_t (0)] (const auto& e) mutable
        {
            const auto a = kRamBase + e.first;
            return e.first == loopSkip.realWords[q++]  // las mismas direcciones (el contador no las mueve)
                && (soc.ram.read32 (e.first) == e.second || (a < sp && a + kDeadStack >= sp)
                    || std::binary_search (loopSkip.deadWords.begin(), loopSkip.deadWords.end(), e.first));
        });
    }

    // de vuelta al final de la vuelta real
    for (auto it = ls.altLog.rbegin(); it != ls.altLog.rend(); ++it)
        soc.ram.restore32 (it->first, it->second);
    for (const auto& [w, val] : ls.realEnd)
        soc.ram.restore32 (w, val);
    cpu0.setPlainState (endCore);
    cpu0.insnCount = insns;
    cpu0.pcHist = hist;
    soc.periph.cycles = cyc;
    soc.periph.nextDue = due;
    soc.periph.wdtAt = wdt;
    soc.periph.setAdcState (adc);
    soc.periph.lastCntRead = lastCnt;
    soc.periph.cntStreak = streak;
    soc.periph.skipped = idleSkipped;
    soc.sideEffects = side;
    soc.clearResetReq();

    if (ls.verdict.size() > 4096)
        ls.verdict.clear();
    ls.verdict[h] = ok ? 1 : -1;
    if (! ok)
        ++ls.timerRejected;
    return ok;
}

void Machine::observeStop()
{
    observe.active = false;
    soc.observing = false;
    soc.ram.log = nullptr;
}

// cpu0 en el pc de la vuelta observada, con su tick pagado: ¿la vuelta se repite igual? Devuelve las instrucciones que se
// saltan (vueltas enteras, dejando sitio para una más antes del siguiente evento y dentro del tramo), 0 si ninguna
uint64_t Machine::observeEnd (uint32_t budget)
{
    auto& o = observe;
    const auto& kn = o.known[o.idx];
    observeStop();
    ++o.rounds;
    Cpu::PlainState core {};
    bool ok = cpu0.insnCount - o.insn0 == kn.len && soc.obsT4 - o.t40 == kn.reads && soc.obsImpure == o.impure0
              && soc.advances == o.adv0 && o.log.size() == kn.writes && soc.periph.cntStreak == 0 && ! soc.periph.anyPending()
              && cpu0.plainState (core) && soc.p33.snap() == o.p33 && soc.periph.adcState() == o.adc;
    if (ok)
    {
        uint64_t hsh = 0;
        for (const auto& e : o.log)
            hsh += (uint64_t) (e.first + 0x9e3779b9u) * 0x9e3779b97f4a7c15ull;
        auto c = core;
        for (int r = 0; r < 16; ++r)
            if ((kn.deadRegs >> r) & 1u)
                c.regs[r] = o.start.regs[r];
        if (kn.deadFlags)
            c.flags = o.start.flags;
        ok = hsh == kn.addrHash && c == o.start;
        // cada palabra escrita vuelve a lo que tenía, salvo la pila muerta (el primer valor apuntado de cada una)
        if (ok)
        {
            std::stable_sort (o.log.begin(), o.log.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
            o.log.erase (std::unique (o.log.begin(), o.log.end(), [] (const auto& a, const auto& b) { return a.first == b.first; }), o.log.end());
            const auto sp = core.sp;
            ok = std::all_of (o.log.begin(), o.log.end(), [this, sp, &kn] (const auto& e)
            {
                const auto a = kRamBase + e.first;
                return soc.ram.read32 (e.first) == e.second || (a < sp && a + kDeadStack >= sp)
                    || std::binary_search (kn.deadWords.begin(), kn.deadWords.end(), e.first);
            });
        }
    }
    if (! ok)
    {
        ++o.misses;
        return 0;
    }
    const uint64_t l = kn.len;
    const auto cyc = soc.periph.cycles;  // (el tick de la instrucción en el pc está pagado)
    const auto due = soc.periph.nextDue;
    const auto byEvent = (due > cyc ? due - cyc : 0) / l;
    const auto byBudget = (uint64_t) budget / l;
    const auto rounds = std::min (byEvent, byBudget);
    if (rounds < 2)
        return 0;
    ++o.hits;
    const auto k = (rounds - 1) * l;
    o.skipped += k;
    return k;
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
        loopSkip.nextTry = 0;
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
            loopSkip.nextTry = 0;
            break;
        }
        cpu0Between();
        // GHOULBOX: la pasada vacía de midi_cc_apply, entera y al mismo ciclo (ver CcPoll). El tick de esta instrucción está
        // pagado, como en el bucle contado de abajo
        if (ccPoll.mode != CcPoll::Off)
        {
            if (! ccPoll.searched)
                ccPollFind();
            if (ccPoll.pending && cpu0.pc == ccPoll.exit)
                ccPollCompare();
            else if (ccPoll.entry != 0 && cpu0.pc == ccPoll.entry && ! ccPoll.pending)
            {
                const auto c = soc.periph.cycles, due = soc.periph.nextDue;
                if (ccPollEnter ((uint32_t) std::min<uint64_t> (n - done, due > c ? due - c : 0)))
                {
                    soc.periph.cycles += (uint64_t) ccPoll.insns - 1;
                    done += ccPoll.insns;
                    continue;
                }
            }
        }
        // vueltas observadas (ver Observe): al acabar una en su pc, el salto; luego, la siguiente se observa
        if (observe.active && cpu0.insnCount - observe.insn0 > observe.known[observe.idx].len)
            observeStop();  // más larga que la conocida: salió del bucle
        bool observed = false;
        if (! observe.known.empty() && loopSkip.enabled)
        {
            // (al cumplir su longitud: el pc puede visitarse varias veces por periodo)
            if (observe.active && cpu0.pc == observe.known[observe.idx].pc0
                && cpu0.insnCount - observe.insn0 == observe.known[observe.idx].len)
            {
                if (const auto k = observeEnd (n - done); k > 0)
                {
                    soc.periph.cycles += k - 1;  // (como CcPoll: el tick de la primera está pagado)
                    cpu0.insnCount += k;
                    done += (uint32_t) k;
                    continue;
                }
            }
            if (const auto idx = observe.pcAt (cpu0.pc); idx != 0xffffffffu && ! observe.active && ! soc.cpu1Release && ! cpu1
                && ! soc.cpu1Running && ! soc.periph.anyPending() && cpu0.plainState (observe.start))
            {
                observe.active = true;
                observe.idx = idx;
                observe.p33 = soc.p33.snap();
                observe.adc = soc.periph.adcState();
                observe.insn0 = cpu0.insnCount;
                observe.t40 = soc.obsT4;
                observe.impure0 = soc.obsImpure;
                observe.adv0 = soc.advances;
                observe.log.clear();
                soc.observing = true;
                soc.ram.log = &observe.log;
            }
            observed = observe.active;
        }
        // (no con una liberación de cpu1 retenida: el tramo debe cortarse donde lo haría sin el salto)
        if (! observed && loopSkip.enabled && ! soc.cpu1Release && n - done >= kProbeKnown && ! soc.periph.anyPending()
            && ((cpu0.pc == loopSkip.forcePc && n - done >= kProbeMin) || loopSkip.mayProbe (soc.periph.cycles, cpu0.pc, n - done)))
        {
            if (soc.periph.nextDue < soc.periph.cycles + kProbeKnown)
                loopSkip.nextTry = soc.periph.nextDue;  // demasiado cerca del siguiente evento
            else if (const auto k = probeLoop (n - done); k > 0)
            {
                done += k;
                continue;  // (el tick de la siguiente instrucción, sin pagar)
            }
        }
        // bucle de retardo contado: vueltas enteras hasta antes del siguiente evento, exacto (ver Cpu::skipCountedLoop). El tick
        // de esta instrucción está pagado: k instrucciones dejan el reloj k - 1 más allá, y el tick de la siguiente lo paga el bucle
        if (cpu0.pc == cpu0.loopHint && cpu0.counted (cpu0.pc) && loopSkip.enabled && ! soc.cpu1Release && ! soc.periph.anyPending())
        {
            const auto c = soc.periph.cycles, due = soc.periph.nextDue;
            const auto room = (uint32_t) std::min<uint64_t> (n - done, due > c ? due - c : 0);
            const auto k = cpu0.skipCountedLoop (soc, room);
            if (k < 0)
                cpu0.markNotCounted (cpu0.pc);
            else if (k > 0)
            {
                soc.periph.cycles += (uint64_t) k - 1;
                done += (uint32_t) k;
                ++loopSkip.counted;
                loopSkip.countedSkipped += (uint64_t) k;
                continue;
            }
        }
        // (lo que vale al empezar la instrucción: si nada está pendiente y no hay accesos con efectos, sigue igual)
        // Con una interrupción pendiente que la CPU aún no acepta (otra ISR de prioridad igual o mayor en curso, cli...), la
        // ráfaga también vale: lo pendiente solo cambia con un tick que vence o un acceso con efectos (la ráfaga se para en
        // los dos), y la CPU la aceptaría en cuanto irqReady (prioridad) sea cierto, que la ráfaga mira antes de cada
        // instrucción (sin ninguna habilitada en el banco de cpu0, nada puede entrar: sin mirar)
        bool calm = fastPath && ! soc.cpu1Release && ! soc.waits[0];
        int blockedPrio = -1;
        ++statTop;
        if (calm && soc.periph.anyPending())
        {
            ++statTopPending;
            if (const auto p = soc.periph.pendingIrqFor (0); p)
            {
                if (cpu0.irqReady (p->second))
                    calm = false;
                else
                    blockedPrio = p->second;
            }
        }
        const auto stop0 = soc.stopEffects;
        const bool trip = cpu0.pcEnter (cpu0.pc);
        if (! cpu0.step (soc))
        {
            error = faultText (cpu0, "cpu0");
            return false;
        }
        ++done;
        if (trip)
            soc.guardTrip (12);  // la primera instrucción fuera de los límites corre, luego la excepción
        // Ráfaga: las instrucciones siguientes sin las comprobaciones de arriba, mientras no puedan cambiar su resultado.
        // Un reset, una interrupción pendiente, la liberación de cpu1, un cerrojo o las guardas de pc solo cambian con un tick
        // que vence o con un acceso de Soc::sideEffects; y se para donde arriba actuaría un salto de bucle (pc de bucle
        // conocido, hueco de sondeo, bucle contado). El tick de cada una se paga aquí: no vence
        if (calm && ! trip)
        {
            // (en Cpu::burst; las paradas son las de burstStop)
            Cpu::BurstStop bs;
            bs.sideEffects = &soc.stopEffects;  // (los que paran la ráfaga: ver Soc::stopEffects)
            bs.side0 = stop0;
            bs.cycles = &soc.periph.cycles;
            bs.due = soc.periph.nextDue;
            bs.inIsr = &soc.periph.inIsr;
            bs.room = n - done;
            bs.blockedPrio = blockedPrio;
            if (ccPoll.mode != CcPoll::Off && ccPoll.entry != 0)
            {
                bs.stopA = ccPoll.entry;
                if (ccPoll.pending)
                    bs.stopB = ccPoll.exit;
            }
            if (! observe.known.empty() && ! observe.active && bs.stopB == 0xffffffffu)
                bs.stopB = observe.known[0].pc0;  // (para empezar a observar; con varias conocidas, la primera)
            if (observe.active && bs.stopB == 0xffffffffu)
            {
                bs.stopB = observe.known[observe.idx].pc0;
                // (y no más allá de su longitud: si sale del bucle, arriba lo deja)
                bs.room = (uint32_t) std::min<uint64_t> (bs.room, observe.known[observe.idx].len + 1 - std::min<uint64_t> (
                                                                      cpu0.insnCount - observe.insn0, observe.known[observe.idx].len));
            }
            // Con una interrupción pendiente (aún sin aceptar), arriba ni se sondea un bucle ni se salta uno contado (los dos
            // piden ! anyPending): pararse por ellos solo daba una instrucción por ráfaga (GHOULBOX con 12 notas: 37 M de 47 M
            // pasos por arriba, 4030 sondeos). Lo pendiente solo cambia con un evento o un acceso con efectos, y la ráfaga se
            // para en los dos
            bs.loopEnabled = loopSkip.enabled && ! soc.periph.anyPending();
            bs.nextTry = loopSkip.nextTry;
            bs.backoffUntil = loopSkip.backoffUntil;
            bs.loops = loopSkip.loops.data();
            bs.probeKnown = kProbeKnown;
            bs.probeMin = kProbeMin;
            static_assert (kProbeMin >= kProbeKnown);
            bs.probeAnyAt = std::max (loopSkip.nextTry, loopSkip.backoffUntil);
            bs.anyLoops = loopSkip.hasLoops;
            bs.loopsMask = loopSkip.loopsMask;
            bool ok = true, tripped = false;
            const auto k = cpu0.burst (soc, bs, ok, tripped);
            done += k;
            statBurst += k;
            if (! ok)
            {
                error = faultText (cpu0, "cpu0");
                return false;
            }
            if (tripped)
                soc.guardTrip (12);
        }
        if (soc.cpu1Release || soc.waits[0])
            break;
    }
    return true;
}

// ¿Actuaría aquí el bucle de runBlocks antes de ejecutar (sondeo de bucle o bucle contado)? Con el tick de la instrucción
// aún sin pagar, como lo mira la ráfaga
bool Machine::burstStop (uint32_t room) const noexcept
{
    if (ccPoll.mode != CcPoll::Off && ccPoll.entry != 0
        && (cpu0.pc == ccPoll.entry || (ccPoll.pending && cpu0.pc == ccPoll.exit)))
        return true;
    if (! loopSkip.enabled)
        return false;
    const auto pc = cpu0.pc;
    if (pc == cpu0.loopHint && cpu0.counted (pc))
        return true;
    const auto now = soc.periph.cycles + 1;
    return room >= kProbeKnown && now >= loopSkip.nextTry
        && (loopSkip.loops[(pc >> 1) & 63] == pc || (room >= kProbeMin && now >= loopSkip.backoffUntil));
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
    c1->fastPath = fastPath;
    c1->jitOn = cpu0.jitOn;  // (el mismo interruptor que cpu0)
    c1->synHist = synHist1;
    c1->pcHist = pcHist1;
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
    if (hle.mode != Hle::Off && ! hle.site && hle.searchIn-- == 0)
    {
        hle.site = findFmCoreRender (soc.ram.ptr(), kRamBase, soc.ram.size());
        if (! hle.site && hle.nKern == 0)
        {
            hle.nKern = findV16Kernels (soc.ram.ptr(), kRamBase, soc.ram.size(), hle.kern.data(), Hle::kMaxKernels);
            for (int i = 0; i < hle.nKern; ++i)
            {
                hle.kMin = std::min (hle.kMin, hle.kern[(size_t) i].hook);
                hle.kMax = std::max (hle.kMax, hle.kern[(size_t) i].hook);
            }
        }
        hle.searchIn = 4096;  // sin el código aún copiado: otra vez dentro de un rato
    }
    const auto probePc = c1.pc;
    auto probeSp = c1.sp;
    uint32_t probeRegs[16];
    std::memcpy (probeRegs, c1.regs, sizeof probeRegs);
    const uint64_t budget = std::min<uint64_t> (cycles, 1u << 20);
    uint64_t left = budget;
    // La interrupción pendiente para cpu1, en caché mientras no haya un acceso con efectos (Soc::sideEffects): lo pendiente
    // y la configuración de los bancos (icfg, icfg1, ilat) solo cambian así dentro de este tramo (los eventos de tiempo van en
    // el de cpu0). irqReady (habilitación y prioridad de la CPU) se mira en cada vuelta. Antes se recalculaba tras cada bloque
    // del JIT o cada paso: medido con 12 notas en Tessera, pendingIrqFor era el 8 % del tiempo del emulador
    uint64_t irqSide = soc.sideEffects - 1;
    std::optional<std::pair<uint8_t, uint8_t>> irq;
    while (left > 0)
    {
        if (soc.sideEffects != irqSide)
        {
            irqSide = soc.sideEffects;
            irq = soc.periph.anyPending() ? soc.periph.pendingIrqFor (1) : std::nullopt;
        }
        if (irq && c1.irqReady (irq->second))
            c1.interrupt (soc.read32 (kVecBase + 4u * irq->first), irq->second);
        if (hle.site && c1.pc == hle.site.entry && hle.mode != Hle::Off && ! hle.pending && hleEnter (c1))
        {
            left -= std::min<uint64_t> (left, hle.costPerCall);
            continue;  // el nativo ya volvió a rets
        }
        if (hle.nKern > 0 && c1.pc >= hle.kMin && c1.pc <= hle.kMax && hle.mode != Hle::Off && ! hle.kOff && ! hle.kPending)
            if (const auto saved = hleKernelEnter (c1); saved > 0)
            {
                left -= std::min<uint64_t> (left, saved);
                continue;  // el nativo ya está en el epílogo
            }
        Core1Bus bus (soc, until);
        // un bloque del JIT si ninguna de las comprobaciones de este bucle puede saltar dentro de él: sin espera de cerrojo
        // ni comprobación del HLE en curso, sin el pc de sondeo ni los ganchos del HLE después de su primera instrucción
        // (las interrupciones y lo demás solo cambian con un acceso con efectos, tras el que el bloque para)
        uint32_t jr = 0;
        if (! until && ! hle.pending && ! hle.kPending)
        {
            bool okj = true;
            jr = c1.runJit (bus, (uint32_t) std::min<uint64_t> (left, 1u << 20), probePc, hle.site ? hle.site.entry : 0xffffffffu,
                            hle.nKern > 0 ? hle.kMin : 1u, hle.nKern > 0 ? hle.kMax : 0u, &soc.sideEffects, okj);
            if (! okj)
            {
                error = faultText (c1, "cpu1");
                return false;
            }
        }
        if (jr > 0)
            left -= jr;
        else
        {
            if (! c1.step (bus))
            {
                error = faultText (c1, "cpu1");
                return false;
            }
            --left;
        }
        if (hle.pending && c1.pc == hle.retPc && c1.sp == hle.retSp)
            hleCompare (c1);
        if (hle.kPending && c1.pc == hle.kExit && c1.sp == hle.kSp)
            hleKernelCompare (c1);
        if (soc.waits[1])
        {
            soc.waits[1].reset();
            break;  // espera un cerrojo de cpu0: toca el tramo de cpu0
        }
        if (soc.resetDue() || soc.cpu1Idle || bus.stop)
            break;
        if (c1.pc == probePc)
        {
            // de vuelta en el pc donde empezó: si la vuelta dejó todo como la anterior, sondea sin nada que hacer y el resto
            // del tramo sería igual (cpu0 no corre mientras tanto). La primera vuelta tras reanudar suele diferir del estado
            // en que se dejó (r0 trae lo último leído): se compara con la vuelta anterior, no solo con el inicio. V13, 4 s: de
            // 117 instrucciones de media por tramo ocioso a ~2 vueltas (fm1-emu compara solo con el inicio)
            if (c1.sp == probeSp && std::memcmp (c1.regs, probeRegs, sizeof probeRegs) == 0)
                break;  // sondeando sin nada que hacer
            probeSp = c1.sp;
            std::memcpy (probeRegs, c1.regs, sizeof probeRegs);
        }
        if (until && ! soc.stillWaits (0, *until))
            break;  // cpu0 ya obtendría lo que espera
    }
    ran = budget - left;
    return true;
}

// ---- HLE de FmCore::render ----------------------------------------------------------------------------------------
namespace
{
// Las cuatro zonas que la rutina escribe: salida (n int32), FmOpParams[6], fb_buf[2] y la fila de su algoritmo
struct HleZones { uint32_t at[4]; size_t len[4]; };
HleZones hleZones (const HleRenderArgs& a) { return { { a.out, a.params, a.fb, a.algTable + 6 * a.algo }, { 4u * a.n, 96, 8, 6 } }; }
} // namespace

// midi_cc_apply de GHOULBOX 2.4 (cabeza H = 0x0201ceb6; el bucle interno, ui_input.c de GHOULBOX):
//   H     r0 = STRIDE; r0 *= r8; r9 = r0 + r12; r15 = r9 + OFF; r0 = 0       (pista t = r8, r12 = &trk[0])
//   H+16  r11 = r0; r0 = r8 << 5; r0 += r11; r2 = r0 + r7; call guarda       (cola i = r11; la guarda: r0 = icfg; cli; rts)
//   H+30  r1 = b[r2 + r4]; b[r2 + r4] = r14; icfg = r0; if (r1 == 0) goto X  (r14 = 0: leer y borrar)
//   X     r0 = r11 + 1; if (r0 != NCC) goto H+16; r8 = r8 + 1; r12 = TRK; if (r8 != NTRK) goto H
// Se busca por los bytes del bucle interno (sin inmediatos ni desplazamientos) y el resto se comprueba decodificando
void Machine::ccPollFind()
{
    auto& p = ccPoll;
    p.searched = true;
    static const uint8_t inner[] = { 0x0b, 0x16, 0xc0, 0xe1, 0x85, 0x00, 0xb0, 0x18, 0x8a, 0x1d };
    static const uint8_t test[] = { 0xd8, 0xee, 0x20, 0x14, 0xd8, 0xee, 0x21, 0xe4, 0x64, 0xe0, 0x80, 0x0b };
    const auto& x = soc.xip;
    struct D { const Fm1IsaEntry* e = nullptr; uint64_t raw = 0; uint32_t pc = 0; };
    const auto dec = [this] (uint32_t pc)
    {
        D d;
        d.pc = pc;
        d.e = decodeAny (((uint64_t) soc.fetch32 (pc + 4) << 32) | soc.fetch32 (pc), d.raw);
        return d;
    };
    const auto sv = [] (const D& d, int k) { return slotValue (d.e->slots[k], d.raw); };
    constexpr int64_t any = INT64_MIN;
    const auto is = [&] (const D& d, const char* name, std::initializer_list<int64_t> want)
    {
        if (d.e == nullptr || std::strcmp (d.e->name, name) != 0 || d.e->slotCount < (int) want.size())
            return false;
        int k = 0;
        for (auto w : want)
            if (w != any && sv (d, k++) != w)
                return false;
            else if (w == any)
                ++k;
        return true;
    };
    const auto target = [&] (const D& d, int k) { return (uint32_t) ((int64_t) d.pc + d.e->len + sv (d, k)); };
    for (size_t o = 16; o + 14 + sizeof test <= x.size(); o += 2)
    {
        if (std::memcmp (&x[o], inner, sizeof inner) != 0 || std::memcmp (&x[o + 14], test, sizeof test) != 0)
            continue;
        const auto h = kXipBase + (uint32_t) o - 16;
        const auto a = dec (h), mul = dec (h + 4), add = dec (h + 6), off = dec (h + 10), zero = dec (h + 14);
        const auto call = dec (h + 26), br = dec (h + 42);
        if (! is (a, "r_mov_i_l4", { 0, any }) || ! is (mul, "r_mul_r", { 0, 8 }) || ! is (add, "r_mov_r_r_4", { 9, 0, 12 })
            || ! is (off, "r_mov_r_i_3", { 15, 9, any }) || ! is (zero, "r_mov_i_l2", { 0, 0 }) || ! is (call, "call_i_l4", { any })
            || ! is (br, "if_r_eq_i_goto_i_l4", { 1, 0, any }))
            continue;
        const auto g = target (call, 0);
        const auto g0 = dec (g), g1 = dec (g + 4), g2 = dec (g + 6);
        if (g0.e == nullptr || std::strcmp (g0.e->syntax, "R = icfg") != 0 || sv (g0, 0) != 0 || ! is (g1, "cli", {})
            || ! is (g2, "rts", {}))
            continue;
        const auto xb = target (br, 2);
        const auto inc = dec (xb), loopI = dec (xb + 4), incT = dec (xb + 8), trk = dec (xb + 12), loopT = dec (xb + 18);
        if (! is (inc, "r_mov_r_i_3", { 0, 11, 1 }) || ! is (loopI, "if_r_ne_i_goto_i_3", { 0, any, any })
            || ! is (incT, "r_mov_r_i_3", { 8, 8, 1 }) || ! is (trk, "r_mov_i", { 12, any }) || ! is (loopT, "if_r_ne_i_goto_i_3", { 8, any, any })
            || target (loopI, 2) != h + 16 || target (loopT, 2) != h)
            continue;
        const auto ncc = sv (loopI, 1), ntrk = sv (loopT, 1);
        if (ncc < 1 || ncc > 32 || ntrk < 1 || ntrk > 16)
            continue;
        p.entry = h;
        p.exit = xb + 22;
        p.stride = (uint32_t) sv (a, 1);
        p.offset = (uint32_t) sv (off, 2);
        p.trkBase = (uint32_t) sv (trk, 1);
        p.ncc = (uint32_t) ncc;
        p.ntrk = (uint32_t) ntrk;
        p.guardRet = h + 30;
        p.insns = p.ntrk * (5 + p.ncc * 14 + 3);
        return;
    }
}

bool Machine::ccPollEnter (uint32_t room)
{
    auto& p = ccPoll;
    auto& c = cpu0;
    // (room > insns: con un evento justo en la instrucción de salida, la emulada entra en la ISR antes de ella y Check
    // compararía tras la ISR; saltar también sería exacto, pero así Check y On cubren los mismos casos)
    if (p.insns >= room || cpu1 != nullptr || soc.cpu1Release || soc.waits[0] || soc.periph.anyPending() || ! c.irqLevels.empty()
        || c.repEnd != 0 || ! c.predSkips.empty() || c.pairPending || c.regs[8] != 0 || c.regs[12] != p.trkBase || c.regs[14] != 0)
        return false;
    const uint64_t q = (uint64_t) c.regs[7] + c.regs[4];
    if (q < kRamBase || q + (uint64_t) (p.ntrk - 1) * 32 + p.ncc > kRamBase + soc.ram.size())
        return false;
    const auto* b = soc.ram.ptr() + (q - kRamBase);
    for (uint32_t t = 0; t < p.ntrk; ++t)
        for (uint32_t i = 0; i < p.ncc; ++i)
            if (b[t * 32 + i] != 0)
                return false;  // un CC pendiente: lo aplica el firmware
    uint32_t r[16];
    std::memcpy (r, c.regs, sizeof r);
    const auto last = p.ntrk - 1;
    r[0] = p.ncc;
    r[1] = 0;
    r[2] = c.regs[7] + last * 32 + (p.ncc - 1);
    r[8] = p.ntrk;
    r[9] = p.stride * last + p.trkBase;
    r[11] = p.ncc - 1;
    r[12] = p.trkBase;
    r[15] = r[9] + p.offset;
    const auto icfgVal = c.icfg();  // la guarda lee icfg y lo vuelve a escribir tal cual (sin ISR en curso)
    // banderas: las de la última suma con banderas, r8 = (NTRK - 1) + 1 (positivo y pequeño: N C Z V a 0; medido en Check)
    const uint32_t flagsOut = 0;
    if (p.mode == CcPoll::On)
    {
        std::memcpy (c.regs, r, sizeof r);
        c.flags = flagsOut;
        c.rets = p.guardRet;
        c.sregStore[0] = icfgVal;
        c.ie = (icfgVal & 0x200) != 0;
        c.pc = p.exit;
        c.insnCount += p.insns;
        ++p.skipped;
        return true;
    }
    std::memcpy (p.expRegs, r, sizeof r);
    p.expRets = p.guardRet;
    p.expIcfg = icfgVal;
    p.expIe = (icfgVal & 0x200) != 0;
    p.expFlags = flagsOut;
    p.insnAtEntry = c.insnCount;
    p.cycAtEntry = soc.periph.cycles;
    p.dueAtEntry = soc.periph.nextDue;
    p.pending = true;
    return false;
}

void Machine::ccPollCompare()
{
    auto& p = ccPoll;
    const auto& c = cpu0;
    p.pending = false;
    ++p.checked;
    char buf[160] {};
    const auto ran = c.insnCount - p.insnAtEntry;
    if (ran != p.insns)
        std::snprintf (buf, sizeof buf, "pasada %llu: %llu instrucciones, esperadas %u (ciclo de entrada %llu, nextDue +%lld, ciclos %llu)",
                       (unsigned long long) p.checked, (unsigned long long) ran, p.insns, (unsigned long long) p.cycAtEntry,
                       (long long) (p.dueAtEntry - p.cycAtEntry), (unsigned long long) (soc.periph.cycles - p.cycAtEntry));
    for (int k = 0; k < 16 && buf[0] == 0; ++k)
        if (c.regs[k] != p.expRegs[k])
            std::snprintf (buf, sizeof buf, "pasada %llu: r%d 0x%08x, esperado 0x%08x", (unsigned long long) p.checked, k, c.regs[k], p.expRegs[k]);
    if (buf[0] == 0 && (c.rets != p.expRets || c.flags != p.expFlags || c.sregStore[0] != p.expIcfg || c.ie != p.expIe))
        std::snprintf (buf, sizeof buf, "pasada %llu: rets 0x%08x/0x%08x flags %x/%x icfg 0x%x/0x%x ie %d/%d", (unsigned long long) p.checked,
                       c.rets, p.expRets, c.flags, p.expFlags, c.sregStore[0], p.expIcfg, (int) c.ie, (int) p.expIe);
    if (buf[0] != 0 && p.mismatches++ == 0)
        p.firstMismatch = buf;
    if (p.mode == CcPoll::Auto)
    {
        if (p.mismatches > 0)
            p.mode = CcPoll::Off;
        else if (p.checked >= p.autoChecks)
            p.mode = CcPoll::On;
    }
}

uint32_t Machine::hleKernelEnter (Cpu& c1)
{
    int i = 0;
    while (i < hle.nKern && hle.kern[(size_t) i].hook != c1.pc)
        ++i;
    if (i == hle.nKern)
        return 0;
    const auto& k = hle.kern[(size_t) i];
    ++hle.kCalls;
    auto* ram = soc.ram.ptr();
    const auto size = soc.ram.size();
    uint32_t regs[16];
    std::memcpy (regs, c1.regs, sizeof regs);
    const auto cost = hle.kCost[(size_t) i];
    if (cost != 0 && (hle.mode == Hle::On || (hle.mode == Hle::Auto && hle.kOn)))
    {
        if (! hleV16Kernel (ram, kRamBase, size, k, regs))
            return 0;
        std::memcpy (c1.regs, regs, sizeof regs);
        c1.pc = k.exit;
        c1.insnCount += cost;
        ++hle.kNative;
        return cost;
    }
    // Check: el nativo calcula aparte (la RAM se restaura) y el firmware sigue con su código hasta el epílogo
    const uint32_t outA = regs[4], fbA = regs[6];
    const bool fb = k.kind == HleKernel::Fb;
    const auto inRam = [&] (uint32_t a, uint32_t len) { return a >= kRamBase && (size_t) (a - kRamBase) + len <= size; };
    if (! inRam (outA, 256) || (fb && ! inRam (fbA, 8)))
        return 0;
    std::array<uint8_t, 256> saveOut;
    std::array<uint8_t, 8> saveFb {};
    std::memcpy (saveOut.data(), ram + (outA - kRamBase), 256);
    if (fb)
        std::memcpy (saveFb.data(), ram + (fbA - kRamBase), 8);
    if (! hleV16Kernel (ram, kRamBase, size, k, regs))
        return 0;
    std::memcpy (hle.kExpOut.data(), ram + (outA - kRamBase), 256);
    if (fb)
        std::memcpy (hle.kExpFb.data(), ram + (fbA - kRamBase), 8);
    std::memcpy (hle.kExpRegs, regs, sizeof regs);
    std::memcpy (ram + (outA - kRamBase), saveOut.data(), 256);
    if (fb)
        std::memcpy (ram + (fbA - kRamBase), saveFb.data(), 8);
    hle.kPending = true;
    hle.kIdx = i;
    hle.kExit = k.exit;
    hle.kSp = c1.sp;
    hle.kOutA = outA;
    hle.kFbA = fb ? fbA : 0;
    hle.kInsnAt = c1.insnCount;
    return 0;
}

void Machine::hleKernelCompare (Cpu& c1)
{
    hle.kPending = false;
    ++hle.kChecked;
    const auto* ram = soc.ram.ptr();
    char buf[200] {};
    if (std::memcmp (ram + (hle.kOutA - kRamBase), hle.kExpOut.data(), 256) != 0)
    {
        int s = 0;
        while (s < 64 && std::memcmp (ram + (hle.kOutA - kRamBase) + 4 * s, hle.kExpOut.data() + 4 * s, 4) == 0)
            ++s;
        uint32_t g, e;
        std::memcpy (&g, ram + (hle.kOutA - kRamBase) + 4 * s, 4);
        std::memcpy (&e, hle.kExpOut.data() + 4 * s, 4);
        std::snprintf (buf, sizeof buf, "kernel %d (0x%08x): salida[%d] emulada 0x%08x, nativa 0x%08x", hle.kIdx,
                       hle.kern[(size_t) hle.kIdx].hook, s, g, e);
    }
    else if (hle.kFbA != 0 && std::memcmp (ram + (hle.kFbA - kRamBase), hle.kExpFb.data(), 8) != 0)
        std::snprintf (buf, sizeof buf, "kernel %d (0x%08x): fb_buf", hle.kIdx, hle.kern[(size_t) hle.kIdx].hook);
    else
        for (int r = 0; r < 16 && buf[0] == 0; ++r)
            if (c1.regs[r] != hle.kExpRegs[r])
                std::snprintf (buf, sizeof buf, "kernel %d (0x%08x): r%d emulado 0x%08x, nativo 0x%08x", hle.kIdx,
                               hle.kern[(size_t) hle.kIdx].hook, r, c1.regs[r], hle.kExpRegs[r]);
    if (buf[0] != 0)
    {
        if (hle.kMismatches++ == 0)
            hle.kFirstMismatch = buf;
        if (hle.mode == Hle::Auto)
            hle.kOff = true;
        return;
    }
    // coste: lo emulado del gancho al epílogo (con una ISR de cpu1 por medio sería mayor: se queda el menor)
    const auto ran = (uint32_t) (c1.insnCount - hle.kInsnAt);
    auto& cost = hle.kCost[(size_t) hle.kIdx];
    cost = cost == 0 ? ran : std::min (cost, ran);
    if (hle.mode == Hle::Auto && hle.kChecked >= hle.kAutoChecks)
        hle.kOn = true;
}

bool Machine::hleEnter (Cpu& c1)
{
    uint8_t* ram = soc.ram.ptr();
    const auto ramSize = soc.ram.size();
    const auto word = [&] (uint32_t at) -> std::optional<uint32_t>
    {
        if (at < kRamBase || at - kRamBase + 4 > ramSize)
            return std::nullopt;
        uint32_t v;
        std::memcpy (&v, ram + (at - kRamBase), 4);
        return v;
    };
    const auto shift = word (c1.sp), n = word (c1.sp + 4);
    if (! shift || ! n)
        return false;
    HleRenderArgs a { c1.regs[0], c1.regs[1], c1.regs[2], c1.regs[3], *shift, *n, hle.site.algTable };
    ++hle.calls;
    if (hle.mode == Hle::On)
    {
        if (! hleFmCoreRender (ram, kRamBase, ramSize, a))
            return false;
        c1.pc = c1.rets;
        return true;
    }
    // Check: el nativo sobre la RAM, se guarda su resultado y se restaura lo que había; la rutina emulada sigue
    const auto z = hleZones (a);
    std::vector<uint8_t> before[4];
    for (int k = 0; k < 4; ++k)
    {
        if (z.at[k] < kRamBase || z.at[k] - kRamBase + z.len[k] > ramSize)
            return false;
        before[k].assign (ram + (z.at[k] - kRamBase), ram + (z.at[k] - kRamBase) + z.len[k]);
    }
    if (! hleFmCoreRender (ram, kRamBase, ramSize, a))
        return false;
    std::vector<uint8_t>* exp[4] = { &hle.expOut, &hle.expParams, &hle.expFb, &hle.expAlg };
    for (int k = 0; k < 4; ++k)
    {
        exp[k]->assign (ram + (z.at[k] - kRamBase), ram + (z.at[k] - kRamBase) + z.len[k]);
        std::memcpy (ram + (z.at[k] - kRamBase), before[k].data(), z.len[k]);
    }
    hle.args = a;
    hle.pending = true;
    hle.retPc = c1.rets;
    hle.retSp = c1.sp;
    hle.insnAtEntry = c1.insnCount;
    return false;  // la rutina emulada corre
}

void Machine::hleCompare (Cpu& c1)
{
    hle.pending = false;
    ++hle.checked;
    hle.emulatedInsns += c1.insnCount - hle.insnAtEntry;
    const auto z = hleZones (hle.args);
    const std::vector<uint8_t>* exp[4] = { &hle.expOut, &hle.expParams, &hle.expFb, &hle.expAlg };
    static const char* const names[4] = { "salida", "FmOpParams", "fb_buf", "tabla de algoritmos" };
    for (int k = 0; k < 4; ++k)
    {
        const uint8_t* got = soc.ram.ptr() + (z.at[k] - kRamBase);
        if (std::memcmp (got, exp[k]->data(), z.len[k]) == 0)
            continue;
        if (hle.mismatches++ == 0)
        {
            size_t i = 0;
            while (got[i] == (*exp[k])[i]) ++i;
            const size_t w = i & ~(size_t) 3;
            uint32_t g = 0, e = 0;
            std::memcpy (&g, got + w, 4);
            std::memcpy (&e, exp[k]->data() + w, 4);
            char buf[200];
            std::snprintf (buf, sizeof buf, "llamada %llu: %s, byte %zu (algoritmo %u, fb_shift %u): emulado 0x%08x, nativo 0x%08x",
                           (unsigned long long) hle.calls, names[k], i, hle.args.algo, hle.args.fbShift, g, e);
            hle.firstMismatch = buf;
        }
        break;
    }
    if (hle.mode == Hle::Auto)
    {
        if (hle.mismatches > 0)
            hle.mode = Hle::Off;
        else if (hle.checked >= hle.autoChecks)
            hle.mode = Hle::On;
    }
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

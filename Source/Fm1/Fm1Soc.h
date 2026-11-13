// El SoC del FM-1 (JieLi AC791N): mapa de memoria, periféricos y el ejecutor de los dos núcleos.
//
// Port a C++ de fm1-emu (Joakim Eriksson, licencia MIT, commit 3e3fb27), crates/fm1-soc/src/lib.rs y machine.rs, placa
// FM-1. Mapa de memoria (de fm1-emu: reconocimiento, HAL de Felucca y literales del corpus):
//   0x0200_0000  XIP de la flash (ventana de la aplicación): el código corre de aquí
//   0x01c0_0000  RAM de datos
//   0x0400_0000  RAM de overlay (512 KiB); 0x01f0_0000 bancos de SRAM (256 KiB)
//   0x5000_0-    puertos GPIO A..H; 0x5100_0 IOMAP; 0x1000_0 reloj; 0x1080_0/0x1090_0 TIMER4/TIMER5
//   0x11c0_0     SPI0 = flash NOR; 0x11d0_0 SPI1 = LCD; 0x11e0_0 SPI2 = registros de la matriz; 0x1180_0 USB
//   0x1210_0     UART1 (MIDI IN); 0x1310_0 ADC; 0x1eee000 controlador de interrupciones; 0x1eee240 DBG_MSG
// Política: un acceso a MMIO desconocido se registra y devuelve un valor inocuo; el registro es la hoja de ruta.
//
// Diferencias con fm1-emu en este port (de momento): solo la ruta de referencia (una instrucción cada vez, sin
// micro-ops, sin JIT ni salto de bucles de sondeo); sin USB ni las unidades DSP / EQ / SRC por hardware (se registran
// como accesos desconocidos); sin el modo estricto (caché de la flash, alineación, costes de ciclo); solo la placa FM-1.
#pragma once

#include "Fm1Cpu.h"
#include "Fm1Devices.h"
#include "Fm1Fwsc.h"
#include "Fm1Periph.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fm1
{
constexpr uint32_t kXipBase = 0x02000000, kRamBase = 0x01c00000, kOvlBase = 0x04000000, kSram2Base = 0x01f00000;
constexpr size_t kXipFlashOffset = 0x4000;  // la flash detrás de XIP 0x02000000 (el área de la aplicación empieza aquí)
constexpr uint32_t kBtWindowBase = 0x01c09e7c;  // el .bd_base de la V13 de fábrica (registro 0x2FC44 de la banda base BT)
constexpr size_t kRamSize = 1u << 20;           // la RAM de datos que usa fm1-live (RAM_SIZE)

struct UnknownAccess { uint32_t addr; uint8_t width; bool isWrite; uint32_t value; };

// En qué espera un núcleo: el cerrojo hardware, o un cerrojo de byte en una dirección
struct Wait { enum Kind : uint8_t { Lock, Byte } kind; uint32_t addr; bool operator== (const Wait&) const = default; };

class Soc : public Bus
{
public:
    Soc (size_t ramSize, std::vector<uint8_t> xip);

    // ---- Bus ----
    uint8_t read8 (uint32_t addr) override;
    uint16_t read16 (uint32_t addr) override;
    uint32_t read32 (uint32_t addr) override;
    uint32_t fetch32 (uint32_t addr) override;
    void write8 (uint32_t addr, uint8_t v) override;
    void write16 (uint32_t addr, uint16_t v) override;
    void write32 (uint32_t addr, uint32_t v) override;
    bool hwLock (uint32_t core, bool set) override;
    void spin (uint32_t core, uint32_t addr) override;
    void idle (uint32_t core) override;

    // ---- SoC ----
    void advance (uint64_t n);  // avanza el reloj de periféricos y saca de la RAM una mitad de audio acabada
    void sfcBoot (uint16_t key, size_t appEnd);
    void restoreFlash (const std::vector<uint8_t>& image);
    void takeBoot (const Cpu& cpu);
    bool resetDue() const noexcept { return resetReq.has_value(); }
    void chipReset (Cpu& cpu);
    bool corePaused (uint32_t core) const noexcept { return core < 2 && paused[core]; }
    bool stillWaits (uint32_t core, const Wait& w) const noexcept;
    void guardTrip (uint32_t bit);
    bool pcWindows (uint32_t out[2][2]);  // los límites de PC; false si el firmware no los ha puesto
    const std::vector<UnknownAccess>& unknownAccesses() const noexcept { return unknownLog; }
    std::string consoleString() const { return std::string (console.begin(), console.end()); }

    std::vector<uint8_t> xip;
    Ram ram, ovl, sram2;
    Lcd lcd;
    SpiFlash flash;
    Periph periph;
    P33 p33;
    std::vector<uint8_t> console;
    uint32_t dbgCause = 0, emuCause = 0;
    bool pcGuardDirty = false;
    bool lcdTimed = false;  // las transferencias del LCD tardan su tiempo en el cable (apagado por defecto, como fm1-emu)
    bool xipFlash = true;   // la ventana XIP sigue en el chip de flash más allá de la imagen
    bool cpu1Release = false, cpu1Idle = false, cpu1Survives = false, cpu1Running = false;
    std::optional<std::string> cpu1Fault;
    std::optional<Wait> waits[2];
    std::optional<uint32_t> lockOwner;
    uint32_t resets = 0;
    uint64_t ticks = 0;

private:
    enum class Region : uint8_t
    {
        Xip, Ram, Ovl, Gpio, Iomap, Clk, Timer, Spi0, Spi1, Usb, Uart1, Adc, Ana, P33, Bt, Spi2, Crc, Rand, XipFlash, Sfcenc,
        Dbg, Guard, Irqc, Irqc2, Alnk, HwDsp, HwEq, HwSrc, Sram2, Unknown
    };
    struct Boot
    {
        Cpu::PlainState cpu;
        std::vector<uint32_t> sfrLo;
        std::unordered_map<uint32_t, uint32_t> sfr;
        uint32_t sfcCon, sfcUnenc0, sfcUnenc1, btWindow;
        bool hasInfo = false;
        uint32_t infoAt = 0;
        std::vector<uint8_t> info;
    };

    Region region (uint32_t addr) const noexcept;
    uint32_t read32Slow (uint32_t addr);
    void write32Slow (uint32_t addr, uint32_t v);
    uint32_t sfrGet (uint32_t addr) const;
    void sfrSet (uint32_t addr, uint32_t v);
    void syncXip();
    bool sfcScrambled (size_t o) const noexcept;
    void xipBlock (size_t blk);
    void sfcReread();
    void p33Event();
    void raiseException();
    void busInvalid (uint32_t bit);
    void logUnknown (uint32_t addr, uint8_t width, bool isWrite, uint32_t value);
    void spi1Write (uint32_t addr, uint32_t v);
    void spi1Done (uint32_t bytes);
    void spi2Write (uint32_t addr, uint32_t v);
    uint32_t timerRead (uint32_t addr);
    std::optional<uint8_t> ramByte (uint32_t a) const noexcept;

    uint32_t btWindow = kBtWindowBase;
    std::vector<uint32_t> sfrLo;
    std::unordered_map<uint32_t, uint32_t> sfr;
    bool paused[2] { false, false };
    std::optional<uint16_t> sfcKey;
    uint32_t sfcCon = 0, sfcUnenc0 = 0, sfcUnenc1 = 0;
    std::vector<bool> sfcFromFlash;
    bool cpu1ReleasedOnce = false;
    uint16_t crc = 0;
    uint64_t rand = 0x9e3779b97f4a7c15ull;
    std::vector<UnknownAccess> unknownLog;
    std::optional<uint8_t> resetReq;
    std::unique_ptr<Boot> boot;
};

// ---- el firmware de un paquete .fwsc ----------------------------------------------------------------------------------
struct Stock
{
    std::vector<uint8_t> xip;    // contenido de la ventana XIP (el área de la aplicación descifrada)
    std::vector<uint8_t> flash;  // la flash completa (imagen tal cual, a 1 MiB, con banco USR por defecto si está vacío)
    uint32_t entry = 0;
    std::string identity;
    uint16_t key = 0;
    size_t appEnd = 0;
    std::optional<size_t> usr;
};

bool stockFromPackage (const std::vector<uint8_t>& pkg, const std::vector<uint8_t>* syx, Stock& out, std::string& error);
// Prepara el SoC para el firmware de un paquete: su flash y los parámetros de arranque del SPL en params. Devuelve r0.
uint32_t stockBoot (Soc& soc, const std::vector<uint8_t>& flash, uint32_t params);
uint32_t writeBootInfo (Soc& soc, uint32_t at, const std::vector<uint8_t>& flash);

// ---- el ejecutor (machine.rs, ruta de referencia) --------------------------------------------------------------------
constexpr uint32_t kSlice = 1024;  // cpu0 corre en tramos de estas instrucciones con cpu1 en marcha, cpu1 tras cada uno

class Machine
{
public:
    Machine (Soc& s, Cpu& c0) : soc (s), cpu0 (c0) {}

    // n instrucciones de cpu0 con cpu1 al lado una vez liberado. false y error si un núcleo se para en un fallo.
    bool run (uint32_t n, std::string& error);

    std::unique_ptr<Cpu> cpu1;

private:
    void cpu0Between();
    bool runBlocks (uint32_t n, uint32_t& done, std::string& error);  // el bucle de bloques de cpu0
    std::unique_ptr<Cpu> releasedCpu1();
    bool runCpu1 (uint64_t cycles, std::optional<Wait> until, uint64_t& ran, std::string& error);
    bool runCpu1On (uint64_t cycles, std::optional<Wait> until, uint64_t& ran, std::string& error);
    bool runDualFrom (uint32_t& done, uint32_t n, std::string& error);

    Soc& soc;
    Cpu& cpu0;
    bool pendingTick = false;
};
} // namespace fm1

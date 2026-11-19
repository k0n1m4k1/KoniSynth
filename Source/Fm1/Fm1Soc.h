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
// Diferencias con fm1-emu en este port (de momento): una instrucción cada vez, con micro-ops y salto exacto de bucles
// de sondeo pero sin bloques ni JIT; sin USB ni las unidades DSP / EQ / SRC por hardware (se registran como accesos
// desconocidos); sin el modo estricto (caché de la flash, alineación, costes de ciclo); solo la placa FM-1.
#pragma once

#include "Fm1Cpu.h"
#include "Fm1Devices.h"
#include "Fm1Fwsc.h"
#include "Fm1Hle.h"
#include "Fm1Periph.h"

#include <array>
#include <map>
#include <memory>
#include <tuple>
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
    void markCode (uint32_t lo, uint32_t hi) override
    {
        if (lo >= kRamBase && hi > lo && hi - kRamBase <= ram.size())
            ram.markCode (lo - kRamBase, hi - kRamBase);
    }
    uint64_t codeWrites() override { return ram.codeWrites; }
    bool ramView (RamView& v) override
    {
        v = { ram.ptr(), kRamBase, (uint32_t) ram.size(), ram.codeMarks(), (void* const*) &ram.log, &ram.codeWrites };
        return true;
    }
    bool xipView (XipView& v) override
    {
        v = { xip.data(), kXipBase, (uint32_t) xip.size() };
        return ! xip.empty();
    }

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
    // ¿Una carga de 32 bits en addr solo mira (sin efectos ni dependencia del reloj)? ¿Puede una vuelta de un bucle de
    // sondeo escribir en addr? (salto de bucles: pure_read y loop_write_ok de fm1-emu)
    bool pureRead (uint32_t addr) const noexcept;
    bool loopWriteOk (uint32_t addr) const noexcept;
    void clearResetReq() noexcept { resetReq.reset(); }
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
    // Accesos que pueden cambiar lo que el bucle de cpu0 mira entre instrucciones (MMIO por la ruta lenta, cerrojos,
    // espera de byte, idle): sin ellos ni un tick vencido, nada de eso cambia (las ráfagas de Machine::runBlocks)
    uint64_t sideEffects = 0;
    // Los accesos con efectos que paran la ráfaga de cpu0 (y su JIT): todos los de sideEffects salvo los que no pueden
    // cambiar nada de lo que la ráfaga deja de mirar (Machine::runBlocks: reset, interrupciones, cpu1, cerrojos, guardas,
    // el siguiente evento): las lecturas del P33, de CON/RES del ADC y de los GPIO, y las escrituras al ADC y a los GPIO
    // (salvo PD0, el chip select de la flash) que no piden reevaluar (Periph::nextDue intacto). Medido: DUB-1 y HYP-1
    // alimentan el perro guardián 117 000 y 181 000 veces por segundo, ~24 accesos al P33 cada vez, y sondean el ADC
    uint64_t stopEffects = 0;
    // (KoniFm1Run --ioprofile) accesos con efectos por dirección & ~3 | 1 si escribe; idle, spin y hwLock como 0xfffffff0,
    // 0xfffffff4 y 0xfffffff8. nullptr: sin contar
    std::unordered_map<uint32_t, uint64_t>* ioHist = nullptr;
    uint64_t ticks = 0;
    // (Machine, vuelta observada) con observing: lecturas del contador del TIMER4 y accesos que una vuelta saltada no
    // repetiría (MMIO con efectos, escrituras fuera de la RAM, cerrojos, idle); advances: veces que se atendió el SoC
    bool observing = false;
    uint64_t obsT4 = 0, obsImpure = 0, advances = 0;

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
    bool fastPath = true;  // la ruta rápida de la CPU también para cpu1 (false: la referencia)
    std::unordered_map<const Fm1IsaEntry*, uint64_t>* synHist1 = nullptr;  // perfil de exec_syntax de cpu1 (Cpu::synHist)
    std::unordered_map<uint32_t, uint64_t>* pcHist1 = nullptr;              // perfil por pc de cpu1 (Cpu::pcHist)

    // Salto exacto de los bucles de sondeo de cpu0 (LoopSkip de fm1-emu): una vuelta que deja la máquina como la
    // encontró se repite igual hasta el siguiente evento temporizado; se saltan las vueltas enteras que caben, al mismo
    // ciclo. Un sondeo que no encuentra vuelta lo deshace todo. Con enabled = false el resultado es el mismo
    struct LoopSkip
    {
        bool enabled = true;
        uint64_t nextTry = 0;                     // sin sondeos hasta este ciclo (un evento cerca)
        uint64_t backoff = 0, backoffUntil = 0;   // espera creciente tras un sondeo fallido (como fm1-emu)
        // pc donde un sondeo encontró vuelta (mapeo directo de 64): ahí se sondea aunque haya espera y con poco tramo por
        // delante. fm1-emu no lo tiene: con cpu1 en marcha, los fallos en otro código (ISR...) y el tramo de 1024 dejaban el
        // bucle de espera de 6 instrucciones de la V15 sin saltar: 140 M de sus 150 M instrucciones en 2,6 s (medido)
        std::array<uint32_t, 64> loops;
        bool hasLoops = false;  // alguna entrada en loops (Cpu::BurstStop::anyLoops)
        uint64_t loopsMask = 0;  // sus entradas ocupadas (bit = (pc >> 1) & 63)
        LoopSkip() { loops.fill (0xffffffffu); }
        uint32_t& loopAt (uint32_t pc) noexcept { return loops[(pc >> 1) & 63]; }
        int64_t credit = 1 << 16;
        uint64_t creditAt = 0;
        uint64_t skipped = 0, probes = 0, hits = 0;  // ciclos (= instrucciones) saltados, sondeos, sondeos con bucle
        uint64_t counted = 0, countedSkipped = 0;  // saltos de bucles de retardo contados (Cpu::skipCountedLoop) y sus instrucciones
        // lo más que un sondeo sigue a cpu0 buscando una vuelta: 8192 deja ver dos vueltas seguidas de la espera de trama de
        // Felucca (2091 instrucciones; con contador hacen falta dos para ver el periodo). Medido: 0,48x -> 0,74x en Felucca 1.8.2
        uint32_t probeMax = 8192;
        uint32_t forcePc = 0xffffffffu;  // (diagnóstico) sondear siempre en este pc
        // por qué falló cada sondeo: un acceso que no se puede repetir, sin vuelta en probeMax, un evento, una interrupción
        uint64_t failBad = 0, failNoRound = 0, failEvent = 0, failIrq = 0;
        std::array<uint64_t, 5> roomHist {};  // ciclos hasta el siguiente evento al empezar cada sondeo: <2k <8k <32k <128k más
        // (diagnóstico) vueltas encontradas: (pc inicial, longitud, lecturas del contador) -> (veces, vueltas saltadas)
        std::map<std::tuple<uint32_t, uint32_t, uint32_t>, std::pair<uint64_t, uint64_t>>* hitsAt = nullptr;
        std::unordered_map<uint32_t, uint64_t>* badAt = nullptr;  // (diagnóstico) sondeos malos por dirección del acceso
        // (diagnóstico) sondeos por pc inicial y resultado: pc << 2 | 0 vuelta, 1 sin vuelta o un acceso, 2 un evento, 3 una
        // interrupción
        std::unordered_map<uint64_t, uint64_t>* probeAt = nullptr;
        struct Visit
        {
            Cpu::PlainState core; P33::Snap p33; Periph::AdcState adc; uint32_t at; size_t log;
            uint64_t tReads, cyc, due, wdt, lastCnt, side; uint32_t streak;  // lo necesario para repetir la vuelta en sombra
        };
        // Vueltas que leen el contador del TIMER4 (0x10804; p. ej. la espera de trama de Felucca, que lee fm1_ticks() en cada
        // pasada de ui_input). El valor leído deja restos distintos en cada vuelta (registros, banderas, palabras de RAM: los
        // "muertos"), así que valen si:
        //   - son un periodo: dos vueltas seguidas con el mismo recorrido de pc (un pc visitado varias veces por periodo no
        //     cuenta; medido: el tramo de 2016 instrucciones entre dos llamadas de Felucca seguido de uno de 75);
        //   - A, en sombra: con los muertos alterados al empezar, la vuelta acaba bit a bit como la real (están muertos);
        //   - B, en sombra: con el contador desplazado 2^31, mismo recorrido, mismas direcciones escritas y mismo estado salvo
        //     los muertos y la pila muerta (el contador solo llega a ellos).
        // Veredicto en caché por firma de la vuelta. Se salta una vuelta menos de las que caben: la última, ejecutada de
        // verdad antes del siguiente evento, reescribe los muertos con lo que les toca, y el estado queda como sin salto
        // (comprobado: hash del estado final igual al de --reference 1)
        bool timerOk = true;
        bool timerSkip = true;  // (diagnóstico) false: se comprueba en sombra pero no se salta
        bool waitOk = true;     // esperas con el contador (Machine::skipTimerWait)
        uint64_t waitHits = 0, waitSkipped = 0;  // sus saltos y sus instrucciones
        // Rechazos seguidos por cabeza, (pc >> 1) & 4095: una cabeza pasa a Cpu::notCounted tras kWaitTries sin un salto en
        // medio. Una espera no siempre se puede saltar (la de 11 instrucciones de Tessera, 0x0202293c, falla ~1 de cada 6
        // visitas): con el primer rechazo ya no se volvía a mirar (11 saltos en 4 s; con reintentos, ~46 000)
        static constexpr uint32_t kWaitTries = 8, kWaitSlots = 4096;
        std::array<uint32_t, kWaitSlots> waitPc {};
        std::array<uint8_t, kWaitSlots> waitBad {};
        std::array<uint8_t, kWaitSlots> waitLen {};  // instrucciones de su última vuelta (0: sin medir)
        bool waitGiveUp (uint32_t pc) noexcept  // un rechazo más: ¿se deja de mirar?
        {
            const auto i = (pc >> 1) & (kWaitSlots - 1);
            if (waitPc[i] != pc)
            {
                waitPc[i] = pc;
                waitBad[i] = 0;
                waitLen[i] = 0;
            }
            return ++waitBad[i] >= kWaitTries;
        }
        void waitGood (uint32_t pc) noexcept
        {
            if (const auto i = (pc >> 1) & (kWaitSlots - 1); waitPc[i] == pc)
                waitBad[i] = 0;
        }
        // (diagnóstico) intentos rechazados: 0 estado, 1 fuera de ISR con skipIdle, 2 la vuelta, 3 sin lecturas del contador,
        // 4 lee restos, 5 sin sitio
        std::array<uint64_t, 6> waitFail {};
        uint64_t tReads = 0;  // lecturas del contador en el sondeo en curso
        uint64_t timerHits = 0, timerVerified = 0, timerRejected = 0;  // saltos con contador, vueltas comprobadas y rechazadas
        std::unordered_map<uint64_t, int8_t> verdict;
        std::vector<std::pair<uint32_t, uint32_t>> altLog, realEnd;
        std::vector<uint32_t> deadWords, realWords;  // (sombra) palabras que la vuelta deja distintas; las que escribe
        std::vector<std::pair<uint32_t, uint32_t>> log, scratch;
        std::vector<Visit> seen;
        std::vector<uint32_t> pcs;  // el pc de cada instrucción del sondeo en curso
        bool mayProbe (uint64_t now, uint32_t pc, uint32_t room);  // room: instrucciones del tramo por delante
        void backOff (uint64_t now);
    } loopSkip;

    // HLE de cpu1 (Fm1Hle.h): FmCore::render nativo en lugar del emulado. Check: el nativo calcula aparte (la RAM se
    // restaura), el firmware ejecuta su código y a la vuelta se comparan salida, FmOpParams, fb_buf y tabla de algoritmos
    // bit a bit. On: el nativo sustituye a la rutina y cpu1 descuenta del tramo las instrucciones medias de la emulada.
    // Auto (el plugin): Check durante las primeras autoChecks llamadas; sin ninguna diferencia pasa a On, con una a Off
    // (un firmware con el mismo prólogo y otra semántica se queda emulado)
    struct Hle
    {
        enum Mode : uint8_t { Off, Check, On, Auto };
        Mode mode = Off;
        uint32_t autoChecks = 4000;
        HleRenderSite site;           // se busca en la RAM cuando cpu1 ya corre (el firmware copia ahí su código caliente)
        uint32_t searchIn = 0;        // tramos de cpu1 hasta volver a buscar
        uint64_t calls = 0, mismatches = 0, emulatedInsns = 0, checked = 0;
        uint32_t costPerCall = 8700;  // instrucciones que se descuentan en On: media de la emulada (Check, V13/V15: 8659-8976)
        std::string firstMismatch;
        // comprobación en curso
        bool pending = false;
        uint32_t retPc = 0, retSp = 0;
        uint64_t insnAtEntry = 0;
        HleRenderArgs args;
        std::vector<uint8_t> expOut, expParams, expFb, expAlg;
        double avgCost() const noexcept { return checked > 0 ? (double) emulatedInsns / (double) checked : 0.0; }
        // V16: los kernels de operador (findV16Kernels), cada uno con su coste medido en Check (instrucciones emuladas del
        // gancho al epílogo; sin medir, se comprueba antes de usarlo). Auto: nativo tras kAutoChecks llamadas comprobadas
        // sin diferencias (salida, fb_buf, los 16 registros); con una, apagado
        static constexpr int kMaxKernels = 8;
        std::array<HleKernel, kMaxKernels> kern {};
        int nKern = 0;
        uint32_t kMin = 0xffffffffu, kMax = 0;
        std::array<uint32_t, kMaxKernels> kCost {};
        uint32_t kAutoChecks = 300;
        bool kOn = false, kOff = false;
        uint64_t kCalls = 0, kNative = 0, kChecked = 0, kMismatches = 0;
        std::string kFirstMismatch;
        bool kPending = false;
        int kIdx = 0;
        uint32_t kExit = 0, kSp = 0, kOutA = 0, kFbA = 0;
        uint64_t kInsnAt = 0;
        uint32_t kExpRegs[16] {};
        std::array<uint8_t, 256> kExpOut {};
        std::array<uint8_t, 8> kExpFb {};
    } hle;

    // HLE de cpu0 para GHOULBOX (firmware de la comunidad): la pasada vacía de midi_cc_apply (su ui_input.c), el sondeo
    // de NTRK x NCC colas de CC MIDI que su bucle principal repite sin pausa, cada cola leída y borrada entre una guarda y
    // su restauración de icfg. Con todas las colas a 0, sin interrupción pendiente ni evento de periférico antes de su
    // final y sin cpu1, la pasada emulada no depende del tiempo: deja r0 r1 r2 r8 r9 r11 r12 r15, rets, banderas e icfg con
    // valores que se calculan aquí, y la RAM igual. Se salta entera al mismo ciclo (exacto, como un bucle contado).
    // Se localiza por su código (ccPollFind); si no aparece, no hace nada. Check: se predice el estado al salir y se
    // compara con el de la pasada emulada. Auto: Check durante autoChecks pasadas; sin diferencias, On; con una, Off
    struct CcPoll
    {
        enum Mode : uint8_t { Off, Check, On, Auto };
        Mode mode = Off;
        uint32_t autoChecks = 200;
        bool searched = false;
        uint32_t entry = 0, exit = 0;  // cabeza del bucle de pistas y primera instrucción tras él
        uint32_t stride = 0, offset = 0, trkBase = 0, ncc = 0, ntrk = 0, insns = 0;  // sacados del código
        uint32_t guardRet = 0;         // rets que deja la llamada a la guarda
        uint64_t skipped = 0, checked = 0, mismatches = 0;
        std::string firstMismatch;
        bool pending = false;
        uint64_t insnAtEntry = 0, cycAtEntry = 0, dueAtEntry = 0;
        uint32_t expRegs[16] {}, expRets = 0, expIcfg = 0, expFlags = 0;
        bool expIe = false;
    } ccPoll;

    // Vueltas con el contador del TIMER4 ya comprobadas en sombra (LoopSkip::timerOk): en lugar de sondearlas (ruta lenta,
    // instrucción a instrucción), se ejecutan de verdad por la ruta rápida apuntando sus escrituras a RAM (Ram::log), sus
    // lecturas del contador y cualquier acceso con efectos. De vuelta en su pc: misma longitud, mismas lecturas, mismas
    // escrituras (número y firma de direcciones), ningún evento ni acceso con efectos en medio y el mismo estado salvo los
    // registros muertos y la pila muerta. Entonces se saltan las vueltas que caben menos una: la siguiente, también
    // observada, es la vuelta real que deja esos registros y esa pila como sin salto
    struct Observe
    {
        struct Known
        {
            uint32_t pc0, len, reads, writes; uint64_t addrHash; uint32_t deadRegs; bool deadFlags;
            std::vector<uint32_t> deadWords;  // palabras de RAM (desplazamiento, ordenadas) que la vuelta deja distintas
        };
        std::vector<Known> known;
        bool enabled = true;
        bool active = false;
        size_t idx = 0;
        Cpu::PlainState start {};
        P33::Snap p33 {};
        Periph::AdcState adc {};
        uint64_t insn0 = 0, t40 = 0, impure0 = 0, adv0 = 0;
        std::vector<std::pair<uint32_t, uint32_t>> log;
        uint64_t rounds = 0, hits = 0, skipped = 0, misses = 0;
        uint32_t pcAt (uint32_t pc) const noexcept
        {
            for (size_t i = 0; i < known.size(); ++i)
                if (known[i].pc0 == pc) return (uint32_t) i;
            return 0xffffffffu;
        }
    } observe;

    // reparto de las instrucciones de cpu0 (medida del intérprete): por la ruta de arriba de runBlocks, de ellas con una
    // interrupción pendiente (anyPending), y en ráfaga
    uint64_t statTop = 0, statTopPending = 0, statBurst = 0;

private:
    void ccPollFind();
    bool ccPollEnter (uint32_t room);  // en la cabeza: true = pasada saltada (On) y pc ya en exit
    void ccPollCompare();              // en exit tras la pasada emulada en Check
    bool hleEnter (Cpu& c1);     // en la entrada de la rutina: true = la ejecutó el nativo (On) y cpu1 ya ha vuelto
    void hleCompare (Cpu& c1);   // de vuelta de la rutina emulada en Check
    uint32_t hleKernelEnter (Cpu& c1);  // V16, en el gancho de un kernel: instrucciones que ahorra (0 = se emula)
    void hleKernelCompare (Cpu& c1);    // en su epílogo tras el emulado en Check
    uint32_t probeLoop (uint32_t budget);
    bool irqWouldEnter() const noexcept;
    // Espera con el contador del TIMER4 en pc (p. ej. "do t = TMR4_CNT - t0; while (t < n)"), también dentro de una ISR:
    // vueltas enteras que siguen igual, exacto. Devuelve las instrucciones saltadas (0: nada ahora; -1: no tiene esa forma)
    int64_t skipTimerWait (uint32_t room);
    // 1: vuelve a la cabeza; 0: sale del bucle (el pc deja la zona de la cabeza a cabeza + 64 B); -1: otra cosa
    int waitRound (const Cpu::RoundState& s, uint64_t cyc, bool record, uint32_t& reads);
    std::vector<uint32_t> waitPath;  // los pc de la vuelta (skipTimerWait)
    uint32_t waitWritten = 0;        // los registros que escriben sus instrucciones
    void observeStop();
    uint64_t observeEnd (uint32_t budget);  // en su pc con la vuelta observada acabada: instrucciones saltadas (0 = ninguna)
    bool probeTimerAlt (const LoopSkip::Visit& v, uint32_t roundLen, uint64_t roundReads);  // ver LoopSkip::timerOk
    bool probeShadow (const LoopSkip::Visit& v, uint32_t roundLen, uint64_t roundReads, bool perturbDead);
    bool burstStop (uint32_t room) const noexcept;
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

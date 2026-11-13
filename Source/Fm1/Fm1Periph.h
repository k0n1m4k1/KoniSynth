// Periféricos temporizados y panel frontal del FM-1.
//
// Port a C++ de fm1-emu (Joakim Eriksson, licencia MIT, commit 3e3fb27), crates/fm1-soc/src/periph.rs, solo la placa
// FM-1 (el SMK-37 queda fuera). Modelo tomado por fm1-emu de la HAL de Felucca y de la documentación de FM-1-RE:
//  - un contador de ciclos (hz, una instrucción por ciclo; 240 MHz como el chip) marca el paso y los temporizadores
//    convierten ciclos en tiempo real;
//  - TIMER4 0x10800: base de tiempo libre de 24 MHz (CNT en +4); TIMER5 0x10900: tick de 10 kHz (PRD a 6 MHz), IRQ 63;
//    TIMER0..3 0x10400 + 0x100·n, IRQ 4 + n;
//  - ALNK0 0x12E00: salida I2S, DMA doble de LEN palabras int32 por mitad (L/R, 24 bits en los bits bajos) a 24 MHz / 544
//    = 44 117,65 Hz por trama; una mitad acabada pone pendientes (CON2), CON0 bit 15 = la mitad que suena; IRQ 11;
//  - SARADC 0x13100: CON (inicio bit 6, hecho bit 7, canal bits 8-11), RES el valor de 10 bits; MASTER en el canal 4,
//    batería en el 3; las filas PA7 / PA8 (canales 0 / 1) leen la fuerza de las teclas (las píldoras de carbón);
//  - controlador de interrupciones: nibbles de habilitación/prioridad en 0x1EEF100 (cpu0) y 0x1EEF300 (cpu1); latch por
//    software en 0x1EEF1A0/1A4 (bit b = IRQ 120 + b); tabla de vectores en RAM 0x01C7FE00;
//  - el tick del SO en 0x1EEF0EC (10 ms), IRQ 3;
//  - UART1 0x12100 (MIDI IN, el jack TRS): bytes a 31 250 baudios (320 us) por DMA a un anillo; IRQ 20;
//  - la matriz de teclas y encoders: 11 columnas de dos 74HC595 (PA4 SER, PA3 SRCLK, PA1 RCLK), filas PA0, PA5-PA8, PB7.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace fm1
{
constexpr uint64_t kCpuHz = 240'000'000;
constexpr uint64_t kNever = UINT64_MAX;
constexpr uint8_t kIrqAlnk0 = 11, kIrqTimer5 = 63, kIrqOsTick = 3, kIrqSpi1 = 16, kIrqSaradc = 24, kIrqSpi2 = 37, kIrqUart1 = 20;
constexpr uint32_t kVecBase = 0x01c7fe00;

// Una acción sobre el panel o el jack MIDI
struct PanelAction
{
    enum Kind : uint8_t { Key, Enc, Adc, Midi, KeyVel, Pressure } kind;
    uint8_t id = 0;     // tecla, encoder, canal de ADC
    int32_t value = 0;  // pulsada (1/0), clics (+ = horario), valor de ADC, byte MIDI, velocidad 1..127
};

// Lo que vence al avanzar: el llamante mueve la mitad de audio fuera de la RAM (y el byte de la UART dentro)
struct PeriphDue
{
    std::optional<std::pair<uint32_t, uint32_t>> audioHalf;  // (dirección en RAM, palabras)
    std::optional<std::pair<uint32_t, uint8_t>> uartRx;      // (dirección, byte) que escribió el DMA
    bool usbFrame = false;
    bool wdt = false;
};

class Periph
{
public:
    Periph();

    uint64_t ms() const noexcept { return cycles * 1000 / hz; }
    uint64_t cycleOfMs (uint64_t m) const noexcept { return m * hz / 1000; }
    // Tramas I2S que ha contado el reloj del DAC desde el arranque: 24 MHz / 544 por segundo, sea cual sea el reloj de CPU
    static uint64_t audioFramesAt (uint64_t cycles, uint64_t hz) noexcept;

    PeriphDue advance (uint64_t n);
    void chipReset();  // el estado propio del chip como al encender; lo de fuera (reloj, teclas, cola MIDI...) se queda
    void apply (const PanelAction& a);
    bool tick() noexcept { ++cycles; return cycles >= nextDue; }
    uint64_t tickRoom() const noexcept { return std::min (nextDue > cycles + 1 ? nextDue - (cycles + 1) : 0, roomCap); }
    bool nextTickDue() const noexcept { return cycles + 1 >= nextDue; }
    bool anyPending() const noexcept;
    bool irqEnabled (uint32_t core, uint8_t n) const noexcept;
    std::optional<std::pair<uint8_t, uint8_t>> pendingIrqFor (uint32_t core) const noexcept;  // (irq, prioridad)

    std::optional<uint32_t> read (uint32_t addr);  // nullopt = SFR de almacenamiento simple
    bool write (uint32_t addr, uint32_t v);         // false = SFR de almacenamiento simple

    std::array<float, 41> ledPeek();
    std::array<float, 41> ledTake();
    void idle();
    void spi1Start (uint32_t bytes, uint32_t baud, bool irq);
    void spi1IrqOff() noexcept { spi1Pending = false; spi1IrqAt = kNever; }
    void spi2Start (uint32_t bytes, uint32_t baud, bool irq);
    void srShiftByte (uint8_t b) noexcept;
    size_t midiQueued() const noexcept { return uart.q.size(); }
    uint64_t midiDropped() const noexcept { return uart.dropped; }

    // ---- estado público (el SoC y el ejecutor lo usan) ----
    uint64_t cycles = 0;
    uint64_t hz = kCpuHz;
    uint64_t cycleTicks = 1;
    bool skipIdle = false;
    bool inIsr = false;
    uint64_t nextDue = 0;
    uint64_t roomCap = kNever;
    uint64_t skipped = 0;
    std::vector<int16_t> audio;  // muestras L/R intercaladas de 16 bits reproducidas
    uint64_t audioHalves = 0;
    std::array<uint16_t, 16> adcValue {};
    uint32_t ilat = 0;
    bool excPending = false;
    std::array<uint32_t, 32> icfg {}, icfg1 {};
    bool spi1Pending = false;
    uint64_t spi1DoneAt = 0;
    uint64_t spi1Wire = 0;
    bool spi2Pending = false;
    uint64_t usbNext = kNever, usbPeriod = 0;
    uint64_t wdtAt = kNever;
    std::array<bool, 41> keysDown {};
    uint8_t keyVel = 100;
    std::deque<std::pair<uint64_t, PanelAction>> script;  // acciones programadas: (ciclo, acción)

private:
    friend struct PeriphTest;  // las pruebas (Bench/Fm1Tests.cpp) leen el estado interno

    struct Uart
    {
        uint32_t con0 = 0, rxsadr = 0, rxeadr = 0, rxcnt = 0, otcnt = 0, hrxcnt = 0, since = 0, wr = 0;
        bool rpnd = false, otpnd = false, tpnd = false;
        std::deque<uint8_t> q;
        uint64_t next = kNever, otNext = kNever, dropped = 0;
    };
    struct Tmr { uint32_t con = 0, prd = 0, cnt0 = 0; uint64_t start = 0, next = kNever; bool pending = false; };
    struct Ramp
    {
        float from = 0, to = 0;
        uint64_t t0 = 0, dur = 0;
        float at (uint64_t t) const noexcept;
    };

    uint64_t t5Period() const noexcept;
    uint64_t tmrCyclesPerCount (int i) const noexcept;
    void tmrArm (int i);
    uint64_t tickPeriod() const noexcept { return std::max<uint64_t> (hz / 100, 1); }
    uint64_t halfPeriod() const noexcept;
    uint64_t encStep() const noexcept { return hz / 500; }
    uint64_t nextEvent() const noexcept;
    uint64_t midiByteTime() const noexcept;
    std::optional<std::pair<uint32_t, uint8_t>> uartReceive();
    void uartUpdate() noexcept;
    void ledCredit();
    void ledFlush();
    void paWrite (uint32_t v) noexcept;
    uint64_t msCycles (double m) const noexcept { return (uint64_t) ((double) hz * m / 1000.0); }
    std::optional<uint16_t> rowReading (uint32_t ch);
    uint32_t closedRows() const noexcept;
    uint32_t paIn() const noexcept;
    uint32_t pbIn() const noexcept;

    uint64_t lastCntRead = 0;
    uint32_t cntStreak = 0;
    uint32_t t4Con = 0;
    uint64_t t4Base = 0;
    uint32_t t5Con = 0, t5Prd = 0;
    uint64_t t5Next = kNever;
    bool t5Pending = false;
    uint32_t alnkCon0 = 0, alnkCon1 = 0, alnkCon3 = 0, alnkPend = 0, alnkAdr = 0, alnkLen = 0, alnkHalf = 0;
    uint64_t alnkNext = kNever;
    uint32_t adcCon = 0;
    uint32_t tickCtl = 0, tickPrd = 0;
    uint64_t tickStart = 0, tickNext = kNever;
    bool tickPending = false;
    uint64_t spi1IrqAt = kNever;
    bool adcDone = false, adcIrq = false;
    uint64_t spi2Next = kNever;
    std::array<Tmr, 4> tmr {};
    Uart uart;
    bool uartIrq = false;
    uint16_t srShift = 0xffff, srLatched = 0xffff;
    uint32_t paOut = 0, phOut = 0;
    std::array<uint64_t, 41> ledAcc {};
    std::pair<uint32_t, uint16_t> ledKey {};
    uint64_t ledPend = 0, ledLast = 0, ledSince = 0;
    std::array<Ramp, 41> keyForce {};
    uint32_t paDir = 0, paPu = 0, paHd0 = 0, paHd = 0;
    uint32_t adcNoise = 1;
    std::array<uint8_t, 8> encState {};
    std::array<int32_t, 8> encPending {};
    std::array<uint64_t, 8> encNext {};
    uint16_t adcSample = 0;
};

// Nombres del panel de Felucca (panel.c) a ids de la matriz: botones 0..13, teclas de nota 14..40 (F3..G5)
std::optional<uint8_t> fm1KeyId (const char* name);
std::optional<uint8_t> fm1EncId (const char* name);
} // namespace fm1

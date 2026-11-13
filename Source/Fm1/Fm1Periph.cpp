// Port a C++ de fm1-emu, crates/fm1-soc/src/periph.rs (MIT), placa FM-1. Ver Fm1Periph.h.
#include "Fm1Periph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace fm1
{
namespace
{
constexpr uint64_t kT4Hz = 24'000'000;   // TIMER4 cuenta el cristal de 24 MHz
constexpr uint64_t kT5Hz = 6'000'000;    // TIMER5 va de OSC/4
constexpr uint64_t kFrameT4 = 544;       // ticks de TIMER4 por trama I2S (DAC_TICKS de Felucca)
constexpr uint64_t kUartHz = 48'000'000; // reloj de la UART (PLL48M; OTCNT lo cuenta)
constexpr uint64_t kMidiByteBits = 10, kMidiBaud = 31'250;
constexpr size_t kMidiQueueMax = 3125;   // ~1 s de MIDI a 31 250 baudios

// La tabla de prescalers de JL_TIMER: CON bits 4-7 la indexan
constexpr uint64_t kTmrDiv[16] = { 1, 4, 16, 64, 2, 8, 32, 128, 256, 1024, 4096, 16384, 512, 2048, 8192, 32768 };

// Id de tecla en (bit de fila, columna física), -1 = ninguna (FM1_KEYMAP de hal/fm1_input.h de Felucca)
constexpr int8_t kKeymap[6][11] = {
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
    { 5, 11, 4, 10, 3, 9, 2, 8, -1, -1, -1 },
    { 34, 35, 36, 37, 38, 40, 39, 13, 7, 6, 12 },
    { 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33 },
    { 0, 1, 15, 14, 17, 16, 19, 18, 20, 21, 22 },
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
};
// Encoder i: contacto A en (columna, fila), B en (columna, fila) (FM1_ENC)
constexpr uint8_t kEnc[7][4] = { { 0, 0, 1, 0 }, { 2, 0, 3, 0 }, { 8, 1, 9, 1 }, { 8, 0, 9, 0 }, { 6, 0, 7, 0 }, { 4, 0, 5, 0 }, { 0, 5, 1, 5 } };
// Estados de cuadratura en orden de giro: (A, B)
constexpr uint8_t kGray[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

// a·b/c como u32: en 128 bits si el producto no cabe en 64 (el cociente es el mismo)
uint32_t mulDiv (uint64_t a, uint64_t b, uint64_t c) noexcept
{
    if (b == 0 || a <= UINT64_MAX / b)
        return (uint32_t) (a * b / c);
#if defined(__SIZEOF_INT128__)
    return (uint32_t) ((unsigned __int128) a * b / c);
#else
    // a·b/c = (a/c)·b + (a%c)·b/c, sin desbordar mientras (a%c)·b quepa (c <= 2^32 aquí)
    return (uint32_t) ((a / c) * b + (a % c) * b / c);
#endif
}

// Fuerza con que aterriza una tecla a velocidad vel (1..127)
float velForce (int vel) noexcept { return 0.33f + 0.67f * (float) std::clamp (vel, 1, 127) / 127.0f; }

// Lectura de ADC de una fila con una tecla de fuerza f (0: abierta) frente a la resistencia de fuente rs, en cuentas.
// Modelo de fm1-emu calibrado con las medidas de CS-1 en un FM-1 (píldora ~100 ohm a fondo, divisor con el diodo y la
// salida baja del 595 = 186 cuentas)
float pillReading (float f, float rs) noexcept
{
    if (f <= 0.001f)
        return 1023.0f;
    const float r = 100.0f * std::pow (std::min (f, 1.0f), -3.06f);
    return 186.0f + 837.0f * r / (r + rs);
}

int signum (int32_t x) noexcept { return (x > 0) - (x < 0); }
} // namespace

float Periph::Ramp::at (uint64_t t) const noexcept
{
    if (dur == 0 || t >= t0 + dur)
        return to;
    const float x = (float) (t > t0 ? t - t0 : 0) / (float) dur;
    return from + (to - from) * x;
}

Periph::Periph()
{
    adcValue.fill (512);
    adcValue[3] = 640;  // batería llena (ui_draw.c: >= 591 son 3 barras)
    adcValue[4] = 900;  // potenciómetro MASTER
}

uint64_t Periph::audioFramesAt (uint64_t c, uint64_t h) noexcept
{
#if defined(__SIZEOF_INT128__)
    return (uint64_t) ((unsigned __int128) c * kT4Hz / ((unsigned __int128) h * kFrameT4));
#else
    // c·24e6 / (h·544): c / h en segundos de máquina; se reparte para no desbordar
    const uint64_t q = c / h, r = c % h;
    return q * kT4Hz / kFrameT4 + (uint64_t) ((long double) r * kT4Hz / ((long double) h * kFrameT4));
#endif
}

uint64_t Periph::t5Period() const noexcept { return std::max<uint64_t> ((uint64_t) std::max<uint32_t> (t5Prd, 1) * hz / kT5Hz, 1); }

uint64_t Periph::tmrCyclesPerCount (int i) const noexcept { return kTmrDiv[(tmr[(size_t) i].con >> 4) & 15] * hz / kT4Hz; }

void Periph::tmrArm (int i)
{
    auto& t = tmr[(size_t) i];
    if ((t.con & 3) == 0 || t.prd == 0)
    {
        t.next = kNever;
        return;
    }
    const uint64_t left = std::max<uint32_t> (t.prd > t.cnt0 ? t.prd - t.cnt0 : 0, 1);
    t.start = cycles;
    t.next = cycles + left * std::max<uint64_t> (tmrCyclesPerCount (i), 1);
}

uint64_t Periph::halfPeriod() const noexcept
{
    return std::max<uint64_t> ((uint64_t) std::max<uint32_t> (alnkLen, 2) / 2 * kFrameT4 * hz / kT4Hz, 1);
}

uint64_t Periph::nextEvent() const noexcept
{
    uint64_t n = std::min ({ t5Next, alnkNext, tickNext, spi2Next, kNever, spi1IrqAt, uart.next, uart.otNext, usbNext, wdtAt });
    for (const auto& t : tmr)
        n = std::min (n, t.next);
    if (! script.empty())
        n = std::min (n, script.front().first);
    for (size_t i = 0; i < 8; ++i)
        if (encPending[i] != 0)
            n = std::min (n, encNext[i]);
    return n;
}

PeriphDue Periph::advance (uint64_t n)
{
    cycles += n;
    PeriphDue due;
    if (cycles < nextDue)
        return due;
    if (cycles >= t5Next)
    {
        t5Pending = true;
        const auto p = t5Period();
        t5Next += p;
        if (t5Next <= cycles) t5Next = cycles + p;
    }
    if (cycles >= spi2Next) { spi2Pending = true; spi2Next = kNever; }
    if (cycles >= spi1IrqAt) { spi1IrqAt = kNever; spi1Pending = true; }
    for (int i = 0; i < 4; ++i)
    {
        auto& t = tmr[(size_t) i];
        if (cycles >= t.next)
        {
            t.pending = true;
            t.cnt0 = 0;
            const auto per = (uint64_t) std::max<uint32_t> (t.prd, 1) * std::max<uint64_t> (tmrCyclesPerCount (i), 1);
            t.start = t.next;
            t.next += per;
            if (t.next <= cycles) t.next = cycles + per;
        }
    }
    if (cycles >= tickNext)
    {
        tickPending = true;
        tickStart = tickNext;
        tickNext += tickPeriod();
        if (tickNext <= cycles) tickNext = cycles + tickPeriod();
    }
    if (cycles >= alnkNext)
    {
        const auto words = std::max<uint32_t> (alnkLen, 2);
        due.audioHalf = std::make_pair (alnkAdr + alnkHalf * words * 4, words);
        alnkHalf ^= 1;
        // cada canal que CON1 configura queda pendiente con el bloque: FM1 Open toma su interrupción del canal de
        // recepción 0 (bit 4), el de fábrica del canal de transmisión 3 (bit 7)
        for (int ch = 0; ch < 4; ++ch)
            if (((alnkCon1 >> (4 * ch)) & 0xf) != 0)
                alnkPend |= 0x10u << ch;
        alnkPend |= 0x80;
        alnkNext += halfPeriod();
        ++audioHalves;
    }
    if (cycles >= uart.next)
        due.uartRx = uartReceive();
    if (cycles >= usbNext)
    {
        due.usbFrame = true;
        usbNext += usbPeriod;
        if (usbNext <= cycles) usbNext = cycles + usbPeriod;
    }
    if (cycles >= wdtAt) { wdtAt = kNever; due.wdt = true; }
    if (cycles >= uart.otNext)
    {
        uart.otNext = kNever;
        if ((uart.con0 & 1) != 0)
        {
            uart.otpnd = true;
            uartUpdate();
        }
    }
    for (size_t i = 0; i < 8; ++i)
        if (encPending[i] != 0 && cycles >= encNext[i])
        {
            const int s = encState[i];
            encState[i] = (uint8_t) ((s + signum (encPending[i])) & 3);
            encPending[i] -= signum (encPending[i]);
            encNext[i] = cycles + encStep();
        }
    while (! script.empty() && script.front().first <= cycles)
    {
        const auto a = script.front().second;
        script.pop_front();
        apply (a);
    }
    nextDue = nextEvent();
    return due;
}

void Periph::chipReset()
{
    Periph p;
    p.cycles = cycles;
    p.hz = hz;
    p.cycleTicks = cycleTicks;
    p.skipIdle = skipIdle;
    p.roomCap = roomCap;
    p.skipped = skipped;
    p.spi1Wire = spi1Wire;
    p.audio = std::move (audio);
    p.audioHalves = audioHalves;
    p.adcValue = adcValue;
    p.usbNext = usbNext;
    p.usbPeriod = usbPeriod;
    p.uart.next = uart.next;
    p.uart.q = std::move (uart.q);
    p.uart.dropped = uart.dropped;
    p.srShift = srShift;
    p.srLatched = srLatched;
    p.ledAcc = ledAcc;
    p.ledLast = ledLast;
    p.ledSince = ledSince;
    p.keysDown = keysDown;
    p.keyForce = keyForce;
    p.keyVel = keyVel;
    p.encState = encState;
    p.encPending = encPending;
    p.encNext = encNext;
    p.script = std::move (script);
    *this = std::move (p);
}

void Periph::apply (const PanelAction& a)
{
    nextDue = 0;
    switch (a.kind)
    {
        case PanelAction::Key:
            if (a.id < 41)
            {
                const auto i = (size_t) a.id;
                const bool down = a.value != 0;
                if (keysDown[i] != down)
                {
                    const float to = down ? (a.id < 14 ? 1.0f : velForce (keyVel)) : 0.0f;
                    keyForce[i] = Ramp { keyForce[i].at (cycles), to, cycles, msCycles (down ? 1.5 : 8.0) };
                }
                keysDown[i] = down;
            }
            break;
        case PanelAction::KeyVel: keyVel = (uint8_t) std::clamp (a.value, 1, 127); break;
        case PanelAction::Pressure:
            if (a.id < 41 && keysDown[a.id] && a.id >= 14)
                keyForce[a.id] = Ramp { keyForce[a.id].at (cycles), velForce (a.value), cycles, msCycles (20.0) };
            break;
        case PanelAction::Enc:
            if (a.id < 7)
            {
                // un clic = un ciclo completo de cuadratura (4 transiciones); horario recorre la secuencia Gray al revés
                encPending[a.id] -= a.value * 4;
                encNext[a.id] = cycles;
            }
            break;
        case PanelAction::Adc:
            if (a.id < 16)
                adcValue[a.id] = (uint16_t) std::clamp (a.value, 0, 1023);
            break;
        case PanelAction::Midi:
            // un teclado USB puede mandar más rápido de lo que acepta el jack de 31 250 baudios: pasado un segundo de
            // retraso los bytes se descartan en vez de sonar tarde
            if (uart.q.size() >= kMidiQueueMax)
            {
                ++uart.dropped;
                return;
            }
            if (uart.q.empty())
                uart.next = std::min (uart.next, cycles + midiByteTime());
            uart.q.push_back ((uint8_t) a.value);
            break;
    }
}

uint64_t Periph::midiByteTime() const noexcept { return std::max<uint64_t> (hz * kMidiByteBits / kMidiBaud, 1); }

std::optional<std::pair<uint32_t, uint8_t>> Periph::uartReceive()
{
    if (uart.q.empty())
        return std::nullopt;
    const auto b = uart.q.front();
    uart.q.pop_front();
    uart.next = uart.q.empty() ? kNever : cycles + midiByteTime();
    auto& u = uart;
    const auto len = u.rxeadr - u.rxsadr;
    if ((u.con0 & 0x41) != 0x41 || len == 0 || len > 0x10000)
    {
        ++u.dropped;
        return std::nullopt;
    }
    const auto at = u.rxsadr + u.wr % len;
    u.wr = (u.wr + 1) % len;
    ++u.since;
    if (u.rxcnt != 0 && u.since >= u.rxcnt)
        u.rpnd = true;
    // el timeout: OTCNT relojes de UART tras el último byte (al menos un tiempo de byte)
    const auto ot = std::max<uint64_t> ((uint64_t) u.otcnt * hz / kUartHz, midiByteTime());
    uart.otNext = cycles + ot;
    uartUpdate();
    return std::make_pair (at, b);
}

void Periph::uartUpdate() noexcept
{
    const auto& u = uart;
    uartIrq = ((u.con0 & 0x08) != 0 && u.rpnd) || ((u.con0 & 0x20) != 0 && u.otpnd) || ((u.con0 & 0x04) != 0 && u.tpnd);
}

bool Periph::irqEnabled (uint32_t core, uint8_t n) const noexcept
{
    const auto& bank = core == 0 ? icfg : icfg1;
    return ((bank[n >> 3] >> ((n & 7) * 4)) & 1) != 0;
}

std::optional<std::pair<uint8_t, uint8_t>> Periph::pendingIrqFor (uint32_t core) const noexcept
{
    if (! anyPending())
        return std::nullopt;
    const auto& bank = core == 0 ? icfg : icfg1;
    std::optional<std::pair<uint8_t, uint8_t>> best;
    const auto consider = [&] (uint8_t n)
    {
        const auto nib = (bank[n >> 3] >> ((n & 7) * 4)) & 0xf;
        if ((nib & 1) != 0)
        {
            const auto prio = (uint8_t) ((nib >> 1) & 7);
            if (! best || prio > best->second)
                best = std::make_pair (n, prio);
        }
    };
    if (excPending && core == 0) consider (1);  // la excepción de CPU (una guarda)
    if (t5Pending) consider (kIrqTimer5);
    if (tickPending) consider (kIrqOsTick);
    if (spi1Pending) consider (kIrqSpi1);
    if (adcIrq) consider (kIrqSaradc);
    if (spi2Pending) consider (kIrqSpi2);
    if (uartIrq) consider (kIrqUart1);
    for (int i = 0; i < 4; ++i)
        if (tmr[(size_t) i].pending) consider ((uint8_t) (4 + i));
    if ((alnkPend & 0xf0) != 0 && (alnkCon0 & 0x800) != 0) consider (kIrqAlnk0);
    for (uint8_t b = 0; b < 8; ++b)
        if ((ilat & (1u << b)) != 0) consider ((uint8_t) (120 + b));
    return best;
}

std::optional<uint32_t> Periph::read (uint32_t addr)
{
    switch (addr)
    {
        case 0x10800: return t4Con;
        case 0x10804:
        {
            if ((t4Con & 1) == 0)
                return 0u;
            // tres lecturas seguidas fuera de una ISR: un bucle de sondeo (con skipIdle se salta el tiempo, como mucho
            // 20 us cada vez y nunca más allá del siguiente evento)
            if (skipIdle && ! inIsr)
            {
                cntStreak = cycles - lastCntRead < 64 ? cntStreak + 1 : 0;
                if (cntStreak >= 2)
                {
                    const auto next = std::min (nextEvent(), cycles + hz / 50'000);
                    if (next > cycles + 1)
                    {
                        skipped += next - 1 - cycles;
                        cycles = next - 1;
                        nextDue = 0;
                    }
                }
                lastCntRead = cycles;
            }
            return mulDiv (cycles - t4Base, kT4Hz, hz);
        }
        case 0x10400: case 0x10500: case 0x10600: case 0x10700:
        {
            const auto& t = tmr[(addr - 0x10400) >> 8];
            return t.con | (t.pending ? 0x8000u : 0u);
        }
        case 0x10404: case 0x10504: case 0x10604: case 0x10704:
        {
            const int i = (int) ((addr - 0x10400) >> 8);
            const auto& t = tmr[(size_t) i];
            if (t.next == kNever)
                return t.cnt0;
            return t.cnt0 + (uint32_t) ((cycles - t.start) / std::max<uint64_t> (tmrCyclesPerCount (i), 1));
        }
        case 0x10408: case 0x10508: case 0x10608: case 0x10708: return tmr[(addr - 0x10400) >> 8].prd;
        case 0x10900: return t5Con | (t5Pending ? 0x8000u : 0u);
        case 0x10904:
            if ((t5Con & 1) != 0 && t5Next != kNever)
            {
                const auto period = t5Period();
                const auto into = period - std::min (t5Next - cycles, period);
                return (uint32_t) ((into * kT5Hz / hz) % std::max<uint32_t> (t5Prd, 1));
            }
            return 0u;
        case 0x10908: return t5Prd;
        case 0x12e00: return (alnkCon0 & ~0x8000u) | (alnkHalf << 15);
        case 0x12e04: return alnkCon1;
        case 0x12e08: return alnkPend;
        case 0x12e0c: return alnkCon3;
        case 0x12e1c: return alnkAdr;
        case 0x12e20: return alnkLen;
        // las conversiones son instantáneas: hechas (bit 7) en cuanto empiezan; el bit 4 se lee como se escribió
        case 0x13100: return (adcCon & ~0x80u) | (adcDone ? 0x80u : 0u);
        case 0x13104: return adcSample;  // RES: el valor de la conversión, tomado al empezar
        // la palabra de poner del latch lee los pendientes; la de borrar lee 0 (el manejador IRQ 122 de fábrica reconoce
        // con [0x1EEF1A4] |= 4, y releer todos los pendientes ahí borraba también un cambio de contexto pendiente)
        case 0x1eef1a0: return ilat;
        case 0x1eef1a4: return 0u;
        case 0x1eef0ec: return tickCtl | (tickPending ? 0x80u : 0u);
        case 0x1eef0f0:
            if (tickNext == kNever)
                return 0u;
            return mulDiv (std::min (cycles - tickStart, tickPeriod()), tickPrd, tickPeriod());  // dentro del periodo
        case 0x1eef0f4: return tickPrd;
        case 0x12100:
            return uart.con0 | ((uint32_t) uart.otpnd << 11) | ((uint32_t) uart.rpnd << 14) | ((uint32_t) uart.tpnd << 15);
        case 0x1211c: return uart.rxsadr;
        case 0x12120: return uart.rxeadr;
        case 0x12124: return uart.rxcnt;
        case 0x12110: return uart.otcnt;
        case 0x12128: return uart.hrxcnt;
        case 0x50004: return paIn();
        case 0x50044: return pbIn();
        default: break;
    }
    if (addr >= 0x1eef100 && addr <= 0x1eef17f) return icfg[(addr - 0x1eef100) / 4];
    if (addr >= 0x1eef300 && addr <= 0x1eef37f) return icfg1[(addr - 0x1eef300) / 4];
    return std::nullopt;
}

bool Periph::write (uint32_t addr, uint32_t v)
{
    // un temporizador o un DMA puede haberse reprogramado: el ejecutor atiende el SoC antes de la siguiente instrucción.
    // No en el ADC del FM-1 (conversiones instantáneas), ni en los pines de puerto (salvo PD0, el chip select de la
    // flash), ni en las palabras de contador que ignoran escrituras
    const bool untimed = addr == 0x13100 || addr == 0x13104 || (addr >= 0x50000 && addr <= 0x501ff && addr != 0x500c0)
        || addr == 0x10808 || addr == 0x10904;
    if (! untimed)
        nextDue = 0;
    switch (addr)
    {
        case 0x10800:
            if ((t4Con & 1) == 0 && (v & 1) != 0)
                t4Base = cycles;
            t4Con = v & ~0x4000u;
            return true;
        case 0x10804: t4Base = cycles; return true;
        case 0x10808: return true;
        case 0x10900:
        {
            if ((v & 0x4000) != 0) t5Pending = false;
            const auto was = t5Con & 1;
            t5Con = v & ~0xc000u;
            if (was == 0 && (v & 1) != 0) t5Next = cycles + t5Period();
            else if ((v & 1) == 0) t5Next = kNever;
            return true;
        }
        case 0x10904: return true;
        case 0x10908: t5Prd = v; return true;
        case 0x10400: case 0x10500: case 0x10600: case 0x10700:
        {
            const int i = (int) ((addr - 0x10400) >> 8);
            if ((v & 0x4000) != 0) tmr[(size_t) i].pending = false;
            tmr[(size_t) i].con = v & ~0xc000u;
            tmrArm (i);
            return true;
        }
        case 0x10404: case 0x10504: case 0x10604: case 0x10704:
        {
            const int i = (int) ((addr - 0x10400) >> 8);
            tmr[(size_t) i].cnt0 = v;
            tmrArm (i);
            return true;
        }
        case 0x10408: case 0x10508: case 0x10608: case 0x10708:
        {
            const int i = (int) ((addr - 0x10400) >> 8);
            tmr[(size_t) i].prd = v;
            tmrArm (i);
            return true;
        }
        case 0x12e00:
        {
            const auto was = alnkCon0 & 0x800;
            alnkCon0 = v & 0xffff & ~0x8000u;
            if (was == 0 && (v & 0x800) != 0)
            {
                alnkHalf = 0;
                alnkNext = cycles + halfPeriod();
            }
            else if ((v & 0x800) == 0)
                alnkNext = kNever;
            return true;
        }
        case 0x12e04: alnkCon1 = v & 0xffff; return true;
        case 0x12e08: alnkPend &= ~((v & 0xf) << 4); return true;  // los bits 0-3 borran los pendientes 4-7
        case 0x12e0c: alnkCon3 = v & 0xff; return true;
        case 0x12e1c: alnkAdr = v; return true;
        case 0x12e20: alnkLen = v & 0xffff; return true;
        case 0x13100:
            // bit 6 con bit 4 lanza una conversión, que acaba al instante; con bit 5 levanta la IRQ 24. Bit 6 sin bit 4
            // reconoce (CON = 0x40)
            if ((v & 0x40) != 0)
            {
                if ((v & 0x10) != 0)
                {
                    const auto ch = (v >> 8) & 0xf;
                    const auto row = rowReading (ch);
                    adcSample = row ? *row : adcValue[ch];
                    adcDone = true;
                    if (adcIrq != ((v & 0x20) != 0)) nextDue = 0;
                    adcIrq = (v & 0x20) != 0;
                }
                else
                {
                    if (adcIrq) nextDue = 0;
                    adcDone = false;
                    adcIrq = false;
                }
            }
            adcCon = v;
            return true;
        case 0x13104: return true;
        case 0x1eef0ec:
        {
            if ((v & 0x40) != 0) tickPending = false;
            const auto was = tickCtl & 1;
            tickCtl = v & 0x3f;
            if (was == 0 && (v & 1) != 0)
            {
                tickStart = cycles;
                tickNext = cycles + tickPeriod();
            }
            else if ((v & 1) == 0)
                tickNext = kNever;
            return true;
        }
        case 0x1eef0f0:
            tickStart = cycles;
            if (tickNext != kNever) tickNext = cycles + tickPeriod();
            return true;
        case 0x1eef0f4: tickPrd = v; return true;
        case 0x1eef1a0: ilat |= v; return true;
        case 0x1eef1a4: ilat &= ~v; return true;
        case 0x12100:
        {
            auto& u = uart;
            if ((v & 0x80) != 0) { u.hrxcnt = u.since; u.since = 0; }
            if ((v & 0x400) != 0) u.otpnd = false;
            if ((v & 0x1000) != 0) u.rpnd = false;
            if ((v & 0x2000) != 0) u.tpnd = false;
            u.con0 = v & 0xffff & ~0xfc80u;
            uartUpdate();
            return true;
        }
        case 0x1211c: uart.rxsadr = v; uart.wr = 0; return true;  // el inicio del anillo reinicia la posición del DMA
        case 0x12120: uart.rxeadr = v; return true;
        case 0x12124: uart.rxcnt = v & 0xffff; return true;
        case 0x12110: uart.otcnt = v; return true;
        case 0x12118: uart.tpnd = true; uartUpdate(); return false;  // TX por DMA: enviado al instante (no hay pin)
        case 0x50000: ledCredit(); paWrite (v); return true;
        // (se guardan también como SFR simples: las lecturas de filas los necesitan)
        case 0x50008: paDir = v; return false;
        case 0x50010: paPu = v; return false;
        case 0x50018: paHd0 = v; return false;
        case 0x5001c: paHd = v; return false;
        case 0x501c0: ledCredit(); phOut = v; return true;
        default: break;
    }
    if (addr >= 0x1eef100 && addr <= 0x1eef17f) { icfg[(addr - 0x1eef100) / 4] = v; return true; }
    if (addr >= 0x1eef300 && addr <= 0x1eef37f) { icfg1[(addr - 0x1eef300) / 4] = v; return true; }
    return false;
}

// ---- LEDs: las líneas PA9, PA10, PH6, PH9 encienden el LED de la columna fijada en las filas 1, 2, 3, 4 ----------------
void Periph::ledCredit()
{
    const auto dt = cycles - ledLast;
    ledLast = cycles;
    if (dt == 0)
        return;
    const uint32_t lines = ((paOut >> 8) & 2) | ((paOut >> 8) & 4) | ((phOut >> 3) & 8) | ((phOut >> 5) & 16);
    if (lines == 0)
        return;
    const std::pair<uint32_t, uint16_t> key { lines, srLatched };
    if (key == ledKey)
    {
        ledPend += dt;  // mismo estado: el tiempo se acumula y se carga al cambiar
        return;
    }
    ledFlush();
    ledKey = key;
    ledPend = dt;
}

void Periph::ledFlush()
{
    const auto [lines, latched] = ledKey;
    const auto dt = ledPend;
    ledPend = 0;
    if (dt == 0)
        return;
    for (int col = 0; col < 11; ++col)
    {
        if ((latched & (1 << col)) != 0)
            continue;
        for (int row = 1; row < 5; ++row)
            if ((lines & (1u << row)) != 0 && kKeymap[row][col] >= 0)
                ledAcc[(size_t) kKeymap[row][col]] += dt;
    }
}

std::array<float, 41> Periph::ledPeek()
{
    ledCredit();
    ledFlush();
    const auto span = (float) std::max<uint64_t> (cycles - ledSince, 1);
    std::array<float, 41> out {};
    for (size_t i = 0; i < 41; ++i)
        out[i] = (float) ledAcc[i] / span;
    return out;
}

std::array<float, 41> Periph::ledTake()
{
    const auto out = ledPeek();
    ledSince = cycles;
    ledAcc.fill (0);
    return out;
}

// cpu0 ejecutó idle: con skipIdle y nada pendiente, el tiempo salta a justo antes del siguiente evento
void Periph::idle()
{
    if (! skipIdle || anyPending())
        return;
    const auto next = nextEvent();
    if (next != kNever && next > cycles + 1)
    {
        skipped += next - 1 - cycles;
        cycles = next - 1;
        nextDue = 0;
    }
}

// Transferencia SPI1 de bytes a BAUD (reloj 60 MHz / (baud + 1): ~12 MHz con 4 para el LCD); con irq, IRQ 16 al final
void Periph::spi1Start (uint32_t bytes, uint32_t baud, bool irq)
{
    const uint64_t clk = 60'000'000 / ((uint64_t) baud + 1);
    const auto dur = std::max<uint64_t> ((uint64_t) std::max<uint32_t> (bytes, 1) * 8 * hz / clk, 1);
    spi1DoneAt = cycles + dur;
    spi1Wire += dur;
    if (irq)
    {
        spi1IrqAt = spi1DoneAt;
        nextDue = 0;
    }
}

// SPI2: 16·(baud + 1) relojes de periférico por byte (48 MHz supuestos por fm1-emu)
void Periph::spi2Start (uint32_t bytes, uint32_t baud, bool irq)
{
    nextDue = 0;
    if (irq)
    {
        const auto clocks = (uint64_t) std::max<uint32_t> (bytes, 1) * 16 * ((uint64_t) baud + 1);
        spi2Next = cycles + std::max<uint64_t> (clocks * hz / 48'000'000, 1);
    }
}

// Un byte a la cadena de 74HC595 por SPI2, primero el bit alto (el firmware de fábrica; Felucca usa PA4/PA3 a mano)
void Periph::srShiftByte (uint8_t b) noexcept
{
    for (int bit = 7; bit >= 0; --bit)
        srShift = (uint16_t) ((srShift << 1) | ((b >> bit) & 1));
}

void Periph::paWrite (uint32_t v) noexcept
{
    const auto old = paOut;
    paOut = v;
    if ((old & 8) == 0 && (v & 8) != 0)
        srShift = (uint16_t) ((srShift << 1) | ((v >> 4) & 1));  // flanco de SRCLK: entra SER (PA4)
    if ((old & 2) == 0 && (v & 2) != 0)
        srLatched = srShift;  // flanco de RCLK: las salidas siguen al registro
}

// Canal de ADC 0 / 1 (fila PA7 / PA8): la tecla de la columna fijada, por el pull-up de la fila o por su driver
std::optional<uint16_t> Periph::rowReading (uint32_t ch)
{
    int row;
    uint32_t pin;
    if (ch == 0) { row = 3; pin = 7; }
    else if (ch == 1) { row = 4; pin = 8; }
    else return std::nullopt;
    const auto m = 1u << pin;
    float rs;
    if ((paDir & m) == 0)
    {
        if ((paOut & m) == 0)
            return (uint16_t) 0;
        static constexpr float drive[4] = { 312.0f, 132.0f, 51.0f, 35.0f };  // ohm por HD0 / HD
        rs = drive[((paHd0 & m) != 0 ? 1 : 0) | ((paHd & m) != 0 ? 2 : 0)];
    }
    else if ((paPu & m) != 0)
        rs = 10'000.0f;
    else
        return std::nullopt;
    float val = 1023.0f;
    for (int col = 0; col < 11; ++col)
    {
        const auto id = kKeymap[row][col];
        if ((srLatched & (1 << col)) == 0 && id >= 0)
            val = std::min (val, pillReading (keyForce[(size_t) id].at (cycles), rs));
    }
    adcNoise = adcNoise * 1'103'515'245u + 12345u;
    const auto noise = (float) ((adcNoise >> 16) % 3) - 1.0f;
    return (uint16_t) std::clamp (std::round (val + (val < 1023.0f ? noise : 0.0f)), 0.0f, 1023.0f);
}

uint32_t Periph::closedRows() const noexcept
{
    uint32_t rows = 0;
    for (int col = 0; col < 11; ++col)
    {
        if ((srLatched & (1 << col)) != 0)
            continue;  // columna no seleccionada (activa a nivel bajo)
        for (int row = 0; row < 6; ++row)
        {
            const auto id = kKeymap[row][col];
            if (id >= 0 && keysDown[(size_t) id])
                rows |= 1u << row;
        }
        for (size_t i = 0; i < 7; ++i)
        {
            const auto* g = kGray[encState[i]];
            if (kEnc[i][0] == col && g[0] != 0) rows |= 1u << kEnc[i][1];
            if (kEnc[i][2] == col && g[1] != 0) rows |= 1u << kEnc[i][3];
        }
    }
    return rows;
}

uint32_t Periph::paIn() const noexcept
{
    const auto r = closedRows();
    const auto low = (r & 1) | ((r & 0x1e) << 4);  // fila 0 = PA0, filas 1-4 = PA5..PA8; lo demás con pull-up
    return (paOut | 0xffff) & ~low;
}

uint32_t Periph::pbIn() const noexcept
{
    const auto r = closedRows();
    return 0xffffu & ~(((r >> 5) & 1) << 7);
}

// ---- nombres -------------------------------------------------------------------------------------------------------------
static std::string upper (const char* s)
{
    std::string n (s);
    for (auto& c : n) c = (char) std::toupper ((unsigned char) c);
    return n;
}

static std::optional<int> parseUInt (const std::string& s)
{
    if (s.empty() || s.size() > 3) return std::nullopt;
    int v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + (c - '0');
    }
    return v;
}

static std::optional<int> parseInt (const std::string& s)
{
    if (! s.empty() && s[0] == '-')
    {
        const auto v = parseUInt (s.substr (1));
        return v ? std::optional<int> (-*v) : std::nullopt;
    }
    return parseUInt (s);
}

// C4, F#3, Bb4 -> índice de tecla de nota (0 = F3 .. 26 = G5)
static std::optional<uint8_t> noteName (const std::string& n)
{
    if (n.empty()) return std::nullopt;
    int base;
    switch (n[0])
    {
        case 'C': base = 0; break;
        case 'D': base = 2; break;
        case 'E': base = 4; break;
        case 'F': base = 5; break;
        case 'G': base = 7; break;
        case 'A': base = 9; break;
        case 'B': base = 11; break;
        default: return std::nullopt;
    }
    int acc = 0;
    std::string rest = n.substr (1);
    if (n.size() > 1 && n[1] == '#') { acc = 1; rest = n.substr (2); }
    else if (n.size() == 3 && n[1] == 'B') { acc = -1; rest = n.substr (2); }  // "BB4" tras pasar a mayúsculas = Bb4
    const auto oct = parseInt (rest);
    if (! oct) return std::nullopt;
    const int k = (*oct + 1) * 12 + base + acc - 53;  // F3 = MIDI 53
    if (k < 0 || k >= 27) return std::nullopt;
    return (uint8_t) k;
}

std::optional<uint8_t> fm1KeyId (const char* name)
{
    const auto n = upper (name);
    static const char* const buttons[] = { "OCTDN", "OCTUP", "FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
    for (uint8_t i = 0; i < 14; ++i)
        if (n == buttons[i]) return i;
    if (n.size() > 1 && n[0] == 'K')
        if (const auto k = parseUInt (n.substr (1)); k && *k < 41) return (uint8_t) *k;
    if (const auto k = noteName (n)) return (uint8_t) (14 + *k);
    if (n.size() > 1 && n[0] == 'N')
        if (const auto k = parseUInt (n.substr (1)); k && *k < 27) return (uint8_t) (14 + *k);
    return std::nullopt;
}

std::optional<uint8_t> fm1EncId (const char* name)
{
    const auto n = upper (name);
    if (n == "SELECT") return (uint8_t) 0;
    if (n == "ALGO" || n == "ALGORITHM") return (uint8_t) 1;
    if (n == "PRESET" || n == "PRESETS") return (uint8_t) 6;
    if (n == "K1") return (uint8_t) 2;
    if (n == "K2") return (uint8_t) 3;
    if (n == "K3") return (uint8_t) 4;
    if (n == "K4") return (uint8_t) 5;
    if (n.rfind ("ENC", 0) == 0)
        if (const auto e = parseUInt (n.substr (3)); e && *e < 7) return (uint8_t) *e;
    return std::nullopt;
}
} // namespace fm1

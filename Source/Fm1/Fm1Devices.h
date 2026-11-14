// Dispositivos hoja del SoC del FM-1: RAM, flash SPI NOR (SPI0), panel LCD 240×240 y puerto P33 (dominio de energía).
//
// Port a C++ de fm1-emu (Joakim Eriksson, licencia MIT, commit 3e3fb27), crates/fm1-soc/src: ram.rs, spiflash.rs, lcd.rs
// y p33.rs. Lo que fm1-emu activa por variables de entorno (trazas, corte de corriente de la flash) son aquí campos.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace fm1
{
// ---- RAM plana little-endian en la dirección 0 -------------------------------------------------------------------------
// Por palabra de 4 bytes: si guarda código del que la CPU ha hecho bloques (markCode). codeWrites cuenta escrituras a
// esas palabras: mientras no cambie, ningún bloque hecho desde RAM ha cambiado.
class Ram
{
public:
    explicit Ram (size_t size) : data (size, 0), code (size / 4 + 1, 0) {}

    void markCode (uint32_t lo, uint32_t hi)
    {
        const size_t last = (size_t) (std::max (hi, lo + 1) - 1) >> 2;
        for (size_t w = (size_t) (lo >= 3 ? lo - 3 : 0) >> 2; w <= std::min (last, code.size() - 1); ++w)
            code[w] = 1;
    }
    bool isCode (uint32_t a) const noexcept { return (a >> 2) < code.size() && code[a >> 2] != 0; }
    size_t size() const noexcept { return data.size(); }
    uint8_t* ptr() noexcept { return data.data(); }

    uint8_t read8 (uint32_t a) const noexcept { return data[a]; }
    uint16_t read16 (uint32_t a) const noexcept { return (uint16_t) (data[a] | (data[a + 1] << 8)); }
    uint32_t read32 (uint32_t a) const noexcept
    {
        uint32_t v;
        std::memcpy (&v, &data[a], 4);  // little-endian en x86 y ARM
        return v;
    }
    void write8 (uint32_t a, uint8_t v) noexcept { wrote (a, 1); data[a] = v; }
    void write16 (uint32_t a, uint16_t v) noexcept { wrote (a, 2); data[a] = (uint8_t) v; data[a + 1] = (uint8_t) (v >> 8); }
    void write32 (uint32_t a, uint32_t v) noexcept { wrote (a, 4); std::memcpy (&data[a], &v, 4); }
    void restore32 (uint32_t a, uint32_t v) noexcept { std::memcpy (&data[a], &v, 4); }  // sin contar (deshacer una sonda)
    void load (uint32_t offset, const uint8_t* bytes, size_t n)
    {
        ++codeWrites;
        std::memcpy (&data[offset], bytes, n);
    }

    uint64_t codeWrites = 0;
    // (Machine, vuelta observada del salto de bucles) cada palabra que se va a escribir con lo que tenía, como las apunta
    // el sondeo: la de la dirección y, si la escritura se sale de ella, la siguiente. nullptr: nada
    std::vector<std::pair<uint32_t, uint32_t>>* log = nullptr;

private:
    void wrote (uint32_t a, uint32_t width) noexcept
    {
        if (code[a >> 2] != 0)
            ++codeWrites;
        if (log != nullptr) [[unlikely]]
        {
            const auto w = a & ~3u;
            log->push_back ({ w, read32 (w) });
            if ((a & 3) + width > 4)
                log->push_back ({ w + 4, read32 (w + 4) });
        }
    }
    std::vector<uint8_t> data, code;
};

// ---- SPI0 (0x11C00) con la flash SPI NOR externa -------------------------------------------------------------------------
// El firmware habla con la flash directamente (con el SFC/XIP apagado) para la sonda JEDEC, el estado, borrados y
// programación de página. Registros: CON bit15 = transferencia pendiente (se pone al acabar un byte), bit14 = borrar
// pendiente (escritura), bit12 = dirección (1 = rx), bit3 = habilitar; BUF el byte a enviar / recibido. El chip select
// es PD0, un GPIO. Flash de 1 MiB con JEDEC 85 60 14 (Puya, como los aparatos reales). Órdenes: 9F, 4B, 05/35, 06/04,
// 03/0B, 20 (borrado de 4 KiB), D8 (64 KiB), 02 (programa de página). Borrar y programar son instantáneos; el estado
// nunca dice ocupado. Cortes de corriente (cutAt = N): la N-ésima operación se queda a medias y nada después llega.
class SpiFlash
{
public:
    static constexpr uint32_t kBase = 0x11c00;
    static constexpr size_t kSize = 1u << 20;

    SpiFlash() : mem (kSize, 0xff) {}

    void load (const uint8_t* image, size_t n) { std::memcpy (mem.data(), image, std::min (n, mem.size())); }
    bool busy() const noexcept { return csLow; }
    void setCs (bool low);
    void chipReset();  // SPI0 apagado y la orden en curso abandonada; la flash conserva contenido y WEL
    bool cut() const noexcept { return cutAt && ops >= *cutAt; }
    uint32_t read (uint32_t a) const noexcept;
    void write (uint32_t a, uint32_t value);

    std::vector<uint8_t> mem;
    uint32_t con = 0, buf = 0;
    std::array<uint8_t, 3> jedec { 0x85, 0x60, 0x14 };
    std::optional<std::pair<size_t, size_t>> changed;  // rango que cambió el último borrado o programa (para la XIP)
    uint32_t revision = 0;
    uint32_t ops = 0;
    std::optional<uint32_t> cutAt;

private:
    uint8_t transfer (uint8_t out);
    std::optional<bool> op();  // true entera, false a medias, nullopt tras el corte
    void erase (size_t size);

    bool csLow = false, wel = false;
    uint8_t cmd = 0;
    size_t nbytes = 0;
    uint32_t addr = 0;
    std::vector<uint8_t> pendingProg;
};

// ---- panel ST7789 de 240×240 (SPI1: PC9 CLK, PC10 DO, PC7 CS, PC8 D/C; retroiluminación PA2) ----------------------------
// Mantiene un framebuffer RGB565. INVON es el modo nativo del panel IPS (lo envían Felucca y el firmware de fábrica): los
// píxeles tal como se escriben son lo que se ve.
class Lcd
{
public:
    static constexpr int W = 240, H = 240;

    Lcd() : fb ((size_t) W * H, 0) {}
    void feed (uint8_t byte, bool data);  // un byte en el bus; data = D/C alto

    std::vector<uint16_t> fb;
    uint64_t frames = 0;
    uint32_t revision = 0;
    bool inverted = false, displayOn = false;
    uint64_t pixelsInWindow = 0;

private:
    enum class Phase { Cmd, Arg, Pixel };
    void handleCmd (uint8_t c);
    void takeArgs();
    void pushPixel (uint16_t px);
    void pixelByte (uint8_t b);

    uint16_t win[4] { 0, 0, W - 1, H - 1 };  // x0, y0, x1, y1
    uint16_t cur[2] { 0, 0 };
    Phase phase = Phase::Cmd;
    uint32_t need = 0;
    uint8_t cmd = 0;
    uint8_t arg[4] {};
    size_t argNext = 0;
    int pixelHi = -1;
};

// RGB565 a RGB888 como el volcado BMP de fm1-emu (escala entera: c·255/31, c·255/63)
inline void rgb565To888 (uint16_t p, uint8_t& r, uint8_t& g, uint8_t& b) noexcept
{
    r = (uint8_t) ((((p >> 11) & 0x1f) * 255u) / 31u);
    g = (uint8_t) ((((p >> 5) & 0x3f) * 255u) / 63u);
    b = (uint8_t) (((p & 0x1f) * 255u) / 31u);
}

// ---- P33: registros analógicos del dominio de energía (P33_CON 0x13E08, P33_DAT 0x13E0C) --------------------------------
// Transferencias de un byte; cada acceso son tres con el chip select (CON bit 0): orden (bit 7 leer; bits 5..6 la
// operación: 0 escribir, 1 or, 2 and, 3 xor; bits 0..1 los bits altos de la dirección), dirección baja y dato. CON bit 8
// elige el dominio RTC. Registros planos, 0 al encender. Actúan dos: WDT_CON (0x80, bit 4 en marcha, bits 0..3 el
// periodo, bit 6 escrito = alimentar) y PR_PWR (0xA0, bit 4 = reset del chip). Un reset deja la causa en RST_SRC (0x12).
class P33
{
public:
    static constexpr uint32_t kCon = 0x13e08, kDat = 0x13e0c;
    static constexpr uint16_t kRstSrc = 0x12, kWdtCon = 0x80, kPrPwr = 0xa0;
    static constexpr uint8_t kRstWdt = 0x04, kRstSoft = 0x40;

    struct Event { enum Kind { Wdt, Reset } kind; bool fed; };

    P33() : regs (0x800, 0) {}
    // Periodo del perro guardián por WDT_CON bits 0..3, en ms: 1 ms doblando hasta 512 ms, luego 1 s doblando hasta 32 s
    static uint64_t wdtMs (uint8_t c) noexcept { const int t = c & 15; return t <= 9 ? (1ull << t) : (1000ull << (t - 10)); }
    std::optional<Event> takeEvent() noexcept { auto e = event; event.reset(); return e; }
    void chipReset (uint8_t cause);
    uint8_t reg (uint16_t a, bool rtc) const noexcept { return regs[index (a, rtc)]; }
    uint32_t read (uint32_t a) const noexcept { return (a & ~3u) == kCon ? (con & ~0x12u) : dat; }  // nunca ocupado
    void write (uint32_t a, uint32_t value);
    uint64_t changed = 0;  // escrituras que cambiaron un valor
    // El puerto en resumen: dos instantáneas iguales son el mismo puerto (sus registros solo cambian junto con changed)
    struct Snap
    {
        uint32_t con, dat; uint8_t step, cmd; uint16_t addr; uint64_t changed;
        bool operator== (const Snap&) const = default;
    };
    Snap snap() const noexcept { return { con, dat, stepN, cmd, addr, changed }; }

private:
    static size_t index (uint16_t a, bool rtc) noexcept { return (size_t) (a & 0x3ff) | (rtc ? 0x400u : 0u); }
    uint8_t transfer (uint8_t b);

    uint32_t con = 0, dat = 0;
    uint8_t stepN = 0, cmd = 0;
    uint16_t addr = 0;
    std::vector<uint8_t> regs;
    std::optional<Event> event;
};
} // namespace fm1

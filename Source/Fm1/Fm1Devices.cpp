// Port a C++ de fm1-emu, crates/fm1-soc/src/{spiflash,lcd,p33}.rs (MIT). Ver Fm1Devices.h.
#include "Fm1Devices.h"

#include <algorithm>

namespace fm1
{
// ---- SpiFlash -------------------------------------------------------------------------------------------------------------
void SpiFlash::setCs (bool low)
{
    if (low && ! csLow)
    {
        cmd = 0;
        nbytes = 0;
        pendingProg.clear();
    }
    if (! low && csLow && cmd == 0x02)
    {
        // el programa de página se aplica al soltar el chip select
        const auto o = op();
        const size_t n = ! o ? 0 : *o ? pendingProg.size() : pendingProg.size() / 2;
        for (size_t i = 0; i < n; ++i)
            mem[(addr + i) & (kSize - 1)] &= pendingProg[i];
        const size_t a = addr & (kSize - 1);
        if (n > 0)
        {
            changed = std::make_pair (a, std::min (a + n, kSize));
            ++revision;
        }
        pendingProg.clear();
        wel = false;
    }
    csLow = low;
}

uint8_t SpiFlash::transfer (uint8_t out)
{
    if (! csLow)
        return 0xff;
    const auto i = nbytes++;
    if (i == 0)
    {
        cmd = out;
        addr = 0;
        if (out == 0x06) wel = true;
        else if (out == 0x04) wel = false;
        return 0xff;
    }
    const auto k = i - 1;  // índice tras la orden
    switch (cmd)
    {
        case 0x9f: return k < 3 ? jedec[k] : (uint8_t) 0xff;
        case 0x4b: { static const uint8_t uid[] = { 0x46, 0x4d, 0x31, 0x2d }; return k < 4 ? (uint8_t) 0xff : uid[(k - 4) & 3]; }  // 4 de relleno y el id único
        case 0x05: return wel ? 0x02 : 0x00;
        case 0x35: return 0x00;
        case 0x03: case 0x0b: case 0x20: case 0xd8: case 0x02:
            if (k < 3)
            {
                addr = (addr << 8) | out;
                if (k == 2)
                {
                    if (cmd == 0x20) erase (0x1000);
                    else if (cmd == 0xd8) erase (0x10000);
                }
                return 0xff;
            }
            if (cmd == 0x0b && k == 3) return 0xff;  // byte de relleno de la lectura rápida
            if (cmd == 0x02) { pendingProg.push_back (out); return 0xff; }
            if (cmd == 0x03 || cmd == 0x0b)
            {
                const auto off = cmd == 0x0b ? k - 4 : k - 3;
                return mem[(addr + off) & (kSize - 1)];
            }
            return 0xff;
        default: return 0xff;
    }
}

std::optional<bool> SpiFlash::op()
{
    ++ops;
    if (cutAt && ops > *cutAt) return std::nullopt;
    if (cutAt && ops == *cutAt) return false;
    return true;
}

void SpiFlash::chipReset()
{
    con = buf = 0;
    csLow = false;
    cmd = 0;
    nbytes = 0;
    pendingProg.clear();
}

void SpiFlash::erase (size_t size)
{
    if (! wel)
        return;
    const auto o = op();
    const size_t n = ! o ? 0 : *o ? size : size / 2;
    const size_t base = addr & ~(size - 1) & (kSize - 1);
    wel = false;
    if (n == 0)
        return;
    std::fill (mem.begin() + (std::ptrdiff_t) base, mem.begin() + (std::ptrdiff_t) std::min (base + n, kSize), (uint8_t) 0xff);
    changed = std::make_pair (base, std::min (base + n, kSize));
    ++revision;
}

uint32_t SpiFlash::read (uint32_t a) const noexcept
{
    switch (a - kBase)
    {
        case 0x0: return con;
        case 0x8: return buf;
        default:  return 0;
    }
}

void SpiFlash::write (uint32_t a, uint32_t value)
{
    switch (a - kBase)
    {
        case 0x0:
        {
            // bit14 borra el pendiente; bit15 es de solo lectura
            auto v = (value & ~0x8000u) | (con & 0x8000u);
            if ((value & 0x4000) != 0) v &= ~0xc000u;
            con = v;
            break;
        }
        case 0x8:
        {
            // escribir BUF lanza la transferencia de un byte; lo recibido queda en BUF con PND
            const auto rx = transfer ((uint8_t) value);
            buf = (con & 0x1000) != 0 ? rx : (value & 0xff);
            con |= 0x8000;
            break;
        }
        default: break;
    }
}

// ---- Lcd ------------------------------------------------------------------------------------------------------------------
void Lcd::feed (uint8_t byte, bool data)
{
    switch (phase)
    {
        case Phase::Cmd:
            if (data) pixelByte (byte); else handleCmd (byte);
            break;
        case Phase::Arg:
            if (! data) { handleCmd (byte); break; }
            if (argNext < 4) arg[argNext] = byte;
            if (++argNext >= need)
            {
                takeArgs();
                phase = Phase::Cmd;
            }
            break;
        case Phase::Pixel:
            if (data) pixelByte (byte);
            else
            {
                phase = Phase::Cmd;
                handleCmd (byte);
            }
            break;
    }
}

void Lcd::handleCmd (uint8_t c)
{
    cmd = c;
    argNext = 0;
    switch (c)
    {
        case 0x2a: case 0x2b: phase = Phase::Arg; need = 4; break;
        case 0x36: case 0x3a: phase = Phase::Arg; need = 1; break;
        case 0x2c:  // RAMWR
            pixelsInWindow = 0;
            cur[0] = win[0];
            cur[1] = win[1];
            phase = Phase::Pixel;
            pixelHi = -1;
            break;
        case 0x21: inverted = true; break;
        case 0x22: inverted = false; break;
        case 0x29: displayOn = true; break;
        case 0x28: displayOn = false; break;
        case 0x01:  // reset software
            std::fill (fb.begin(), fb.end(), (uint16_t) 0);
            ++revision;
            displayOn = false;
            inverted = false;
            ++frames;
            break;
        default: break;
    }
}

void Lcd::takeArgs()
{
    const auto be = [this] (int i) { return (uint16_t) ((arg[i] << 8) | arg[i + 1]); };
    if (cmd == 0x2a)
    {
        win[0] = be (0);
        win[2] = std::min<uint16_t> (be (2), W - 1);
    }
    else if (cmd == 0x2b)
    {
        win[1] = be (0);
        win[3] = std::min<uint16_t> (be (2), H - 1);
    }
}

void Lcd::pushPixel (uint16_t px)
{
    const auto x0 = win[0], y0 = win[1], x1 = win[2], y1 = win[3];
    if (x0 > x1 || y0 > y1)
        return;
    ++pixelsInWindow;
    const int x = cur[0], y = cur[1];
    if (x < W && y < H && fb[(size_t) y * W + (size_t) x] != px)
    {
        fb[(size_t) y * W + (size_t) x] = px;
        ++revision;
    }
    // avance por la ventana
    if (cur[0] >= x1)
    {
        cur[0] = x0;
        if (cur[1] >= y1)
        {
            cur[1] = y0;
            ++frames;  // ventana completa
        }
        else if (cur[1] < 0xffff)
            ++cur[1];
    }
    else if (cur[0] < 0xffff)
        ++cur[0];
}

void Lcd::pixelByte (uint8_t b)
{
    if (pixelHi < 0)
        pixelHi = b;
    else
    {
        const auto px = (uint16_t) ((pixelHi << 8) | b);
        pixelHi = -1;
        pushPixel (px);
    }
}

// ---- P33 ------------------------------------------------------------------------------------------------------------------
void P33::chipReset (uint8_t cause)
{
    regs[kRstSrc] = cause;
    regs[kWdtCon] = 0;
    con = dat = 0;
    stepN = 0;
    event.reset();
    ++changed;
}

void P33::write (uint32_t a, uint32_t value)
{
    if ((a & ~3u) == kDat)
    {
        dat = value;
        return;
    }
    if ((value & 1) == 0)
        stepN = 0;  // sin seleccionar: la siguiente transferencia es una orden nueva
    con = value;
    if ((value & 0x10) != 0 && (value & 1) != 0)
        dat = transfer ((uint8_t) dat);
}

uint8_t P33::transfer (uint8_t b)
{
    const auto s = stepN;
    if (stepN < 255) ++stepN;
    if (s == 0)
    {
        cmd = b;
        addr = (uint16_t) ((b & 3) << 8);
        return 0;
    }
    if (s == 1)
    {
        addr |= b;
        return 0;
    }
    if (s != 2)
        return 0;
    const auto i = index (addr, (con & 0x100) != 0);
    if ((cmd & 0x80) != 0)
        return regs[i];
    auto& r = regs[i];
    uint8_t v;
    switch ((cmd >> 5) & 3)
    {
        case 0:  v = b; break;
        case 1:  v = (uint8_t) (r | b); break;
        case 2:  v = (uint8_t) (r & b); break;
        default: v = (uint8_t) (r ^ b); break;
    }
    // (los bits que actúan leen 0: alimentar el perro en un bucle de sondeo no cambia nada ahí)
    if (i == kWdtCon)
    {
        event = Event { Event::Wdt, (v & 0x40) != 0 };
        v &= (uint8_t) ~0x40;
    }
    else if (i == kPrPwr && (v & 0x10) != 0)
    {
        event = Event { Event::Reset, false };
        v &= (uint8_t) ~0x10;
    }
    if (v != r)
    {
        r = v;
        ++changed;
    }
    return 0;
}
} // namespace fm1

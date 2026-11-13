// Port a C++ de fm1-emu, crates/fm1-soc/src/fwsc.rs (MIT). Ver Fm1Fwsc.h.
#include "Fm1Fwsc.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <cstring>

namespace fm1
{
namespace
{
uint16_t u16le (const uint8_t* b) noexcept { return (uint16_t) (b[0] | (b[1] << 8)); }
uint32_t u32le (const uint8_t* b) noexcept { return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24); }

FwscHead head (const std::vector<uint8_t>& b, size_t o)
{
    FwscHead h;
    for (size_t i = o + 16; i < o + 32 && b[i] != 0; ++i)
        h.name.push_back ((char) b[i]);
    h.offset = u32le (&b[o + 4]);
    h.size = u32le (&b[o + 8]);
    h.flags = b[o + 12];
    h.last = u16le (&b[o + 14]) != 0;
    return h;
}

// La imagen de flash del paquete, tomando la cabecera como `blocks` bloques
bool rawFlashN (const std::vector<uint8_t>& file, size_t blocks, std::vector<uint8_t>& out, std::string& error)
{
    if (file.size() < blocks * 0x30 || blocks < 20)
    {
        error = "too short for an FM-1 update package (.fwsc)";
        return false;
    }
    std::vector<uint8_t> hdr;
    hdr.reserve (blocks * 0x2f);
    for (size_t i = 0; i < blocks; ++i)
        hdr.insert (hdr.end(), file.begin() + (std::ptrdiff_t) (i * 0x30), file.begin() + (std::ptrdiff_t) (i * 0x30 + 0x2f));
    fwscUnmask (hdr.data(), 0x40, 0xffff);
    if (fwscCrc16 (&hdr[2], 0x3e) != u16le (&hdr[0]))
    {
        error = "not an FM-1 update package (.fwsc)";
        return false;
    }
    const auto n = (size_t) u16le (&hdr[8]);
    for (size_t k = 0; k < n; ++k)
    {
        const auto at = 0x40 + k * 0x50;
        if (at + 0x50 > hdr.size())
            break;
        uint8_t e[0x50];
        std::memcpy (e, &hdr[at], sizeof e);
        fwscUnmask (e, sizeof e, 0xffff);
        if (u16le (e) == 0)
        {
            // los offsets cuentan en el fichero sin sus marcadores; con 36 bloques (0x6C0 bytes) llegan dentro de la
            // imagen en 0x400, así que los marcadores salen también del principio de la imagen
            const size_t off = u32le (e + 8), len = u32le (e + 12);
            std::vector<uint8_t> stream = hdr;
            stream.insert (stream.end(), file.begin() + (std::ptrdiff_t) (blocks * 0x30), file.end());
            if (off + len > stream.size())
            {
                error = "truncated package";
                return false;
            }
            out.assign (stream.begin() + (std::ptrdiff_t) off, stream.begin() + (std::ptrdiff_t) (off + len));
            return true;
        }
    }
    error = "no flash image in the package";
    return false;
}

// La clave de 16 bits del área de la aplicación, del bloque de 32 bytes de isd_config.ini
uint16_t chipKey (const uint8_t* blob) noexcept
{
    uint32_t s = 0;
    for (int i = 0; i < 16; ++i) s += blob[i];
    auto sum = (uint8_t) (s & 0xff);
    if (sum >= 0xe0) sum = 0xaa;
    else if (sum <= 0x10) sum = 0x55;
    uint16_t key = 0;
    for (int i = 0; i < 16; ++i)
        if ((uint8_t) (blob[16 + i] ^ blob[15 - i]) < sum)
            key |= (uint16_t) (1u << i);
    return key;
}

void unscramble (std::vector<uint8_t>& buf, size_t from, size_t len, size_t base, uint16_t key) noexcept
{
    for (size_t i = 0; i < len; i += 32)
    {
        const auto n = std::min<size_t> (32, len - i);
        fwscUnmask (&buf[from + i], n, (uint16_t) (key ^ (uint16_t) ((from + i - base) >> 2)));
    }
}

bool unpackN (const std::vector<uint8_t>& file, size_t blocks, FwscUnpacked& u, std::string& error)
{
    std::vector<uint8_t> raw;
    if (! rawFlashN (file, blocks, raw, error))
        return false;
    auto flash = raw;
    bool haveKey = false, haveUnchecked = false, haveArea = false;
    uint16_t key = 0, keyUnchecked = 0;
    size_t area = 0;
    // (un isd_config.ini cuyo CRC no cuadra: los paquetes de fábrica del SMK-37 guardan CRC erróneos; se toma si no hay
    // uno comprobado)
    for (size_t o = 32; o + 32 <= flash.size(); o += 32)
    {
        fwscUnmask (&flash[o], 32, 0xffff);
        const auto h = head (flash, o);
        if (h.name == "isd_config.ini" && h.offset + 34 <= flash.size())
        {
            const auto k = chipKey (&flash[h.offset]);
            if (fwscCrc16 (&flash[h.offset], 32) == u16le (&flash[h.offset + 32])) { key = k; haveKey = true; }
            else { keyUnchecked = k; haveUnchecked = true; }
        }
        if (h.name == "app_dir_head" && h.flags == 0x81 && ! haveArea) { area = h.offset; haveArea = true; }
        if (h.last)
            break;
    }
    if (! haveKey && haveUnchecked) { key = keyUnchecked; haveKey = true; }
    if (! haveKey || ! haveArea)
    {
        error = "no chip key or app area in the flash image";
        return false;
    }
    const auto first = ((area + 32 + 31) & ~(size_t) 31) - area;
    unscramble (flash, area, first, area, key);
    const auto size = head (flash, area).size;
    if (area + size > flash.size())
    {
        error = "app area runs past the flash image";
        return false;
    }
    if (size > first)
        unscramble (flash, area + first, size - first, area, key);
    std::vector<FwscHead> dir;
    for (size_t o = area + 32; o + 32 <= area + size; o += 32)
    {
        auto h = head (flash, o);
        const bool last = h.last;
        dir.push_back (std::move (h));
        if (last)
            break;
    }
    if (std::none_of (dir.begin(), dir.end(), [] (const FwscHead& h) { return h.name == "app.bin"; }))
    {
        error = "no app.bin in the app area";
        return false;
    }
    // El SFC descifra toda la imagen protegida, no solo el área (el firmware de fábrica pone su ventana en el final de
    // la imagen: 0x93000 en la V15). Justo después del área va el directorio EXT_RESERVED: la V16 encuentra su banco
    // USER por él y sin él no arrancan ni el motor ni la interfaz. El área puede acabar a mitad de bloque: ese bloque se
    // vuelve a descifrar desde su inicio.
    size_t end = area + size;
    for (const auto& h : dir)
        if (h.name == "PRCT" && h.offset == 0)
        {
            if (h.size > area + size && h.size <= flash.size())
                end = h.size;
            break;
        }
    if (end > area + size)
    {
        const auto tail = (area + size > area + first) ? ((area + size - (area + first)) & ~(size_t) 31) : 0;
        const auto blk = area + first + tail;
        std::vector<uint8_t> t (raw.begin(), raw.begin() + (std::ptrdiff_t) end);
        unscramble (t, blk, end - blk, area, key);
        std::copy (t.begin() + (std::ptrdiff_t) (area + size), t.begin() + (std::ptrdiff_t) end, flash.begin() + (std::ptrdiff_t) (area + size));
    }
    u.raw = std::move (raw);
    u.xip.assign (flash.begin() + (std::ptrdiff_t) area, flash.end());
    u.area = area;
    u.size = size;
    u.end = end;
    u.dir = std::move (dir);
    u.key = key;
    return true;
}

// ---- banco por defecto de fm1-emu para un área USR vacía -----------------------------------------------------------------
using Op = std::array<uint8_t, 17>;

Op op (uint8_t level, uint8_t coarse, uint8_t fine, uint8_t detune, std::array<uint8_t, 4> rates, std::array<uint8_t, 4> levels, uint8_t vel)
{
    Op o {};
    std::copy (rates.begin(), rates.end(), o.begin());
    std::copy (levels.begin(), levels.end(), o.begin() + 4);
    o[8] = 39;  // punto de corte C3
    o[12] = (uint8_t) (detune << 3);
    o[13] = (uint8_t) (vel << 2);
    o[14] = level;
    o[15] = (uint8_t) (coarse << 1);
    o[16] = fine;
    return o;
}

const Op kOff { 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 7 << 3, 0, 0, 2, 0 };

std::array<uint8_t, 128> voice (const std::array<Op, 6>& ops, uint8_t algorithm, uint8_t feedback, const char* name)
{
    std::array<uint8_t, 128> v {};
    for (size_t i = 0; i < 6; ++i)
        std::copy (ops[i].begin(), ops[i].end(), v.begin() + (std::ptrdiff_t) ((5 - i) * 17));  // OP6 primero
    const uint8_t peg[] = { 99, 99, 99, 99, 50, 50, 50, 50 };  // EG de tono plana
    std::copy (std::begin (peg), std::end (peg), v.begin() + 102);
    v[110] = (uint8_t) (algorithm - 1);
    v[111] = (uint8_t) ((1 << 3) | feedback);  // sincronía de oscilador
    const uint8_t lfo[] = { 35, 0, 0, 0, 0, 24 };  // LFO casi apagado; transposición C3
    std::copy (std::begin (lfo), std::end (lfo), v.begin() + 112);
    v[116] = 3 << 4;
    const auto n = std::strlen (name);
    for (size_t i = 0; i < 10; ++i)
        v[118 + i] = i < n ? (uint8_t) name[i] : (uint8_t) ' ';
    return v;
}

std::vector<std::array<uint8_t, 128>> defaultVoices()
{
    const std::array<uint8_t, 4> pluck { 99, 30, 25, 60 };
    std::vector<std::array<uint8_t, 128>> bank {
        voice ({ op (99, 1, 0, 7, { 96, 28, 22, 65 }, { 99, 80, 0, 0 }, 2), op (58, 14, 0, 7, { 99, 60, 40, 70 }, { 99, 0, 0, 0 }, 3),
                 op (92, 1, 0, 9, { 95, 30, 24, 65 }, { 99, 78, 0, 0 }, 2), op (76, 1, 0, 5, { 94, 40, 30, 70 }, { 99, 60, 0, 0 }, 2), kOff, kOff },
               5, 0, "EMU E.PNO "),
        voice ({ op (99, 0, 0, 7, { 99, 40, 30, 70 }, { 99, 85, 70, 0 }, 1), op (82, 0, 0, 7, { 99, 45, 35, 70 }, { 99, 60, 40, 0 }, 2),
                 kOff, kOff, kOff, kOff }, 5, 5, "EMU BASS  "),
        voice ({ op (99, 1, 0, 7, { 99, 22, 18, 30 }, { 99, 60, 0, 0 }, 1), op (74, 3, 50, 7, { 99, 20, 18, 30 }, { 99, 50, 0, 0 }, 2),
                 op (80, 1, 0, 10, { 99, 25, 20, 30 }, { 99, 55, 0, 0 }, 1), op (60, 4, 0, 7, { 99, 25, 20, 30 }, { 99, 40, 0, 0 }, 2), kOff, kOff },
               5, 0, "EMU BELL  "),
        voice ({ op (99, 1, 0, 7, { 60, 40, 30, 55 }, { 99, 90, 88, 0 }, 1), op (80, 1, 0, 7, { 52, 35, 30, 55 }, { 99, 85, 80, 0 }, 2),
                 op (95, 1, 0, 9, { 62, 40, 30, 55 }, { 99, 90, 88, 0 }, 1), op (72, 1, 0, 5, { 55, 35, 30, 55 }, { 99, 85, 80, 0 }, 2), kOff, kOff },
               5, 6, "EMU BRASS "),
        voice ({ op (95, 1, 0, 4, { 40, 30, 30, 45 }, { 99, 95, 95, 0 }, 0), op (55, 1, 0, 7, { 40, 30, 30, 45 }, { 99, 90, 90, 0 }, 0),
                 op (95, 1, 0, 10, { 40, 30, 30, 45 }, { 99, 95, 95, 0 }, 0), op (55, 1, 0, 7, { 40, 30, 30, 45 }, { 99, 90, 90, 0 }, 0),
                 op (90, 2, 0, 7, { 40, 30, 30, 45 }, { 99, 95, 95, 0 }, 0), op (45, 1, 0, 7, { 40, 30, 30, 45 }, { 99, 90, 90, 0 }, 0) },
               5, 0, "EMU PAD   "),
        voice ({ op (99, 1, 0, 7, pluck, { 99, 70, 0, 0 }, 3), op (78, 3, 0, 7, { 99, 45, 35, 70 }, { 99, 40, 0, 0 }, 3), kOff, kOff, kOff, kOff },
               5, 4, "EMU CLAV  "),
        voice ({ op (99, 1, 0, 7, { 99, 40, 30, 50 }, { 99, 0, 0, 0 }, 2), op (70, 4, 0, 7, { 99, 60, 40, 60 }, { 99, 0, 0, 0 }, 3),
                 op (85, 4, 0, 7, { 99, 55, 40, 55 }, { 99, 0, 0, 0 }, 1), kOff, kOff, kOff }, 5, 0, "EMU MARIMB"),
        voice ({ op (99, 1, 0, 7, { 99, 99, 99, 70 }, { 99, 99, 99, 0 }, 0), op (92, 2, 0, 7, { 99, 99, 99, 70 }, { 99, 99, 99, 0 }, 0),
                 op (86, 4, 0, 7, { 99, 99, 99, 70 }, { 99, 99, 99, 0 }, 0), op (80, 8, 0, 7, { 99, 99, 99, 70 }, { 99, 99, 99, 0 }, 0), kOff, kOff },
               32, 0, "EMU ORGAN "),
    };
    const auto init = voice ({ op (99, 1, 0, 7, { 99, 99, 99, 99 }, { 99, 99, 99, 0 }, 0), kOff, kOff, kOff, kOff, kOff }, 1, 0, "INIT VOICE");
    while (bank.size() < 128)
        bank.push_back (init);
    return bank;
}

constexpr size_t kSlots = 128;
} // namespace

const UsrLayout UsrLayout::fm1 { 0x4000, 0x10000, 59, 0 };
const UsrLayout UsrLayout::smk37 { 0, 0x4000, 163, 128 };

const FwscHead* FwscUnpacked::entry (const std::string& name) const noexcept
{
    for (const auto& h : dir)
        if (h.name == name)
            return &h;
    return nullptr;
}

void fwscUnmask (uint8_t* buf, size_t n, uint16_t key) noexcept
{
    for (size_t i = 0; i < n; ++i)
    {
        buf[i] ^= (uint8_t) key;
        key = (uint16_t) ((key << 1) ^ ((key & 0x8000) != 0 ? 0x1021 : 0));
    }
}

uint16_t fwscCrc16 (const uint8_t* buf, size_t n) noexcept
{
    uint16_t crc = 0;
    for (size_t i = 0; i < n; ++i)
    {
        crc ^= (uint16_t) (buf[i] << 8);
        for (int k = 0; k < 8; ++k)
            crc = (uint16_t) ((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

size_t fwscContainer (const std::vector<uint8_t>& file, std::string& identity)
{
    const auto marker = [&file] (size_t i, uint8_t& m)
    {
        const auto at = i * 0x30 + 0x2f;
        if (at >= file.size()) return false;
        m = file[at];
        return true;
    };
    size_t n = 0;
    uint8_t m;
    while (marker (n, m))
    {
        if (n >= 20 && m != 0x7d)
            break;
        ++n;
    }
    // la identidad: los marcadores hasta la racha de 0x7D que cierra la cabecera
    size_t end = n;
    while (end > 0 && marker (end - 1, m) && m == 0x7d)
        --end;
    identity.clear();
    for (size_t i = 0; i < end; ++i)
        if (marker (i, m))
            identity.push_back ((char) (uint8_t) (m - (uint8_t) (i + 1)));
    return n;
}

bool fwscUnpack (const std::vector<uint8_t>& file, FwscUnpacked& out, std::string& error)
{
    std::string identity;
    const auto blocks = fwscContainer (file, identity);
    // (un byte de datos tras la cabecera puede leerse como 0x7D: la cabecera contada sale larga y nada cuadra; 20
    // bloques es el respaldo, como hacen los instaladores)
    bool ok = unpackN (file, blocks, out, error);
    if (! ok && blocks != 20)
        ok = unpackN (file, 20, out, error);
    if (ok)
        out.identity = identity;
    return ok;
}

std::vector<uint8_t> fwscFullFlash (const std::vector<uint8_t>& raw, size_t size)
{
    auto f = raw;
    f.resize (std::max (size, raw.size()), 0xff);
    return f;
}

bool usrFillDefaults (std::vector<uint8_t>& flash, size_t usr, const UsrLayout& layout)
{
    const auto v0 = usr + layout.voices, v1 = v0 + kSlots * 128;
    const auto s0 = usr + layout.settings, s1 = s0 + kSlots * layout.settingsLen + layout.tail;
    if (s1 > flash.size() || v1 > flash.size()
        || std::any_of (flash.begin() + (std::ptrdiff_t) v0, flash.begin() + (std::ptrdiff_t) v1, [] (uint8_t b) { return b != 0xff; }))
        return false;
    const auto bank = defaultVoices();
    for (size_t i = 0; i < bank.size(); ++i)
        std::copy (bank[i].begin(), bank[i].end(), flash.begin() + (std::ptrdiff_t) (v0 + i * 128));
    std::fill (flash.begin() + (std::ptrdiff_t) s0, flash.begin() + (std::ptrdiff_t) s1, (uint8_t) 0);
    return true;
}

int usrLoadSyx (std::vector<uint8_t>& flash, size_t usr, const UsrLayout& layout, const std::vector<uint8_t>& syx, size_t first,
                std::string& error)
{
    const uint8_t* data;
    if (syx.size() >= 4104 && syx[0] == 0xf0 && syx[1] == 0x43 && syx[3] == 0x09)
        data = syx.data() + 6;
    else if (syx.size() == 4096)
        data = syx.data();
    else
    {
        error = "not a DX7 32-voice bank (4104-byte sysex or 4096 bytes)";
        return -1;
    }
    const auto n = std::min<size_t> (32, kSlots > first ? kSlots - first : 0);
    const auto at = usr + layout.voices + first * 128;
    const auto s = usr + layout.settings + first * layout.settingsLen;
    if (at + n * 128 > flash.size() || s + n * layout.settingsLen > flash.size())
    {
        error = "USR area past the flash";
        return -1;
    }
    std::memcpy (&flash[at], data, n * 128);
    for (size_t i = s; i < s + n * layout.settingsLen; ++i)
        if (flash[i] == 0xff)
            flash[i] = 0;
    return (int) n;
}
} // namespace fm1

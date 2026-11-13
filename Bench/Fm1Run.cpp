// Arranca un firmware del M-VAVE FM-1 (.fwsc) en el emulador de Source/Fm1, sin interfaz. Sin JUCE.
//
// Uso: KoniFm1Run <paquete.fwsc> [opciones]
//   --ms N              milisegundos de máquina a ejecutar (3000)
//   --key MS:NOMBRE:D   pulsa (D=1) o suelta (D=0) una tecla en el ms dado: OCTDN OCTUP FX SCL ENV LFO EDIT GLO HOME SAVE
//                       ARP SEQ PLAY REC, una nota (C4, F#3, Bb4; de F3 a G5) o K0..K40
//   --enc MS:NOMBRE:N   gira un encoder N clics (+ horario): SELECT ALGO K1 K2 K3 K4 PRESET
//   --midi MS:HEX       bytes MIDI al jack MIDI IN en el ms dado (p. ej. 1000:903c64)
//   --vel V             velocidad de las teclas del panel (1..127, 100)
//   --shot MS:FICHERO   guarda la pantalla (BMP 240×240) en ese ms; se puede repetir
//   --wav FICHERO       guarda el audio (WAV estéreo de 16 bits)
//   --syx FICHERO       un banco DX7 de 32 voces en el área USR
//   --profile N         las N clases de instrucción más ejecutadas por cpu0
//   --reference 1       sin la ruta rápida de la CPU ni el salto de bucles (para la prueba diferencial: el hash del estado final debe coincidir)
// El WAV se escribe a 44 118 Hz: el FM-1 saca tramas a 24 MHz / 544 = 44 117,65 Hz (el entero más cercano; error 0,008 ct).
#include "../Source/Fm1/Fm1Soc.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace fm1;

namespace
{
std::vector<uint8_t> readFile (const char* path)
{
    std::ifstream in (path, std::ios::binary);
    return std::vector<uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

void put16 (std::vector<uint8_t>& v, uint32_t x) { v.push_back ((uint8_t) x); v.push_back ((uint8_t) (x >> 8)); }
void put32 (std::vector<uint8_t>& v, uint32_t x) { put16 (v, x & 0xffff); put16 (v, x >> 16); }

bool writeBmp (const std::string& path, const Lcd& lcd)
{
    std::vector<uint8_t> f;
    const uint32_t row = Lcd::W * 3, size = 54 + row * Lcd::H;  // 720 bytes por fila: sin relleno
    f.push_back ('B'); f.push_back ('M');
    put32 (f, size); put32 (f, 0); put32 (f, 54); put32 (f, 40); put32 (f, Lcd::W); put32 (f, Lcd::H);
    put16 (f, 1); put16 (f, 24); put32 (f, 0); put32 (f, row * Lcd::H); put32 (f, 0); put32 (f, 0); put32 (f, 0); put32 (f, 0);
    for (int y = Lcd::H - 1; y >= 0; --y)
        for (int x = 0; x < Lcd::W; ++x)
        {
            uint8_t r, g, b;
            rgb565To888 (lcd.fb[(size_t) y * Lcd::W + (size_t) x], r, g, b);
            f.push_back (b); f.push_back (g); f.push_back (r);
        }
    std::ofstream out (path, std::ios::binary);
    out.write ((const char*) f.data(), (std::streamsize) f.size());
    return (bool) out;
}

bool writeWav (const std::string& path, const std::vector<int16_t>& s, uint32_t rate)
{
    std::vector<uint8_t> f;
    const auto bytes = (uint32_t) (s.size() * 2);
    f.insert (f.end(), { 'R', 'I', 'F', 'F' }); put32 (f, 36 + bytes);
    f.insert (f.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' }); put32 (f, 16);
    put16 (f, 1); put16 (f, 2); put32 (f, rate); put32 (f, rate * 4); put16 (f, 4); put16 (f, 16);
    f.insert (f.end(), { 'd', 'a', 't', 'a' }); put32 (f, bytes);
    for (auto x : s) put16 (f, (uint16_t) x);
    std::ofstream out (path, std::ios::binary);
    out.write ((const char*) f.data(), (std::streamsize) f.size());
    return (bool) out;
}

bool parseHexBytes (const char* s, std::vector<uint8_t>& out)
{
    const auto n = std::strlen (s);
    if (n == 0 || n % 2 != 0) return false;
    for (size_t i = 0; i < n; i += 2)
    {
        unsigned v;
        if (std::sscanf (s + i, "%2x", &v) != 1) return false;
        out.push_back ((uint8_t) v);
    }
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("uso: KoniFm1Run <paquete.fwsc> [--ms N] [--key MS:NOMBRE:D] [--enc MS:NOMBRE:N] [--midi MS:HEX] [--vel V]\n"
                     "                 [--shot MS:FICHERO] [--wav FICHERO] [--syx FICHERO]\n");
        return 2;
    }
    uint64_t ms = 3000;
    std::vector<std::pair<uint64_t, PanelAction>> actions;
    std::vector<std::pair<uint64_t, std::string>> shots;
    std::string wav;
    bool reference = false;  // --reference 1: sin la ruta rápida de la CPU (prueba diferencial)
    int profile = 0;  // --profile N: las N clases de instrucción más ejecutadas por cpu0
    std::vector<uint8_t> syx;
    bool haveSyx = false;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string opt = argv[i];
        const char* v = argv[i + 1];
        char name[64] = {};
        unsigned long long at = 0;
        int d = 0;
        if (opt == "--ms") ms = std::strtoull (v, nullptr, 10);
        else if (opt == "--vel") actions.push_back ({ 0, { PanelAction::KeyVel, 0, std::atoi (v) } });
        else if (opt == "--profile") profile = std::atoi (v);
        else if (opt == "--reference") reference = std::atoi (v) != 0;
        else if (opt == "--wav") wav = v;
        else if (opt == "--syx") { syx = readFile (v); haveSyx = true; }
        else if (opt == "--key" && std::sscanf (v, "%llu:%63[^:]:%d", &at, name, &d) == 3)
        {
            const auto id = fm1KeyId (name);
            if (! id) { std::printf ("tecla desconocida: %s\n", name); return 2; }
            actions.push_back ({ at, { PanelAction::Key, *id, d } });
        }
        else if (opt == "--enc" && std::sscanf (v, "%llu:%63[^:]:%d", &at, name, &d) == 3)
        {
            const auto id = fm1EncId (name);
            if (! id) { std::printf ("encoder desconocido: %s\n", name); return 2; }
            actions.push_back ({ at, { PanelAction::Enc, *id, d } });
        }
        else if (opt == "--midi" && std::sscanf (v, "%llu:%63s", &at, name) == 2)
        {
            std::vector<uint8_t> bytes;
            if (! parseHexBytes (name, bytes)) { std::printf ("MIDI no válido: %s\n", v); return 2; }
            for (auto b : bytes) actions.push_back ({ at, { PanelAction::Midi, 0, b } });
        }
        else if (opt == "--shot" && std::sscanf (v, "%llu:", &at) == 1 && std::strchr (v, ':') != nullptr)
            shots.push_back ({ at, std::strchr (v, ':') + 1 });
        else { std::printf ("opción no válida: %s %s\n", opt.c_str(), v); return 2; }
    }

    Stock st;
    std::string err;
    if (! stockFromPackage (readFile (argv[1]), haveSyx ? &syx : nullptr, st, err))
    {
        std::printf ("%s: %s\n", argv[1], err.c_str());
        return 1;
    }
    Soc soc (kRamSize, st.xip);
    soc.periph.skipIdle = true;  // como fm1-live: el tiempo salta los bucles de espera de TIMER4 e idle
    Cpu cpu (st.entry);
    cpu.user = true;  // el SPL entrega cpu0 en modo usuario
    const uint32_t params = kRamBase + 0x7fc00;
    soc.write32 (params, params + 0x40);
    cpu.regs[0] = stockBoot (soc, st.flash, params);
    soc.sfcBoot (st.key, st.appEnd);
    std::stable_sort (actions.begin(), actions.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [t, a] : actions)
        soc.periph.script.push_back ({ soc.periph.cycleOfMs (t), a });
    std::sort (shots.begin(), shots.end());

    std::printf ("%s: \"%s\", entrada 0x%08x, XIP %zu B, clave 0x%04x\n", argv[1], st.identity.c_str(), st.entry, st.xip.size(), st.key);
    std::unordered_map<const Fm1IsaEntry*, uint64_t> hist;
    if (profile > 0) cpu.hist = &hist;
    cpu.fastPath = ! reference;
    Machine m (soc, cpu);
    m.fastPath = ! reference;
    m.loopSkip.enabled = ! reference;
    const auto t0 = std::chrono::steady_clock::now();
    size_t nextShot = 0;
    bool ok = true;
    while (soc.periph.ms() < ms)
    {
        if (! m.run (65536, err))
        {
            std::printf ("PARADA en %llu ms: %s\n", (unsigned long long) soc.periph.ms(), err.c_str());
            ok = false;
            break;
        }
        while (nextShot < shots.size() && soc.periph.ms() >= shots[nextShot].first)
        {
            writeBmp (shots[nextShot].second, soc.lcd);
            std::printf ("  pantalla en %llu ms -> %s\n", (unsigned long long) soc.periph.ms(), shots[nextShot].second.c_str());
            ++nextShot;
        }
    }
    for (; nextShot < shots.size(); ++nextShot)
        writeBmp (shots[nextShot].second, soc.lcd);
    const auto wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    std::printf ("máquina %llu ms (%.2f s reales, %.3f× tiempo real); cpu0 %llu instrucciones, cpu1 %s %llu; saltado por idle %.1f %%\n",
                 (unsigned long long) soc.periph.ms(), wall, (double) soc.periph.ms() / 1000.0 / wall, (unsigned long long) cpu.insnCount,
                 m.cpu1 ? "en marcha," : "sin arrancar,", (unsigned long long) (m.cpu1 ? m.cpu1->insnCount : 0),
                 100.0 * (double) soc.periph.skipped / (double) std::max<uint64_t> (soc.periph.cycles, 1));
    std::printf ("salto de bucles: %llu sondeos, %llu con bucle, %llu instrucciones saltadas\n", (unsigned long long) m.loopSkip.probes,
                 (unsigned long long) m.loopSkip.hits, (unsigned long long) m.loopSkip.skipped);
    std::printf ("pantalla: encendida %d, %llu ventanas, revisión %u; audio %zu tramas (%.2f s); resets %u\n", soc.lcd.displayOn ? 1 : 0,
                 (unsigned long long) soc.lcd.frames, soc.lcd.revision, soc.periph.audio.size() / 2,
                 (double) soc.periph.audio.size() / 2 / 44117.65, soc.resets);
    // accesos desconocidos por dirección (la hoja de ruta de lo que falta)
    std::map<std::pair<uint32_t, bool>, size_t> unk;
    for (const auto& u : soc.unknownAccesses())
        ++unk[{ u.addr & ~3u, u.isWrite }];
    std::vector<std::pair<size_t, std::pair<uint32_t, bool>>> top;
    for (const auto& [k, c] : unk) top.push_back ({ c, k });
    std::sort (top.rbegin(), top.rend());
    std::printf ("accesos desconocidos (últimos %zu, %zu direcciones):\n", soc.unknownAccesses().size(), top.size());
    for (size_t i = 0; i < std::min<size_t> (top.size(), 24); ++i)
        std::printf ("  %8zu  %s 0x%08x\n", top[i].first, top[i].second.second ? "esc" : "lee", top[i].second.first);
    if (profile > 0)
    {
        std::vector<std::pair<uint64_t, const Fm1IsaEntry*>> h;
        uint64_t total = 0;
        for (const auto& [e, c] : hist) { h.push_back ({ c, e }); total += c; }
        std::sort (h.rbegin(), h.rend());
        std::printf ("perfil de cpu0 (%llu instrucciones, %zu clases; ruta aproximada):\n", (unsigned long long) total, h.size());
        double acc = 0;
        for (size_t i = 0; i < std::min<size_t> (h.size(), (size_t) profile); ++i)
        {
            Fm1Pre pre;
            computePre (*h[i].second, 0, pre);
            acc += 100.0 * (double) h[i].first / (double) total;
            std::printf ("  %6.2f %%  (acum %6.2f %%)  ruta %2u%s %-28s %s\n", 100.0 * (double) h[i].first / (double) total, acc, pre.route, pre.fast != Fm1Pre::FastNone ? "*" : " ",
                         h[i].second->name, h[i].second->syntax);
        }
    }
    {
        // hash FNV-1a del estado final: RAM, registros de los dos núcleos, pantalla y audio (prueba diferencial)
        uint64_t hsh = 0xcbf29ce484222325ull;
        const auto mix = [&hsh] (const void* p, size_t n)
        {
            const auto* b = (const uint8_t*) p;
            for (size_t i = 0; i < n; ++i) { hsh ^= b[i]; hsh *= 0x100000001b3ull; }
        };
        mix (soc.ram.ptr(), soc.ram.size());
        mix (cpu.regs, sizeof cpu.regs);
        mix (&cpu.pc, 4);
        mix (&cpu.sp, 4);
        if (m.cpu1) { mix (m.cpu1->regs, sizeof m.cpu1->regs); mix (&m.cpu1->pc, 4); }
        mix (soc.lcd.fb.data(), soc.lcd.fb.size() * 2);
        mix (soc.periph.audio.data(), soc.periph.audio.size() * 2);
        std::printf ("hash del estado: %016llx (%s)\n", (unsigned long long) hsh, reference ? "referencia" : "ruta rápida");
    }
    if (! soc.console.empty())
        std::printf ("consola: %s\n", soc.consoleString().c_str());
    if (! wav.empty())
        std::printf ("%s %s\n", writeWav (wav, soc.periph.audio, 44118) ? "WAV ->" : "no se pudo escribir", wav.c_str());
    return ok ? 0 : 1;
}

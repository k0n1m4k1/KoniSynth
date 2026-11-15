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
//   --pcprofile N       los N pc de cpu0 más ejecutados, con su instrucción (incluye las de los sondeos del salto de bucles)
//   --jitprofile N      las N clases de micro-op que el JIT de cpu0 hace con jitOp (las que no tiene en nativo)
//   --stepprofile N     los N pc de cpu0 que la ráfaga interpreta en vez de usar el JIT, con el motivo
//   --pcprofile1 N      lo mismo para cpu1 (los dos con el mismo formato, cpu1 detrás)
//   --hostprofile N    perfil del propio emulador por muestreo (Windows; necesita el PDB): N funciones y líneas
//   --reference 1       sin la ruta rápida de la CPU ni el salto de bucles (para la prueba diferencial: el hash del estado final debe coincidir)
//   --chunk N           instrucciones de cpu0 por llamada a Machine::run (65536; el hash depende de él: comparar con el mismo)
// El WAV se escribe a 44 118 Hz: el FM-1 saca tramas a 24 MHz / 544 = 44 117,65 Hz (el entero más cercano; error 0,008 ct).
#include "../Source/Fm1/Fm1Soc.h"

#ifdef _WIN32
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <dbghelp.h>
 #include <timeapi.h>
 #pragma comment (lib, "dbghelp.lib")
 #pragma comment (lib, "winmm.lib")
#endif

#include <algorithm>
#include <atomic>
#include <thread>
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

#ifdef _WIN32
// Perfil del propio emulador (--hostprofile): un hilo para el hilo principal cada ~1 ms y apunta su RIP; al final se
// resuelve con dbghelp contra el PDB (función y fichero:línea). Sin elevación ni herramientas externas.
class HostSampler
{
public:
    void start()
    {
        DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS);
        timeBeginPeriod (1);
        worker = std::thread ([this]
        {
            while (! stop.load())
            {
                Sleep (1);
                if (SuspendThread (target) == (DWORD) -1) continue;
                CONTEXT c {};
                c.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext (target, &c)) rips.push_back (c.Rip);
                ResumeThread (target);
            }
        });
    }
    void report (int top)
    {
        stop = true;
        worker.join();
        timeEndPeriod (1);
        const auto proc = GetCurrentProcess();
        SymSetOptions (SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize (proc, nullptr, TRUE);
        std::map<std::string, uint64_t> byFn, byLine;
        alignas (SYMBOL_INFO) char buf[sizeof (SYMBOL_INFO) + 512];
        for (const auto rip : rips)
        {
            auto* s = reinterpret_cast<SYMBOL_INFO*> (buf);
            s->SizeOfStruct = sizeof (SYMBOL_INFO);
            s->MaxNameLen = 511;
            DWORD64 d64 = 0;
            ++byFn[SymFromAddr (proc, rip, &d64, s) ? std::string (s->Name) : "?"];
            IMAGEHLP_LINE64 ln {};
            ln.SizeOfStruct = sizeof ln;
            DWORD d = 0;
            if (SymGetLineFromAddr64 (proc, rip, &d, &ln))
            {
                std::string f = ln.FileName;
                if (const auto k = f.find_last_of ("\\/"); k != std::string::npos) f = f.substr (k + 1);
                ++byLine[f + ":" + std::to_string (ln.LineNumber)];
            }
            else
                ++byLine["?"];
        }
        SymCleanup (proc);
        const auto print = [&] (const char* title, const std::map<std::string, uint64_t>& m)
        {
            std::vector<std::pair<uint64_t, std::string>> v;
            for (const auto& [k, n] : m) v.push_back ({ n, k });
            std::sort (v.rbegin(), v.rend());
            std::printf ("%s (%zu muestras):\n", title, rips.size());
            double acc = 0;
            for (size_t i = 0; i < std::min<size_t> (v.size(), (size_t) top); ++i)
            {
                const double p = 100.0 * (double) v[i].first / (double) std::max<size_t> (rips.size(), 1);
                acc += p;
                std::printf ("  %6.2f %%  (acum %6.2f %%)  %s\n", p, acc, v[i].second.c_str());
            }
        };
        print ("perfil del emulador por función", byFn);
        print ("perfil del emulador por línea", byLine);
    }
    ~HostSampler() { if (target != nullptr) CloseHandle (target); }

private:
    HANDLE target = nullptr;
    std::thread worker;
    std::atomic<bool> stop { false };
    std::vector<DWORD64> rips;
};
#endif
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
    int pcProfile1 = 0;  // --pcprofile1 N: lo mismo para cpu1
    uint32_t disIni = 0, disFin = 0;  // --disasm INI:FIN (hex): desensamblado con el valor de cada ranura (registros, inmediatos)
    int pcProfile = 0;  // --pcprofile N: los N pc de cpu0 más ejecutados
    int jitProfile = 0;  // --jitprofile N: las N clases de micro-op por jitOp
    int stepProfile = 0;  // --stepprofile N: los N pc interpretados por la ráfaga
    std::unordered_map<uint32_t, uint64_t> stepHist;
    std::unordered_map<uint32_t, uint64_t> jitOpHist;
    int hostProfile = 0;  // --hostprofile N: las N funciones y líneas del emulador con más muestras (Windows)
    std::vector<uint8_t> syx;
    bool haveSyx = false;
    // --dumpcpu1 FICHERO con --dumpaddr ÍNDICE:BÚFER (hexadecimal): cada bloque de 64 float que deja el núcleo de sonido
    // (cpu1) en su búfer doble, para la prueba diferencial. ÍNDICE: el byte del búfer en uso; búferes en BÚFER + 256·índice
    std::string dumpCpu1;
    uint32_t probeMax = 0;
    uint32_t chunk = 65536;  // --chunk N: instrucciones de cpu0 por llamada a Machine::run (el plugin: Fm1Engine::kChunk)
    bool loopTimer = true;
    bool observeOn = true;
    bool jitOn = true;  // --jit off: sin el JIT (Cpu::jitOn)
    bool timerSkip = true;  // --timerskip off: las vueltas con contador se comprueban en sombra pero no se saltan  // --observe off: sin vueltas observadas (Machine::Observe)
    uint32_t forcePc = 0xffffffffu;  // --probeat PC (hex): sondear siempre ahí (diagnóstico)  // --looptimer off: el salto de bucles no admite vueltas que leen el contador del TIMER4  // --probemax N: lo más que sigue un sondeo del salto de bucles (0: el de serie)
    uint32_t dumpIdx = 0, dumpBuf = 0;
    std::vector<std::pair<uint64_t, std::string>> ramDumps;  // --dumpram MS:FICHERO: la RAM entera en ese ms
    // --hle off|check|on: FmCore::render nativo (Fm1Hle.h); check lo compara con la emulada en cada llamada
    auto hleMode = Machine::Hle::Off;
    // --ccpoll off|check|on|auto: la pasada vacía de midi_cc_apply de GHOULBOX, saltada al mismo ciclo (Machine::CcPoll)
    auto ccMode = Machine::CcPoll::Off;
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
        else if (opt == "--hostprofile") hostProfile = std::atoi (v);
        else if (opt == "--pcprofile") pcProfile = std::atoi (v);
        else if (opt == "--jitprofile") jitProfile = std::atoi (v);
        else if (opt == "--stepprofile") stepProfile = std::atoi (v);
        else if (opt == "--pcprofile1") pcProfile1 = std::atoi (v);
        else if (opt == "--disasm" && std::sscanf (v, "%x:%x", &disIni, &disFin) == 2) {}
        else if (opt == "--wav") wav = v;
        else if (opt == "--syx") { syx = readFile (v); haveSyx = true; }
        else if (opt == "--hle")
            hleMode = std::string (v) == "on" ? Machine::Hle::On : std::string (v) == "check" ? Machine::Hle::Check
                    : std::string (v) == "auto" ? Machine::Hle::Auto : Machine::Hle::Off;
        else if (opt == "--ccpoll")
            ccMode = std::string (v) == "on" ? Machine::CcPoll::On : std::string (v) == "check" ? Machine::CcPoll::Check
                   : std::string (v) == "auto" ? Machine::CcPoll::Auto : Machine::CcPoll::Off;
        else if (opt == "--dumpcpu1") dumpCpu1 = v;
        else if (opt == "--probemax") probeMax = (uint32_t) std::strtoul (v, nullptr, 10);
        else if (opt == "--chunk") chunk = std::max (1ul, std::strtoul (v, nullptr, 10));
        else if (opt == "--looptimer") loopTimer = std::string (v) != "off";
        else if (opt == "--timerskip") timerSkip = std::string (v) != "off";
        else if (opt == "--jit") jitOn = std::string (v) != "off";
        else if (opt == "--observe") observeOn = std::string (v) != "off";
        else if (opt == "--probeat") forcePc = (uint32_t) std::strtoul (v, nullptr, 16);
        else if (opt == "--dumpaddr" && std::sscanf (v, "%x:%x", &dumpIdx, &dumpBuf) == 2) {}
        else if (opt == "--dumpram" && std::sscanf (v, "%llu:", &at) == 1 && std::strchr (v, ':') != nullptr)
            ramDumps.push_back ({ at, std::strchr (v, ':') + 1 });
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
    if (jitProfile > 0) cpu.jitOpHist = &jitOpHist;
    if (stepProfile > 0) cpu.stepHist = &stepHist;
    std::unordered_map<const Fm1IsaEntry*, uint64_t> synHist;  // los dos núcleos
    if (profile > 0) cpu.synHist = &synHist;
    std::unordered_map<uint32_t, uint64_t> pcHist;
    if (pcProfile > 0) cpu.pcHist = &pcHist;
    std::unordered_map<uint32_t, uint64_t> pcHist1;  // cpu1, con --pcprofile1
    cpu.fastPath = ! reference;
    Machine m (soc, cpu);
    m.fastPath = ! reference;
    if (profile > 0) m.synHist1 = &synHist;
    if (pcProfile1 > 0) m.pcHist1 = &pcHist1;
    m.loopSkip.enabled = ! reference;
    std::unordered_map<uint32_t, uint64_t> badAt;
    m.loopSkip.badAt = &badAt;
    if (probeMax > 0)
        m.loopSkip.probeMax = probeMax;
    m.loopSkip.timerOk = loopTimer;
    m.loopSkip.forcePc = forcePc;
    m.observe.enabled = observeOn;
    m.loopSkip.timerSkip = timerSkip;
    cpu.jitOn = jitOn;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, std::pair<uint64_t, uint64_t>> hitsAt;
    m.loopSkip.hitsAt = &hitsAt;
    m.hle.mode = hleMode;
    m.ccPoll.mode = ccMode;
#ifdef _WIN32
    HostSampler sampler;
    if (hostProfile > 0) sampler.start();
#endif
    const auto t0 = std::chrono::steady_clock::now();
    size_t nextShot = 0, nextRam = 0;
    std::sort (ramDumps.begin(), ramDumps.end());
    if (! dumpCpu1.empty() && dumpIdx == 0)
    {
        std::printf ("--dumpcpu1 necesita --dumpaddr ÍNDICE:BÚFER\n");
        return 2;
    }
    std::FILE* dump = dumpCpu1.empty() ? nullptr : std::fopen (dumpCpu1.c_str(), "wb");
    const auto ramAt = [&soc] (uint32_t a) { return soc.ram.ptr() + (a - kRamBase); };
    uint8_t lastIdx = dump != nullptr ? *ramAt (dumpIdx) : 0;
    uint64_t blocks = 0, skippedBlocks = 0;
    bool ok = true;
    uint64_t maxStep = 0;  // el mayor avance del reloj en una llamada a Machine::run (ciclos de CPU)
    // Tramos de 50 ms de máquina (a partir de 1 s, pasado el arranque): su velocidad (máquina / real). Los cortes del
    // plugin los dan los picos, no la media: con la cola de 30 ms, un tramo por debajo de 1x más largo que la cola calla
    constexpr uint64_t kWinMs = 50;
    auto winWall = std::chrono::steady_clock::now();
    uint64_t winStart = 0;
    double worstSpeed = 1e9;
    uint64_t worstAt = 0, wins = 0, winsSlow = 0;
    // la cola que haría falta para no callar: el mayor retraso acumulado de la máquina respecto al reloj real desde su
    // mejor adelanto (lag = real - máquina; caída máxima). Con la máquina más rápida de media, la cola se rellena entre picos
    double lag = 0.0, lagMin = 0.0, needMs = 0.0;
    while (soc.periph.ms() < ms)
    {
        if (const auto now = soc.periph.ms(); now >= winStart + kWinMs)
        {
            const auto wallNow = std::chrono::steady_clock::now();
            if (winStart >= 1000)
            {
                const auto speed = (double) (now - winStart) / 1000.0 / std::chrono::duration<double> (wallNow - winWall).count();
                ++wins;
                if (speed < 1.0) ++winsSlow;
                if (speed < worstSpeed) { worstSpeed = speed; worstAt = winStart; }
                // (con la cola acotada a L y llena al empezar, calla si y solo si la caída supera L: recurrencia de Lindley)
                lag += 1000.0 * std::chrono::duration<double> (wallNow - winWall).count() - (double) (now - winStart);
                lagMin = std::min (lagMin, lag);
                needMs = std::max (needMs, lag - lagMin);
            }
            winStart = now;
            winWall = wallNow;
        }
        // con volcado, tramos de 2048 ciclos: un bloque de cpu1 son ~11 000 instrucciones (estimado), así que entre dos
        // lecturas del índice no caben dos bloques; si el índice no cambia en ±1 se cuenta como bloque perdido
        const auto before = soc.periph.cycles;
        if (! m.run (dump != nullptr ? 2048 : chunk, err))
        {
            std::printf ("PARADA en %llu ms: %s\n", (unsigned long long) soc.periph.ms(), err.c_str());
            ok = false;
            break;
        }
        maxStep = std::max (maxStep, soc.periph.cycles - before);
        // el firmware hace índice + 1 y, si llega a 2, lo pone a 0: el 2 es transitorio y no marca un bloque
        if (dump != nullptr && *ramAt (dumpIdx) != lastIdx && *ramAt (dumpIdx) < 2)
        {
            // registro (268 B): tramas de DAC ya emitidas (uint64), índice del búfer escrito (uint32), sus 64 float
            const uint8_t idx = *ramAt (dumpIdx);
            if (((lastIdx + 1) & 1) != idx) ++skippedBlocks;
            const uint64_t frames = soc.periph.audio.size() / 2;
            const uint32_t which = lastIdx;
            std::fwrite (&frames, 8, 1, dump);
            std::fwrite (&which, 4, 1, dump);
            std::fwrite (ramAt (dumpBuf + 256u * which), 1, 256, dump);
            lastIdx = idx;
            ++blocks;
        }
        while (nextRam < ramDumps.size() && soc.periph.ms() >= ramDumps[nextRam].first)
        {
            if (std::FILE* f = std::fopen (ramDumps[nextRam].second.c_str(), "wb"))
            {
                std::fwrite (soc.ram.ptr(), 1, soc.ram.size(), f);
                std::fclose (f);
            }
            std::printf ("  RAM en %llu ms -> %s\n", (unsigned long long) soc.periph.ms(), ramDumps[nextRam].second.c_str());
            ++nextRam;
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
    if (dump != nullptr)
    {
        std::fclose (dump);
        std::printf ("cpu1: %llu bloques -> %s (índice fuera de secuencia %llu veces)\n", (unsigned long long) blocks, dumpCpu1.c_str(),
                     (unsigned long long) skippedBlocks);
    }
    const auto wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
#ifdef _WIN32
    if (hostProfile > 0) sampler.report (hostProfile);
#endif

    std::printf ("máquina %llu ms (%.2f s reales, %.3f× tiempo real); cpu0 %llu instrucciones, cpu1 %s %llu; saltado por idle %.1f %%\n",
                 (unsigned long long) soc.periph.ms(), wall, (double) soc.periph.ms() / 1000.0 / wall, (unsigned long long) cpu.insnCount,
                 m.cpu1 ? "en marcha," : "sin arrancar,", (unsigned long long) (m.cpu1 ? m.cpu1->insnCount : 0),
                 100.0 * (double) soc.periph.skipped / (double) std::max<uint64_t> (soc.periph.cycles, 1));
    std::printf ("paso de %u instrucciones: el mayor avance de una llamada, %.3f ms de máquina\n", chunk,
                 1000.0 * (double) maxStep / (double) kCpuHz);
    if (wins > 0)
        std::printf ("tramos de %llu ms (desde 1 s): el más lento %.3fx en el ms %llu; por debajo de 1x %llu de %llu\n",
                     (unsigned long long) kWinMs, worstSpeed, (unsigned long long) worstAt, (unsigned long long) winsSlow,
                     (unsigned long long) wins);
    if (wins > 0)
        std::printf ("cola necesaria sin cortes (mayor retraso acumulado desde 1 s): %.1f ms (el plugin: 30 ms)\n", needMs);
    if (profile > 0)
    {
        uint64_t tot = 0;
        for (const auto c : cpu.routeCount) tot += c;
        std::printf ("cpu0 por ruta (de %llu; 255 = micro-op): ", (unsigned long long) tot);
        for (int r = 0; r < 256; ++r)
            if (cpu.routeCount[r] * 200 >= tot && tot > 0)
                std::printf ("%d %.1f %%  ", r, 100.0 * (double) cpu.routeCount[r] / (double) tot);
        std::printf ("; cabezas de par %.1f %%\n", tot > 0 ? 100.0 * (double) cpu.pairHeads / (double) tot : 0.0);
    }
    std::printf ("JIT: cpu0 %llu bloques ejecutados, %llu instrucciones, %llu compilados, %llu vaciados de RAM, %llu del búfer, %llu enlaces\n",
                 (unsigned long long) cpu.jitRuns, (unsigned long long) cpu.jitInsns, (unsigned long long) cpu.jitCompiled,
                 (unsigned long long) cpu.jitRamFlushes, (unsigned long long) cpu.jitResets, (unsigned long long) cpu.jitLinked);
    {
        // por qué volvió la cadena del JIT a la ráfaga (Cpu::jitExits, JitCtx::why): los motivos con más del 1 %
        static const char* const kFast[] = { "None", "Ld", "St", "BrI", "BrR", "AluI", "AluR", "Mov", "MovI", "Goto", "Call",
            "Rts", "Part", "LdX", "StX", "SpAdd", "SpPlus", "Cnum", "Field", "Adc", "Ie", "AndNot", "Shift", "DecBr", "PopPc",
            "IcfgRd", "IcfgWr", "Div", "Abs", "Not", "Tb", "CallR", "Rti", "PushList", "PopList", "PushRets", "PopRetPc",
            "BitBr", "Nop", "ExecRaw" };
        uint64_t tot = 0;
        for (const auto x : cpu.jitExits) tot += x;
        std::printf ("salidas del JIT a la ráfaga (cpu0, %llu):", (unsigned long long) tot);
        for (int w = 0; w < 64; ++w)
        {
            if (cpu.jitExits[w] * 100 < tot || cpu.jitExits[w] == 0)
                continue;
            const char* name = w == 0 ? "sin motivo" : w == 1 ? "presupuesto" : w == 2 ? "enlace" : w == 3 ? "itab"
                             : w == 6 ? "if sin cumplirse" : w == 7 ? "RAM reescrita" : w == 9 ? "acceso con efectos"
                             : w == 10 ? "fallo" : w == 11 ? "ventanas" : w == 12 ? "stop" : w == 13 ? "avoid"
                             : w == 14 ? "loopHint" : w == 15 ? "tabla de bucles" : nullptr;
            char buf[32];
            if (name == nullptr)
            {
                std::snprintf (buf, sizeof buf, "fin %s", w >= 16 && (size_t) (w - 16) < std::size (kFast) ? kFast[w - 16] : "?");
                name = buf;
            }
            std::printf ("  %s %.1f %%", name, 100.0 * (double) cpu.jitExits[w] / (double) tot);
        }
        std::printf ("\n");
    }
    if (m.cpu1)
        std::printf ("JIT: cpu1 %llu bloques ejecutados, %llu instrucciones, %llu compilados, %llu vaciados de RAM, %llu del búfer\n",
                     (unsigned long long) m.cpu1->jitRuns, (unsigned long long) m.cpu1->jitInsns, (unsigned long long) m.cpu1->jitCompiled,
                     (unsigned long long) m.cpu1->jitRamFlushes, (unsigned long long) m.cpu1->jitResets);
    std::printf ("salto de bucles: %llu sondeos, %llu con bucle, %llu instrucciones saltadas\n", (unsigned long long) m.loopSkip.probes,
                 (unsigned long long) m.loopSkip.hits, (unsigned long long) m.loopSkip.skipped);
    std::printf ("  con el contador del TIMER4: %llu saltos; %llu vueltas comprobadas en sombra, %llu rechazadas\n",
                 (unsigned long long) m.loopSkip.timerHits, (unsigned long long) m.loopSkip.timerVerified,
                 (unsigned long long) m.loopSkip.timerRejected);
    std::printf ("  ciclos hasta el siguiente evento al sondear: <2k %llu, <8k %llu, <32k %llu, <128k %llu, más %llu\n",
                 (unsigned long long) m.loopSkip.roomHist[0], (unsigned long long) m.loopSkip.roomHist[1], (unsigned long long) m.loopSkip.roomHist[2],
                 (unsigned long long) m.loopSkip.roomHist[3], (unsigned long long) m.loopSkip.roomHist[4]);
    {
        std::vector<std::pair<uint64_t, std::tuple<uint32_t, uint32_t, uint32_t>>> hits;
        for (const auto& [key, c] : hitsAt)
            hits.push_back ({ c.first, key });
        std::sort (hits.rbegin(), hits.rend());
        for (size_t q = 0; q < hits.size() && q < 8; ++q)
        {
            const auto& [pc, len, reads] = hits[q].second;
            std::printf ("    vuelta en 0x%08x: %u instrucciones, %u lecturas del contador; %llu veces, %llu vueltas saltadas\n", pc, len, reads,
                         (unsigned long long) hits[q].first, (unsigned long long) hitsAt[hits[q].second].second);
        }
    }
    std::printf ("  vueltas observadas: %zu conocidas, %llu observadas, %llu con salto, %llu distintas, %llu instrucciones saltadas\n",
                 m.observe.known.size(), (unsigned long long) m.observe.rounds, (unsigned long long) m.observe.hits,
                 (unsigned long long) m.observe.misses, (unsigned long long) m.observe.skipped);
    for (const auto& kn : m.observe.known)
        std::printf ("    0x%08x: %u instrucciones, %u lecturas del contador, %u escrituras, registros muertos 0x%04x%s\n", kn.pc0, kn.len,
                     kn.reads, kn.writes, kn.deadRegs, kn.deadFlags ? " y banderas" : "");
    std::printf ("  sondeos fallidos: %llu por un acceso, %llu sin vuelta, %llu por un evento, %llu por una interrupción\n",
                 (unsigned long long) m.loopSkip.failBad, (unsigned long long) m.loopSkip.failNoRound,
                 (unsigned long long) m.loopSkip.failEvent, (unsigned long long) m.loopSkip.failIrq);
    {
        std::vector<std::pair<uint64_t, uint32_t>> bad;
        for (const auto& [a, c] : badAt)
            bad.push_back ({ c, a });
        std::sort (bad.rbegin(), bad.rend());
        for (size_t q = 0; q < bad.size() && q < 24; ++q)
            std::printf ("    %llu  0x%08x\n", (unsigned long long) bad[q].first, bad[q].second);
    }
    std::printf ("bucles de retardo contados: %llu saltos, %llu instrucciones saltadas\n", (unsigned long long) m.loopSkip.counted,
                 (unsigned long long) m.loopSkip.countedSkipped);
    std::printf ("pantalla: encendida %d, %llu ventanas, revisión %u; audio %zu tramas (%.2f s); resets %u\n", soc.lcd.displayOn ? 1 : 0,
                 (unsigned long long) soc.lcd.frames, soc.lcd.revision, soc.periph.audio.size() / 2,
                 (double) soc.periph.audio.size() / 2 / 44117.65, soc.resets);
    if (hleMode != Machine::Hle::Off)
        std::printf ("HLE FmCore::render (modo final %s): %s en 0x%08x (tabla 0x%08x); %llu llamadas, %llu comprobadas, %llu distintas, "
                     "%.0f instrucciones emuladas por llamada%s%s\n",
                     m.hle.mode == Machine::Hle::On ? "on" : m.hle.mode == Machine::Hle::Check ? "check" : m.hle.mode == Machine::Hle::Auto ? "auto" : "off",
                     m.hle.site ? "encontrada" : "NO encontrada", m.hle.site.entry, m.hle.site.algTable,
                     (unsigned long long) m.hle.calls, (unsigned long long) m.hle.checked, (unsigned long long) m.hle.mismatches,
                     m.hle.avgCost(), m.hle.firstMismatch.empty() ? "" : "; primera: ", m.hle.firstMismatch.c_str());
    std::printf ("cpu0 por la ruta de arriba %llu (con interrupción pendiente %llu), en ráfaga %llu\n", (unsigned long long) m.statTop,
                 (unsigned long long) m.statTopPending, (unsigned long long) m.statBurst);
    if (ccMode != Machine::CcPoll::Off)
        std::printf ("HLE midi_cc_apply (modo final %s): %s en 0x%08x (salida 0x%08x, %u x %u colas, %u instrucciones); %llu saltadas, "
                     "%llu comprobadas, %llu distintas%s%s\n",
                     m.ccPoll.mode == Machine::CcPoll::On ? "on" : m.ccPoll.mode == Machine::CcPoll::Check ? "check"
                         : m.ccPoll.mode == Machine::CcPoll::Auto ? "auto" : "off",
                     m.ccPoll.entry != 0 ? "encontrada" : "NO encontrada", m.ccPoll.entry, m.ccPoll.exit, m.ccPoll.ntrk, m.ccPoll.ncc,
                     m.ccPoll.insns, (unsigned long long) m.ccPoll.skipped, (unsigned long long) m.ccPoll.checked,
                     (unsigned long long) m.ccPoll.mismatches, m.ccPoll.firstMismatch.empty() ? "" : "; primera: ",
                     m.ccPoll.firstMismatch.c_str());
    if (hleMode != Machine::Hle::Off && m.hle.nKern > 0)
        std::printf ("HLE kernels V16: %d encontrados; %llu llamadas, %llu nativas, %llu comprobadas, %llu distintas, nativo %s%s%s\n",
                     m.hle.nKern, (unsigned long long) m.hle.kCalls, (unsigned long long) m.hle.kNative, (unsigned long long) m.hle.kChecked,
                     (unsigned long long) m.hle.kMismatches, m.hle.kOff ? "apagado" : m.hle.kOn ? "encendido" : "sin decidir",
                     m.hle.kFirstMismatch.empty() ? "" : "; primera: ", m.hle.kFirstMismatch.c_str());
    std::printf ("UART MIDI: con0 0x%08x, búfer de recepción %u B, bytes descartados %llu\n", soc.periph.uartCon0(),
                 soc.periph.uartRxLen(), (unsigned long long) soc.periph.midiDropped());
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
        h.clear();
        uint64_t syn = 0;
        for (const auto& [e, c] : synHist) { h.push_back ({ c, e }); syn += c; }
        std::sort (h.rbegin(), h.rend());
        std::printf ("por exec_syntax de verdad (los dos núcleos): %llu instrucciones, %zu clases\n", (unsigned long long) syn, h.size());
        acc = 0;
        for (size_t i = 0; i < std::min<size_t> (h.size(), (size_t) profile); ++i)
        {
            acc += 100.0 * (double) h[i].first / (double) std::max<uint64_t> (syn, 1);
            std::printf ("  %6.2f %%  (acum %6.2f %%)  %10llu  %-28s %s\n", 100.0 * (double) h[i].first / (double) std::max<uint64_t> (syn, 1), acc,
                         (unsigned long long) h[i].first, h[i].second->name, h[i].second->canon);
        }
    }
    if (jitProfile > 0)
    {
        // fast: el orden de Fm1Pre::FastOp
        static const char* const kFast[] = { "None", "Ld", "St", "BrI", "BrR", "AluI", "AluR", "Mov", "MovI", "Goto", "Call",
            "Rts", "Part", "LdX", "StX", "SpAdd", "SpPlus", "Cnum", "Field", "Adc", "Ie", "AndNot", "Shift", "DecBr", "PopPc",
            "IcfgRd", "IcfgWr", "Div", "Abs", "Not", "Tb", "CallR", "Rti", "PushList", "PopList", "PushRets", "PopRetPc",
            "BitBr", "Nop", "ExecRaw" };
        std::vector<std::pair<uint64_t, uint32_t>> h;
        uint64_t tot = 0;
        for (const auto& [k, c] : jitOpHist) { h.push_back ({ c, k }); tot += c; }
        std::sort (h.rbegin(), h.rend());
        std::printf ("jitOp de cpu0: %llu instrucciones (%.1f %% de las %llu del JIT), %zu clases:\n", (unsigned long long) tot,
                     cpu.jitInsns > 0 ? 100.0 * (double) tot / (double) cpu.jitInsns : 0.0, (unsigned long long) cpu.jitInsns, h.size());
        for (size_t i = 0; i < std::min<size_t> (h.size(), (size_t) jitProfile); ++i)
        {
            const auto k = h[i].second, f = k >> 24;
            std::printf ("  %6.2f %%  %10llu  %-9s ruta %3u  fmode %u  fidx %u  fcond %u\n", 100.0 * (double) h[i].first / (double) tot,
                         (unsigned long long) h[i].first, f < std::size (kFast) ? kFast[f] : "?", (k >> 16) & 0xff, (k >> 8) & 0xff,
                         (k >> 7) & 1, k & 0x7f);
        }
    }
    if (stepProfile > 0)
    {
        // instrucciones de cpu0 que la ráfaga interpreta: por motivo y los pc más frecuentes con su micro-op
        uint64_t byWhy[16] {}, tot = 0;
        std::vector<std::pair<uint64_t, uint32_t>> h;
        for (const auto& [k, c] : stepHist) { h.push_back ({ c, k }); byWhy[(k >> 28) & 15] += c; tot += c; }
        std::sort (h.rbegin(), h.rend());
        std::printf ("ráfaga interpretada (cpu0): %llu instrucciones; por estado %llu, sin bloque %llu, no cabe %llu\n",
                     (unsigned long long) tot, (unsigned long long) byWhy[1], (unsigned long long) byWhy[2],
                     (unsigned long long) (byWhy[3] + byWhy[4] + byWhy[5] + byWhy[6] + byWhy[7] + byWhy[8]));
        static const char* const kWhy[] = { "?", "estado", "sin bloque", "room/evento", "stop", "loopHint", "sondeo", "tabla",
                                            "ventanas", "tras enlace", "?", "?", "?", "?", "?", "?" };
        std::printf ("  no cabe por: room/evento %llu, stop %llu, loopHint %llu, sondeo %llu, tabla %llu, ventanas %llu\n",
                     (unsigned long long) byWhy[3], (unsigned long long) byWhy[4], (unsigned long long) byWhy[5],
                     (unsigned long long) byWhy[6], (unsigned long long) byWhy[7], (unsigned long long) byWhy[8]);
        for (size_t i = 0; i < std::min<size_t> (h.size(), (size_t) stepProfile); ++i)
        {
            const auto pc = h[i].second & 0x0fffffffu;
            uint64_t raw = 0;
            const auto* e = decodeAny (((uint64_t) soc.fetch32 (pc + 4) << 32) | soc.fetch32 (pc), raw);
            Fm1Pre pre;
            if (e != nullptr) computePre (*e, raw, pre);
            std::printf ("  %6.2f %%  %10llu  %-10s 0x%08x  %-30s fast %2u ruta %3u\n", 100.0 * (double) h[i].first / (double) tot,
                         (unsigned long long) h[i].first, kWhy[(h[i].second >> 28) & 15], pc, e != nullptr ? e->syntax : "?",
                         e != nullptr ? (unsigned) pre.fast : 0u, e != nullptr ? (unsigned) (pre.route & 0xff) : 0u);
        }
    }
    for (uint32_t pc = disIni; pc < disFin;)
    {
        uint64_t raw = 0;
        const auto* e = decodeAny (((uint64_t) soc.fetch32 (pc + 4) << 32) | soc.fetch32 (pc), raw);
        std::printf ("0x%08x  %-34s", pc, e != nullptr ? e->syntax : "?");
        if (e != nullptr)
            for (int k = 0; k < e->slotCount; ++k)
                std::printf (" %lld", (long long) slotValue (e->slots[k], raw));
        std::printf ("   [%s, %d B]\n", e != nullptr ? e->name : "?", e != nullptr ? e->len : 2);
        pc += e != nullptr && e->len > 0 ? e->len : 2u;
    }
    for (int core = 0; core < 2; ++core)
    {
        const auto& hist1 = core == 0 ? pcHist : pcHist1;
        const auto limit = core == 0 ? pcProfile : pcProfile1;
        if (limit <= 0)
            continue;
        std::vector<std::pair<uint64_t, uint32_t>> h;
        uint64_t total = 0;
        for (const auto& [a, c] : hist1) { h.push_back ({ c, a }); total += c; }
        std::sort (h.rbegin(), h.rend());
        std::printf ("perfil por pc de cpu%d (%llu instrucciones, %zu pc):\n", core, (unsigned long long) total, h.size());
        double acc = 0;
        for (size_t i = 0; i < std::min<size_t> (h.size(), (size_t) limit); ++i)
        {
            uint64_t raw = 0;
            const auto* e = decodeAny (((uint64_t) soc.fetch32 (h[i].second + 4) << 32) | soc.fetch32 (h[i].second), raw);
            acc += 100.0 * (double) h[i].first / (double) total;
            std::printf ("  %6.2f %%  (acum %6.2f %%)  %10llu  0x%08x  %-28s raw 0x%llx\n", 100.0 * (double) h[i].first / (double) total, acc,
                         (unsigned long long) h[i].first, h[i].second, e != nullptr ? e->syntax : "?", (unsigned long long) raw);
            if (e != nullptr)
            {
                Fm1Pre pre;
                computePre (*e, raw, pre);
                if (pre.route == 14)  // bloque predicado: unidades de then y de else
                    std::printf ("            then %u, else %u\n", (unsigned) pre.ibThen, (unsigned) pre.ibElse);
            }
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

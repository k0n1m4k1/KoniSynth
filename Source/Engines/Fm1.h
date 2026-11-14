// Extraído como cabecera: se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (usa SincResampler).
#pragma once

#include "../Fm1/Fm1Soc.h"

//==============================================================================
// Motor M-VAVE FM-1 (índice 16): el firmware del usuario (.fwsc, de fábrica o de la comunidad) corre en el emulador de
// Source/Fm1 (port de fm1-emu, MIT), con su pantalla, su panel y su jack MIDI. KoniSynth no incluye ningún firmware.
//
// Hilos. El emulador necesita casi un núcleo entero (la V15 de fábrica, ≈ 1,0x tiempo real en el equipo de desarrollo,
// 2026-11-13): no cabe en el hilo de audio. Corre en un hilo propio por delante del audio la latencia elegida (30 ms por
// defecto), con tres colas sin bloqueo:
//   audio  emulador → audio: tramas estéreo de 16 bits a 24 MHz / 544 = 44 117,65 Hz (el reloj del DAC del chip)
//   MIDI   audio → emulador: cada byte con la trama del emulador en la que entra por el jack (la del host + la latencia)
//   panel  interfaz → emulador: teclas y encoders, en cuanto llegan
// Al exportar (render no en tiempo real) el hilo de audio avanza la máquina hasta tener lo que necesita: el bounce no
// depende de la velocidad del equipo. En tiempo real, si el emulador no llega, la salida calla (underruns) y lo cuenta.
//
// Remuestreo a la frecuencia del host con SincResampler (8 tramas del chip de latencia, 0,18 ms). Supuesto: el DAC del
// AC791N filtra como uno con sobremuestreo (lo habitual en un sigma-delta de audio integrado); no medido en el aparato.
class Fm1Engine
{
public:
    static constexpr double kChipRate = 24'000'000.0 / 544.0;                     // 44 117,647 Hz
    static constexpr uint64_t kCyclesPerFrame = fm1::kCpuHz * 544 / 24'000'000;  // 5440 ciclos de CPU por trama, exacto
    static constexpr int kScreen = fm1::Lcd::W;                                  // 240 × 240

    Fm1Engine() = default;
    ~Fm1Engine() { stop(); }

    // ---- hilo de mensajes ---------------------------------------------------------------------------------------------
    // Carga el paquete y arranca la máquina en su hilo. false y el motivo en status si no se puede. La memoria del aparato
    // (lo que el firmware guardó en su flash: SAVE, ajustes): la de memoryB64 (preset o proyecto) si es de este paquete; si
    // no, la del fichero <firmware>.fm1save junto al .fwsc; si no, la flash del paquete tal cual
    bool start (const juce::File& fwsc, juce::String& error, const juce::String& memoryB64 = {})
    {
        stop();
        juce::MemoryBlock data;
        if (! fwsc.loadFileAsData (data))
        {
            error = "Cannot read " + fwsc.getFullPathName();
            return false;
        }
        const auto* bytes = static_cast<const uint8_t*> (data.getData());
        auto e = boot (std::vector<uint8_t> (bytes, bytes + data.getSize()), error);
        if (e == nullptr)
            return false;
        e->packageHash = fnv64 (bytes, data.getSize());
        e->savePath = fwsc.withFileExtension ("fm1save");
        {
            juce::MemoryBlock fromPreset, fromFile;
            juce::String note;
            if (memoryB64.isNotEmpty() && fromPreset.fromBase64Encoding (memoryB64) && applyMemory (*e, fromPreset, note))
                note = "memory from the preset";
            else if (e->savePath.loadFileAsData (fromFile) && applyMemory (*e, fromFile, note))
                note = "memory from " + e->savePath.getFileName();
            memoryNote = note;
        }
        e->savedRev = e->seenRev = e->soc->flash.revision;
        publishMemory (*e);
        audioFifo.reset();
        midiFifo.reset();
        panelFifo.reset();
        consumed = 0;
        underruns = 0;
        primed = false;
        cacheLen = cachePos = 0;
        resampler.reset();
        statusBooting = true;
        {
            const std::lock_guard<std::mutex> g (emuLock);
            emu = std::move (e);
        }
        loaded = fwsc;
        setStatus ("Booting " + fwsc.getFileName());
        live.store (true, std::memory_order_release);
        worker = std::make_unique<Worker> (*this);
        worker->startThread (juce::Thread::Priority::high);
        return true;
    }

    // Para la máquina y la libera (al cambiar de motor: sin el FM-1 elegido no gasta CPU)
    void stop()
    {
        live.store (false, std::memory_order_release);
        while (inRender.load (std::memory_order_acquire) > 0) // un bloque de audio que empezó antes: que termine
            juce::Thread::sleep (1);
        if (worker != nullptr)
        {
            worker->stopThread (4000);
            worker.reset();
        }
        const std::lock_guard<std::mutex> g (emuLock);
        if (emu != nullptr)
            saveMemory (*emu, true); // lo que el firmware escribió desde el último guardado
        emu.reset();
    }

    bool isLive() const noexcept { return live.load (std::memory_order_acquire); }

    // La memoria del aparato en Base64 (el formato de <firmware>.fm1save), para el preset y el proyecto; vacía sin máquina
    juce::String memoryBase64() const
    {
        const std::lock_guard<std::mutex> g (memoryLock);
        return memoryB64Now;
    }
    juce::File loadedFile() const { return loaded; }

    // Latencia añadida (ms) y su valor en muestras del host (la cola por delante del audio + el remuestreador)
    void setLatencyMs (double ms) { latencyFrames = juce::jlimit (64, kAudioCap / 2, (int) std::lround (ms * kChipRate / 1000.0)); }
    double latencyMs() const noexcept { return latencyFrames.load() * 1000.0 / kChipRate; }
    int latencySamples (double rate) const noexcept
    {
        return (int) std::lround ((latencyFrames.load() + SincResampler::kTaps / 2) * rate / kChipRate);
    }

    // Una tecla o un encoder del panel (cualquier hilo salvo el de audio)
    void panel (const fm1::PanelAction& a)
    {
        const juce::ScopedLock sl (panelWriteLock);
        if (panelFifo.getFreeSpace() < 1)
            return;
        const auto scope = panelFifo.write (1);
        scope.forEach ([&] (int i) { panelBuf[(size_t) i] = a; });
    }

    // La pantalla si cambió desde rev (hilo de mensajes): RGB565 de 240 × 240. false = sin cambios
    bool takeScreen (std::vector<uint16_t>& to, uint64_t& rev, bool& on)
    {
        const std::lock_guard<std::mutex> g (screenLock);
        if (screenRev == rev)
            return false;
        to = screen;
        on = screenOn;
        rev = screenRev;
        return true;
    }

    struct View
    {
        bool live = false;
        double speed = 0.0;       // tiempo de máquina / tiempo real del hilo del emulador (1 = justo)
        uint64_t underruns = 0;   // tramas que faltaron en tiempo real
        juce::String status;
    };
    View view() const
    {
        View v;
        v.live = isLive();
        v.speed = speed.load();
        v.underruns = underruns.load();
        const juce::ScopedLock sl (statusLock);
        v.status = status;
        return v;
    }

    // ---- hilo de audio ------------------------------------------------------------------------------------------------
    void prepare (double sampleRate, int)
    {
        hostRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        ratio = kChipRate / hostRate;
        resampler.prepare (kChipRate, hostRate);
    }

    // Al elegir el motor o en un panic: todas las notas fuera, como un "All Notes Off" en los 16 canales del jack
    void reset() { panicPending.store (true, std::memory_order_release); }

    // Suma la salida del FM-1 a out. offline = el host exporta: la máquina se avanza aquí hasta tener lo necesario
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, bool offline)
    {
        const InRender guard (inRender);
        offlineMode.store (offline, std::memory_order_release);
        if (! live.load (std::memory_order_acquire))
            return;
        const auto n = out.getNumSamples();
        const auto lat = (int64_t) latencyFrames.load (std::memory_order_relaxed);
        const auto base = consumed.load (std::memory_order_relaxed);
        if (panicPending.exchange (false, std::memory_order_acq_rel))
            for (uint8_t ch = 0; ch < 16; ++ch)
                for (const uint8_t b : { (uint8_t) (0xB0 | ch), (uint8_t) 123, (uint8_t) 0 })
                    pushMidi (base + lat, b);
        for (const auto meta : midi)
        {
            const auto frame = base + (int64_t) (meta.samplePosition * ratio) + lat;
            const auto* raw = meta.data;
            for (int i = 0; i < meta.numBytes; ++i) // (con el SysEx entero: F0 ... F7)
                pushMidi (frame, raw[i]);
        }
        if (offline)
        {
            const std::lock_guard<std::mutex> g (emuLock);
            if (emu != nullptr)
            {
                // lo que este bloque va a pedir (la primera vez, además la latencia que llena la cola). El MIDI va a su trama
                // absoluta (deliver): sale la latencia después, como en tiempo real, sin que la máquina se adelante a él
                const auto want = std::max (lat, base + (int64_t) std::ceil (n * ratio) + SincResampler::kTaps);
                const auto start = emu->soc->periph.cycles;
                while (emu->produced < want && emu->soc->periph.cycles - start < 2 * fm1::kCpuHz) // (sin audio en 2 s: calla)
                    if (! pump (*emu, want, 1u << 20))
                        break;
            }
        }
        if (! primed)
        {
            if (audioFifo.getNumReady() < lat)
                return; // arrancando: la cola aún no tiene la latencia elegida
            primed = true;
        }
        auto* l = out.getWritePointer (0);
        auto* r = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const auto s = resampler.nextStereo ([this] { return pop(); });
            if (r != nullptr) { l[i] += s[0]; r[i] += s[1]; }
            else l[i] += 0.5f * (s[0] + s[1]);
        }
    }

private:
    struct Emu
    {
        fm1::Stock stock;
        std::unique_ptr<fm1::Soc> soc;
        std::unique_ptr<fm1::Cpu> cpu;
        std::unique_ptr<fm1::Machine> machine;
        int64_t produced = 0;     // tramas de audio que ha dado
        int64_t dacOffset = -1;   // trama del reloj del DAC de la primera trama dada (-1: aún sin audio)
        uint64_t shownAt = 0;     // ciclo de la última copia de la pantalla
        uint32_t shownRev = ~0u;  // y su revisión
        uint64_t packageHash = 0; // FNV-1a del .fwsc: la memoria guardada solo vale para el mismo paquete
        juce::File savePath;      // <firmware>.fm1save
        uint64_t savedRev = 0;    // SpiFlash::revision de lo último guardado
        uint64_t seenRev = 0;     // la última revisión vista y cuándo (ms) se vio cambiar
        double changedAt = 0.0;
    };

    //==========================================================================
    // Memoria del aparato: los sectores de 4 KiB de la flash que difieren de la del paquete (Stock::flash). Formato, comprimido
    // con gzip: "KFM1", versión 1 (u32), FNV-1a del .fwsc (u64), n (u32) y n veces (desplazamiento u32, 4096 bytes); enteros
    // en little endian. Fichero <firmware>.fm1save junto al .fwsc y, en Base64, la propiedad fm1Memory del preset y del proyecto
    static constexpr size_t kSector = 4096;
    static constexpr double kSaveDelayMs = 1000.0; // tras la última escritura del firmware (un SAVE son varias órdenes)

    static uint64_t fnv64 (const uint8_t* p, size_t n)
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
        return h;
    }

    static juce::MemoryBlock encodeMemory (const Emu& e)
    {
        const auto& now = e.soc->flash.mem;
        const auto& base = e.stock.flash;
        juce::MemoryOutputStream raw;
        raw.write ("KFM1", 4);
        raw.writeInt (1);
        raw.writeInt64 ((juce::int64) e.packageHash);
        std::vector<uint32_t> sectors;
        for (size_t at = 0; at < now.size(); at += kSector)
        {
            const auto n = std::min (kSector, now.size() - at);
            const bool same = at + n <= base.size() ? std::memcmp (&now[at], &base[at], n) == 0 : false;
            if (! same)
                sectors.push_back ((uint32_t) at);
        }
        raw.writeInt ((int) sectors.size());
        for (const auto at : sectors)
        {
            raw.writeInt ((int) at);
            raw.write (&now[at], std::min (kSector, now.size() - at));
        }
        juce::MemoryOutputStream packed;
        {
            juce::GZIPCompressorOutputStream gz (packed, 9);
            gz.write (raw.getData(), raw.getDataSize());
        }
        return packed.getMemoryBlock();
    }

    // La memoria guardada sobre la flash recién arrancada (antes de la primera instrucción). false (y por qué en note) si no
    // es de este paquete o no se lee
    static bool applyMemory (Emu& e, const juce::MemoryBlock& packed, juce::String& note)
    {
        juce::MemoryInputStream in (packed, false);
        juce::GZIPDecompressorInputStream gz (in);
        juce::MemoryBlock raw;
        gz.readIntoMemoryBlock (raw);
        juce::MemoryInputStream r (raw, false);
        char magic[4] = {};
        if (r.read (magic, 4) != 4 || std::memcmp (magic, "KFM1", 4) != 0 || r.readInt() != 1)
        {
            note = "saved memory not readable";
            return false;
        }
        if ((uint64_t) r.readInt64() != e.packageHash)
        {
            note = "saved memory is for another build of this firmware: not loaded";
            return false;
        }
        const auto n = r.readInt();
        auto image = e.stock.flash;
        image.resize (e.soc->flash.mem.size(), 0xff);
        for (int k = 0; k < n; ++k)
        {
            const auto at = (size_t) (uint32_t) r.readInt();
            const auto len = std::min (kSector, image.size() - std::min (at, image.size()));
            if (at >= image.size() || r.read (&image[at], (int) len) != (int) len)
            {
                note = "saved memory truncated";
                return false;
            }
        }
        e.soc->restoreFlash (image); // (y la ventana XIP, por si el firmware guarda en ella)
        return true;
    }

    void publishMemory (const Emu& e)
    {
        const auto b64 = encodeMemory (e).toBase64Encoding();
        const std::lock_guard<std::mutex> g (memoryLock);
        memoryB64Now = b64;
    }

    // (con emuLock) la flash cambió: cuando lleva kSaveDelayMs sin cambiar (o con force), al fichero y al preset. El
    // fichero se escribe aparte y se renombra: un corte a medias deja el anterior entero
    void saveMemory (Emu& e, bool force)
    {
        const auto rev = e.soc->flash.revision;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (rev != e.seenRev)
        {
            e.seenRev = rev;
            e.changedAt = now;
        }
        if (rev == e.savedRev || (! force && now - e.changedAt < kSaveDelayMs))
            return;
        const auto packed = encodeMemory (e);
        juce::TemporaryFile tmp (e.savePath);
        if (tmp.getFile().replaceWithData (packed.getData(), packed.getSize()))
            tmp.overwriteTargetFileWithTemporary();
        {
            const auto b64 = packed.toBase64Encoding();
            const std::lock_guard<std::mutex> g (memoryLock);
            memoryB64Now = b64;
        }
        e.savedRev = rev;
    }

    struct Worker : juce::Thread
    {
        explicit Worker (Fm1Engine& e) : juce::Thread ("KoniSynth FM-1"), engine (e) {}
        void run() override { engine.workerLoop (*this); }
        Fm1Engine& engine;
    };

    struct InRender
    {
        explicit InRender (std::atomic<int>& c) : count (c) { count.fetch_add (1, std::memory_order_acq_rel); }
        ~InRender() { count.fetch_sub (1, std::memory_order_acq_rel); }
        std::atomic<int>& count;
    };

    static constexpr int kAudioCap = 1 << 16;       // tramas (1,49 s)
    static constexpr int kMidiCap = 1 << 14;        // bytes MIDI en vuelo
    static constexpr int kPanelCap = 1024;
    // Instrucciones de cpu0 por paso de la máquina. Cada llamada a Machine::run corta los saltos de bucle en su borde: con
    // 16384, Felucca 1.8.2 iba un 13 % más lenta que con 65536 (KoniFm1Run --chunk, régimen de 4 a 8 s: 0,903x frente a
    // 1,036x; la V15, la V16 y GHOULBOX, ±3 %). El mayor avance de un paso medido es 35,9 ms de máquina (GHOULBOX y
    // Felucca, 6 s): muy por debajo de las 8192 tramas (186 ms) libres que pide pump antes de cada paso
    static constexpr uint32_t kChunk = 65536;

    // Arranque del firmware de un paquete (como KoniFm1Run): su flash, los parámetros del SPL y la clave del SFC
    static std::unique_ptr<Emu> boot (const std::vector<uint8_t>& pkg, juce::String& error)
    {
        auto e = std::make_unique<Emu>();
        std::string err;
        if (! fm1::stockFromPackage (pkg, nullptr, e->stock, err))
        {
            error = "Not an FM-1 firmware package: " + juce::String (err);
            return nullptr;
        }
        e->soc = std::make_unique<fm1::Soc> (fm1::kRamSize, e->stock.xip);
        e->soc->periph.skipIdle = true; // como fm1-live: el tiempo salta los bucles de espera de TIMER4 e idle
        e->cpu = std::make_unique<fm1::Cpu> (e->stock.entry);
        e->cpu->user = true; // el SPL entrega cpu0 en modo usuario
        const uint32_t params = fm1::kRamBase + 0x7fc00;
        e->soc->write32 (params, params + 0x40);
        e->cpu->regs[0] = fm1::stockBoot (*e->soc, e->stock.flash, params);
        e->soc->sfcBoot (e->stock.key, e->stock.appEnd);
        e->machine = std::make_unique<fm1::Machine> (*e->soc, *e->cpu);
        // núcleo de operadores nativo (Fm1Hle.h): se comprueba bit a bit en las primeras llamadas y solo entonces se usa
        e->machine->hle.mode = fm1::Machine::Hle::Auto;
        // GHOULBOX: la pasada vacía de su sondeo de CC MIDI, saltada al mismo ciclo (Machine::CcPoll); otros firmwares no la tienen
        e->machine->ccPoll.mode = fm1::Machine::CcPoll::Auto;
        return e;
    }

    // Lo que llegó del panel (en cuanto llega) y del jack. Un byte MIDI va a su trama en el reloj del DAC (la del audio que
    // sale más el desfase de la primera trama): no depende de cuándo se vacía la cola, solo de que sea antes de ese ciclo.
    // Sin audio aún (arrancando) el MIDI espera en la cola
    void deliver (Emu& e)
    {
        auto& periph = e.soc->periph;
        if (panelFifo.getNumReady() > 0)
        {
            const auto scope = panelFifo.read (panelFifo.getNumReady());
            scope.forEach ([&] (int i) { periph.schedule (periph.cycles, panelBuf[(size_t) i]); });
        }
        if (e.dacOffset >= 0 && midiFifo.getNumReady() > 0)
        {
            const auto scope = midiFifo.read (midiFifo.getNumReady());
            scope.forEach ([&] (int i)
            {
                const auto& m = midiBuf[(size_t) i];
                const auto at = (uint64_t) std::max<int64_t> (0, m.frame + e.dacOffset) * kCyclesPerFrame;
                periph.schedule (std::max (at, periph.cycles), fm1::PanelAction { fm1::PanelAction::Midi, 0, m.byte });
            });
        }
    }

    // Avanza la máquina hasta haber dado want tramas (o budget instrucciones de cpu0) con emuLock tomado, entregando el panel
    // y el jack antes de cada paso. false = la máquina se paró (el motivo queda en el estado)
    bool pump (Emu& e, int64_t want, uint32_t budget)
    {
        deliver (e);
        for (uint32_t ran = 0; e.produced < want && ran < budget && audioFifo.getFreeSpace() >= 8192; ran += kChunk)
        {
            deliver (e);
            std::string err;
            if (! e.machine->run (kChunk, err))
            {
                setStatus ("Stopped: " + juce::String (err));
                return false;
            }
            moveAudio (e);
        }
        publishScreen (e);
        return true;
    }

    void moveAudio (Emu& e)
    {
        auto& a = e.soc->periph.audio;
        const auto frames = (int) (a.size() / 2);
        if (frames == 0)
            return;
        const auto scope = audioFifo.write (std::min (frames, audioFifo.getFreeSpace()));
        int k = 0;
        scope.forEach ([&] (int i)
        {
            audioBuf[(size_t) (2 * i)] = a[(size_t) (2 * k)];
            audioBuf[(size_t) (2 * i + 1)] = a[(size_t) (2 * k + 1)];
            ++k;
        });
        e.produced += frames;
        if (e.dacOffset < 0) // la primera trama dada: su lugar en el reloj del DAC (el audio sale a su ritmo exacto desde aquí)
            e.dacOffset = (int64_t) fm1::Periph::audioFramesAt (e.soc->periph.cycles, e.soc->periph.hz) - e.produced;
        a.clear();
    }

    // La pantalla para la interfaz: como mucho 30 veces por segundo de máquina, y solo si cambió
    void publishScreen (Emu& e)
    {
        const auto& lcd = e.soc->lcd;
        const auto now = e.soc->periph.cycles;
        if (lcd.revision == e.shownRev || now - e.shownAt < fm1::kCpuHz / 30)
            return;
        e.shownRev = lcd.revision;
        e.shownAt = now;
        const std::lock_guard<std::mutex> g (screenLock);
        screen = lcd.fb;
        screenOn = lcd.displayOn;
        ++screenRev;
    }

    // El hilo del emulador: mantiene la cola de audio la latencia por delante de lo que el audio ha sacado
    void workerLoop (juce::Thread& t)
    {
        auto wall0 = juce::Time::getMillisecondCounterHiRes();
        uint64_t cycles0 = 0;
        bool first = true;
        while (! t.threadShouldExit())
        {
            if (offlineMode.load (std::memory_order_acquire))
            {
                t.wait (5); // exportando: la máquina la avanza el render (el resultado no depende de este hilo)
                continue;
            }
            bool behind = false;
            {
                const std::lock_guard<std::mutex> g (emuLock);
                if (emu == nullptr)
                    break;
                const auto want = consumed.load (std::memory_order_acquire) + latencyFrames.load();
                behind = emu->produced < want;
                if (! pump (*emu, want, behind ? (1u << 18) : 0u)) // (sin retraso: solo el panel, el jack y la pantalla)
                {
                    live.store (false, std::memory_order_release);
                    break;
                }
                if (emu->produced > 0 && statusBooting)
                {
                    statusBooting = false;
                    setStatus ("Running " + loaded.getFileName() + (memoryNote.isNotEmpty() ? " (" + memoryNote + ")" : juce::String()));
                }
                saveMemory (*emu, false); // lo que el firmware guardó en su flash (SAVE), al fichero y al preset
                // velocidad: tiempo de máquina / tiempo real, cada ~0,5 s
                const auto wall = juce::Time::getMillisecondCounterHiRes();
                const auto cycles = emu->soc->periph.cycles;
                if (first) { first = false; wall0 = wall; cycles0 = cycles; }
                else if (wall - wall0 >= 500.0)
                {
                    speed = (double) (cycles - cycles0) / (double) fm1::kCpuHz / ((wall - wall0) / 1000.0);
                    wall0 = wall;
                    cycles0 = cycles;
                }
            }
            if (! behind)
                t.wait (1);
        }
    }

    void pushMidi (int64_t frame, uint8_t byte)
    {
        if (midiFifo.getFreeSpace() < 1)
            return; // (16 384 bytes en vuelo: un SysEx enorme que no cabe se recorta)
        const auto scope = midiFifo.write (1);
        scope.forEach ([&] (int i) { midiBuf[(size_t) i] = { frame, byte }; });
    }

    // Una trama del emulador para el remuestreador; sin trama, silencio (underrun) sin consumir
    std::array<float, 2> pop()
    {
        if (cachePos == cacheLen)
        {
            const auto scope = audioFifo.read (std::min ((int) cache.size() / 2, audioFifo.getNumReady()));
            cacheLen = cachePos = 0;
            scope.forEach ([&] (int i)
            {
                cache[(size_t) (2 * cacheLen)] = audioBuf[(size_t) (2 * i)];
                cache[(size_t) (2 * cacheLen + 1)] = audioBuf[(size_t) (2 * i + 1)];
                ++cacheLen;
            });
            consumed.fetch_add (cacheLen, std::memory_order_acq_rel);
            if (cacheLen == 0)
            {
                underruns.fetch_add (1, std::memory_order_relaxed);
                return { 0.0f, 0.0f };
            }
        }
        const auto k = (size_t) (2 * cachePos++);
        return { cache[k] / 32768.0f, cache[k + 1] / 32768.0f };
    }

    void setStatus (const juce::String& s)
    {
        const juce::ScopedLock sl (statusLock);
        status = s;
    }

    std::mutex emuLock;             // la máquina: el hilo del emulador, o el de audio al exportar
    std::unique_ptr<Emu> emu;       // protegido por emuLock
    std::unique_ptr<Worker> worker;
    std::atomic<bool> live { false };
    std::atomic<int> inRender { 0 };
    std::atomic<bool> panicPending { false };
    std::atomic<bool> offlineMode { false }; // el host exporta: el hilo del emulador espera
    juce::File loaded;
    bool statusBooting = true;

    juce::AbstractFifo audioFifo { kAudioCap };
    std::vector<int16_t> audioBuf = std::vector<int16_t> ((size_t) kAudioCap * 2);
    struct MidiByte { int64_t frame; uint8_t byte; };
    juce::AbstractFifo midiFifo { kMidiCap };
    std::vector<MidiByte> midiBuf = std::vector<MidiByte> ((size_t) kMidiCap);
    juce::AbstractFifo panelFifo { kPanelCap };
    std::vector<fm1::PanelAction> panelBuf = std::vector<fm1::PanelAction> ((size_t) kPanelCap, fm1::PanelAction { fm1::PanelAction::Key, 0, 0 });
    juce::CriticalSection panelWriteLock; // (escritores fuera del audio: la interfaz y el agente)

    std::atomic<int64_t> consumed { 0 };  // tramas que el audio ha sacado de la cola
    std::atomic<int> latencyFrames { 1324 }; // 30 ms
    std::atomic<uint64_t> underruns { 0 };
    std::atomic<double> speed { 0.0 };
    bool primed = false;                  // (hilo de audio) la cola llegó a la latencia elegida
    std::array<int16_t, 512> cache {};    // (hilo de audio) tramas leídas de la cola
    int cacheLen = 0, cachePos = 0;
    double hostRate = 48000.0, ratio = kChipRate / 48000.0;
    SincResampler resampler;

    std::mutex screenLock;
    std::vector<uint16_t> screen = std::vector<uint16_t> ((size_t) kScreen * kScreen, 0);
    bool screenOn = false;
    uint64_t screenRev = 0;

    juce::CriticalSection statusLock;
    juce::String status;

    mutable std::mutex memoryLock;
    juce::String memoryB64Now; // (memoryLock) la memoria del aparato en Base64, la última guardada
    juce::String memoryNote;   // de dónde vino la memoria al arrancar (o por qué no se cargó la guardada)
};

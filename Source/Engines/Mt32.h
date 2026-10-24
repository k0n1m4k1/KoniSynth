// Motor Roland MT-32 / CM-32L (1987; síntesis LA: parciales PCM + sintéticos, reverb digital, LCD de 20 caracteres) con Munt
// mt32emu 2.8.3 (LGPL-2.1+, Source/ThirdParty/mt32emu), sin modificar: fidelidad al aparato con sus bugs (desbordamiento del DAC
// de la 1.ª generación, reverb y mensajes del LCD según la ROM). Las ROM (control + PCM) son de Roland y no se incluyen: el usuario
// las deja en una carpeta y se identifican por tamaño y SHA-1 (ROMInfo de Munt); las mitades y los pares Mux se fusionan.
// Salida analógica ACCURATE (filtro de salida del aparato, 48 kHz) convertida a la frecuencia del host.
// Hilos: la máquina se construye en el de mensajes (abrir cuesta ~decenas de ms: tablas y ROM) y se publica por DataSlot; desde
// entonces solo la toca el hilo de audio. El LCD se copia en el audio y la UI lo lee con un contador de secuencia.
// Extraído como cabecera: se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación.
#pragma once

#include <mt32emu.h>

struct Mt32Machine
{
    // Los ArrayFile de Munt apuntan a estos bloques: viven tanto como la máquina
    std::vector<std::unique_ptr<juce::MemoryBlock>> data;
    std::vector<std::unique_ptr<MT32Emu::ArrayFile>> files;
    const MT32Emu::ROMImage* control = nullptr;
    const MT32Emu::ROMImage* pcm = nullptr;
    // Solo el hilo de audio, tras publicarse (DataSlot entrega punteros const)
    mutable MT32Emu::Synth synth;
    mutable std::unique_ptr<MT32Emu::SampleRateConverter> converter;
    juce::String description;
    bool firstGeneration = false, cm32l = false;

    ~Mt32Machine()
    {
        converter.reset();
        synth.close();
        if (control != nullptr) MT32Emu::ROMImage::freeROMImage (control);
        if (pcm != nullptr)     MT32Emu::ROMImage::freeROMImage (pcm);
    }

    // Carpeta de ROM: la elegida por el usuario (fichero de ajustes global) o Documentos/KoniSynth/ROMs/MT-32
    static juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("KoniSynth/mt32-roms.txt");
    }
    static juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KoniSynth/ROMs/MT-32");
    }
    static juce::File romFolder()
    {
        // KONI_MT32_ROMS (pruebas y KoniBench): carpeta sin tocar el ajuste del usuario
        if (const auto env = juce::SystemStats::getEnvironmentVariable ("KONI_MT32_ROMS", {}); env.isNotEmpty())
            return juce::File (env);
        const auto path = settingsFile().loadFileAsString().trim();
        return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : defaultFolder();
    }
    static void setRomFolder (const juce::File& folder)
    {
        settingsFile().getParentDirectory().createDirectory();
        settingsFile().replaceWithText (folder.getFullPathName());
    }

    // Busca y abre una pareja control + PCM. model: 0 = MT-32 (1.ª generación primero), 1 = CM-32L. Si falta el modelo pedido
    // y hay el otro, abre el otro y lo dice en 'report'. nullptr si no hay pareja válida
    static std::unique_ptr<Mt32Machine> open (const juce::File& folder, int model, double hostRate, juce::String& report)
    {
        using namespace MT32Emu;
        auto m = std::make_unique<Mt32Machine>();
        struct Found { const ROMImage* image; const ROMInfo* info; };
        std::vector<Found> full;
        std::vector<std::pair<File*, const ROMInfo*>> partial;
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, true))
        {
            const auto size = f.getSize();
            if (size != 32768 && size != 65536 && size != 131072 && size != 262144 && size != 524288 && size != 1048576)
                continue;
            auto block = std::make_unique<juce::MemoryBlock>();
            if (! f.loadFileAsData (*block))
                continue;
            auto file = std::make_unique<ArrayFile> (static_cast<const Bit8u*> (block->getData()), block->getSize());
            const auto* info = ROMInfo::getROMInfo (file.get());
            if (info == nullptr)
                continue;
            if (info->pairType == ROMInfo::Full)
            {
                if (const auto* image = ROMImage::makeROMImage (file.get()))
                    full.push_back ({ image, image->getROMInfo() });
            }
            else
            {
                partial.push_back ({ file.get(), info });
            }
            m->data.push_back (std::move (block));
            m->files.push_back (std::move (file));
        }
        // Mitades (L/H) y pares Mux (A/B): la segunda de cada par apunta a su compañera (pairROMInfo)
        for (const auto& [file, info] : partial)
            if (info->pairROMInfo != nullptr)
                for (const auto& [other, otherInfo] : partial)
                    if (otherInfo == info->pairROMInfo)
                        if (const auto* image = ROMImage::makeROMImage (other, file))
                            full.push_back ({ image, image->getROMInfo() });

        auto rank = [] (const char* name, int wantModel)
        {
            static const char* mt32[] = { "ctrl_mt32_1_07", "ctrl_mt32_1_06", "ctrl_mt32_1_05", "ctrl_mt32_1_04", "ctrl_mt32_bluer",
                                          "ctrl_mt32_2_07", "ctrl_mt32_2_06", "ctrl_mt32_2_04", "ctrl_mt32_2_03" };
            static const char* cm[] = { "ctrl_cm32l_1_02", "ctrl_cm32l_1_00", "ctrl_cm32ln_1_00" };
            const auto n = juce::String (name);
            for (int i = 0; i < (int) std::size (mt32); ++i) if (n == mt32[i]) return wantModel == 0 ? i : 100 + i;
            for (int i = 0; i < (int) std::size (cm); ++i)   if (n == cm[i])   return wantModel == 1 ? i : 100 + i;
            return 1000;
        };
        const Found* control = nullptr;
        for (const auto& c : full)
            if (c.info->type == ROMInfo::Control && rank (c.info->shortName, model) < 1000
                && (control == nullptr || rank (c.info->shortName, model) < rank (control->info->shortName, model)))
                control = &c;
        const Found* pcm = nullptr;
        if (control != nullptr)
        {
            const auto isCm = juce::String (control->info->shortName).startsWith ("ctrl_cm32l");
            for (const auto& c : full)
                if (c.info->type == ROMInfo::PCM && juce::String (c.info->shortName) == (isCm ? "pcm_cm32l" : "pcm_mt32"))
                    pcm = &c;
        }
        for (const auto& c : full) // las que no se usan
            if (&c != control && &c != pcm)
                ROMImage::freeROMImage (c.image);
        if (control == nullptr || pcm == nullptr)
        {
            if (control != nullptr) ROMImage::freeROMImage (control->image);
            if (pcm != nullptr)     ROMImage::freeROMImage (pcm->image);
            report = control == nullptr ? "No MT-32 / CM-32L control ROM in " + folder.getFullPathName()
                                        : juce::String ("Control ROM found (") + control->info->description + ") but not its PCM ROM in "
                                              + folder.getFullPathName();
            return nullptr;
        }
        m->control = control->image;
        m->pcm = pcm->image;
        const auto name = juce::String (control->info->shortName);
        m->cm32l = name.startsWith ("ctrl_cm32l");
        m->firstGeneration = name.startsWith ("ctrl_mt32_1_") || name.startsWith ("ctrl_mt32_bluer");
        if (! m->synth.open (*m->control, *m->pcm, AnalogOutputMode_ACCURATE))
        {
            report = "Munt could not open the ROM pair " + juce::String (control->info->description);
            return nullptr;
        }
        // Fidelidad: Munt activa por defecto NiceAmpRamp ("we prefer the quality improvement over the emulation accuracy",
        // Synth.h): aquí arranca apagado, como el aparato (NicePanning y NicePartialMixing ya vienen apagados). Los tres son
        // parámetros (mt32Nice*) que aplica el render. El retardo MIDI de cable (DELAY_SHORT_MESSAGES_ONLY) se conserva
        m->synth.setNiceAmpRampEnabled (false);
        // Los mensajes pasan por la cola de Munt (render): búfer de SysEx preasignado para no reservar memoria en el hilo de
        // audio (sin él, Munt hace new por cada SysEx encolado). 32 KB: varios volcados completos de parches
        m->synth.configureMIDIEventQueueSysexStorage (32768);
        m->converter = std::make_unique<SampleRateConverter> (m->synth, hostRate, SamplerateConversionQuality_GOOD);
        m->description = juce::String (control->info->description) + " + " + pcm->info->description;
        report = (model == 1) != m->cm32l ? m->description + juce::String (" (the other model was not found)") : m->description;
        return m;
    }
};

using Mt32Slot = DataSlot<Mt32Machine>;

struct Mt32Engine
{
    static constexpr int lcdSize = 20; // MT32Emu::Display::LCD_TEXT_SIZE
    Mt32Slot slot;

    void prepare (double, int maxBlock) { interleaved.assign ((size_t) juce::jmax (1, maxBlock) * 2, 0.0f); }
    void reset() { silencePending = true; }

    // Teclado: Part 1 (canal 2 del MT-32, con el Program del panel) o Rhythm (canal 10). Multi (aparte): canales MIDI tal cual,
    // para ficheros .mid del MT-32 (partes en los canales 2-9 y ritmo en el 10). SysEx siempre (patches propios y mensajes del LCD)
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        const auto* m = slot.current.load (std::memory_order_acquire);
        if (m == nullptr)
            return;
        auto& synth = m->synth;
        if (m != lastMachine) // máquina nueva: sus ajustes se reenvían
        {
            lastMachine = m;
            lastReverb = lastDac = lastProgram = lastNice = -1;
        }
        if (silencePending)
        {
            silencePending = false;
            synth.flushMIDIQueue(); // lo que quede en la cola se ejecuta ya: ninguna nota encolada suena después del silencio
            for (juce::uint32 ch = 0; ch < 16; ++ch)
            {
                synth.playMsgNow (0xB0 | ch | (123u << 8)); // All Notes Off
                synth.playMsgNow (0xB0 | ch | (64u << 8));  // pedal arriba
            }
        }
        const auto reverb = p.mt32Reverb->load() > 0.5f ? 1 : 0;
        if (reverb != lastReverb) { lastReverb = reverb; synth.setReverbEnabled (reverb != 0); }
        auto dac = juce::jlimit (0, 4, (int) p.mt32Dac->load());
        if (dac != lastDac)
        {
            lastDac = dac;
            static const MT32Emu::DACInputMode modes[] = { MT32Emu::DACInputMode_NICE, MT32Emu::DACInputMode_PURE, MT32Emu::DACInputMode_GENERATION1,
                                       MT32Emu::DACInputMode_GENERATION2 };
            synth.setDACInputMode (dac == 0 ? (m->firstGeneration ? MT32Emu::DACInputMode_GENERATION1 : MT32Emu::DACInputMode_GENERATION2)
                                            : modes[dac - 1]);
        }
        const auto nice = (p.mt32NiceAmp->load() > 0.5f ? 1 : 0) | (p.mt32NicePan->load() > 0.5f ? 2 : 0) | (p.mt32NiceMix->load() > 0.5f ? 4 : 0);
        if (nice != lastNice)
        {
            lastNice = nice;
            synth.setNiceAmpRampEnabled ((nice & 1) != 0);
            synth.setNicePanningEnabled ((nice & 2) != 0);
            synth.setNicePartialMixingEnabled ((nice & 4) != 0);
        }
        const auto mode = p.mt32Multi->load() > 0.5f ? 2 : juce::jlimit (0, 1, (int) p.mt32Mode->load());
        const auto program = juce::jlimit (1, 128, (int) p.mt32Program->load());
        if (mode == 0 && program != lastProgram)
        {
            lastProgram = program;
            synth.playMsgNow (0xC1u | ((juce::uint32) (program - 1) << 8));
        }

        const auto n = out.getNumSamples();
        int done = 0;
        auto renderTo = [&] (int end)
        {
            while (done < end)
            {
                const auto chunk = juce::jmin (end - done, (int) interleaved.size() / 2);
                m->converter->getOutputSamples (interleaved.data(), (unsigned int) chunk);
                for (int i = 0; i < chunk; ++i)
                {
                    const auto l = interleaved[(size_t) (2 * i)], r = interleaved[(size_t) (2 * i + 1)];
                    if (out.getNumChannels() >= 2) { out.addSample (0, done + i, l); out.addSample (1, done + i, r); }
                    else if (out.getNumChannels() == 1) out.addSample (0, done + i, 0.5f * (l + r));
                }
                done += chunk;
            }
        };
        for (const auto meta : midi)
        {
            renderTo (juce::jlimit (0, n, meta.samplePosition));
            const auto msg = meta.getMessage();
            // Todo por la cola de Munt, en orden de llegada. Con playMsgNow, si la nota necesita abortar una poly (single-assign o
            // sin parciales libres) Munt sale sin tocarla (Part::playPoly) y se pierde: medido, 1 de cada 2 notas repetidas. La
            // cola la reintenta al terminar el aborto (Synth.cpp, doRenderStreams) y aplica el retardo MIDI de cable
            if (msg.isSysEx())
            {
                if (! synth.playSysex (msg.getRawData(), (MT32Emu::Bit32u) msg.getRawDataSize()))
                    synth.playSysexNow (msg.getRawData(), (MT32Emu::Bit32u) msg.getRawDataSize()); // no cabe en el búfer
                continue;
            }
            const auto* raw = msg.getRawData();
            const auto size = msg.getRawDataSize();
            if (size < 1 || raw[0] < 0x80 || raw[0] >= 0xF0)
                continue;
            auto status = (juce::uint32) raw[0];
            if (mode == 0)      status = (status & 0xF0) | 1u; // canal 2: parte 1
            else if (mode == 1) status = (status & 0xF0) | 9u; // canal 10: ritmo
            if (mode == 0 && (status & 0xF0) == 0xC0)
                continue; // en Part 1 el programa lo fija el panel (Program)
            const auto d1 = size > 1 ? (juce::uint32) raw[1] : 0u, d2 = size > 2 ? (juce::uint32) raw[2] : 0u;
            if (! synth.playMsg (status | (d1 << 8) | (d2 << 16)))
                synth.playMsgNow (status | (d1 << 8) | (d2 << 16)); // cola llena (1024 eventos)
        }
        renderTo (n);

        // LCD a ~10 Hz (lo dibuja la UI): seqlock de un escritor
        lcdCountdown -= n;
        if (lcdCountdown <= 0)
        {
            lcdCountdown = 4800;
            char text[lcdSize + 1] {};
            synth.getDisplayState (text, false);
            lcdSeq.fetch_add (1, std::memory_order_acq_rel);
            for (int i = 0; i < (int) lcdSize + 1; ++i)
                lcd[(size_t) i].store (text[i], std::memory_order_relaxed);
            lcdSeq.fetch_add (1, std::memory_order_release);
        }
    }

    // Hilo de mensajes: texto del LCD (vacío si aún no hay máquina)
    juce::String lcdText() const
    {
        char text[lcdSize + 1] {};
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto s0 = lcdSeq.load (std::memory_order_acquire);
            if ((s0 & 1) != 0) continue;
            for (int i = 0; i < (int) lcdSize + 1; ++i)
                text[i] = lcd[(size_t) i].load (std::memory_order_relaxed);
            if (lcdSeq.load (std::memory_order_acquire) == s0)
                break;
        }
        text[lcdSize] = 0;
        return juce::String (juce::CharPointer_ASCII (text));
    }

private:
    std::vector<float> interleaved;
    const Mt32Machine* lastMachine = nullptr;
    int lastReverb = -1, lastDac = -1, lastProgram = -1, lastNice = -1, lcdCountdown = 0;
    bool silencePending = false;
    std::atomic<juce::uint32> lcdSeq { 0 };
    std::array<std::atomic<char>, lcdSize + 1> lcd {};
};

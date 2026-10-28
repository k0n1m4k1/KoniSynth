#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "SimdUnison.h"
#include <BinaryData.h> // juce_add_binary_data (KoniSynthData): avatar del diálogo About
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h> // menú Options y aviso de la ventana standalone
#endif

using APVTS = juce::AudioProcessorValueTreeState;

#include "Settings/Config.h" // config.settings: ajustes globales (skin, AVX2, Agent, favoritos...)
#include "Params/Attributes.h"

#include "Engines/RhythmPatterns.h"
#include "Engines/UserRhythms.h"

#include "Dsp/ModMatrix.h"
#include "Params/Layout.h"

#include "Params/Help.h"

#include "Dsp/RandomClock.h"
#include "Dsp/Tuning.h"
#include "Params/Params.h"
#include "Params/PerfMacros.h"
#include "Params/UndoHistory.h"

#include "Presets/Factory.h"

#include "Presets/PresetManager.h"
#include "Agent/AudioTap.h"
#include "Agent/AgentScheduler.h"
#include "Transport/SessionTransport.h"
#include "Agent/AgentServer.h" // control por agente: inerte si está desactivado

#include "Dsp/OscillatorCore.h"
#include "Dsp/MinBlep.h"
#include "Dsp/AnalogEnvelope.h"
#include "Dsp/VoiceSaturator.h"

#include "Engines/SynthData.h"
#include "Dsp/Filters.h"
#include "Import/Brr.h"
#include "Import/Spc.h"
#include "Import/Sf2.h"
#include "Import/SampleImport.h"
#include "Import/Dx7Syx.h"
#include "Dsp/FrameMacro.h"

#include "Engines/Synth.h"

#include "Effects/PlateReverb.h"

#include "Effects/Chorus.h"

#include "Engines/Theremin.h"

#include "Engines/VL1.h"

#include "Engines/Stylophone.h"

#include "Dsp/Biquad.h"

#include "Dsp/StepClock.h"

#include "Engines/DrumMachine.h"

#include "Engines/Omnichord.h"

#include "Dsp/BlepBuffer.h"

#include "Engines/FmEngine.h"

#include "Engines/ChipEngine.h"

#include "Dsp/SincResampler.h"
#include "Dsp/ZohResampler.h"

#include "Engines/MegaDrive.h"

#include "Dsp/WaveSources.h"
#include "Engines/Snes.h"
#include "Engines/N64.h"
#include "Engines/Opll.h"
#include "Engines/Opl.h"
#include "Engines/Mt32.h"

#include "Engines/Polivoks.h"

#include "Engines/Ans.h"

#include "Effects/SpringReverb.h"
#include "Effects/Equalizer.h"
#include "Effects/Dynamics.h"
#include "Effects/AmpSim.h"

#include "Effects/FrequencyShifter.h"

#include "Effects/Leslie.h"

#include "Effects/Flanger.h"

#include "Effects/Vocoder.h"

#include "Effects/Formant.h"

#include "Effects/Underwater.h"

#include "Effects/Media.h"
#include "Effects/EasterEgg.h"

#include "Midi/MidiLearn.h"
#include "Midi/Controllers/ControllerProfile.h"
#include "Midi/Controllers/OxygenProMini.h"
#include "Midi/Controllers/CasioCtS400.h"
#include "Midi/Controllers/RefaceYc.h"
#include "Midi/Controllers/YamahaPsrEw425.h"

#include "Midi/Arpeggiator.h"

#include "Midi/StepSequencer.h"

#include "Midi/ScaleQuantizer.h"

#include "Midi/MonoNoteFilter.h"

//==============================================================================
class KoniSynthProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    // Entrada de audio (micro/línea) para el vocoder y el monitor; puede venir desactivada (layouts sin entrada)
    KoniSynthProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", createLayout()),
          presetManager (apvts),
          params (apvts)
    {
        for (int i = 0; i < 8; ++i)
        {
            auto* voice = new SynthVoice (params, sampleSlot, osc2SampleSlot, wavetableSlot, osc2WavetableSlot);
            voice->cardIndex = i; // tarjeta de voz física (Tolerances: Per Voice Card)
            synth.addVoice (voice);
        }
        synth.addSound (new SynthSound());
        synth.assignMode = params.voiceAssign;
        monoSynth.addVoice (new SynthVoice (params, sampleSlot, osc2SampleSlot, wavetableSlot, osc2WavetableSlot)); // Mono / Legato: una voz (el robo implementa la prioridad)
        monoSynth.addSound (new SynthSound());
        // Cada evento MIDI en su muestra: por defecto JUCE no parte el bloque si el siguiente evento está a menos de 32
        // muestras y lo adelanta (medido: notas a 1, 7 y 31 muestras de otra sonaban a la vez que ella; juce_Synthesiser.cpp:217).
        // Con 1 solo se agrupan eventos de la misma muestra; el bloque se parte solo donde hay eventos
        synth.setMinimumRenderingSubdivisionSize (1, true);
        monoSynth.setMinimumRenderingSubdivisionSize (1, true);
        perfMacros.attach (apvts);
        undoHistory.attach (*this, apvts, { PerfMacros::mapProp, PerfMacros::namesProp, chipMacrosProp, mdMacrosProp, snesMacrosProp,
                                            "presetName", "presetFactory", "dx7Voice" });
        presetManager.onBulkChange = [this] (bool begin) { if (begin) undoHistory.beginGroup(); else undoHistory.endGroup(); };

        migrateLegacyConfig(); // .txt de ajustes de versiones anteriores -> config.settings (Settings/Config.h)
        if (auto config = openConfig())
        {
            simdStatus().userEnabled = config->getBoolValue ("useAvx2", true);
            if (config->getBoolValue ("agentControl", false)) // desactivado por defecto
                setAgentEnabled (true, false);
        }

        syncRandomState(); // semilla de la aleatoriedad reproducible (Dsp/RandomClock.h)
        startTimer (100); // libera datos retirados (DataSlot::collect) y genera la wavetable incluida elegida
        presetManager.onPresetFiles = [this] (std::optional<juce::String> sample, std::optional<juce::String> wavetable,
                                              std::optional<juce::String> cabIr, juce::String osc2SamplePath,
                                              std::optional<juce::String> osc2Wavetable)
        {
            if (sample.has_value())    restoreSample (*sample);
            if (wavetable.has_value()) restoreWavetable (*wavetable);
            if (osc2Wavetable.has_value()) restoreWavetable (*osc2Wavetable, wtOsc2);
            if (cabIr.has_value())     restoreCabIr (*cabIr);
            restoreSample (osc2SamplePath, osc2Sample);
        };
        tuning.setMtsEnabled (true); // MTS-ESP por defecto: sin maestro (o sin LIBMTS instalada) no cambia nada
        actionPoll.tick = [this] { pollActions(); };
        actionPoll.startTimerHz (50);
    }

    ~KoniSynthProcessor() override { stopTimer(); }

    // Los dos samples del motor Synth (which): 0 = la fuente Sample (también la "User Sample" de los motores de consola y el
    // DPCM), 1 = Osc 2 en modo Sample. Mismo código para los dos; cada uno con su DataSlot, su ruta en el estado y su aviso
    static constexpr int sourceSample = 0, osc2Sample = 1;

    // Las dos wavetables del motor Synth (which): 0 = la fuente Wavetable, 1 = Osc 2 en modo Wavetable. Mismo código para las
    // dos; cada una con su DataSlot, su parámetro de tabla, su ruta en el estado y su aviso
    static constexpr int wtMain = 0, wtOsc2 = 1;

    // Carga un sample (cualquier hilo salvo el de audio). item: elemento de un banco .sf2/.spc (Import/SampleImport.h).
    // Devuelve el error o una cadena vacía.
    juce::String loadSample (const juce::File& file, const juce::String& item = {}, int which = sourceSample)
    {
        juce::String error;
        auto data = SampleImport::load (file, item, error);
        if (data == nullptr)
            return error;

        {
            const juce::ScopedLock sl (sampleInfoLock);
            auto& info = sampleInfos[(size_t) which];
            info.text = data->name + (data->truncated ? " (truncated to 60 s)" : "");
            info.rootKey = data->rootKey;
            info.hasLoop = data->loopStart >= 0;
        }
        slotFor (which).replace (std::move (data));
        apvts.state.setProperty (pathPropFor (which), SampleImport::pathOf (file, item), nullptr);
        return {};
    }

    // Tira de expresión del Gen X-2 tocada en el pad (UI/StylophonePad.h)
    void setStyloStrip (float v) { params.stripTouch.store (juce::jlimit (-1.0f, 1.0f, v)); }

    // Del último sample cargado: nota raíz del formato (-1 = desconocida) y si trae bucle propio
    std::pair<int, bool> getSampleFormatInfo (int which = sourceSample) const
    {
        const juce::ScopedLock sl (sampleInfoLock);
        return { sampleInfos[(size_t) which].rootKey, sampleInfos[(size_t) which].hasLoop };
    }

    void clearSample (int which = sourceSample)
    {
        slotFor (which).replace (nullptr);
        if (apvts.state.hasProperty (pathPropFor (which))) // vacía = ausente: cargar un estado sin la propiedad no la crea
            apvts.state.setProperty (pathPropFor (which), juce::String(), nullptr);
        const juce::ScopedLock sl (sampleInfoLock);
        sampleInfos[(size_t) which].text = {};
    }

    // MT-32 (cualquier hilo salvo el de audio): busca las ROM y publica la máquina. Un intento por modelo y frecuencia; "Rescan"
    // o una carpeta nueva lo repiten
    void rebuildMt32()
    {
        mt32BuiltModel = (int) params.mt32Model->load();
        mt32Rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        juce::String report;
        auto machine = Mt32Machine::open (mt32RomFolder(), mt32BuiltModel, mt32Rate, report);
        mt32Engine.slot.replace (std::move (machine));
        const juce::ScopedLock sl (sampleInfoLock);
        mt32Status = report;
    }
    void setMt32Folder (const juce::File& folder) { setMt32RomFolder (folder); rebuildMt32(); }
    juce::String getMt32Status() const { const juce::ScopedLock sl (sampleInfoLock); return mt32Status; }
    juce::String getMt32Lcd() const { return mt32Engine.lcdText(); }
    bool getMt32Led() const { return mt32Engine.lcdLed(); }
    juce::StringArray getMt32ProgramNames() const { return mt32Engine.programNames(); }
    juce::uint32 getMt32ProgramNamesVersion() const { return mt32Engine.programNamesVersion(); }
    juce::uint32 getVl1Notes() const { return vl1.displayNotes(); }
    int getVl1Pulse() const { return vl1.rhythmPulse(); }
    int getDrumLed() const { return drumMachine.currentLed(); }
    // Paso del ritmo que suena (-1 parado) para el editor de ritmos de cada motor
    long long getRhythmStep (UserRhythms::Target t) const
    {
        return t == UserRhythms::Target::omnichord ? omnichord.currentStep() : t == UserRhythms::Target::vl1 ? vl1.currentStep()
                                                                                                            : drumMachine.currentStep();
    }
    bool hasMt32Machine() const { bool has = false; mt32Engine.slot.withCurrent ([&has] (const Mt32Machine* m) { has = m != nullptr; }); return has; }

    juce::String getSampleInfo (int which = sourceSample) const
    {
        const juce::ScopedLock sl (sampleInfoLock);
        return sampleInfos[(size_t) which].text;
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        randomAnchorPending = true; // un render offline empieza aquí: anclaje de la aleatoriedad
        if (agentTap != nullptr)
        {
            if (agentScheduler != nullptr && agentTap->sampleRate() != sampleRate)
                agentScheduler->reset(); // notas del agente programadas a la frecuencia anterior: se descartan
            agentTap->prepare (sampleRate); // otra frecuencia: la captura se vacía y, si hace falta, se amplía
        }
        sessionTransport.prepare (sampleRate);
        // Una vez por proceso; decide si la ruta AVX2 es utilizable. Con KONISYNTH_SIMD_BENCH, su informe de medida va a
        // config.settings (clave simdBench)
        if (const auto bench = runSimdSelfTest(); bench.isNotEmpty())
            setConfigText ("simdBench", bench);
        synth.setCurrentPlaybackSampleRate (sampleRate);
        monoSynth.setCurrentPlaybackSampleRate (sampleRate);
        monoFilter.prepare();
        keyboardState.reset();
        theremin.prepare (sampleRate);
        vl1.prepare (sampleRate);
        stylophone.prepare (sampleRate);
        omnichord.prepare (sampleRate);
        drumMachine.prepare (sampleRate);
        updateUserRhythms(); // un render sin bucle de mensajes (bounce, KoniBench) también tiene el banco desde el primer bloque
        fmEngine.prepare (sampleRate);
        chipEngine.prepare (sampleRate, samplesPerBlock);
        polivoks.prepare (sampleRate);
        ansEngine.prepare (sampleRate);
        easterEgg.prepare (sampleRate);
        mdEngine.prepare (sampleRate, samplesPerBlock);
        snesEngine.prepare (sampleRate, samplesPerBlock);
        n64Engine.prepare (sampleRate, samplesPerBlock);
        oplEngine.prepare (sampleRate, samplesPerBlock);
        mt32Engine.prepare (sampleRate, samplesPerBlock);
        if ((int) params.engine->load() == 14 && sampleRate != mt32Rate) // la conversión de Munt depende de la frecuencia
            rebuildMt32();
        updateAnsScore(); // la partitura existe antes del primer bloque (no espera al temporizador)
        updateBuiltInWavetable();
        updateMacros();
        perfMacros.update();
        flanger.prepare (sampleRate);
        freqShifter.prepare (sampleRate);
        leslie.prepare (sampleRate);
        spring.prepare (sampleRate);
        equalizer.prepare (sampleRate);
        transient.prepare (sampleRate);
        compressor.prepare (sampleRate);
        limiter.prepare (sampleRate);
        ampSim.prepare (sampleRate, samplesPerBlock);
        setLatencySamples (currentLatency());
        arp.prepare (sampleRate);
        sequencer.prepare (sampleRate);
        noteScale.prepare();
        vocoder.prepare (sampleRate);
        formant.prepare (sampleRate);
        inputMono.assign ((size_t) juce::jmax (samplesPerBlock, 1), 0.0f);
        lastInputGain = juce::Decibels::decibelsToGain (params.inputGain->load());

        echo.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) getTotalNumOutputChannels() });
        echo.setMaximumDelayInSamples ((int) std::ceil (sampleRate * 1.1)); // 1000 ms máx + 10 % de margen
        echoDelay.reset (sampleRate, 0.05); // 50 ms de rampa al girar Time: sin clicks

        // Válvula: 2x (1 etapa) con half-band IIR polifásico: su latencia se comunica al host con la válvula encendida (currentLatency)
        tubeOversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) getTotalNumOutputChannels(), 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        tubeOversampling->initProcessing ((size_t) samplesPerBlock);
        maxBlockSize = samplesPerBlock;
        dcCoef = 1.0f - juce::MathConstants<float>::twoPi * 10.0f / (float) sampleRate; // DC blocker a 10 Hz

        chorus.prepare (sampleRate, samplesPerBlock);
        media.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
        underwater.prepare (sampleRate);

        reverb.prepare (sampleRate);

        lastGain = params.gain->load();
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& out = layouts.getMainOutputChannelSet();
        const auto& in  = layouts.getMainInputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
            && (in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }

    void processBlock (juce::AudioBuffer<float>& ioBuffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const KoniTuning::Scope tuningScope (tuning); // los motores leen la afinación de esta instancia (Dsp/Tuning.h)
        const auto numSamples  = ioBuffer.getNumSamples();
        const auto numChannels = juce::jmin (getTotalNumOutputChannels(), ioBuffer.getNumChannels());

        // Entrada: media de los canales de entrada × Input Gain (rampa por bloque), leída antes de limpiar el buffer
        // (entrada y salida comparten buffer). Bloque mayor que el anunciado: se amplía (asignación excepcional).
        if ((int) inputMono.size() < numSamples)
            inputMono.resize ((size_t) numSamples);
        const auto numInputs = juce::jmin (getTotalNumInputChannels(), ioBuffer.getNumChannels());
        const auto inputGain = juce::Decibels::decibelsToGain (params.inputGain->load());
        for (int i = 0; i < numSamples; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numInputs; ++ch)
                s += ioBuffer.getSample (ch, i);
            const auto g = lastInputGain + (inputGain - lastInputGain) * (float) i / (float) numSamples;
            inputMono[(size_t) i] = numInputs > 0 ? g * s / (float) numInputs : 0.0f;
        }
        lastInputGain = inputGain;

        ioBuffer.clear();
        // Vista con solo los canales de salida (con entrada estéreo y salida mono el buffer trae 2 canales)
        juce::AudioBuffer<float> buffer (ioBuffer.getArrayOfWritePointers(), numChannels, numSamples);

        // Canal MIDI de entrada: con un canal elegido se descartan los mensajes de canal de los demás (teclados que mandan el
        // acompañamiento por otros canales); los de sistema pasan siempre. Antes del teclado en pantalla, que no se filtra
        if (const auto inChannel = (int) params.midiInChannel->load(); inChannel > 0)
        {
            juce::MidiBuffer kept;
            for (const auto metadata : midi)
            {
                const auto m = metadata.getMessage();
                if (m.getChannel() == 0 || m.getChannel() == inChannel)
                    kept.addEvent (m, metadata.samplePosition);
            }
            midi.swapWith (kept);
        }

        // CC 120-127 son mensajes de modo de canal y la especificación MIDI exige valor 0 (salvo Local Control 122 y
        // Mono Mode 126, que KoniSynth no implementa). JUCE no mira el valor (juce_MidiMessage.cpp:648-663): un knob de
        // controlador asignado a esos CC (posible en el Oxygen Pro) cortaría el sonido al girarlo. Se descartan si valor != 0.
        {
            bool modeJunk = false;
            for (const auto metadata : midi)
            {
                const auto m = metadata.getMessage();
                if (m.isController() && m.getControllerNumber() >= 120 && m.getControllerValue() != 0)
                {
                    modeJunk = true;
                    if (midiLearn.learning.load() >= 0)
                        midiLearn.rejectedCc.store (m.getControllerNumber());
                }
            }
            if (modeJunk)
            {
                juce::MidiBuffer kept;
                for (const auto metadata : midi)
                {
                    const auto m = metadata.getMessage();
                    if (! (m.isController() && m.getControllerNumber() >= 120 && m.getControllerValue() != 0))
                        kept.addEvent (m, metadata.samplePosition);
                }
                midi.swapWith (kept);
            }
        }

        // Transporte: eventos del .mid en reproducción (antes de leer bend, rueda y CC) y notas que deja al parar
        sessionTransport.injectMidi (midi, numSamples);
        tuning.beginBlock (midi); // MTS-ESP: ¿hay maestro? y SysEx MTS del controlador

        // MIDI learn y All Sound Off (CC 120): JUCE lo trata como All Notes Off con release (juce_Synthesiser.cpp:278),
        // pero "sound off" debe silenciar ya, colas de efectos incluidas -> panic completo
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isAllSoundOff())
                panicRequested = true;
            midiLearn.handle (message, getParameters());
            const auto mpe = params.mpeOn->load() > 0.5f;
            if (message.isPitchWheel() && ! (mpe && message.getChannel() != 1)) // 0..16383, centro 8192; en MPE solo el maestro
                params.pitchBend = (float) (message.getPitchWheelValue() - 8192) / 8192.0f * params.bendRange->load();
            else if (message.isControllerOfType (74))
                params.channelTimbre[(size_t) juce::jlimit (1, 16, message.getChannel()) - 1].store ((float) message.getControllerValue() / 127.0f);
            else if (message.isControllerOfType (1))
                params.modWheel = (float) message.getControllerValue() / 127.0f;
        }
        perfMacros.apply(); // macros de interpretación: tras el MIDI learn, antes de leer los parámetros del bloque
        if (midiLearn.takeAction (MidiLearn::actionPanic)) // botón PANIC asignado a un CC: en este mismo bloque
            panicRequested = true;

        if (panicRequested.exchange (false))
            panic();
        // Calidad del ladder pedida: se aplica cuando no suena ninguna voz del Synth (también justo tras un panic). Así todas las
        // voces usan siempre el mismo modo y ninguna cambia de frecuencia interna ni de estados a mitad de una nota
        if (const auto q = filterQualityRequested.load(); q != params.ladderQuality.load() && ! anySynthVoiceActive())
            params.ladderQuality.store (q);

        keyboardState.processNextMidiBuffer (midi, 0, numSamples, true); // mezcla las notas del teclado en pantalla
        // Notas de play del agente, en su muestra exacta (Agent/AgentScheduler.h); sin agente, nulo. La posición en la captura
        // es la del inicio de este bloque (el tap lo recibe al final)
        if (auto* scheduler = agentSchedulerActive.load (std::memory_order_acquire))
        {
            const auto* tap = agentTapActive.load (std::memory_order_acquire);
            scheduler->process (midi, numSamples, getSampleRate(), tap != nullptr ? tap->framesWritten() : 0);
        }
        sessionTransport.captureMidi (midi); // REC: lo que entra al motor (controlador, teclado y .mid), antes de secuenciador y arpegiador
        {
            // Transpose y escala (Midi/ScaleQuantizer.h). En la Drum Machine y la parte de ritmo del MT-32 la nota elige el
            // sonido: ahí no se transpone ni se ajusta
            const auto eng = (int) params.engine->load();
            const auto noteIsSound = eng == 5 || (eng == 14 && params.mt32Mode->load() > 0.5f);
            noteScale.process (midi, (int) params.keyTranspose->load(), (int) params.scaleMode->load(), (int) params.scaleRoot->load(),
                           (int) params.scaleType->load(), (int) params.scaleRound->load(), noteIsSound);
        }
        tuning.filterNotes (midi); // teclas sin mapear del .kbm ('x') o filtradas por el maestro MTS-ESP

        // Arpegiador: sustituye las notas por el patrón (tempo y posición del host si los hay)
        StepClock::Transport transport; // reloj de los ritmos de Omnichord y Drum Machine
        AudioTap::Clock tapClock;       // lo mismo, para el agente (solo se publica con el control por agente activo)
        {
            Arpeggiator::HostTime host;
            std::optional<juce::int64> hostSamples;
            if (auto* hostPlayHead = getPlayHead())
                if (const auto pos = hostPlayHead->getPosition())
                {
                    if (const auto bpm = pos->getBpm()) host.bpm = *bpm;
                    if (const auto ppq = pos->getPpqPosition()) host.ppq = *ppq;
                    host.playing = pos->getIsPlaying();
                    if (const auto ts = pos->getTimeInSamples()) hostSamples = *ts;
                }
            params.tempoBpm = host.bpm.value_or ((double) params.arpBpm->load());
            {
                auto shown = params.tempoBpm.load();
                if (! host.bpm.has_value())
                    switch ((int) params.engine->load())
                    {
                        case 5: shown = params.drumTempo->load(); break;
                        case 4: shown = params.omniTempo->load(); break;
                        default: break;
                    }
                // El VL-1 tiene reloj propio (no sigue al host): negra = 32 ticks de (5,5 + (9 - tempo)·0,98) ms
                if ((int) params.engine->load() == 2)
                    shown = 60.0 / (32.0 * (0.0055 + (9.0 - params.vl1Tempo->load()) * 0.00098));
                params.displayBpm.store ((float) shown, std::memory_order_relaxed);
                params.displayBpmFromHost.store (host.bpm.has_value() && (int) params.engine->load() != 2, std::memory_order_relaxed);
                tapClock.displayBpm = (float) shown;
                tapClock.displayFromHost = host.bpm.has_value() && (int) params.engine->load() != 2;
            }
            tapClock.hasBpm = host.bpm.has_value();
            tapClock.bpm = host.bpm.value_or (0.0);
            tapClock.hasPpq = host.ppq.has_value();
            tapClock.ppq = host.ppq.value_or (0.0);
            tapClock.playing = host.playing;
            tapClock.hasTime = hostSamples.has_value();
            tapClock.timeSamples = hostSamples.value_or (0);
            tapClock.tempoBpm = params.tempoBpm.load();
            updateRandomClock (host.playing ? hostSamples : std::nullopt, numSamples); // antes del arpegiador (usa aleatorio)
            transport = { host.bpm, host.ppq, host.playing };
            sequencer.process (midi, numSamples, params, host); // si está activo, sustituye las notas antes que el arpegiador
            if (params.seqOn->load() < 0.5f)
                arp.process (midi, numSamples, params, host);
        }

        // Motor: Synth (voces), Theremin, Casio VL-1 o Stylophone; al cambiar de motor se cortan las notas para que no queden congeladas
        const auto engine = (int) params.engine->load();
        const auto thereminOn = engine == 1;
        const auto vl1On = engine == 2;
        const auto styloOn = engine == 3;
        const auto omniOn = engine == 4;
        const auto drumOn = engine == 5;
        const auto fmOn = engine == 6;
        const auto chipOn = engine == 7;
        const auto pvOn = engine == 8;
        const auto ansOn = engine == 9;
        const auto mdOn = engine == 10;
        const auto snesOn = engine == 11;
        const auto n64On = engine == 12;
        const auto oplOn = engine == 13;
        const auto mt32On = engine == 14;
        if (thereminOn)
        {
            if (! thereminWasOn)
            {
                allVoicesOff();
                theremin.reset();
            }
            theremin.render (buffer, midi, params);
        }
        else if (vl1On)
        {
            if (! vl1WasOn)
            {
                allVoicesOff();
                vl1.reset();
            }
            vl1.render (buffer, midi, params, rhythmBank (UserRhythms::Target::vl1));
        }
        else if (styloOn)
        {
            if (! styloWasOn)
            {
                allVoicesOff();
                stylophone.reset();
            }
            stylophone.render (buffer, midi, params);
        }
        else if (omniOn)
        {
            if (! omniWasOn)
            {
                allVoicesOff();
                omnichord.reset();
            }
            omnichord.render (buffer, midi, params, transport, rhythmBank (UserRhythms::Target::omnichord));
        }
        else if (drumOn)
        {
            if (! drumWasOn)
            {
                allVoicesOff();
                drumMachine.reset();
            }
            drumMachine.render (buffer, midi, params, transport, rhythmBank (UserRhythms::Target::drums));
        }
        else if (fmOn)
        {
            if (! fmWasOn)
            {
                allVoicesOff();
                fmEngine.reset();
            }
            fmEngine.render (buffer, midi, params);
        }
        else if (chipOn)
        {
            if (! chipWasOn)
            {
                allVoicesOff();
                chipEngine.reset();
            }
            chipEngine.render (buffer, midi, params, sampleSlot.current.load (std::memory_order_acquire), chipMacroSlot.current.load (std::memory_order_acquire));
        }
        else if (pvOn)
        {
            if (! pvWasOn)
            {
                allVoicesOff();
                polivoks.reset();
            }
            polivoks.render (buffer, midi, params);
        }
        else if (oplOn)
        {
            if (! oplWasOn)
            {
                allVoicesOff();
                oplEngine.reset();
            }
            oplEngine.render (buffer, midi, params);
        }
        else if (mt32On)
        {
            if (! mt32WasOn)
            {
                allVoicesOff();
                mt32Engine.reset();
            }
            mt32Engine.render (buffer, midi, params);
        }
        else if (n64On)
        {
            if (! n64WasOn)
            {
                allVoicesOff();
                n64Engine.reset();
            }
            n64Engine.render (buffer, midi, params, sampleSlot.current.load (std::memory_order_acquire), (int) params.sampleRoot->load());
        }
        else if (snesOn)
        {
            if (! snesWasOn)
            {
                allVoicesOff();
                snesEngine.reset();
            }
            snesEngine.render (buffer, midi, params, sampleSlot.current.load (std::memory_order_acquire), (int) params.sampleRoot->load(),
                               snesMacroSlot.current.load (std::memory_order_acquire));
        }
        else if (mdOn)
        {
            if (! mdWasOn)
            {
                allVoicesOff();
                mdEngine.reset();
            }
            mdEngine.render (buffer, midi, params, sampleSlot.current.load (std::memory_order_acquire), (int) params.sampleRoot->load(),
                             mdMacroSlot.current.load (std::memory_order_acquire));
        }
        else if (ansOn)
        {
            if (! ansWasOn)
            {
                allVoicesOff();
                ansEngine.reset();
            }
            ansEngine.render (buffer, midi, params, ansSlot.current.load (std::memory_order_acquire));
        }
        else
        {
            // Poly: 8 voces. Mono/Legato: filtro monofónico + 1 voz. Al cambiar de modo se corta el que deja de usarse
            const auto mono = (int) params.voiceMode->load() != 0;
            if (mono != lastMono)
            {
                (mono ? synth : monoSynth).allNotesOff (0, false);
                monoFilter.reset();
                lastMono = mono;
            }
            if (mono)
            {
                monoFilter.process (midi);
                monoSynth.renderNextBlock (buffer, midi, 0, numSamples);
            }
            else
            {
                synth.renderNextBlock (buffer, midi, 0, numSamples);
            }
        }
        thereminWasOn = thereminOn;
        vl1WasOn = vl1On;
        styloWasOn = styloOn;
        omniWasOn = omniOn;
        drumWasOn = drumOn;
        fmWasOn = fmOn;
        chipWasOn = chipOn;
        pvWasOn = pvOn;
        ansWasOn = ansOn;
        mdWasOn = mdOn;
        snesWasOn = snesOn;
        n64WasOn = n64On;
        oplWasOn = oplOn;
        mt32WasOn = mt32On;

        // Captura "engine" del agente: la salida del motor con sus efectos propios, antes de todo lo añadido (Agent/AudioTap.h)
        if (auto* tap = agentTapActive.load (std::memory_order_acquire))
            tap->pushEngine (buffer);

        // Vocoder: la salida del motor es la portadora y la entrada la moduladora
        const auto vocoderOn = params.vocoderOn->load() > 0.5f;
        if (vocoderOn)
        {
            if (! vocoderWasOn)
                vocoder.reset();
            vocoder.process (buffer, inputMono.data(), params);
        }
        vocoderWasOn = vocoderOn;

        // Monitor: la entrada entra en la cadena de efectos (micro como fuente: válvula, echo, reverb, soporte...)
        if (const auto monitor = params.inputMonitor->load(); monitor > 0.0f)
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom (ch, 0, inputMono.data(), numSamples, monitor);

        // Cadena de efectos: los 6 grupos en el orden elegido (fxOrder; por defecto COLOR, AMP, DYNAMICS, MOD FX, ENSEMBLE,
        // SPACE, el de siempre). OUTPUT (soporte, Gain, Mono y limitador) va siempre al final, como el máster
        const auto fxOrder = fxOrderPacked.load (std::memory_order_relaxed);
        for (int slot = 0; slot < numFxGroups; ++slot)
        {
            switch ((int) (fxOrder >> (4 * slot)) & 0xF)
            {
                case fxColor:
                {
                // Crush: sample & hold a Crush Rate (fase acumulada: tasas no enteras sin deriva) y cuantización a 2^bits niveles
                // repartidos en [-1, 1] (paso 2/(2^bits - 1); el 0 no es un nivel exacto). Sin antialias: el aliasing es el carácter buscado
                if (params.crushOn->load() > 0.5f)
                {
                    const auto steps = std::exp2 (params.crushBits->load()) - 1.0f;
                    const auto holdInc = params.crushRate->load() / (float) getSampleRate();
                    const auto mix = params.crushMix->load();
                    const auto drive = juce::Decibels::decibelsToGain (params.crushDrive->load());
                    for (int i = 0; i < numSamples; ++i)
                    {
                        crushPhase += holdInc;
                        const auto take = crushPhase >= 1.0f;
                        if (take)
                            crushPhase -= std::floor (crushPhase);
                        for (int ch = 0; ch < numChannels; ++ch)
                        {
                            const auto x = buffer.getSample (ch, i);
                            if (take)
                            {
                                const auto c = juce::jlimit (-1.0f, 1.0f, x * drive);
                                crushHeld[(size_t) ch] = (std::round ((c + 1.0f) * 0.5f * steps) / steps * 2.0f - 1.0f) / drive;
                            }
                            buffer.setSample (ch, i, x + mix * (crushHeld[(size_t) ch] - x));
                        }
                    }
                }

                // Filtro de formantes (tras el crush, antes de la válvula)
                if (params.formantOn->load() > 0.5f)
                {
                    if (! formantWasOn)
                        formant.reset();
                    formant.process (buffer, params);
                }
                formantWasOn = params.formantOn->load() > 0.5f;

                processTube (buffer);

                // Ecualizador (tras la válvula, antes de la modulación): ver Effects/Equalizer.h
                const auto eqOn = params.eqOn->load() > 0.5f;
                if (eqOn)
                {
                    if (! eqWasOn) equalizer.reset();
                    static constexpr int orders[] = { 1, 2, 4, 0 }; // 0 = un polo (6 dB/oct), Effects/Equalizer.h
                    Eq::Settings es;
                    es.lowCut = params.eqLowCut->load(); es.highCut = params.eqHighCut->load();
                    es.lowFreq = params.eqLowFreq->load(); es.lowGain = params.eqLowGain->load();
                    es.mid1Freq = params.eqMid1Freq->load(); es.mid1Gain = params.eqMid1Gain->load(); es.mid1Q = params.eqMid1Q->load();
                    es.mid2Freq = params.eqMid2Freq->load(); es.mid2Gain = params.eqMid2Gain->load(); es.mid2Q = params.eqMid2Q->load();
                    es.highFreq = params.eqHighFreq->load(); es.highGain = params.eqHighGain->load(); es.outputDb = params.eqOutput->load();
                    es.lowCutOrder = orders[juce::jlimit (0, 3, (int) params.eqLowCutSlope->load())];
                    es.highCutOrder = orders[juce::jlimit (0, 3, (int) params.eqHighCutSlope->load())];
                    equalizer.process (buffer, es);
                }
                eqWasOn = eqOn;
                    break;
                }
                case fxAmp:
                {
                // Ampli + pantalla (tras el color, antes de la dinámica): ver Effects/AmpSim.h
                const auto ampOn = params.ampOn->load() > 0.5f;
                if (ampOn)
                {
                    if (! ampWasOn) ampSim.reset();
                    AmpSim::Settings as;
                    as.model = (int) params.ampModel->load(); as.cab = (int) params.cabType->load();
                    as.gain = params.ampGain->load(); as.bass = params.ampBass->load(); as.mid = params.ampMid->load();
                    as.treble = params.ampTreble->load(); as.presence = params.ampPresence->load(); as.master = params.ampMaster->load();
                    as.sag = params.ampSag->load(); as.outputDb = params.ampOutput->load(); as.cabMix = params.cabMix->load();
                    ampSim.process (buffer, as);
                }
                ampWasOn = ampOn;
                    break;
                }
                case fxDynamics:
                {
                // Dinámica: transitorios y compresor (tras el EQ, antes de la modulación): ver Effects/Dynamics.h
                const auto transOn = params.transOn->load() > 0.5f;
                if (transOn)
                {
                    if (! transWasOn) transient.reset();
                    transient.process (buffer, params.transAttack->load(), params.transSustain->load());
                }
                transWasOn = transOn;
                const auto compOn = params.compOn->load() > 0.5f;
                if (compOn)
                {
                    if (! compWasOn) compressor.reset();
                    Compressor::Settings cs;
                    cs.mode = juce::jlimit (0, 2, (int) params.compMode->load());
                    cs.thresholdDb = params.compThreshold->load(); cs.ratio = params.compRatio->load();
                    cs.attackMs = params.compAttack->load(); cs.releaseMs = params.compRelease->load();
                    cs.kneeDb = params.compKnee->load(); cs.makeupDb = params.compMakeup->load(); cs.mix = params.compMix->load();
                    cs.scHpf = params.compScHpf->load(); cs.external = (int) params.compSource->load() == 1;
                    compressor.process (buffer, inputMono.data(), cs);
                }
                else
                    compressor.meterGr.store (0.0f, std::memory_order_relaxed);
                compWasOn = compOn;
                    break;
                }
                case fxModFx:
                {
                // Phaser: 4 pasa-todo de 1er orden en serie, y = a·x + x1 - a·y1 con a = (t - 1)/(t + 1), t = tan(π·fc/fs).
                // fc = 1 kHz · 2^(3·Depth·lfo) (±3 octavas a Depth 100 %: rango elegido, no medido del Small Stone); LFO seno con el
                // canal derecho desfasado 90°; Color realimenta la salida de la 4ª etapa a la entrada.
                if (params.phaserOn->load() > 0.5f)
                {
                    const auto sr = getSampleRate();
                    const auto inc = params.phaserRate->load() / sr;
                    const auto depth = 3.0 * params.phaserDepth->load();
                    const auto fb = params.phaserFeedback->load();
                    const auto mix = params.phaserMix->load();
                    for (int i = 0; i < numSamples; ++i)
                    {
                        phaserPhase += inc;
                        if (phaserPhase >= 1.0) phaserPhase -= 1.0;
                        for (int ch = 0; ch < numChannels; ++ch)
                        {
                            const auto lfo = std::sin (juce::MathConstants<double>::twoPi * (phaserPhase + 0.25 * ch));
                            const auto fc = juce::jlimit (20.0, 0.45 * sr, 1000.0 * std::exp2 (depth * lfo));
                            const auto t = std::tan (juce::MathConstants<double>::pi * fc / sr);
                            const auto a = (float) ((t - 1.0) / (t + 1.0));
                            auto& st = phaserState[(size_t) ch];
                            const auto x = buffer.getSample (ch, i);
                            auto v = x + fb * st.last;
                            for (int k = 0; k < 4; ++k)
                            {
                                const auto y = a * v + st.x1[(size_t) k] - a * st.y1[(size_t) k];
                                st.x1[(size_t) k] = v;
                                st.y1[(size_t) k] = y;
                                v = y;
                            }
                            st.last = v;
                            buffer.setSample (ch, i, (1.0f - mix) * x + mix * v);
                        }
                    }
                }

                // Flanger y desplazador de frecuencia (tras el phaser, antes del chorus)
                const auto flangerOn = params.flangerOn->load() > 0.5f;
                if (flangerOn)
                {
                    if (! flangerWasOn) flanger.reset();
                    flanger.process (buffer, params.flangerRate->load(), params.flangerRange->load(), params.flangerColor->load(),
                                     params.flangerMatrix->load() > 0.5f, params.flangerMix->load());
                }
                flangerWasOn = flangerOn;
                const auto fshiftOn = params.fshiftOn->load() > 0.5f;
                if (fshiftOn)
                {
                    if (! fshiftWasOn) freqShifter.reset();
                    freqShifter.process (buffer, params.fshiftHz->load(), params.fshiftBalance->load(), params.fshiftMix->load());
                }
                fshiftWasOn = fshiftOn;
                    break;
                }
                case fxEnsemble:
                {
                const auto chorusOn = params.chorusOn->load() > 0.5f;
                if (chorusOn)
                {
                    if (! chorusWasOn)
                        chorus.reset();
                    chorus.process (buffer, (int) params.chorusMode->load(), params.chorusDepth->load(), params.chorusMix->load());
                }
                chorusWasOn = chorusOn;

                // Leslie (después del chorus, antes del eco: el altavoz y luego la sala)
                const auto leslieOn = params.leslieOn->load() > 0.5f;
                if (leslieOn)
                {
                    if (! leslieWasOn) leslie.reset();
                    leslie.process (buffer, (int) params.leslieSpeed->load(), params.leslieDrive->load(), params.leslieMix->load());
                }
                leslieWasOn = leslieOn;
                    break;
                }
                case fxSpace:
                {
                // Echo de cinta: d = línea con tiempo modulado (wow/flutter), t = pasa-bajos(d) (Tone),
                // y = x + mix·t,  línea <- x + feedback·tanh(1,5·t)/1,5 (pendiente 1 en 0: lineal a nivel bajo, acota la autooscilación)
                const auto sampleRate = (float) getSampleRate();
                const auto echoOn = params.echoOn->load() > 0.5f;
                static constexpr float echoBeats[] = { 0.0f, 1.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
                const auto echoSync = juce::jlimit (0, 7, (int) params.echoSync->load());
                const auto echoMs = echoSync == 0 ? params.echoTime->load()
                                                  : juce::jlimit (10.0f, 1000.0f, (float) (60000.0 / params.tempoBpm.load()) * echoBeats[echoSync]);
                const auto echoTarget = echoMs * 0.001f * sampleRate;

                if (echoOn)
                {
                    if (! echoWasOn) // al reactivar: sin ecos residuales ni barrido de tiempo
                    {
                        echo.reset();
                        echoDelay.setCurrentAndTargetValue (echoTarget);
                        toneState = {};
                    }

                    echoDelay.setTargetValue (echoTarget);
                    const auto feedback = params.echoFeedback->load();
                    const auto mix      = params.echoMix->load();
                    const auto toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * params.echoTone->load() / sampleRate);
                    const auto wowDepth = params.echoWow->load() * sampleRate;   // en muestras por segundo de desviación
                    constexpr auto twoPi = juce::MathConstants<float>::twoPi;
                    const auto pingPong = numChannels == 2 && params.echoPingPong->load() > 0.5f;

                    for (int i = 0; i < numSamples; ++i)
                    {
                        wowPhase     += 0.5f / sampleRate;
                        flutterPhase += 6.3f / sampleRate;
                        if (wowPhase >= 1.0f)     wowPhase -= 1.0f;
                        if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;

                        // ±2 ms a 0,5 Hz -> ±0,63 % de pitch; ±0,15 ms a 6,3 Hz -> ±0,59 % (a Wow 100 %)
                        const auto mod = wowDepth * (0.002f * std::sin (twoPi * wowPhase) + 0.00015f * std::sin (twoPi * flutterPhase));
                        const auto delay = echoDelay.getNextValue() + mod;

                        if (pingPong)
                        {
                            // L <- entrada en mono + realimentación de R; R <- realimentación de L: repeticiones L, R, L...
                            const auto xl = buffer.getSample (0, i), xr = buffer.getSample (1, i);
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                auto& t = toneState[(size_t) ch];
                                t += toneCoef * (echo.popSample (ch, delay) - t);
                            }
                            const auto tl = toneState[0], tr = toneState[1];
                            echo.pushSample (0, 0.5f * (xl + xr) + feedback * std::tanh (1.5f * tr) / 1.5f);
                            echo.pushSample (1, feedback * std::tanh (1.5f * tl) / 1.5f);
                            buffer.setSample (0, i, xl + mix * tl);
                            buffer.setSample (1, i, xr + mix * tr);
                            continue;
                        }
                        for (int ch = 0; ch < numChannels; ++ch)
                        {
                            const auto x = buffer.getSample (ch, i);
                            const auto d = echo.popSample (ch, delay);
                            auto& t = toneState[(size_t) ch];
                            t += toneCoef * (d - t);
                            echo.pushSample (ch, x + feedback * std::tanh (1.5f * t) / 1.5f);
                            buffer.setSample (ch, i, x + mix * t);
                        }
                    }
                }
                echoWasOn = echoOn;

                // Reverb de placa (Dattorro). Mezcla seco/húmedo a potencia constante, como el chorus
                const auto ambientOn = params.ambientOn->load() > 0.5f;

                if (ambientOn)
                {
                    if (! ambientWasOn)
                        reverb.reset();

                    reverb.process (buffer, params.ambientSize->load(), params.reverbDecay->load(), params.reverbPredelay->load(),
                                    params.reverbDiffusion->load(), params.reverbDamp->load(), params.reverbTone->load(),
                                    params.reverbMod->load(), params.reverbWidth->load(), params.reverbFreeze->load() > 0.5f,
                                    params.ambientMix->load());
                }
                ambientWasOn = ambientOn;

                // Reverb de muelles (tras la de placa)
                const auto springOn = params.springOn->load() > 0.5f;
                if (springOn)
                {
                    if (! springWasOn) spring.reset();
                    spring.process (buffer, params.springDecay->load(), params.springMix->load());
                }
                springWasOn = springOn;
                    break;
                }
                default: break;
            }
        }

        // Media: la mezcla completa se "graba" en el soporte (antes del Gain, como un máster)
        const auto mediaOn = params.mediaOn->load() > 0.5f;
        if (mediaOn)
        {
            if (! mediaWasOn)
            {
                media.reset();
                underwater.reset();
            }
            if ((int) params.mediaType->load() == 5)
                underwater.process (buffer, params.mediaAge->load(), params.mediaNoise->load(), params.mediaWow->load(), params.mediaDrive->load());
            else
                media.process (buffer, (int) params.mediaType->load(), params.mediaAge->load(),
                               params.mediaNoise->load(), params.mediaWow->load(), params.mediaDrive->load());
        }
        mediaWasOn = mediaOn;

        const auto g = params.gain->load();
        buffer.applyGainRamp (0, numSamples, lastGain, g); // rampa por bloque: evita zipper noise
        lastGain = g;
        if (params.monoOut->load() > 0.5f && buffer.getNumChannels() >= 2)
        {
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
                l[i] = r[i] = 0.5f * (l[i] + r[i]);
        }

        // Limitador: lo último de la cadena (tras Gain y Mono). La anticipación (y el sobremuestreo del ampli) es latencia:
        // se comunica al host al cambiar
        const auto lookahead = currentLookahead();
        if (const auto total = currentLatency(); total != getLatencySamples())
            setLatencySamples (total);
        const auto limOn = params.limOn->load() > 0.5f;
        if (limOn)
        {
            if (! limWasOn) limiter.reset();
            limiter.process (buffer, params.limCeiling->load(), params.limRelease->load(), lookahead);
        }
        else
            limiter.meterGr.store (0.0f, std::memory_order_relaxed);
        limWasOn = limOn;

        easterEgg.render (buffer); // huevo de pascua del About: se suma al final, fuera de la cadena (solo con el About abierto)
        sessionTransport.processAudio (buffer); // PLAY de un fichero de audio (se suma) y REC de lo que sale
        pushToScope (buffer);
        if (auto* tap = agentTapActive.load (std::memory_order_acquire)) // control por agente (F3); desactivado: nulo
        {
            tapClock.latencySamples = getLatencySamples();
            tap->push (buffer, tapClock);
        }
        sampleSlot.blocksDone.fetch_add (1, std::memory_order_release); // ver DataSlot
        for (auto& b : rhythmBanks) b.slot.blocksDone.fetch_add (1, std::memory_order_release);
        osc2SampleSlot.blocksDone.fetch_add (1, std::memory_order_release);
        ansSlot.blocksDone.fetch_add (1, std::memory_order_release);
        chipMacroSlot.blocksDone.fetch_add (1, std::memory_order_release);
        mdMacroSlot.blocksDone.fetch_add (1, std::memory_order_release);
        snesMacroSlot.blocksDone.fetch_add (1, std::memory_order_release);
        wavetableSlot.blocksDone.fetch_add (1, std::memory_order_release);
        osc2WavetableSlot.blocksDone.fetch_add (1, std::memory_order_release);
        mt32Engine.slot.blocksDone.fetch_add (1, std::memory_order_release);
    }

    // Ring buffer mono para el osciloscopio: escribe el hilo de audio, lee el de UI (lock-free).
    static constexpr int scopeSize = 4096; // potencia de 2: índice = pos & (scopeSize - 1)
    std::array<std::atomic<float>, scopeSize> scopeLeft {}, scopeRight {}; // display (UI/Visualizer.h): L y R
    std::atomic<int> scopeWritePos { 0 };
    // Tempo efectivo para el LED del editor (lo escribe processBlock)
    float getDisplayBpm() const noexcept       { return params.displayBpm.load (std::memory_order_relaxed); }
    bool isDisplayBpmFromHost() const noexcept { return params.displayBpmFromHost.load (std::memory_order_relaxed); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override  { return JucePlugin_Name; }
    bool acceptsMidi() const override            { return true; }
    bool producesMidi() const override           { return false; }
    bool isMidiEffect() const override           { return false; }
    // Cola: release (hasta 4 s) + RT60 de la reverb + predelay, mínimo 5 s. Con Freeze la cola es indefinida:
    // se informa del máximo del parámetro (30 s) en lugar de infinito.
    double getTailLengthSeconds() const override
    {
        const auto reverbTail = params.ambientOn->load() > 0.5f
                                    ? (params.reverbFreeze->load() > 0.5f ? 30.0 : (double) params.reverbDecay->load())
                                      + 0.001 * params.reverbPredelay->load()
                                    : 0.0;
        return juce::jmax (5.0, 4.0 + reverbTail + (params.springOn->load() > 0.5f ? (double) params.springDecay->load() : 0.0));
    }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
        {
            // Valor real guardado = punto fijo de real(normalizado(v)) (curvas pow en float: el viaje de ida y vuelta mueve el
            // último bit). Así guardar, cargar y volver a guardar da los mismos bytes (clap-validator,
            // state-reproducibility-binary); diferencia con el valor sin ajustar: 1-2 ulp, inaudible
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (e->getStringAttribute ("id"))))
                {
                    const auto original = (float) e->getDoubleAttribute ("value");
                    auto v = original;
                    for (int i = 0; i < 4; ++i)
                    {
                        const auto next = (float) p->convertFrom0to1 (p->convertTo0to1 (v));
                        if (next == v) break;
                        v = next;
                    }
                    if (v != original)
                        e->setAttribute ("value", (double) v);
                }
            // Asignaciones de MIDI learn por ID de parámetro (estable aunque cambie el orden de los parámetros)
            auto* learnXml = xml->createNewChildElement ("MIDILEARN");
            const auto& allParams = getParameters();
            for (int cc = 0; cc < 120; ++cc)
            {
                const auto index = midiLearn.ccToParam[(size_t) cc].load();
                if (auto* p = index >= 0 && index < allParams.size() ? dynamic_cast<juce::AudioProcessorParameterWithID*> (allParams[index]) : nullptr)
                {
                    auto* map = learnXml->createNewChildElement ("MAP");
                    map->setAttribute ("cc", cc);
                    map->setAttribute ("param", p->paramID);
                }
                else if (index >= MidiLearn::actionBase && index < MidiLearn::actionBase + MidiLearn::numActions)
                {
                    auto* map = learnXml->createNewChildElement ("MAP");
                    map->setAttribute ("cc", cc);
                    map->setAttribute ("param", juce::String ("action:") + MidiLearn::actionId (index - MidiLearn::actionBase));
                }
            }
            // KSL de los operadores OPL en dB/oct (PresetManager::swapOplKsl): un estado sin esta marca es anterior
            xml->setAttribute (kslDbAttribute, 1);
            copyXmlToBinary (*xml, dest);
        }
    }
    static constexpr const char* kslDbAttribute = "kslDb";

    // Grupos de la cadena de efectos (bloques COLOR..SPACE del editor), en su orden de siempre
    enum FxGroup { fxColor = 0, fxAmp, fxDynamics, fxModFx, fxEnsemble, fxSpace, numFxGroups };

    // "3,0,1,2,4,5" -> orden; si no es una permutación de los 6 grupos, el de siempre
    static std::array<int, 6> parseFxOrder (const juce::String& text)
    {
        std::array<int, 6> order { 0, 1, 2, 3, 4, 5 };
        juce::StringArray parts;
        parts.addTokens (text, ",", {});
        if (parts.size() != numFxGroups)
            return order;
        std::array<bool, 6> seen {};
        std::array<int, 6> read {};
        for (int i = 0; i < numFxGroups; ++i)
        {
            const auto g = parts[i].trim().getIntValue();
            if (! parts[i].trim().containsOnly ("0123456789") || g < 0 || g >= numFxGroups || seen[(size_t) g])
                return order;
            seen[(size_t) g] = true;
            read[(size_t) i] = g;
        }
        return read;
    }
    std::array<int, 6> getFxOrder() const { return parseFxOrder (apvts.state.getProperty (fxOrderProp).toString()); }
    // Hilo de mensajes (editor): el orden por defecto se guarda como propiedad vacía (los presets no la escriben)
    void setFxOrder (const std::array<int, 6>& order)
    {
        juce::StringArray parts;
        for (const auto g : order) parts.add (juce::String (g));
        const auto isDefault = order == std::array<int, 6> { 0, 1, 2, 3, 4, 5 };
        apvts.state.setProperty (fxOrderProp, isDefault ? juce::String() : parts.joinIntoString (","), nullptr);
        refreshFxOrder();
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
            {
                if (auto* learnXml = xml->getChildByName ("MIDILEARN"))
                {
                    midiLearn.clearAll();
                    for (auto* map : learnXml->getChildIterator())
                    {
                        const auto cc = map->getIntAttribute ("cc", -1);
                        if (cc < 0 || cc >= 120) continue;
                        const auto id = map->getStringAttribute ("param");
                        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
                            midiLearn.assign (cc, p->getParameterIndex());
                        else if (id.startsWith ("action:"))
                            for (int k = 0; k < MidiLearn::numActions; ++k)
                                if (id.fromFirstOccurrenceOf (":", false, false) == MidiLearn::actionId (k))
                                    midiLearn.assign (cc, MidiLearn::actionBase + k);
                    }
                    xml->removeChildElement (learnXml, true); // no debe entrar en el ValueTree de parámetros
                }

                // Estados anteriores al KSL del OPL en dB/oct (sin la marca kslDb="1"): con OPL2/OPL3 se intercambian 1 y 2 para que suene
                // igual. La marca no se queda en el estado: la pone getStateInformation al guardar
                if (xml->getIntAttribute (kslDbAttribute, 0) != 1)
                {
                    const auto param = [&xml] (const juce::String& id) { return xml->getChildByAttribute ("id", id); };
                    PresetManager::swapOplKsl ([&param] (const juce::String& id, float def)
                                               { const auto* e = param (id); return e != nullptr ? (float) e->getDoubleAttribute ("value") : def; },
                                               [&param] (const juce::String& id, float v)
                                               { if (auto* e = param (id)) e->setAttribute ("value", (double) v); });
                }
                xml->removeAttribute (kslDbAttribute);

                // Estados guardados antes de "engine": <PARAM id="thereminOn" value="1"/> -> engine = Theremin
                if (auto* legacy = xml->getChildByAttribute ("id", "thereminOn"); legacy != nullptr && xml->getChildByAttribute ("id", "engine") == nullptr)
                {
                    auto* e = xml->createNewChildElement ("PARAM");
                    e->setAttribute ("id", "engine");
                    e->setAttribute ("value", legacy->getDoubleAttribute ("value") > 0.5 ? 1.0 : 0.0);
                }

                // Estados anteriores a la tabla propia de Osc 2 (sin osc2WtTable): Osc 2 leía la de la fuente Wavetable;
                // se le da la misma tabla y el mismo fichero para que suene igual. En ellos osc2Wave tenía Wavetable y Sample
                // en 4.º y 5.º lugar: se remapea (PresetManager::osc2WaveFromFormat3)
                if (xml->getChildByAttribute ("id", "osc2WtTable") == nullptr)
                {
                    if (auto* wave2 = xml->getChildByAttribute ("id", "osc2Wave"))
                        wave2->setAttribute ("value", PresetManager::osc2WaveFromFormat3 (juce::roundToInt (wave2->getDoubleAttribute ("value"))));
                    auto* e = xml->createNewChildElement ("PARAM");
                    e->setAttribute ("id", "osc2WtTable");
                    const auto* main = xml->getChildByAttribute ("id", "wtTable");
                    e->setAttribute ("value", main != nullptr ? main->getDoubleAttribute ("value") : 0.0);
                    if (! xml->hasAttribute (osc2WtPathProp.toString()))
                        xml->setAttribute (osc2WtPathProp.toString(), xml->getStringAttribute (wtPathProp.toString()));
                }

                apvts.replaceState (juce::ValueTree::fromXml (*xml));
                presetManager.invalidateSnapshot();
                perfMacros.update();
                syncRandomState();
                filterQualityRequested = juce::jlimit (0, 2, (int) apvts.state.getProperty (filterQualityProp, 0)); // sin ella: Classic
                fxOrderText = "?"; // fuerza la relectura
                refreshFxOrder();

                restoreFiles (apvts.state.getProperty (samplePathProp).toString(), apvts.state.getProperty (wtPathProp).toString());
                restoreSample (apvts.state.getProperty (osc2SamplePathProp).toString(), osc2Sample); // sin la propiedad (proyectos anteriores): sin fichero
                restoreWavetable (apvts.state.getProperty (osc2WtPathProp).toString(), wtOsc2);
                restoreCabIr (apvts.state.getProperty (cabIrPathProp).toString());
                restoreTuning();
                // El host debe releer los valores: el wrapper CLAP lo traduce a params->rescan (clap-validator,
                // state-reproducibility); en VST3 es el aviso de valores cambiados
                updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
            }
    }

    // Microafinación (Dsp/Tuning.h). El estado guarda el texto de la escala y del mapa (el proyecto no depende de los
    // ficheros); los presets no la llevan: es de la sesión, como la afinación de un piano. Hilo de mensajes
    juce::String loadTuningFile (const juce::File& file)
    {
        const auto text = file.loadFileAsString();
        const auto isKbm = file.hasFileExtension ("kbm");
        auto scl = isKbm ? tuning.getScl() : text;
        if (scl.isEmpty())
            scl = twelveTet();
        const auto kbm = isKbm ? text : tuning.getKbm();
        const auto name = isKbm ? (tuning.hasScala() ? tuning.getScaleName() : juce::String ("12-TET")) + " / " + file.getFileNameWithoutExtension()
                                : file.getFileNameWithoutExtension();
        if (const auto error = tuning.load (scl, kbm, name); error.isNotEmpty())
            return file.getFileName() + ": " + error;
        apvts.state.setProperty (tuningSclProp, tuning.getScl(), nullptr);
        apvts.state.setProperty (tuningKbmProp, tuning.getKbm(), nullptr);
        apvts.state.setProperty (tuningNameProp, tuning.getScaleName(), nullptr);
        return {};
    }

    void resetTuning()
    {
        tuning.reset();
        for (const auto& id : { tuningSclProp, tuningKbmProp, tuningNameProp })
            apvts.state.removeProperty (id, nullptr);
    }

    void setMtsEnabled (bool on)
    {
        tuning.setMtsEnabled (on);
        apvts.state.setProperty (mtsProp, on, nullptr);
    }

    Tuning tuning;

    // Ficheros de un preset de usuario o de un proyecto. Sample: ruta vacía = quitar el sample. Wavetable: solo si la tabla elegida
    // es "User File". Fichero ausente o ilegible: se conserva la ruta (al guardar no se pierde la referencia) y se avisa en la UI.
    void restoreFiles (const juce::String& samplePath, const juce::String& wavetablePath)
    {
        restoreSample (samplePath);
        restoreWavetable (wavetablePath);
    }

    void restoreSample (const juce::String& samplePath, int which = sourceSample)
    {
        if (samplePath.isEmpty())
        {
            clearSample (which);
        }
        else
        {
            const auto file = SampleImport::fileOf (samplePath);
            const auto error = file.existsAsFile() ? loadSample (file, SampleImport::itemOf (samplePath), which) : juce::String ("File not found");
            if (error.isNotEmpty())
            {
                slotFor (which).replace (nullptr);
                apvts.state.setProperty (pathPropFor (which), samplePath, nullptr);
                {
                    const juce::ScopedLock sl (sampleInfoLock);
                    sampleInfos[(size_t) which].text = (file.existsAsFile() ? "(unreadable) " : "(missing) ") + file.getFileName();
                }
                reportMissing (which == osc2Sample ? MissingFile::Kind::osc2Sample : MissingFile::Kind::sample, file, file.existsAsFile());
            }
        }
    }

    void restoreWavetable (const juce::String& wavetablePath, int which = wtMain)
    {
        if ((int) tableParam (which)->load() == 5 && wavetablePath.isNotEmpty())
        {
            const juce::File file (wavetablePath);
            const auto error = file.existsAsFile() ? loadWavetable (file, which) : juce::String ("File not found");
            if (error.isNotEmpty())
            {
                apvts.state.setProperty (tablePathProp (which), wavetablePath, nullptr);
                reportMissing (which == wtOsc2 ? MissingFile::Kind::osc2Wavetable : MissingFile::Kind::wavetable, file, file.existsAsFile());
            }
        }
    }

    // IR de pantalla del usuario (cualquier hilo salvo el de audio). Hasta 3 s (las de caja duran 20-500 ms); la convolución la
    // recorta (silencio inicial y final) y la normaliza. Devuelve el error o una cadena vacía
    juce::String loadCabIr (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return "Not an audio file this build can read (WAV, AIFF, FLAC...).";
        const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (3.0 * reader->sampleRate));
        if (len <= 0)
            return "The file is empty.";
        juce::AudioBuffer<float> ir ((int) juce::jmin (2u, reader->numChannels), len);
        reader->read (&ir, 0, len, 0, true, ir.getNumChannels() > 1);
        ampSim.setUserIr (std::move (ir), reader->sampleRate);
        apvts.state.setProperty (cabIrPathProp, file.getFullPathName(), nullptr);
        const juce::ScopedLock sl (sampleInfoLock);
        cabIrInfo = file.getFileNameWithoutExtension();
        return {};
    }

    void clearCabIr()
    {
        ampSim.clearUserIr();
        if (apvts.state.hasProperty (cabIrPathProp)) // vacía = ausente (como el sample)
            apvts.state.setProperty (cabIrPathProp, juce::String(), nullptr);
        const juce::ScopedLock sl (sampleInfoLock);
        cabIrInfo = {};
    }

    juce::String getCabIrInfo() const
    {
        const juce::ScopedLock sl (sampleInfoLock);
        return cabIrInfo;
    }

    // Como el sample: ruta vacía = sin IR; fichero ausente o ilegible = se conserva la ruta y se avisa
    void restoreCabIr (const juce::String& path)
    {
        if (path.isEmpty()) { clearCabIr(); return; }
        const juce::File file (path);
        const auto error = file.existsAsFile() ? loadCabIr (file) : juce::String ("File not found");
        if (error.isNotEmpty())
        {
            ampSim.clearUserIr();
            apvts.state.setProperty (cabIrPathProp, path, nullptr);
            {
                const juce::ScopedLock sl (sampleInfoLock);
                cabIrInfo = (file.existsAsFile() ? "(unreadable) " : "(missing) ") + file.getFileName();
            }
            reportMissing (MissingFile::Kind::cabIr, file, file.existsAsFile());
        }
    }

    // Avisos de ficheros no encontrados: los produce cualquier hilo (el host puede restaurar el estado fuera del de mensajes)
    // y los consume el editor, uno a uno
    struct MissingFile
    {
        enum class Kind { sample, wavetable, cabIr, osc2Sample, osc2Wavetable };
        Kind kind;
        juce::File file;
        bool unreadable = false;
    };

    void reportMissing (MissingFile::Kind kind, const juce::File& file, bool unreadable)
    {
        const juce::ScopedLock sl (missingLock);
        for (const auto& m : missing)
            if (m.kind == kind && m.file == file)
                return;
        missing.push_back ({ kind, file, unreadable });
    }

    std::optional<MissingFile> popMissing()
    {
        const juce::ScopedLock sl (missingLock);
        if (missing.empty())
            return std::nullopt;
        auto m = missing.front();
        missing.erase (missing.begin());
        return m;
    }

    // Aleatoriedad (Dsp/RandomClock.h). Semilla de instancia y modo en el estado del proyecto (hilo de mensajes)
    const Params& getParams() const { return params; } // solo lectura (anillos de la matriz en el editor)
    bool isRandomLocked() const { return ! randomFree.load(); }
    void setRandomLocked (bool locked)
    {
        apvts.state.setProperty (randomFreeProp, ! locked, nullptr);
        syncRandomState();
    }
    void newRandomSeed()
    {
        apvts.state.setProperty (randomSeedProp, juce::String::toHexString ((juce::int64) juce::Random().nextInt64()), nullptr);
        syncRandomState();
    }

    // Lee (o crea) la semilla y el modo del estado; pide un anclaje para aplicarlos
    void syncRandomState()
    {
        auto hex = apvts.state.getProperty (randomSeedProp).toString();
        if (hex.isEmpty())
        {
            hex = juce::String::toHexString ((juce::int64) juce::Random().nextInt64());
            apvts.state.setProperty (randomSeedProp, hex, nullptr);
        }
        instanceSeed.store ((juce::uint64) hex.getHexValue64());
        randomFree.store ((bool) apvts.state.getProperty (randomFreeProp, false));
        randomAnchorPending = true;
    }

private:
    // Inicio de bloque (hilo de audio): posición, modo y, si toca, anclaje = resiembra de todos los generadores
    void updateRandomClock (std::optional<juce::int64> hostPos, int numSamples)
    {
        auto& rc = params.randomClock;
        rc.fs = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        rc.locked = ! randomFree.load();
        rc.seed = rc.locked ? instanceSeed.load() : sessionSeed;
        const auto playing = rc.locked && hostPos.has_value();
        const auto pos = playing ? *hostPos : localSamples;
        const auto jumped = playing && (! rc.playing || pos != rc.timeline + lastClockBlock);
        rc.playing = playing;
        rc.timeline = pos;
        rc.eventOffset = 0;
        localSamples += numSamples;
        lastClockBlock = numSamples;
        // Deriva común del motor Synth: generadores de la semilla vigente; valores al inicio y al final del bloque
        if (rc.seed != commonSeed)
        {
            commonSeed = rc.seed;
            commonPink.init (KoniRandom::key (rc.seed, 0xC0FFEEull), 0.005, 8);
            temperaturePink.init (KoniRandom::key (rc.seed, 0x7E4Dull), 0.002, 5);
        }
        rc.blockLength = juce::jmax (1, numSamples);
        rc.commonA = commonPink.at (rc.seconds (0));
        rc.commonB = commonPink.at (rc.seconds (numSamples));
        rc.temperatureA = temperaturePink.at (rc.seconds (0));
        rc.temperatureB = temperaturePink.at (rc.seconds (numSamples));
        if (rc.locked && (jumped || randomAnchorPending.exchange (false)))
        {
            rc.liveCounter = 0;
            const auto base = KoniRandom::key (rc.seed, (juce::uint64) pos);
            auto seedFor = [base] (juce::uint64 stream) { return (juce::int64) KoniRandom::key (base, stream); };
            theremin.reseed (seedFor (1)); stylophone.reseed (seedFor (2)); drumMachine.reseed (seedFor (3));
            polivoks.reseed (seedFor (4)); ansEngine.reseed (seedFor (5)); arp.reseed (seedFor (6));
            media.reseed (seedFor (7)); underwater.reseed (seedFor (8)); omnichord.reseed (seedFor (9));
            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i))) v->reseed (seedFor (100u + (juce::uint64) i));
            for (int i = 0; i < monoSynth.getNumVoices(); ++i)
                if (auto* v = dynamic_cast<SynthVoice*> (monoSynth.getVoice (i))) v->reseed (seedFor (200u + (juce::uint64) i));
        }
    }

    static inline const juce::Identifier randomSeedProp { "randomSeed" }, randomFreeProp { "randomFree" };
    std::atomic<juce::uint64> instanceSeed { 1 };
    std::atomic<bool> randomFree { false }, randomAnchorPending { true };
    const juce::uint64 sessionSeed = (juce::uint64) juce::Random().nextInt64();
    juce::int64 localSamples = 0;
    int lastClockBlock = 0;
    KoniRandom::Pink commonPink, temperaturePink;
    juce::uint64 commonSeed = 0;

public:
    // ¿Suena alguna voz del Synth (poly o mono, release incluido)? Hilo de audio
    bool anySynthVoiceActive() const
    {
        for (const auto* s : { &synth, &monoSynth })
            for (int i = 0; i < s->getNumVoices(); ++i)
                if (s->getVoice (i)->isVoiceActive())
                    return true;
        return false;
    }

    // Corta todas las voces sin release (poly y mono) y vacía la pila del modo monofónico. Hilo de audio.
    void allVoicesOff()
    {
        synth.allNotesOff (0, false);
        monoSynth.allNotesOff (0, false);
        monoFilter.reset();
    }

    // Panic: lo pide la UI (botón) o el CC 120; se ejecuta al principio del siguiente bloque de audio
    void requestPanic() { panicRequested = true; }

    // Calidad del ladder del Synth: 0 Classic (1x), 1 Balanced (2x + ADAA), 2 High Quality (4x). Es de la instancia, como la
    // afinación: va en el estado del proyecto y los presets no la llevan (no está en su lista de propiedades). Hilo de mensajes
    void setFilterQuality (int q)
    {
        q = juce::jlimit (0, 2, q);
        apvts.state.setProperty (filterQualityProp, q, nullptr);
        filterQualityRequested = q;
    }
    int getFilterQuality() const { return filterQualityRequested.load(); }
    bool isFilterQualityPending() const { return filterQualityRequested.load() != params.ladderQuality.load(); }
    void applyFilterQualityNow() { requestPanic(); } // corta las voces: el modo pedido se aplica en ese mismo bloque

    juce::MidiKeyboardState keyboardState;
    SessionTransport sessionTransport; // REC / STOP / PLAY (Transport/SessionTransport.h); inactivo hasta pulsar

    // Botones de transporte por MIDI learn (MidiLearn::actionRecord/Stop/Play): el hilo de audio los marca y aquí se
    // ejecutan en el hilo de mensajes (crean y cierran ficheros). 50 Hz: hasta 20 ms desde el CC. Tras sessionTransport:
    // se destruye antes que él
    void pollActions()
    {
        if (midiLearn.takeAction (MidiLearn::actionRecord))
        {
            if (sessionTransport.isRecording()) sessionTransport.stopRecording();
            else sessionTransport.startRecording (SessionTransport::newRecordingFile(), getDisplayBpm());
        }
        if (midiLearn.takeAction (MidiLearn::actionStop))
            sessionTransport.stop();
        if (midiLearn.takeAction (MidiLearn::actionPlay))
        {
            if (sessionTransport.isPlaying()) sessionTransport.pause();
            else sessionTransport.play();
        }
    }
    struct ActionPoll : juce::Timer { std::function<void()> tick; void timerCallback() override { tick(); } };
    ActionPoll actionPoll;
    EasterEgg easterEgg;
    APVTS apvts;
    PresetManager presetManager;
    MidiLearn midiLearn;
    PerfMacros perfMacros; // macros de interpretación (Params/PerfMacros.h); attach() en el constructor
    UndoHistory undoHistory; // tras apvts: se destruye (y se desengancha) antes que él

    // Control por agente (Agent/AgentServer.h): nullptr = desactivado, sin hilo, socket ni fichero. Declarado tras apvts,
    // presetManager y undoHistory: se destruye (y se para) antes que ellos. Hilo de mensajes
    bool isAgentEnabled() const { return agentServer != nullptr; }
    int agentPort() const { return agentServer != nullptr ? agentServer->getPort() : 0; }
    int agentClients() const { return agentServer != nullptr ? agentServer->connectedClients() : 0; }
    bool setAgentEnabled (bool on, bool persist = true)
    {
        if (on && agentServer == nullptr)
        {
            if (agentTap == nullptr) // se crean una vez y no se liberan hasta destruir el procesador (ver Agent/AudioTap.h)
                agentTap = std::make_unique<AudioTap> (getSampleRate());
            if (agentScheduler == nullptr)
                agentScheduler = std::make_unique<AgentScheduler>();
            agentServer = std::make_unique<AgentServer> (AgentServer::Host { apvts, presetManager, undoHistory, JucePlugin_VersionString,
                                                                              keyboardState, *agentTap, *agentScheduler });
            if (agentServer->start())
            {
                agentTapActive.store (agentTap.get(), std::memory_order_release);
                agentSchedulerActive.store (agentScheduler.get(), std::memory_order_release);
            }
            else
                agentServer.reset();
        }
        else if (! on)
        {
            agentTapActive.store (nullptr, std::memory_order_release);
            agentServer.reset();
        }
        if (persist)
            if (auto config = openConfig())
            {
                config->setValue ("agentControl", on);
                config->saveIfNeeded();
            }
        return isAgentEnabled() == on;
    }
    std::unique_ptr<AudioTap> agentTap;            // salida reciente para el agente (F3); nullptr hasta la primera activación
    std::atomic<AudioTap*> agentTapActive { nullptr }; // lo que lee processBlock: nulo con el control desactivado
    // Notas de play del agente (S2). Publicado desde la primera activación y no se retira al desactivar: el stop del
    // servidor le manda cancelar y el hilo de audio debe recibirlo para soltar las notas que sonaban. Después queda
    // ocioso (comprobar su cola vacía por bloque: dos cargas atómicas)
    std::unique_ptr<AgentScheduler> agentScheduler;
    std::atomic<AgentScheduler*> agentSchedulerActive { nullptr };
    // Tras tap y planificador: se destruye antes que ellos (su stop manda cancelar al planificador)
    std::unique_ptr<AgentServer> agentServer;

private:
    // Silencio inmediato: corta voces sin release y vacía todos los estados con memoria (colas de echo, reverb, chorus,
    // soporte, oversampling y theremin). Hilo de audio.
    // PANIC: silencio total desde este bloque. Todos los motores y efectos vuelven a reposo (sus reset ya se usan en este
    // hilo al cambiar de motor o encender un efecto), el transporte se pone en pausa, se descartan las notas del agente y los
    // ritmos que suenan solos (Drum Machine, Omnichord, ANS con Hold) quedan parados hasta la siguiente nota. Lo que se toca
    // con un control mantenido (Theremin, monitor de entrada) vuelve a sonar en cuanto llega su señal
    void panic()
    {
        allVoicesOff();
        keyboardState.allNotesOff (0); // suelta también las teclas pulsadas en pantalla
        theremin.reset();
        vl1.reset();
        stylophone.reset();
        omnichord.reset();
        omnichord.stopRhythm();
        drumMachine.reset();
        drumMachine.stopRhythm();
        fmEngine.reset();
        chipEngine.reset();
        polivoks.reset();
        ansEngine.reset();
        ansEngine.stopHold();
        mdEngine.reset();
        mdEngine.settleOutput();
        snesEngine.reset();
        snesEngine.settleOutput();
        n64Engine.reset();
        oplEngine.reset();
        mt32Engine.reset();
        flanger.reset();
        freqShifter.reset();
        leslie.reset();
        spring.reset();
        underwater.reset();
        sessionTransport.pauseFromAudioThread();
        if (auto* scheduler = agentSchedulerActive.load (std::memory_order_acquire))
            scheduler->reset(); // notas programadas por play: fuera, y se sueltan las que sonaban
        arp.reset();
        sequencer.reset();
        noteScale.reset();
        equalizer.reset();
        ampSim.reset();
        transient.reset();
        compressor.reset();
        limiter.reset();
        vocoder.reset();
        crushHeld = {};
        formant.reset();
        phaserState = {};
        echo.reset();
        toneState = {};
        reverb.reset();
        chorus.reset();
        media.reset();
        if (tubeOversampling != nullptr)
            tubeOversampling->reset();
        dcState = {};
    }

    // Válvula: y = (tanh(g·x + B) - tanh(B)) · comp. B desplaza el punto de trabajo (asimetría -> armónicos pares);
    // restar tanh(B) quita el DC estático y el DC blocker el dinámico. comp normaliza para que una señal de pico 0,3
    // salga con pico ~0,3 a cualquier drive.
    void processTube (juce::AudioBuffer<float>& buffer)
    {
        const auto on = params.tubeOn->load() > 0.5f;
        if (on && ! tubeWasOn)
        {
            tubeOversampling->reset();
            dcState = {};
        }
        tubeWasOn = on;

        if (! on)
            return;

        const auto g = juce::Decibels::decibelsToGain (params.tubeDrive->load());
        const auto B = params.tubeBias->load();
        const auto offset = std::tanh (B);
        const auto comp = 0.3f / (0.5f * (std::tanh (0.3f * g + B) - std::tanh (-0.3f * g + B)));

        auto shape = [&] (float* data, size_t n)
        {
            for (size_t i = 0; i < n; ++i)
                data[i] = (std::tanh (g * data[i] + B) - offset) * comp;
        };

        juce::dsp::AudioBlock<float> block (buffer);

        if (buffer.getNumSamples() <= maxBlockSize)
        {
            auto up = tubeOversampling->processSamplesUp (block);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                shape (up.getChannelPointer (ch), up.getNumSamples());
            tubeOversampling->processSamplesDown (block);
        }
        else // bloque mayor que el anunciado en prepareToPlay: sin oversampling antes que fallar
        {
            for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
                shape (block.getChannelPointer (ch), block.getNumSamples());
        }

        // DC blocker: y[n] = x[n] - x[n-1] + R·y[n-1]
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& [x1, y1] = dcState[(size_t) ch];
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto x = data[i];
                y1 = x - x1 + dcCoef * y1;
                x1 = x;
                data[i] = y1;
            }
        }
    }

    void pushToScope (const juce::AudioBuffer<float>& buffer)
    {
        const auto numChannels = buffer.getNumChannels();
        if (numChannels == 0)
            return;

        auto pos = scopeWritePos.load (std::memory_order_relaxed);

        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (numChannels > 1 ? 1 : 0);
        for (int i = 0; i < buffer.getNumSamples(); ++i, ++pos)
        {
            scopeLeft[(size_t) (pos & (scopeSize - 1))].store (l[i], std::memory_order_relaxed);
            scopeRight[(size_t) (pos & (scopeSize - 1))].store (r[i], std::memory_order_relaxed);
        }

        scopeWritePos.store (pos, std::memory_order_release);
    }

    Params params;
    TimelineSynth synth { params.randomClock }, monoSynth { params.randomClock }; // anotan la posición de cada note-on
    MonoNoteFilter monoFilter;
    bool lastMono = false;
    Theremin theremin;
    bool thereminWasOn = false;
    VL1 vl1;
    bool vl1WasOn = false;
    Stylophone stylophone;
    bool styloWasOn = false;
    Omnichord omnichord;
    bool omniWasOn = false;
    DrumMachine drumMachine;
    bool drumWasOn = false;
    FmEngine fmEngine;
    bool fmWasOn = false;
    ChipEngine chipEngine;
    bool chipWasOn = false;
    PolivoksEngine polivoks;
    bool pvWasOn = false;
    AnsEngine ansEngine;
    bool ansWasOn = false;
    MegaDriveEngine mdEngine;
    bool mdWasOn = false;
    SnesEngine snesEngine;
    bool snesWasOn = false;
    N64Engine n64Engine;
    OplEngine oplEngine;
    bool n64WasOn = false, oplWasOn = false, mt32WasOn = false;
    Mt32Engine mt32Engine;
    double mt32Rate = 0.0;
    int mt32BuiltModel = -1;
    juce::String mt32Status; // protegido por sampleInfoLock
    AnsSlot ansSlot;
    MacroSlot chipMacroSlot, mdMacroSlot, snesMacroSlot;
    std::array<juce::int64, 3> macroHash { -1, -1, -1 };
    int ansShown = -1;
    juce::int64 ansUserHash = 0;
    std::atomic<int> ansVersion { 0 };
    BbdFlanger flanger;
    FrequencyShifter freqShifter;
    LeslieSpeaker leslie;
    SpringReverb spring;
    Equalizer equalizer;
    bool eqWasOn = false;
public:
    Compressor compressor; // público: el diálogo lee sus medidores
    Limiter limiter;
private:
    TransientShaper transient;
    AmpSim ampSim;
    bool ampWasOn = false;
    bool transWasOn = false, compWasOn = false, limWasOn = false;
    // Latencia total (muestras): sobremuestreo del ampli (encendido) + anticipación del limitador
    int currentLatency() const
    {
        const auto amp = params.ampOn != nullptr && params.ampOn->load() > 0.5f ? ampSim.latency() : 0;
        // Sobremuestreo IIR de la válvula (2x) y del motor Polivoks (4x): JUCE da su retardo de fase a frecuencia muy baja
        // (juce_Oversampling.cpp:285-289), el que interesa para alinear pistas; redondeado a muestras enteras
        const auto tube = params.tubeOn->load() > 0.5f && tubeOversampling != nullptr ? (int) std::lround (tubeOversampling->getLatencyInSamples()) : 0;
        const auto pv = (int) params.engine->load() == 8 ? polivoks.latency() : 0;
        return amp + tube + pv + currentLookahead();
    }
    // Latencia del limitador (muestras): solo con el limitador encendido y la anticipación activa
    int currentLookahead() const
    {
        static constexpr double ms[] = { 0.0, 1.0, 2.0, 5.0 };
        if (params.limOn == nullptr || params.limOn->load() < 0.5f) return 0;
        return Limiter::lookaheadSamples (ms[juce::jlimit (0, 3, (int) params.limLookahead->load())], getSampleRate());
    }
    bool flangerWasOn = false, fshiftWasOn = false, leslieWasOn = false, springWasOn = false;
    Arpeggiator arp;
    StepSequencer sequencer;
    ScaleQuantizer noteScale; // Transpose y escala de las notas de entrada (pie del teclado)
    Vocoder vocoder;
    bool vocoderWasOn = false;
    FormantFilter formant;
    bool formantWasOn = false;
    float crushPhase = 0.0f;
    struct PhaserChannel { std::array<float, 4> x1 {}, y1 {}; float last = 0.0f; };
    std::array<PhaserChannel, 2> phaserState {};
    double phaserPhase = 0.0;
    std::array<float, 2> crushHeld {};
    std::vector<float> inputMono;
    float lastInputGain = 1.0f;
    SampleSlot sampleSlot, osc2SampleSlot;
    WavetableSlot wavetableSlot, osc2WavetableSlot;
    juce::CriticalSection missingLock;
    std::vector<MissingFile> missing;
    std::array<std::atomic<int>, 2> builtInLoaded { -1, -1 }; // tabla incluida cargada en cada slot (wtMain, wtOsc2); -1 = fichero
    static inline const juce::Identifier wtPathProp { "wtPath" }, osc2WtPathProp { "osc2WtPath" };
    WavetableSlot& tableSlot (int which) { return which == wtOsc2 ? osc2WavetableSlot : wavetableSlot; }
    const WavetableSlot& tableSlot (int which) const { return which == wtOsc2 ? osc2WavetableSlot : wavetableSlot; }
    std::atomic<float>* tableParam (int which) const { return which == wtOsc2 ? params.osc2WtTable : params.wtTable; }
    static const juce::Identifier& tablePathProp (int which) { return which == wtOsc2 ? osc2WtPathProp : wtPathProp; }
    juce::CriticalSection sampleInfoLock;
    // Por sample (sourceSample, osc2Sample): texto para la UI, nota raíz del formato (-1 = desconocida) y bucle propio
    struct SampleInfo { juce::String text; int rootKey = -1; bool hasLoop = false; };
    std::array<SampleInfo, 2> sampleInfos;
    static inline const juce::Identifier samplePathProp { "samplePath" }, osc2SamplePathProp { "osc2SamplePath" };
    SampleSlot& slotFor (int which) { return which == osc2Sample ? osc2SampleSlot : sampleSlot; }
    const SampleSlot& slotFor (int which) const { return which == osc2Sample ? osc2SampleSlot : sampleSlot; }
    static const juce::Identifier& pathPropFor (int which) { return which == osc2Sample ? osc2SamplePathProp : samplePathProp; }
    static inline const juce::Identifier cabIrPathProp { "cabIrPath" }; // mismo nombre que el atributo del preset
    // Orden de la cadena de efectos (propiedad de texto "fxOrder", también en los presets: los 6 grupos separados por comas;
    // vacía o no válida = el orden de siempre). El audio lee la copia empaquetada: 4 bits por posición
    static inline const juce::Identifier fxOrderProp { "fxOrder" };
    static constexpr juce::uint32 defaultFxOrder = 0x543210;
    std::atomic<juce::uint32> fxOrderPacked { defaultFxOrder };
    juce::String fxOrderText;
    void refreshFxOrder()
    {
        const auto text = apvts.state.getProperty (fxOrderProp).toString();
        if (text == fxOrderText && fxOrderText.isNotEmpty())
            return;
        fxOrderText = text;
        fxOrderPacked.store (packFxOrder (parseFxOrder (text)), std::memory_order_relaxed);
    }
    static juce::uint32 packFxOrder (const std::array<int, 6>& order)
    {
        juce::uint32 packed = 0;
        for (int slot = 0; slot < numFxGroups; ++slot)
            packed |= (juce::uint32) order[(size_t) slot] << (4 * slot);
        return packed;
    }
    static inline const juce::Identifier tuningSclProp { "tuningScl" }, tuningKbmProp { "tuningKbm" }, tuningNameProp { "tuningName" },
                                         mtsProp { "mtsEsp" };

    static juce::String twelveTet()
    {
        juce::String t = "12-TET\n12\n";
        for (int i = 1; i <= 12; ++i) t << juce::String (i * 100) << ".0\n";
        return t;
    }

    // Estado del host o del proyecto: escala guardada y MTS-ESP (activo por defecto: sin maestro no hace nada)
    void restoreTuning()
    {
        tuning.setMtsEnabled ((bool) apvts.state.getProperty (mtsProp, true));
        const auto scl = apvts.state.getProperty (tuningSclProp).toString();
        if (scl.isEmpty() || tuning.load (scl, apvts.state.getProperty (tuningKbmProp).toString(), apvts.state.getProperty (tuningNameProp).toString()).isNotEmpty())
            tuning.reset();
    }
    juce::String cabIrInfo; // protegido por sampleInfoLock

    // Hilo de mensajes: libera datos retirados y, si la tabla elegida es una incluida distinta de la cargada, la genera
    // (64 fotogramas × 11 niveles: 704 FFT de 2048 puntos, fuera del hilo de audio)
    void timerCallback() override
    {
        sampleSlot.collect();
        osc2SampleSlot.collect();
        wavetableSlot.collect();
        osc2WavetableSlot.collect();
        ansSlot.collect();
        updateAnsScore();
        chipMacroSlot.collect();
        mdMacroSlot.collect();
        snesMacroSlot.collect();
        mt32Engine.slot.collect();
        for (auto& b : rhythmBanks) b.slot.collect();
        if (++rhythmPollTicks >= 10) // ficheros de ritmos de usuario: una vez por segundo
        {
            rhythmPollTicks = 0;
            updateUserRhythms();
        }
        if ((int) params.engine->load() == 14 && ((int) params.mt32Model->load() != mt32BuiltModel || getSampleRate() != mt32Rate))
            rebuildMt32(); // solo con el motor elegido: desactivado no carga ROM ni ocupa memoria
        updateMacros();
        perfMacros.update();
        updateBuiltInWavetable();
        refreshFxOrder(); // un preset cargado puede traer otro orden de la cadena de efectos
    }

    // Ritmos de usuario de la caja de ritmos, el Omnichord y el VL-1 (Engines/UserRhythms.h): un banco por motor, se relee
    // si cambia la firma de sus ficheros (tamaño y fecha) y se publica con DataSlot. Hilo de mensajes o prepare. El editor
    // de ritmos llama aquí tras guardar
public:
    void updateUserRhythms (bool force = false)
    {
        for (int t = 0; t < (int) UserRhythms::Target::count; ++t)
            updateUserRhythms ((UserRhythms::Target) t, force);
    }
    void updateUserRhythms (UserRhythms::Target t, bool force)
    {
        auto& b = rhythmBanks[(size_t) t];
        const auto sig = UserRhythms::signature (t);
        if (! force && sig == b.signature && b.slot.current.load() != nullptr)
            return;
        b.signature = sig;
        b.slot.replace (UserRhythms::loadBank (t));
    }
private:
    struct RhythmBank { DataSlot<UserRhythms::Bank> slot; juce::String signature; };
    std::array<RhythmBank, (size_t) UserRhythms::Target::count> rhythmBanks;
    int rhythmPollTicks = 0;
    const UserRhythms::Bank* rhythmBank (UserRhythms::Target t) const // hilo de audio
    {
        return rhythmBanks[(size_t) t].slot.current.load (std::memory_order_acquire);
    }

    // Wavetable incluida elegida y aún no generada: se genera (FFT, fuera del hilo de audio). Temporizador y prepareToPlay:
    // un render offline sin bucle de mensajes (bounce, KoniBench) también la tiene desde el primer bloque
    void updateBuiltInWavetable()
    {
        for (const auto which : { wtMain, wtOsc2 })
        {
            const auto table = (int) tableParam (which)->load();
            auto& loaded = builtInLoaded[(size_t) which];
            if (table < 5 && table != loaded)
            {
                tableSlot (which).replace (WavetableData::builtIn (table));
                loaded = table;
            }
            else if (table == 5)
            {
                loaded = -1;
            }
        }
    }

    // Macros por frame (Dsp/FrameMacro.h): texto en las propiedades de estado "chipMacros", "mdMacros" y "snesMacros"; se
    // publican interpretadas cuando cambian. Texto con errores: se publica vacío (el editor muestra el error).
    // Hilo de mensajes o prepare
    void updateMacros()
    {
        const std::array<std::pair<const char*, MacroSlot*>, 3> targets { { { chipMacrosProp, &chipMacroSlot },
                                                                             { mdMacrosProp, &mdMacroSlot },
                                                                             { snesMacrosProp, &snesMacroSlot } } };
        for (size_t k = 0; k < targets.size(); ++k)
        {
            const auto text = apvts.state.getProperty (targets[k].first).toString();
            const auto h = text.hashCode64();
            if (h == macroHash[k])
                continue;
            macroHash[k] = h;
            auto set = std::make_unique<MacroSet>();
            if (MacroSet::parse (text, *set).isNotEmpty() || set->empty())
                set.reset();
            targets[k].second->replace (std::move (set));
        }
    }

public:
    static constexpr const char* chipMacrosProp = "chipMacros";
    static constexpr const char* mdMacrosProp = "mdMacros";
    static constexpr const char* snesMacrosProp = "snesMacros";

private:
    // ANS: partitura de fábrica (generada) o del usuario (estado "ansUser", comprimida en base64). Hilo de mensajes o prepare
    void updateAnsScore()
    {
        const auto ansIdx = (int) params.ansScore->load();
        if (ansIdx < 6)
        {
            if (ansIdx != ansShown)
            {
                ansSlot.replace (AnsScore::factory (ansIdx));
                ansShown = ansIdx;
                ++ansVersion;
            }
        }
        else
        {
            const auto text = apvts.state.getProperty (ansUserProp).toString();
            const auto h = text.hashCode64();
            if (ansShown != 6 || h != ansUserHash)
            {
                ansSlot.replace (decodeAnsScore (text));
                ansShown = 6;
                ansUserHash = h;
                ++ansVersion;
            }
        }
    }

public:
    // Carga una wavetable del usuario (cualquier hilo salvo el de audio) y selecciona "User File" en la tabla de 'which'.
    // Devuelve el error o "".
    juce::String loadWavetable (const juce::File& file, int which = wtMain)
    {
        juce::String error;
        auto data = WavetableData::fromFile (file, error);
        if (data == nullptr)
            return error;
        tableSlot (which).replace (std::move (data));
        builtInLoaded[(size_t) which] = -1;
        apvts.state.setProperty (tablePathProp (which), file.getFullPathName(), nullptr);
        if (auto* p = apvts.getParameter (which == wtOsc2 ? "osc2WtTable" : "wtTable"))
            p->setValueNotifyingHost (p->convertTo0to1 (5.0f));
        return {};
    }

    // Copia de los fotogramas de nivel 0 para dibujarlos (hilo de mensajes): hasta maxFrames fotogramas, cada uno con 'points' puntos
    std::vector<std::vector<float>> getWavetableOverview (int maxFramesToDraw, int points, int which = wtMain) const
    {
        std::vector<std::vector<float>> out;
        tableSlot (which).withCurrent ([&] (const WavetableData* wt)
        {
            if (wt == nullptr)
                return;
            const auto count = juce::jmin (maxFramesToDraw, wt->numFrames);
            for (int k = 0; k < count; ++k)
            {
                const auto f = count > 1 ? (int) std::round ((double) k * (wt->numFrames - 1) / (count - 1)) : 0;
                const auto* t = wt->table (f, 0);
                std::vector<float> frame ((size_t) points);
                for (int i = 0; i < points; ++i)
                    frame[(size_t) i] = t[(size_t) i * WavetableData::frameSize / (size_t) points];
                out.push_back (std::move (frame));
            }
        });
        return out;
    }

    int getSequencerStep() const { return sequencer.uiStep.load(); }

    // ANS (hilo de mensajes)
    AnsScore copyAnsScore() const
    {
        AnsScore copy;
        ansSlot.withCurrent ([&copy] (const AnsScore* d) { if (d != nullptr) copy = *d; });
        return copy;
    }
    int getAnsColumn() const { return ansEngine.playColumn.load(); }
    int getAnsVersion() const { return ansVersion.load(); }
    void setAnsUserScore (std::unique_ptr<AnsScore> score)
    {
        juce::MemoryOutputStream mo;
        {
            juce::GZIPCompressorOutputStream gz (mo, 9);
            gz.write (score->cells.data(), score->cells.size());
        }
        const auto text = mo.getMemoryBlock().toBase64Encoding();
        apvts.state.setProperty (ansUserProp, text, nullptr);
        ansUserHash = text.hashCode64();
        ansSlot.replace (std::move (score));
        ansShown = 6;
        ++ansVersion;
        if (auto* prm = apvts.getParameter ("ansScore"))
            prm->setValueNotifyingHost (prm->convertTo0to1 (6.0f));
    }

    static std::unique_ptr<AnsScore> decodeAnsScore (const juce::String& text)
    {
        auto score = std::make_unique<AnsScore>();
        juce::MemoryBlock block;
        if (text.isEmpty() || ! block.fromBase64Encoding (text))
            return score;
        juce::MemoryInputStream mi (block, false);
        juce::GZIPDecompressorInputStream gz (mi);
        gz.read (score->cells.data(), (int) score->cells.size());
        return score;
    }

    static constexpr const char* ansUserProp = "ansUser";

    juce::String getWavetableName (int which = wtMain) const
    {
        juce::String name;
        tableSlot (which).withCurrent ([&] (const WavetableData* wt) { if (wt != nullptr) name = wt->name; });
        return name;
    }

    // Resumen de la forma de onda del sample para dibujarla: pares (mín, máx) por columna, y duración en muestras / frecuencia
    std::vector<std::pair<float, float>> getSampleOverview (int columns, int& lengthOut, double& rateOut, int which = sourceSample) const
    {
        std::vector<std::pair<float, float>> out;
        lengthOut = 0;
        rateOut = 44100.0;
        slotFor (which).withCurrent ([&] (const SampleData* d)
        {
            if (d == nullptr || columns <= 0)
                return;
            const auto& m = d->levels[0];
            lengthOut = (int) m.size();
            rateOut = d->sampleRate;
            out.resize ((size_t) columns, { 0.0f, 0.0f });
            for (int c = 0; c < columns; ++c)
            {
                const auto from = (size_t) ((double) c * m.size() / columns);
                const auto to = juce::jmax (from + 1, (size_t) ((double) (c + 1) * m.size() / columns));
                float lo = 1.0f, hi = -1.0f;
                for (auto k = from; k < to && k < m.size(); ++k)
                {
                    lo = juce::jmin (lo, m[k]);
                    hi = juce::jmax (hi, m[k]);
                }
                out[(size_t) c] = { lo, hi };
            }
        });
        return out;
    }

private:
    Chorus chorus;
    bool chorusWasOn = false;
    Media media;
    Underwater underwater;
    bool mediaWasOn = false;
    std::atomic<bool> panicRequested { false };
    static inline const juce::Identifier filterQualityProp { "filterQuality" };
    std::atomic<int> filterQualityRequested { 0 };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> echo;
    juce::SmoothedValue<float> echoDelay;
    std::array<float, 2> toneState {}; // 2 = máximo de canales (isBusesLayoutSupported)
    float wowPhase = 0.0f, flutterPhase = 0.0f;
    bool echoWasOn = false;

    std::unique_ptr<juce::dsp::Oversampling<float>> tubeOversampling;
    std::array<std::pair<float, float>, 2> dcState {}; // (x[n-1], y[n-1]) por canal
    float dcCoef = 0.999f;
    int maxBlockSize = 0;
    bool tubeWasOn = false;

    PlateReverb reverb;
    bool ambientWasOn = false;

    float lastGain = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KoniSynthProcessor)
};

#include "UI/Skin.h" // antes que cualquier UI: Skin::c() en todos los colores
#include "UI/Overlay.h" // antes de Widgets: el menú de parámetro abre "Set value..." en un Overlay
#include "UI/Widgets.h"
#include "UI/SourceTabs.h"

#include "UI/Displays.h"
#include "UI/TransportPanel.h"
#include "UI/AgentPanel.h"
#include "UI/Arcade.h"
#include "UI/Visualizer.h"

#include "UI/PresetBar.h"
#include "UI/PresetBrowser.h"

#include "UI/ThereminPad.h"
#include "UI/StylophonePad.h"

#include "UI/About.h"

#include "UI/SamplePanel.h"

#include "UI/WavetablePanel.h"

#include "UI/FilterView.h"
#include "UI/ModViews.h"
#include "UI/WaveView.h"

#include "UI/SequencerPanel.h"

#include "UI/RhythmEditor.h"

#include "UI/FmPanel.h"

#include "UI/MdPanel.h"
#include "UI/OplPanel.h"
#include "UI/MacroPanel.h"
#include "UI/PerfMacroPanel.h"
#include "UI/EqPanel.h"
#include "UI/DynamicsPanel.h"
#include "UI/MatrixPanel.h"

#include "UI/AnsPanel.h"

#include "UI/SettingsPanel.h"
#include "UI/ControllerSetupPanel.h"
#include "UI/ScaleBar.h"

#include "UI/Editor.h"

juce::AudioProcessorEditor* KoniSynthProcessor::createEditor() { return new KoniSynthEditor (*this); }

// Punto de entrada que JUCE usa tanto para el VST3 como para el Standalone.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KoniSynthProcessor(); }

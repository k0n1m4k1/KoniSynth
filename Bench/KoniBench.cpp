// KoniBench: carga el KoniSynth.vst3 compilado y mide el coste de processBlock sin tarjeta de audio.
// Uso: KoniBench <ruta a KoniSynth.vst3>
// Salida por escenario: factor de tiempo real (audio procesado / tiempo de CPU), % de un núcleo y peor bloque
// frente a la duración del bloque (256 muestras a 48 kHz = 5,33 ms).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "../Source/Import/Dx7Syx.h" // syx: prueba del lector de SysEx del DX7 (sin el plugin)
using APVTS = juce::AudioProcessorValueTreeState;
#include "../Source/Params/UndoHistory.h" // undotest: prueba unitaria del historial (sin el plugin)
#include <bitset>
#include "../Source/Transport/SessionTransport.h" // transporttest: REC / PLAY / MIDI (sin el plugin)
#include "../Source/Dsp/Tuning.h" // tuningtest: Scala y MTS-ESP (sin el plugin)
#include <complex>
#include "../Source/Dsp/VoiceSaturator.h" // sattest: media banda 2x de la saturación por voz
#include "../Source/Engines/RhythmPatterns.h" // rhythmtest: ritmos de usuario en MIDI (sin el plugin)
#include "../Source/Engines/UserRhythms.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;
constexpr double kSeconds = 10.0;

juce::AudioProcessorParameter* find (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == name)
            return param;
    std::printf ("  [aviso] parámetro no encontrado: %s\n", name.toRawUTF8());
    return nullptr;
}

void set (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    if (auto* param = find (p, name))
        param->setValue (normalised);
}

struct Scenario
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // nombre del parámetro, valor normalizado 0..1
    int notes;
};

void run (juce::AudioPluginInstance& plugin, const Scenario& s)
{
    // Restablece a valores por defecto y aplica el escenario
    for (auto* param : plugin.getParameters())
        param->setValue (param->getDefaultValue());
    for (const auto& [name, value] : s.values)
        set (plugin, name, value);

    plugin.prepareToPlay (kSampleRate, kBlockSize);
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    const auto numBlocks = (int) (kSeconds * kSampleRate / kBlockSize);
    double total = 0.0, worst = 0.0;
    int worstBlock = -1, over50 = 0; // peor bloque y bloques por encima del 50 % de su duración
    std::vector<double> times;

    for (int b = 0; b < numBlocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n = 0; n < s.notes; ++n)
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + 3 * n, (juce::uint8) 100), 0);

        buffer.clear();
        const auto t0 = std::chrono::high_resolution_clock::now();
        plugin.processBlock (buffer, midi);
        const auto t1 = std::chrono::high_resolution_clock::now();

        const auto dt = std::chrono::duration<double> (t1 - t0).count();
        if (b > 10) // descarta los primeros bloques (caches frías, asignaciones de arranque)
        {
            total += dt;
            if (dt > worst) { worst = dt; worstBlock = b; }
            if (dt > 0.5 * kBlockSize / kSampleRate) ++over50;
            times.push_back (dt);
        }
    }

    plugin.releaseResources();
    std::sort (times.begin(), times.end());
    const auto p99 = times[(size_t) (0.99 * (double) (times.size() - 1))];
    const auto audio = (numBlocks - 11) * kBlockSize / kSampleRate;
    std::printf ("%-34s  x%6.1f  CPU %5.2f %%  p99 %5.3f ms  peor %5.3f ms (bloque %d)  >50 %%: %d bloques\n",
                 s.name, audio / total, 100.0 * total / audio, 1000.0 * p99, 1000.0 * worst, worstBlock, over50);
}
}

// Modo render: KoniBench <vst3> render <salida.wav> <segundos> [Nombre=texto ...] [nota:inicio:duración:velocidad[:canal] ...]
// Los valores se dan como texto del parámetro ("Engine=Drum Machine", "Drum Tempo=120"). Mono a 48 kHz, 24 bits.
int render (juce::AudioPluginInstance& plugin, int argc, char** argv)
{
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[3])));
    const auto seconds = juce::String (argv[4]).getDoubleValue();
    struct Note { int note; double start, length; int velocity, channel; };
    std::vector<Note> notes;
    // Eventos de expresión: pb:t:valor:canal (pitch bend -8192..8191), cc:t:num:valor:canal, cp:t:valor:canal (presión de canal),
    // pat:t:nota:valor:canal (aftertouch polifónico); t en segundos
    struct Event { double time; juce::MidiMessage message; };
    std::vector<Event> events;
    auto stereo = false; // --stereo: WAV de 2 canales (por defecto, solo el izquierdo, como siempre)
    auto rate = kSampleRate; // --rate=<Hz>: frecuencia de muestreo del render (por defecto 48 kHz)
    // Propiedades del estado del plugin: --sample=<wav> (samplePath, el sample del panel Sample), --osc2-sample=<wav>
    // (osc2SamplePath, el de Osc 2 en modo Sample) y --prop=<nombre>=<valor>
    std::vector<std::pair<juce::String, juce::String>> props;
    juce::File dumpState; // --dump-state=<fichero>: estado del plugin (tras parámetros y propiedades) en el formato de filterState del standalone
    for (int a = 5; a < argc; ++a)
    {
        const juce::String arg = juce::String::fromUTF8 (argv[a]);
        if (arg == "--stereo")
            stereo = true;
        else if (arg.startsWith ("--rate="))
            rate = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (arg.startsWith ("--sample=") || arg.startsWith ("--osc2-sample="))
            props.push_back ({ arg.startsWith ("--sample=") ? "samplePath" : "osc2SamplePath",
                               juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)).getFullPathName() });
        else if (arg.startsWith ("--dump-state="))
            dumpState = juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));
        else if (arg.startsWith ("--param=")) // parámetro por su ID y valor real (como en los presets de fábrica)
            props.push_back ({ "param:" + arg.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf ("=", false, false),
                               arg.fromFirstOccurrenceOf ("=", false, false).fromFirstOccurrenceOf ("=", false, false) });
        else if (arg.startsWith ("--learn=")) // --learn=<cc>:<ID de parámetro o action:panic|record|stop|play>: MIDI learn en el estado
            props.push_back ({ "learn:" + arg.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf (":", false, false),
                               arg.fromFirstOccurrenceOf ("=", false, false).fromFirstOccurrenceOf (":", false, false) });
        else if (arg.startsWith ("--prop="))
            props.push_back ({ arg.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf ("=", false, false),
                               arg.fromFirstOccurrenceOf ("=", false, false).fromFirstOccurrenceOf ("=", false, false) });
        else if (arg.containsChar ('='))
        {
            if (auto* param = find (plugin, arg.upToFirstOccurrenceOf ("=", false, false)))
                param->setValue (param->getValueForText (arg.fromFirstOccurrenceOf ("=", false, false)));
        }
        else if (arg.startsWith ("pb:") || arg.startsWith ("cc:") || arg.startsWith ("cp:") || arg.startsWith ("pat:"))
        {
            const auto p = juce::StringArray::fromTokens (arg, ":", "");
            const auto t = p[1].getDoubleValue();
            if (p[0] == "pb")       events.push_back ({ t, juce::MidiMessage::pitchWheel (p[3].getIntValue(), juce::jlimit (0, 16383, p[2].getIntValue() + 8192)) });
            else if (p[0] == "cc")  events.push_back ({ t, juce::MidiMessage::controllerEvent (p[4].getIntValue(), p[2].getIntValue(), p[3].getIntValue()) });
            else if (p[0] == "cp")  events.push_back ({ t, juce::MidiMessage::channelPressureChange (p[3].getIntValue(), p[2].getIntValue()) });
            else                    events.push_back ({ t, juce::MidiMessage::aftertouchChange (p[4].getIntValue(), p[2].getIntValue(), p[3].getIntValue()) });
        }
        else
        {
            const auto parts = juce::StringArray::fromTokens (arg, ":", "");
            notes.push_back ({ parts[0].getIntValue(), parts[1].getDoubleValue(), parts[2].getDoubleValue(), parts[3].getIntValue(),
                              parts.size() > 4 ? parts[4].getIntValue() : 1 }); // canal MIDI opcional (1..16)
        }
    }

    if (! props.empty())
    {
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        // El host VST3 de JUCE envuelve el estado: <VST3PluginState><IComponent>base64</IComponent>. Dentro, el bloque XML
        // binario del plugin (cabecera de 8 bytes + texto + 0) y, detrás, los datos privados del wrapper, que se conservan
        auto wrapper = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto* component = wrapper != nullptr ? wrapper->getChildByName ("IComponent") : nullptr;
        juce::MemoryBlock inner;
        if (component != nullptr && inner.fromBase64Encoding (component->getAllSubText()) && inner.getSize() > 8)
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize()))
            {
                const auto oldLength = 9 + (size_t) juce::ByteOrder::littleEndianInt (static_cast<const char*> (inner.getData()) + 4);
                for (const auto& [name, value] : props)
                {
                    if (name.startsWith ("learn:"))
                    {
                        auto* learnXml = xml->getChildByName ("MIDILEARN");
                        if (learnXml == nullptr) learnXml = xml->createNewChildElement ("MIDILEARN");
                        auto* map = learnXml->createNewChildElement ("MAP");
                        map->setAttribute ("cc", name.substring (6).getIntValue());
                        map->setAttribute ("param", value);
                    }
                    else if (name.startsWith ("param:"))
                    {
                        if (auto* e = xml->getChildByAttribute ("id", name.substring (6)))
                            e->setAttribute ("value", value);
                        else
                            std::printf ("  [aviso] parámetro no encontrado en el estado: %s\n", name.substring (6).toRawUTF8());
                    }
                    else
                        xml->setAttribute (name, value);
                }
                juce::MemoryBlock updatedInner;
                juce::AudioProcessor::copyXmlToBinary (*xml, updatedInner);
                if (oldLength < inner.getSize())
                    updatedInner.append (static_cast<const char*> (inner.getData()) + oldLength, inner.getSize() - oldLength);
                component->deleteAllTextElements();
                component->addTextElement (updatedInner.toBase64Encoding());
                juce::MemoryBlock updated;
                juce::AudioProcessor::copyXmlToBinary (*wrapper, updated);
                plugin.setStateInformation (updated.getData(), (int) updated.getSize());
            }
        if (wrapper == nullptr || component == nullptr)
            std::printf ("  [aviso] estado VST3 con formato inesperado: no se aplicaron las propiedades\n");
    }

    if (dumpState != juce::File())
    {
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        auto wrapper = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto* component = wrapper != nullptr ? wrapper->getChildByName ("IComponent") : nullptr;
        juce::MemoryBlock inner;
        if (component != nullptr && inner.fromBase64Encoding (component->getAllSubText()))
            dumpState.replaceWithText (inner.toBase64Encoding());
    }

    plugin.prepareToPlay (rate, kBlockSize);
    const auto total = (int) (seconds * rate);
    const auto outChannels = stereo ? 2 : 1;
    juce::AudioBuffer<float> result (outChannels, total);
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += kBlockSize)
    {
        const auto n = std::min (kBlockSize, total - pos);
        midi.clear();
        for (const auto& note : notes)
        {
            const auto on = (int) (note.start * rate), off = (int) ((note.start + note.length) * rate);
            if (on >= pos && on < pos + n)   midi.addEvent (juce::MidiMessage::noteOn (note.channel, note.note, (juce::uint8) note.velocity), on - pos);
            if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (note.channel, note.note), off - pos);
        }
        for (const auto& e : events)
            if (const auto at = (int) (e.time * rate); at >= pos && at < pos + n)
                midi.addEvent (e.message, at - pos);
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        plugin.processBlock (buffer, midi);
        for (int c = 0; c < outChannels; ++c)
            result.copyFrom (c, pos, buffer, c, 0, n);
    }
    plugin.releaseResources();

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (outChannels).withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (result, 0, total))
    {
        std::printf ("no se pudo escribir %s\n", out.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("%s: %.2f s, pico %.3f, latencia %d muestras\n", out.getFullPathName().toRawUTF8(), seconds,
                 result.getMagnitude (0, total), plugin.getLatencySamples());
    return 0;
}

// undotest: procesador mínimo con 2 parámetros y el mismo UndoHistory del plugin; gestos, propiedad y carga agrupada
namespace
{
struct TinyProcessor : juce::AudioProcessor
{
    TinyProcessor() : apvts (*this, nullptr, "P", { std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "a", 1 }, "A", 0.0f, 1.0f, 0.0f),
                                                    std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "b", 1 }, "B", 0.0f, 1.0f, 0.0f) }) {}
    const juce::String getName() const override { return "tiny"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    APVTS apvts;
};

int undoTest()
{
    TinyProcessor proc;
    UndoHistory h;
    h.attach (proc, proc.apvts, { "perfMacros" });
    auto* a = proc.apvts.getParameter ("a");
    auto* b = proc.apvts.getParameter ("b");
    auto gesture = [] (juce::RangedAudioParameter* p, std::initializer_list<float> values)
    {
        p->beginChangeGesture();
        for (auto v : values) p->setValueNotifyingHost (v);
        p->endChangeGesture();
    };
    int fails = 0;
    auto near = [] (float x, float y) { return std::abs (x - y) < 1.0e-6f; };
    auto check = [&fails] (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "ok   " : "FALLO", what); fails += ok ? 0 : 1; };
    gesture (a, { 0.1f, 0.2f, 0.3f });                         // arrastre: 3 valores, un paso
    gesture (b, { 0.5f });
    check (h.canUndo() && h.undoName() == "B", "dos gestos -> dos pasos, el último es B");
    h.undo();
    check (near (b->getValue(), 0.0f) && near (a->getValue(), 0.3f), "undo: B vuelve a 0, A sigue en 0.3");
    h.undo();
    check (near (a->getValue(), 0.0f) && ! h.canUndo(), "undo: A vuelve a 0 (antes del arrastre entero)");
    h.redo(); h.redo();
    check (near (a->getValue(), 0.3f) && near (b->getValue(), 0.5f) && ! h.canRedo(), "redo x2: A 0.3, B 0.5");
    h.undo();
    gesture (a, { 0.9f });
    check (! h.canRedo(), "gesto nuevo tras undo borra el redo");
    a->setValueNotifyingHost (0.7f);                           // sin gesto (host, macro, MIDI): no entra
    check (h.undoName() == "A" && near (a->getValue(), 0.7f), "cambio sin gesto no crea paso");
    proc.apvts.state.setProperty ("perfMacros", "1|a|0|1", nullptr);
    check (h.undoName() == "perfMacros", "propiedad seguida = paso");
    proc.apvts.state.setProperty ("foldFx", true, nullptr);
    check (h.undoName() == "perfMacros", "propiedad no seguida no crea paso");
    h.undo();
    check (proc.apvts.state.getProperty ("perfMacros").toString().isEmpty(), "undo de la propiedad");
    h.beginGroup();                                             // carga de preset: un paso
    gesture (a, { 0.25f }); gesture (b, { 0.75f });
    proc.apvts.state.setProperty ("perfMacros", "2|b|0|1", nullptr);
    h.endGroup();
    check (h.undoName() == "Preset / 3 changes", "carga agrupada = un paso con 3 cambios");
    h.undo();
    check (near (a->getValue(), 0.7f) && near (b->getValue(), 0.0f) && proc.apvts.state.getProperty ("perfMacros").toString().isEmpty(),
           "undo de la carga: A 0.7 (su valor previo, puesto sin gesto), B 0, propiedad vacía");
    // Gestos seguidos sobre el mismo parámetro (rueda: un gesto por clic) se funden en un paso hasta tocar otro
    h.clear();
    a->setValueNotifyingHost (0.0f); b->setValueNotifyingHost (0.0f);
    gesture (a, { 0.1f }); gesture (a, { 0.2f }); gesture (a, { 0.3f });
    gesture (b, { 0.4f });
    gesture (a, { 0.5f }); gesture (a, { 0.6f });
    h.undo();
    check (near (a->getValue(), 0.3f) && near (b->getValue(), 0.4f), "rueda: dos gestos de A tras B = un paso (A vuelve a 0.3)");
    h.undo();
    check (near (b->getValue(), 0.0f), "B: su propio paso");
    h.undo();
    check (near (a->getValue(), 0.0f) && ! h.canUndo(), "tres gestos de A seguidos = un paso (A vuelve a 0)");
    h.redo();
    gesture (a, { 0.35f });
    check (h.canUndo() && ! h.canRedo(), "tras rehacer, un gesto del mismo parámetro abre paso nuevo");
    h.undo();
    check (near (a->getValue(), 0.3f), "...y deshacerlo vuelve al valor rehecho (0.3), no a 0");
    h.clear();
    gesture (a, { 0.8f }); gesture (a, { 0.3f });
    check (! h.canUndo(), "fundido que vuelve al valor de partida: sin paso");

    for (int i = 0; i < 250; ++i) gesture (i % 2 == 0 ? a : b, { (float) (i % 100) / 100.0f + 0.001f }); // alternos: no se funden
    int steps = 0; while (h.canUndo()) { h.undo(); ++steps; }
    check (steps == (int) UndoHistory::maxSteps, "tope de 200 pasos");
    proc.apvts.replaceState (juce::ValueTree ("P"));
    check (! h.canUndo() && ! h.canRedo(), "estado nuevo del host borra el historial");
    h.detach();
    std::printf ("%d fallos\n", fails);
    return fails;
}

// transporttest: graba 200 bloques de 512 con notas, relee WAV y .mid, reproduce el audio (a 48 y 44,1 kHz) y el .mid
int transportTest()
{
    int fails = 0;
    const auto check = [&fails] (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "OK   " : "FALLO", what.toRawUTF8());
        if (! ok) ++fails;
    };
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KoniTransportTest");
    dir.deleteRecursively();
    dir.createDirectory();
    constexpr double fs = 48000.0;
    constexpr int bs = 512, blocks = 200, total = bs * blocks;
    const auto signal = [] (int ch, juce::int64 n)
    {
        return ch == 0 ? 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) n / fs)
                       : (float) (n % 1000) / 1000.0f - 0.5f;
    };
    SessionTransport t;
    t.prepare (fs);

    // 1) REC
    const auto wav = dir.getChildFile ("take.wav");
    check (t.startRecording (wav, 120.0).isEmpty(), "REC arranca");
    std::vector<juce::int64> onTimes;
    juce::AudioBuffer<float> buf (2, bs);
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < bs; ++i)
                buf.setSample (ch, i, signal (ch, (juce::int64) b * bs + i));
        juce::MidiBuffer midi;
        if (b % 20 == 3) { midi.addEvent (juce::MidiMessage::noteOn (1, 60 + b / 20, (juce::uint8) 100), 137); onTimes.push_back ((juce::int64) b * bs + 137); }
        if (b % 20 == 13) midi.addEvent (juce::MidiMessage::noteOff (1, 60 + b / 20), 5);
        t.injectMidi (midi, bs);
        t.captureMidi (midi);
        t.processAudio (buf);
    }
    const auto recSecs = t.recordedSeconds();
    check (t.stopRecording() == wav && std::abs (recSecs - total / fs) < 1.0e-9, "STOP cierra la toma: " + juce::String (recSecs, 4) + " s");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (wav));
        check (r != nullptr && r->lengthInSamples == total && r->numChannels == 2 && r->bitsPerSample == 32 && r->usesFloatingPointData,
               "WAV: " + juce::String (r != nullptr ? r->lengthInSamples : -1) + " muestras, 2 canales, 32 bit float");
        juce::AudioBuffer<float> back (2, total);
        r->read (&back, 0, total, 0, true, true);
        float err = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < total; ++n)
                err = juce::jmax (err, std::abs (back.getSample (ch, n) - signal (ch, n)));
        check (err == 0.0f, "WAV bit a bit: error máximo " + juce::String (err));
    }
    const auto mid = wav.withFileExtension ("mid");
    {
        juce::FileInputStream in (mid);
        juce::MidiFile mf;
        check (in.openedOk() && mf.readFrom (in), ".mid hermano escrito");
        mf.convertTimestampTicksToSeconds();
        std::vector<juce::int64> fileOn;
        for (int k = 0; k < mf.getNumTracks(); ++k)
            for (const auto* e : *mf.getTrack (k))
                if (e->message.isNoteOn()) fileOn.push_back (std::llround (e->message.getTimeStamp() * fs));
        juce::int64 worst = fileOn.size() == onTimes.size() ? 0 : 1 << 30;
        for (size_t k = 0; k < juce::jmin (fileOn.size(), onTimes.size()); ++k) worst = juce::jmax (worst, std::abs (fileOn[k] - onTimes[k]));
        check (worst <= 13, juce::String ((int) fileOn.size()) + " notas en el .mid; error de tiempo máximo " + juce::String (worst) + " muestras (medio tick a 120 BPM = 12,5)");
    }

    // 2) PLAY del audio a 48 kHz: la toma es la fuente
    const auto playAudio = [&t] (int expectFrames, std::vector<float>& left)
    {
        juce::Thread::sleep (100); // precarga del hilo del transporte
        t.play();
        juce::AudioBuffer<float> out (2, bs);
        for (int guard = 0; t.isPlaying() && guard < 4 * expectFrames / bs; ++guard)
        {
            out.clear();
            juce::MidiBuffer none;
            t.injectMidi (none, bs);
            t.processAudio (out);
            for (int i = 0; i < bs; ++i) left.push_back (out.getSample (0, i));
            juce::Thread::sleep (2);
        }
    };
    check (t.getSource() == SessionTransport::Source::audio, "tras grabar, la fuente es la toma");
    {
        std::vector<float> left;
        playAudio (total, left);
        float err = left.size() >= (size_t) total ? 0.0f : 1.0f;
        for (int n = 0; n < juce::jmin (total, (int) left.size()); ++n) err = juce::jmax (err, std::abs (left[(size_t) n] - signal (0, n)));
        check (! t.isPlaying() && err == 0.0f, "PLAY 48 kHz: se para sola al final; error máximo " + juce::String (err) + "; drops " + juce::String (t.getDrops()));
    }
    // 3) PLAY a 44,1 kHz: remuestreo; el tono sigue en 440 Hz
    {
        t.prepare (44100.0);
        check (t.loadAudio (wav).isEmpty(), "carga del WAV a 44,1 kHz");
        std::vector<float> left;
        playAudio (total, left);
        int crossings = 0, first = -1, lastX = -1;
        const auto expect = juce::jmin ((int) left.size(), (int) std::floor (total * 44100.0 / fs));
        for (int n = 200; n < expect - 200; ++n) // sin los bordes: arranque del interpolador y corte a 0 al final
            if (left[(size_t) n - 1] < 0.0f && left[(size_t) n] >= 0.0f) { if (first < 0) first = n; lastX = n; ++crossings; }
        const auto f = lastX > first ? (crossings - 1) * 44100.0 / (lastX - first) : 0.0;
        check (std::abs (f - 440.0) < 0.05, "PLAY 44,1 kHz: tono " + juce::String (f, 3) + " Hz (440), " + juce::String ((int) left.size()) + " muestras");
        t.prepare (fs);
    }
    // 4) PLAY del .mid: mismos instantes que la toma
    {
        check (t.loadMidi (mid).isEmpty(), "carga del .mid");
        t.play();
        std::vector<juce::int64> got;
        int offs = 0;
        for (int b = 0; t.isPlaying() && b < 2 * blocks; ++b)
        {
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> out (2, bs);
            out.clear();
            t.injectMidi (midi, bs);
            t.processAudio (out);
            for (const auto m : midi)
            {
                if (m.getMessage().isNoteOn()) got.push_back ((juce::int64) b * bs + m.samplePosition);
                if (m.getMessage().isNoteOff()) ++offs;
            }
        }
        juce::int64 worst = got.size() == onTimes.size() ? 0 : 1 << 30;
        for (size_t k = 0; k < juce::jmin (got.size(), onTimes.size()); ++k) worst = juce::jmax (worst, std::abs (got[k] - onTimes[k]));
        check (! t.isPlaying() && offs == (int) got.size() && worst <= 13,
               "PLAY .mid: " + juce::String ((int) got.size()) + " notas, " + juce::String (offs) + " apagadas; error máximo " + juce::String (worst) + " muestras");
    }
    // 4b) Un .mid con SysEx (p. ej. patches propios de un juego para el MT-32): la importación los conserva y PLAY los inyecta
    {
        juce::MidiMessageSequence seq;
        const juce::uint8 sysex[] = { 0x41, 0x10, 0x16, 0x12, 0x20, 0x00, 0x00, 0x4B, 0x4F, 0x4E, 0x49, 0x00 }; // texto al LCD
        seq.addEvent (juce::MidiMessage::createSysExMessage (sysex, (int) sizeof (sysex)), 0.0);
        seq.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 960.0);
        seq.addEvent (juce::MidiMessage::noteOff (2, 60), 1920.0);
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (960);
        mf.addTrack (seq);
        const auto sx = dir.getChildFile ("sysex.mid");
        { juce::FileOutputStream os (sx); os.setPosition (0); os.truncate(); mf.writeTo (os); }
        check (t.loadMidi (sx).isEmpty(), "carga del .mid con SysEx");
        t.play();
        int sysexCount = 0, notes = 0;
        for (int b = 0; t.isPlaying() && b < 400; ++b)
        {
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> out (2, bs);
            out.clear();
            t.injectMidi (midi, bs);
            t.processAudio (out);
            for (const auto m : midi)
            {
                sysexCount += m.getMessage().isSysEx() ? 1 : 0;
                notes += m.getMessage().isNoteOn() ? 1 : 0;
            }
        }
        t.stop();
        check (sysexCount == 1 && notes == 1, "PLAY .mid con SysEx: " + juce::String (sysexCount) + " SysEx y " + juce::String (notes) + " nota (1 y 1)");
        check (t.loadMidi (mid).isEmpty(), "vuelve a cargar el .mid de la toma");
    }
    // 5) PAUSE con una nota sonando: se apaga en el bloque siguiente
    {
        t.play();
        bool on = false, off = false;
        for (int b = 0; b < 6; ++b)
        {
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> out (2, bs);
            out.clear();
            t.injectMidi (midi, bs);
            t.processAudio (out);
            for (const auto m : midi) on |= m.getMessage().isNoteOn();
        }
        t.pause();
        juce::MidiBuffer midi;
        t.injectMidi (midi, bs);
        for (const auto m : midi) off |= m.getMessage().isNoteOff() && m.getMessage().getNoteNumber() == 60;
        check (on && off, "PAUSE apaga la nota que sonaba");
        t.stop();
    }
    // 6) Exportar el MIDI de la toma
    {
        const auto out = dir.getChildFile ("export.mid");
        const auto ok = t.exportMidi (out).isEmpty();
        check (ok && out.getSize() == mid.getSize() && out.getSize() > 0, "Export MIDI: " + juce::String (out.getSize()) + " bytes (igual que el .mid hermano)");
    }
    std::printf ("%d fallos\n", fails);
    return fails;
}

// rhythmtest: ritmos de usuario en MIDI (Engines/UserRhythms.h). Ida y vuelta de los patrones de fábrica, compases de vals,
// tresillo y 32 pasos, e importación de un MIDI como lo exporta un DAW (tempo en otra pista, notas desplazadas, alias GM)
int rhythmTest()
{
    int fails = 0;
    const auto check = [&fails] (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "OK   " : "FALLO", what.toRawUTF8());
        if (! ok) ++fails;
    };
    const auto fromPattern = [] (const RhythmPattern& p)
    {
        UserRhythms::Rhythm r;
        r.name = p.name;
        r.clear (p.numSteps, p.stepsPerBeat);
        for (const auto& [slot, lane] : p.lanes)
            r.lanes[(size_t) slot] = lane;
        return r;
    };
    const auto roundTrip = [] (const UserRhythms::Rhythm& r, UserRhythms::Rhythm& back, juce::String& error)
    {
        juce::MemoryOutputStream out;
        if (! UserRhythms::write (r, out)) { error = "write"; return false; }
        juce::MemoryInputStream in (out.getData(), out.getDataSize(), false);
        return UserRhythms::read (in, "x", back, error);
    };
    const auto same = [] (const UserRhythms::Rhythm& a, const UserRhythms::Rhythm& b)
    {
        if (a.numSteps != b.numSteps || a.stepsPerBeat != b.stepsPerBeat || a.name != b.name) return false;
        for (size_t s = 0; s < a.lanes.size(); ++s)
        {
            // Un carril sin golpes equivale a ninguno
            const auto empty = [] (const std::string& l) { return l.find_first_not_of ('.') == std::string::npos; };
            if (empty (a.lanes[s]) != empty (b.lanes[s])) return false;
            if (! empty (a.lanes[s]) && a.lanes[s] != b.lanes[s]) return false;
        }
        return true;
    };

    // 1) Los 29 patrones de fábrica: escribir y leer da el mismo patrón (longitud, rejilla, niveles y carriles)
    int ok = 0;
    juce::String bad;
    for (const auto& p : drumPatterns())
    {
        const auto r = fromPattern (p);
        UserRhythms::Rhythm back;
        juce::String error;
        if (roundTrip (r, back, error) && same (r, back)) ++ok; else bad << p.name << " (" << error << ") ";
    }
    check (ok == (int) drumPatterns().size(), "ida y vuelta de los patrones de fábrica: " + juce::String (ok) + " de "
           + juce::String ((int) drumPatterns().size()) + (bad.isNotEmpty() ? " | " + bad : juce::String()));

    // 2) Vals (12 pasos a 4 por tiempo, 3/4), tresillo (12 a 3, en 4/4) y dos compases (32)
    {
        UserRhythms::Rhythm r, back; juce::String e;
        r.name = "Waltz test"; r.clear (12, 4); r.lanes[(size_t) DrumSlot::BD] = "x..........."; r.lanes[(size_t) DrumSlot::CH] = "....x...x...";
        check (roundTrip (r, back, e) && back.numSteps == 12 && back.stepsPerBeat == 4 && same (r, back), "vals 12/4 (compás 3/4) " + e);
        r.name = "Shuffle test"; r.clear (12, 3); r.lanes[(size_t) DrumSlot::BD] = "x.....x....."; r.lanes[(size_t) DrumSlot::SD] = "...X.....X.."; r.lanes[(size_t) DrumSlot::CH] = "x.xx.xx.xx.x";
        check (roundTrip (r, back, e) && back.numSteps == 12 && back.stepsPerBeat == 3 && same (r, back), "tresillo 12/3 (corcheas de tresillo en 4/4) " + e);
        r.name = "Two bars"; r.clear (32, 4); r.lanes[(size_t) DrumSlot::BD] = "x...x...x...x...x...x...x...x.x."; r.lanes[(size_t) DrumSlot::OH] = std::string (31, '.') + "X";
        check (roundTrip (r, back, e) && back.numSteps == 32 && same (r, back), "dos compases (32 pasos, último paso con acento) " + e);
        // Casos que solo resuelve la marca de rejilla: tresillo con golpes solo en los tiempos y segundo compás vacío
        r.name = "Beats only"; r.clear (12, 3); r.lanes[(size_t) DrumSlot::BD] = "x..x..x..x..";
        check (roundTrip (r, back, e) && back.numSteps == 12 && back.stepsPerBeat == 3 && same (r, back), "tresillo solo en los tiempos (marca de rejilla) " + e);
        r.name = "Bar one only"; r.clear (32, 4); r.lanes[(size_t) DrumSlot::BD] = "x...x...x...x..." + std::string (16, '.');
        check (roundTrip (r, back, e) && back.numSteps == 32 && same (r, back), "32 pasos con el segundo compás vacío (marca de rejilla) " + e);
    }

    // 3) Importación como de un DAW: tipo 1, tempo en la pista 0, batería en la 1 con 480 ticks por negra, notas adelantadas
    //    o retrasadas 20 ticks, alias GM (35 = bombo, 44 = charles pedal), una nota fuera del mapa (60) y velocities intermedias
    {
        juce::MidiMessageSequence tempo, drums;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent (468750), 0.0); // 128 BPM
        tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        drums.addEvent (juce::MidiMessage::textMetaEvent (3, "DAW Beat"), 0.0);
        const auto at = [&drums] (int note, double tick, int vel) { drums.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) vel), tick); drums.addEvent (juce::MidiMessage::noteOff (10, note), tick + 60.0); };
        at (35, 0.0, 120);     // bombo acento, paso 0
        at (38, 960.0 + 20.0, 100); // caja normal, paso 8 (retrasada 20 ticks)
        at (44, 480.0 - 20.0, 60);  // charles suave, paso 4 (adelantado)
        at (60, 240.0, 100);   // bongo: fuera del mapa -> ignorada
        drums.updateMatchedPairs();
        juce::MidiFile mf; mf.setTicksPerQuarterNote (480); mf.addTrack (tempo); mf.addTrack (drums);
        juce::MemoryOutputStream out; mf.writeTo (out, 1);
        juce::MemoryInputStream in (out.getData(), out.getDataSize(), false);
        UserRhythms::Rhythm r; juce::String e;
        const auto read = UserRhythms::read (in, "fallback", r, e);
        check (read && r.name == "DAW Beat" && r.numSteps == 16 && r.stepsPerBeat == 4, "MIDI de DAW: nombre, 16 pasos, semicorcheas " + e);
        check (read && r.lanes[(size_t) DrumSlot::BD].substr (0, 1) == "X" && r.lanes[(size_t) DrumSlot::SD][8] == 'x'
               && r.lanes[(size_t) DrumSlot::CH][4] == 'o' && r.ignored == 1,
               "cuantización ±20 ticks, alias GM 35/44, niveles por velocity (120 X, 100 x, 60 o), 1 nota ignorada (60)");
    }

    // 4) Ficheros que no son ritmos
    {
        juce::MemoryInputStream junk ("not midi", 8, false);
        UserRhythms::Rhythm r; juce::String e;
        // read antes que el mensaje: el orden de evaluación de los argumentos no está definido (MSVC, de derecha a izquierda)
        auto junkOk = UserRhythms::read (junk, "j", r, e);
        check (! junkOk && e.isNotEmpty(), "fichero que no es MIDI: rechazado (" + e + ")");
        juce::MidiMessageSequence melody; melody.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
        juce::MidiFile mf; mf.setTicksPerQuarterNote (96); mf.addTrack (melody);
        juce::MemoryOutputStream out; mf.writeTo (out, 0);
        juce::MemoryInputStream in (out.getData(), out.getDataSize(), false);
        e.clear();
        junkOk = UserRhythms::read (in, "m", r, e);
        check (! junkOk && e.contains ("channel 10"), "MIDI sin canal 10: rechazado (" + e + ")");
    }

    // 5) Omnichord: los 12 ritmos de fábrica con su bajo (canal 2) van y vuelven iguales; restrict los acepta
    {
        ok = 0; bad.clear();
        for (const auto& p : omnichordPatterns())
        {
            const auto r = fromPattern (p);
            UserRhythms::Rhythm back; juce::String error;
            const auto good = roundTrip (r, back, error) && same (r, back)
                              && UserRhythms::restrict (back, UserRhythms::spec (UserRhythms::Target::omnichord), error) && back.ignored == 0;
            if (good) ++ok; else bad << p.name << " (" << error << ") ";
        }
        check (ok == (int) omnichordPatterns().size(), "Omnichord: ida y vuelta con el bajo en el canal 2: " + juce::String (ok) + " de "
               + juce::String ((int) omnichordPatterns().size()) + (bad.isNotEmpty() ? " | " + bad : juce::String()));
    }

    // 6) restrict por motor: la caja de ritmos quita el bajo; el VL-1 deja sus 3 carriles, aplana niveles, un sonido por paso
    //    (gana BD, luego RS, luego CH) y rechaza el tresillo
    {
        UserRhythms::Rhythm r; juce::String e;
        r.clear (16, 4);
        r.lanes[(size_t) DrumSlot::BD] = "X...x...o...x...";
        r.lanes[(size_t) DrumSlot::RS] = "x.x.............";
        r.lanes[(size_t) DrumSlot::CH] = "x.x.x...........";
        r.lanes[(size_t) DrumSlot::SD] = "....x...........";
        r.lanes[(size_t) DrumSlot::Bass] = "r.......f.......";
        auto d = r;
        const auto drumsOk = UserRhythms::restrict (d, UserRhythms::spec (UserRhythms::Target::drums), e);
        check (drumsOk && d.lanes[(size_t) DrumSlot::Bass].empty() && d.ignored == 2, "caja de ritmos: el bajo se ignora (2 notas) " + e);
        auto v = r;
        const auto vlOk = UserRhythms::restrict (v, UserRhythms::spec (UserRhythms::Target::vl1), e);
        check (vlOk && v.lanes[(size_t) DrumSlot::BD] == "x...x...x...x..." && v.lanes[(size_t) DrumSlot::RS] == "..x............."
               && v.lanes[(size_t) DrumSlot::CH] == "................" && v.lanes[(size_t) DrumSlot::SD].empty() && v.ignored == 7,
               "VL-1: 3 carriles, sin niveles, un sonido por paso (BD > RS > CH), 7 notas ignoradas: " + juce::String (v.ignored) + " " + e);
        UserRhythms::Rhythm t; t.clear (12, 3); t.lanes[(size_t) DrumSlot::BD] = "x.....x.....";
        e.clear();
        const auto tripletOk = UserRhythms::restrict (t, UserRhythms::spec (UserRhythms::Target::vl1), e);
        check (! tripletOk && e.contains ("triplet"), "VL-1: tresillo rechazado (" + e + ")");
    }

    // 7) Patrones de duraciones (VL-1, H = 16 ticks = corchea, W = 32 = negra) a semicorcheas de 8 ticks: Rock 1 llena un
    //    compás; March (2 negras) se repite hasta 16 pasos; Waltz (3 negras) da el vals de 12
    {
        using S = DrumSlot;
        const int rockLane[] = { (int) S::BD, (int) S::RS, (int) S::CH, (int) S::CH, (int) S::BD, (int) S::RS, (int) S::CH, (int) S::RS };
        const int rockTicks[] = { 16, 16, 16, 16, 16, 16, 16, 16 };
        UserRhythms::Rhythm r;
        const auto rock = UserRhythms::fromDurations (rockLane, rockTicks, 8, 8, 32, r);
        check (rock && r.numSteps == 16 && r.lanes[(size_t) S::BD] == "x.......x......." && r.lanes[(size_t) S::RS] == "..x.......x...x."
               && r.lanes[(size_t) S::CH] == "....x.x.....x...", "duraciones: Rock 1 del VL-1 en 16 semicorcheas");
        const int marchLane[] = { (int) S::BD, (int) S::CH };
        const int marchTicks[] = { 32, 32 };
        const auto march = UserRhythms::fromDurations (marchLane, marchTicks, 2, 8, 32, r);
        check (march && r.numSteps == 16 && r.lanes[(size_t) S::BD] == "x.......x......." && r.lanes[(size_t) S::CH] == "....x.......x...",
               "duraciones: March (2 tiempos) repetida hasta 16 pasos");
        const int waltzLane[] = { (int) S::BD, (int) S::CH, (int) S::CH };
        const int waltzTicks[] = { 32, 32, 32 };
        const auto waltz = UserRhythms::fromDurations (waltzLane, waltzTicks, 3, 8, 32, r);
        check (waltz && r.isWaltz() && r.lanes[(size_t) S::CH] == "....x...x...", "duraciones: Waltz en el vals de 12 pasos");
    }
    std::printf ("%d fallos\n", fails);
    return fails;
}

// tuningtest: lector Scala (.scl/.kbm) con valores derivados a mano
int tuningTest()
{
    int fails = 0;
    const auto check = [&fails] (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "OK   " : "FALLO", what.toRawUTF8());
        if (! ok) ++fails;
    };
    const auto et = [] (int k) { return 440.0 * std::exp2 ((k - 69) / 12.0); };
    Tuning t;
    const KoniTuning::Scope scope (t);
    const auto hz = [&] (int k) { return et (k) * KoniTuning::ratio (k); };

    // 1) Sin afinación: ratio exactamente 1
    bool exact = true;
    for (int k = 0; k < 128; ++k) exact &= KoniTuning::ratio (k) == 1.0;
    check (exact, "sin escala: ratio == 1.0 exacto en las 128 teclas");

    // 2) 12-TET escrito como .scl (cents y la octava como razón 2/1): igual a 12-TET con error < 1e-12
    juce::String scl12 = "! 12tet.scl\n12-TET\n 12\n!\n";
    for (int i = 1; i < 12; ++i) scl12 << juce::String (i * 100) << ".0\n";
    scl12 << "2/1\n";
    check (t.load (scl12, {}, {}).isEmpty(), "carga de 12tet.scl");
    double worst = 0.0;
    for (int k = 0; k < 128; ++k) worst = juce::jmax (worst, std::abs (KoniTuning::ratio (k) - 1.0));
    check (worst < 1.0e-12, "12tet.scl = 12-TET: desviación máxima " + juce::String (worst, 3));

    // 3) Mesotónico de 1/4 de coma (meanquar.scl de la especificación): E4 (64) = 5/4 sobre C4 = 261,6256 · 1,25
    const juce::String mean = "! meanquar.scl\n!\n1/4-comma meantone scale. Pietro Aaron's temperament (1523)\n 12\n!\n 76.04900\n 193.15686\n 310.26303\n"
                              " 5/4\n 503.42157\n 579.47057\n 696.57843\n 25/16\n 889.73529\n 1006.84314\n 1082.89214\n 2/1\n";
    check (t.load (mean, {}, {}).isEmpty() && t.getScaleName().startsWith ("1/4-comma"), "carga de meanquar.scl; nombre = descripción");
    const auto c4 = 440.0 * std::exp2 (-9.0 / 12.0);
    check (std::abs (hz (64) - c4 * 1.25) < 1.0e-9 && std::abs (hz (60) - c4) < 1.0e-9 && std::abs (hz (72) - 2.0 * c4) < 1.0e-9,
           "meantone: C4 " + juce::String (hz (60), 4) + ", E4 " + juce::String (hz (64), 4) + " (5/4 = " + juce::String (c4 * 1.25, 4) + "), C5 " + juce::String (hz (72), 4));
    check (std::abs (hz (55) - c4 / 2.0 * std::exp2 (696.57843 / 1200.0)) < 1.0e-9, "meantone: G3 = C3 · 2^(696,57843/1200) (octava hacia abajo)");

    // 4) 31-EDO: tecla 61 = C4 · 2^(1/31); tecla 60 + 31 = C5
    juce::String edo = "31-EDO\n31\n";
    for (int i = 1; i <= 31; ++i) edo << juce::String (i * 1200.0 / 31.0, 6) << "\n";
    check (t.load (edo, {}, "31-EDO").isEmpty(), "carga de 31-EDO");
    check (std::abs (hz (61) / c4 - std::exp2 (1.0 / 31.0)) < 1.0e-6 && std::abs (hz (91) / c4 - 2.0) < 1.0e-6,
           "31-EDO: paso " + juce::String (1200.0 * std::log2 (hz (61) / c4), 4) + " cents (38,7097), tecla 91 = " + juce::String (hz (91) / c4, 6) + " · C4");

    // 5) .kbm: diatónica de 7 notas en las teclas blancas (negras = x), A4 = 432 Hz, octava formal = grado 7
    const juce::String dia = "Ptolemy diatonic\n7\n9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1\n";
    const juce::String kbm = "! white keys\n12\n0\n127\n60\n69\n432.0\n7\n! map\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n";
    check (t.load (dia, kbm, "dia").isEmpty(), "carga de diatónica + .kbm");
    // A4 (69) = grado 5 = 5/3 sobre C4 => C4 = 432 · 3/5 = 259,2; E4 = 5/4 · 259,2 = 324; C5 = 518,4; C3 = 129,6
    check (std::abs (hz (69) - 432.0) < 1.0e-9 && std::abs (hz (64) - 324.0) < 1.0e-9 && std::abs (hz (72) - 518.4) < 1.0e-9 && std::abs (hz (48) - 129.6) < 1.0e-9,
           "kbm: A4 " + juce::String (hz (69), 4) + " (432), E4 " + juce::String (hz (64), 4) + " (324), C5 " + juce::String (hz (72), 4) + " (518,4), C3 " + juce::String (hz (48), 4) + " (129,6)");
    check (t.isFiltered (61) && t.isFiltered (70) && ! t.isFiltered (60) && ! t.isFiltered (71), "kbm: negras sin mapear (x) se filtran; blancas no");
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
    t.filterNotes (midi);
    int ons = 0; for (const auto m : midi) ons += m.getMessage().isNoteOn() ? 1 : 0;
    t.reset();
    juce::MidiBuffer offs;
    offs.addEvent (juce::MidiMessage::noteOff (1, 61), 0);
    t.filterNotes (offs); // la escala ya no está, pero el note-off de la tecla quitada también se quita
    check (ons == 1 && offs.getNumEvents() == 0, "filterNotes: quita el note-on de la tecla x y su note-off aunque la escala cambie");
    check (KoniTuning::ratio (64) == 1.0, "reset: vuelve a 12-TET exacto");

    // 6) Errores
    check (t.load ("bad\nseven\n", {}, {}).isNotEmpty(), "rechaza un número de notas no numérico");
    check (t.load ("neg\n1\n-3/2\n", {}, {}).isNotEmpty(), "rechaza una razón negativa");
    check (t.load ("short\n3\n100.0\n", {}, {}).isNotEmpty(), "rechaza una escala con menos grados de los anunciados");
    check (t.load (dia, "12\n0\n127\n60\n69\n440.0\n7\n0\nx\n1\nx\n2\n3\nx\n4\nx\nx\n", {}).isNotEmpty(), "rechaza una nota de referencia sin mapear");
    check (KoniTuning::ratio (64) == 1.0, "tras un error, la afinación anterior sigue (12-TET)");
    std::printf ("%d fallos\n", fails);
    return fails;
}

// sattest: media banda 2x de la saturación por voz (sin la curva): ganancia, retardo, rechazo de imágenes y de aliasing
int satTest()
{
    int fails = 0;
    const auto check = [&fails] (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "OK   " : "FALLO", what.toRawUTF8());
        if (! ok) ++fails;
    };
    constexpr double fs = 48000.0, pi = juce::MathConstants<double>::pi;
    // Amplitud y fase de una componente f en x (correlación con ventana de Hann sobre la segunda mitad: transitorio fuera)
    const auto tone = [] (const std::vector<double>& x, double f, double rate)
    {
        const auto n0 = x.size() / 2, n = x.size() - n0;
        std::complex<double> acc {}; double wsum = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n);
            acc += w * x[n0 + i] * std::exp (std::complex<double> (0.0, -2.0 * pi * f * (double) (n0 + i) / rate));
            wsum += w;
        }
        return 2.0 * acc / wsum;
    };
    // 1) Subir y bajar: plano en la banda útil
    for (const auto f : { 100.0, 1000.0, 10000.0, 18000.0, 20000.0 })
    {
        VoiceSat::Upsampler2x up; VoiceSat::Downsampler2x down;
        std::vector<double> y;
        for (int i = 0; i < 48000; ++i)
        {
            const auto [e, o] = up.process (std::sin (2.0 * pi * f * i / fs));
            y.push_back (down.process (e, o));
        }
        const auto c = tone (y, f, fs);
        const auto delay = -std::arg (c * std::exp (std::complex<double> (0.0, pi / 2.0))) / (2.0 * pi * f) * fs; // sin = fase -π/2
        check (std::abs (20.0 * std::log10 (std::abs (c))) < 0.01, juce::String (f, 0) + " Hz: ganancia " + juce::String (20.0 * std::log10 (std::abs (c)), 4)
               + " dB, retardo " + juce::String (delay, 2) + " muestras");
    }
    // 2) Imagen del interpolador: 15 kHz -> su imagen en 48 - 15 = 33 kHz (a 96 kHz) debe quedar <= -99 dB
    {
        VoiceSat::Upsampler2x up; std::vector<double> y;
        for (int i = 0; i < 48000; ++i) { const auto [e, o] = up.process (std::sin (2.0 * pi * 15000.0 * i / fs)); y.push_back (e); y.push_back (o); }
        const auto img = std::abs (tone (y, 33000.0, 2.0 * fs)) / std::abs (tone (y, 15000.0, 2.0 * fs));
        check (20.0 * std::log10 (img) < -95.0, "imagen de 15 kHz en 33 kHz: " + juce::String (20.0 * std::log10 (img), 1) + " dB");
    }
    // 3) Diezmador: un tono de 30 kHz a 96 kHz se plegaría a 18 kHz; debe quedar <= -99 dB respecto a uno de 18 kHz
    {
        const auto run = [&] (double f)
        {
            VoiceSat::Downsampler2x down; std::vector<double> y;
            for (int i = 0; i < 48000; ++i)
                y.push_back (down.process (std::sin (2.0 * pi * f * (2 * i) / (2.0 * fs)), std::sin (2.0 * pi * f * (2 * i + 1) / (2.0 * fs))));
            return std::abs (tone (y, 18000.0, fs));
        };
        const auto rej = run (30000.0) / run (18000.0);
        check (20.0 * std::log10 (rej) < -95.0, "30 kHz plegado a 18 kHz: " + juce::String (20.0 * std::log10 (rej), 1) + " dB");
    }
    // 4) Volcado para medir aliasing fuera (KONI_SATDUMP=fichero): seno de C4 a 0,5 de pico, Hard, 24 dB, 72000 muestras
    if (const auto dumpPath = juce::SystemStats::getEnvironmentVariable ("KONI_SATDUMP", {}); dumpPath.isNotEmpty())
    {
        VoiceSat::Channel ch; ch.reset (VoiceSat::hard);
        const auto g = std::pow (10.0, 24.0 / 20.0), f0 = 440.0 * std::pow (2.0, -9.0 / 12.0);
        juce::File (dumpPath).deleteFile();
        juce::FileOutputStream out { juce::File (dumpPath) };
        for (int i = 0; i < 72000; ++i)
            out.writeFloat (ch.process (VoiceSat::hard, (float) (0.5 * std::sin (2.0 * pi * f0 * i / fs)), g, VoiceSat::makeup (VoiceSat::hard, g), 1.0f, 0.999f));
    }
    std::printf ("%d fallos\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 2 && juce::String (argv[1]) == "sattest")
        return satTest();
    if (argc >= 2 && juce::String (argv[1]) == "tuningtest")
    {
        juce::ScopedJuceInitialiser_GUI init;
        return tuningTest();
    }
    if (argc >= 2 && juce::String (argv[1]) == "transporttest")
    {
        juce::ScopedJuceInitialiser_GUI init;
        return transportTest();
    }
    if (argc >= 2 && juce::String (argv[1]) == "rhythmtest")
    {
        juce::ScopedJuceInitialiser_GUI init;
        return rhythmTest();
    }
    if (argc >= 2 && juce::String (argv[1]) == "undotest")
    {
        juce::ScopedJuceInitialiser_GUI init;
        return undoTest();
    }
    // KoniBench syx <fichero.syx>: imprime nombre y los 155 bytes VCED de cada voz (para comparar con el empaquetador de prueba)
    if (argc >= 3 && juce::String (argv[1]) == "syx")
    {
        std::vector<Dx7Syx::Voice> voices;
        if (const auto error = Dx7Syx::loadVoices (juce::File (juce::String::fromUTF8 (argv[2])), voices); error.isNotEmpty())
        {
            std::printf ("error: %s\n", error.toRawUTF8());
            return 1;
        }
        for (const auto& v : voices)
        {
            std::printf ("%s|", v.name.toRawUTF8());
            for (const auto b : v.vced) std::printf (" %d", (int) b);
            std::printf ("\n");
        }
        return 0;
    }
    if (argc < 2)
    {
        std::printf ("uso: KoniBench <KoniSynth.vst3> [render <salida.wav> <segundos> [Nombre=texto ...] [nota:inicio:duración:velocidad ...]]\n");
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI init;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, juce::String::fromUTF8 (argv[1]));
    if (types.isEmpty())
    {
        std::printf ("no se encontró ningún plugin en %s\n", argv[1]);
        return 1;
    }

    juce::String error;
    auto plugin = format.createInstanceFromDescription (*types[0], kSampleRate, kBlockSize, error);
    if (plugin == nullptr)
    {
        std::printf ("error al cargar: %s\n", error.toRawUTF8());
        return 1;
    }
    plugin->enableAllBuses();
    if (argc >= 5 && juce::String (argv[2]) == "render")
        return render (*plugin, argc, argv);
    // params: nombre, pasos y texto de cada paso (hasta 64) por parámetro, separado por tabuladores (UTF-8)
    if (argc >= 3 && juce::String (argv[2]) == "params")
    {
        for (auto* param : plugin->getParameters())
        {
            const auto n = param->getNumSteps();
            juce::String line = param->getName (64) + "\t" + juce::String (n);
            for (int i = 0; n >= 2 && n <= 64 && i < n; ++i)
                line << "\t" << param->getText ((float) i / (float) (n - 1), 64);
            line << "\t|\t" << param->getText (0.0f, 64) << "\t" << param->getText (1.0f, 64) << "\t" << param->getCurrentValueAsText();
            for (const auto v : { 0.1f, 0.5f, 0.9f }) // ida y vuelta texto -> valor (lo que hace "Set value...")
                line << "\t" << v << "=" << param->getText (v, 64) << "=" << param->getText (param->getValueForText (param->getText (v, 64)), 64);
            std::printf ("%s\n", line.toRawUTF8());
        }
        return 0;
    }

    std::printf ("%s, %d parámetros, %.0f Hz, bloque %d, %.0f s por escenario\n\n",
                 plugin->getName().toRawUTF8(), plugin->getParameters().size(), kSampleRate, kBlockSize, kSeconds);

    const std::vector<Scenario> scenarios {
        { "1. Init, 8 notas", {}, 8 },
        { "2. Unisono 7 + filtro, 8 notas", { { "Unison Voices", 1.0f }, { "Resonance", 0.5f }, { "Filter Drive", 0.5f } }, 8 },
        { "3. Complex 2x + unisono 7, 8 notas", { { "Complex On", 1.0f }, { "Unison Voices", 1.0f }, { "FM Index", 0.5f },
                                                  { "Timbre", 0.6f }, { "AM Depth", 0.3f } }, 8 },
        { "4. Todo activo, 8 notas", { { "Complex On", 1.0f }, { "Unison Voices", 1.0f }, { "FM Index", 0.5f }, { "Timbre", 0.6f },
                                       { "Tube On", 1.0f }, { "Chorus On", 1.0f }, { "Echo On", 1.0f }, { "Reverb On", 1.0f },
                                       { "Media On", 1.0f }, { "LFO On", 1.0f } }, 8 },
        { "4b. Todo activo (repetición)", { { "Complex On", 1.0f }, { "Unison Voices", 1.0f }, { "FM Index", 0.5f }, { "Timbre", 0.6f },
                                       { "Tube On", 1.0f }, { "Chorus On", 1.0f }, { "Echo On", 1.0f }, { "Reverb On", 1.0f },
                                       { "Media On", 1.0f }, { "LFO On", 1.0f } }, 8 },
        { "4c. Todo activo (repetición)", { { "Complex On", 1.0f }, { "Unison Voices", 1.0f }, { "FM Index", 0.5f }, { "Timbre", 0.6f },
                                       { "Tube On", 1.0f }, { "Chorus On", 1.0f }, { "Echo On", 1.0f }, { "Reverb On", 1.0f },
                                       { "Media On", 1.0f }, { "LFO On", 1.0f } }, 8 },
        { "5. Solo efectos (sin notas)", { { "Tube On", 1.0f }, { "Chorus On", 1.0f }, { "Echo On", 1.0f },
                                           { "Reverb On", 1.0f }, { "Media On", 1.0f } }, 0 },
        { "6. Theremin fisico + efectos", { { "Theremin On", 1.0f }, { "Theremin Volume", 0.8f }, { "Reverb On", 1.0f },
                                            { "Echo On", 1.0f }, { "Media On", 1.0f } }, 0 },
        // Motores retro (Engine: 10 opciones -> índice / 9)
        { "7. Stylophone", { { "Engine", 3.0f / 9.0f } }, 8 },
        { "8. Omnichord Rock 1, acorde", { { "Engine", 4.0f / 9.0f }, { "Omnichord Rhythm", 1.0f / 12.0f } }, 1 },
        { "9. Drum Machine 808 Electro", { { "Engine", 5.0f / 9.0f }, { "Drum Pattern", 5.0f / 28.0f } }, 0 },
        { "9b. Mini Pops Slow Rock + Rock 1", { { "Engine", 5.0f / 9.0f }, { "Drum Kit", 0.0f }, { "Drum Pattern", 19.0f / 28.0f },
                                                { "Drum Pattern 2", 1.0f / 28.0f } }, 0 },
        { "10. FM 6-Op alg 32, 6 op, 8 notas", { { "Engine", 6.0f / 9.0f }, { "FM Algorithm", 1.0f }, { "FM OP2 Level", 0.9f },
                                                 { "FM OP3 Level", 0.9f }, { "FM OP4 Level", 0.9f }, { "FM OP5 Level", 0.9f },
                                                 { "FM OP6 Level", 0.9f } }, 8 },
        { "11. Chip NES Poly, arpegio y vibrato", { { "Engine", 7.0f / 9.0f }, { "Chip Voice", 0.75f }, { "Chip Arpeggio", 0.5f },
                                                   { "Chip Vibrato Depth", 0.3f } }, 3 },
        { "12. Chip NES ruido corto", { { "Engine", 7.0f / 9.0f }, { "Chip Voice", 0.5f }, { "Chip Noise Mode", 1.0f } }, 1 },
        { "13. Polivoks, 2 notas", { { "Engine", 8.0f / 9.0f }, { "Polivoks Resonance", 0.6f } }, 2 },
        { "14. ANS bandas de ruido", { { "Engine", 1.0f }, { "ANS Score", 4.0f / 6.0f }, { "ANS Hold", 1.0f } }, 0 },
        { "15. Flanger + shifter + Leslie + muelles, 8 notas", { { "Flanger On", 1.0f }, { "Freq Shift On", 1.0f }, { "Leslie On", 1.0f },
                                                                 { "Spring On", 1.0f } }, 8 },
        { "16. Ampli JCM800 + 4x12 + compresor + limitador 5 ms, 8 notas", { { "Amp On", 1.0f }, { "Amp Gain", 0.8f },
                                                                             { "Comp On", 1.0f }, { "Limiter On", 1.0f },
                                                                             { "Limiter Lookahead", 1.0f } }, 8 },
        // Osc 2: sierra frente a Wavetable (Osc2 Wave normalizado: Saw = 2/4, Wavetable = 4/4), con sync
        { "17. Osc 2 sierra + sync, 8 notas", { { "Osc2 Wave", 0.5f }, { "Osc2 Level", 1.0f }, { "Osc2 Sync", 1.0f }, { "Osc2 Semi", 0.8f } }, 8 },
        { "18. Osc 2 Wavetable + sync, 8 notas", { { "Osc2 Wave", 1.0f }, { "Osc2 Level", 1.0f }, { "Osc2 Sync", 1.0f }, { "Osc2 Semi", 0.8f },
                                                    { "Osc2 WT Position", 0.5f }, { "Wavetable", 0.4f } }, 8 },
        // El 2 con Osc 3 en sierra a +1 octava (Sub Octave -1, Semi +24: normalizado 1): coste de Osc 3 frente al 2
        { "19. Unisono 7 + filtro + Osc 3 sierra, 8 notas", { { "Unison Voices", 1.0f }, { "Resonance", 0.5f }, { "Filter Drive", 0.5f },
                                                              { "Sub Level", 1.0f }, { "Osc3 Wave", 2.0f / 3.0f }, { "Osc3 Semi", 1.0f } }, 8 },
        // El 3 con Osc 2 / Osc 3 como modulador del Complex (Complex Mod Source: Osc 2 = 1/2, Osc 3 = 2/2), a nivel 0: coste del
        // modulador externo frente al interno
        { "20. Complex 2x + unisono 7, modulador Osc 2 sierra + sync", { { "Complex On", 1.0f }, { "Unison Voices", 1.0f }, { "FM Index", 0.5f },
                                                                        { "Timbre", 0.6f }, { "AM Depth", 0.3f }, { "Complex Mod Source", 0.5f },
                                                                        { "Osc2 Sync", 1.0f }, { "Osc2 Semi", 0.8f } }, 8 },
        { "21. Complex 2x + unisono 7, modulador Osc 3", { { "Complex On", 1.0f }, { "Unison Voices", 1.0f }, { "FM Index", 0.5f },
                                                          { "Timbre", 0.6f }, { "AM Depth", 0.3f }, { "Complex Mod Source", 1.0f },
                                                          { "Osc3 Semi", 1.0f } }, 8 },
    };

    for (const auto& s : scenarios)
        run (*plugin, s);

    return 0;
}

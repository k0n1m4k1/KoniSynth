// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, KoniSynthProcessor, SessionTransport). No es una cabecera independiente.
#pragma once

//==============================================================================
// Transporte de sesión (Transport/SessionTransport.h): REC / STOP / PLAY-PAUSE, tiempo en LED rojo (como el de BPM) y
// menú (botón "..." o clic derecho) para cargar audio, importar MIDI, exportar el MIDI de la toma y abrir las grabaciones.
// Alto >= 44 px: tiempo arriba y botones abajo; menor (barra inferior con el display plegado): todo en una línea
class TransportPanel : public juce::Component, private juce::Timer
{
public:
    std::function<void (const juce::String& title, const juce::String& message)> onError;

    explicit TransportPanel (KoniSynthProcessor& p) : processor (p), transport (p.sessionTransport)
    {
        rec.setTooltip ("Record: the output to a WAV file (32-bit float) and the incoming MIDI to a .mid file next to it, in "
                        "Documents/KoniSynth/Recordings. Press again to stop recording; the take becomes the player source. "
                        "Right-click: MIDI Learn.");
        stop.setTooltip ("Stop: ends recording and playback and goes back to the start. Right-click: MIDI Learn.");
        play.setTooltip ("Play / Pause: plays the loaded source. An audio file is mixed into the output; a MIDI file plays the "
                         "current sound as if it came from your controller (record it to render it to WAV). Right-click: MIDI Learn.");
        more.setTooltip ("Load an audio file, import or export MIDI, open the recordings folder.");
        rec.onClick = [this] { toggleRecord(); };
        stop.onClick = [this] { transport.stop(); refresh(); };
        play.onClick = [this] { if (transport.isPlaying()) transport.pause(); else transport.play(); refresh(); };
        more.onClick = [this] { showMenu(); };
        rec.learn = stop.learn = play.learn = &p.midiLearn; // clic derecho: MIDI Learn (acciones de MidiLearn)
        rec.action = MidiLearn::actionRecord;
        stop.action = MidiLearn::actionStop;
        play.action = MidiLearn::actionPlay;
        for (auto* b : { &rec, &stop, &play, &more })
        {
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible (*b);
        }
        startTimerHz (10);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showMenu();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (Skin::c (0xff120607));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Skin::white().withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        const auto lit = Skin::c (0xffff2a1a), dim = Skin::c (0xff7a2a22);
        const auto recording = transport.isRecording();
        // Tiempo: grabado (REC) o posición / duración de la fuente
        const auto text = recording ? "REC " + clock (transport.recordedSeconds())
                                    : clock (transport.positionSeconds()) + (transport.getSource() != SessionTransport::Source::none
                                                                                 ? " / " + clock (transport.getLengthSeconds()) : juce::String());
        g.setColour (lit.withAlpha (recording && blink ? 0.55f : 1.0f));
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), compact() ? 13.0f : 15.0f, juce::Font::bold));
        g.drawText (text, timeArea, juce::Justification::centredLeft, true);
        if (! compact())
        {
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (dim);
            const auto name = transport.getSourceName();
            g.drawText (name.isEmpty() ? juce::String ("no source") : name, nameArea, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 3);
        if (compact())
        {
            const auto b = r.getHeight();
            for (auto* button : { &rec, &stop, &play })
                button->setBounds (r.removeFromLeft (b + 4).withTrimmedRight (4));
            more.setBounds (r.removeFromRight (b));
            timeArea = r.reduced (4, 0);
            nameArea = {};
            return;
        }
        auto buttons = r.removeFromBottom (juce::jmin (28, r.getHeight() / 2));
        timeArea = r.removeFromTop (r.getHeight() / 2 + 2);
        nameArea = r;
        const auto w = buttons.getWidth() / 4;
        for (auto* button : { &rec, &stop, &play, &more })
            button->setBounds (buttons.removeFromLeft (w).reduced (2, 0));
    }

private:
    // Botón con icono dibujado: círculo (REC, rojo encendido grabando), cuadrado (STOP), triángulo / dos barras (PLAY /
    // PAUSE, verde reproduciendo) y tres puntos (menú)
    struct TransportButton : MidiActionButton<juce::Button>
    {
        enum class Icon { rec, stop, play, more };
        TransportButton (Icon i, const juce::String& name) : MidiActionButton<juce::Button> (name), icon (i) {}
        bool active = false;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Skin::c (0xff2c333b).brighter (down ? 0.2f : highlighted ? 0.1f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);
            const auto s = juce::jmin (r.getWidth(), r.getHeight()) * 0.45f;
            const auto b = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
            const auto ink = Skin::white().withAlpha (0.85f);
            juce::Path p;
            switch (icon)
            {
                case Icon::rec:
                    g.setColour (active ? Skin::c (0xffff2a1a) : Skin::c (0xffb03a30));
                    g.fillEllipse (b);
                    break;
                case Icon::stop:
                    g.setColour (ink);
                    g.fillRect (b.reduced (s * 0.06f));
                    break;
                case Icon::play:
                    g.setColour (active ? Skin::c (0xff4cd964) : ink);
                    if (active) // pausa
                    {
                        g.fillRect (b.withWidth (s * 0.34f));
                        g.fillRect (b.withLeft (b.getRight() - s * 0.34f));
                    }
                    else
                    {
                        p.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
                        g.fillPath (p);
                    }
                    break;
                case Icon::more:
                    g.setColour (ink);
                    for (int k = -1; k <= 1; ++k)
                        g.fillEllipse (juce::Rectangle<float> (s * 0.22f, s * 0.22f).withCentre (b.getCentre().translated ((float) k * s * 0.38f, 0.0f)));
                    break;
            }
        }

        Icon icon;
    };

    bool compact() const { return getHeight() < 44; }

    static juce::String clock (double seconds)
    {
        const auto tenths = juce::jmax (0, (int) (seconds * 10.0));
        return juce::String (tenths / 600).paddedLeft ('0', 2) + ":" + juce::String ((tenths / 10) % 60).paddedLeft ('0', 2) + "." + juce::String (tenths % 10);
    }

    void toggleRecord()
    {
        if (transport.isRecording())
            transport.stopRecording();
        else if (const auto error = transport.startRecording (SessionTransport::newRecordingFile(), processor.getDisplayBpm()); error.isNotEmpty())
            report ("Record", error);
        refresh();
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem ("Load Audio File...", [this] { choose (false); });
        m.addItem ("Import MIDI File...", [this] { choose (true); });
        m.addItem ("Export Take MIDI...", transport.hasTakeMidi(), false, [this] { exportMidi(); });
        m.addSeparator();
        const auto last = transport.lastRecording();
        m.addItem ("Show Last Recording", last.existsAsFile(), false, [last] { last.revealToUser(); });
        m.addItem ("Open Recordings Folder", [] { auto d = SessionTransport::recordingsFolder(); d.createDirectory(); d.startAsProcess(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more));
    }

    void choose (bool midi)
    {
        chooser = std::make_unique<juce::FileChooser> (midi ? "Import MIDI file" : "Load audio file", SessionTransport::recordingsFolder(),
                                                       midi ? "*.mid;*.midi;*.smf" : "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<TransportPanel> (this), midi] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                const auto error = midi ? safe->transport.loadMidi (fc.getResult()) : safe->transport.loadAudio (fc.getResult());
                if (error.isNotEmpty())
                    safe->report (midi ? "Import MIDI" : "Load Audio", error);
                safe->refresh();
            });
    }

    void exportMidi()
    {
        const auto last = transport.lastRecording();
        const auto start = last != juce::File() ? last.withFileExtension ("mid") : SessionTransport::recordingsFolder().getChildFile ("KoniSynth.mid");
        chooser = std::make_unique<juce::FileChooser> ("Export take MIDI", start, "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe = SafePointer<TransportPanel> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                if (const auto error = safe->transport.exportMidi (fc.getResult().withFileExtension ("mid")); error.isNotEmpty())
                    safe->report ("Export MIDI", error);
            });
    }

    void report (const juce::String& title, const juce::String& message)
    {
        if (onError) onError (title, message);
    }

    void refresh()
    {
        rec.active = transport.isRecording();
        play.active = transport.isPlaying();
        for (auto* b : { &rec, &stop, &play }) b->repaint(); // estado y marco de MIDI learn
        repaint();
    }

    void timerCallback() override
    {
        blink = transport.isRecording() && (++ticks / 5) % 2 == 1; // parpadeo a 1 Hz grabando
        refresh();
    }

    KoniSynthProcessor& processor;
    SessionTransport& transport;
    TransportButton rec { TransportButton::Icon::rec, "Record" }, stop { TransportButton::Icon::stop, "Stop" },
                    play { TransportButton::Icon::play, "Play" }, more { TransportButton::Icon::more, "Transport menu" };
    juce::Rectangle<int> timeArea, nameArea;
    bool blink = false;
    int ticks = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

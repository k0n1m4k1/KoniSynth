// Se incluye desde KoniSynth.cpp, en el mismo punto, dentro de la misma unidad de compilación; usa las declaraciones
// previas de KoniSynth.cpp (JUCE, procesador, Skin, UserRhythms). No es una cabecera independiente.
#pragma once

//==============================================================================
// Editor de ritmos de usuario de la caja de ritmos (banco de ficheros MIDI, Engines/UserRhythms.h): rejilla de carriles ×
// pasos. Clic: recorre normal -> acento -> suave -> silencio; clic derecho: silencio; arrastrar pinta el nivel del primer
// clic. Cada cambio se guarda solo en su fichero (300 ms después del último, como una caja de ritmos de hardware: lo que
// suena es lo que hay en la rejilla) y el procesador relee el banco. Los carriles que el kit elegido no tiene, atenuados.
class RhythmEditor : public juce::Component, private juce::Timer
{
public:
    explicit RhythmEditor (KoniSynthProcessor& p) : koni (p)
    {
        for (int i = 0; i < UserRhythms::numSlots; ++i)
            slotBox.addItem ("User " + juce::String (i + 1), i + 1);
        slotBox.setTooltip ("Which of the 8 user rhythms you edit. Each one is a MIDI file in the KoniSynth settings folder "
                            "(rhythms/Drum Machine/User N.mid) that you can also edit in any sequencer.");
        slotBox.onChange = [this] { loadSlot (slotBox.getSelectedId() - 1); };

        nameBox.setTooltip ("Name of the rhythm (stored in the MIDI file).");
        nameBox.onTextChange = [this] { rhythm.name = nameBox.getText(); touch(); };

        lengthBox.addItem ("16 steps (4/4)", 1);
        lengthBox.addItem ("32 steps (2 bars)", 2);
        lengthBox.addItem ("12 steps (waltz 3/4)", 3);
        lengthBox.addItem ("12 steps (shuffle)", 4);
        lengthBox.setTooltip ("Length and grid: one bar of sixteenths, two bars, a waltz bar, or a bar of triplet eighths. "
                              "Changing it keeps the hits that still fit.");
        lengthBox.onChange = [this] { setLength (lengthBox.getSelectedId()); };

        copyButton.setTooltip ("Start from one of the factory patterns (replaces the grid).");
        copyButton.onClick = [this] { showCopyMenu(); };
        clearButton.setTooltip ("Empty the grid.");
        clearButton.onClick = [this] { rhythm.clear (rhythm.numSteps, rhythm.stepsPerBeat); touch(); };
        useButton.setTooltip ("Play this rhythm (selects it in the Pattern menu) or stop it (Pattern back to Off).");
        useButton.onClick = [this] { togglePlay(); };
        folderButton.setTooltip ("Open the folder with the rhythm files.");
        folderButton.onClick = [] {
            const auto f = UserRhythms::folder (KoniSynthProcessor::drumRhythmEngine);
            f.createDirectory();
            f.startAsProcess();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &slotBox, &nameBox, &lengthBox, &copyButton, &clearButton, &useButton,
                                                                  &folderButton, &status })
            addAndMakeVisible (c);
        status.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.7f));

        // Ranura inicial: la que suena en Pattern si es de usuario; si no, User 1
        const auto factory = (int) drumPatterns().size();
        const auto current = (int) koni.apvts.getRawParameterValue ("drumPattern")->load() - 1 - factory;
        slotBox.setSelectedId (juce::jlimit (0, UserRhythms::numSlots - 1, current) + 1, juce::dontSendNotification);
        loadSlot (slotBox.getSelectedId() - 1);
        updatePlayButton();
        startTimerHz (30);
    }

    ~RhythmEditor() override { if (dirty) save(); }

    void paint (juce::Graphics& g) override
    {
        const auto kit = juce::jlimit (0, 2, (int) koni.apvts.getRawParameterValue ("drumKit")->load());
        const auto n = rhythm.numSteps;
        const auto perBar = rhythm.isWaltz() ? 12 : rhythm.stepsPerBeat == 3 ? 12 : 16;
        g.setFont (juce::FontOptions (13.0f));
        for (int s = 0; s < lanesShown; ++s)
        {
            const auto row = rowArea (s);
            const auto inKit = slotVoice (kit, (DrumSlot) s) >= 0;
            g.setColour (Skin::white().withAlpha (inKit ? 0.85f : 0.3f));
            g.drawText (laneName (s), row.withWidth (labelWidth - 8), juce::Justification::centredRight);
            const auto& lane = rhythm.lanes[(size_t) s];
            for (int i = 0; i < n; ++i)
            {
                const auto cell = cellArea (s, i).toFloat().reduced (2.0f);
                const auto c = lane.empty() ? '.' : lane[(size_t) i];
                const auto beatStart = i % rhythm.stepsPerBeat == 0;
                auto base = Skin::black().withAlpha (beatStart ? 0.55f : 0.35f);
                if (i == playing) base = Skin::c (0xff42a2c8).withAlpha (0.45f);
                g.setColour (base);
                g.fillRoundedRectangle (cell, 3.0f);
                if (c != '.')
                {
                    const auto a = c == 'X' ? 1.0f : c == 'x' ? 0.7f : 0.38f;
                    g.setColour (Skin::accent().withAlpha (a * (inKit ? 1.0f : 0.35f)));
                    g.fillRoundedRectangle (cell.reduced (c == 'o' ? 4.0f : 2.0f), 3.0f);
                    if (c == 'X')
                    {
                        g.setColour (Skin::white().withAlpha (inKit ? 0.9f : 0.3f));
                        g.drawRoundedRectangle (cell.reduced (1.0f), 3.0f, 1.5f);
                    }
                }
            }
        }
        // Números de paso y separación de compases
        g.setColour (Skin::white().withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        for (int i = 0; i < n; ++i)
            if (i % rhythm.stepsPerBeat == 0)
                g.drawText (juce::String (i + 1), cellArea (0, i).translated (0, -18).withHeight (16), juce::Justification::centred);
        if (n > perBar)
        {
            const auto x = (float) cellArea (0, perBar).getX() - 1.0f;
            g.setColour (Skin::white().withAlpha (0.4f));
            g.drawLine (x, (float) grid.getY(), x, (float) grid.getBottom(), 1.5f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (30);
        slotBox.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        nameBox.setBounds (top.removeFromLeft (220));
        top.removeFromLeft (8);
        lengthBox.setBounds (top.removeFromLeft (190));
        top.removeFromLeft (8);
        for (auto* b : { &copyButton, &clearButton, &useButton, &folderButton })
        {
            b->setBounds (top.removeFromLeft (b == &copyButton ? 120 : 100));
            top.removeFromLeft (6);
        }
        status.setBounds (r.removeFromBottom (22));
        r.removeFromTop (24); // números de paso
        grid = r;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto [lane, step] = hit (e.getPosition());
        if (lane < 0) return;
        const auto current = cell (lane, step);
        paint_ = e.mods.isPopupMenu() ? '.' : current == '.' ? 'x' : current == 'x' ? 'X' : current == 'X' ? 'o' : '.';
        setCell (lane, step, paint_);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto [lane, step] = hit (e.getPosition());
        if (lane >= 0 && cell (lane, step) != paint_)
            setCell (lane, step, paint_);
    }

private:
    static juce::String laneName (int s)
    {
        static const char* names[] = { "Bass Drum", "Snare", "Rim Shot", "Hand Clap", "Closed Hat", "Open Hat", "Cymbal", "Cowbell",
                                       "Low Tom", "High Tom", "Low Conga", "Maracas", "Claves", "Tambourine", "Guiro" };
        return names[s];
    }

    juce::Rectangle<int> rowArea (int s) const
    {
        const auto h = grid.getHeight() / lanesShown;
        return { grid.getX(), grid.getY() + s * h, grid.getWidth(), h };
    }
    juce::Rectangle<int> cellArea (int s, int i) const
    {
        const auto row = rowArea (s);
        const auto perBar = rhythm.isWaltz() || rhythm.stepsPerBeat == 3 ? 12 : 16;
        const auto gaps = rhythm.numSteps > perBar ? 1 : 0;
        const auto w = (row.getWidth() - labelWidth - gaps * 8) / juce::jmax (1, rhythm.numSteps);
        return { row.getX() + labelWidth + i * w + (i >= perBar ? 8 : 0), row.getY(), w, row.getHeight() };
    }
    std::pair<int, int> hit (juce::Point<int> p) const
    {
        for (int s = 0; s < lanesShown; ++s)
            for (int i = 0; i < rhythm.numSteps; ++i)
                if (cellArea (s, i).contains (p))
                    return { s, i };
        return { -1, -1 };
    }

    char cell (int lane, int step) const
    {
        const auto& l = rhythm.lanes[(size_t) lane];
        return l.empty() ? '.' : l[(size_t) step];
    }
    void setCell (int lane, int step, char c)
    {
        auto& l = rhythm.lanes[(size_t) lane];
        if (l.empty()) l.assign ((size_t) rhythm.numSteps, '.');
        l[(size_t) step] = c;
        touch();
    }

    void setLength (int id)
    {
        const auto steps = id == 2 ? 32 : id >= 3 ? 12 : 16;
        const auto perBeat = id == 4 ? 3 : 4;
        if (steps == rhythm.numSteps && perBeat == rhythm.stepsPerBeat) return;
        for (auto& l : rhythm.lanes)
            if (! l.empty())
                l.resize ((size_t) steps, '.'); // conserva los pasos que caben (con su posición)
        rhythm.numSteps = steps;
        rhythm.stepsPerBeat = perBeat;
        touch();
    }

    void showCopyMenu()
    {
        juce::PopupMenu m;
        const auto& list = drumPatterns();
        for (int i = 0; i < (int) list.size(); ++i)
            m.addItem (i + 1, list[(size_t) i].name);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&copyButton), [this] (int r)
        {
            if (r <= 0) return;
            const auto& p = drumPatterns()[(size_t) r - 1];
            rhythm.clear (p.numSteps, p.stepsPerBeat);
            for (const auto& [slot, lane] : p.lanes)
                rhythm.lanes[(size_t) slot] = lane;
            if (nameBox.getText().startsWith ("User ") || nameBox.getText().isEmpty())
                nameBox.setText (juce::String (p.name) + " (copy)", juce::sendNotification);
            syncLength();
            touch();
        });
    }

    // Índice de Pattern de esta ranura (0 = Off, luego los de fábrica)
    int patternIndex() const { return 1 + (int) drumPatterns().size() + slot; }
    bool isPlayingThis() const { return (int) koni.apvts.getRawParameterValue ("drumPattern")->load() == patternIndex(); }

    // Play it / Stop: el estado sale del parámetro Pattern (sobrevive a cerrar y abrir el editor y a cambiarlo fuera).
    // Stop deja Pattern en Off
    void togglePlay()
    {
        if (dirty) save();
        const auto index = isPlayingThis() ? 0 : patternIndex();
        if (auto* param = koni.apvts.getParameter ("drumPattern"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
            param->endChangeGesture();
        }
        updatePlayButton();
    }
    void updatePlayButton()
    {
        const auto text = isPlayingThis() ? "Stop" : "Play it";
        if (useButton.getButtonText() != text) useButton.setButtonText (text);
    }

    void loadSlot (int s)
    {
        if (dirty) save();
        slot = s;
        const auto file = UserRhythms::slotFile (KoniSynthProcessor::drumRhythmEngine, slot);
        juce::String error;
        if (! file.existsAsFile() || ! UserRhythms::read (file, rhythm, error))
        {
            rhythm = {};
            rhythm.name = "User " + juce::String (slot + 1);
            rhythm.clear (16, 4);
            status.setText (file.existsAsFile() ? "Could not read " + file.getFileName() + ": " + error + " (saving replaces it)"
                                                : "Empty slot: draw a rhythm and it is saved to " + file.getFullPathName(),
                            juce::dontSendNotification);
        }
        else
        {
            status.setText (file.getFullPathName() + (rhythm.ignored > 0 ? "  (" + juce::String (rhythm.ignored) + " notes outside the drum map or the length were ignored)" : juce::String()),
                            juce::dontSendNotification);
        }
        fileTime = file.getLastModificationTime();
        nameBox.setText (rhythm.name, juce::dontSendNotification);
        syncLength();
        dirty = false;
        repaint();
    }

    // El desplegable de longitud refleja la rejilla del ritmo (tras cargar, copiar o releer el fichero)
    void syncLength()
    {
        lengthBox.setSelectedId (rhythm.numSteps == 32 ? 2 : rhythm.isWaltz() ? 3 : rhythm.stepsPerBeat == 3 ? 4 : 1, juce::dontSendNotification);
    }

    void touch() { dirty = true; lastEdit = juce::Time::getMillisecondCounter(); repaint(); }

    void save()
    {
        dirty = false;
        const auto file = UserRhythms::slotFile (KoniSynthProcessor::drumRhythmEngine, slot);
        if (UserRhythms::write (rhythm, file))
        {
            status.setText ("Saved " + file.getFullPathName(), juce::dontSendNotification);
            fileTime = file.getLastModificationTime();
            koni.updateUserRhythms (true);
        }
        else
        {
            status.setText ("Could not save " + file.getFullPathName(), juce::dontSendNotification);
        }
    }

    void timerCallback() override
    {
        if (dirty && juce::Time::getMillisecondCounter() - lastEdit > 300)
            save();
        // Cambiado fuera (otro programa) y sin ediciones pendientes: se vuelve a leer
        const auto file = UserRhythms::slotFile (KoniSynthProcessor::drumRhythmEngine, slot);
        if (! dirty && file.existsAsFile() && file.getLastModificationTime() != fileTime && ++externalCheck % 15 == 0)
            loadSlot (slot);
        // Paso que suena: solo si este ritmo es el de Pattern y no hay segundo patrón
        updatePlayButton();
        const auto mine = isPlayingThis() && (int) koni.apvts.getRawParameterValue ("drumPattern2")->load() == 0;
        const auto step = koni.getDrumStep();
        const auto now = mine && step >= 0 ? (int) (step % rhythm.numSteps) : -1;
        if (now != playing) { playing = now; repaint(); }
    }

    KoniSynthProcessor& koni;
    juce::ComboBox slotBox, lengthBox;
    juce::TextEditor nameBox;
    juce::TextButton copyButton { "Copy from..." }, clearButton { "Clear" }, useButton { "Play it" }, folderButton { "Open folder" };
    juce::Label status;
    juce::Rectangle<int> grid;
    UserRhythms::Rhythm rhythm;
    int slot = 0, playing = -1, externalCheck = 0;
    bool dirty = false;
    char paint_ = 'x';
    juce::uint32 lastEdit = 0;
    juce::Time fileTime;
    static constexpr int lanesShown = 15, labelWidth = 100;
};

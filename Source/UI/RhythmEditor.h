// Se incluye desde KoniSynth.cpp, en el mismo punto, dentro de la misma unidad de compilación; usa las declaraciones
// previas de KoniSynth.cpp (JUCE, procesador, Skin, UserRhythms). No es una cabecera independiente.
#pragma once

//==============================================================================
// Editor de ritmos de usuario (banco de ficheros MIDI por motor, Engines/UserRhythms.h): rejilla de carriles × pasos para
// la caja de ritmos, el Omnichord (con el carril del bajo automático) y el VL-1 (Pock, Peek, Noise: un sonido por paso).
// Clic: recorre los valores de la celda (normal -> acento -> suave; bajo: raíz -> raíz grave -> quinta -> tercera; VL-1:
// sí / no); clic derecho: silencio; arrastrar pinta el valor del primer clic. Cada cambio se guarda solo en su fichero
// (300 ms después del último: lo que suena es lo que hay en la rejilla) y el procesador relee el banco. En la caja de
// ritmos, los carriles que el kit elegido no tiene, atenuados.
class RhythmEditor : public juce::Component, private juce::Timer
{
public:
    RhythmEditor (KoniSynthProcessor& p, UserRhythms::Target t) : koni (p), target (t), rules (UserRhythms::spec (t))
    {
        using S = DrumSlot;
        switch (target)
        {
            case UserRhythms::Target::drums:
                paramId = "drumPattern";
                factoryCount = (int) drumPatterns().size();
                for (int s = 0; s < (int) S::Bass; ++s) lanes.push_back ({ (S) s, drumLaneName (s) });
                break;
            case UserRhythms::Target::omnichord:
                paramId = "omniRhythm";
                factoryCount = (int) omnichordPatterns().size();
                for (int s = 0; s < (int) S::Bass; ++s) // solo los sonidos de su caja de ritmos (la del CR-78, Omnichord.h)
                    if (slotVoice (1, (S) s) >= 0) lanes.push_back ({ (S) s, drumLaneName (s) });
                lanes.push_back ({ S::Bass, "Bass" });
                break;
            default:
                paramId = "vl1Rhythm";
                factoryCount = VL1::numPatterns;
                lanes = { { S::BD, "Pock" }, { S::RS, "Peek" }, { S::CH, "Noise" } };
                break;
        }

        for (int i = 0; i < UserRhythms::numSlots; ++i)
            slotBox.addItem ("User " + juce::String (i + 1), i + 1);
        slotBox.setTooltip ("Which of the 8 user rhythms you edit. Each one is a MIDI file in the KoniSynth settings folder (rhythms/"
                            + juce::String (rules.folder) + "/User N.mid) that you can also edit in any sequencer.");
        slotBox.onChange = [this] { loadSlot (slotBox.getSelectedId() - 1); };

        nameBox.setTooltip ("Name of the rhythm (stored in the MIDI file).");
        nameBox.onTextChange = [this] { rhythm.name = nameBox.getText(); touch(); };

        lengthBox.addItem ("16 steps (4/4)", 1);
        lengthBox.addItem ("32 steps (2 bars)", 2);
        lengthBox.addItem ("12 steps (waltz 3/4)", 3);
        if (rules.triplets)
            lengthBox.addItem ("12 steps (shuffle)", 4);
        lengthBox.setTooltip ("Length and grid: one bar of sixteenths, two bars, a waltz bar"
                              + juce::String (rules.triplets ? ", or a bar of triplet eighths" : "") + ". Changing it keeps the hits that still fit.");
        lengthBox.onChange = [this] { setLength (lengthBox.getSelectedId()); };

        copyButton.setTooltip ("Start from one of the factory rhythms (replaces the grid).");
        copyButton.onClick = [this] { showCopyMenu(); };
        clearButton.setTooltip ("Empty the grid.");
        clearButton.onClick = [this] { rhythm.clear (rhythm.numSteps, rhythm.stepsPerBeat); touch(); };
        useButton.setTooltip ("Play this rhythm (selects it in the rhythm menu) or stop it (rhythm back to Off).");
        useButton.onClick = [this] { togglePlay(); };
        folderButton.setTooltip ("Open the folder with the rhythm files.");
        folderButton.onClick = [this] {
            const auto f = UserRhythms::folder (target);
            f.createDirectory();
            f.startAsProcess();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &slotBox, &nameBox, &lengthBox, &copyButton, &clearButton, &useButton,
                                                                  &folderButton, &status })
            addAndMakeVisible (c);
        status.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.7f));

        // Ranura inicial: la que suena si es de usuario; si no, User 1
        const auto current = (int) koni.apvts.getRawParameterValue (paramId)->load() - 1 - factoryCount;
        slotBox.setSelectedId (juce::jlimit (0, UserRhythms::numSlots - 1, current) + 1, juce::dontSendNotification);
        loadSlot (slotBox.getSelectedId() - 1);
        updatePlayButton();
        startTimerHz (30);
    }

    ~RhythmEditor() override { if (dirty) save(); }

    void paint (juce::Graphics& g) override
    {
        const auto n = rhythm.numSteps;
        const auto perBar = barSteps();
        for (int k = 0; k < (int) lanes.size(); ++k)
        {
            const auto row = rowArea (k);
            const auto laneSlot = lanes[(size_t) k].slot;
            const auto playable = inKit (laneSlot);
            g.setFont (juce::FontOptions (13.0f));
            g.setColour (Skin::white().withAlpha (playable ? 0.85f : 0.3f));
            g.drawText (lanes[(size_t) k].name, row.withWidth (labelWidth - 8), juce::Justification::centredRight);
            const auto& lane = rhythm.lanes[(size_t) laneSlot];
            for (int i = 0; i < n; ++i)
            {
                const auto cellBox = cellArea (k, i).toFloat().reduced (2.0f);
                const auto c = lane.empty() ? '.' : lane[(size_t) i];
                const auto beatStart = i % rhythm.stepsPerBeat == 0;
                auto base = Skin::black().withAlpha (beatStart ? 0.55f : 0.35f);
                if (i == playing) base = Skin::c (0xff42a2c8).withAlpha (0.45f);
                g.setColour (base);
                g.fillRoundedRectangle (cellBox, 3.0f);
                if (c == '.')
                    continue;
                if (laneSlot == DrumSlot::Bass)
                {
                    // Bajo: nota relativa al acorde pulsado
                    g.setColour (Skin::accent().withAlpha (0.75f));
                    g.fillRoundedRectangle (cellBox.reduced (2.0f), 3.0f);
                    g.setColour (Skin::black());
                    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                    g.drawText (c == 'r' ? "Root" : c == 'R' ? "Low" : c == 'f' ? "5th" : "3rd", cellBox, juce::Justification::centred, false);
                    continue;
                }
                const auto a = c == 'X' ? 1.0f : c == 'x' ? 0.7f : 0.38f;
                g.setColour (Skin::accent().withAlpha (a * (playable ? 1.0f : 0.35f)));
                g.fillRoundedRectangle (cellBox.reduced (c == 'o' ? 4.0f : 2.0f), 3.0f);
                if (c == 'X')
                {
                    g.setColour (Skin::white().withAlpha (playable ? 0.9f : 0.3f));
                    g.drawRoundedRectangle (cellBox.reduced (1.0f), 3.0f, 1.5f);
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
            g.drawLine (x, (float) grid.getY(), x, (float) rowArea ((int) lanes.size() - 1).getBottom(), 1.5f);
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
        paintValue = e.mods.isPopupMenu() ? '.' : next (lanes[(size_t) lane].slot, cell (lane, step));
        setCell (lane, step, paintValue);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto [lane, step] = hit (e.getPosition());
        if (lane >= 0 && cell (lane, step) != paintValue)
            setCell (lane, step, paintValue);
    }

private:
    struct Lane { DrumSlot slot; juce::String name; };

    static juce::String drumLaneName (int s)
    {
        static const char* names[] = { "Bass Drum", "Snare", "Rim Shot", "Hand Clap", "Closed Hat", "Open Hat", "Cymbal", "Cowbell",
                                       "Low Tom", "High Tom", "Low Conga", "Maracas", "Claves", "Tambourine", "Guiro" };
        return names[s];
    }

    // Valor siguiente de una celda al hacer clic
    char next (DrumSlot laneSlot, char c) const
    {
        if (laneSlot == DrumSlot::Bass) return c == '.' ? 'r' : c == 'r' ? 'R' : c == 'R' ? 'f' : c == 'f' ? 't' : '.';
        if (! rules.levels)         return c == '.' ? 'x' : '.';
        return c == '.' ? 'x' : c == 'x' ? 'X' : c == 'X' ? 'o' : '.';
    }

    // ¿Suena el carril con el kit elegido? Solo la caja de ritmos cambia de kit
    bool inKit (DrumSlot laneSlot) const
    {
        if (target != UserRhythms::Target::drums) return true;
        return slotVoice (kitFromParam (koni.apvts.getRawParameterValue ("drumKit")->load()), laneSlot) >= 0;
    }

    int barSteps() const { return rhythm.isWaltz() || rhythm.stepsPerBeat == 3 ? 12 : 16; }

    juce::Rectangle<int> rowArea (int k) const
    {
        const auto h = juce::jmin (44, grid.getHeight() / juce::jmax (1, (int) lanes.size())); // pocos carriles: filas de 44 px
        return { grid.getX(), grid.getY() + k * h, grid.getWidth(), h };
    }
    juce::Rectangle<int> cellArea (int k, int i) const
    {
        const auto row = rowArea (k);
        const auto perBar = barSteps();
        const auto gaps = rhythm.numSteps > perBar ? 1 : 0;
        const auto w = (row.getWidth() - labelWidth - gaps * 8) / juce::jmax (1, rhythm.numSteps);
        return { row.getX() + labelWidth + i * w + (i >= perBar ? 8 : 0), row.getY(), w, row.getHeight() };
    }
    std::pair<int, int> hit (juce::Point<int> p) const
    {
        for (int k = 0; k < (int) lanes.size(); ++k)
            for (int i = 0; i < rhythm.numSteps; ++i)
                if (cellArea (k, i).contains (p))
                    return { k, i };
        return { -1, -1 };
    }

    char cell (int k, int step) const
    {
        const auto& l = rhythm.lanes[(size_t) lanes[(size_t) k].slot];
        return l.empty() ? '.' : l[(size_t) step];
    }
    void setCell (int k, int step, char c)
    {
        const auto laneSlot = lanes[(size_t) k].slot;
        if (rules.onePerStep && c != '.') // VL-1: un sonido por paso, el nuevo sustituye al que hubiera
            for (const auto& other : lanes)
                if (other.slot != laneSlot && ! rhythm.lanes[(size_t) other.slot].empty())
                    rhythm.lanes[(size_t) other.slot][(size_t) step] = '.';
        auto& l = rhythm.lanes[(size_t) laneSlot];
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

    // Copia de un ritmo de fábrica (nombres del propio parámetro)
    void showCopyMenu()
    {
        juce::PopupMenu m;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (koni.apvts.getParameter (paramId)))
            for (int i = 0; i < factoryCount; ++i)
                m.addItem (i + 1, choice->choices[i + 1]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&copyButton), [this] (int r)
        {
            if (r <= 0) return;
            const auto index = r - 1;
            juce::String name;
            if (target == UserRhythms::Target::vl1)
            {
                if (! VL1::factoryGrid (index, rhythm)) return;
            }
            else
            {
                const auto& p = (target == UserRhythms::Target::drums ? drumPatterns() : omnichordPatterns())[(size_t) index];
                rhythm.clear (p.numSteps, p.stepsPerBeat);
                for (const auto& [slot, lane] : p.lanes)
                    rhythm.lanes[(size_t) slot] = lane;
            }
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (koni.apvts.getParameter (paramId)))
                name = choice->choices[index + 1];
            if (nameBox.getText().startsWith ("User ") || nameBox.getText().isEmpty())
                nameBox.setText (name + " (copy)", juce::sendNotification);
            syncLength();
            touch();
        });
    }

    // Índice del parámetro de ritmo de esta ranura (0 = Off, luego los de fábrica)
    int patternIndex() const { return 1 + factoryCount + slot; }
    bool isPlayingThis() const { return (int) koni.apvts.getRawParameterValue (paramId)->load() == patternIndex(); }

    // Play it / Stop: el estado sale del parámetro de ritmo (sobrevive a cerrar y abrir el editor y a cambiarlo fuera).
    // Stop lo deja en Off
    void togglePlay()
    {
        if (dirty) save();
        const auto index = isPlayingThis() ? 0 : patternIndex();
        if (auto* param = koni.apvts.getParameter (paramId))
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
        const auto file = UserRhythms::slotFile (target, slot);
        juce::String error;
        if (! file.existsAsFile() || ! UserRhythms::read (file, rules, rhythm, error))
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
            status.setText (file.getFullPathName() + (rhythm.ignored > 0 ? "  (" + juce::String (rhythm.ignored) + " notes this engine cannot play were ignored)" : juce::String()),
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
        const auto file = UserRhythms::slotFile (target, slot);
        if (UserRhythms::write (rhythm, file))
        {
            status.setText ("Saved " + file.getFullPathName(), juce::dontSendNotification);
            fileTime = file.getLastModificationTime();
            koni.updateUserRhythms (target, true);
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
        const auto file = UserRhythms::slotFile (target, slot);
        if (! dirty && file.existsAsFile() && file.getLastModificationTime() != fileTime && ++externalCheck % 15 == 0)
            loadSlot (slot);
        // Paso que suena: solo si este ritmo es el elegido (en la caja de ritmos, además, sin segundo patrón)
        updatePlayButton();
        const auto mine = isPlayingThis() && (target != UserRhythms::Target::drums
                                              || (int) koni.apvts.getRawParameterValue ("drumPattern2")->load() == 0);
        const auto step = koni.getRhythmStep (target);
        const auto now = mine && step >= 0 ? (int) (step % rhythm.numSteps) : -1;
        if (now != playing) { playing = now; repaint(); }
    }

    KoniSynthProcessor& koni;
    const UserRhythms::Target target;
    const UserRhythms::Spec& rules;
    const char* paramId = "drumPattern";
    int factoryCount = 0;
    std::vector<Lane> lanes;
    juce::ComboBox slotBox, lengthBox;
    juce::TextEditor nameBox;
    juce::TextButton copyButton { "Copy from..." }, clearButton { "Clear" }, useButton { "Play it" }, folderButton { "Open folder" };
    juce::Label status;
    juce::Rectangle<int> grid;
    UserRhythms::Rhythm rhythm;
    int slot = 0, playing = -1, externalCheck = 0;
    bool dirty = false;
    char paintValue = 'x';
    juce::uint32 lastEdit = 0;
    juce::Time fileTime;
    static constexpr int labelWidth = 100;
};

// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Rejilla del secuenciador (vista principal), con el lenguaje del editor de ritmos: 16 pasos agrupados por pulsos (según
// Rate: 1/16 = 4 pasos por pulso, 1/8T = 3...), el paso que suena resaltado y los de más allá de Steps atenuados.
// Filas: intervalo (semitonos desde la tecla pulsada: +7 = una quinta sobre ella, no una nota fija; "—" = silencio), la
// nota que daría con una raíz de referencia (solo de vista, se guarda con la instancia) y la articulación (Normal, Tie =
// ligado al siguiente sin nuevo ataque, — silencio). Encima, el contorno melódico.
// Edición: arrastre vertical o rueda en un paso = ±1 st (Shift: ±12); doble clic = 0; clic en la articulación = Normal ->
// Tie -> Silencio; clic derecho = silencio sí/no. Teclado: flechas (Shift amplía la selección), Arriba/Abajo ±1 (Shift
// ±12), un número con signo y Intro, R = silencio, T = ligadura, Supr = silencio, Ctrl+A / Ctrl+C / Ctrl+V. Los mismos
// parámetros que los sliders (stepNote/stepOn/stepTie): presets y automatización no cambian.
class SeqGrid : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit SeqGrid (KoniSynthProcessor& p) : koni (p)
    {
        setWantsKeyboardFocus (true);
        setTooltip ("Step grid. Drag up/down or use the mouse wheel on a step: interval (Shift: octaves). Click the articulation row: "
                    "Normal -> Tie -> Rest. Right-click: rest. Keys: arrows, a signed number + Enter, R rest, T tie, Ctrl+C / Ctrl+V.");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto steps = stepsUsed();
        const auto font = juce::FontOptions (13.0f), bold = juce::FontOptions (13.0f, juce::Font::bold);
        // Etiquetas de fila
        g.setFont (bold);
        g.setColour (Skin::white().withAlpha (0.7f));
        static const char* rowNames[] = { "Step", "Interval", "", "Articulation" };
        for (int row = 0; row < 4; ++row)
        {
            auto b = rowArea (row).withWidth (labelWidth - 8);
            g.drawText (row == 2 ? "Note (root " + noteName (root()) + ")" : juce::String (rowNames[row]), b, juce::Justification::centredLeft);
        }
        // Contorno: intervalos de -24 a +24; los silencios cortan la línea; la ligadura une con el siguiente
        const auto cont = contourArea().toFloat();
        g.setColour (Skin::black().withAlpha (0.3f));
        g.fillRoundedRectangle (cont, 4.0f);
        g.setColour (Skin::white().withAlpha (0.12f));
        g.drawHorizontalLine (juce::roundToInt (yForInterval (0)), cont.getX(), cont.getRight());
        juce::Path line;
        bool open = false;
        for (int i = 0; i < 16; ++i)
        {
            const auto x = (float) column (i).getCentreX();
            if (! on (i)) { open = false; continue; }
            const juce::Point<float> pt (x, yForInterval (note (i)));
            if (open) line.lineTo (pt); else line.startNewSubPath (pt);
            open = true;
            g.setColour ((i < steps ? Skin::c (0xffffb000) : Skin::white().withAlpha (0.3f)));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));
        }
        g.setColour (Skin::c (0xffffb000).withAlpha (0.7f));
        g.strokePath (line, juce::PathStrokeType (1.5f));

        // Celdas
        const auto perBeat = stepsPerBeat();
        for (int i = 0; i < 16; ++i)
        {
            const auto active = i < steps;
            const auto beatStart = i % perBeat == 0;
            for (int row = 0; row < 4; ++row)
            {
                const auto cell = cellArea (i, row).toFloat().reduced (1.5f);
                auto base = Skin::black().withAlpha (beatStart ? 0.55f : 0.35f);
                if (i == playing && active) base = Skin::c (0xff42a2c8).withAlpha (0.45f);
                g.setColour (base);
                g.fillRoundedRectangle (cell, 3.0f);
                if (! active)
                {
                    g.setColour (Skin::black().withAlpha (0.35f));
                    g.fillRoundedRectangle (cell, 3.0f);
                }
                if (selected (i))
                {
                    g.setColour (Skin::c (0xffffb000).withAlpha (hasKeyboardFocus (true) ? 0.9f : 0.5f));
                    g.drawRoundedRectangle (cell, 3.0f, 1.5f);
                }
                juce::String text;
                switch (row)
                {
                    case 0:  text = juce::String (i + 1).paddedLeft ('0', 2); break;
                    case 1:  text = on (i) ? signedText (note (i)) : juce::String::fromUTF8 ("\xe2\x80\x94"); break;
                    case 2:  text = on (i) ? noteName (root() + note (i)) : juce::String ("Rest"); break;
                    default: text = ! on (i) ? juce::String::fromUTF8 ("\xe2\x80\x94") : tie (i) ? juce::String::fromUTF8 ("Tie \xe2\x86\x92") : juce::String ("Normal"); break;
                }
                g.setFont (row == 1 ? bold : font);
                g.setColour (Skin::white().withAlpha (active ? (row == 0 ? 0.55f : 0.95f) : 0.35f));
                g.drawText (text, cell, juce::Justification::centred);
            }
        }
        if (typed.isNotEmpty())
        {
            g.setColour (Skin::c (0xffffb000));
            g.setFont (bold);
            g.drawText ("Enter: " + typed + " st", rowArea (0).withWidth (labelWidth - 8).translated (0, -18), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto i = stepAt (e.position.toInt());
        if (i < 0) return;
        const auto row = rowAt (e.position.toInt());
        if (e.mods.isShiftDown()) selEnd = i; else selStart = selEnd = i;
        if (e.mods.isPopupMenu()) { setOn (i, ! on (i)); repaint(); return; }
        if (row == 3) // articulación: Normal -> Tie -> Silencio -> Normal
        {
            if (! on (i)) { setOn (i, true); setTie (i, false); }
            else if (! tie (i)) setTie (i, true);
            else { setTie (i, false); setOn (i, false); }
        }
        dragStep = (row == 1 || row == 2 || row == -1) && on (i) ? i : -1;
        dragStartY = e.position.y;
        dragStartValue = dragStep >= 0 ? note (dragStep) : 0;
        if (dragStep >= 0) param ("stepNote", dragStep)->beginChangeGesture();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStep < 0) return;
        const auto deltaSteps = juce::roundToInt ((dragStartY - e.position.y) / 6.0f) * (e.mods.isShiftDown() ? 12 : 1);
        setReal (*param ("stepNote", dragStep), (float) juce::jlimit (-24, 24, dragStartValue + deltaSteps));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragStep >= 0) param ("stepNote", dragStep)->endChangeGesture();
        dragStep = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto i = stepAt (e.position.toInt());
        if (i >= 0 && rowAt (e.position.toInt()) != 3) setNote (i, 0);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto i = stepAt (e.position.toInt());
        if (i < 0 || w.deltaY == 0.0f || ! on (i)) return;
        setNote (i, juce::jlimit (-24, 24, note (i) + (w.deltaY > 0.0f ? 1 : -1) * (e.mods.isShiftDown() ? 12 : 1)));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto code = k.getKeyCode();
        const auto shift = k.getModifiers().isShiftDown(), cmd = k.getModifiers().isCommandDown();
        const auto ch = k.getTextCharacter();
        if (cmd && (ch == 'a' || ch == 'A')) { selStart = 0; selEnd = 15; repaint(); return true; }
        if (cmd && (ch == 'c' || ch == 'C')) { copy(); return true; }
        if (cmd && (ch == 'v' || ch == 'V')) { paste(); return true; }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            const auto d = code == juce::KeyPress::leftKey ? -1 : 1;
            selEnd = juce::jlimit (0, 15, selEnd + d);
            if (! shift) selStart = selEnd;
            typed.clear();
            repaint();
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            const auto d = (code == juce::KeyPress::upKey ? 1 : -1) * (shift ? 12 : 1);
            forSelection ([this, d] (int i) { if (on (i)) setNote (i, juce::jlimit (-24, 24, note (i) + d)); });
            return true;
        }
        if (code == juce::KeyPress::returnKey && typed.isNotEmpty())
        {
            const auto v = juce::jlimit (-24, 24, typed.getIntValue());
            forSelection ([this, v] (int i) { setOn (i, true); setNote (i, v); });
            typed.clear();
            selStart = selEnd = juce::jmin (15, juce::jmax (selStart, selEnd) + 1); // siguiente paso, como en una hoja de cálculo
            repaint();
            return true;
        }
        if (code == juce::KeyPress::escapeKey && typed.isNotEmpty()) { typed.clear(); repaint(); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            if (typed.isNotEmpty()) typed = typed.dropLastCharacters (1);
            else forSelection ([this] (int i) { setOn (i, false); });
            repaint();
            return true;
        }
        if ((ch >= '0' && ch <= '9') || ((ch == '-' || ch == '+') && typed.isEmpty()))
        {
            if (typed.length() < 3) typed << juce::String::charToString (ch);
            repaint();
            return true;
        }
        if (ch == 'r' || ch == 'R' || ch == ' ') { forSelection ([this] (int i) { setOn (i, ! on (i)); }); return true; }
        if (ch == 't' || ch == 'T' || ch == 'l' || ch == 'L') { forSelection ([this] (int i) { if (on (i)) setTie (i, ! tie (i)); }); return true; }
        return false;
    }

    void focusLost (FocusChangeType) override { repaint(); }

    void resized() override {}

    static constexpr int labelWidth = 150;

private:
    struct StepValue { int note; bool on, tie; };

    // Geometría: contorno arriba (70 px); 4 filas de celdas; columna de etiquetas a la izquierda; separación de 6 px entre pulsos
    juce::Rectangle<int> contourArea() const { return { labelWidth, 0, getWidth() - labelWidth, 70 }; }
    juce::Rectangle<int> rowArea (int row) const
    {
        const auto top = 80, h = (getHeight() - top) / 4;
        return { 0, top + row * h, getWidth(), h };
    }
    juce::Rectangle<int> column (int i) const
    {
        const auto perBeat = stepsPerBeat();
        const auto groups = (16 + perBeat - 1) / perBeat;
        const auto gap = 6;
        const auto w = (float) (getWidth() - labelWidth - gap * (groups - 1)) / 16.0f;
        const auto x = (float) labelWidth + w * (float) i + (float) (gap * (i / perBeat));
        return { juce::roundToInt (x), 0, juce::roundToInt (w), getHeight() };
    }
    juce::Rectangle<int> cellArea (int i, int row) const { const auto r = rowArea (row); return column (i).withY (r.getY()).withHeight (r.getHeight()); }
    int stepAt (juce::Point<int> p) const
    {
        for (int i = 0; i < 16; ++i)
            if (p.x >= column (i).getX() && p.x < column (i).getRight() + 6) return i;
        return -1;
    }
    int rowAt (juce::Point<int> p) const
    {
        for (int row = 0; row < 4; ++row) if (rowArea (row).contains (p)) return row;
        return -1; // contorno: se edita como el intervalo
    }
    float yForInterval (int st) const
    {
        const auto r = contourArea().toFloat().reduced (0.0f, 6.0f);
        return r.getCentreY() - (float) st / 24.0f * r.getHeight() * 0.5f;
    }

    // Parámetros
    juce::RangedAudioParameter* param (const char* base, int i) const { return koni.apvts.getParameter (base + juce::String (i + 1)); }
    float raw (const char* base, int i) const { return koni.apvts.getRawParameterValue (base + juce::String (i + 1))->load(); }
    int note (int i) const { return (int) raw ("stepNote", i); }
    bool on (int i) const { return raw ("stepOn", i) > 0.5f; }
    bool tie (int i) const { return raw ("stepTie", i) > 0.5f; }
    static void setReal (juce::RangedAudioParameter& p, float v) { p.setValueNotifyingHost (p.convertTo0to1 (v)); }
    void set (const char* base, int i, float v)
    {
        auto* p = param (base, i);
        p->beginChangeGesture();
        setReal (*p, v);
        p->endChangeGesture();
    }
    void setNote (int i, int v) { set ("stepNote", i, (float) v); }
    void setOn (int i, bool v) { set ("stepOn", i, v ? 1.0f : 0.0f); }
    void setTie (int i, bool v) { set ("stepTie", i, v ? 1.0f : 0.0f); }

    int stepsUsed() const { return (int) koni.apvts.getRawParameterValue ("seqSteps")->load(); }
    int stepsPerBeat() const
    {
        static constexpr int perBeat[6] = { 1, 2, 3, 4, 6, 8 }; // 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32
        return perBeat[juce::jlimit (0, 5, (int) koni.apvts.getRawParameterValue ("seqRate")->load())];
    }
    int root() const { return (int) koni.apvts.state.getProperty ("seqViewRoot", 48); }
    bool selected (int i) const { return i >= juce::jmin (selStart, selEnd) && i <= juce::jmax (selStart, selEnd); }
    template <typename F> void forSelection (F&& f)
    {
        for (int i = juce::jmin (selStart, selEnd); i <= juce::jmax (selStart, selEnd); ++i) f (i);
        repaint();
    }

    void copy()
    {
        clipboard.clear();
        forSelection ([this] (int i) { clipboard.push_back ({ note (i), on (i), tie (i) }); });
    }
    void paste()
    {
        const auto first = juce::jmin (selStart, selEnd);
        for (size_t k = 0; k < clipboard.size() && first + (int) k < 16; ++k)
        {
            const auto i = first + (int) k;
            setNote (i, clipboard[k].note);
            setOn (i, clipboard[k].on);
            setTie (i, clipboard[k].tie);
        }
        repaint();
    }

    static juce::String signedText (int v) { return v > 0 ? "+" + juce::String (v) : juce::String (v); }

public:
    static juce::String noteName (int n)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        n = juce::jlimit (0, 127, n);
        return juce::String (names[n % 12]) + juce::String (n / 12 - 1); // C4 = 60
    }

private:
    void timerCallback() override
    {
        // Repinta si cambia el paso que suena o cualquier valor (preset, automatización, la vista de sliders)
        double h = koni.getSequencerStep() * 7.0 + stepsUsed() * 131.0 + stepsPerBeat() * 977.0 + root();
        for (int i = 0; i < 16; ++i) h = h * 1.0001 + note (i) * 3.0 + (on (i) ? 1.0 : 0.0) + (tie (i) ? 2.0 : 0.0);
        if (h != lastHash) { lastHash = h; playing = koni.getSequencerStep(); repaint(); }
    }

    KoniSynthProcessor& koni;
    int selStart = 0, selEnd = 0, playing = -1, dragStep = -1, dragStartValue = 0;
    float dragStartY = 0.0f;
    juce::String typed;
    std::vector<StepValue> clipboard;
    double lastHash = -1.0;
};

//==============================================================================
// UI: secuenciador de 16 pasos. Vista principal: la rejilla (SeqGrid). Vista "Sliders" (opcional, la analógica): por
// columna, slider de tono (±24 st), LED de paso activo y LED de Tie. La vista elegida y la raíz de referencia de la rejilla
// se guardan con la instancia (propiedades de estado seqView y seqViewRoot, no parámetros).
class SequencerPanel : public juce::Component, private juce::Timer
{
public:
    explicit SequencerPanel (KoniSynthProcessor& p) : koni (p), grid (p)
    {
        auto& s = p.apvts;
        header.withToggle (s, "seqOn").withCombo (s, "seqRate").withExtraToggle (s, "seqLatch", "Latch")
              .withKnob (s, "seqSteps", "Steps").withKnob (s, "seqGate", "Gate").withKnob (s, "arpBpm", "BPM");
        addAndMakeVisible (header);
        for (int i = 0; i < 16; ++i)
        {
            auto& c = columns[(size_t) i];
            const auto n = juce::String (i + 1);
            c.note = std::make_unique<Knob> (s, "stepNote" + n, n, Knob::Style::vertical);
            c.on.setButtonText ("On");
            c.tie.setButtonText ("Tie");
            c.onAttachment = std::make_unique<APVTS::ButtonAttachment> (s, "stepOn" + n, c.on);
            c.tieAttachment = std::make_unique<APVTS::ButtonAttachment> (s, "stepTie" + n, c.tie);
            c.on.setTooltip (paramTooltip (s, "stepOn" + n));
            c.tie.setTooltip (paramTooltip (s, "stepTie" + n));
            c.onMenu = std::make_unique<ParamMenu> (s, "stepOn" + n, c.on);
            c.tieMenu = std::make_unique<ParamMenu> (s, "stepTie" + n, c.tie);
            addChildComponent (*c.note);
            addChildComponent (c.on);
            addChildComponent (c.tie);
        }
        addChildComponent (grid);
        // Vista: Grid / Sliders
        for (auto* b : { &gridButton, &slidersButton })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (0x5e9);
            addAndMakeVisible (*b);
        }
        gridButton.setTooltip ("Grid: intervals, notes and articulation as a table (main editor).");
        slidersButton.setTooltip ("Sliders: one pitch slider per step (analog-style view, for drawing contours).");
        gridButton.onClick = [this] { setView (0); };
        slidersButton.onClick = [this] { setView (1); };
        // Raíz de referencia de la rejilla (solo de vista: la nota real es la tecla que se pulsa)
        for (int n = 24; n <= 84; ++n) rootBox.addItem (SeqGrid::noteName (n), n + 1);
        rootBox.setSelectedId ((int) p.apvts.state.getProperty ("seqViewRoot", 48) + 1, juce::dontSendNotification);
        rootBox.setTooltip ("Reference root for the Note row (view only: the sequence always follows the key you hold).");
        rootBox.onChange = [this] { koni.apvts.state.setProperty ("seqViewRoot", rootBox.getSelectedId() - 1, nullptr); grid.repaint(); };
        addAndMakeVisible (rootBox);
        rootLabel.setText ("Root", juce::dontSendNotification);
        rootLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (rootLabel);
        setView ((int) p.apvts.state.getProperty ("seqView", 0));
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        if (view != 1)
            return;
        const auto steps = (int) koni.apvts.getRawParameterValue ("seqSteps")->load();
        for (int i = 0; i < 16; ++i)
        {
            const auto b = columnArea (i).toFloat();
            const auto active = i < steps;
            g.setColour (i == playing ? Skin::c (0xff42a2c8).withAlpha (0.35f)
                                      : Skin::black().withAlpha (active ? 0.25f : 0.55f));
            g.fillRoundedRectangle (b, 4.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header.setBounds (r.removeFromTop (140));
        auto bar = r.removeFromTop (30).reduced (0, 3);
        slidersButton.setBounds (bar.removeFromRight (90));
        gridButton.setBounds (bar.removeFromRight (70));
        bar.removeFromRight (12);
        rootBox.setBounds (bar.removeFromRight (80));
        rootLabel.setBounds (bar.removeFromRight (50));
        r.removeFromTop (4);
        area = r;
        grid.setBounds (r);
        for (int i = 0; i < 16; ++i)
        {
            auto b = columnArea (i).reduced (3);
            auto& c = columns[(size_t) i];
            c.tie.setBounds (b.removeFromBottom (24));
            c.on.setBounds (b.removeFromBottom (24));
            c.note->setBounds (b);
        }
    }

private:
    void setView (int v)
    {
        view = juce::jlimit (0, 1, v);
        koni.apvts.state.setProperty ("seqView", view, nullptr);
        (view == 0 ? gridButton : slidersButton).setToggleState (true, juce::dontSendNotification);
        grid.setVisible (view == 0);
        rootBox.setVisible (view == 0);
        rootLabel.setVisible (view == 0);
        for (auto& c : columns)
        {
            c.note->setVisible (view == 1);
            c.on.setVisible (view == 1);
            c.tie.setVisible (view == 1);
        }
        if (view == 0) grid.grabKeyboardFocus();
        repaint();
    }

    juce::Rectangle<int> columnArea (int i) const
    {
        const auto w = area.getWidth() / 16;
        return { area.getX() + i * w, area.getY(), w, area.getHeight() };
    }

    void timerCallback() override
    {
        if (view != 1) return;
        const auto step = koni.getSequencerStep();
        const auto steps = (int) koni.apvts.getRawParameterValue ("seqSteps")->load();
        if (step != playing || steps != lastSteps)
        {
            playing = step;
            lastSteps = steps;
            for (int i = 0; i < 16; ++i)
                columns[(size_t) i].note->setAlpha (i < steps ? 1.0f : 0.45f);
            repaint();
        }
    }

    struct Column
    {
        std::unique_ptr<Knob> note;
        juce::ToggleButton on, tie;
        std::unique_ptr<APVTS::ButtonAttachment> onAttachment, tieAttachment;
        std::unique_ptr<ParamMenu> onMenu, tieMenu;
    };

    KoniSynthProcessor& koni;
    Section header { "Step Sequencer" };
    SeqGrid grid;
    juce::TextButton gridButton { "Grid" }, slidersButton { "Sliders" };
    juce::ComboBox rootBox;
    juce::Label rootLabel;
    std::array<Column, 16> columns;
    juce::Rectangle<int> area;
    int playing = -1, lastSteps = -1, view = 0;
};

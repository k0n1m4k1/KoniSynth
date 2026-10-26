// Pie del teclado: Transpose, Key (raíz), Scale, Mode (Off / Round / Filter) y Rounding, como parámetros (preset,
// automatización, menú de parámetro con MIDI Learn) y, a la derecha, las notas de la escala. Lo aplica Midi/ScaleQuantizer.h.
// Visible siempre bajo el panel (también con el teclado plegado o flotante). Se incluye desde KoniSynth.cpp (mismo TU)
class ScaleBar : public juce::Component, private juce::Timer
{
public:
    explicit ScaleBar (APVTS& st) : s (st)
    {
        transpose.setSliderStyle (juce::Slider::IncDecButtons);
        transpose.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
        transpose.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Horizontal);
        transpose.setViewportIgnoreDragFlag (true);
        transposeAttachment = std::make_unique<APVTS::SliderAttachment> (s, "keyTranspose", transpose);
        decorate ("keyTranspose", transpose);
        addAndMakeVisible (transpose);
        addCombo (root, "scaleRoot", rootAttachment);
        addCombo (type, "scaleType", typeAttachment);
        addCombo (mode, "scaleMode", modeAttachment);
        addCombo (rounding, "scaleRound", roundAttachment);
        for (auto* l : { &transposeLabel, &rootLabel, &typeLabel, &modeLabel, &roundLabel })
        {
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (*l);
        }
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (Skin::c (0xff1b1f24));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Skin::c (0xff3a434d));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        // Notas de la escala (o "every note" con Mode Off), en su raíz
        const auto on = (int) s.getRawParameterValue ("scaleMode")->load() != ScaleQuantizer::off;
        juce::String text = "ALL NOTES";
        if (on)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const auto rootPc = (int) s.getRawParameterValue ("scaleRoot")->load();
            const auto m = ScaleQuantizer::mask ((int) s.getRawParameterValue ("scaleType")->load());
            juce::StringArray notes;
            for (int d = 0; d < 12; ++d)
                if ((m >> d) & 1u) notes.add (names[(rootPc + d) % 12]);
            text = notes.joinIntoString (" ");
        }
        g.setColour (Skin::white().withAlpha (on ? 0.85f : 0.45f));
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        g.drawText (text, notesArea.toFloat(), juce::Justification::centredRight, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 3);
        const auto put = [&r] (juce::Label& l, int labelW, juce::Component& c, int w)
        {
            l.setBounds (r.removeFromLeft (labelW));
            r.removeFromLeft (6);
            c.setBounds (r.removeFromLeft (w));
            r.removeFromLeft (14);
        };
        put (transposeLabel, 70, transpose, 96);
        put (rootLabel, 30, root, 64);
        put (typeLabel, 42, type, 150);
        put (modeLabel, 40, mode, 80);
        put (roundLabel, 66, rounding, 90);
        notesArea = r;
    }

private:
    void addCombo (juce::ComboBox& c, const char* id, std::unique_ptr<APVTS::ComboBoxAttachment>& att)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
            c.addItemList (choice->choices, 1);
        att = std::make_unique<APVTS::ComboBoxAttachment> (s, id, c);
        c.setViewportIgnoreDragFlag (true);
        decorate (id, c);
        addAndMakeVisible (c);
    }
    void decorate (const char* id, juce::Component& c)
    {
        if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (&c))
            tc->setTooltip (paramTooltip (s, id));
        menus.push_back (std::make_unique<ParamMenu> (s, id, c));
    }

    void timerCallback() override
    {
        // Rounding solo actúa en Round; las notas de la derecha siguen a raíz, escala y modo
        const auto m = (int) s.getRawParameterValue ("scaleMode")->load();
        const auto alpha = m == ScaleQuantizer::round ? 1.0f : 0.35f;
        rounding.setAlpha (alpha);
        roundLabel.setAlpha (alpha);
        const auto key = m * 10000 + (int) s.getRawParameterValue ("scaleRoot")->load() * 100 + (int) s.getRawParameterValue ("scaleType")->load();
        if (key != lastKey) { lastKey = key; repaint(); if (onChanged) onChanged(); }
    }

public:
    std::function<void()> onChanged; // raíz, escala o modo cambiados: el editor repinta las marcas del teclado
    // Máscara de 12 bits por clase de nota absoluta (bit 0 = C) y raíz; máscara 0 con Mode Off
    static std::pair<int, int> info (APVTS& s)
    {
        if ((int) s.getRawParameterValue ("scaleMode")->load() == ScaleQuantizer::off) return { 0, 0 };
        const auto rootPc = (int) s.getRawParameterValue ("scaleRoot")->load();
        const auto m = (int) ScaleQuantizer::mask ((int) s.getRawParameterValue ("scaleType")->load());
        const auto abs = ((m << rootPc) | (m >> (12 - rootPc))) & 0xfff; // rotación de los grados a la raíz
        return { abs, rootPc };
    }

private:

    APVTS& s;
    juce::Slider transpose;
    juce::ComboBox root, type, mode, rounding;
    juce::Label transposeLabel { {}, "TRANSPOSE" }, rootLabel { {}, "KEY" }, typeLabel { {}, "SCALE" }, modeLabel { {}, "MODE" },
                roundLabel { {}, "ROUNDING" };
    std::unique_ptr<APVTS::SliderAttachment> transposeAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> rootAttachment, typeAttachment, modeAttachment, roundAttachment;
    std::vector<std::unique_ptr<ParamMenu>> menus; // tras los attachments: se destruyen antes
    juce::Rectangle<int> notesArea;
    int lastKey = -1;
};

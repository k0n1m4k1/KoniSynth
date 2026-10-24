// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Editor FM: cabecera (algoritmo, feedback, ajustes de la voz, envolvente de tono y el diagrama en texto) y una columna por
// operador, OP1 a OP6
class FmPanel : public juce::Component, private juce::Timer
{
public:
    explicit FmPanel (KoniSynthProcessor& p) : koni (p)
    {
        auto& s = p.apvts;
        header.withKnob (s, "fmAlgorithm", "Algorithm").withKnob (s, "fmFeedback", "Feedback");
        addAndMakeVisible (header);
        voice.withExtraToggle (s, "fmOscSync", "Osc Sync").withKnob (s, "fmTranspose", "Transpose");
        addAndMakeVisible (voice);
        for (int k = 1; k <= 4; ++k) pitchEg.withSlider (s, "fmPR" + juce::String (k), "R" + juce::String (k));
        for (int k = 1; k <= 4; ++k) pitchEg.withSlider (s, "fmPL" + juce::String (k), "L" + juce::String (k));
        addAndMakeVisible (pitchEg);
        diagram.setJustificationType (juce::Justification::centredLeft);
        diagram.setFont (juce::FontOptions (17.0f));
        addAndMakeVisible (diagram);

        for (int i = 0; i < 6; ++i)
        {
            auto& c = columns[(size_t) i];
            const auto pre = "fm" + juce::String (i + 1);
            c.title.setText ("OP" + juce::String (i + 1), juce::dontSendNotification);
            c.title.setJustificationType (juce::Justification::centred);
            c.title.setFont (juce::FontOptions (18.0f));
            addAndMakeVisible (c.title);
            const auto add = [&] (const char* field, const char* text, Knob::Style st = Knob::Style::rotary)
            {
                c.knobs.push_back (std::make_unique<Knob> (s, pre + field, text, st));
                addAndMakeVisible (*c.knobs.back());
            };
            // Fila 1: frecuencia y nivel; 2-3: envolvente; 4: sensibilidades; 5: level scaling
            add ("Coarse", "Coarse"); add ("Fine", "Fine"); add ("Detune", "Detune"); add ("Level", "Level");
            for (int k = 1; k <= 4; ++k) add (("R" + juce::String (k)).toRawUTF8(), ("R" + juce::String (k)).toRawUTF8(), Knob::Style::vertical);
            for (int k = 1; k <= 4; ++k) add (("L" + juce::String (k)).toRawUTF8(), ("L" + juce::String (k)).toRawUTF8(), Knob::Style::vertical);
            add ("Vel", "Vel"); add ("RateScale", "Rate Sc"); add ("Ams", "AMS"); add ("Break", "Break");
            add ("LDepth", "L Depth"); add ("RDepth", "R Depth"); add ("LCurve", "L Curve"); add ("RCurve", "R Curve");
            c.fixed.setButtonText ("Fixed");
            c.fixedAttachment = std::make_unique<APVTS::ButtonAttachment> (s, pre + "Fixed", c.fixed);
            c.fixed.setTooltip (paramTooltip (s, pre + "Fixed"));
            c.fixedMenu = std::make_unique<ParamMenu> (s, pre + "Fixed", c.fixed);
            addAndMakeVisible (c.fixed);
        }
        startTimerHz (5);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < 6; ++i)
        {
            // Portadoras resaltadas: son las que se oyen con el algoritmo actual
            g.setColour (carrier[(size_t) i] ? Skin::c (0xff42a2c8).withAlpha (0.22f) : Skin::black().withAlpha (0.3f));
            g.fillRoundedRectangle (columnArea (i).toFloat(), 4.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (150);
        header.setBounds (top.removeFromLeft (220));
        voice.setBounds (top.removeFromLeft (190).withTrimmedLeft (8));
        pitchEg.setBounds (top.removeFromLeft (440).withTrimmedLeft (8));
        diagram.setBounds (top.reduced (16, 0));
        r.removeFromTop (8);
        grid = r;
        for (int i = 0; i < 6; ++i)
        {
            auto b = columnArea (i).reduced (4);
            auto& c = columns[(size_t) i];
            c.title.setBounds (b.removeFromTop (26));
            const auto rowH = (b.getHeight() - 30) / 6;
            auto row = [&] (int first, int count, int height)
            {
                auto line = b.removeFromTop (height);
                const auto w = line.getWidth() / count;
                for (int k = 0; k < count; ++k)
                    c.knobs[(size_t) (first + k)]->setBounds (line.removeFromLeft (w));
            };
            row (0, 4, rowH);                    // Coarse Fine Detune Level
            c.fixed.setBounds (b.removeFromTop (30).withSizeKeepingCentre (90, 26));
            row (4, 4, rowH + rowH / 2);         // R1..R4
            row (8, 4, rowH + rowH / 2);         // L1..L4
            row (12, 4, rowH);                   // Vel Rate Sc AMS Break
            row (16, 4, b.getHeight());          // L Depth R Depth L Curve R Curve
        }
    }

private:
    juce::Rectangle<int> columnArea (int i) const
    {
        const auto w = grid.getWidth() / 6;
        return { grid.getX() + i * w, grid.getY(), w, grid.getHeight() };
    }

    void timerCallback() override
    {
        const auto alg = juce::jlimit (1, 32, (int) koni.apvts.getRawParameterValue ("fmAlgorithm")->load());
        if (alg == shownAlgorithm)
            return;
        shownAlgorithm = alg;
        diagram.setText ("Algorithm " + juce::String (alg) + ":   " + FmAlgorithms::describe (alg - 1), juce::dontSendNotification);
        for (int k = 0; k < 6; ++k) // flags en orden OP6..OP1
            carrier[(size_t) (5 - k)] = (FmAlgorithms::flags[alg - 1][k] & 3) == 0;
        repaint();
    }

    struct Column
    {
        juce::Label title;
        std::vector<std::unique_ptr<Knob>> knobs;
        juce::ToggleButton fixed;
        std::unique_ptr<APVTS::ButtonAttachment> fixedAttachment;
        std::unique_ptr<ParamMenu> fixedMenu;
    };

    KoniSynthProcessor& koni;
    Section header { "FM 6-Op" }, voice { "Voice" }, pitchEg { "Pitch EG" };
    juce::Label diagram;
    std::array<Column, 6> columns;
    std::array<bool, 6> carrier {};
    juce::Rectangle<int> grid;
    int shownAlgorithm = -1;
};

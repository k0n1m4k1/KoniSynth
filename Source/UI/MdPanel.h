// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Panel del motor Mega Drive: los 4 operadores con sus registros nativos (columnas OP1..OP4, portadoras resaltadas)
class MdPanel : public juce::Component, private juce::Timer
{
public:
    explicit MdPanel (KoniSynthProcessor& p) : koni (p)
    {
        auto& s = p.apvts;
        header.withKnob (s, "mdAlgorithm", "Algorithm").withKnob (s, "mdFeedback", "Feedback");
        addAndMakeVisible (header);
        diagram.setJustificationType (juce::Justification::centredLeft);
        diagram.setFont (juce::FontOptions (17.0f));
        addAndMakeVisible (diagram);

        for (int i = 0; i < 4; ++i)
        {
            auto& c = columns[(size_t) i];
            const auto pre = "md" + juce::String (i + 1);
            c.title.setText ("OP" + juce::String (i + 1), juce::dontSendNotification);
            c.title.setJustificationType (juce::Justification::centred);
            c.title.setFont (juce::FontOptions (18.0f));
            addAndMakeVisible (c.title);
            const auto add = [&] (const char* field, const char* text, Knob::Style st = Knob::Style::rotary)
            {
                c.knobs.push_back (std::make_unique<Knob> (s, pre + field, text, st));
                addAndMakeVisible (*c.knobs.back());
            };
            // Fila 1: nivel y frecuencia; 2: envolvente (sliders); 3: escalado y SSG-EG
            add ("Tl", "TL"); add ("Mul", "MUL"); add ("Dt", "DT");
            add ("Ar", "AR", Knob::Style::vertical); add ("Dr", "DR", Knob::Style::vertical); add ("Sr", "SR", Knob::Style::vertical);
            add ("Sl", "SL", Knob::Style::vertical); add ("Rr", "RR", Knob::Style::vertical);
            add ("Ks", "KS"); add ("Ssg", "SSG-EG");
            c.am.setButtonText ("AM");
            c.amAttachment = std::make_unique<APVTS::ButtonAttachment> (s, pre + "Am", c.am);
            c.am.setTooltip (paramTooltip (s, pre + "Am"));
            c.amMenu = std::make_unique<ParamMenu> (s, pre + "Am", c.am);
            addAndMakeVisible (c.am);
        }
        startTimerHz (5);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (carrier[(size_t) i] ? Skin::c (0xff42a2c8).withAlpha (0.22f) : Skin::black().withAlpha (0.3f));
            g.fillRoundedRectangle (columnArea (i).toFloat(), 4.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (130);
        header.setBounds (top.removeFromLeft (260));
        diagram.setBounds (top.reduced (16, 0));
        r.removeFromTop (8);
        grid = r;
        for (int i = 0; i < 4; ++i)
        {
            auto b = columnArea (i).reduced (4);
            auto& c = columns[(size_t) i];
            c.title.setBounds (b.removeFromTop (26));
            const auto rowH = (b.getHeight() - 30) / 4;
            auto row = [&] (int first, int count, int height)
            {
                auto line = b.removeFromTop (height);
                const auto w = line.getWidth() / count;
                for (int k = 0; k < count; ++k)
                    c.knobs[(size_t) (first + k)]->setBounds (line.removeFromLeft (w));
            };
            row (0, 3, rowH);                    // TL MUL DT
            row (3, 5, rowH * 2);                // AR DR SR SL RR
            c.am.setBounds (b.removeFromTop (30).withSizeKeepingCentre (90, 26));
            row (8, 2, b.getHeight());           // KS SSG-EG
        }
    }

private:
    juce::Rectangle<int> columnArea (int i) const
    {
        const auto w = grid.getWidth() / 4;
        return { grid.getX() + i * w, grid.getY(), w, grid.getHeight() };
    }

    void timerCallback() override
    {
        const auto alg = juce::jlimit (0, 7, (int) koni.apvts.getRawParameterValue ("mdAlgorithm")->load());
        if (alg == shownAlgorithm)
            return;
        shownAlgorithm = alg;
        diagram.setText ("Algorithm " + juce::String (alg) + ":   " + MegaDriveEngine::describe (alg) + "   (feedback on OP1)",
                         juce::dontSendNotification);
        for (int k = 0; k < 4; ++k)
            carrier[(size_t) k] = ((MegaDriveEngine::carriers (alg) >> k) & 1) != 0;
        repaint();
    }

    struct Column
    {
        juce::Label title;
        std::vector<std::unique_ptr<Knob>> knobs;
        juce::ToggleButton am;
        std::unique_ptr<APVTS::ButtonAttachment> amAttachment;
        std::unique_ptr<ParamMenu> amMenu;
    };

    KoniSynthProcessor& koni;
    Section header { "Mega Drive" };
    juce::Label diagram;
    std::array<Column, 4> columns;
    std::array<bool, 4> carrier {};
    juce::Rectangle<int> grid;
    int shownAlgorithm = -1;
};

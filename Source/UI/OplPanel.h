// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

// Panel del motor OPL: los operadores con sus registros nativos (columnas OP1..OP4, portadoras resaltadas; OP3/OP4 solo en
// modo de 4 operadores del OPL3)
class OplPanel : public juce::Component, private juce::Timer
{
public:
    explicit OplPanel (KoniSynthProcessor& p) : koni (p)
    {
        auto& s = p.apvts;
        header.withKnob (s, "oplAlgorithm", "Algorithm").withKnob (s, "oplFeedback", "Feedback");
        addAndMakeVisible (header);
        diagram.setJustificationType (juce::Justification::centredLeft);
        diagram.setFont (juce::FontOptions (17.0f));
        addAndMakeVisible (diagram);
        for (int i = 0; i < 4; ++i)
        {
            auto& c = columns[(size_t) i];
            const auto pre = "opl" + juce::String (i + 1);
            c.title.setText ("OP" + juce::String (i + 1), juce::dontSendNotification);
            c.title.setJustificationType (juce::Justification::centred);
            c.title.setFont (juce::FontOptions (18.0f));
            addAndMakeVisible (c.title);
            const auto add = [&] (const char* field, const char* text, Knob::Style st = Knob::Style::rotary)
            {
                c.knobs.push_back (std::make_unique<Knob> (s, pre + field, text, st));
                addAndMakeVisible (*c.knobs.back());
            };
            // Fila 1: nivel, frecuencia, escalado; 2: envolvente (sliders); 3: forma de onda; interruptores AM VIB EGT KSR
            add ("Tl", "TL"); add ("Mult", "MULT"); add ("Ksl", "KSL");
            add ("Ar", "AR", Knob::Style::vertical); add ("Dr", "DR", Knob::Style::vertical);
            add ("Sl", "SL", Knob::Style::vertical); add ("Rr", "RR", Knob::Style::vertical);
            add ("Wave", "WAVE");
            static const char* flagNames[4][2] = { { "Am", "AM" }, { "Vib", "VIB" }, { "Egt", "EGT" }, { "Ksr", "KSR" } };
            for (size_t f = 0; f < 4; ++f)
            {
                auto& b = c.flags[f];
                b.setButtonText (flagNames[f][1]);
                c.attachments[f] = std::make_unique<APVTS::ButtonAttachment> (s, pre + flagNames[f][0], b);
                b.setTooltip (paramTooltip (s, pre + flagNames[f][0]));
                c.menus[f] = std::make_unique<ParamMenu> (s, pre + flagNames[f][0], b);
                addAndMakeVisible (b);
            }
        }
        startTimerHz (5);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto used = i < (fourOp ? 4 : 2);
            g.setColour (! used ? Skin::black().withAlpha (0.12f)
                                : carrier[(size_t) i] ? Skin::c (0xff42a2c8).withAlpha (0.22f) : Skin::black().withAlpha (0.3f));
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
            const auto rowH = (b.getHeight() - 34) / 4;
            auto row = [&] (int first, int count, int height)
            {
                auto line = b.removeFromTop (height);
                const auto w = line.getWidth() / count;
                for (int k = 0; k < count; ++k)
                    c.knobs[(size_t) (first + k)]->setBounds (line.removeFromLeft (w));
            };
            row (0, 3, rowH);          // TL MULT KSL
            row (3, 4, rowH * 2);      // AR DR SL RR
            auto flagRow = b.removeFromTop (34);
            const auto fw = flagRow.getWidth() / 4;
            for (auto& f : c.flags) f.setBounds (flagRow.removeFromLeft (fw).reduced (2, 4));
            row (7, 1, b.getHeight()); // WAVE
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
        auto& s = koni.apvts;
        const auto four = (int) s.getRawParameterValue ("oplModel")->load() == 1 && s.getRawParameterValue ("oplFourOp")->load() > 0.5f;
        auto alg = juce::jlimit (0, 3, (int) s.getRawParameterValue ("oplAlgorithm")->load());
        if (! four) alg &= 1;
        if (alg == shownAlgorithm && four == fourOp)
            return;
        shownAlgorithm = alg;
        fourOp = four;
        diagram.setText ((four ? "4-operator algorithm " : "2-operator connection ") + juce::String (alg) + ":   "
                         + OplEngine::describe (four, alg) + "   (feedback on OP1)", juce::dontSendNotification);
        const auto mask = OplEngine::carrierMask (four, alg);
        for (int k = 0; k < 4; ++k)
        {
            carrier[(size_t) k] = ((mask >> k) & 1) != 0;
            const auto used = k < (four ? 4 : 2);
            auto& c = columns[(size_t) k];
            for (auto& kn : c.knobs) kn->setEnabled (used);
            for (auto& f : c.flags) f.setEnabled (used);
        }
        repaint();
    }

    struct Column
    {
        juce::Label title;
        std::vector<std::unique_ptr<Knob>> knobs;
        std::array<juce::ToggleButton, 4> flags;
        std::array<std::unique_ptr<APVTS::ButtonAttachment>, 4> attachments;
        std::array<std::unique_ptr<ParamMenu>, 4> menus;
    };

    KoniSynthProcessor& koni;
    Section header { "OPL" };
    juce::Label diagram;
    std::array<Column, 4> columns;
    std::array<bool, 4> carrier {};
    juce::Rectangle<int> grid;
    int shownAlgorithm = -1;
    bool fourOp = false;
};

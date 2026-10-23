// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// UI: secuenciador de 16 pasos. Por columna: slider de tono (±24 st), LED de paso activo y LED de Tie. Columnas más allá de
// Steps atenuadas; el paso que suena, resaltado. Todos los controles con tooltip y menú de parámetro.
class SequencerPanel : public juce::Component, private juce::Timer
{
public:
    explicit SequencerPanel (KoniSynthProcessor& p) : koni (p)
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
            addAndMakeVisible (*c.note);
            addAndMakeVisible (c.on);
            addAndMakeVisible (c.tie);
        }
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
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
        r.removeFromTop (8);
        grid = r;
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
    juce::Rectangle<int> columnArea (int i) const
    {
        const auto w = grid.getWidth() / 16;
        return { grid.getX() + i * w, grid.getY(), w, grid.getHeight() };
    }

    void timerCallback() override
    {
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
    std::array<Column, 16> columns;
    juce::Rectangle<int> grid;
    int playing = -1, lastSteps = -1;
};

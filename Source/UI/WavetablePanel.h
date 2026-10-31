// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// UI: wavetable como pila de fotogramas en perspectiva (el actual, según Position, resaltado). which: la de la fuente
// Wavetable (KoniSynthProcessor::wtMain), la de Osc 2 (wtOsc2) o la de Osc 3 (wtOsc3)
class WavetableView : public juce::Component, private juce::Timer
{
public:
    WavetableView (KoniSynthProcessor& p, int w) : koni (p), which (w) { startTimerHz (15); }

    // Parámetro de posición de cada tabla
    static const char* positionId (int which)
    {
        return which == KoniSynthProcessor::wtOsc2 ? "osc2WtPosition" : which == KoniSynthProcessor::wtOsc3 ? "osc3WtPosition" : "wtPosition";
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (Skin::black().withAlpha (0.55f));
        g.fillRoundedRectangle (r, 6.0f);
        if (frames.empty())
        {
            g.setColour (Skin::white().withAlpha (0.4f));
            g.drawText ("Generating wavetable...", r, juce::Justification::centred);
            return;
        }
        const auto n = (int) frames.size();
        const auto area = r.reduced (16.0f);
        const auto depthX = area.getWidth() * 0.25f, depthY = area.getHeight() * 0.35f;
        const auto w = area.getWidth() - depthX, h = area.getHeight() - depthY;
        const auto position = koni.apvts.getRawParameterValue (positionId (which))->load();
        const auto current = juce::roundToInt (position * (float) (n - 1));

        auto drawFrame = [&] (int k, juce::Colour colour, float thickness)
        {
            const auto t = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
            const auto x0 = area.getX() + t * depthX, y0 = area.getBottom() - h * 0.5f - t * depthY;
            juce::Path path;
            const auto& f = frames[(size_t) k];
            for (size_t i = 0; i < f.size(); ++i)
            {
                const auto x = x0 + w * (float) i / (float) (f.size() - 1);
                const auto y = y0 - f[i] * h * 0.42f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (thickness));
        };
        for (int k = n - 1; k >= 0; --k)
            if (k != current)
                drawFrame (k, Skin::white().withAlpha (0.12f), 1.0f);
        drawFrame (current, Skin::c (0xff42a2c8), 2.2f);
        g.setColour (Skin::white().withAlpha (0.7f));
        g.drawText (koni.getWavetableName (which) + "  |  Position " + juce::String (juce::roundToInt (position * 100.0f)) + " %  (up to 32 frames drawn)",
                    r.reduced (8.0f, 4.0f), juce::Justification::topLeft);
    }

private:
    void timerCallback() override
    {
        const auto name = koni.getWavetableName (which);
        if (name != lastName || frames.empty())
        {
            lastName = name;
            frames = koni.getWavetableOverview (32, 256, which);
        }
        repaint();
    }

    KoniSynthProcessor& koni;
    int which;
    std::vector<std::vector<float>> frames;
    juce::String lastName;
};

class WavetablePanel : public juce::Component
{
public:
    // which: KoniSynthProcessor::wtMain (pestaña Wavetable), wtOsc2 (Osc 2 en modo Wavetable, nivel en Osc 2) o wtOsc3 (Osc 3,
    // nivel en Osc 3: subLevel)
    static const char* titleOf (int which)
    {
        return which == KoniSynthProcessor::wtOsc2 ? "Osc 2 Wavetable" : which == KoniSynthProcessor::wtOsc3 ? "Osc 3 Wavetable" : "Wavetable";
    }

    WavetablePanel (KoniSynthProcessor& p, int which, std::function<void()> chooseFile)
        : view (p, which), controls (titleOf (which))
    {
        auto& s = p.apvts;
        const auto osc2 = which == KoniSynthProcessor::wtOsc2, osc3 = which == KoniSynthProcessor::wtOsc3;
        controls.withCombo (s, osc2 ? "osc2WtTable" : osc3 ? "osc3WtTable" : "wtTable")
                .withHeaderButton ("Load", "Load a wavetable file: WAV with 2048 samples per frame (Serum format) or a single cycle.", std::move (chooseFile))
                .withKnob (s, osc2 ? "osc2WtLevel" : osc3 ? "osc3WtLevel" : "wtLevel", "Level").withKnob (s, WavetableView::positionId (which), "Position");
        lfoHint.setText (osc2 ? "Osc 2 plays this table with its Wave set to Wavetable. Tip: a matrix slot with destination 'Osc 2 WT Position' sweeps it."
                       : osc3 ? "Osc 3 plays this table with its Wave set to Wavetable. Tip: a matrix slot with destination 'Osc 3 WT Position' sweeps it."
                              : "Tip: patch LFO 2 to WT POS in Mod Patch to sweep the table.", juce::dontSendNotification);
        lfoHint.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
        addAndMakeVisible (view);
        addAndMakeVisible (controls);
        addAndMakeVisible (lfoHint);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        lfoHint.setBounds (r.removeFromBottom (22));
        controls.setBounds (r.removeFromBottom (140));
        r.removeFromBottom (8);
        view.setBounds (r);
    }

private:
    WavetableView view;
    Section controls;
    juce::Label lfoHint;
};

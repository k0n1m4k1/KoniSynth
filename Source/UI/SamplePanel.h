// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Gesto de parámetro desde la UI (arrastre de un marcador)
static void setParameterReal (APVTS& s, const juce::String& id, float value)
{
    if (auto* p = s.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

// Parámetros de cada sample del motor Synth: el mismo panel para la fuente Sample y para Osc 2 en modo Sample
struct SampleIds
{
    int which; // KoniSynthProcessor::sourceSample u osc2Sample
    const char *level, *root, *start, *length, *loop, *title;

    static SampleIds of (int which)
    {
        if (which == KoniSynthProcessor::osc2Sample)
            return { which, "osc2Level", "osc2SampleRoot", "osc2SampleStart", "osc2SampleLength", "osc2SampleLoop", "Osc 2 Sample" };
        return { which, "sampleLevel", "sampleRoot", "sampleStart", "sampleLength", "sampleLoop", "Sample" };
    }
};

// UI: forma de onda del sample (mín/máx por columna) con Start (arrastrable), fin de Length y zona de bucle.
class SampleWaveView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    SampleWaveView (KoniSynthProcessor& p, SampleIds i) : koni (p), ids (i)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTooltip ("Drag to move the sample Start point. Shaded area: the part that plays (Length / Loop).");
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (Skin::black().withAlpha (0.55f));
        g.fillRoundedRectangle (r, 6.0f);
        if (overview.empty() || length <= 0)
        {
            g.setColour (Skin::white().withAlpha (0.4f));
            g.drawText ("No sample loaded: press Load", r, juce::Justification::centred);
            return;
        }

        auto& s = koni.apvts;
        const auto start = s.getRawParameterValue (ids.start)->load();
        const auto lengthMs = s.getRawParameterValue (ids.length)->load();
        const auto loop = s.getRawParameterValue (ids.loop)->load() > 0.5f;
        const auto startX = r.getX() + start * r.getWidth();
        auto endFrac = 1.0f;
        if (lengthMs >= 0.5f)
            endFrac = juce::jmin (1.0f, start + (float) (0.001 * lengthMs * rate / length));
        const auto endX = r.getX() + endFrac * r.getWidth();

        g.setColour (Skin::c (0xff42a2c8).withAlpha (loop ? 0.22f : 0.12f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (startX, r.getY(), endX, r.getBottom()));

        const auto mid = r.getCentreY();
        const auto half = r.getHeight() * 0.45f;
        g.setColour (Skin::c (0xffd8e6ee));
        for (size_t c = 0; c < overview.size(); ++c)
        {
            const auto x = r.getX() + (float) c * r.getWidth() / (float) overview.size();
            g.drawVerticalLine (juce::roundToInt (x), mid - overview[c].second * half, mid - overview[c].first * half + 1.0f);
        }

        g.setColour (Skin::c (0xffff5050));
        g.fillRect (startX - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.drawText ("Start", juce::Rectangle<float> (startX + 4.0f, r.getY() + 2.0f, 60.0f, 16.0f), juce::Justification::left);
        if (lengthMs >= 0.5f || loop)
        {
            g.setColour (Skin::c (0xffffc040));
            g.fillRect (endX - 1.0f, r.getY(), 2.0f, r.getHeight());
            g.drawText (loop ? "Loop end" : "Length", juce::Rectangle<float> (endX - 70.0f, r.getY() + 2.0f, 66.0f, 16.0f), juce::Justification::right);
        }
        g.setColour (Skin::white().withAlpha (0.7f));
        g.drawText (juce::String ((double) length / rate, 2) + " s  |  " + juce::String (rate / 1000.0, 1) + " kHz",
                    r.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
    }

    void resized() override { refresh(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (auto* p = koni.apvts.getParameter (ids.start))
            p->beginChangeGesture();
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        setParameterReal (koni.apvts, ids.start, juce::jlimit (0.0f, 0.99f, (float) e.x / (float) juce::jmax (1, getWidth())));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (auto* p = koni.apvts.getParameter (ids.start))
            p->endChangeGesture();
    }

private:
    void timerCallback() override
    {
        if (const auto info = koni.getSampleInfo (ids.which); info != lastInfo)
        {
            lastInfo = info;
            refresh();
        }
        repaint();
    }

    void refresh() { overview = koni.getSampleOverview (juce::jmax (1, getWidth()), length, rate, ids.which); repaint(); }

    KoniSynthProcessor& koni;
    const SampleIds ids;
    std::vector<std::pair<float, float>> overview;
    int length = 0;
    double rate = 44100.0;
    juce::String lastInfo;
};

// Cuerpo del panel Sample: forma de onda + controles
class SamplePanel : public juce::Component, private juce::Timer
{
public:
    SamplePanel (KoniSynthProcessor& p, SampleIds i, std::function<void()> chooseFile) : koni (p), ids (i), wave (p, i), controls { i.title }
    {
        auto& s = p.apvts;
        controls.withExtraToggle (s, ids.loop, "Loop")
                .withHeaderButton ("Load", "Load an audio file (WAV, AIFF, FLAC, OGG) as a sound source.", std::move (chooseFile))
                .withHeaderButton ("Clear", "Remove the loaded sample.", [this] { koni.clearSample (ids.which); })
                .withKnob (s, ids.level, "Level").withKnob (s, ids.root, "Root")
                .withKnob (s, ids.start, "Start").withKnob (s, ids.length, "Length");
        addAndMakeVisible (wave);
        addAndMakeVisible (controls);
        timerCallback();
        startTimerHz (4);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        controls.setBounds (r.removeFromBottom (140));
        r.removeFromBottom (8);
        wave.setBounds (r);
    }

private:
    void timerCallback() override
    {
        const auto t = koni.getSampleInfo (ids.which);
        controls.setInfo (t.isEmpty() ? juce::String ("(no sample)") : t);
    }

    KoniSynthProcessor& koni;
    const SampleIds ids;
    SampleWaveView wave;
    Section controls;
};

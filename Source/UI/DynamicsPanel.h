// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, Dyn, Section, KoniSynthProcessor). No es una cabecera independiente.
#pragma once

//==============================================================================
// UI: curva de transferencia del compresor (entrada -> salida en dB, de -60 a 0 dB en ambos ejes) con el nivel actual del
// detector como punto, y medidores de reducción de ganancia del compresor y del limitador (barra + pico retenido 1 s).
// La curva sale de la misma función que el audio (Dyn::gainReductionDb), con el maquillaje sumado.
class TransferCurve : public juce::Component, private juce::Timer
{
public:
    explicit TransferCurve (KoniSynthProcessor& p) : koni (p), apvts (p.apvts) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (6.0f);
        auto meters = area.removeFromRight (90.0f);
        const auto r = area.withSizeKeepingCentre (std::min (area.getWidth(), area.getHeight()), std::min (area.getWidth(), area.getHeight()));
        g.setColour (Skin::black().withAlpha (0.35f));
        g.fillRect (r);
        g.setFont (juce::FontOptions (10.0f));
        auto px = [&r] (float db) { return r.getX() + r.getWidth() * (db + 60.0f) / 60.0f; };
        auto py = [&r] (float db) { return r.getBottom() - r.getHeight() * (db + 60.0f) / 60.0f; };
        for (float db = -60.0f; db <= 0.0f; db += 12.0f)
        {
            g.setColour (Skin::white().withAlpha (0.08f));
            g.drawVerticalLine (juce::roundToInt (px (db)), r.getY(), r.getBottom());
            g.drawHorizontalLine (juce::roundToInt (py (db)), r.getX(), r.getRight());
            g.setColour (Skin::white().withAlpha (0.45f));
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (px (db) + 2, r.getBottom() - 12, 30, 12), juce::Justification::left);
        }
        g.setColour (Skin::white().withAlpha (0.2f));
        g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY()); // 1:1
        const auto thr = raw ("compThreshold"), ratio = raw ("compRatio"), makeup = raw ("compMakeup");
        const auto knee = (int) raw ("compMode") == Compressor::opto ? std::max (raw ("compKnee"), 12.0f) : raw ("compKnee");
        juce::Path curve;
        for (int i = 0; i <= 120; ++i)
        {
            const auto x = -60.0f + 0.5f * (float) i;
            const auto y = juce::jlimit (-60.0f, 0.0f, x - Dyn::gainReductionDb (x, thr, ratio, knee) + makeup);
            if (i == 0) curve.startNewSubPath (px (x), py (y)); else curve.lineTo (px (x), py (y));
        }
        const auto on = raw ("compOn") > 0.5f;
        g.setColour (Skin::c (0xff42a2c8).withAlpha (on ? 1.0f : 0.4f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
        if (on && level > -60.0f)
        {
            const auto y = juce::jlimit (-60.0f, 0.0f, level - Dyn::gainReductionDb (level, thr, ratio, knee) + makeup);
            g.setColour (Skin::c (0xffffb000));
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ px (std::min (level, 0.0f)), py (y) }));
        }
        g.setColour (Skin::white().withAlpha (0.6f));
        g.drawText ("in (dB) ->", r.reduced (4).removeFromBottom (26), juce::Justification::bottomRight);
        meter (g, meters.removeFromLeft (45.0f).reduced (6.0f, 0.0f), "COMP", comp, compPeak, on);
        meter (g, meters.reduced (6.0f, 0.0f), "LIM", lim, limPeak, raw ("limOn") > 0.5f);
    }

private:
    // Barra de reducción de ganancia, de arriba abajo, 0-24 dB
    static void meter (juce::Graphics& g, juce::Rectangle<float> r, const char* name, float gr, float peak, bool on)
    {
        g.setColour (Skin::white().withAlpha (0.6f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (name, r.removeFromTop (14.0f), juce::Justification::centred);
        auto val = r.removeFromBottom (14.0f);
        g.setColour (Skin::black().withAlpha (0.35f));
        g.fillRect (r);
        const auto h = r.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 24.0f);
        g.setColour (Skin::c (0xffd8402a).withAlpha (on ? 1.0f : 0.3f));
        g.fillRect (r.withHeight (h));
        g.setColour (Skin::white());
        g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, peak / 24.0f)), r.getX(), r.getRight());
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("-" + juce::String (peak, 1), val, juce::Justification::centred);
    }

    float raw (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        comp = koni.compressor.meterGr.load (std::memory_order_relaxed);
        lim = koni.limiter.meterGr.load (std::memory_order_relaxed);
        level = koni.compressor.meterLevel.load (std::memory_order_relaxed);
        if (comp >= compPeak || now - compAt > 1000) { compPeak = comp; compAt = now; }
        if (lim >= limPeak || now - limAt > 1000) { limPeak = lim; limAt = now; }
        repaint();
    }

    KoniSynthProcessor& koni;
    APVTS& apvts;
    float comp = 0, lim = 0, level = -120.0f, compPeak = 0, limPeak = 0;
    juce::uint32 compAt = 0, limAt = 0;
};

// Cuerpo del diálogo "Dynamics": curva y medidores arriba; debajo, todos los controles de la dinámica en orden de cadena
class DynamicsPanel : public juce::Component
{
public:
    explicit DynamicsPanel (KoniSynthProcessor& p) : curve (p)
    {
        auto& s = p.apvts;
        addAndMakeVisible (curve);
        transient.withToggle (s, "transOn").withKnob (s, "transAttack", "Attack").withKnob (s, "transSustain", "Sustain");
        comp.withToggle (s, "compOn").withCombo (s, "compMode")
            .withKnob (s, "compThreshold", "Thresh").withKnob (s, "compRatio", "Ratio").withKnob (s, "compKnee", "Knee")
            .withKnob (s, "compMakeup", "Makeup");
        timing.withKnob (s, "compAttack", "Attack").withKnob (s, "compRelease", "Release").withKnob (s, "compMix", "Mix");
        key.withCombo (s, "compSource").withKnob (s, "compScHpf", "SC HPF");
        limiter.withToggle (s, "limOn").withCombo (s, "limLookahead").withKnob (s, "limCeiling", "Ceiling").withKnob (s, "limRelease", "Release");
        for (auto* sec : sections()) addAndMakeVisible (*sec);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        curve.setBounds (r.removeFromTop (r.getHeight() * 55 / 100));
        float total = 0;
        for (auto* sec : sections()) total += sec->layoutWeight();
        const auto unit = (float) r.getWidth() / total;
        for (auto* sec : sections())
            sec->setBounds (r.removeFromLeft (juce::roundToInt (unit * sec->layoutWeight())).reduced (3, 0));
    }

private:
    std::array<Section*, 5> sections() { return { &transient, &comp, &timing, &key, &limiter }; }

    TransferCurve curve;
    Section transient { "Transient" }, comp { "Compressor" }, timing { "Timing" }, key { "Sidechain" }, limiter { "Limiter" };
};

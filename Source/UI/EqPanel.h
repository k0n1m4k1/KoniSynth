// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, Eq, Section, Visualizer). No es una cabecera independiente.
#pragma once

//==============================================================================
// UI: curva del ecualizador sobre el espectro en vivo de la salida. Misma escala que el analizador (Visualizer):
// logarítmica 20 Hz - 20 kHz con margen de 6 px; vertical ±24 dB. La curva sale de los mismos coeficientes que el audio
// (Eq::responseDb). Nodos: Low Cut y High Cut (solo frecuencia), Low, Mid 1, Mid 2 y High (frecuencia y ganancia).
// Arrastrar = mover (gesto de parámetro: deshacer, automatización); rueda sobre un medio = Q; doble clic = ganancia 0 dB.
class EqCurve : public juce::Component, private juce::Timer
{
public:
    explicit EqCurve (APVTS& s) : apvts (s)
    {
        setInterceptsMouseClicks (true, false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = plot();
        g.setFont (juce::FontOptions (10.0f));
        for (const auto f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const auto x = xFor (f);
            g.setColour (Skin::white().withAlpha (0.10f));
            g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
            g.setColour (Skin::white().withAlpha (0.45f));
            g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String (f, 0), juce::Rectangle<float> (x + 2, r.getBottom() - 12, 30, 12), juce::Justification::left);
        }
        for (const auto db : { -18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0 })
        {
            g.setColour (Skin::white().withAlpha (db == 0.0 ? 0.25f : 0.08f));
            g.drawHorizontalLine (juce::roundToInt (yFor (db)), r.getX(), r.getRight());
            g.setColour (Skin::white().withAlpha (0.45f));
            g.drawText ((db > 0 ? "+" : "") + juce::String (db, 0), juce::Rectangle<float> (r.getRight() - 28, yFor (db) - 12, 26, 12), juce::Justification::right);
        }
        const auto s = settings();
        const auto fs = 48000.0;
        juce::Path curve;
        for (int px = 0; px <= (int) r.getWidth(); px += 2)
        {
            const auto f = freqFor (r.getX() + (float) px);
            const auto y = yFor (juce::jlimit (-30.0, 30.0, Eq::responseDb (s, fs, f)));
            if (px == 0) curve.startNewSubPath (r.getX(), y); else curve.lineTo (r.getX() + (float) px, y);
        }
        const auto on = apvts.getRawParameterValue ("eqOn")->load() > 0.5f;
        auto fill = curve;
        fill.lineTo (r.getRight(), yFor (0.0));
        fill.lineTo (r.getX(), yFor (0.0));
        fill.closeSubPath();
        g.setColour (Skin::c (0xff42a2c8).withAlpha (on ? 0.18f : 0.06f));
        g.fillPath (fill);
        g.setColour (Skin::c (0xff42a2c8).withAlpha (on ? 1.0f : 0.4f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
        for (int i = 0; i < numNodes; ++i)
        {
            const auto p = nodePos (i, s);
            g.setColour (i == hot || i == dragging ? Skin::white() : Skin::c (0xffffb000));
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (p));
            g.setColour (Skin::black());
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (nodeLabel (i), juce::Rectangle<float> (12.0f, 12.0f).withCentre (p), juce::Justification::centred);
        }
        if (! on)
        {
            g.setColour (Skin::white().withAlpha (0.6f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("EQ OFF", r.reduced (8).removeFromTop (18), juce::Justification::topRight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const auto h = nodeAt (e.position); if (h != hot) { hot = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hot = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = nodeAt (e.position);
        if (dragging < 0) return;
        for (auto* p : nodeParams (dragging)) if (p != nullptr) p->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        const auto ps = nodeParams (dragging);
        if (ps[0] != nullptr) setReal (*ps[0], freqFor (e.position.x));
        if (ps[1] != nullptr) setReal (*ps[1], juce::jlimit (-18.0, 18.0, dbFor (e.position.y)));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0)
            for (auto* p : nodeParams (dragging)) if (p != nullptr) p->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto n = nodeAt (e.position);
        if (n < 0) return;
        if (auto* gp = nodeParams (n)[1]) { gp->beginChangeGesture(); setReal (*gp, 0.0); gp->endChangeGesture(); }
        else if (auto* fp = nodeParams (n)[0]) { fp->beginChangeGesture(); fp->setValueNotifyingHost (fp->getDefaultValue()); fp->endChangeGesture(); }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto n = nodeAt (e.position);
        auto* q = n == 3 ? apvts.getParameter ("eqMid1Q") : n == 4 ? apvts.getParameter ("eqMid2Q") : nullptr;
        if (q == nullptr || w.deltaY == 0.0f) return;
        q->beginChangeGesture();
        q->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, q->getValue() + (w.deltaY > 0 ? 0.03f : -0.03f)));
        q->endChangeGesture();
    }

private:
    static constexpr int numNodes = 6; // 0 Low Cut, 1 High Cut, 2 Low, 3 Mid 1, 4 Mid 2, 5 High

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (6.0f); }
    float xFor (double f) const { const auto r = plot(); return r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); }
    double freqFor (float x) const { const auto r = plot(); return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) (x - r.getX()) / r.getWidth())); }
    float yFor (double db) const { const auto r = plot(); return r.getCentreY() - (float) (db / 24.0) * r.getHeight() * 0.5f; }
    double dbFor (float y) const { const auto r = plot(); return (r.getCentreY() - y) / (r.getHeight() * 0.5) * 24.0; }

    float raw (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    Eq::Settings settings() const
    {
        Eq::Settings s;
        s.lowCut = raw ("eqLowCut"); s.highCut = raw ("eqHighCut");
        s.lowFreq = raw ("eqLowFreq"); s.lowGain = raw ("eqLowGain");
        s.mid1Freq = raw ("eqMid1Freq"); s.mid1Gain = raw ("eqMid1Gain"); s.mid1Q = raw ("eqMid1Q");
        s.mid2Freq = raw ("eqMid2Freq"); s.mid2Gain = raw ("eqMid2Gain"); s.mid2Q = raw ("eqMid2Q");
        s.highFreq = raw ("eqHighFreq"); s.highGain = raw ("eqHighGain"); s.outputDb = raw ("eqOutput");
        static constexpr int orders[] = { 1, 2, 4 };
        s.lowCutOrder = orders[juce::jlimit (0, 2, (int) raw ("eqLowCutSlope"))];
        s.highCutOrder = orders[juce::jlimit (0, 2, (int) raw ("eqHighCutSlope"))];
        return s;
    }

    juce::Point<float> nodePos (int i, const Eq::Settings& s) const
    {
        switch (i)
        {
            case 0:  return { xFor (s.lowCut), yFor (0.0) };
            case 1:  return { xFor (s.highCut), yFor (0.0) };
            case 2:  return { xFor (s.lowFreq), yFor (s.lowGain) };
            case 3:  return { xFor (s.mid1Freq), yFor (s.mid1Gain) };
            case 4:  return { xFor (s.mid2Freq), yFor (s.mid2Gain) };
            default: return { xFor (s.highFreq), yFor (s.highGain) };
        }
    }

    static const char* nodeLabel (int i) { static const char* l[numNodes] = { "C", "C", "L", "1", "2", "H" }; return l[i]; }

    std::array<juce::RangedAudioParameter*, 2> nodeParams (int i) const
    {
        static const char* f[numNodes] = { "eqLowCut", "eqHighCut", "eqLowFreq", "eqMid1Freq", "eqMid2Freq", "eqHighFreq" };
        static const char* gn[numNodes] = { nullptr, nullptr, "eqLowGain", "eqMid1Gain", "eqMid2Gain", "eqHighGain" };
        return { apvts.getParameter (f[i]), gn[i] != nullptr ? apvts.getParameter (gn[i]) : nullptr };
    }

    int nodeAt (juce::Point<float> p) const
    {
        const auto s = settings();
        int best = -1;
        float bestD = 10.0f; // radio de captura en px
        for (int i = numNodes - 1; i >= 0; --i)
            if (const auto d = nodePos (i, s).getDistanceFrom (p); d < bestD) { bestD = d; best = i; }
        return best;
    }

    static void setReal (juce::RangedAudioParameter& p, double v) { p.setValueNotifyingHost (p.convertTo0to1 ((float) v)); }

    void timerCallback() override
    {
        double h = 0;
        for (const auto* id : { "eqOn", "eqLowCut", "eqHighCut", "eqLowFreq", "eqLowGain", "eqMid1Freq", "eqMid1Gain", "eqMid1Q",
                                "eqMid2Freq", "eqMid2Gain", "eqMid2Q", "eqHighFreq", "eqHighGain", "eqOutput", "eqLowCutSlope", "eqHighCutSlope" })
            h = h * 1.0001 + raw (id);
        if (h != lastHash) { lastHash = h; repaint(); }
    }

    APVTS& apvts;
    int hot = -1, dragging = -1;
    double lastHash = 0;
};

// Cuerpo del diálogo "EQ": espectro en vivo con la curva encima y, debajo, todos los controles por etapa
class EqPanel : public juce::Component
{
public:
    explicit EqPanel (KoniSynthProcessor& p) : spectrum (p), curve (p.apvts)
    {
        auto& s = p.apvts;
        spectrum.setMode (Visualizer::spectrum);
        addAndMakeVisible (spectrum);
        addAndMakeVisible (curve);
        hint.setText ("Drag the nodes (C = cuts, L / 1 / 2 / H = bands); mouse wheel on 1 or 2 = Q; double-click = 0 dB.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
        addAndMakeVisible (hint);
        lowCut.withCombo (s, "eqLowCutSlope").withKnob (s, "eqLowCut", "Freq");
        low.withKnob (s, "eqLowFreq", "Freq").withKnob (s, "eqLowGain", "Gain");
        mid1.withKnob (s, "eqMid1Freq", "Freq").withKnob (s, "eqMid1Gain", "Gain").withKnob (s, "eqMid1Q", "Q");
        mid2.withKnob (s, "eqMid2Freq", "Freq").withKnob (s, "eqMid2Gain", "Gain").withKnob (s, "eqMid2Q", "Q");
        high.withKnob (s, "eqHighFreq", "Freq").withKnob (s, "eqHighGain", "Gain");
        highCut.withCombo (s, "eqHighCutSlope").withKnob (s, "eqHighCut", "Freq");
        output.withToggle (s, "eqOn").withKnob (s, "eqOutput", "Output");
        for (auto* sec : sections()) addAndMakeVisible (*sec);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (r.getHeight() * 55 / 100);
        spectrum.setBounds (top);
        curve.setBounds (top);
        hint.setBounds (r.removeFromTop (22));
        float total = 0;
        for (auto* sec : sections()) total += sec->layoutWeight();
        const auto unit = (float) r.getWidth() / total;
        for (auto* sec : sections())
            sec->setBounds (r.removeFromLeft (juce::roundToInt (unit * sec->layoutWeight())).reduced (3, 0));
    }

private:
    std::array<Section*, 7> sections() { return { &lowCut, &low, &mid1, &mid2, &high, &highCut, &output }; }

    Visualizer spectrum;
    EqCurve curve;
    juce::Label hint;
    Section lowCut { "Low Cut" }, low { "Low" }, mid1 { "Mid 1" }, mid2 { "Mid 2" }, high { "High" }, highCut { "High Cut" }, output { "EQ" };
};

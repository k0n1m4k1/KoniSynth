// Mini-displays de modulación del Synth, a la izquierda de las perillas de su sección: envolventes (Filter Env, Amp Env) y
// LFO 1 / LFO 2. Dibujan con las fórmulas del motor y llevan asas que mueven los parámetros (ParamDot: hasta dos parámetros
// por asa, uno por eje, con tooltip, menú del parámetro principal, doble clic = valores por defecto y gesto de
// automatización). Se incluye desde KoniSynth.cpp (mismo TU), tras UI/FilterView.h.
#pragma once

// Asa con hasta dos parámetros: x (horizontal) e y (vertical); el dueño convierte la posición en valores (onDrag)
struct ParamDot : juce::Component, juce::SettableTooltipClient
{
    ParamDot (APVTS& s, const char* xId, const char* yId, const juce::String& hint)
        : px (xId != nullptr ? s.getParameter (xId) : nullptr), py (yId != nullptr ? s.getParameter (yId) : nullptr)
    {
        const auto* main = xId != nullptr ? xId : yId;
        setTooltip (paramTooltip (s, main) + "\n" + hint);
        setMouseCursor (px != nullptr && py != nullptr ? juce::MouseCursor::DraggingHandCursor
                        : px != nullptr ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
        menu = std::make_unique<ParamMenu> (s, main, *this);
    }

    std::function<void (juce::Point<float> inParent)> onDrag;

    void paint (juce::Graphics& g) override
    {
        const auto e = juce::Rectangle<float> (11.0f, 11.0f).withCentre (getLocalBounds().toFloat().getCentre());
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillEllipse (e);
        g.setColour (dragging || isMouseOver() ? Skin::accent().brighter (0.4f) : Skin::accent());
        g.drawEllipse (e, 2.0f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        dragging = true;
        for (auto* p : { px, py }) if (p != nullptr) p->beginChangeGesture();
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging && onDrag && getParentComponent() != nullptr)
            onDrag (e.getEventRelativeTo (getParentComponent()).position);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        for (auto* p : { px, py }) if (p != nullptr) p->endChangeGesture();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        for (auto* p : { px, py })
            if (p != nullptr)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
    }
    static void set (juce::RangedAudioParameter* p, float real) { if (p != nullptr) p->setValueNotifyingHost (p->convertTo0to1 (real)); }

    juce::RangedAudioParameter *px, *py;
    std::unique_ptr<ParamMenu> menu;
    bool dragging = false;
};

//==============================================================================
// Envolvente ADSR con la curva del motor (Env Curve, la misma para las dos envolventes: Synth.h updateEnvelopes):
//   Linear (juce::ADSR): rectas de 0 a 1 en A, de 1 al sostenido en D y del nivel a 0 en R;
//   Analog RC (Dsp/AnalogEnvelope.h): ataque hacia la asíntota 1,4 con τ = A / ln 3,5 (llega a 1 justo en A), decay y release
//   RC con τ = T / ln 100 (T = tiempo hasta -40 dB del recorrido; se dibuja hasta T).
// Eje de tiempo por tramos: cada fase ocupa un ancho que crece con el logaritmo de su tiempo (A hasta 5 s: 24 % del ancho;
// D y R hasta 20 s: 28 % y 32 %) y dentro de su tramo el tiempo es lineal; el sostenido, un tramo fijo del 16 %. Así cada asa
// depende solo de su parámetro. Asas: pico (A), codo (D y S) y final (R)
class EnvView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    EnvView (APVTS& st, const char* a, const char* d, const char* su, const char* r)
        : s (st), ids { a, d, su, r },
          attackDot (st, a, nullptr, "Drag left/right."), decayDot (st, d, su, "Drag: left/right = Decay, up/down = Sustain."),
          releaseDot (st, r, nullptr, "Drag left/right.")
    {
        setTooltip ("The envelope with the engine's curve (Env Curve). Drag the points: peak = Attack, knee = Decay and "
                    "Sustain, end = Release. Each phase has its own stretch of the time axis (log of its time).");
        for (auto* dot : { &attackDot, &decayDot, &releaseDot }) addAndMakeVisible (*dot);
        attackDot.onDrag = [this] (juce::Point<float> p) { ParamDot::set (attackDot.px, timeAt (p.x - x0(), wA, 5.0f)); };
        decayDot.onDrag = [this] (juce::Point<float> p)
        {
            ParamDot::set (decayDot.px, timeAt (p.x - xA(), wD, 20.0f));
            ParamDot::set (decayDot.py, juce::jlimit (0.0f, 1.0f, (plot().getBottom() - p.y) / plot().getHeight()));
        };
        releaseDot.onDrag = [this] (juce::Point<float> p) { ParamDot::set (releaseDot.px, timeAt (p.x - xS(), wR, 20.0f)); };
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        const auto screen = getLocalBounds().toFloat();
        Skin::screen (g, screen, 6.0f);
        const auto r = plot();
        const auto trace = Skin::trace();
        const auto A = v (0), D = v (1), S = v (2), R = v (3);
        const auto analog = s.getRawParameterValue ("envCurve")->load() > 0.5f;
        // Separadores de fase
        for (const auto x : { xA(), xD(), xS() })
        {
            g.setColour (trace.withAlpha (0.12f));
            g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
        }
        juce::Path p;
        const auto y = [&r] (float level) { return r.getBottom() - level * r.getHeight(); };
        p.startNewSubPath (x0(), y (0.0f));
        constexpr int n = 40;
        // Ataque
        const auto ratio = 1.4f, tauA = A / std::log (ratio / (ratio - 1.0f));
        for (int i = 1; i <= n; ++i)
        {
            const auto t = A * (float) i / n;
            const auto level = analog ? juce::jmin (1.0f, ratio - ratio * std::exp (-t / juce::jmax (1.0e-6f, tauA))) : t / juce::jmax (1.0e-6f, A);
            p.lineTo (x0() + (xA() - x0()) * (float) i / n, y (juce::jmin (1.0f, level)));
        }
        // Decay hasta el sostenido
        for (int i = 1; i <= n; ++i)
        {
            const auto f = (float) i / n;
            const auto level = analog ? S + (1.0f - S) * std::exp (-std::log (100.0f) * f) : 1.0f + (S - 1.0f) * f;
            p.lineTo (xA() + (xD() - xA()) * f, y (level));
        }
        p.lineTo (xS(), y (S));
        // Release desde el sostenido
        for (int i = 1; i <= n; ++i)
        {
            const auto f = (float) i / n;
            const auto level = analog ? S * std::exp (-std::log (100.0f) * f) : S * (1.0f - f);
            p.lineTo (xS() + (xR() - xS()) * f, y (level));
        }
        juce::Path fill (p);
        fill.lineTo (xR(), r.getBottom());
        fill.closeSubPath();
        g.setColour (trace.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (trace.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (trace);
        g.strokePath (p, juce::PathStrokeType (1.8f));

        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        g.setColour (trace.withAlpha (0.85f));
        const auto ms = [] (float sec) { return sec < 1.0f ? juce::String (juce::roundToInt (sec * 1000.0f)) + "ms" : juce::String (sec, 2) + "s"; };
        auto text = screen.reduced (9.0f, 6.0f);
        // Dos líneas: con ~150 px de ancho no cabe en una
        g.drawText ("A " + ms (A) + "  D " + ms (D), text, juce::Justification::topLeft, false);
        g.drawText ("S " + juce::String (juce::roundToInt (S * 100.0f)) + "%  R " + ms (R), text.withTrimmedTop (12.0f), juce::Justification::topLeft, false);
        g.setColour (trace.withAlpha (0.55f));
        g.drawText (analog ? "ANALOG RC" : "LINEAR", text, juce::Justification::bottomRight, false);
        Skin::glass (g, screen, 6.0f);
    }

    void resized() override { place(); }

private:
    float v (int i) const { return s.getRawParameterValue (ids[(size_t) i])->load(); }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 18.0f); }
    static constexpr float wA = 0.24f, wD = 0.28f, wHold = 0.16f, wR = 0.32f;
    // Ancho de un tramo para un tiempo t (0,2 ms .. tmax): logarítmico con referencia 1 ms
    float widthOf (float t, float frac, float tmax) const
    {
        return plot().getWidth() * frac * std::log1p (juce::jmax (0.0f, t) / 0.001f) / std::log1p (tmax / 0.001f);
    }
    float timeAt (float dx, float frac, float tmax) const
    {
        const auto u = juce::jlimit (0.0f, 1.0f, dx / (plot().getWidth() * frac));
        return juce::jlimit (0.0002f, tmax, 0.001f * std::expm1 (u * std::log1p (tmax / 0.001f)));
    }
    float x0() const { return plot().getX(); }
    float xA() const { return x0() + widthOf (v (0), wA, 5.0f); }
    float xD() const { return xA() + widthOf (v (1), wD, 20.0f); }
    float xS() const { return xD() + plot().getWidth() * wHold; }
    float xR() const { return xS() + widthOf (v (3), wR, 20.0f); }

    void place()
    {
        const auto r = plot();
        const auto at = [] (ParamDot& d, float x, float y) { d.setBounds (juce::Rectangle<int> (20, 20).withCentre ({ juce::roundToInt (x), juce::roundToInt (y) })); };
        at (attackDot, xA(), r.getY());
        at (decayDot, xD(), r.getBottom() - v (2) * r.getHeight());
        at (releaseDot, xR(), r.getBottom());
    }

    void timerCallback() override
    {
        std::uint64_t h = (std::uint64_t) std::hash<float>{} (s.getRawParameterValue ("envCurve")->load());
        for (int i = 0; i < 4; ++i) h = h * 1000003u ^ (std::uint64_t) std::hash<float>{} (v (i));
        if (h != lastHash) { lastHash = h; place(); repaint(); }
    }

    APVTS& s;
    std::array<const char*, 4> ids;
    ParamDot attackDot, decayDot, releaseDot;
    std::uint64_t lastHash = 0;
};

//==============================================================================
// LFO: dos ciclos de la forma del motor (VoiceLfo, Synth.h: seno, triángulo, sierra descendente, cuadrada, S&H, Random con
// valores de una semilla fija) a la amplitud de Depth, el alcance real en su destino (Synth.h: Vibrato ±2·Depth st,
// Tremolo hasta -Depth, Filter ±2·Depth oct, PWM ±0,45·Depth, Osc 2 Pitch ±2·Depth st, WT Position ±0,5·Depth) y un punto
// que recorre la forma a la frecuencia de Rate. Ese punto es una fase visual libre: en el motor cada voz tiene su LFO con
// key sync (empieza con la nota). Asas: Depth (vertical, en el pico) y Rate (raíl inferior, escala log 0,02-100 Hz)
class LfoView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    LfoView (APVTS& st, bool second)
        : s (st), two (second),
          depthDot (st, nullptr, second ? "lfo2Depth" : "lfoDepth", "Drag up/down."),
          rateDot (st, second ? "lfo2Rate" : "lfoRate", nullptr, "Drag left/right (Free sync only; with a note value the tempo sets the rate).")
    {
        setTooltip ("The LFO shape at its depth and what it does to its target. The moving dot is a visual phase at the Rate "
                    "(in the engine each voice restarts its LFO with the note).");
        addAndMakeVisible (depthDot);
        addAndMakeVisible (rateDot);
        depthDot.onDrag = [this] (juce::Point<float> p)
        {
            const auto r = plot();
            ParamDot::set (depthDot.py, juce::jlimit (0.0f, 1.0f, (r.getCentreY() - p.y) / (r.getHeight() * 0.45f)));
        };
        rateDot.onDrag = [this] (juce::Point<float> p)
        {
            const auto r = plot();
            const auto u = juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth());
            ParamDot::set (rateDot.px, 0.02f * std::pow (5000.0f, u)); // 0,02 .. 100 Hz
        };
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto screen = getLocalBounds().toFloat();
        Skin::screen (g, screen, 6.0f);
        const auto r = plot();
        const auto trace = Skin::trace();
        const auto on = v ("On") > 0.5f;
        const auto depth = v ("Depth");
        const auto shape = (int) v ("Shape");
        g.setColour (trace.withAlpha (0.2f));
        g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
        juce::Path p;
        constexpr int n = 160;
        for (int i = 0; i <= n; ++i)
        {
            const auto t = 2.0f * (float) i / n;
            const auto x = r.getX() + r.getWidth() * (float) i / n;
            const auto y = r.getCentreY() - depth * r.getHeight() * 0.45f * shapeAt (shape, t);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (trace.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (trace.withAlpha (on ? 1.0f : 0.4f));
        g.strokePath (p, juce::PathStrokeType (1.8f));
        if (on) // fase visual
        {
            const auto t = (float) (2.0 * phase);
            const auto dot = juce::Point<float> (r.getX() + r.getWidth() * t / 2.0f, r.getCentreY() - depth * r.getHeight() * 0.45f * shapeAt (shape, t));
            g.setColour (trace);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
        }
        // Raíl de Rate
        g.setColour (trace.withAlpha (0.25f));
        g.drawHorizontalLine (juce::roundToInt (r.getBottom() + 8.0f), r.getX(), r.getRight());

        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        g.setColour (trace.withAlpha (0.85f));
        auto text = screen.reduced (9.0f, 5.0f);
        const auto sync = (int) v ("Sync");
        const auto rate = sync == 0 ? juce::String (v ("Rate"), v ("Rate") < 10.0f ? 2 : 1) + " Hz" : syncText();
        g.drawText (on ? rate : juce::String ("OFF"), text, juce::Justification::topLeft, false);
        if (on) g.drawText (reach (depth), text.withTrimmedTop (12.0f), juce::Justification::topLeft, false);
        Skin::glass (g, screen, 6.0f);
    }

    void resized() override { place(); }

private:
    juce::String id (const char* suffix) const { return juce::String (two ? "lfo2" : "lfo") + suffix; }
    float v (const char* suffix) const { return s.getRawParameterValue (id (suffix))->load(); }
    juce::String syncText() const { auto* p = s.getParameter (id ("Sync")); return p != nullptr ? p->getCurrentValueAsText() : juce::String(); }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 18.0f).withTrimmedBottom (8.0f); }

    // Alcance en el destino, como lo aplica Synth.h
    juce::String reach (float depth) const
    {
        const auto target = (int) v ("Target");
        const auto st = [] (float x) { return juce::String (x, 2); };
        switch (target)
        {
            case 0:  return "VIBRATO +/-" + st (2.0f * depth) + " st";
            case 1:  return "TREMOLO -" + juce::String (juce::roundToInt (depth * 100.0f)) + "%";
            case 2:  return "FILTER +/-" + st (2.0f * depth) + " oct";
            case 3:  return "PWM +/-" + juce::String (juce::roundToInt (45.0f * depth)) + "%";
            case 4:  return "OSC 2 +/-" + st (2.0f * depth) + " st";
            default: return "WT POS +/-" + juce::String (juce::roundToInt (50.0f * depth)) + "%";
        }
    }

    // Forma de VoiceLfo en t ciclos (S&H y Random: valores de una semilla fija, uno por ciclo)
    static float shapeAt (int shape, float t)
    {
        const auto cycle = (int) std::floor (t);
        const auto p = t - (float) cycle;
        const auto rnd = [] (int k) { juce::Random r (0x1F0 + k); return 2.0f * r.nextFloat() - 1.0f; };
        switch (shape)
        {
            case 1:  return 1.0f - 4.0f * std::abs (p - 0.5f);
            case 2:  return 1.0f - 2.0f * p;
            case 3:  return p < 0.5f ? 1.0f : -1.0f;
            case 4:  return rnd (cycle + 1);
            case 5:  return rnd (cycle) + (rnd (cycle + 1) - rnd (cycle)) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * p));
            default: return std::sin (juce::MathConstants<float>::twoPi * p);
        }
    }

    void place()
    {
        const auto r = plot();
        const auto shape = (int) v ("Shape");
        // Depth en el máximo del primer ciclo (fase 0,25 en el seno, 0,5 en el triángulo, 0 en sierra y cuadrada), a la
        // altura de +Depth (S&H y Random: en la misma x, a +Depth aunque su valor no llegue)
        const auto tPeak = shape == 0 ? 0.25f : shape == 1 ? 0.5f : 0.0f;
        depthDot.setBounds (juce::Rectangle<int> (20, 20).withCentre ({ juce::roundToInt (r.getX() + r.getWidth() * tPeak / 2.0f),
                                                                       juce::roundToInt (r.getCentreY() - v ("Depth") * r.getHeight() * 0.45f) }));
        const auto u = std::log (juce::jlimit (0.02f, 100.0f, v ("Rate")) / 0.02f) / std::log (5000.0f);
        rateDot.setBounds (juce::Rectangle<int> (20, 20).withCentre ({ juce::roundToInt (r.getX() + u * r.getWidth()), juce::roundToInt (r.getBottom() + 8.0f) }));
        rateDot.setVisible ((int) v ("Sync") == 0);
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto dt = juce::jlimit (0.0, 0.2, (now - lastMs) / 1000.0);
        lastMs = now;
        // Fase visual: Rate en Hz (con Sync, ~1 Hz por no conocer aquí el tempo), dos ciclos a lo ancho
        const auto hz = (int) v ("Sync") == 0 ? (double) v ("Rate") : 1.0;
        phase = std::fmod (phase + 0.5 * juce::jmin (hz, 20.0) * dt, 1.0);
        std::uint64_t h = 0;
        for (const auto* sfx : { "On", "Depth", "Shape", "Rate", "Sync", "Target" })
            h = h * 1000003u ^ (std::uint64_t) std::hash<float>{} (v (sfx));
        if (h != lastHash) { lastHash = h; place(); }
        if (v ("On") > 0.5f || h != lastPainted) { lastPainted = h; repaint(); }
    }

    APVTS& s;
    const bool two;
    ParamDot depthDot, rateDot;
    double phase = 0.0, lastMs = juce::Time::getMillisecondCounterHiRes();
    std::uint64_t lastHash = 0, lastPainted = 1;
};

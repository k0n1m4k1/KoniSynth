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
        setViewportIgnoreDragFlag (true); // arrastrar el asa no desplaza el panel
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
    // Arriba, banda de 36 px para la lectura de dos líneas (6 + 2 × 12 + 6): el pico del ataque no queda debajo del texto
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (36.0f).withTrimmedBottom (18.0f); }
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
        // Fase visual: la frecuencia del LFO (con Sync, la figura al tempo actual, como el motor: Params::lfoHz), dos ciclos a
        // lo ancho
        const auto hz = dynamic_cast<KoniSynthProcessor&> (s.processor).getParams().lfoHz ((int) v ("Sync"), v ("Rate"));
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

//==============================================================================
// Envolventes de los motores emulados (Polivoks, MD PSG, N64, VL-1, Gen X-2 y los operadores OPL): la pantalla de EnvView con
// un modelo por motor que da los tramos (duración en s y nivel 0..1 a lo largo del tramo) con las fórmulas de su motor; el
// nivel es amplitud lineal. Eje de tiempo por tramos como EnvView: ancho = fracción · log1p (t / 1 ms) / log1p (tmax / 1 ms);
// un tramo fijo (el sostenido) ocupa su fracción entera. Las asas van al final de su tramo; al arrastrar se prueba la rejilla de
// valores del parámetro (sus pasos si es entero, 1001 si es continuo) y se queda el que deja el final del tramo más cerca del
// puntero: no hace falta invertir cada fórmula y vale aunque el tiempo de un tramo dependa de otro parámetro (VL-1, OPL)
struct EnvStage
{
    float seconds = 0.0f, frac = 0.25f, tmax = 1.0f; // duración; fracción máxima del ancho y el tiempo que la llena
    std::function<float (float)> level;               // nivel 0..1 en la fracción f del tramo
    bool fixed = false;                               // ancho fijo (sostenido): seconds no cuenta
};
using EnvRead = std::function<float (const juce::String&)>;

struct EnvSpec
{
    struct Handle { juce::String x, y; int stage; juce::String hint; }; // stage < 0: el último tramo
    std::vector<juce::String> ids;                                  // parámetros que redibujan
    std::function<std::vector<EnvStage> (const EnvRead&)> stages;
    std::function<juce::String (const EnvRead&)> readout;          // dos líneas separadas por '\n'
    std::vector<Handle> handles;
    juce::String tag, tip;
    bool bipolar = false; // nivel 0,5 = centro (envolvente de tono): línea central y relleno hacia ella
};

static juce::String envTimeText (float sec)
{
    if (! std::isfinite (sec)) return "inf";
    return sec < 1.0f ? juce::String (juce::roundToInt (sec * 1000.0f)) + "ms" : juce::String (sec, 2) + "s";
}

class StageEnvView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    StageEnvView (APVTS& st, EnvSpec sp) : s (st), spec (std::move (sp))
    {
        setTooltip (spec.tip);
        for (size_t k = 0; k < spec.handles.size(); ++k)
        {
            const auto& h = spec.handles[k];
            auto dot = std::make_unique<ParamDot> (s, h.x.isEmpty() ? nullptr : h.x.toRawUTF8(), h.y.isEmpty() ? nullptr : h.y.toRawUTF8(), h.hint);
            dot->onDrag = [this, k] (juce::Point<float> p) { drag (k, p); };
            addAndMakeVisible (*dot);
            dots.push_back (std::move (dot));
        }
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        const auto screen = getLocalBounds().toFloat();
        Skin::screen (g, screen, 6.0f);
        const auto r = plot();
        const auto trace = Skin::trace();
        const auto read = reader();
        const auto st = spec.stages (read);
        const auto x = edges (st);
        for (size_t i = 1; i + 1 < x.size(); ++i)
        {
            g.setColour (trace.withAlpha (0.12f));
            g.drawVerticalLine (juce::roundToInt (x[i]), r.getY(), r.getBottom());
        }
        const auto y = [&r] (float level) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, level) * r.getHeight(); };
        juce::Path p;
        p.startNewSubPath (x.front(), y (st.empty() ? 0.0f : st.front().level (0.0f)));
        constexpr int n = 96; // los escalones de VL-1 y MD PSG (15 niveles) se ven
        for (size_t i = 0; i < st.size(); ++i)
            for (int j = 0; j <= n; ++j)
            {
                const auto f = (float) j / n;
                p.lineTo (x[i] + (x[i + 1] - x[i]) * f, y (st[i].level (f)));
            }
        const auto base = spec.bipolar ? r.getCentreY() : r.getBottom();
        if (spec.bipolar)
        {
            g.setColour (trace.withAlpha (0.25f));
            g.drawHorizontalLine (juce::roundToInt (base), r.getX(), r.getRight());
        }
        juce::Path fill (p);
        fill.lineTo (x.back(), base);
        fill.lineTo (x.front(), base);
        fill.closeSubPath();
        g.setColour (trace.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (trace.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (trace);
        g.strokePath (p, juce::PathStrokeType (1.8f));

        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        g.setColour (trace.withAlpha (0.85f));
        auto text = screen.reduced (9.0f, 6.0f);
        const auto lines = juce::StringArray::fromLines (spec.readout (read));
        for (int i = 0; i < juce::jmin (2, lines.size()); ++i)
            g.drawText (lines[i], text.withTrimmedTop (12.0f * (float) i), juce::Justification::topLeft, false);
        g.setColour (trace.withAlpha (0.55f));
        g.drawText (spec.tag, text, juce::Justification::bottomRight, false);
        Skin::glass (g, screen, 6.0f);
    }

    void resized() override { place(); }

private:
    EnvRead reader (const juce::String& over = {}, float value = 0.0f) const
    {
        return [this, over, value] (const juce::String& id) { return id == over ? value : s.getRawParameterValue (id)->load(); };
    }
    // Arriba, banda de 36 px para la lectura de dos líneas (como EnvView)
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (36.0f).withTrimmedBottom (18.0f); }
    float widthOf (const EnvStage& t) const
    {
        const auto w = plot().getWidth() * t.frac;
        if (t.fixed) return w;
        const auto sec = std::isfinite (t.seconds) ? juce::jlimit (0.0f, t.tmax, t.seconds) : t.tmax;
        return w * std::log1p (sec / 0.001f) / std::log1p (t.tmax / 0.001f);
    }
    std::vector<float> edges (const std::vector<EnvStage>& st) const
    {
        std::vector<float> x { plot().getX() };
        for (const auto& t : st) x.push_back (x.back() + widthOf (t));
        return x;
    }
    static size_t stageOf (int stage, size_t count) { return stage < 0 ? count - 1 : (size_t) juce::jmin (stage, (int) count - 1); }

    // Valor normalizado del parámetro que minimiza cost (rejilla de sus pasos, o 1001 puntos)
    void search (juce::RangedAudioParameter* p, const juce::String& id, const std::function<float (const std::vector<EnvStage>&)>& cost)
    {
        if (p == nullptr) return;
        const auto steps = juce::jlimit (2, 1001, p->getNumSteps());
        auto best = p->getValue();
        auto bestCost = std::numeric_limits<float>::max();
        for (int k = 0; k < steps; ++k)
        {
            const auto u = (float) k / (float) (steps - 1);
            const auto c = cost (spec.stages (reader (id, p->convertFrom0to1 (u))));
            if (c < bestCost - 1.0e-6f) { bestCost = c; best = u; }
        }
        if (std::abs (best - p->getValue()) > 1.0e-6f) p->setValueNotifyingHost (best);
    }

    void drag (size_t k, juce::Point<float> p)
    {
        const auto& h = spec.handles[k];
        const auto r = plot();
        if (h.y.isNotEmpty())
        {
            const auto target = juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight());
            search (dots[k]->py, h.y, [&] (const std::vector<EnvStage>& st) { return std::abs (st[stageOf (h.stage, st.size())].level (1.0f) - target); });
        }
        if (h.x.isNotEmpty())
            search (dots[k]->px, h.x, [&] (const std::vector<EnvStage>& st)
            {
                return std::abs (edges (st)[stageOf (h.stage, st.size()) + 1] - p.x);
            });
    }

    void place()
    {
        const auto st = spec.stages (reader());
        if (st.empty()) return;
        const auto x = edges (st);
        const auto r = plot();
        for (size_t k = 0; k < dots.size(); ++k)
        {
            const auto i = stageOf (spec.handles[k].stage, st.size());
            const auto lv = juce::jlimit (0.0f, 1.0f, st[i].level (1.0f));
            dots[k]->setBounds (juce::Rectangle<int> (20, 20).withCentre ({ juce::roundToInt (x[i + 1]), juce::roundToInt (r.getBottom() - lv * r.getHeight()) }));
        }
    }

    void timerCallback() override
    {
        std::uint64_t h = 0;
        for (const auto& id : spec.ids) h = h * 1000003u ^ (std::uint64_t) std::hash<float>{} (s.getRawParameterValue (id)->load());
        if (h != lastHash) { lastHash = h; place(); repaint(); }
    }

    APVTS& s;
    EnvSpec spec;
    std::vector<std::unique_ptr<ParamDot>> dots;
    std::uint64_t lastHash = 1;
};

// Modelos por motor: cada uno reproduce la envolvente de su motor (fichero y líneas en cada función)
namespace EngineEnv
{
    // Envolvente de tono del Synth (Engines/Synth.h, pitchEnvValue): salta a Amount semitonos con la nota y vuelve a 0
    // exponencialmente con τ = Decay (pitchEnvValue *= e^(-1 / (Decay · fs))); se dibuja hasta -40 dB (τ · ln 100). Escala
    // vertical ±48 st (el rango de Amount), centro = sin desviación
    static EnvSpec synthPitch()
    {
        EnvSpec e;
        e.ids = { "pitchEnvAmount", "pitchEnvDecay" };
        e.bipolar = true;
        e.stages = [] (const EnvRead& v)
        {
            const auto a = v ("pitchEnvAmount") / 48.0f, tau = v ("pitchEnvDecay") * 0.001f, ln100 = std::log (100.0f);
            return std::vector<EnvStage> {
                { 0.0f, 0.0f, 1.0f, [a] (float f) { return 0.5f + 0.5f * a * f; } },
                { tau * ln100, 0.92f, 2.0f * ln100, [a] (float f) { return 0.5f + 0.5f * a * std::exp (-std::log (100.0f) * f); } } };
        };
        e.readout = [] (const EnvRead& v)
        {
            const auto a = v ("pitchEnvAmount");
            return juce::String (a >= 0.0f ? "+" : "") + juce::String (a, 1) + " st\ntau " + envTimeText (v ("pitchEnvDecay") * 0.001f);
        };
        e.handles = { { {}, "pitchEnvAmount", 0, "Drag up/down: Amount (semitones at the start of the note)." },
                      { "pitchEnvDecay", {}, 1, "Drag left/right: Decay (time constant)." } };
        e.tag = "PITCH";
        e.tip = "Pitch envelope of Osc 1-3: the note starts Amount semitones away and glides back exponentially (Decay = time "
                "constant; drawn to -40 dB). Drag the points.";
        return e;
    }

    // Polivoks (Engines/Polivoks.h, envStep): ataque RC hacia 1,25 con τ = A (llega a 1 en A·ln (1,25 - nivel inicial) / 0,25),
    // decay RC con τ = D hacia S, release RC con τ = R; decay y release se dibujan hasta -40 dB de su recorrido (τ·ln 100).
    // Repeat: AD que se repite mientras la tecla está pulsada (decay hacia 0 hasta 0,01 y vuelta a atacar): dos ciclos
    static EnvSpec polivoks (const juce::String& pre)
    {
        EnvSpec e;
        const auto A = pre + "Attack", D = pre + "Decay", S = pre + "Sustain", R = pre + "Release", L = pre + "Loop";
        e.ids = { A, D, S, R, L };
        e.stages = [=] (const EnvRead& v)
        {
            const auto a = juce::jmax (0.0005f, v (A)), d = juce::jmax (0.0005f, v (D)), su = v (S), r = juce::jmax (0.0005f, v (R));
            const auto loop = v (L) > 0.5f;
            const auto ln100 = std::log (100.0f), tmaxA = 3.0f * std::log (5.0f), tmaxDR = 12.0f * ln100;
            const auto attack = [=] (float from, float frac)
            {
                const auto t1 = a * std::log ((1.25f - from) / 0.25f);
                return EnvStage { t1, frac, tmaxA, [=] (float f) { return juce::jmin (1.0f, 1.25f - (1.25f - from) * std::exp (-f * t1 / a)); } };
            };
            std::vector<EnvStage> st;
            if (loop)
            {
                const auto decay = EnvStage { d * ln100, 0.18f, tmaxDR, [] (float f) { return std::exp (-std::log (100.0f) * f); } };
                st = { attack (0.0f, 0.12f), decay, attack (0.01f, 0.12f), decay };
                st.push_back ({ r * ln100, 0.30f, tmaxDR, [] (float f) { return 0.01f * std::exp (-std::log (100.0f) * f); } });
            }
            else
            {
                st = { attack (0.0f, 0.22f),
                       { d * ln100, 0.28f, tmaxDR, [=] (float f) { return su + (1.0f - su) * std::exp (-std::log (100.0f) * f); } },
                       { 0.0f, 0.16f, 0.0f, [=] (float) { return su; }, true },
                       { r * ln100, 0.32f, tmaxDR, [=] (float f) { return su * std::exp (-std::log (100.0f) * f); } } };
            }
            return st;
        };
        e.readout = [=] (const EnvRead& v)
        {
            return "A " + envTimeText (v (A)) + "  D " + envTimeText (v (D)) + "\n"
                 + (v (L) > 0.5f ? juce::String ("REPEAT") : "S " + juce::String (juce::roundToInt (v (S) * 100.0f)) + "%") + "  R " + envTimeText (v (R));
        };
        e.handles = { { A, {}, 0, "Drag left/right." }, { D, S, 1, "Drag: left/right = Decay, up/down = Sustain." }, { R, {}, -1, "Drag left/right." } };
        e.tag = "RC";
        e.tip = "The envelope with the engine's RC curves (time constants A, D, R; decay and release drawn to -40 dB). With Repeat, "
                "two attack-decay cycles. Drag the points.";
        return e;
    }

    // Mega Drive PSG (Engines/MegaDrive.h, psgKeyOn / psgEnvFrame): atenuación 0..15 en pasos de 2 dB (15 = apagado), un paso
    // cada 'rate' frames de 59,922743 Hz (0 = inmediato). Ataque 15 -> 0, decay 0 -> S, release S -> 15
    static EnvSpec mdPsg()
    {
        EnvSpec e;
        e.ids = { "mdPsgAttack", "mdPsgDecay", "mdPsgSustain", "mdPsgRelease" };
        e.stages = [] (const EnvRead& v)
        {
            const auto frame = 1.0f / 59.922743f;
            const auto a = juce::roundToInt (v ("mdPsgAttack")), d = juce::roundToInt (v ("mdPsgDecay"));
            const auto su = juce::roundToInt (v ("mdPsgSustain")), r = juce::roundToInt (v ("mdPsgRelease"));
            const auto amp = [] (int att) { return att >= 15 ? 0.0f : std::pow (10.0f, -0.1f * (float) att); };
            const auto stepAt = [] (float f, int count) { return juce::jmin (count, (int) std::floor (f * (float) count + 1.0e-4f)); };
            const auto tmax = 15.0f * 15.0f * frame;
            return std::vector<EnvStage> {
                { 15.0f * (float) a * frame, 0.24f, tmax, [=] (float f) { return a == 0 ? 1.0f : amp (15 - stepAt (f, 15)); } },
                { (float) (su * d) * frame, 0.28f, tmax, [=] (float f) { return amp (d == 0 ? su : stepAt (f, su)); } },
                { 0.0f, 0.16f, 0.0f, [=] (float) { return amp (su); }, true },
                { (float) ((15 - su) * r) * frame, 0.32f, tmax, [=] (float f) { return r == 0 ? 0.0f : amp (su + stepAt (f, 15 - su)); } } };
        };
        e.readout = [] (const EnvRead& v)
        {
            const auto frame = 1.0f / 59.922743f;
            const auto su = juce::roundToInt (v ("mdPsgSustain"));
            return "A " + envTimeText (15.0f * v ("mdPsgAttack") * frame) + "  D " + envTimeText ((float) su * v ("mdPsgDecay") * frame) + "\n"
                 + "S -" + juce::String (2 * su) + "dB  R " + envTimeText ((float) (15 - su) * v ("mdPsgRelease") * frame);
        };
        e.handles = { { "mdPsgAttack", {}, 0, "Drag left/right." }, { "mdPsgDecay", "mdPsgSustain", 1, "Drag: left/right = Decay, up/down = Sustain." },
                      { "mdPsgRelease", {}, -1, "Drag left/right." } };
        e.tag = "2 dB / FRAME";
        e.tip = "The driver envelope: 2 dB steps, one every 'rate' video frames (59.92 Hz). Drag the points.";
        return e;
    }

    // N64 (Engines/N64.h, ramp): rampas geométricas de libultra desde el volumen actual (mínimo 1/32767) hasta Attack Volume
    // en Attack, hasta Decay Volume en Decay y hasta 1/32767 en Release; pico = 1 (velocidad 127)
    static EnvSpec n64()
    {
        EnvSpec e;
        e.ids = { "n64Attack", "n64Decay", "n64Release", "n64AttackVolume", "n64DecayVolume" };
        e.stages = [] (const EnvRead& v)
        {
            const auto lo = 1.0f / 32767.0f;
            const auto av = juce::jmax (lo, v ("n64AttackVolume") / 127.0f), dv = juce::jmax (lo, v ("n64DecayVolume") / 127.0f);
            const auto geo = [] (float from, float to) { return [=] (float f) { return from * std::pow (to / from, f); }; };
            return std::vector<EnvStage> {
                { v ("n64Attack") * 0.001f, 0.24f, 5.0f, geo (lo, av) },
                { v ("n64Decay") * 0.001f, 0.28f, 5.0f, geo (av, dv) },
                { 0.0f, 0.16f, 0.0f, [=] (float) { return dv; }, true },
                { v ("n64Release") * 0.001f, 0.32f, 5.0f, geo (dv, lo) } };
        };
        e.readout = [] (const EnvRead& v)
        {
            return "A " + envTimeText (v ("n64Attack") * 0.001f) + " " + juce::String (juce::roundToInt (v ("n64AttackVolume"))) + "  D "
                 + envTimeText (v ("n64Decay") * 0.001f) + " " + juce::String (juce::roundToInt (v ("n64DecayVolume"))) + "\nR " + envTimeText (v ("n64Release") * 0.001f);
        };
        e.handles = { { "n64Attack", "n64AttackVolume", 0, "Drag: left/right = Attack, up/down = Attack Volume." },
                      { "n64Decay", "n64DecayVolume", 1, "Drag: left/right = Decay, up/down = Decay Volume." },
                      { "n64Release", {}, -1, "Drag left/right." } };
        e.tag = "LIBULTRA";
        e.tip = "The library envelope: geometric volume ramps (straight lines in dB) to Attack Volume, Decay Volume and silence. "
                "Drag the points.";
        return e;
    }

    // Casio VL-1 (Engines/VL1.h: tablas de las líneas 27-33, startLine y clockEnvelope): 15 niveles, unidad de 12 ms; rectas
    // cuyo nivel entero se actualiza cada 12 ms (subiendo: techo; bajando: suelo). Ataque desde attackBegin hasta 15, decay a
    // 15 niveles por decayUnits hasta SL, sostenido hacia 0 a 15 niveles por sustainUnits (ST 9: plano), release a 14 niveles
    // por releaseUnits desde el nivel al final del sostenido
    static EnvSpec vl1()
    {
        EnvSpec e;
        e.ids = { "vl1Attack", "vl1Decay", "vl1SustainLevel", "vl1SustainTime", "vl1Release" };
        e.stages = [] (const EnvRead& v)
        {
            constexpr float unit = 0.012f;
            const auto digit = [&v] (const char* id) { return juce::jlimit (0, 9, juce::roundToInt (v (id))); };
            const auto a = digit ("vl1Attack"), d = digit ("vl1Decay"), sl = VL1::sustainLevel[digit ("vl1SustainLevel")];
            const auto stU = VL1::sustainUnits[digit ("vl1SustainTime")], rU = VL1::releaseUnits[digit ("vl1Release")];
            const auto begin = VL1::attackBegin[a], aU = VL1::attackUnits[a], dU = VL1::decayUnits[d];
            // Recta de 'from' con pendiente span / (units · 12 ms), nivel entero cada 12 ms, hasta 'to'
            const auto line = [=] (int from, int to, int span, int units, float seconds)
            {
                return [=] (float f)
                {
                    if (units <= 0) return (float) to / 15.0f;
                    const auto tq = std::floor (f * seconds / unit + 1.0e-4f) * unit;
                    const auto y = (float) from + (to > from ? 1.0f : -1.0f) * (float) span * tq / ((float) units * unit);
                    const auto lv = to > from ? juce::jmin (to, (int) std::ceil (y - 1.0e-4f)) : juce::jmax (to, (int) std::floor (y + 1.0e-4f));
                    return (float) (f >= 1.0f ? to : lv) / 15.0f;
                };
            };
            // Duración hasta recorrer 'levels' niveles, redondeada a ticks de 12 ms
            const auto timeOf = [] (int levels, int span, int units) { return units <= 0 ? 0.0f : std::ceil ((float) levels * (float) units / (float) span - 1.0e-4f) * unit; };
            std::vector<EnvStage> st;
            const auto tA = (float) aU * unit;
            st.push_back ({ tA, 0.14f, 25.0f * unit, line (begin, 15, 15 - begin, aU, tA) });
            const auto tD = d > 0 ? timeOf (15 - sl, 15, dU) : 0.0f;
            st.push_back ({ tD, 0.22f, 87.0f * unit, line (15, sl, 15, d > 0 ? dU : 0, tD) });
            auto end = sl;
            if (stU < 0)
                st.push_back ({ 0.0f, 0.30f, 0.0f, [=] (float) { return (float) sl / 15.0f; }, true });
            else
            {
                const auto tS = timeOf (sl, 15, stU);
                st.push_back ({ tS, 0.30f, 615.0f * unit, line (sl, 0, 15, stU, tS) });
                end = 0;
            }
            // Sostenido que ya llegó a 0: el release no tiene recorrido; su tramo toma el tiempo de 14 niveles (la tabla) para
            // que el asa de R quede en su sitio y no encima de la de ST
            const auto tR = end > 0 ? timeOf (end, 14, rU) : (float) rU * unit;
            st.push_back ({ tR, 0.30f, 217.0f * unit * 15.0f / 14.0f, end > 0 ? line (end, 0, 14, rU, tR) : std::function<float (float)> ([] (float) { return 0.0f; }) });
            return st;
        };
        e.readout = [] (const EnvRead& v)
        {
            const auto dg = [&v] (const char* id) { return juce::String (juce::roundToInt (v (id))); };
            const auto st = VL1::sustainUnits[juce::jlimit (0, 9, juce::roundToInt (v ("vl1SustainTime")))];
            const auto sl = VL1::sustainLevel[juce::jlimit (0, 9, juce::roundToInt (v ("vl1SustainLevel")))];
            // Tiempo real del sostenido: de SL a 0 a 15 niveles por sustainUnits (en ticks de 12 ms)
            const auto tS = st <= 0 ? 0.0f : std::ceil ((float) sl * (float) st / 15.0f - 1.0e-4f) * 0.012f;
            return "A" + dg ("vl1Attack") + " D" + dg ("vl1Decay") + " SL" + dg ("vl1SustainLevel") + " ST" + dg ("vl1SustainTime") + " R" + dg ("vl1Release")
                 + "\nSustain " + (st < 0 ? juce::String ("held") : "fades in " + envTimeText (tS));
        };
        e.handles = { { "vl1Attack", {}, 0, "Drag left/right." }, { "vl1Decay", "vl1SustainLevel", 1, "Drag: left/right = Decay, up/down = Sustain Level." },
                      { "vl1SustainTime", {}, 2, "Drag left/right (all the way right: held)." }, { "vl1Release", {}, -1, "Drag left/right." } };
        e.tag = "12 ms STEPS";
        e.tip = "The VL-1 envelope: 15 levels on a 12 ms clock, from the ROM tables. With Sustain Time below 9 the note fades "
                "while held; the release starts from where the sustain is. Drag the points.";
        return e;
    }

    // Gen X-2 (Engines/Stylophone.h, genxSample): ataque lineal 0 -> 1 en Attack, decay exponencial hasta -60 dB en Decay
    // (Decay 0 = sin decay). Mueve el tono (Pitch) y el corte del filtro; el VCA es de puerta
    static EnvSpec genx()
    {
        EnvSpec e;
        e.ids = { "genxAttack", "genxDecay" };
        e.stages = [] (const EnvRead& v)
        {
            const auto a = juce::jmax (0.001f, v ("genxAttack")), d = v ("genxDecay");
            std::vector<EnvStage> st { { a, 0.40f, 2.0f, [] (float f) { return f; } } };
            if (d < 0.0005f) st.push_back ({ 0.0f, 0.55f, 0.0f, [] (float) { return 1.0f; }, true });
            else st.push_back ({ d, 0.55f, 4.0f, [] (float f) { return std::pow (0.001f, f); } });
            return st;
        };
        e.readout = [] (const EnvRead& v)
        {
            const auto d = v ("genxDecay");
            return "A " + envTimeText (juce::jmax (0.001f, v ("genxAttack"))) + "\nD " + (d < 0.0005f ? juce::String ("off") : envTimeText (d) + " to -60dB");
        };
        e.handles = { { "genxAttack", {}, 0, "Drag left/right." }, { "genxDecay", {}, 1, "Drag left/right." } };
        e.tag = "PITCH / FILTER";
        e.tip = "The attack-decay envelope that moves the pitch (Pitch) and the filter cutoff; the amplifier opens with the "
                "key. Drag the points.";
        return e;
    }

    // Operador OPL (Engines/Opl.h, envelope = OPL3_EnvelopeCalc): atenuación de 9 bits (0,1875 dB por unidad; apagado desde
    // 0x1f8 = 94,5 dB). Tiempos por rate (4·R + KS) medidos con una simulación exacta de ese código a 49 716 Hz: decay de 0 a
    // 0x1f8 en 41,48 s · 2^(1 - rate/4) / (1 + (rate%4)/4) (rate 4: 41 482 ms; 60-63: 2,53 ms) y ataque en 2,8425 s con la
    // misma ley (rate 4: 2842 ms; ±10 % por encima de rate 52; 60-63: inmediato). R = 0: no avanza. KS a C4 (bloque 4,
    // ksv = 8: KSR on 8, off 2; supuesto de bloque). Ataque: la curva de la iteración rout += ~rout >> 3 del chip
    static EnvSpec oplOperator (int op)
    {
        EnvSpec e;
        const auto pre = "opl" + juce::String (op);
        const auto AR = pre + "Ar", DR = pre + "Dr", SL = pre + "Sl", RR = pre + "Rr", EGT = pre + "Egt", KSR = pre + "Ksr";
        e.ids = { AR, DR, SL, RR, EGT, KSR };
        // Tiempo de un recorrido completo (s) para un registro de rate 0..15; infinito con 0
        const auto fullTime = [] (const EnvRead& v, const juce::String& id, const juce::String& ksr, float base)
        {
            const auto reg = juce::roundToInt (v (id));
            if (reg == 0) return std::numeric_limits<float>::infinity();
            const auto rate = juce::jmin (63, 4 * reg + (v (ksr) > 0.5f ? 8 : 2));
            return base * std::exp2 (1.0f - (float) (rate >> 2)) / (1.0f + (float) (rate & 3) / 4.0f);
        };
        e.stages = [=] (const EnvRead& v)
        {
            static const std::vector<int> attackCurve = []
            {
                std::vector<int> c { 0x1ff };
                for (int rout = 0x1ff; rout != 0 && c.size() < 200;) { rout = (rout + (~rout >> 3)) & 0x1ff; c.push_back (rout); }
                return c;
            }();
            const auto amp = [] (float units) { return units >= 504.0f ? 0.0f : std::pow (10.0f, -0.1875f * units / 20.0f); };
            const auto sl = juce::roundToInt (v (SL));
            const auto slU = (float) (sl == 15 ? 0x1f : sl) * 16.0f;
            const auto reg = juce::roundToInt (v (AR));
            const auto rateA = juce::jmin (63, 4 * reg + (v (KSR) > 0.5f ? 8 : 2));
            const auto tA = reg == 0 ? std::numeric_limits<float>::infinity() : rateA >= 60 ? 0.0f : fullTime (v, AR, KSR, 2.8425f);
            // Parte de un recorrido completo; 0 unidades = 0 s aunque el rate sea 0 (evita 0 · inf)
            const auto part = [] (float full, float units) { return units <= 0.0f ? 0.0f : full * units / 504.0f; };
            const auto tD = part (fullTime (v, DR, KSR, 41.482f), slU);
            const auto tR = fullTime (v, RR, KSR, 41.482f);
            std::vector<EnvStage> st;
            st.push_back ({ tA, 0.22f, 2.85f, [=] (float f)
            {
                if (reg == 0) return 0.0f;
                return amp ((float) attackCurve[(size_t) juce::jlimit (0, (int) attackCurve.size() - 1, juce::roundToInt (f * (float) (attackCurve.size() - 1)))]);
            } });
            // DR = 0 con SL > 0: el decay no avanza (se queda arriba)
            const auto dStuck = ! std::isfinite (tD);
            st.push_back ({ dStuck ? 41.5f : tD, 0.28f, 41.5f, [=] (float f) { return dStuck ? 1.0f : amp (slU * f); } });
            const auto holdLevel = dStuck ? 0.0f : slU;
            if (v (EGT) > 0.5f)
                st.push_back ({ 0.0f, 0.16f, 0.0f, [=] (float) { return amp (holdLevel); }, true });
            else // sin EGT el sostenido sigue bajando con RR mientras la tecla está pulsada
                st.push_back ({ part (tR, 504.0f - holdLevel), 0.16f, 41.5f, [=] (float f) { return amp (holdLevel + (504.0f - holdLevel) * f); } });
            const auto from = v (EGT) > 0.5f ? holdLevel : 504.0f;
            // Sin EGT el sostenido ya llegó al apagado: el release no tiene recorrido; su tramo toma el tiempo completo de RR para
            // que el asa no quede encima de la anterior
            st.push_back ({ from >= 504.0f ? tR : part (tR, 504.0f - from), 0.32f, 41.5f, [=] (float f) { return amp (from + (504.0f - from) * f); } });
            return st;
        };
        e.readout = [=] (const EnvRead& v)
        {
            const auto sl = juce::roundToInt (v (SL));
            return "AR" + juce::String (juce::roundToInt (v (AR))) + " DR" + juce::String (juce::roundToInt (v (DR))) + " RR" + juce::String (juce::roundToInt (v (RR)))
                 + "\nSL -" + juce::String ((sl == 15 ? 31 : sl) * 3) + "dB" + (v (EGT) > 0.5f ? "" : " no EGT");
        };
        e.handles = { { AR, {}, 0, "Drag left/right." }, { DR, SL, 1, "Drag: left/right = DR, up/down = SL." }, { RR, {}, -1, "Drag left/right." } };
        e.tag = "C4";
        e.tip = "The operator envelope with the chip's rates (times at C4, with KSR). Level in amplitude: the chip works in dB, "
                "so decays look exponential. Drag the points.";
        return e;
    }
}

//==============================================================================
// Curva de transferencia estática de una saturación (entrada -> salida, ±range en los dos ejes), con la diagonal 1:1 de
// referencia. No dibuja lo que depende del tiempo (ADAA, sobremuestreo, bloqueadores de continua, sample & hold). Asas en
// coordenadas normalizadas del dibujo; al arrastrar, la misma búsqueda en la rejilla del parámetro que StageEnvView
struct ShaperSpec
{
    struct Handle { juce::String x, y; std::function<juce::Point<float> (const EnvRead&)> pos; juce::String hint; }; // pos: 0..1, y hacia arriba
    std::vector<juce::String> ids;
    float range = 1.0f;
    std::function<float (float, const EnvRead&)> curve;
    std::function<bool (const EnvRead&)> on;
    std::function<juce::String (const EnvRead&)> readout;
    std::vector<Handle> handles;
    juce::String tip;
};

class ShaperView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ShaperView (APVTS& st, ShaperSpec sp) : s (st), spec (std::move (sp))
    {
        setTooltip (spec.tip);
        for (size_t k = 0; k < spec.handles.size(); ++k)
        {
            const auto& h = spec.handles[k];
            auto dot = std::make_unique<ParamDot> (s, h.x.isEmpty() ? nullptr : h.x.toRawUTF8(), h.y.isEmpty() ? nullptr : h.y.toRawUTF8(), h.hint);
            dot->onDrag = [this, k] (juce::Point<float> p) { drag (k, p); };
            addAndMakeVisible (*dot);
            dots.push_back (std::move (dot));
        }
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        const auto screen = getLocalBounds().toFloat();
        Skin::screen (g, screen, 6.0f);
        const auto r = plot();
        const auto trace = Skin::trace();
        const auto read = reader();
        const auto on = spec.on (read);
        g.setColour (trace.withAlpha (0.12f));
        g.drawVerticalLine (juce::roundToInt (r.getCentreX()), r.getY(), r.getBottom());
        g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
        g.setColour (trace.withAlpha (0.25f));
        g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f); // 1:1
        const auto R = spec.range;
        juce::Path p;
        constexpr int n = 240; // los escalones de Crush a 4 bits (15 pasos) se ven
        for (int i = 0; i <= n; ++i)
        {
            const auto x = -R + 2.0f * R * (float) i / n;
            const auto y = juce::jlimit (-R, R, spec.curve (x, read));
            const juce::Point<float> pt { r.getX() + r.getWidth() * (x + R) / (2.0f * R), r.getCentreY() - 0.5f * r.getHeight() * y / R };
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        const auto c = on ? trace : trace.withAlpha (0.4f);
        g.setColour (c.withAlpha (0.18f * c.getFloatAlpha()));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.8f));
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        g.setColour (trace.withAlpha (0.85f));
        auto text = screen.reduced (9.0f, 6.0f);
        const auto lines = juce::StringArray::fromLines (spec.readout (read));
        for (int i = 0; i < juce::jmin (2, lines.size()); ++i)
            g.drawText (lines[i], text.withTrimmedTop (12.0f * (float) i), juce::Justification::topLeft, false);
        g.setColour (trace.withAlpha (0.55f));
        g.drawText (on ? "IN -> OUT" : "OFF", text, juce::Justification::bottomRight, false);
        Skin::glass (g, screen, 6.0f);
    }

    void resized() override { place(); }

private:
    EnvRead reader (const juce::String& over = {}, float value = 0.0f) const
    {
        return [this, over, value] (const juce::String& id) { return id == over ? value : s.getRawParameterValue (id)->load(); };
    }
    // Cuadrado centrado bajo la banda de lectura de dos líneas
    juce::Rectangle<float> plot() const
    {
        const auto a = getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (18.0f);
        const auto side = juce::jmin (a.getWidth(), a.getHeight());
        return a.withSizeKeepingCentre (side, side);
    }
    juce::Point<float> toScreen (juce::Point<float> u) const
    {
        const auto r = plot();
        return { r.getX() + u.x * r.getWidth(), r.getBottom() - u.y * r.getHeight() };
    }

    void search (juce::RangedAudioParameter* p, const juce::String& id, const std::function<float (const EnvRead&)>& cost)
    {
        if (p == nullptr) return;
        const auto steps = juce::jlimit (2, 1001, p->getNumSteps());
        auto best = p->getValue();
        auto bestCost = std::numeric_limits<float>::max();
        for (int k = 0; k < steps; ++k)
        {
            const auto u = (float) k / (float) (steps - 1);
            const auto c = cost (reader (id, p->convertFrom0to1 (u)));
            if (c < bestCost - 1.0e-6f) { bestCost = c; best = u; }
        }
        if (std::abs (best - p->getValue()) > 1.0e-6f) p->setValueNotifyingHost (best);
    }

    void drag (size_t k, juce::Point<float> p)
    {
        const auto& h = spec.handles[k];
        if (h.x.isNotEmpty()) search (dots[k]->px, h.x, [&] (const EnvRead& v) { return std::abs (toScreen (h.pos (v)).x - p.x); });
        if (h.y.isNotEmpty()) search (dots[k]->py, h.y, [&] (const EnvRead& v) { return std::abs (toScreen (h.pos (v)).y - p.y); });
    }

    void place()
    {
        const auto read = reader();
        for (size_t k = 0; k < dots.size(); ++k)
            dots[k]->setBounds (juce::Rectangle<int> (20, 20).withCentre (toScreen (spec.handles[k].pos (read)).roundToInt()));
    }

    void timerCallback() override
    {
        std::uint64_t h = 0;
        for (const auto& id : spec.ids) h = h * 1000003u ^ (std::uint64_t) std::hash<float>{} (s.getRawParameterValue (id)->load());
        if (h != lastHash) { lastHash = h; place(); repaint(); }
    }

    APVTS& s;
    ShaperSpec spec;
    std::vector<std::unique_ptr<ParamDot>> dots;
    std::uint64_t lastHash = 1;
};

namespace Shapers
{
    // Saturación por voz del Synth (Dsp/VoiceSaturator.h): salida = x + wet · (makeup · curva (g·x) - x), g = Drive en dB,
    // wet = Drive / 3 dB (fundido de entrada), makeup para el mismo pico a pico con entrada de pico 1. Diode: el bloqueador de
    // 10 Hz quita además la continua que deja la asimetría (no se dibuja). Entrada ±1,5 (un oscilador al 100 % = ±1)
    static ShaperSpec voiceSat()
    {
        ShaperSpec e;
        e.ids = { "voiceSatMode", "voiceSatDrive" };
        e.range = 1.5f;
        e.curve = [] (float x, const EnvRead& v)
        {
            const auto m = juce::roundToInt (v ("voiceSatMode"));
            const auto db = v ("voiceSatDrive");
            const auto g = (double) juce::Decibels::decibelsToGain (db);
            const auto wet = juce::jlimit (0.0f, 1.0f, db / 3.0f);
            const auto sat = (float) (VoiceSat::makeup (m, g) * VoiceSat::curve (m, g * (double) x));
            return x + wet * (sat - x);
        };
        e.on = [] (const EnvRead& v) { return v ("voiceSatDrive") >= 0.05f; };
        e.readout = [] (const EnvRead& v)
        {
            static const char* names[] = { "SOFT", "DIODE", "HARD", "FOLD" };
            const auto db = v ("voiceSatDrive");
            return juce::String (names[juce::jlimit (0, 3, juce::roundToInt (v ("voiceSatMode")))]) + "\n"
                 + (db < 0.05f ? juce::String ("Off") : "+" + juce::String (db, 1) + " dB");
        };
        e.handles = { { "voiceSatDrive", {}, [] (const EnvRead& v) { return juce::Point<float> (v ("voiceSatDrive") / 36.0f, 0.04f); },
                        "Drag left/right: Drive." } };
        e.tip = "Per-voice saturation: output against input (an oscillator at 100 % reaches 1). Drag the point on the bottom "
                "rail for the drive; the curve is chosen in the menu.";
        return e;
    }

    // Válvula (KoniSynth.cpp, processTube): (tanh (g·x + B) - tanh B) · comp, comp = 0,3 / pico medio de ±0,3; entrada ±0,6
    // (las voces suman unos ±0,3). Sin el filtro de continua de la salida
    static ShaperSpec tube()
    {
        ShaperSpec e;
        e.ids = { "tubeOn", "tubeDrive", "tubeBias" };
        e.range = 0.6f;
        e.curve = [] (float x, const EnvRead& v)
        {
            const auto g = juce::Decibels::decibelsToGain (v ("tubeDrive")), B = v ("tubeBias");
            const auto comp = 0.3f / (0.5f * (std::tanh (0.3f * g + B) - std::tanh (-0.3f * g + B)));
            return (std::tanh (g * x + B) - std::tanh (B)) * comp;
        };
        e.on = [] (const EnvRead& v) { return v ("tubeOn") > 0.5f; };
        e.readout = [] (const EnvRead& v)
        {
            return "+" + juce::String (v ("tubeDrive"), 1) + " dB\nBias " + juce::String (juce::roundToInt (v ("tubeBias") * 100.0f)) + "%";
        };
        e.handles = { { "tubeDrive", "tubeBias", [] (const EnvRead& v) { return juce::Point<float> (v ("tubeDrive") / 24.0f, v ("tubeBias")); },
                        "Drag: left/right = Drive, up/down = Bias." } };
        e.tip = "Tube: output against input. Bias bends the curve (even harmonics). Drag the point: left/right = Drive, "
                "up/down = Bias.";
        return e;
    }

    // Crush (KoniSynth.cpp, grupo fxColor): x·drive recortado a ±1, cuantizado a 2^bits niveles y devuelto entre drive, mezclado
    // con Mix. La reducción de tasa (Rate) es temporal y no se dibuja
    static ShaperSpec crush()
    {
        ShaperSpec e;
        e.ids = { "crushOn", "crushBits", "crushDrive", "crushMix", "crushRate" };
        e.range = 1.0f;
        e.curve = [] (float x, const EnvRead& v)
        {
            const auto steps = std::exp2 (v ("crushBits")) - 1.0f;
            const auto drive = juce::Decibels::decibelsToGain (v ("crushDrive"));
            const auto c = juce::jlimit (-1.0f, 1.0f, x * drive);
            const auto q = (std::round ((c + 1.0f) * 0.5f * steps) / steps * 2.0f - 1.0f) / drive;
            return x + v ("crushMix") * (q - x);
        };
        e.on = [] (const EnvRead& v) { return v ("crushOn") > 0.5f; };
        e.readout = [] (const EnvRead& v)
        {
            return juce::String (juce::roundToInt (v ("crushBits"))) + " bit  +" + juce::String (v ("crushDrive"), 1) + " dB\n"
                 + juce::String (v ("crushRate") / 1000.0f, 1) + " kHz";
        };
        e.handles = { { "crushDrive", "crushBits", [] (const EnvRead& v) { return juce::Point<float> (v ("crushDrive") / 24.0f, (v ("crushBits") - 1.0f) / 15.0f); },
                        "Drag: left/right = Drive, up/down = Bits." } };
        e.tip = "Crush: the quantizer steps, output against input (Mix included; the rate reduction is not drawn). Drag the point: "
                "left/right = Drive, up/down = Bits.";
        return e;
    }
}

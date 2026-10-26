// Filtro del Synth en la interfaz: FilterSim pasa una señal por el mismo VoiceFilter del motor (Dsp/Filters.h) con la misma
// etapa de entrada (Drive con tanh, Clean Drive, compensación del ladder, escala 0,5 / x2: Engines/Synth.h, runFilter), y
// FilterView dibuja su respuesta en frecuencia con un asa para Cutoff (horizontal) y Reso (vertical).
// Supuestos de la vista, los del display de onda: Do3 (nota 48, 130,81 Hz) a 48 kHz, velocity 100 y la envolvente de filtro
// en su nivel de sostenido; sin LFO ni matriz, sin tolerancia de corte por voz y con el ladder Classic (los modos
// sobremuestreados cambian por encima del corte: Synth.h). Se incluye desde KoniSynth.cpp (mismo TU), antes de UI/WaveView.h.
#pragma once

struct FilterSim
{
    static constexpr double fs = 48000.0, f0 = 130.81;

    struct Settings
    {
        bool on = false, clean = false;
        int mode = 0;
        float k = 0.0f, comp = 1.0f, drive = 1.0f, norm = 1.0f, wet = 0.0f, spread = 1.0f;
        float cutoff = 20000.0f, fc = 20000.0f;  // Cutoff del mando y corte que suena en Do3 (Key y envolvente en sostenido)
        float keyEnvFactor = 1.0f;               // fc = cutoff · keyEnvFactor (acotado a 20 Hz .. 0,45·fs)
        float envOct = 0.0f, fcPeak = 20000.0f;  // octavas de la envolvente a velocity 100 y corte en su pico (ataque)
    };

    static Settings read (APVTS& s)
    {
        const auto v = [&s] (const char* id) { return s.getRawParameterValue (id)->load(); };
        Settings st;
        st.on = v ("filterOn") > 0.5f;
        st.mode = (int) v ("filterMode");
        st.k = 4.4f * v ("resonance");
        st.comp = st.mode < VoiceFilters::svfLp ? 1.0f + 0.5f * st.k : 1.0f;
        st.drive = juce::Decibels::decibelsToGain (26.0f * v ("filterDrive"));
        st.norm = 1.0f / std::sqrt (st.drive);
        st.clean = v ("filterCleanDrive") > 0.5f;
        st.wet = juce::jmin (1.0f, 4.0f * v ("filterDrive"));
        st.spread = v ("filterSpread");
        st.cutoff = v ("cutoff");
        const auto velAmount = v ("velToFilter");
        st.envOct = 5.0f * v ("filterEnv") * (1.0f - velAmount + velAmount * (100.0f / 127.0f));
        // Do3: Key resta una octava al 100 % (keyOctaves = (48 - 60)/12, Synth.h)
        st.keyEnvFactor = std::exp2 (-v ("filterKey") + st.envOct * v ("filterSustain"));
        st.fc = juce::jlimit (20.0f, 0.45f * (float) fs, st.cutoff * st.keyEnvFactor);
        st.fcPeak = juce::jlimit (20.0f, 0.45f * (float) fs, st.cutoff * std::exp2 (-v ("filterKey") + st.envOct));
        return st;
    }

    // Una muestra por la etapa de entrada y el filtro, como runFilter en Synth.h
    static float run (VoiceFilters::VoiceFilter& f, const Settings& st, float x, float g)
    {
        auto in = std::tanh (st.drive * 0.5f * x) * st.norm;
        if (st.clean)
            in = 0.5f * x + st.wet * (in - 0.5f * x);
        return 2.0f * f.process (st.comp * in, g, st.k, st.mode, st.fc, (float) fs, st.spread);
    }

    // Estado estable: 40 ciclos de Do3 por el filtro; devuelve los ciclos 38..40 (2 ciclos) en 'columns' + 1 puntos.
    // src(t): la fuente en el tiempo t medido en ciclos de Do3. Coste: ~14 700 muestras (estimado < 1 ms)
    static std::vector<float> steady (const Settings& st, const std::function<float (double)>& src, int columns)
    {
        auto f = std::make_unique<VoiceFilters::VoiceFilter>(); // el peine reserva 64 KB: en el montón
        const auto g = std::tan (juce::MathConstants<float>::pi * st.fc / (float) fs);
        const auto period = fs / f0;
        const auto first = (int) std::floor (38.0 * period), total = (int) std::ceil (40.0 * period) + 2;
        std::vector<float> tail ((size_t) (total - first));
        for (int i = 0; i < total; ++i)
        {
            const auto y = run (*f, st, src ((double) i / period), g);
            if (i >= first) tail[(size_t) (i - first)] = y;
        }
        std::vector<float> out ((size_t) columns + 1);
        for (int c = 0; c <= columns; ++c)
        {
            const auto pos = (38.0 + 2.0 * c / columns) * period - first; // índice fraccionario en 'tail'
            const auto i = juce::jlimit (0, (int) tail.size() - 2, (int) std::floor (pos));
            const auto t = (float) (pos - i);
            out[(size_t) c] = tail[(size_t) i] + t * (tail[(size_t) i + 1] - tail[(size_t) i]);
        }
        return out;
    }

    // Respuesta en frecuencia (dB) en las frecuencias pedidas: respuesta al impulso de 2^15 muestras (0,68 s) con amplitud
    // 1e-4 (región lineal de las tanh) y FFT. Sin la etapa de Drive: la curva es la del filtro (con la compensación del
    // ladder). Resolución 1,46 Hz; el peine con Reso alta decae más despacio que 0,68 s: sus picos salen algo romos
    static std::vector<float> responseDb (Settings st, float fc, const std::vector<float>& freqs)
    {
        constexpr int order = 15, n = 1 << order;
        constexpr float a = 1.0e-4f;
        st.fc = fc;
        auto f = std::make_unique<VoiceFilters::VoiceFilter>();
        const auto g = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        std::vector<float> buf ((size_t) (2 * n), 0.0f);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = 2.0f * f->process (st.comp * 0.5f * (i == 0 ? a : 0.0f), g, st.k, st.mode, fc, (float) fs, st.spread) / a;
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        std::vector<float> db;
        db.reserve (freqs.size());
        for (const auto hz : freqs)
        {
            const auto bin = (double) hz / fs * n;
            const auto i = juce::jlimit (0, n / 2 - 1, (int) bin);
            const auto t = (float) (bin - i);
            const auto m = buf[(size_t) i] + t * (buf[(size_t) i + 1] - buf[(size_t) i]);
            db.push_back (20.0f * std::log10 (juce::jmax (1.0e-6f, m)));
        }
        return db;
    }
};

//==============================================================================
// Mini-display del filtro (a la izquierda de sus perillas): respuesta en frecuencia de 20 Hz a 20 kHz (log) y de -48 a
// +24 dB, banda del barrido de la envolvente (del sostenido al pico) y asa: horizontal = Cutoff (el que suena en Do3),
// vertical = Reso. Menú del asa: el de Cutoff (Reso tiene su perilla)
class FilterView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit FilterView (KoniSynthProcessor& p) : s (p.apvts), handle (*this)
    {
        setTooltip ("Filter response at C3, velocity 100, with the filter envelope at its sustain level. Drag the point: "
                    "left/right = Cutoff, up/down = Resonance. Shaded: the envelope sweep up to its peak.");
        addAndMakeVisible (handle);
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        const auto screen = getLocalBounds().toFloat();
        Skin::screen (g, screen, 6.0f);
        const auto r = plot();
        const auto trace = Skin::trace();
        g.saveState();
        g.reduceClipRegion (screen.reduced (4.0f).toNearestInt());
        for (const auto hz : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (trace.withAlpha (0.14f));
            g.drawVerticalLine (juce::roundToInt (xOf (hz)), r.getY(), r.getBottom());
        }
        for (const auto dB : { 0.0f, -24.0f })
        {
            g.setColour (trace.withAlpha (dB == 0.0f ? 0.22f : 0.10f));
            g.drawHorizontalLine (juce::roundToInt (yOf (dB)), r.getX(), r.getRight());
        }
        if (st.on && std::abs (st.envOct) > 0.01f) // barrido de la envolvente
        {
            const auto x0 = xOf (st.fc), x1 = xOf (st.fcPeak);
            g.setColour (Skin::accent().withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x0, x1), r.getY(), juce::jmax (x0, x1), r.getBottom()));
        }
        if (! curve.empty())
        {
            juce::Path p;
            for (size_t i = 0; i < curve.size(); ++i)
            {
                const auto x = r.getX() + r.getWidth() * (float) i / (float) (curve.size() - 1);
                const auto y = yOf (st.on ? curve[i] : 0.0f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (trace.withAlpha (0.18f));
            g.strokePath (p, juce::PathStrokeType (5.0f));
            g.setColour (trace.withAlpha (st.on ? 1.0f : 0.4f));
            g.strokePath (p, juce::PathStrokeType (1.8f));
        }
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        g.setColour (trace.withAlpha (0.85f));
        const auto fcText = st.fc >= 1000.0f ? juce::String (st.fc / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (st.fc)) + " Hz";
        auto text = screen.reduced (9.0f, 6.0f);
        g.drawText (st.on ? modeName + "  " + fcText + "  RES " + juce::String (juce::roundToInt (st.k / 4.4f * 100.0f)) + "%" : juce::String ("BYPASS"),
                    text, juce::Justification::topLeft, false);
        g.setColour (trace.withAlpha (0.55f));
        g.drawText ("C3 VEL 100 SUSTAIN", text, juce::Justification::bottomRight, false);
        g.restoreState();
        Skin::glass (g, screen, 6.0f);
    }

    void resized() override { recompute(); }

private:
    struct Handle : juce::Component, juce::SettableTooltipClient
    {
        explicit Handle (FilterView& o) : owner (o), cutoff (o.s.getParameter ("cutoff")), reso (o.s.getParameter ("resonance"))
        {
            setTooltip (paramTooltip (o.s, "cutoff") + "\nDrag: left/right = Cutoff, up/down = Resonance. Right-click: Cutoff menu.");
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            menu = std::make_unique<ParamMenu> (o.s, "cutoff", *this);
        }
        void paint (juce::Graphics& g) override
        {
            const auto c = getLocalBounds().toFloat().getCentre();
            const auto e = juce::Rectangle<float> (12.0f, 12.0f).withCentre (c);
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
            cutoff->beginChangeGesture();
            reso->beginChangeGesture();
            mouseDrag (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging) return;
            const auto at = e.getEventRelativeTo (&owner).position;
            const auto r = owner.plot();
            // El asa marca el corte que suena en Do3: el mando es ese corte sin Key ni envolvente
            const auto hz = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (at.x - r.getX()) / r.getWidth()));
            cutoff->setValueNotifyingHost (cutoff->convertTo0to1 (juce::jlimit (20.0f, 20000.0f, hz / owner.st.keyEnvFactor)));
            reso->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (r.getBottom() - at.y) / r.getHeight()));
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (! dragging) return;
            dragging = false;
            cutoff->endChangeGesture();
            reso->endChangeGesture();
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            for (auto* p : { cutoff, reso })
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
        }
        FilterView& owner;
        juce::RangedAudioParameter *cutoff, *reso;
        std::unique_ptr<ParamMenu> menu;
        bool dragging = false;
    };

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 18.0f); }
    float xOf (float hz) const { const auto r = plot(); return r.getX() + r.getWidth() * std::log10 (juce::jmax (20.0f, hz) / 20.0f) / 3.0f; }
    float yOf (float dB) const { const auto r = plot(); return r.getY() + r.getHeight() * (24.0f - juce::jlimit (-48.0f, 24.0f, dB)) / 72.0f; }

    void recompute()
    {
        st = FilterSim::read (s);
        if (auto* p = s.getParameter ("filterMode")) modeName = p->getCurrentValueAsText().toUpperCase();
        const auto n = juce::jmax (32, (int) plot().getWidth() / 2);
        std::vector<float> freqs ((size_t) n);
        for (int i = 0; i < n; ++i) freqs[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (n - 1));
        curve = FilterSim::responseDb (st, st.fc, freqs);
        // Asa en el corte, a la altura de la Reso (todo el alto = 0..100 %)
        const auto r = plot();
        handle.setBounds (juce::Rectangle<int> (22, 22).withCentre ({ juce::roundToInt (xOf (st.fc)),
                                                                     juce::roundToInt (r.getBottom() - st.k / 4.4f * r.getHeight()) }));
        repaint();
    }

    void timerCallback() override
    {
        static const char* ids[] = { "filterOn", "filterMode", "cutoff", "resonance", "filterDrive", "filterCleanDrive", "filterSpread",
                                     "filterKey", "filterEnv", "filterSustain", "velToFilter" };
        std::uint64_t h = 0;
        for (const auto* id : ids)
            h = h * 1000003u ^ (std::uint64_t) std::hash<float>{} (s.getRawParameterValue (id)->load());
        if (h != lastHash) { lastHash = h; recompute(); }
    }

    APVTS& s;
    FilterSim::Settings st;
    juce::String modeName;
    std::vector<float> curve;
    Handle handle;
    std::uint64_t lastHash = 0;
};

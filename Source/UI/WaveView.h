// Display de onda del Synth, a la izquierda de las pestañas de fuentes. Dibuja la fuente de la pestaña elegida con las
// funciones del propio motor (oscillator y TriShape de Dsp/OscillatorCore.h, el resumen de la wavetable y del sample del
// procesador) y lleva asas sobre la traza que mueven parámetros: PW, Tri Sym, Tri Rnd, WT Position, Sample Start / Length /
// Loop, String Pick / Pickup. Cada asa es un componente con tooltip, menú de parámetro (MIDI Learn, Set value, Reset),
// doble clic = valor por defecto y gesto de automatización. Las perillas siguen en sus secciones: el display no sustituye
// ningún control (Tests/test_ui_reach.py). Vistas: una por fuente, en el orden de las pestañas, y MIX
// Se incluye desde KoniSynth.cpp (mismo TU), tras UI/SourceTabs.h.
#pragma once

class WaveView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum View { osc1, wavetable, sample, string, osc2, osc3, noise, mix, numViews };

    explicit WaveView (KoniSynthProcessor& p) : koni (p), s (p.apvts)
    {
        setTooltip ("The selected source, drawn with the engine's own waveform. Drag the markers on the trace (right-click: "
                    "MIDI Learn, Set value, Reset); the knobs stay in the sections. The labels on top choose the view.");
        // Asas: parámetro, forma y vista en la que aparecen
        addHandle ("pulseWidth", Handle::circle);
        addHandle ("osc1TriSym", Handle::circle);
        addHandle ("osc1TriRound", Handle::diamond);
        addHandle ("osc2TriSym", Handle::circle);
        addHandle ("osc2TriRound", Handle::diamond);
        addHandle ("osc3TriSym", Handle::circle);
        addHandle ("osc3TriRound", Handle::diamond);
        addHandle ("wtPosition", Handle::circle);
        addHandle ("osc2WtPosition", Handle::circle);
        addHandle ("sampleStart", Handle::bar);
        addHandle ("sampleLength", Handle::bar);
        addHandle ("sampleLoop", Handle::button);
        addHandle ("stringPick", Handle::circle);
        addHandle ("stringPickup", Handle::bar);
        addHandle ("stringDecay", Handle::level);
        // Más mandos sobre la traza: nivel de cada fuente (borde izquierdo, arrastre vertical), Spread del unísono (el punto
        // de la derecha), profundidad de PWM (borde de la banda) y el semitono de Osc 2 / Osc 3 (fin del primer ciclo)
        for (const auto* id : { "osc1Level", "wtLevel", "sampleLevel", "stringLevel", "osc2Level", "subLevel", "noiseLevel" })
            addHandle (id, Handle::level);
        addHandle ("unisonSpread", Handle::bar);
        addHandle ("pwmDepth", Handle::bar);
        addHandle ("osc2Semi", Handle::bar);
        addHandle ("osc3Semi", Handle::bar);
        startTimerHz (20);
    }

    // El editor sincroniza la vista con las pestañas: la pestaña elegida cambia la vista y una etiqueta de la vista elige
    // la pestaña (onViewPicked)
    std::function<void (int view)> onViewPicked;
    void setView (int v)
    {
        if (v == view) return;
        view = v;
        sampleWidth = -1; // el resumen del sample se pide de nuevo para la vista que lo use
        resized(); // asas y traza tras el filtro
        repaint();
    }
    int currentView() const { return view; }

    void paint (juce::Graphics& g) override
    {
        const auto accent = Skin::accent();
        // Etiquetas de vista y, tras MIX, el interruptor POST (la traza tras el filtro)
        for (int v = 0; v <= numViews; ++v)
        {
            const auto r = chipBounds (v).toFloat();
            const auto on = v == numViews ? showPost : v == view;
            g.setColour (on ? accent : accent.withAlpha (0.35f));
            if (on) g.fillRoundedRectangle (r, 3.0f);
            else    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (on ? juce::Colours::black : Skin::white().withAlpha (0.8f));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
            g.drawText (viewName (v), r, juce::Justification::centred);
        }

        // Pantalla: fondo, retícula y la vista
        const auto screen = plot().toFloat();
        Skin::screen (g, screen);
        g.saveState();
        g.reduceClipRegion (screen.toNearestInt());
        grid (g, screen);
        switch (view)
        {
            case osc1: case osc2: case osc3: paintOsc (g, screen); break;
            case wavetable: paintTable (g, screen, "wtPosition", 0.0f); break;
            case sample: paintSample (g, screen); break;
            case string: paintString (g, screen); break;
            case noise: paintNoise (g, screen); break;
            default: paintMix (g, screen); break;
        }
        paintPost (g);
        readout (g, screen);
        g.restoreState();
        Skin::glass (g, screen, 8.0f);
    }

    void resized() override { placeHandles(); updatePost(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (chipBounds (numViews).contains (e.getPosition()))
        {
            showPost = ! showPost;
            updatePost();
            repaint();
            return;
        }
        for (int v = 0; v < numViews; ++v)
            if (chipBounds (v).contains (e.getPosition()))
            {
                setView (v);
                if (onViewPicked) onViewPicked (v);
                return;
            }
    }

private:
    //==========================================================================
    // Asa: arrastre según la vista (la conversión posición -> valor la hace el WaveView), tooltip y menú del parámetro
    struct Handle : juce::Component, juce::SettableTooltipClient
    {
        enum Shape { circle, diamond, bar, button, level }; // bar: arrastre horizontal; level: vertical
        Handle (WaveView& o, const juce::String& id, Shape sh) : owner (o), paramId (id), shape (sh), param (o.s.getParameter (id))
        {
            setTooltip (paramTooltip (o.s, id) + "\nDrag on the display; right-click: MIDI Learn, Set value, Reset.");
            setMouseCursor (sh == bar ? juce::MouseCursor::LeftRightResizeCursor : sh == level ? juce::MouseCursor::UpDownResizeCursor
                          : sh == button ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
            menu = std::make_unique<ParamMenu> (o.s, id, *this);
            setViewportIgnoreDragFlag (true); // arrastrar el asa no desplaza el panel
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (2.0f);
            const auto accent = Skin::accent();
            const auto c = r.getCentre();
            if (shape == button)
            {
                const auto on = param->getValue() > 0.5f;
                g.setColour (on ? accent : juce::Colours::black.withAlpha (0.7f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (on ? juce::Colours::black : accent);
                g.drawRoundedRectangle (r, 3.0f, 1.2f);
                g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
                g.drawText ("LOOP", r, juce::Justification::centred);
                return;
            }
            juce::Path p;
            if (shape == circle) p.addEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
            else if (shape == diamond)
            {
                p.startNewSubPath (c.x, c.y - 7.0f); p.lineTo (c.x + 7.0f, c.y); p.lineTo (c.x, c.y + 7.0f); p.lineTo (c.x - 7.0f, c.y);
                p.closeSubPath();
            }
            else if (shape == level) p.addRoundedRectangle (juce::Rectangle<float> (18.0f, 10.0f).withCentre (c), 2.0f);
            else p.addRoundedRectangle (juce::Rectangle<float> (10.0f, 18.0f).withCentre (c), 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillPath (p);
            g.setColour (dragging || isMouseOver() ? accent.brighter (0.4f) : accent);
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return; // menú del parámetro (ParamMenu)
            if (shape == button)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
                param->endChangeGesture();
                return;
            }
            dragging = true;
            param->beginChangeGesture();
            drag (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (dragging) drag (e); }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (! dragging) return;
            dragging = false;
            param->endChangeGesture();
            repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || shape == button) return;
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
        }

        void drag (const juce::MouseEvent& e)
        {
            const auto at = e.getEventRelativeTo (&owner).position;
            const auto v = owner.valueAt (paramId, at);
            if (v >= 0.0f)
                param->setValueNotifyingHost (param->convertTo0to1 (v));
        }

        WaveView& owner;
        const juce::String paramId;
        const Shape shape;
        juce::RangedAudioParameter* param;
        std::unique_ptr<ParamMenu> menu;
        bool dragging = false;
    };

    void addHandle (const char* id, Handle::Shape shape)
    {
        auto& h = handles.emplace_back (std::make_unique<Handle> (*this, id, shape));
        addChildComponent (*h);
    }

    Handle* handle (const juce::String& id)
    {
        for (auto& h : handles)
            if (h->paramId == id) return h.get();
        return nullptr;
    }

    float value (const char* id) const { return s.getRawParameterValue (id)->load(); }
    int choice (const char* id) const { return (int) value (id); }

    //==========================================================================
    // Geometría
    static constexpr int chipH = 18, chipGap = 3;
    static const char* viewName (int v)
    {
        static const char* names[] = { "OSC 1", "WT", "SMP", "STR", "OSC 2", "OSC 3", "NOISE", "MIX", "POST FILTER" };
        return names[v];
    }
    juce::Rectangle<int> chipBounds (int v) const // v = numViews: el interruptor POST FILTER
    {
        static const int widths[] = { 46, 30, 36, 36, 46, 46, 50, 36, 90 };
        int x = 0;
        for (int i = 0; i < v; ++i) x += widths[i] + chipGap + (i == 3 || i == 6 ? 6 : i == 7 ? 14 : 0); // grupos A, B, MIX y POST
        return { x, 0, widths[v], chipH };
    }
    juce::Rectangle<int> plot() const { return getLocalBounds().withTrimmedTop (chipH + 5); }
    juce::Rectangle<float> inner() const { return plot().toFloat().reduced (14.0f, 16.0f); }

    // Onda de cada oscilador: código de forma del motor, ancho de pulso, triángulo, nivel y relación de frecuencia con Osc 1
    struct OscSpec { int wave; float pw; TriShape tri; float level; double ratio; const char *symId, *roundId; };
    OscSpec spec (int v) const
    {
        OscSpec o {};
        if (v == osc2)
        {
            o.wave = choice ("osc2Wave"); // osc2Wave usa los códigos del motor tal cual (4 Wavetable, 5 Sample)
            o.pw = value ("pulseWidth");
            o.tri.set (value ("osc2TriSym"), value ("osc2TriRound"));
            o.level = value ("osc2Level");
            o.ratio = std::exp2 ((double) value ("osc2Octave") + ((double) value ("osc2Semi") + (double) value ("osc2Fine") / 100.0) / 12.0);
            o.symId = "osc2TriSym"; o.roundId = "osc2TriRound";
        }
        else if (v == osc3)
        {
            o.wave = waveFromChoice (choice ("osc3Wave"));
            o.pw = 0.5f; // la cuadrada de Osc 3 va siempre al 50 % (Synth.h)
            o.tri.set (value ("osc3TriSym"), value ("osc3TriRound"));
            o.level = value ("subLevel");
            o.ratio = (choice ("subOctave") == 0 ? 0.5 : 0.25) * std::exp2 (((double) value ("osc3Semi") + (double) value ("osc3Fine") / 100.0) / 12.0);
            o.symId = "osc3TriSym"; o.roundId = "osc3TriRound";
        }
        else
        {
            o.wave = waveFromChoice (choice ("wave"));
            o.pw = value ("pulseWidth");
            o.tri.set (value ("osc1TriSym"), value ("osc1TriRound"));
            o.level = value ("osc1Level");
            o.ratio = 1.0;
            o.symId = "osc1TriSym"; o.roundId = "osc1TriRound";
        }
        return o;
    }
    static float sampleOsc (const OscSpec& o, double t) // t: fase en [0, 1)
    {
        const auto x = oscillator (o.wave, t, 0.0, o.pw, false);
        return o.tri.active && o.wave == triangle ? o.tri (x) : x;
    }

    // Dos ciclos de Osc 1 a lo ancho; amplitud a ±0,42 del alto (el triángulo con Sym llega a +1,15)
    static constexpr double cycles = 2.0;
    float yOf (const juce::Rectangle<float>& r, float v) const { return r.getCentreY() - v * r.getHeight() * 0.42f; }

    //==========================================================================
    // Vistas
    void grid (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto trace = Skin::trace();
        for (int i = 1; i < 8; ++i)
        {
            g.setColour (trace.withAlpha (i == 4 ? 0.20f : 0.08f));
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY(), r.getBottom());
        }
        for (int i = 1; i < 6; ++i)
        {
            g.setColour (trace.withAlpha (i == 3 ? 0.20f : 0.08f));
            g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * (float) i / 6.0f), r.getX(), r.getRight());
        }
    }

    static void strokeTrace (juce::Graphics& g, const juce::Path& p, float alpha = 1.0f)
    {
        const auto trace = Skin::trace();
        g.setColour (trace.withAlpha (0.18f * alpha));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); // halo del fósforo
        g.setColour (trace.withAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintOsc (juce::Graphics& g, juce::Rectangle<float> screen)
    {
        const auto o = spec (view);
        const auto r = inner();
        if (view == osc2 && o.wave == 4) { paintTable (g, screen, "osc2WtPosition", 0.0f); return; }
        if (view == osc2 && o.wave == 5) { paintSampleOverview (g, KoniSynthProcessor::osc2Sample, false); return; }
        const auto alpha = o.level > 0.0f ? 1.0f : 0.45f;

        // PWM (LFO 1 ± PWM Depth alrededor de PW, acotado a 5-95 %, Synth.h): banda de la zona que barre el flanco
        const auto usesPw = view != osc3 && waveUsesPw (o.wave);
        const auto cycleW = r.getWidth() / (float) cycles;
        if (usesPw && view == osc1 && value ("pwmDepth") > 0.0f)
        {
            const auto lo = juce::jlimit (0.05f, 0.95f, o.pw - value ("pwmDepth")), hi = juce::jlimit (0.05f, 0.95f, o.pw + value ("pwmDepth"));
            for (int c = 0; c < (int) cycles; ++c)
            {
                const auto x0 = r.getX() + cycleW * ((float) c + edgeOf (o.wave, lo)), x1 = r.getX() + cycleW * ((float) c + edgeOf (o.wave, hi));
                g.setColour (Skin::trace().withAlpha (0.10f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x0, x1), r.getY(), juce::jmax (x0, x1), r.getBottom()));
            }
        }

        // Traza a la amplitud de su nivel; debajo, la forma a nivel 100 % muy tenue (se lee aunque el nivel sea bajo)
        juce::Path p, full;
        const int n = juce::jmax (64, (int) r.getWidth());
        for (int i = 0; i <= n; ++i)
        {
            const auto t = cycles * (double) i / (double) n * o.ratio;
            const auto x = r.getX() + r.getWidth() * (float) i / (float) n;
            const auto v = sampleOsc (o, t - std::floor (t));
            if (i == 0) { p.startNewSubPath (x, yOf (r, o.level * v)); full.startNewSubPath (x, yOf (r, v)); }
            else        { p.lineTo (x, yOf (r, o.level * v)); full.lineTo (x, yOf (r, v)); }
        }
        g.setColour (Skin::trace().withAlpha (0.16f));
        g.strokePath (full, juce::PathStrokeType (1.0f));
        strokeTrace (g, p, alpha);

        // Unísono de Osc 1: un punto por oscilador en su posición de panorama (±Spread, Synth.h)
        if (view == osc1 && (int) value ("unisonVoices") > 1)
        {
            const auto n1 = (int) value ("unisonVoices");
            const auto spread = value ("unisonSpread");
            const auto y = screen.getBottom() - 30.0f; // por encima del nombre de la forma (abajo a la derecha)
            g.setColour (Skin::trace().withAlpha (0.25f));
            g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
            g.setColour (Skin::trace().withAlpha (0.9f));
            for (int u = 0; u < n1; ++u)
            {
                const auto pos = 2.0f * (float) u / (float) (n1 - 1) - 1.0f;
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getCentreX() + pos * spread * r.getWidth() * 0.5f, y }));
            }
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
            g.drawText ("L", juce::Rectangle<float> (screen.getX() + 3.0f, y - 6.0f, 10.0f, 12.0f), juce::Justification::centred);
            g.drawText ("R", juce::Rectangle<float> (screen.getRight() - 13.0f, y - 6.0f, 10.0f, 12.0f), juce::Justification::centred);
        }
    }

    // Flanco del pulso dentro del ciclo: la cuadrada sube a 0 y baja en t = d; las mezclas con pulso, alto desde t = 1 - d
    static float edgeOf (int wave, float d) { return wave == square ? d : 1.0f - d; }

    void paintTable (juce::Graphics& g, juce::Rectangle<float>, const char* posId, float)
    {
        const auto name = koni.getWavetableName();
        if (name != tableName || frames.empty())
        {
            tableName = name;
            frames = koni.getWavetableOverview (32, 256);
        }
        const auto r = inner().withTrimmedRight (40.0f);
        if (frames.empty())
        {
            g.setColour (Skin::trace().withAlpha (0.6f));
            g.drawText ("Generating wavetable...", r, juce::Justification::centred);
            return;
        }
        const auto n = (int) frames.size();
        const auto depthX = r.getWidth() * 0.22f, depthY = r.getHeight() * 0.35f;
        const auto w = r.getWidth() - depthX, h = r.getHeight() - depthY;
        const auto position = value (posId);
        const auto current = juce::roundToInt (position * (float) (n - 1));
        auto drawFrame = [&] (int k, float alpha, float thickness)
        {
            const auto t = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
            const auto x0 = r.getX() + t * depthX, y0 = r.getBottom() - h * 0.5f - t * depthY;
            juce::Path path;
            const auto& f = frames[(size_t) k];
            for (size_t i = 0; i < f.size(); ++i)
            {
                const auto x = x0 + w * (float) i / (float) (f.size() - 1);
                const auto y = y0 - f[i] * h * 0.42f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            if (thickness > 1.5f) strokeTrace (g, path);
            else { g.setColour (Skin::trace().withAlpha (alpha)); g.strokePath (path, juce::PathStrokeType (thickness)); }
        };
        for (int k = n - 1; k >= 0; --k)
            if (k != current) drawFrame (k, 0.16f, 1.0f);
        drawFrame (current, 1.0f, 2.2f);
        // Raíl de Position a la derecha
        const auto rail = railBounds();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (rail, 6.0f);
        g.setColour (Skin::trace().withAlpha (0.45f));
        g.drawRoundedRectangle (rail, 6.0f, 1.0f);
    }
    juce::Rectangle<float> railBounds() const { const auto r = inner(); return { r.getRight() - 14.0f, r.getY(), 12.0f, r.getHeight() }; }

    void paintSample (juce::Graphics& g, juce::Rectangle<float> screen) { paintSampleOverview (g, KoniSynthProcessor::sourceSample, true); juce::ignoreUnused (screen); }

    void paintSampleOverview (juce::Graphics& g, int which, bool marks)
    {
        const auto r = inner();
        const auto w = juce::jmax (1, (int) r.getWidth());
        if (w != sampleWidth || which != sampleWhich || koni.getSampleInfo (which) != sampleInfo)
        {
            sampleWidth = w;
            sampleWhich = which;
            sampleInfo = koni.getSampleInfo (which);
            overview = koni.getSampleOverview (w, sampleLength, sampleRate, which);
        }
        if (overview.empty() || sampleLength <= 0)
        {
            g.setColour (Skin::trace().withAlpha (0.6f));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
            g.drawText ("NO SAMPLE LOADED: Edit Sample... > Load", r, juce::Justification::centred);
            return;
        }
        // Fichero entero en el tiempo (mín/máx por columna), no un ciclo: un sample no es periódico
        juce::Path env;
        const auto mid = r.getCentreY(), half = r.getHeight() * 0.46f;
        for (size_t c = 0; c < overview.size(); ++c)
        {
            const auto x = r.getX() + (float) c * r.getWidth() / (float) overview.size();
            if (c == 0) env.startNewSubPath (x, mid - overview[c].second * half); else env.lineTo (x, mid - overview[c].second * half);
        }
        for (size_t c = overview.size(); c-- > 0;)
            env.lineTo (r.getX() + (float) c * r.getWidth() / (float) overview.size(), mid - overview[c].first * half);
        env.closeSubPath();
        g.setColour (Skin::trace().withAlpha (0.35f));
        g.fillPath (env);
        strokeTrace (g, env, 0.8f);
        if (! marks) return;
        // Fuera de Start .. fin, oscurecido; con Loop, la zona del bucle en el acento
        const auto [x0, x1] = sampleSpan();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX() - 14.0f, r.getY() - 16.0f, x0, r.getBottom() + 16.0f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, r.getY() - 16.0f, r.getRight() + 14.0f, r.getBottom() + 16.0f));
        if (value ("sampleLoop") > 0.5f)
        {
            g.setColour (Skin::accent().withAlpha (0.14f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()));
        }
        g.setColour (Skin::accent());
        g.fillRect (x0 - 1.0f, r.getY() - 16.0f, 2.0f, r.getHeight() + 32.0f);
        g.fillRect (x1 - 1.0f, r.getY() - 16.0f, 2.0f, r.getHeight() + 32.0f);
    }
    // x de Start y del fin (Length en ms; 0 = el sample entero) sobre el resumen
    std::pair<float, float> sampleSpan() const
    {
        const auto r = inner();
        const auto start = value ("sampleStart"), lengthMs = value ("sampleLength");
        auto endFrac = 1.0f;
        if (lengthMs >= 0.5f && sampleLength > 0)
            endFrac = juce::jmin (1.0f, start + (float) (0.001 * lengthMs * sampleRate / sampleLength));
        return { r.getX() + start * r.getWidth(), r.getX() + endFrac * r.getWidth() };
    }

    void paintString (juce::Graphics& g, juce::Rectangle<float>)
    {
        const auto r = inner();
        // Arriba, la cuerda en el instante del ataque: con Excite = Pick, un triángulo con el vértice en Pick Position (Help.h);
        // con Noise, una ráfaga de ruido. Abajo, la caída: Decay es el T60 (s), dibujado en una ventana de 1,5·T60
        const auto top = r.withHeight (r.getHeight() * 0.4f);
        const auto yRest = top.getBottom() - 6.0f;
        const auto pick = value ("stringPick");
        g.setColour (Skin::trace().withAlpha (0.25f));
        g.drawHorizontalLine (juce::roundToInt (yRest), top.getX(), top.getRight());
        juce::Path str;
        if (choice ("stringExcite") == 1)
        {
            str.startNewSubPath (top.getX(), yRest);
            str.lineTo (top.getX() + pick * top.getWidth(), top.getY() + 6.0f);
            str.lineTo (top.getRight(), yRest);
        }
        else
        {
            juce::Random rng (7);
            for (int i = 0; i <= 120; ++i)
            {
                const auto x = top.getX() + top.getWidth() * (float) i / 120.0f;
                const auto y = yRest - (i == 0 || i == 120 ? 0.0f : (rng.nextFloat() * 2.0f - 1.0f) * (top.getHeight() * 0.35f));
                if (i == 0) str.startNewSubPath (x, y); else str.lineTo (x, y);
            }
        }
        strokeTrace (g, str, value ("stringLevel") > 0.0f ? 1.0f : 0.45f);

        const auto bottom = r.withTrimmedTop (r.getHeight() * 0.5f);
        const auto t60 = value ("stringDecay");
        juce::Path decay;
        for (int i = 0; i <= 160; ++i)
        {
            const auto t = 1.5f * t60 * (float) i / 160.0f;
            const auto a = std::exp (-6.9078f * t / t60); // -60 dB en T60
            const auto x = bottom.getX() + bottom.getWidth() * (float) i / 160.0f;
            if (i == 0) decay.startNewSubPath (x, bottom.getBottom() - a * bottom.getHeight());
            else        decay.lineTo (x, bottom.getBottom() - a * bottom.getHeight());
        }
        strokeTrace (g, decay, 0.7f);
        g.setColour (Skin::trace().withAlpha (0.6f));
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        const auto xT60 = bottom.getX() + bottom.getWidth() / 1.5f;
        g.drawVerticalLine (juce::roundToInt (xT60), bottom.getY(), bottom.getBottom());
        g.drawText ("-60 dB", juce::Rectangle<float> (xT60 + 4.0f, bottom.getY(), 60.0f, 12.0f), juce::Justification::left);
    }

    void paintNoise (juce::Graphics& g, juce::Rectangle<float>)
    {
        // Los generadores del motor (Synth.h): blanco uniforme, rosa (filtro de Paul Kellet), LFSR de 15 bits largo o corto
        // a la frecuencia de C3 (130,81 Hz) y 48 kHz; semilla fija: la imagen no cambia
        const auto r = inner();
        const auto color = choice ("noiseColor");
        juce::Random rng (0x5EED);
        float pink[7] {};
        std::uint32_t lfsr = 1;
        double lfsrPhase = 0.0;
        const auto dt = 130.81 / 48000.0;
        juce::Path p;
        const int n = juce::jmax (64, (int) r.getWidth());
        for (int i = 0; i <= n; ++i)
        {
            const auto w = 2.0f * rng.nextFloat() - 1.0f;
            auto v = w;
            if (color >= 2)
            {
                const auto shortMode = color == 3;
                lfsrPhase += dt * (shortMode ? 93.0 : 8.0) * 4.0; // 4 muestras por columna
                while (lfsrPhase >= 1.0)
                {
                    lfsrPhase -= 1.0;
                    const auto fb = (lfsr ^ (lfsr >> (shortMode ? 6 : 1))) & 1u;
                    lfsr = (lfsr >> 1) | (fb << 14);
                }
                v = (lfsr & 1u) ? 0.577f : -0.577f;
            }
            else if (color == 1)
            {
                pink[0] = 0.99886f * pink[0] + w * 0.0555179f; pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
                pink[2] = 0.96900f * pink[2] + w * 0.1538520f; pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
                pink[4] = 0.55000f * pink[4] + w * 0.5329522f; pink[5] = -0.7616f * pink[5] - w * 0.0168980f;
                v = 0.3277f * (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362f);
                pink[6] = w * 0.115926f;
            }
            const auto x = r.getX() + r.getWidth() * (float) i / (float) n;
            const auto y = yOf (r, value ("noiseLevel") * v); // a la amplitud de su nivel, como los osciladores
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        strokeTrace (g, p, value ("noiseLevel") > 0.0f ? 0.9f : 0.4f);
    }

    // Fuente periódica de una vista, a su nivel, en función del tiempo t en ciclos de Osc 1 (Do3); vacía si la vista no es
    // periódica (sample, cuerda, ruido; Osc 2 en modo Sample). MIX: los osciladores que suenan (nivel > 0 y audibles con
    // Mute / Solo) y la wavetable con su fotograma actual
    std::function<float (double)> sourceFn (int v)
    {
        if (frames.empty()) frames = koni.getWavetableOverview (32, 256);
        const auto frameAt = [this] (const char* posId)
        {
            return frames.empty() ? std::vector<float>() : frames[(size_t) juce::roundToInt (value (posId) * (float) (frames.size() - 1))];
        };
        const auto table = [] (const std::vector<float>& f, double t)
        {
            const auto ph = t - std::floor (t);
            return f.empty() ? 0.0f : f[(size_t) juce::jmin ((int) f.size() - 1, (int) (ph * (double) f.size()))];
        };
        if (v == osc1 || v == osc2 || v == osc3)
        {
            const auto o = spec (v);
            if (v == osc2 && o.wave == 5) return {};
            if (v == osc2 && o.wave == 4)
                return [f = frameAt ("osc2WtPosition"), table, level = o.level, ratio = o.ratio] (double t) { return level * table (f, t * ratio); };
            return [o] (double t) { const auto ph = t * o.ratio; return o.level * sampleOsc (o, ph - std::floor (ph)); };
        }
        if (v == wavetable)
            return [f = frameAt ("wtPosition"), table, level = value ("wtLevel")] (double t) { return level * table (f, t); };
        if (v != mix) return {};
        const auto solo = koni.getParams().soloSource.load();
        const auto audible = [&] (int src, const char* muteId) { return solo >= 0 ? solo == src : value (muteId) < 0.5f; };
        std::array<OscSpec, 3> o { spec (osc1), spec (osc2), spec (osc3) };
        // Osc 2 en modo Wavetable (4) o Sample (5): fuera, como el sample
        const std::array<bool, 3> on { o[0].level > 0.0f && audible (Params::srcOsc1, "osc1Mute"),
                                       o[1].level > 0.0f && o[1].wave != 4 && o[1].wave != 5 && audible (Params::srcOsc2, "osc2Mute"),
                                       o[2].level > 0.0f && audible (Params::srcOsc3, "osc3Mute") };
        const auto wtLevel = value ("wtLevel") > 0.0f && audible (Params::srcWavetable, "wtMute") ? value ("wtLevel") : 0.0f;
        return [o, on, wtLevel, f = frameAt ("wtPosition"), table] (double t)
        {
            float y = 0.0f;
            for (size_t k = 0; k < 3; ++k)
                if (on[k]) { const auto ph = t * o[k].ratio; y += o[k].level * sampleOsc (o[k], ph - std::floor (ph)); }
            return y + wtLevel * table (f, t);
        };
    }

    // Traza tras el filtro: la fuente de la vista por el VoiceFilter del motor (FilterSim), en estado estable
    void updatePost()
    {
        post.clear();
        postLo.clear();
        postHi.clear();
        postSettings = FilterSim::read (s);
        if (! showPost || ! postSettings.on || inner().getWidth() < 8.0f) return;
        if (auto fn = sourceFn (view))
        {
            const auto columns = (int) inner().getWidth();
            post = FilterSim::steady (postSettings, fn, columns);
            // LFO con destino Filter: ±2·Depth octavas cada uno (Synth.h); los extremos del barrido, como trazas tenues
            if (const auto oct = lfoFilterOctaves(); oct > 0.01f)
                for (const auto sign : { -1.0f, 1.0f })
                {
                    auto st = postSettings;
                    st.fc = juce::jlimit (20.0f, 0.45f * (float) FilterSim::fs, st.fc * std::exp2 (sign * oct));
                    (sign < 0.0f ? postLo : postHi) = FilterSim::steady (st, fn, columns);
                }
        }
    }

    float lfoFilterOctaves() const
    {
        auto oct = 0.0f;
        if (value ("lfoOn") > 0.5f && choice ("lfoTarget") == 2) oct += 2.0f * value ("lfoDepth");
        if (value ("lfo2On") > 0.5f && choice ("lfo2Target") == 2) oct += 2.0f * value ("lfo2Depth");
        return oct;
    }
    float tremoloDepth() const // reducción máxima de amplitud de los LFO con destino Tremolo (se multiplican)
    {
        auto gain = 1.0f;
        if (value ("lfoOn") > 0.5f && choice ("lfoTarget") == 1) gain *= 1.0f - value ("lfoDepth");
        if (value ("lfo2On") > 0.5f && choice ("lfo2Target") == 1) gain *= 1.0f - value ("lfo2Depth");
        return 1.0f - gain;
    }

    void paintPost (juce::Graphics& g)
    {
        const auto r = inner();
        // Tremolo de los LFO: la amplitud baja hasta (1 - Depth); líneas discontinuas en ±ese nivel del pico de la fuente
        if (const auto trem = tremoloDepth(); trem > 0.001f && view != sample && view != string)
        {
            const auto lv = levelId (view) != nullptr ? value (levelId (view)) : 1.0f;
            const float dash[] = { 4.0f, 4.0f };
            g.setColour (Skin::accent().withAlpha (0.6f));
            for (const auto sign : { -1.0f, 1.0f })
            {
                const auto y = yOf (r, sign * lv * (1.0f - trem));
                g.drawDashedLine ({ r.getX(), y, r.getRight(), y }, dash, 2, 1.0f);
            }
        }
        if (post.empty()) return;
        const auto scale = view == mix ? 1.0f / juce::jmax (1.0f, mixPeak) : 1.0f; // MIX va normalizada a su pico
        const auto pathOf = [&] (const std::vector<float>& ys)
        {
            juce::Path p;
            for (size_t i = 0; i < ys.size(); ++i)
            {
                const auto x = r.getX() + r.getWidth() * (float) i / (float) (ys.size() - 1);
                const auto y = yOf (r, ys[i] * scale);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };
        for (const auto* range : { &postLo, &postHi }) // extremos del barrido del LFO sobre el corte
            if (! range->empty())
            {
                g.setColour (Skin::accent().withAlpha (0.35f));
                g.strokePath (pathOf (*range), juce::PathStrokeType (1.0f));
            }
        const auto p = pathOf (post);
        const auto c = Skin::accent();
        g.setColour (c.withAlpha (0.2f));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintMix (juce::Graphics& g, juce::Rectangle<float>)
    {
        // Sample, String y Noise no son periódicos: no entran (lo dice la lectura)
        const auto r = inner();
        const auto fn = sourceFn (mix);
        juce::Path p;
        const int n = juce::jmax (64, (int) r.getWidth());
        float peak = 1.0e-6f;
        std::vector<float> ys ((size_t) n + 1);
        for (int i = 0; i <= n; ++i)
        {
            const auto y = fn (cycles * (double) i / (double) n);
            ys[(size_t) i] = y;
            peak = juce::jmax (peak, std::abs (y));
        }
        // Normalizada a su pico (la lectura da el pico real)
        for (int i = 0; i <= n; ++i)
        {
            const auto x = r.getX() + r.getWidth() * (float) i / (float) n;
            const auto y = yOf (r, ys[(size_t) i] / juce::jmax (1.0f, peak));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        mixPeak = peak;
        strokeTrace (g, p);
    }

    //==========================================================================
    // Lecturas numéricas arriba a la derecha y el nombre de la forma abajo a la derecha
    void readout (juce::Graphics& g, juce::Rectangle<float> screen)
    {
        juce::StringArray lines;
        juce::String info;
        const auto pct = [this] (const char* id) { return juce::String (juce::roundToInt (value (id) * 100.0f)) + "%"; };
        const auto text = [this] (const char* id) { auto* p = s.getParameter (id); return p != nullptr ? p->getCurrentValueAsText() : juce::String(); };
        switch (view)
        {
            case osc1:
                lines.add ("VOICES " + juce::String ((int) value ("unisonVoices")) + " | DETUNE " + text ("unisonDetune") + " | SPREAD " + pct ("unisonSpread"));
                lines.add ((waveUsesPw (spec (osc1).wave) ? "PW " + pct ("pulseWidth") + " +/- " + pct ("pwmDepth") + " | " : juce::String())
                           + "DRIFT " + pct ("drift") + " | LEVEL " + pct ("osc1Level"));
                if (spec (osc1).wave == triangle) lines.add ("TRI SYM " + pct ("osc1TriSym") + " | RND " + pct ("osc1TriRound"));
                info = text ("wave");
                break;
            case osc2:
                lines.add ("OCT " + text ("osc2Octave") + " | SEMI " + text ("osc2Semi") + " | FINE " + text ("osc2Fine"));
                lines.add ("RATIO x" + juce::String (spec (osc2).ratio, 3) + " | LEVEL " + pct ("osc2Level"));
                if (spec (osc2).wave == 4) lines.add ("WT POS " + pct ("osc2WtPosition"));
                info = text ("osc2Wave");
                break;
            case osc3:
                lines.add (text ("subOctave") + " | SEMI " + text ("osc3Semi") + " | FINE " + text ("osc3Fine"));
                lines.add ("RATIO x" + juce::String (spec (osc3).ratio, 3) + " | LEVEL " + pct ("subLevel"));
                info = text ("osc3Wave");
                break;
            case wavetable:
                lines.add ("POSITION " + pct ("wtPosition") + " | LEVEL " + pct ("wtLevel"));
                info = koni.getWavetableName();
                break;
            case sample:
                lines.add ("START " + text ("sampleStart") + " | LENGTH " + text ("sampleLength") + " | LOOP " + juce::String (value ("sampleLoop") > 0.5f ? "ON" : "OFF"));
                lines.add ("ROOT " + text ("sampleRoot") + " | LEVEL " + pct ("sampleLevel"));
                info = sampleInfo.isEmpty() ? juce::String ("WHOLE FILE, TIME DOMAIN") : sampleInfo;
                break;
            case string:
                lines.add ("DECAY " + text ("stringDecay") + " (T60) | DAMPING " + pct ("stringDamping") + " | BRIGHT " + pct ("stringBright"));
                lines.add ("PICK " + pct ("stringPick") + " | PICKUP " + text ("stringPickup") + " | LEVEL " + pct ("stringLevel"));
                info = "EXCITE " + text ("stringExcite");
                break;
            case noise:
                lines.add ("LEVEL " + pct ("noiseLevel"));
                info = text ("noiseColor");
                break;
            default:
                lines.add ("PEAK " + juce::String (mixPeak, 2) + " (before the filter) | OSC 1-3 + WAVETABLE");
                info = "SAMPLE, STRING AND NOISE NOT DRAWN";
                break;
        }
        if (! post.empty())
        {
            const auto fc = postSettings.fc;
            lines.add ("POST FILTER " + text ("filterMode") + " " + (fc >= 1000.0f ? juce::String (fc / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (fc)) + " Hz")
                       + " (C3, vel 100, sustain)");
        }
        // Cada línea sobre una franja oscura: la traza pasa por debajo sin tapar el texto
        const juce::Font font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        g.setFont (font);
        auto area = screen.reduced (10.0f, 6.0f);
        for (const auto& l : lines)
        {
            const auto line = area.removeFromTop (14.0f);
            const auto w = juce::GlyphArrangement::getStringWidth (font, l) + 8.0f;
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRect (line.withLeft (line.getRight() - w).withTrimmedRight (-4.0f));
            g.setColour (Skin::trace().withAlpha (0.9f));
            g.drawText (l, line, juce::Justification::topRight, false);
        }
        g.setColour (Skin::trace().withAlpha (0.7f));
        g.drawText (info.toUpperCase(), screen.reduced (10.0f, 5.0f), juce::Justification::bottomRight, false);
    }

    //==========================================================================
    // Posición de cada asa en la vista actual (ocultas las que no aplican) y su conversión inversa
    void placeHandles()
    {
        for (auto& h : handles) h->setVisible (false);
        const auto r = inner();
        if (r.isEmpty()) return;
        const auto show = [this] (const char* id, juce::Point<float> c, int w = 22, int h = 22)
        {
            if (auto* hd = handle (id))
            {
                hd->setBounds (juce::Rectangle<int> (w, h).withCentre (c.roundToInt()));
                hd->setVisible (true);
            }
        };
        const auto cycleW = r.getWidth() / (float) cycles;
        if (view == osc1 || view == osc2 || view == osc3)
        {
            const auto o = spec (view);
            if (view == osc2 && o.wave == 4)
            {
                const auto rail = railBounds();
                show ("osc2WtPosition", { rail.getCentreX(), rail.getBottom() - value ("osc2WtPosition") * rail.getHeight() });
                show ("osc2Level", { r.getX() - 6.0f, yOf (r, value ("osc2Level")) }, 22, 16);
                return;
            }
            if (view != osc3 && waveUsesPw (o.wave))
                show ("pulseWidth", { r.getX() + cycleW * edgeOf (o.wave, o.pw), r.getY() - 2.0f });
            if (o.wave == triangle && o.ratio > 0.0)
            {
                // Sym: en el pico del primer ciclo (fase 0,5); Rnd: en el valle al final de ese ciclo (fase 1). Arrastre vertical
                const auto xPeak = r.getX() + (float) (0.5 / o.ratio) * cycleW, xValley = r.getX() + (float) (1.0 / o.ratio) * cycleW;
                show (o.symId, { juce::jmin (xPeak, r.getRight()), yOf (r, o.level * sampleOsc (o, 0.5)) });
                show (o.roundId, { juce::jmin (xValley, r.getRight()), yOf (r, o.level * sampleOsc (o, 0.0)) + 18.0f });
            }
            show (levelId (view), { r.getX() - 6.0f, yOf (r, o.level) }, 22, 16);
            if (view == osc1 && (int) value ("unisonVoices") > 1)
                show ("unisonSpread", { r.getCentreX() + value ("unisonSpread") * r.getWidth() * 0.5f, plot().toFloat().getBottom() - 30.0f }, 16, 22);
            if (view == osc1 && waveUsesPw (o.wave))
                show ("pwmDepth", { r.getX() + cycleW * edgeOf (o.wave, juce::jlimit (0.05f, 0.95f, o.pw + value ("pwmDepth"))), r.getBottom() + 2.0f }, 16, 22);
            if ((view == osc2 || view == osc3) && o.ratio > 0.0)
                show (view == osc2 ? "osc2Semi" : "osc3Semi", { juce::jmin (r.getRight(), r.getX() + cycleW / (float) o.ratio), r.getBottom() + 2.0f }, 16, 22);
        }
        else if (view == wavetable)
        {
            const auto rail = railBounds();
            show ("wtPosition", { rail.getCentreX(), rail.getBottom() - value ("wtPosition") * rail.getHeight() });
        }
        else if (view == sample && sampleLength > 0)
        {
            const auto [x0, x1] = sampleSpan();
            show ("sampleStart", { x0, r.getY() - 4.0f }, 16, 24);
            show ("sampleLength", { x1, r.getBottom() + 4.0f }, 16, 24);
            show ("sampleLoop", { plot().toFloat().getX() + 34.0f, plot().toFloat().getY() + 16.0f }, 50, 20);
        }
        else if (view == string)
        {
            const auto top = r.withHeight (r.getHeight() * 0.4f);
            show ("stringPick", { top.getX() + value ("stringPick") * top.getWidth(), top.getY() + 6.0f });
            show ("stringPickup", { top.getX() + value ("stringPickup") * top.getWidth(), top.getBottom() + 6.0f }, 16, 24);
            const auto bottom = r.withTrimmedTop (r.getHeight() * 0.5f);
            show ("stringDecay", { bottom.getX() + bottom.getWidth() / 1.5f, decayY (bottom, value ("stringDecay")) }, 22, 16);
        }
        // Nivel de las vistas que no son osciladores: en el borde izquierdo, todo el alto = 0..100 % (en las de oscilador va a
        // la altura del pico de la traza, arriba)
        if (view == wavetable || view == sample || view == string || view == noise)
            if (auto* lv = levelId (view))
                show (lv, { r.getX() - 6.0f, view == noise ? yOf (r, value (lv)) : r.getBottom() - value (lv) * r.getHeight() }, 22, 16);
    }

    // Nivel de cada vista (nullptr en MIX)
    static const char* levelId (int v)
    {
        static const char* ids[] = { "osc1Level", "wtLevel", "sampleLevel", "stringLevel", "osc2Level", "subLevel", "noiseLevel", nullptr };
        return ids[juce::jlimit (0, (int) numViews - 1, v)];
    }
    // Decay de la cuerda (T60, 0,1-20 s) en escala logarítmica sobre el alto de la mitad inferior: arriba = largo
    static float decayY (juce::Rectangle<float> b, float t60)
    {
        return b.getBottom() - b.getHeight() * std::log (juce::jlimit (0.1f, 20.0f, t60) / 0.1f) / std::log (200.0f);
    }

    // Valor real del parámetro para la posición del ratón (coordenadas del WaveView); < 0 = sin cambio
    float valueAt (const juce::String& id, juce::Point<float> at) const
    {
        const auto r = inner();
        const auto cycleW = r.getWidth() / (float) cycles;
        if (id == "pulseWidth")
        {
            const auto e = juce::jlimit (0.0f, 1.0f, (at.x - r.getX()) / cycleW);
            return juce::jlimit (0.05f, 0.95f, spec (view).wave == square ? e : 1.0f - e);
        }
        if (id.endsWith ("TriSym") || id.endsWith ("TriRound")) // arriba = más; todo el alto del área = 0..100 %
            return juce::jlimit (0.0f, 1.0f, (r.getBottom() - at.y) / r.getHeight());
        if (id == "wtPosition" || id == "osc2WtPosition")
        {
            const auto rail = railBounds();
            return juce::jlimit (0.0f, 1.0f, (rail.getBottom() - at.y) / rail.getHeight());
        }
        if (id == "sampleStart")
            return juce::jlimit (0.0f, 0.99f, (at.x - r.getX()) / r.getWidth());
        if (id == "sampleLength")
        {
            // Al borde derecho: 0 (el sample entero); si no, ms desde Start, dentro del rango del parámetro
            const auto frac = (at.x - r.getX()) / r.getWidth();
            if (frac >= 0.995f || sampleLength <= 0) return 0.0f;
            const auto ms = (frac - value ("sampleStart")) * (float) sampleLength / (float) sampleRate * 1000.0f;
            return juce::jlimit (1.0f, 2000.0f, ms);
        }
        if (levelId (view) != nullptr && id == levelId (view))
        {
            if (view == wavetable || view == sample || view == string)
                return juce::jlimit (0.0f, 1.0f, (r.getBottom() - at.y) / r.getHeight());
            return juce::jlimit (0.0f, 1.0f, (r.getCentreY() - at.y) / (r.getHeight() * 0.42f));
        }
        if (id == "unisonSpread")
            return juce::jlimit (0.0f, 1.0f, (at.x - r.getCentreX()) / (r.getWidth() * 0.5f));
        if (id == "pwmDepth") // solo el lado de arriba de PW: la banda es simétrica
        {
            const auto o = spec (view);
            const auto e = juce::jlimit (0.0f, 1.0f, (at.x - r.getX()) / cycleW);
            return juce::jlimit (0.0f, 0.45f, (o.wave == square ? e : 1.0f - e) - o.pw);
        }
        if (id == "osc2Semi" || id == "osc3Semi")
        {
            // Largo del primer ciclo -> relación con Osc 1 -> semitonos, quitando octava (o la del Sub) y Fine
            const auto semis = 12.0f * std::log2 (cycleW / juce::jmax (4.0f, at.x - r.getX()));
            if (id == "osc2Semi")
                return juce::jlimit (-12.0f, 12.0f, (float) juce::roundToInt (semis - 12.0f * value ("osc2Octave") - value ("osc2Fine") / 100.0f));
            const auto base = choice ("subOctave") == 0 ? -12.0f : -24.0f;
            return juce::jlimit (-24.0f, 24.0f, (float) juce::roundToInt (semis - base - value ("osc3Fine") / 100.0f));
        }
        if (id == "stringDecay")
        {
            const auto b = r.withTrimmedTop (r.getHeight() * 0.5f);
            return juce::jlimit (0.1f, 20.0f, 0.1f * std::pow (200.0f, (b.getBottom() - at.y) / b.getHeight()));
        }
        if (id == "stringPick")
            return juce::jlimit (0.02f, 0.5f, (at.x - r.getX()) / r.getWidth());
        if (id == "stringPickup")
            return juce::jlimit (0.0f, 0.5f, (at.x - r.getX()) / r.getWidth());
        return -1.0f;
    }

    void timerCallback() override
    {
        // Repinta y recoloca las asas si cambia algún valor que se dibuja (automatización, perillas, presets)
        auto h = (std::uint64_t) view;
        for (const auto* id : watched)
            if (auto* v = s.getRawParameterValue (id))
                h = h * 1000003u ^ (std::uint64_t) (std::hash<float>{} (v->load()));
        h ^= (std::uint64_t) std::hash<juce::String>{} (koni.getWavetableName()) ^ (std::uint64_t) std::hash<juce::String>{} (koni.getSampleInfo (KoniSynthProcessor::sourceSample)) * 31u;
        h ^= (std::uint64_t) (koni.getParams().soloSource.load() + 2) << 56;
        if (h != lastHash)
        {
            lastHash = h;
            if (view == mix && frames.empty()) frames = koni.getWavetableOverview (32, 256);
            placeHandles();
            updatePost();
            repaint();
        }
    }

    static constexpr const char* watched[] = {
        "wave", "osc1Level", "unisonVoices", "unisonDetune", "unisonSpread", "pulseWidth", "pwmDepth", "drift", "osc1TriSym", "osc1TriRound",
        "osc2Wave", "osc2Level", "osc2Octave", "osc2Semi", "osc2Fine", "osc2WtPosition", "osc2TriSym", "osc2TriRound",
        "osc3Wave", "subLevel", "subOctave", "osc3Semi", "osc3Fine", "osc3TriSym", "osc3TriRound", "noiseColor", "noiseLevel",
        "wtTable", "wtLevel", "wtPosition", "sampleLevel", "sampleStart", "sampleLength", "sampleLoop", "sampleRoot",
        "stringLevel", "stringDecay", "stringBright", "stringPick", "stringPickup", "stringExcite", "stringDamping",
        "osc1Mute", "wtMute", "sampleMute", "stringMute", "osc2Mute", "osc3Mute", "noiseMute",
        "filterOn", "filterMode", "cutoff", "resonance", "filterDrive", "filterCleanDrive", "filterSpread", "filterKey", "filterEnv",
        "filterSustain", "velToFilter", "lfoOn", "lfoTarget", "lfoDepth", "lfo2On", "lfo2Target", "lfo2Depth" };

    KoniSynthProcessor& koni;
    APVTS& s;
    std::vector<std::unique_ptr<Handle>> handles;
    int view = osc1;
    std::vector<std::vector<float>> frames;
    juce::String tableName, sampleInfo;
    std::vector<std::pair<float, float>> overview;
    int sampleWidth = -1, sampleWhich = -1, sampleLength = 0;
    double sampleRate = 44100.0;
    float mixPeak = 0.0f;
    std::uint64_t lastHash = 0;
    bool showPost = true;                // interruptor POST FILTER (estado de la vista, no se guarda)
    std::vector<float> post;             // traza tras el filtro (vacía: sin filtro o vista no periódica)
    std::vector<float> postLo, postHi;   // la misma en los extremos del barrido de los LFO sobre el corte
    FilterSim::Settings postSettings;
};

//==============================================================================
// Tira del recorrido de la señal del Synth, encima de las fuentes: fuentes A + Complex + fuentes B -> mezclador -> filtro
// -> saturación -> VCA -> efectos -> salida, con un LED en lo que actúa (fuente que suena, Complex, filtro y saturación
// encendidos) y, a la derecha, las modulaciones activas (envolvente de filtro, LFO 1/2 con su destino, rutas de la matriz).
// Solo informa: clic en una fuente = su pestaña y su vista en el display (onSourcePicked)
class SignalStrip : public Section, public juce::TooltipClient, private juce::Timer
{
public:
    explicit SignalStrip (KoniSynthProcessor& p) : Section ({}), koni (p), s (p.apvts)
    {
        hideFrame();
        stretchToFill = true;
        startTimerHz (10);
    }

    std::function<void (int source)> onSourcePicked; // Params::SourceIndex
    void setRoutes (int n) { if (n != routes) { routes = n; repaint(); } }

    float layoutWeight() const override { return 1.3f; }
    float naturalWidth() const override { return 300.0f; }

    void paint (juce::Graphics& g) override
    {
        chips.clear();
        const auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        g.setColour (Skin::c (0xff14171b).withAlpha (0.85f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Skin::c (0xff3a434d));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        const juce::Font font (juce::FontOptions (12.5f));
        const juce::Font small (juce::FontOptions (11.0f));
        auto x = r.getX() + 10.0f;
        const auto cy = r.getCentreY();
        const auto chipH = juce::jmin (22.0f, r.getHeight() - 6.0f);
        g.setFont (small);
        g.setColour (Skin::white().withAlpha (0.55f));
        g.drawText ("SIGNAL", juce::Rectangle<float> (x, r.getY(), 50.0f, r.getHeight()), juce::Justification::centredLeft);
        x += 52.0f;

        // Ficha: LED (-1 sin LED), texto, fuente a la que lleva (-1 ninguna) y tooltip
        const auto chip = [&] (const juce::String& text, int led, int source, const juce::String& tip, bool accent = false)
        {
            g.setFont (font);
            const auto w = juce::GlyphArrangement::getStringWidth (font, text) + (led >= 0 ? 26.0f : 16.0f);
            const juce::Rectangle<float> b (x, cy - chipH * 0.5f, w, chipH);
            const auto selected = source >= 0 && source == selectedSource;
            g.setColour (Skin::c (0xff2c333b));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (accent || selected ? Skin::accent() : Skin::c (0xff8e989b).withAlpha (0.6f));
            g.drawRoundedRectangle (b, 3.0f, selected ? 2.0f : 1.0f);
            auto text_ = b.withTrimmedLeft (8.0f);
            if (led >= 0)
            {
                const auto d = 9.0f;
                const auto l = juce::Rectangle<float> (d, d).withCentre ({ b.getX() + 10.0f, cy });
                if (led > 0) { g.setColour (juce::Colour (0xffff3030).withAlpha (0.35f)); g.fillEllipse (l.expanded (2.5f)); }
                g.setGradientFill (juce::ColourGradient (led > 0 ? juce::Colour (0xffff8a80) : juce::Colour (0xff5a1e1e), l.getX(), l.getY(),
                                                         led > 0 ? juce::Colour (0xffd01010) : juce::Colour (0xff2a0c0c), l.getRight(), l.getBottom(), true));
                g.fillEllipse (l);
                text_ = b.withTrimmedLeft (18.0f);
            }
            g.setColour (Skin::white().withAlpha (0.9f));
            g.drawText (text, text_, juce::Justification::centredLeft, false);
            chips.push_back ({ b, source, tip });
            x += w + 4.0f;
        };
        const auto glyph = [&] (const juce::String& t)
        {
            g.setFont (font);
            g.setColour (Skin::white().withAlpha (0.5f));
            g.drawText (t, juce::Rectangle<float> (x, r.getY(), 16.0f, r.getHeight()), juce::Justification::centred);
            x += 18.0f;
        };
        const auto outline = [&] (float x0)
        {
            const juce::Rectangle<float> b (x0 - 3.0f, cy - chipH * 0.5f - 3.0f, x - 4.0f - x0 + 6.0f, chipH + 6.0f);
            g.setColour (Skin::white().withAlpha (0.3f));
            const float dash[] = { 3.0f, 3.0f };
            juce::Path p; p.addRoundedRectangle (b, 4.0f);
            juce::Path dashed; juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dash, 2);
            g.fillPath (dashed);
        };

        static const char* names[] = { "Osc 1", "WT", "Sample", "String", "Osc 2", "Osc 3", "Noise" };
        static const char* levels[] = { "osc1Level", "wtLevel", "sampleLevel", "stringLevel", "osc2Level", "subLevel", "noiseLevel" };
        static const char* mutes[] = { "osc1Mute", "wtMute", "sampleMute", "stringMute", "osc2Mute", "osc3Mute", "noiseMute" };
        const auto solo = koni.getParams().soloSource.load();
        const auto sounding = [&] (int k) { return value (levels[k]) > 0.0f && (solo >= 0 ? solo == k : value (mutes[k]) < 0.5f); };
        const auto sourceTip = [] (int k) { return juce::String (names[k]) + ": LED lit while it sounds. Click: its tab and its view on the wave display."; };

        auto x0 = x;
        for (int k = 0; k < 4; ++k) chip (names[k], sounding (k) ? 1 : 0, k, sourceTip (k));
        outline (x0);
        x += 4.0f;
        glyph ("+");
        chip ("Complex", value ("complexOn") > 0.5f ? 1 : 0, -1, "Complex oscillator (FM / AM / ring on its own carrier): LED lit when it is on.");
        glyph ("+");
        x0 = x + 4.0f;
        x += 4.0f;
        for (int k = 4; k < 7; ++k) chip (names[k], sounding (k) ? 1 : 0, k, sourceTip (k));
        outline (x0);
        x += 4.0f;
        glyph (juce::String::fromUTF8 ("\xe2\x86\x92"));
        chip ("Mixer", -1, -1, "All sources are summed at their levels.", true);
        glyph (juce::String::fromUTF8 ("\xe2\x86\x92"));
        chip ("Filter " + text ("filterMode"), value ("filterOn") > 0.5f ? 1 : 0, -1, "Filter: LED lit when it is on (off = the sound goes straight through).");
        glyph (juce::String::fromUTF8 ("\xe2\x86\x92"));
        chip ("Sat", value ("voiceSatDrive") > 0.0f ? 1 : 0, -1, "Per-voice saturation after the filter (Sat knob, Saturation... dialog): LED lit above 0.");
        glyph (juce::String::fromUTF8 ("\xe2\x86\x92"));
        chip ("VCA " + text ("envCurve"), -1, -1, "Amplifier with the Amp Envelope (curve shown).");
        glyph (juce::String::fromUTF8 ("\xe2\x86\x92"));
        chip ("FX", -1, -1, "The effects chain: COLOR, AMP, DYNAMICS, MOD FX, ENSEMBLE, SPACE, OUTPUT blocks below.");
        glyph (juce::String::fromUTF8 ("\xe2\x86\x92"));
        chip ("Out", -1, -1, "Plugin output.");

        // Modulaciones activas, a la derecha (se recortan si no caben)
        juce::StringArray mods;
        if (value ("filterOn") > 0.5f && std::abs (value ("filterEnv")) > 0.0f) mods.add ("F.Env > Cutoff");
        if (value ("lfoOn") > 0.5f && value ("lfoDepth") > 0.0f) mods.add ("LFO 1 > " + text ("lfoTarget"));
        if (value ("lfo2On") > 0.5f && value ("lfo2Depth") > 0.0f) mods.add ("LFO 2 > " + text ("lfo2Target"));
        if (value ("pwmDepth") > 0.0f) mods.add ("LFO 1 > PW");
        if (routes > 0) mods.add ("Matrix: " + juce::String (routes) + (routes == 1 ? " route" : " routes"));
        g.setFont (small);
        g.setColour (Skin::white().withAlpha (0.6f));
        const auto modArea = juce::Rectangle<float>::leftTopRightBottom (x + 16.0f, r.getY(), r.getRight() - 10.0f, r.getBottom());
        if (modArea.getWidth() > 60.0f)
            g.drawFittedText ("MOD: " + (mods.isEmpty() ? juce::String ("none") : mods.joinIntoString ("  |  ")), modArea.toNearestInt(),
                              juce::Justification::centredRight, 1, 0.8f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (const auto& c : chips)
            if (c.source >= 0 && c.bounds.contains (e.position) && onSourcePicked)
            {
                selectedSource = c.source;
                onSourcePicked (c.source);
                repaint();
            }
    }

    juce::String getTooltip() override
    {
        const auto p = getMouseXYRelative().toFloat();
        for (const auto& c : chips)
            if (c.bounds.contains (p)) return c.tip;
        return "The signal path of the Synth engine, from the sources to the output, and the active modulations.";
    }

    void setSelectedSource (int k) { if (k != selectedSource) { selectedSource = k; repaint(); } }

private:
    float value (const char* id) const { return s.getRawParameterValue (id)->load(); }
    juce::String text (const char* id) const { auto* p = s.getParameter (id); return p != nullptr ? p->getCurrentValueAsText() : juce::String(); }

    void timerCallback() override
    {
        // Repinta si cambia algo de lo que se muestra
        static const char* ids[] = { "osc1Level", "wtLevel", "sampleLevel", "stringLevel", "osc2Level", "subLevel", "noiseLevel",
                                     "osc1Mute", "wtMute", "sampleMute", "stringMute", "osc2Mute", "osc3Mute", "noiseMute",
                                     "complexOn", "filterOn", "filterMode", "voiceSatDrive", "envCurve", "filterEnv",
                                     "lfoOn", "lfoDepth", "lfoTarget", "lfo2On", "lfo2Depth", "lfo2Target", "pwmDepth" };
        std::uint64_t h = (std::uint64_t) (koni.getParams().soloSource.load() + 2);
        for (const auto* id : ids)
            h = h * 1000003u ^ (std::uint64_t) std::hash<float>{} (value (id));
        if (h != lastHash) { lastHash = h; repaint(); }
    }

    struct Chip { juce::Rectangle<float> bounds; int source; juce::String tip; };
    KoniSynthProcessor& koni;
    APVTS& s;
    std::vector<Chip> chips;
    int routes = 0, selectedSource = 0;
    std::uint64_t lastHash = 0;
};

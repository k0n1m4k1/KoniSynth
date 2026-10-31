// Panel de conexiones (PATCH): clavijas y cables como en un panel de módulos. Es la vista y el mando de parámetros de
// enrutado que ya existen (qué modula a qué, qué se sincroniza, adónde va un LFO); las cantidades siguen siendo perillas.
// Genérico: un Model dice qué clavijas hay, qué cables resultan de los parámetros y qué hace conectar o desenchufar
// (complexPatch y lfoPatch, al final del fichero).
//  - Arrastrar de una salida a una entrada (o al revés) conecta, si el modelo lo admite.
//  - Arrastrar la clavija de una entrada y soltarla fuera la desenchufa (si es removible; si no, vuelve a su sitio); soltarla
//    en otra entrada la cambia de sitio.
//  - Cada cambio es un gesto del parámetro (un paso de deshacer); clic derecho en una clavija con parámetro: su menú
//    (MIDI Learn, Set value, Reset, macro).
// Al aparecer un cable, oscila ~0,35 s (solo dibujo). Los parámetros se sondean a 15 Hz (presets, automatización, MIDI).
// Se incluye desde KoniSynth.cpp (mismo TU), tras UI/Widgets.h.
#pragma once

class PatchBay : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    struct Jack
    {
        juce::String name, sub, tip; // rótulo, mando equivalente (bajo las entradas) y ayuda
        bool output = false;
        const char* menuParam = nullptr; // parámetro del menú contextual de esta clavija (o ninguno)
    };
    struct Cable { int out, in; juce::Colour colour; bool faint; };
    struct Model
    {
        juce::String title;
        std::vector<Jack> jacks;                                // las salidas, primero
        std::function<std::vector<Cable>()> cables;             // los cables según los parámetros
        std::function<bool (int out, int in)> connect;          // fija parámetros; false = conexión no válida
        std::function<bool (int out, int in)> removable;        // ese cable, soltado fuera, se quita (si no, vuelve a su sitio)
        std::function<void (int out, int in)> unplug;           // quitar ese cable
        std::function<bool (int jack)> visible;                 // clavijas ocultas en este contexto (opcional)
    };

    PatchBay (APVTS& s, Model m) : apvts (s), model (std::move (m))
    {
        setInterceptsMouseClicks (true, true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor); // solo donde hitTest acepta: las clavijas
        for (size_t j = 0; j < model.jacks.size(); ++j)
            if (model.jacks[j].menuParam != nullptr)
            {
                auto& h = handles.emplace_back (std::make_unique<JackHandle> (*this, (int) j));
                h->setTooltip (model.jacks[j].tip);
                addAndMakeVisible (*h);
                menus.push_back (std::make_unique<ParamMenu> (s, model.jacks[j].menuParam, *h));
            }
        jacks.resize (model.jacks.size());
        startTimerHz (15);
    }

    // Ancho que pide: 40 px de rótulos de fila y 52 por columna (la fila con más clavijas)
    int preferredWidth() const { return 40 + 52 * juce::jmax (1, columns()); }

    // Acoplado en su padre (oscilador complejo): de x = left hasta 10 px del borde derecho y de top hasta bottom px del de
    // abajo; todo el hueco que dejan los controles (Section::controlsWidth)
    void dockFrom (int left, int top, int bottom)
    {
        dock = { left, top, bottom };
        parentSizeChanged();
    }

    void parentSizeChanged() override
    {
        if (dock.left > 0)
            if (auto* p = getParentComponent())
                setBounds (dock.left, dock.top, juce::jmax (0, p->getWidth() - 10 - dock.left), juce::jmax (0, p->getHeight() - dock.top - dock.bottom));
    }

    bool hitTest (int x, int y) override { return nearestJack ({ (float) x, (float) y }, 9.0f * k) >= 0; }

    juce::String getTooltip() override
    {
        const auto j = nearestJack (getMouseXYRelative().toFloat(), 12.0f * k);
        return j >= 0 ? model.jacks[(size_t) j].tip : juce::String();
    }

    void resized() override
    {
        // Fila OUT arriba, fila IN debajo; cada fila reparte sus clavijas visibles en todo el ancho. En alto: OUT al 30 %
        // (mínimo: título, si lo hay, y rótulo encima) e IN al 64 % (debajo, sus dos líneas de rótulo). Escala k de clavijas,
        // cables y rótulos: lo que dé el sitio respecto al tamaño base (52 px por columna, 150 px de alto), entre 1 y 1,6
        const auto r = getLocalBounds().toFloat().reduced (3.0f); // 3 px de margen: los rótulos no tocan el borde de la placa
        const auto colW = (r.getWidth() - 40.0f) / (float) juce::jmax (1, columns());
        k = juce::jlimit (1.0f, 1.6f, juce::jmin (colW / 52.0f, r.getHeight() / 150.0f));
        const auto top = model.title.isEmpty() ? 4.0f : 18.0f;
        const auto outY = juce::jmax (top + 28.0f * k, r.getHeight() * 0.30f);
        // IN: sus dos líneas de rótulo (hasta 35·k px bajo la clavija) dentro de la placa, con su margen
        const auto inY = juce::jmin (juce::jmax (outY + 54.0f * k, r.getHeight() * 0.62f), r.getHeight() - 35.0f * k);
        for (const auto output : { true, false })
        {
            std::vector<int> row;
            for (size_t j = 0; j < model.jacks.size(); ++j)
                if (model.jacks[j].output == output && shown ((int) j)) row.push_back ((int) j);
            const auto w = (r.getWidth() - 40.0f) / (float) juce::jmax (1, (int) row.size());
            (output ? outLabelW : inLabelW) = w - 4.0f; // rótulos como mucho del ancho de su columna (no invaden la vecina)
            for (size_t n = 0; n < row.size(); ++n)
                jacks[(size_t) row[n]] = { r.getX() + 40.0f + w * ((float) n + 0.5f), r.getY() + (output ? outY : inY) };
        }
        const auto handle = juce::roundToInt (18.0f * k);
        for (auto& h : handles)
        {
            h->setVisible (shown (h->jack));
            h->setBounds (juce::Rectangle<int> (handle, handle).withCentre (jacks[(size_t) h->jack].roundToInt()));
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintPlate (g);
        for (size_t j = 0; j < jacks.size(); ++j)
            if (shown ((int) j)) paintJack (g, jacks[j]);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (const auto& c : current)
        {
            if (! shown (c.out) || ! shown (c.in) || (c.out == dragOut && c.in == pickedIn))
                continue;
            const auto it = wobbleStart.find (key (c));
            paintCable (g, jacks[(size_t) c.out], jacks[(size_t) c.in], c.colour.withMultipliedAlpha (c.faint ? 0.4f : 1.0f),
                        k * (model.jacks[(size_t) c.out].name == "OSC 1" ? mainWidth : cableWidth), it != wobbleStart.end() ? wobble (now, it->second) : 1.0f);
        }
        if (dragFrom >= 0) // cable en la mano: del conector fijo al ratón
            paintCable (g, jacks[(size_t) dragFrom], dragPoint, dragColour, k * cableWidth, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = dragOut = pickedIn = -1;
        if (e.mods.isPopupMenu())
            return;
        const auto j = nearestJack (e.position, 12.0f * k);
        if (j < 0) return;
        dragFrom = j;
        dragColour = Skin::white().withAlpha (0.8f);
        if (model.jacks[(size_t) j].output)
            dragOut = j;
        else // entrada con cable: se coge la clavija (la última dibujada); el cable cuelga de su salida
            for (const auto& c : current)
                if (c.in == j) { dragFrom = dragOut = c.out; pickedIn = j; dragColour = c.colour; }
        dragPoint = e.position;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0) return;
        dragPoint = e.position;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0) return;
        const auto target = nearestJack (e.position, 14.0f * k);
        const auto targetIn = target >= 0 && ! model.jacks[(size_t) target].output;
        if (pickedIn >= 0 && target != pickedIn && model.removable (dragOut, pickedIn) && (targetIn || target < 0))
            model.unplug (dragOut, pickedIn); // la clavija sale de su entrada (a otra entrada o fuera)
        if (target >= 0 && target != pickedIn)
        {
            const auto out = dragOut >= 0 ? dragOut : (model.jacks[(size_t) target].output ? target : -1);
            const auto in = targetIn ? target : (! model.jacks[(size_t) dragFrom].output ? dragFrom : -1);
            if (out >= 0 && in >= 0)
                model.connect (out, in);
        }
        dragFrom = dragOut = pickedIn = -1;
        timerCallback(); // el cambio se ve (y oscila) ya, sin esperar al temporizador
        repaint();
    }

    // Fija un parámetro con su gesto (un paso de deshacer); para los modelos
    static void setParam (APVTS& s, const char* id, float value)
    {
        if (auto* p = s.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
    static float param (APVTS& s, const char* id)
    {
        auto* v = s.getRawParameterValue (id);
        return v != nullptr ? v->load() : 0.0f;
    }

private:
    // Asa invisible sobre una clavija con parámetro: recibe el clic derecho (ParamMenu) y pasa el resto del ratón al panel
    struct JackHandle : juce::Component, juce::SettableTooltipClient
    {
        JackHandle (PatchBay& o, int j) : owner (o), jack (j) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            forwarding = ! e.mods.isPopupMenu();
            if (forwarding) owner.mouseDown (e.getEventRelativeTo (&owner));
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (forwarding) owner.mouseDrag (e.getEventRelativeTo (&owner)); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (forwarding) owner.mouseUp (e.getEventRelativeTo (&owner));
            forwarding = false;
        }
        PatchBay& owner;
        const int jack;
        bool forwarding = false;
    };

    static int key (const Cable& c) { return c.out * 64 + c.in; }
    bool shown (int j) const { return ! model.visible || model.visible (j); }

    int columns() const
    {
        int outs = 0, ins = 0;
        for (size_t j = 0; j < model.jacks.size(); ++j)
            if (shown ((int) j)) ++(model.jacks[j].output ? outs : ins);
        return juce::jmax (outs, ins);
    }

    int nearestJack (juce::Point<float> p, float radius) const
    {
        int best = -1;
        auto bestD = radius;
        for (size_t j = 0; j < jacks.size(); ++j)
        {
            if (! shown ((int) j)) continue;
            const auto d = p.getDistanceFrom (jacks[j]);
            if (d <= bestD) { bestD = d; best = (int) j; }
        }
        return best;
    }

    // Oscilación al conectar: 1 + 0,8·e^(-t/80 ms)·sin(2π·6 Hz·t) durante 350 ms (a los 350 ms la envolvente es e^-4,4 = 1,2 %)
    static float wobble (double nowMs, double startMs)
    {
        const auto t = (nowMs - startMs) / 1000.0;
        if (t < 0.0 || t > 0.35) return 1.0f;
        return (float) (1.0 + 0.8 * std::exp (-t / 0.08) * std::sin (juce::MathConstants<double>::twoPi * 6.0 * t));
    }

    static constexpr float mainWidth = 4.5f, cableWidth = 3.6f; // grosor del cable (px): el de OSC 1, más grueso

    // Placa serigrafiada: fondo más oscuro con tornillos en las esquinas, título, rótulos de fila y, bajo cada clavija, su
    // nombre (salidas en recuadro claro encima, como en los paneles de módulos; entradas debajo, con el mando equivalente)
    void paintPlate (juce::Graphics& g) const
    {
        const auto plate = getLocalBounds().toFloat();
        g.setColour (Skin::black().withAlpha (0.35f));
        g.fillRoundedRectangle (plate, 5.0f);
        g.setColour (Skin::white().withAlpha (0.12f));
        g.drawRoundedRectangle (plate.reduced (0.5f), 5.0f, 1.0f);
        for (auto c : { plate.getTopLeft() + juce::Point<float> (7.0f, 7.0f), plate.getTopRight() + juce::Point<float> (-7.0f, 7.0f),
                        plate.getBottomLeft() + juce::Point<float> (7.0f, -7.0f), plate.getBottomRight() + juce::Point<float> (-7.0f, -7.0f) })
        {
            const auto screw = juce::Rectangle<float> (6.0f, 6.0f).withCentre (c);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc8ccd0), screw.getX(), screw.getY(), juce::Colour (0xff50555a),
                                                     screw.getRight(), screw.getBottom(), false));
            g.fillEllipse (screw);
            g.setColour (juce::Colour (0xff30343a));
            g.drawLine ({ c.translated (-2.0f, 1.0f), c.translated (2.0f, -1.0f) }, 0.9f);
        }
        g.setColour (Skin::white().withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        if (model.title.isNotEmpty()) // sin título si ya lo lleva la sección (MOD PATCH)
            g.drawText (model.title, plate.withHeight (16.0f).withTrimmedLeft (16.0f).translated (0.0f, 3.0f), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (9.5f * k, juce::Font::bold));
        g.setColour (Skin::white().withAlpha (0.5f));
        for (const auto output : { true, false }) // rótulo de fila, una vez, a la altura de su primera clavija visible
            for (size_t j = 0; j < jacks.size(); ++j)
                if (shown ((int) j) && model.jacks[j].output == output)
                {
                    g.drawText (output ? "OUT" : "IN", juce::Rectangle<float> (6.0f, jacks[j].y - 6.0f * k, 34.0f, 12.0f * k),
                                juce::Justification::centredLeft);
                    break;
                }
        for (size_t j = 0; j < jacks.size(); ++j)
        {
            if (! shown ((int) j)) continue;
            const auto& d = model.jacks[j];
            const auto at = jacks[j];
            if (d.output) // encima de la clavija: los cables bajan desde ella y no lo tapan
            {
                const auto box = juce::Rectangle<float> (juce::jmin (46.0f * k, outLabelW), 12.0f * k).withCentre (at.translated (0.0f, -17.0f * k));
                g.setColour (Skin::white().withAlpha (0.78f));
                g.fillRoundedRectangle (box, 2.0f);
                g.setColour (Skin::black());
                g.setFont (juce::FontOptions (9.5f * k, juce::Font::bold));
                g.drawFittedText (d.name, box.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                continue;
            }
            g.setColour (Skin::white().withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.5f * k, juce::Font::bold));
            g.drawFittedText (d.name, juce::Rectangle<float> (juce::jmin (50.0f * k, inLabelW), 12.0f * k).withCentre (at.translated (0.0f, 17.0f * k)).toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);
            g.setColour (Skin::white().withAlpha (0.45f));
            g.setFont (juce::FontOptions (9.0f * k));
            g.drawFittedText (d.sub, juce::Rectangle<float> (juce::jmin (54.0f * k, inLabelW), 11.0f * k).withCentre (at.translated (0.0f, 29.0f * k)).toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);
        }
    }

    // Hembra del panel: tuerca metálica (degradado radial, luz arriba a la izquierda), aro de sombra y el agujero
    void paintJack (juce::Graphics& g, juce::Point<float> c) const
    {
        const auto nut = juce::Rectangle<float> (14.0f * k, 14.0f * k).withCentre (c);
        g.setColour (Skin::black().withAlpha (0.45f));
        g.fillEllipse (nut.translated (0.6f, 1.2f).expanded (0.8f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8dce0), nut.getX() + 3.0f, nut.getY() + 3.0f,
                                                 juce::Colour (0xff4a4f55), nut.getRight(), nut.getBottom(), true));
        g.fillEllipse (nut);
        g.setColour (juce::Colour (0xff22262a));
        g.drawEllipse (nut.reduced (0.5f), 0.8f);
        const auto hole = juce::Rectangle<float> (6.0f * k, 6.0f * k).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff000000), hole.getCentreX(), hole.getCentreY(),
                                                 juce::Colour (0xff30343a), hole.getRight(), hole.getBottom(), true));
        g.fillEllipse (hole);
    }

    // Cable colgante entre dos puntos: el vientre baja con la distancia (de 10 a 40 px), escalado por sag
    void paintCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour, float width, float sag) const
    {
        const auto drop = juce::jlimit (10.0f * k, 40.0f * k, 0.3f * a.getDistanceFrom (b)) * sag;
        const auto c1 = a + (b - a) / 3.0f + juce::Point<float> (0.0f, drop), c2 = a + (b - a) * (2.0f / 3.0f) + juce::Point<float> (0.0f, drop);
        juce::Path p;
        p.startNewSubPath (a);
        p.cubicTo (c1, c2, b);
        strokeCable (g, p, a, c1, b, c2, colour, width);
    }

    // Cable de goma con volumen: sombra proyectada difusa (dos trazos), borde oscuro, cuerpo, núcleo más claro desplazado
    // hacia la luz (arriba a la izquierda) y brillo especular fino. Después, las dos clavijas, orientadas hacia su punto de
    // control (la dirección por la que sale el cable)
    void strokeCable (juce::Graphics& g, const juce::Path& p, juce::Point<float> a, juce::Point<float> ca,
                             juce::Point<float> b, juce::Point<float> cb, juce::Colour colour, float width) const
    {
        const auto alpha = colour.getFloatAlpha();
        const auto stroke = [&] (float w, juce::Colour c, juce::Point<float> off)
        {
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::translation (off.x, off.y));
        };
        stroke (width + 6.0f, Skin::black().withAlpha (0.12f * alpha), { 2.0f, 4.0f });
        stroke (width + 2.5f, Skin::black().withAlpha (0.22f * alpha), { 1.2f, 2.6f });
        stroke (width + 1.6f, colour.darker (0.9f), {});
        stroke (width, colour, {});
        stroke (width * 0.45f, colour.brighter (0.45f).withMultipliedAlpha (0.8f), { -0.25f * width, -0.25f * width });
        stroke (0.9f, juce::Colours::white.withAlpha (0.45f * alpha), { -0.32f * width, -0.32f * width });
        paintPlug (g, a, ca, colour, width);
        paintPlug (g, b, cb, colour, width);
    }

    // Clavija enchufada vista desde delante: funda de alivio (del color del cable, se estrecha hasta el cable a lo largo de
    // su salida, con dos estrías), collar metálico sobre la hembra y capuchón del color del cable con reflejo
    void paintPlug (juce::Graphics& g, juce::Point<float> at, juce::Point<float> toward, juce::Colour colour, float width) const
    {
        const auto alpha = colour.getFloatAlpha();
        auto dir = toward - at;
        const auto len = dir.getDistanceFromOrigin();
        dir = len > 0.001f ? dir / len : juce::Point<float> (0.0f, 1.0f);
        const juce::Point<float> nrm (-dir.y, dir.x);
        // Funda: trapecio de 14 px, de 9 px de ancho junto al collar al ancho del cable
        const auto bootLen = 14.0f * k, w0 = 4.5f * k, w1 = 0.5f * width + 0.3f;
        const auto p0 = at + dir * (4.0f * k), p1 = at + dir * bootLen;
        juce::Path boot;
        boot.startNewSubPath (p0 + nrm * w0);
        boot.lineTo (p1 + nrm * w1);
        boot.lineTo (p1 - nrm * w1);
        boot.lineTo (p0 - nrm * w0);
        boot.closeSubPath();
        g.setColour (Skin::black().withAlpha (0.25f * alpha));
        g.fillPath (boot, juce::AffineTransform::translation (1.2f, 2.6f));
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.35f), p0 + nrm * w0, colour.darker (0.7f), p0 - nrm * w0, false));
        g.fillPath (boot);
        g.setColour (colour.darker (1.0f).withAlpha (0.6f * alpha));
        for (auto t : { 7.0f * k, 10.0f * k }) // estrías
        {
            const auto c = at + dir * t;
            const auto half = w0 + (w1 - w0) * (t - 4.0f * k) / (bootLen - 4.0f * k);
            g.drawLine ({ c + nrm * half, c - nrm * half }, 0.9f);
        }
        // Collar metálico (más ancho que la hembra) y capuchón
        const auto collar = juce::Rectangle<float> (15.0f * k, 15.0f * k).withCentre (at);
        g.setColour (Skin::black().withAlpha (0.4f * alpha));
        g.fillEllipse (collar.translated (0.8f, 1.8f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff0f2f4).withAlpha (alpha), collar.getX() + 3.0f, collar.getY() + 3.0f,
                                                 juce::Colour (0xff5a6068).withAlpha (alpha), collar.getRight(), collar.getBottom(), true));
        g.fillEllipse (collar);
        g.setColour (juce::Colour (0xff202428).withAlpha (alpha));
        g.drawEllipse (collar.reduced (0.4f), 0.8f);
        const auto cap = collar.reduced (3.5f * k);
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.6f), cap.getX() + 1.5f, cap.getY() + 1.5f,
                                                 colour.darker (0.8f), cap.getRight(), cap.getBottom(), true));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.55f * alpha));
        g.fillEllipse (juce::Rectangle<float> (2.6f, 2.0f).withCentre (cap.getPosition() + juce::Point<float> (2.6f, 2.4f)));
    }

    // Sondea los cables: los nuevos oscilan; si cambia la visibilidad (otro motor), recoloca. A 60 Hz solo mientras algo
    // oscila; si no, 15 Hz
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        auto next = model.cables();
        bool changed = next.size() != current.size();
        for (const auto& c : next)
        {
            const auto same = std::find_if (current.begin(), current.end(), [&] (const Cable& o) { return key (o) == key (c); });
            if (same == current.end()) { wobbleStart[key (c)] = now; changed = true; }
            else if (same->faint != c.faint) changed = true;
        }
        unsigned visibleMask = 0;
        for (size_t j = 0; j < model.jacks.size() && j < 32; ++j)
            visibleMask |= shown ((int) j) ? 1u << j : 0u;
        if (visibleMask != lastVisible) { lastVisible = visibleMask; resized(); changed = true; }
        if (changed)
        {
            current = std::move (next);
            repaint();
        }
        double latest = -1.0e9;
        for (const auto& w : wobbleStart) latest = juce::jmax (latest, w.second);
        const auto animating = now - latest < 400.0;
        if (animating) repaint();
        const auto hz = animating ? 60 : 15;
        if (getTimerInterval() != 1000 / hz) startTimerHz (hz);
    }

    APVTS& apvts;
    Model model;
    std::vector<juce::Point<float>> jacks;
    std::vector<std::unique_ptr<JackHandle>> handles; // antes que menus: los ParamMenu se destruyen primero
    std::vector<std::unique_ptr<ParamMenu>> menus;
    std::vector<Cable> current;
    std::map<int, double> wobbleStart;
    unsigned lastVisible = ~0u;
    struct Dock { int left = 0, top = 0, bottom = 0; } dock;
    float k = 1.0f; // escala de clavijas, cables y rótulos (resized)
    float outLabelW = 46.0f, inLabelW = 54.0f; // ancho máximo de los rótulos: el de la columna de su fila (resized)
    int dragFrom = -1, dragOut = -1, pickedIn = -1;
    juce::Colour dragColour;
    juce::Point<float> dragPoint;
};

// Oscilador complejo y sincronías de los osciladores. Salidas: OSC 1, OSC 2, OSC 3, NOISE, INT (el modulador interno: Mod
// Wave y Ratio). Entradas:
//  - CARR = complexOn: solo desde OSC 1 (la portadora).
//  - MOD = complexModSource (Internal, Osc 2, Osc 3, Noise): siempre tiene cable; quitarlo vuelve a INT.
//  - SYNC = modSync: Osc 1 se reinicia con el modulador; solo desde la salida que ya va a MOD (hay un único modulador) y no
//    desde NOISE (no tiene ciclo).
//  - SYNC 2 = osc2Sync, SYNC 3 = osc3Sync: Osc 2 / Osc 3 se reinician con Osc 1; solo desde OSC 1.
inline PatchBay::Model complexPatch (APVTS& s)
{
    enum { osc1, osc2, osc3, noise, intMod, carr, mod, sync, sync2, sync3 };
    const auto outOf = [] (int src) { return src == 1 ? (int) osc2 : src == 2 ? (int) osc3 : src == 3 ? (int) noise : (int) intMod; };
    PatchBay::Model m;
    m.title = "PATCH";
    m.jacks = {
        { "OSC 1", {}, "OSC 1 output. Drag a cable to CARR (Osc 1 goes through the complex oscillator) or to SYNC 2 / SYNC 3.", true },
        { "OSC 2", {}, "OSC 2 output. Drag a cable to MOD: Osc 2 modulates Osc 1 (FM, AM / ring).", true },
        { "OSC 3", {}, "OSC 3 output. Drag a cable to MOD: Osc 3 modulates Osc 1 (FM, AM / ring).", true },
        { "NOISE", {}, "NOISE output (its current colour). Drag a cable to MOD: noise modulates Osc 1 (noisy FM, AM / ring).", true },
        { "INT", {}, "INT: the internal modulator of the complex oscillator (Mod Wave and Ratio). Drag a cable to MOD.", true },
        { "CARR", "= On", "CARR (carrier): plugged from OSC 1 = Complex On; Osc 1 goes through the complex oscillator. Drag the "
                          "plug out to turn it off. Right-click: MIDI Learn, value, reset.", false, "complexOn" },
        { "MOD", "= Mod Src", "MOD: the modulator of Osc 1. INT = the internal one (Mod Wave and Ratio); OSC 2 / OSC 3 = that "
                              "oscillator as tuned in its tab; NOISE = the noise. Drag the plug out to go back to INT. Right-click: "
                              "MIDI Learn, value, reset.", false, "complexModSource" },
        { "SYNC", "= Sync", "SYNC: plugged = Osc 1 restarts its cycle with each cycle of the modulator (hard sync). Only from the "
                            "output plugged into MOD, not from NOISE. Drag the plug out to turn it off.", false, "modSync" },
        { "SYNC 2", "= Osc 2 Sync", "SYNC 2: plugged from OSC 1 = Osc 2 restarts with each cycle of Osc 1 (hard sync; raise "
                                    "the tuning of Osc 2 for the classic sync lead). Drag the plug out to turn it off.", false, "osc2Sync" },
        { "SYNC 3", "= Osc 3 Sync", "SYNC 3: plugged from OSC 1 = Osc 3 restarts with each cycle of Osc 1 (hard sync). Drag "
                                    "the plug out to turn it off.", false, "osc3Sync" } };
    const auto on = [&s] { return PatchBay::param (s, "complexOn") > 0.5f; };
    const auto src = [&s] { return juce::jlimit (0, 3, (int) PatchBay::param (s, "complexModSource")); };
    m.cables = [&s, on, src, outOf]
    {
        std::vector<PatchBay::Cable> c;
        const auto faint = ! on(); // con el complejo apagado, MOD y SYNC no actúan
        c.push_back ({ outOf (src()), mod, Skin::c (0xffe0a040), faint });
        if (PatchBay::param (s, "modSync") > 0.5f) c.push_back ({ outOf (src()), sync, Skin::c (0xff60c080), faint });
        if (PatchBay::param (s, "osc2Sync") > 0.5f) c.push_back ({ osc1, sync2, Skin::c (0xffb080e0), false });
        if (PatchBay::param (s, "osc3Sync") > 0.5f) c.push_back ({ osc1, sync3, Skin::c (0xffb080e0), false });
        if (on()) c.push_back ({ osc1, carr, Skin::accent(), false });
        return c;
    };
    m.connect = [&s, src, outOf] (int out, int in)
    {
        if (in == carr && out == osc1)                       PatchBay::setParam (s, "complexOn", 1.0f);
        else if (in == mod && out != osc1)                   PatchBay::setParam (s, "complexModSource", out == intMod ? 0.0f : out == osc2 ? 1.0f : out == osc3 ? 2.0f : 3.0f);
        else if (in == sync && out == outOf (src()) && out != noise) PatchBay::setParam (s, "modSync", 1.0f);
        else if (in == sync2 && out == osc1)                 PatchBay::setParam (s, "osc2Sync", 1.0f);
        else if (in == sync3 && out == osc1)                 PatchBay::setParam (s, "osc3Sync", 1.0f);
        else return false;
        return true;
    };
    m.removable = [] (int, int) { return true; };
    m.unplug = [&s] (int, int in)
    {
        static const std::map<int, const char*> ids { { carr, "complexOn" }, { mod, "complexModSource" }, { sync, "modSync" },
                                                      { sync2, "osc2Sync" }, { sync3, "osc3Sync" } };
        if (const auto it = ids.find (in); it != ids.end())
            PatchBay::setParam (s, it->second, 0.0f); // MOD: Internal
    };
    return m;
}

// Destinos de los LFO (lfoTarget, lfo2Target): cada LFO va siempre a un destino (un cable que se cambia de sitio, no se
// quita). LFO 1: PITCH (vibrato), AMP (trémolo) o CUTOFF; LFO 2, además, PWM, PITCH 2 (Osc 2) y WT POS. Cable atenuado con
// su LFO apagado. Además, LFO 1 -> PWM: la modulación del ancho de pulso de Osc 1 (Synth.h, pulseWidth + pwmDepth · LFO 1),
// que actúa aparte del destino de LFO 1 y con el LFO apagado; la perilla PWM de Osc 1 es su atenuador. El cable existe si
// pwmDepth > 0; conectarlo devuelve el último valor distinto de 0 (30 % la primera vez) y quitarlo lo pone a 0.
// withLfo2 false (Theremin): solo LFO 1, a PITCH o AMP
inline PatchBay::Model lfoPatch (APVTS& s, std::function<bool()> withLfo2)
{
    enum { lfo1, lfo2, pitch, amp, cutoff, pwm, pitch2, wtPos };
    PatchBay::Model m;
    // Sin título en la placa: ya lo lleva su sección ("Mod Patch")
    m.jacks = {
        { "LFO 1", {}, "LFO 1 output: drag its cable to PITCH, AMP or CUTOFF (LFO Target), and a second cable to PWM (pulse "
                       "width of Osc 1, amount = PWM knob). Right-click: MIDI Learn, value, reset of LFO Target.",
          true, "lfoTarget" },
        { "LFO 2", {}, "LFO 2 output: drag its cable to PITCH, AMP, CUTOFF, PWM, PITCH 2 or WT POS (LFO 2 Target). Right-click: "
                       "MIDI Learn, value, reset.", true, "lfo2Target" },
        { "PITCH", "vibrato", "PITCH: the LFO moves the pitch of the voice (vibrato)." },
        { "AMP", "tremolo", "AMP: the LFO moves the volume (tremolo)." },
        { "CUTOFF", "filter", "CUTOFF: the LFO moves the filter cutoff." },
        { "PWM", "pulse width", "PWM: the pulse width of Osc 1. From LFO 1: its amount is the PWM knob of Osc 1 (the "
                                "attenuator); drag the plug out to turn it off. From LFO 2: LFO 2 Target = PWM, amount = its Depth." },
        { "PITCH 2", "Osc 2", "PITCH 2: LFO 2 moves the pitch of Osc 2 only (LFO 2 only)." },
        { "WT POS", "wavetable", "WT POS: LFO 2 moves the wavetable position (LFO 2 only)." } };
    auto lastPwm = std::make_shared<float> (0.3f); // último pwmDepth distinto de 0 (para volver a conectar)
    m.cables = [&s, lastPwm]
    {
        const auto t1 = juce::jlimit (0, 2, (int) PatchBay::param (s, "lfoTarget"));
        const auto t2 = juce::jlimit (0, 5, (int) PatchBay::param (s, "lfo2Target"));
        const auto pwmDepth = PatchBay::param (s, "pwmDepth");
        std::vector<PatchBay::Cable> c {
            { lfo2, pitch + t2, Skin::c (0xffd070c0), PatchBay::param (s, "lfo2On") < 0.5f },
            { lfo1, pitch + t1, Skin::c (0xff42a2c8), PatchBay::param (s, "lfoOn") < 0.5f } };
        if (pwmDepth > 0.0f) // el último: en la entrada PWM es el que se coge primero (y el que va encima)
        {
            *lastPwm = pwmDepth;
            c.push_back ({ lfo1, pwm, Skin::c (0xff42a2c8), false });
        }
        return c;
    };
    m.connect = [&s, lastPwm] (int out, int in)
    {
        if (out == lfo1 && in == pwm)                   { PatchBay::setParam (s, "pwmDepth", *lastPwm); return true; }
        if (out == lfo1 && in >= pitch && in <= cutoff) { PatchBay::setParam (s, "lfoTarget", (float) (in - pitch)); return true; }
        if (out == lfo2 && in >= pitch && in <= wtPos)  { PatchBay::setParam (s, "lfo2Target", (float) (in - pitch)); return true; }
        return false;
    };
    m.removable = [] (int out, int in) { return out == lfo1 && in == pwm; }; // los destinos no se quitan: cambian de sitio
    m.unplug = [&s] (int out, int in) { if (out == lfo1 && in == pwm) PatchBay::setParam (s, "pwmDepth", 0.0f); };
    m.visible = [withLfo2] (int j) { return withLfo2() || j == lfo1 || j == pitch || j == amp; }; // Theremin: sin filtro (Theremin.h:165)
    return m;
}

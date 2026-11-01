// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Placa del ANS en pantalla de fósforo: la partitura (720 tonos × 512 mm) con el trazo del skin, líneas de octava con Hz y
// nota, regla de tiempo en mm y segundos a la velocidad actual, y el cursor de lectura. Compacta (bloque ANS del panel):
// solo la placa y el cursor; un clic llama a onClick (abre Plate...). Grande (diálogo Plate...): se dibuja con las
// herramientas de 'brush' (Scratch, Erase, Line, Band; botón derecho = borrar con la herramienta elegida) y da una lectura
// de lo que hay bajo el ratón (onHover). Cada trazo es un paso de deshacer (propiedad "ansUser")
class AnsPlateView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum class Tool { scratch, erase, line, band };
    struct Brush
    {
        Tool tool = Tool::scratch;
        int size = 5;        // diámetro en tonos (filas): 1-36
        float depth = 1.0f;  // lo que añade cada pasada en el centro, 0,1-1 de 255
        bool soft = true;    // borde suave (cae linealmente hasta el radio) o duro
        int snap = 0;        // 0 = libre; 6 = semitono del ANS (6 tonos); 3 = cuarto de tono
    };
    // Márgenes de la placa grande: rótulos de octava a la izquierda y regla de tiempo abajo. La franja de registros usa los
    // mismos arriba y abajo para quedar a la altura de sus tonos
    static constexpr int gutterLeft = 92, gutterRight = 6, gutterTop = 8, gutterBottom = 28;

    std::function<void()> onClick;                      // compacta
    std::function<void (const juce::String&)> onHover;  // grande: lectura bajo el ratón (vacía al salir)
    Brush brush;

    AnsPlateView (KoniSynthProcessor& p, bool isCompact) : koni (p), compact (isCompact)
    {
        if (compact)
            setTooltip ("The ANS plate (the score) with the reading cursor. Click: Plate... (draw your own score).");
        setMouseCursor (compact ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
        rebuildImage();
        startTimerHz (compact ? 15 : 30);
    }

    // Placa vacía (Clear plate): un paso de deshacer
    void clearPlate()
    {
        koni.setAnsUserScore (std::make_unique<AnsScore>());
        rebuildImage();
    }

    // Área de la partitura dentro del componente
    juce::Rectangle<float> plot() const
    {
        auto r = getLocalBounds().toFloat();
        if (compact)
            return r.reduced (6.0f);
        return r.withTrimmedLeft ((float) gutterLeft).withTrimmedRight ((float) gutterRight)
                .withTrimmedTop ((float) gutterTop).withTrimmedBottom ((float) gutterBottom);
    }

    void paint (juce::Graphics& g) override
    {
        const auto screen = getLocalBounds().toFloat();
        Skin::screen (g, screen, compact ? 6.0f : 8.0f);
        const auto r = plot();
        const auto trace = Skin::trace();
        g.drawImage (image, r);

        if (! compact)
        {
            // Octavas: cada 72 tonos, con su frecuencia y la nota temperada más cercana (el tono 0 son 21,09 Hz)
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
            for (int o = 0; o <= 10; ++o)
            {
                const auto y = r.getBottom() - (float) o * 72.0f / AnsScore::kRows * r.getHeight();
                g.setColour (trace.withAlpha (0.16f));
                g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
                if (o < 10)
                {
                    const auto hz = AnsScore::toneHz (o * 72);
                    g.setColour (trace.withAlpha (0.75f));
                    g.drawText (noteName (hz) + " " + juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz",
                                juce::Rectangle<float> (4.0f, y - 16.0f, (float) gutterLeft - 8.0f, 14.0f), juce::Justification::centredRight);
                }
            }
            // Regla de tiempo: cada 64 mm, en mm y en segundos a la velocidad de la placa
            const auto speed = koni.apvts.getRawParameterValue ("ansSpeed")->load();
            for (int mm = 0; mm <= AnsScore::kColumns; mm += 64)
            {
                const auto x = r.getX() + (float) mm / AnsScore::kColumns * r.getWidth();
                g.setColour (trace.withAlpha (0.12f));
                g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
                if (mm < AnsScore::kColumns)
                {
                    g.setColour (trace.withAlpha (0.75f));
                    g.drawText (juce::String (mm) + " mm  " + (speed > 0.0f ? juce::String (mm / speed, 1) + " s" : juce::String ("-")),
                                juce::Rectangle<float> (x + 3.0f, r.getBottom() + 6.0f, 120.0f, 16.0f), juce::Justification::centredLeft);
                }
            }
        }

        // Cursor de lectura con halo
        const auto x = r.getX() + (float) koni.getAnsColumn() / AnsScore::kColumns * r.getWidth();
        g.setColour (Skin::c (0xffe04040).withAlpha (0.3f));
        g.fillRect (x - 2.5f, r.getY(), 5.0f, r.getHeight());
        g.setColour (Skin::c (0xffe04040));
        g.fillRect (x - 0.75f, r.getY(), 1.5f, r.getHeight());

        if (! compact && hovering)
            paintBrushPreview (g, r);
        Skin::glass (g, screen, compact ? 6.0f : 8.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (compact) return;
        hovering = plot().contains (e.position);
        mousePos = e.position;
        report (e.position);
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        if (compact) return;
        hovering = false;
        if (onHover) onHover ({});
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (compact)
        {
            if (onClick) onClick();
            return;
        }
        if (! plot().contains (e.position))
            return;
        edit = std::make_unique<AnsScore> (koni.copyAnsScore());
        erasing = e.mods.isRightButtonDown() || brush.tool == Tool::erase;
        anchor = last = cellAt (e.position);
        mousePos = e.position;
        hovering = true;
        if (brush.tool == Tool::scratch || brush.tool == Tool::erase)
            dab (anchor, false);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (compact || edit == nullptr) return;
        mousePos = e.position;
        report (e.position);
        const auto now = cellAt (e.position);
        if (brush.tool == Tool::scratch || brush.tool == Tool::erase)
        {
            // Pasadas a lo largo del segmento, cada medio radio: un arrastre rápido no deja huecos
            const auto [rx, ry] = radii();
            const auto steps = juce::jmax (1, (int) std::ceil (std::max (std::abs (now.x - last.x) / (rx * 0.5f), std::abs (now.y - last.y) / (ry * 0.5f))));
            for (int k = 1; k <= steps; ++k)
                dab (last + (now - last) * ((float) k / (float) steps), false);
        }
        last = now;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (compact || edit == nullptr) return;
        if (brush.tool == Tool::line)
        {
            // Línea de A a B con la forma del pincel; cada celda queda con el máximo (no se acumula donde se solapan)
            const auto [rx, ry] = radii();
            const auto steps = juce::jmax (1, (int) std::ceil (std::max (std::abs (last.x - anchor.x) / (rx * 0.25f), std::abs (last.y - anchor.y) / (ry * 0.25f))));
            for (int k = 0; k <= steps; ++k)
                dab (anchor + (last - anchor) * ((float) k / (float) steps), true);
        }
        else if (brush.tool == Tool::band)
        {
            // Rectángulo de A a B (cluster o banda de ruido): todo al nivel de Depth, o restado al borrar
            const auto v = (int) std::lround (brush.depth * 255.0f);
            const auto c0 = juce::jlimit (0, AnsScore::kColumns - 1, (int) std::floor (std::min (anchor.x, last.x)));
            const auto c1 = juce::jlimit (0, AnsScore::kColumns - 1, (int) std::floor (std::max (anchor.x, last.x)));
            const auto r0 = juce::jlimit (0, AnsScore::kRows - 1, (int) std::lround (std::min (anchor.y, last.y)));
            const auto r1 = juce::jlimit (0, AnsScore::kRows - 1, (int) std::lround (std::max (anchor.y, last.y)));
            for (int row = r0; row <= r1; ++row)
                for (int col = c0; col <= c1; ++col)
                    put (row, col, v, true);
        }
        koni.setAnsUserScore (std::move (edit));
        rebuildImage();
    }

    // Lectura de un punto de la placa: tono, Hz, nota, mm, segundos, banda de registro y su nivel
    juce::String readout (juce::Point<float> p) const
    {
        const auto c = cellAt (p);
        const auto tone = juce::jlimit (0, AnsScore::kRows - 1, (int) std::lround (c.y));
        const auto mm = juce::jlimit (0, AnsScore::kColumns - 1, (int) std::floor (c.x));
        const auto hz = AnsScore::toneHz (tone);
        const auto speed = koni.apvts.getRawParameterValue ("ansSpeed")->load();
        const auto band = tone / 36;
        const auto level = koni.apvts.getRawParameterValue ("ansReg" + juce::String (band + 1))->load();
        return "TONE " + juce::String (tone).paddedLeft (' ', 3) + "   " + juce::String (hz, 1) + " Hz   " + noteName (hz, true)
               + "   |   " + juce::String (mm) + " mm   " + (speed > 0.0f ? juce::String (mm / speed, 1) + " s" : juce::String ("-"))
               + "   |   BAND " + juce::String (band + 1) + " (" + noteName (AnsScore::toneHz (band * 36)) + "-"
               + noteName (AnsScore::toneHz (band * 36 + 35)) + ")  " + juce::String (juce::roundToInt (level * 100.0f)) + " %";
    }

    // Nota temperada más cercana (La4 = 440 Hz), con los cents si se piden
    static juce::String noteName (double hz, bool withCents = false)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const auto n = (int) std::lround (midi);
        auto text = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
        if (withCents)
        {
            const auto cents = (int) std::lround ((midi - n) * 100.0);
            text << (cents >= 0 ? " +" : " ") << cents << " ct";
        }
        return text;
    }

private:
    // Posición en celdas (columna, fila desde abajo; con decimales), con el ajuste de 'snap' en la fila
    juce::Point<float> cellAt (juce::Point<float> p) const
    {
        const auto r = plot();
        auto col = (p.x - r.getX()) / r.getWidth() * AnsScore::kColumns;
        auto row = (r.getBottom() - p.y) / r.getHeight() * AnsScore::kRows;
        if (brush.snap > 0)
            row = (float) brush.snap * std::round (row / (float) brush.snap);
        return { juce::jlimit (0.0f, (float) AnsScore::kColumns - 0.001f, col), juce::jlimit (0.0f, (float) AnsScore::kRows - 1.0f, row) };
    }

    // Radios del pincel en celdas: el vertical es la mitad del tamaño en tonos; el horizontal, el mismo en píxeles (círculo
    // en pantalla), así que depende de la escala de la placa
    std::pair<float, float> radii() const
    {
        const auto r = plot();
        const auto ry = juce::jmax (0.5f, (float) brush.size * 0.5f);
        const auto rowPx = r.getHeight() / AnsScore::kRows, colPx = r.getWidth() / AnsScore::kColumns;
        return { juce::jmax (0.5f, ry * rowPx / colPx), ry };
    }

    // Una pasada del pincel en el centro c. useMax: la celda queda con el mayor valor (Line); si no, suma (Scratch) o resta
    void dab (juce::Point<float> c, bool useMax)
    {
        const auto [rx, ry] = radii();
        const auto peak = brush.depth * 255.0f;
        for (int row = (int) std::floor (c.y - ry); row <= (int) std::ceil (c.y + ry); ++row)
            for (int col = (int) std::floor (c.x - rx); col <= (int) std::ceil (c.x + rx); ++col)
            {
                if (row < 0 || row >= AnsScore::kRows || col < 0 || col >= AnsScore::kColumns) continue;
                const auto d = std::hypot (((float) col + 0.5f - c.x) / rx, ((float) row - c.y) / ry);
                const auto v = brush.soft ? peak * std::max (0.0f, 1.0f - d) : (d <= 1.0f ? peak : 0.0f);
                if (v > 0.0f)
                    put (row, col, (int) std::lround (v), useMax);
            }
    }

    void put (int row, int col, int v, bool useMax)
    {
        auto& cell = edit->at (row, col);
        cell = (std::uint8_t) (erasing ? std::max (0, (int) cell - v)
                                       : useMax ? std::max ((int) cell, v) : std::min (255, (int) cell + v));
        image.setPixelAt (col, AnsScore::kRows - 1 - row, shade (cell));
    }

    juce::Colour shade (std::uint8_t v) const { return lcd.interpolatedWith (ink, (float) v / 255.0f); }

    // Contorno del pincel bajo el ratón, o la línea / el rectángulo que se está trazando
    void paintBrushPreview (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto toScreen = [&r] (juce::Point<float> c)
        {
            return juce::Point<float> (r.getX() + c.x / AnsScore::kColumns * r.getWidth(), r.getBottom() - c.y / AnsScore::kRows * r.getHeight());
        };
        const auto [rx, ry] = radii();
        const auto pxX = rx * r.getWidth() / AnsScore::kColumns, pxY = ry * r.getHeight() / AnsScore::kRows;
        g.setColour ((erasing && edit != nullptr ? Skin::c (0xffe04040) : Skin::trace()).withAlpha (0.9f));
        const float dash[] = { 4.0f, 3.0f };
        juce::Path p;
        if (edit != nullptr && brush.tool == Tool::line)
        {
            const auto a = toScreen (anchor), b = toScreen (last);
            juce::Path centre;
            centre.startNewSubPath (a);
            centre.lineTo (b);
            juce::PathStrokeType (juce::jmax (1.5f, 2.0f * pxY)).createStrokedPath (p, centre);
        }
        else if (edit != nullptr && brush.tool == Tool::band)
        {
            p.addRectangle (juce::Rectangle<float> (toScreen (anchor), toScreen (last)));
        }
        else
        {
            const auto c = toScreen (cellAt (mousePos));
            p.addEllipse (c.x - pxX, c.y - pxY, 2.0f * pxX, 2.0f * pxY);
        }
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dash, 2);
        g.fillPath (dashed);
    }

    void report (juce::Point<float> p)
    {
        if (onHover)
            onHover (plot().contains (p) ? readout (p) : juce::String());
    }

    void rebuildImage()
    {
        const auto s = koni.copyAnsScore();
        lcd = Skin::mono() ? Skin::ramp().bg : juce::Colour (0xff03080c);
        ink = Skin::trace();
        image = juce::Image (juce::Image::RGB, AnsScore::kColumns, AnsScore::kRows, false);
        juce::Colour lut[256];
        for (int v = 0; v < 256; ++v) lut[v] = shade ((std::uint8_t) v);
        for (int r = 0; r < AnsScore::kRows; ++r)
            for (int c = 0; c < AnsScore::kColumns; ++c)
                image.setPixelAt (c, AnsScore::kRows - 1 - r, lut[s.at (r, c)]);
        shownVersion = koni.getAnsVersion();
    }

    void timerCallback() override
    {
        // Partitura cambiada fuera (preset, deshacer, otra ventana) o skin nuevo: se redibuja la imagen
        if (edit == nullptr && (koni.getAnsVersion() != shownVersion || Skin::trace() != ink))
            rebuildImage();
        repaint();
    }

    KoniSynthProcessor& koni;
    const bool compact;
    juce::Image image;
    juce::Colour lcd, ink;
    std::unique_ptr<AnsScore> edit;
    juce::Point<float> anchor, last, mousePos;
    bool erasing = false, hovering = false;
    int shownVersion = -1;
};

//==============================================================================
// Los 20 registros del ANS junto a la placa: una barra por banda de media octava (36 tonos), a la altura de los tonos que
// filtra (mismos márgenes que AnsPlateView), y a su derecha una perilla. Arrastrar la barra en horizontal = nivel (un gesto:
// deshacer, automatización); doble clic = 100 %. Las perillas, como cualquier mando: menú con Reset, Set value y MIDI Learn
class AnsRegisters : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    static constexpr int knobWidth = 34;

    explicit AnsRegisters (APVTS& s)
    {
        setTooltip ("ANS registers: gain of each half-octave band (36 tones), drawn at the height of the tones it filters. "
                    "Drag a bar sideways or turn its knob; double click = 100 %.");
        for (int b = 0; b < 20; ++b)
        {
            const auto id = "ansReg" + juce::String (b + 1);
            params[(size_t) b] = dynamic_cast<juce::RangedAudioParameter*> (s.getParameter (id));
            auto& k = knobs[(size_t) b];
            k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            k.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            k.setTooltip (paramTooltip (s, id));
            attachments[(size_t) b] = std::make_unique<APVTS::SliderAttachment> (s, id, k);
            menus[(size_t) b] = std::make_unique<ParamMenu> (s, id, k);
            addAndMakeVisible (k);
        }
        startTimerHz (15);
    }

    void resized() override
    {
        for (int b = 0; b < 20; ++b)
        {
            const auto row = rowArea (b);
            knobs[(size_t) b].setBounds (juce::Rectangle<int> (getWidth() - knobWidth, row.getY(), knobWidth, row.getHeight())
                                             .withSizeKeepingCentre (knobWidth, juce::jmin (knobWidth, row.getHeight() + 2)));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto screen = getLocalBounds().withTrimmedRight (knobWidth + 4).toFloat();
        Skin::screen (g, screen, 8.0f);
        const auto trace = Skin::trace();
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        for (int b = 0; b < 20; ++b)
        {
            const auto row = rowArea (b).toFloat().reduced (0.0f, 1.0f);
            const auto bar = barArea (row);
            g.setColour (trace.withAlpha (0.08f));
            g.fillRect (bar);
            const auto level = params[(size_t) b] != nullptr ? params[(size_t) b]->getValue() : 1.0f;
            g.setColour (trace.withAlpha (0.25f));
            g.fillRect (bar.withWidth (bar.getWidth() * level).expanded (0.0f, 1.0f)); // halo
            g.setColour (trace.withAlpha (0.8f));
            g.fillRect (bar.withWidth (bar.getWidth() * level));
            g.setColour (trace.withAlpha (0.75f));
            g.drawText (juce::String (b + 1).paddedLeft (' ', 2), row.withWidth (22.0f).translated (4.0f, 0.0f), juce::Justification::centredLeft);
        }
        Skin::glass (g, screen, 8.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = bandAt (e.position);
        if (dragged < 0 || params[(size_t) dragged] == nullptr) return;
        params[(size_t) dragged]->beginChangeGesture();
        setFrom (e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override { setFrom (e.position); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragged >= 0 && params[(size_t) dragged] != nullptr)
            params[(size_t) dragged]->endChangeGesture();
        dragged = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto b = bandAt (e.position);
        if (b < 0 || params[(size_t) b] == nullptr) return;
        auto* p = params[(size_t) b];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }

private:
    // Fila de la banda b (0 = la grave, abajo), entre los mismos márgenes que la placa
    juce::Rectangle<int> rowArea (int b) const
    {
        const auto top = (float) AnsPlateView::gutterTop, bottom = (float) (getHeight() - AnsPlateView::gutterBottom);
        const auto h = (bottom - top) / 20.0f;
        const auto y0 = bottom - (float) (b + 1) * h;
        return juce::Rectangle<float> (0.0f, y0, (float) getWidth(), h).toNearestInt();
    }
    juce::Rectangle<float> barArea (juce::Rectangle<float> row) const
    {
        return row.withX (28.0f).withRight ((float) (getWidth() - knobWidth - 12));
    }
    int bandAt (juce::Point<float> p) const
    {
        for (int b = 0; b < 20; ++b)
            if (rowArea (b).toFloat().contains (p) && p.x < (float) (getWidth() - knobWidth - 4))
                return b;
        return -1;
    }
    void setFrom (juce::Point<float> p)
    {
        if (dragged < 0 || params[(size_t) dragged] == nullptr) return;
        const auto bar = barArea (rowArea (dragged).toFloat());
        params[(size_t) dragged]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (p.x - bar.getX()) / bar.getWidth()));
    }

    void timerCallback() override { repaint(); } // niveles cambiados por la perilla, MIDI o automatización

    std::array<juce::RangedAudioParameter*, 20> params {};
    std::array<juce::Slider, 20> knobs;
    std::array<std::unique_ptr<APVTS::SliderAttachment>, 20> attachments;
    std::array<std::unique_ptr<ParamMenu>, 20> menus;
    int dragged = -1;
};

//==============================================================================
// Lectura de una línea en pantalla de fósforo (lo que hay bajo el ratón en la placa, o la ayuda)
class PhosphorReadout : public juce::Component
{
public:
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        Skin::screen (g, r, 6.0f);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), juce::jmin (16.0f, r.getHeight() * 0.55f), juce::Font::plain));
        g.setColour (Skin::trace());
        g.drawFittedText (text, r.reduced (12.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        Skin::glass (g, r, 6.0f);
    }
private:
    juce::String text;
};

//==============================================================================
// Diálogo Plate... del ANS: cabecera (partitura, Hold, Speed, Emulsion), herramientas de dibujo, lectura bajo el ratón y la
// placa con los 20 registros a su derecha, ambos con el mismo alto. La placa conserva su proporción (2,1:1, ~ 0,7 mm de
// placa por tono a lo ancho) y se centra en el sitio que quede
class AnsPanel : public juce::Component
{
public:
    explicit AnsPanel (KoniSynthProcessor& p) : koni (p), plate (p, false), registers (p.apvts)
    {
        auto& s = p.apvts;
        header.withList (s, "ansScore", "Score", 3.0f).withExtraToggle (s, "ansHold", "Hold")
              .withKnob (s, "ansSpeed", "Speed").withKnob (s, "ansEmulsion", "Emulsion");
        if (auto* list = header.listFor ("ansScore")) list->setPhosphor (44);
        addAndMakeVisible (header);

        // Herramientas (de esta ventana, no son parámetros; se recuerdan mientras dure la sesión)
        plate.brush = lastBrush();
        static const char* toolNames[4] = { "Scratch", "Erase", "Line", "Band" };
        static const char* toolTips[4] = { "Add sound where you drag (repeated passes get louder).", "Remove sound where you drag.",
                                           "Drag from A to B: a straight tone or glissando with the brush's width.",
                                           "Drag a rectangle: a cluster or a noise band at the Depth level." };
        for (int t = 0; t < 4; ++t)
        {
            auto& b = tools[(size_t) t];
            b.setButtonText (toolNames[t]);
            b.setTooltip (juce::String (toolTips[t]) + " Right drag: erase with the same tool.");
            b.setClickingTogglesState (true);
            b.setRadioGroupId (701);
            b.setToggleState ((int) plate.brush.tool == t, juce::dontSendNotification);
            b.onClick = [this, t] { if (tools[(size_t) t].getToggleState()) { plate.brush.tool = (AnsPlateView::Tool) t; remember(); } };
            toolBox.addAndMakeVisible (b);
        }
        setupBar (size, 1.0, 36.0, 1.0, " tones", plate.brush.size, "Brush size in tones (rows): 1-36. 72 tones = one octave.",
                  [this] { plate.brush.size = (int) size.getValue(); remember(); });
        setupBar (depth, 10.0, 100.0, 1.0, " %", plate.brush.depth * 100.0f, "How much each pass adds (or removes) at the brush centre.",
                  [this] { plate.brush.depth = (float) depth.getValue() / 100.0f; remember(); });
        for (int k = 0; k < 2; ++k)
        {
            auto& b = edges[(size_t) k];
            b.setButtonText (k == 0 ? "Soft" : "Hard");
            b.setTooltip (k == 0 ? "Soft edge: the brush fades towards its rim." : "Hard edge: full level up to the rim.");
            b.setClickingTogglesState (true);
            b.setRadioGroupId (702);
            b.setToggleState (plate.brush.soft == (k == 0), juce::dontSendNotification);
            b.onClick = [this, k] { if (edges[(size_t) k].getToggleState()) { plate.brush.soft = k == 0; remember(); } };
            toolBox.addAndMakeVisible (b);
        }
        snap.addItemList ({ "Snap: off", "Snap: semitone", "Snap: quarter tone" }, 1);
        snap.setSelectedId (plate.brush.snap == 6 ? 2 : plate.brush.snap == 3 ? 3 : 1, juce::dontSendNotification);
        snap.setTooltip ("Snap the pitch of strokes to the ANS's semitones (6 tones) or quarter tones (3 tones), counted from "
                         "tone 0 (21.09 Hz). The keyboard transposes in the same semitone steps.");
        snap.onChange = [this] { plate.brush.snap = snap.getSelectedId() == 2 ? 6 : snap.getSelectedId() == 3 ? 3 : 0; remember(); };
        toolBox.addAndMakeVisible (snap);
        clear.setButtonText ("Clear plate");
        clear.setTooltip ("Erase the whole plate (one undo step).");
        clear.onClick = [this] { plate.clearPlate(); };
        toolBox.addAndMakeVisible (clear);
        addAndMakeVisible (toolBox);

        readout.setText (help);
        plate.onHover = [this] (const juce::String& t) { readout.setText (t.isEmpty() ? help : t); };
        addAndMakeVisible (readout);
        addAndMakeVisible (plate);
        addAndMakeVisible (registers);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (124);
        header.setBounds (top.removeFromLeft (520));
        toolBox.setBounds (top.withTrimmedLeft (8));
        layoutTools();
        r.removeFromTop (6);
        readout.setBounds (r.removeFromTop (32));
        r.removeFromTop (6);
        // Placa y registros con el mismo alto; la placa en su proporción, el conjunto centrado
        constexpr float aspect = 2.1f;
        constexpr int regWidth = 170, gap = 8;
        const auto plateH = juce::jmin ((float) r.getHeight(), (float) (r.getWidth() - regWidth - gap) / aspect);
        const auto plateW = juce::roundToInt (plateH * aspect);
        const auto h = juce::roundToInt (plateH);
        auto body = r.withSizeKeepingCentre (plateW + gap + regWidth, h);
        plate.setBounds (body.removeFromLeft (plateW));
        body.removeFromLeft (gap);
        registers.setBounds (body);
    }

private:
    static AnsPlateView::Brush& lastBrush() { static AnsPlateView::Brush b; return b; }
    void remember() { lastBrush() = plate.brush; }

    void setupBar (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix, double value, const juce::String& tip,
                   std::function<void()> changed)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setNumDecimalPlacesToDisplay (0);
        s.setValue (value, juce::dontSendNotification);
        s.setTooltip (tip);
        s.onValueChange = std::move (changed);
        toolBox.addAndMakeVisible (s);
    }

    // Herramientas en dos filas: las cuatro de dibujo y Clear plate; debajo, tamaño, profundidad, borde y ajuste
    void layoutTools()
    {
        auto b = toolBox.getLocalBounds().reduced (4, 8);
        auto row1 = b.removeFromTop (34);
        b.removeFromTop (12);
        auto row2 = b.removeFromTop (34);
        for (auto& t : tools) t.setBounds (row1.removeFromLeft (100).reduced (3, 0));
        clear.setBounds (row1.removeFromRight (130).reduced (3, 0));
        size.setBounds (row2.removeFromLeft (150).reduced (3, 2));
        depth.setBounds (row2.removeFromLeft (150).reduced (3, 2));
        row2.removeFromLeft (10);
        for (auto& e : edges) e.setBounds (row2.removeFromLeft (70).reduced (3, 0));
        row2.removeFromLeft (10);
        snap.setBounds (row2.removeFromLeft (220).reduced (3, 2));
    }

    KoniSynthProcessor& koni;
    Section header { "ANS" };
    juce::Component toolBox;
    std::array<juce::TextButton, 4> tools;
    std::array<juce::TextButton, 2> edges;
    juce::Slider size, depth;
    juce::ComboBox snap;
    juce::TextButton clear;
    PhosphorReadout readout;
    AnsPlateView plate;
    AnsRegisters registers;
    const juce::String help { "Left drag: draw with the tool. Right drag: erase. Up: higher pitch (72 tones per octave). Right: later in time." };
};

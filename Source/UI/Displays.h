// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Dígito de 7 segmentos inclinado (LED del BPM y del DX7, LCD del VL-1). mask: bits 0-6 = segmentos a..g (orden clásico:
// a arriba, b-c derecha, d abajo, e-f izquierda, g centro); dot: punto decimal abajo a la derecha (-1 = no se dibuja)
struct SevenSegment
{
    static constexpr unsigned char digitMask[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };

    static void draw (juce::Graphics& g, juce::Rectangle<float> r, int mask, juce::Colour lit, juce::Colour off, int dot = -1)
    {
        const auto t = juce::jmax (2.0f, r.getWidth() * 0.16f), sk = r.getWidth() * 0.08f; // grosor e inclinación
        const auto x0 = r.getX(), x1 = r.getRight(), y0 = r.getY(), ym = r.getCentreY(), y1 = r.getBottom();
        auto seg = [&] (int bit, juce::Point<float> a, juce::Point<float> b)
        {
            const auto slant = [&] (juce::Point<float> p) { return p.translated (sk * (ym - p.y) / (y1 - y0) * 2.0f, 0.0f); };
            juce::Path path;
            path.startNewSubPath (slant (a));
            path.lineTo (slant (b));
            g.setColour ((mask >> bit) & 1 ? lit : off);
            g.strokePath (path, juce::PathStrokeType (t, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        };
        const auto i = t * 0.5f + 1.0f;
        seg (0, { x0 + i, y0 + t * 0.5f }, { x1 - i, y0 + t * 0.5f });  // a
        seg (1, { x1 - t * 0.5f, y0 + i }, { x1 - t * 0.5f, ym - i });  // b
        seg (2, { x1 - t * 0.5f, ym + i }, { x1 - t * 0.5f, y1 - i });  // c
        seg (3, { x0 + i, y1 - t * 0.5f }, { x1 - i, y1 - t * 0.5f });  // d
        seg (4, { x0 + t * 0.5f, ym + i }, { x0 + t * 0.5f, y1 - i });  // e
        seg (5, { x0 + t * 0.5f, y0 + i }, { x0 + t * 0.5f, ym - i });  // f
        seg (6, { x0 + i, ym }, { x1 - i, ym });                        // g
        if (dot >= 0)
        {
            g.setColour (dot > 0 ? lit : off);
            g.fillEllipse (juce::Rectangle<float> (t * 1.1f, t * 1.1f).withCentre ({ x1 + t * 0.9f, y1 - t * 0.5f }));
        }
    }
};

// Indicador de tempo: display LED rojo de 7 segmentos (estilo cajas de ritmos de los 80) con el tempo efectivo.
// "HOST" encendido si el tempo viene del DAW; apagado si es el reloj interno. Repinta solo al cambiar (timer de 10 Hz).
// Colores fijos: el LED rojo no sigue el skin (tono original en todos)
class BpmDisplay : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit BpmDisplay (KoniSynthProcessor& p) : processor (p), menu (p.apvts, "arpBpm", *this)
    {
        menu.paramSource = [this] { return clockParam(); };
        setTooltip ("Effective tempo: the host tempo when the DAW sends one (HOST lit), otherwise the internal clock of the "
                    "active engine (Drum Machine / Omnichord tempo, VL-1 clock) or the internal BPM (arpeggiator, sequencer, "
                    "synced echo). Drag up/down or use the mouse wheel to change the internal clock (not while HOST is lit); "
                    "right-click: exact value, reset, MIDI learn.");
        startTimerHz (10);
    }

    // Edición: arrastre vertical (4 px por paso) o rueda (1 paso por muesca) sobre el reloj interno que se muestra.
    // Paso: 1 BPM (Drum, Omnichord, BPM interno) o 1 unidad del Tempo del VL-1 (-9..9). Con el tempo del host, nada
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return; // menú de parámetro (ParamMenu)
        dragStartY = e.y;
        if (auto* prm = clockParam()) { prm->beginChangeGesture(); dragging = prm; dragStartValue = prm->convertFrom0to1 (prm->getValue()); }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging != nullptr)
            setClock (*dragging, dragStartValue + (float) ((dragStartY - e.y) / 4));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging != nullptr) dragging->endChangeGesture();
        dragging = nullptr;
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (auto* prm = clockParam(); prm != nullptr && w.deltaY != 0.0f)
        {
            prm->beginChangeGesture();
            setClock (*prm, prm->convertFrom0to1 (prm->getValue()) + (w.deltaY > 0.0f ? 1.0f : -1.0f));
            prm->endChangeGesture();
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (clockParam() != nullptr ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff120607));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        // Compacto (banda del display plegado, ~24 px): márgenes y etiquetas menores para que los dígitos se lean
        const auto compact = getHeight() < 40;
        r = compact ? r.reduced (6.0f, 3.0f) : r.reduced (10.0f, 8.0f);
        auto labels = r.removeFromRight (compact ? 30.0f : 34.0f);
        const auto lit = juce::Colour (0xffff2a1a), off = juce::Colour (0xff3a0d0a);

        // 4 dígitos con un decimal: "128.0"; ceros a la izquierda apagados como en un LED real
        const auto tenths = juce::jlimit (0, 9999, juce::roundToInt (bpm * 10.0f));
        const int digits[4] = { tenths / 1000, (tenths / 100) % 10, (tenths / 10) % 10, tenths % 10 };
        // Hueco propio para el punto decimal (40 % de un dígito) entre el tercer y el cuarto dígito: el punto no queda pegado
        // a los segmentos inclinados. Punto cuadrado del grosor de un segmento, como en los LED de 7 segmentos
        const auto gap = r.getWidth() * 0.4f / 4.4f, w = (r.getWidth() - gap) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const auto leadingZero = (i == 0 && digits[0] == 0) || (i == 1 && digits[0] == 0 && digits[1] == 0);
            const auto x = r.getX() + w * (float) i + (i == 3 ? gap : 0.0f);
            drawDigit (g, r.withX (x).withWidth (w).reduced (3.0f, 0.0f), leadingZero ? -1 : digits[i], lit, off);
        }
        const auto side = juce::jmax (3.0f, (w - 6.0f) * 0.16f);
        const auto dot = juce::Rectangle<float> (side, side).withCentre ({ r.getX() + 3.0f * w + gap * 0.5f, r.getBottom() - side * 0.5f });
        g.setColour (lit);
        g.fillRect (dot);

        g.setFont (juce::FontOptions (compact ? 8.5f : 11.0f, juce::Font::bold));
        g.setColour (lit);
        g.drawText ("BPM", labels.removeFromBottom (labels.getHeight() * 0.5f), juce::Justification::centred);
        g.setColour (fromHost ? lit : off);
        g.drawText ("HOST", labels, juce::Justification::centred);
    }

private:
    // -1 = dígito apagado
    static void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, int d, juce::Colour lit, juce::Colour off)
    {
        SevenSegment::draw (g, r, d < 0 ? 0 : SevenSegment::digitMask[d], lit, off);
    }

    // Reloj que se ve en el display (mismo criterio que processBlock al calcular displayBpm); nullptr con tempo del host
    juce::RangedAudioParameter* clockParam() const
    {
        const auto engine = (int) processor.apvts.getRawParameterValue ("engine")->load();
        if (fromHost && engine != 2)
            return nullptr;
        const char* id = engine == 5 ? "drumTempo" : engine == 4 ? "omniTempo" : engine == 2 ? "vl1Tempo" : "arpBpm";
        return processor.apvts.getParameter (id);
    }

    static void setClock (juce::RangedAudioParameter& prm, float value)
    {
        const auto& range = prm.getNormalisableRange();
        prm.setValueNotifyingHost (prm.convertTo0to1 (juce::jlimit (range.start, range.end, std::round (value))));
    }

    void timerCallback() override
    {
        const auto b = processor.getDisplayBpm();
        const auto h = processor.isDisplayBpmFromHost();
        if (juce::roundToInt (b * 10.0f) != juce::roundToInt (bpm * 10.0f) || h != fromHost)
        {
            bpm = b;
            fromHost = h;
            repaint();
        }
    }

    KoniSynthProcessor& processor;
    float bpm = 120.0f;
    bool fromHost = false;
    juce::RangedAudioParameter* dragging = nullptr;
    ParamMenu menu; // tras processor: se construye con su apvts
    int dragStartY = 0;
    float dragStartValue = 0.0f;
};

//==============================================================================
// Fuente 5×7 propia de los LCD de caracteres (MT-32, DX7): no copia la CGROM de ningún controlador
struct DotMatrix
{
    // Fila (0-6) del carácter: 5 bits, el 4 a la izquierda. 1 = bloque lleno; ASCII 0x20-0x7E; el resto, en blanco
    static int glyphRow (unsigned char code, int row)
    {
        static const unsigned char font[95 * 7] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ' '
        0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, // '!'
        0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00, // '"'
        0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a, // '#'
        0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04, // '$'
        0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03, // '%'
        0x0c, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0d, // '&'
        0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, // "'"
        0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, // '('
        0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, // ')'
        0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00, // '*'
        0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00, // '+'
        0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08, // ','
        0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, // '-'
        0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c, // '.'
        0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00, // '/'
        0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e, // '0'
        0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e, // '1'
        0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f, // '2'
        0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e, // '3'
        0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02, // '4'
        0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e, // '5'
        0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e, // '6'
        0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, // '7'
        0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e, // '8'
        0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c, // '9'
        0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00, // ':'
        0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08, // ';'
        0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, // '<'
        0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00, // '='
        0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, // '>'
        0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, // '?'
        0x0e, 0x11, 0x01, 0x0d, 0x15, 0x15, 0x0e, // '@'
        0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, // 'A'
        0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, // 'B'
        0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, // 'C'
        0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c, // 'D'
        0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f, // 'E'
        0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10, // 'F'
        0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f, // 'G'
        0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, // 'H'
        0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e, // 'I'
        0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c, // 'J'
        0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, // 'K'
        0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, // 'L'
        0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11, // 'M'
        0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, // 'N'
        0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, // 'O'
        0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, // 'P'
        0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d, // 'Q'
        0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11, // 'R'
        0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e, // 'S'
        0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, // 'T'
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, // 'U'
        0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04, // 'V'
        0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a, // 'W'
        0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, // 'X'
        0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, // 'Y'
        0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f, // 'Z'
        0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e, // '['
        0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00, // barra invertida
        0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, // ']'
        0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00, // '^'
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, // '_'
        0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, // '`'
        0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f, // 'a'
        0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1e, // 'b'
        0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e, // 'c'
        0x01, 0x01, 0x0d, 0x13, 0x11, 0x11, 0x0f, // 'd'
        0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e, // 'e'
        0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08, // 'f'
        0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01, 0x0e, // 'g'
        0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11, // 'h'
        0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e, // 'i'
        0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c, // 'j'
        0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, // 'k'
        0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e, // 'l'
        0x00, 0x00, 0x1a, 0x15, 0x15, 0x11, 0x11, // 'm'
        0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, // 'n'
        0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e, // 'o'
        0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10, // 'p'
        0x00, 0x00, 0x0d, 0x13, 0x0f, 0x01, 0x01, // 'q'
        0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, // 'r'
        0x00, 0x00, 0x0e, 0x10, 0x0e, 0x01, 0x1e, // 's'
        0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06, // 't'
        0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d, // 'u'
        0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04, // 'v'
        0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a, // 'w'
        0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11, // 'x'
        0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x0e, // 'y'
        0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f, // 'z'
        0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02, // '{'
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, // '|'
        0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08, // '}'
        0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, // '~'
        };
        if (code == 1)
            return 0x1f;
        if (code < 0x20 || code > 0x7e)
            return 0;
        return font[(code - 0x20) * 7 + row];
    }
};

//==============================================================================
// LCD del Roland MT-32: 20 caracteres de matriz de puntos sobre fondo verde retroiluminado ("a 20-character, green backlit
// LCD display", Sound On Sound, sept. 1987) y el LED MIDI MESSAGE. Texto y LED los da Munt (Synth::getDisplayState), leídos a
// 30 Hz. El carácter 1 es el bloque lleno que marca las partes que suenan (Display.cpp). Fuente 5×7 propia (no es la CGROM
// del SED1200 del aparato); los puntos apagados se ven tenues, como en un LCD real. Colores fijos: no siguen el skin
class Mt32Lcd : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit Mt32Lcd (KoniSynthProcessor& p) : processor (p)
    {
        setTooltip ("The MT-32 display as the unit shows it: a block replaces the number of each part that is sounding, then "
                    "the patch or the last message, and the master volume. MIDI MESSAGE lights while messages arrive.");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto glass = juce::Colour (0xffa8c23e), dotOff = juce::Colour (0xff9ab237), dotOn = juce::Colour (0xff1f2a0e);
        // Panel del tamaño justo, centrado: cristal de 20 caracteres (121 × 9 puntos más márgenes) y, a su derecha, el LED con
        // su rótulo. El paso del punto es el mayor que cabe a lo alto y a lo ancho
        auto r = getLocalBounds().toFloat();
        const auto ledW = 96.0f;
        const auto pitch0 = juce::jmin ((r.getHeight() - 14.0f) / 9.0f, (r.getWidth() - ledW - 16.0f) / 121.0f);
        r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 121.0f * pitch0 + 16.0f + ledW), juce::jmin (r.getHeight(), 9.0f * pitch0 + 14.0f));
        g.setColour (juce::Colour (0xff15170f)); // marco del panel
        g.fillRoundedRectangle (r, 5.0f);

        // LED (rojo, como el BPM) con su rótulo
        auto ledArea = r.removeFromRight (ledW);
        const auto ledD = juce::jlimit (6.0f, 12.0f, r.getHeight() * 0.28f);
        const auto led = juce::Rectangle<float> (ledD, ledD).withCentre ({ ledArea.getX() + ledD, ledArea.getCentreY() });
        g.setColour (ledOn ? juce::Colour (0xffff2a1a) : juce::Colour (0xff3a0d0a));
        g.fillEllipse (led);
        if (ledOn)
        {
            g.setColour (juce::Colour (0xffff2a1a).withAlpha (0.25f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (juce::Colour (0xffc8c8c0));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawFittedText ("MIDI\nMESSAGE", ledArea.withTrimmedLeft (ledD * 2.0f + 4.0f).toNearestInt(), juce::Justification::centredLeft, 2);

        // Cristal: 20 celdas de 5×7 puntos con un punto de separación (120 columnas) y margen de un punto
        auto lcd = r.reduced (6.0f, 5.0f);
        g.setColour (glass);
        g.fillRoundedRectangle (lcd, 2.0f);
        const auto pitch = juce::jmin ((lcd.getWidth() - 4.0f) / 121.0f, (lcd.getHeight() - 4.0f) / 9.0f);
        if (pitch <= 0.0f)
            return;
        const auto dot = pitch * 0.82f;
        const auto x0 = lcd.getCentreX() - pitch * 60.5f, y0 = lcd.getCentreY() - pitch * 3.5f;
        for (int c = 0; c < 20; ++c)
        {
            const auto code = (unsigned char) text[(size_t) c];
            for (int row = 0; row < 7; ++row)
            {
                const auto bits = DotMatrix::glyphRow (code, row);
                for (int col = 0; col < 5; ++col)
                {
                    g.setColour (((bits >> (4 - col)) & 1) != 0 ? dotOn : dotOff);
                    g.fillRect (x0 + pitch * (float) (c * 6 + col), y0 + pitch * (float) row, dot, dot);
                }
            }
        }
    }

private:
    void timerCallback() override
    {
        const auto t = processor.hasMt32Machine() ? processor.getMt32Lcd() : juce::String();
        const auto l = processor.getMt32Led();
        std::array<char, 20> now {};
        now.fill (' ');
        for (int i = 0; i < juce::jmin (20, t.length()); ++i)
            now[(size_t) i] = (char) t[i];
        if (now != text || l != ledOn)
        {
            text = now;
            ledOn = l;
            repaint();
        }
    }

    KoniSynthProcessor& processor;
    std::array<char, 20> text {};
    bool ledOn = false;
};

//==============================================================================
// Pantalla del Yamaha DX7 (DX7 Operating Manual; páginas impresas): LCD de 16 caracteres × 2 líneas sin retroiluminación
// ("LCD DISPLAY 16 char. x 2 lines", especificaciones, p. 63) y, a su izquierda, el LED rojo de 2 dígitos con el número de la
// voz ("2 numbers w/decimal"). Contenido, según las figuras del manual:
//  - Voz: "INTERNAL VOICE" / "INT 1 nombre" o "CARTRIDGE VOICE" / "CRT 5 BRASSHORNS" (p. 14). Aquí el cartucho es la voz
//    cargada de un .syx (propiedad dx7Voice "n|NOMBRE", n = posición en el banco, 0 = voz suelta) y la memoria interna, el
//    preset de KoniSynth (sin número: el LED queda apagado)
//  - Punto decimal del LED en cuanto se edita la voz (pp. 5 y 47): aquí, si un parámetro FM difiere de la voz cargada
//  - Modo EDIT (pp. 28-43): línea 1 "ALG1 111111 OP1" (algoritmo, operadores encendidos, operador actual; sin "OPn" en los
//    parámetros de la voz entera), línea 2 el parámetro y su valor. Se muestra 2 s al mover un mando FM (elección de interfaz)
// Etiquetas verificadas en las figuras salvo las marcadas "estimada" en editLine. Colores fijos: no siguen el skin
class Dx7Display : public juce::Component, public juce::SettableTooltipClient, private juce::Timer, private juce::ValueTree::Listener
{
public:
    explicit Dx7Display (KoniSynthProcessor& p) : processor (p)
    {
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm); rp != nullptr && isDx7Param (rp->getParameterID()))
                params.push_back (rp);
        snapshot = lastSeen = values();
        processor.apvts.state.addListener (this);
        setTooltip ("The DX7 display: the voice and its number (CRT = loaded from a .syx bank, INT = a KoniSynth preset). A dot "
                    "after the number means the voice has been edited. Moving an FM control shows it as the DX7 EDIT mode does.");
        startTimerHz (20);
    }

    ~Dx7Display() override { processor.apvts.state.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        // Panel: LED de 2 dígitos a la izquierda y cristal de 16 × 2 (97 × 17 puntos con márgenes), centrado y del mayor
        // tamaño que cabe
        auto r = getLocalBounds().toFloat();
        const auto pitch0 = juce::jmin ((r.getHeight() - 14.0f) / 17.0f, (r.getWidth() - 16.0f) / (97.0f + 17.0f * 1.1f));
        if (pitch0 <= 0.0f)
            return;
        const auto ledW = 17.0f * pitch0 * 1.1f;
        r = r.withSizeKeepingCentre (97.0f * pitch0 + ledW + 16.0f, 17.0f * pitch0 + 14.0f);
        g.setColour (juce::Colour (0xff15170f));
        g.fillRoundedRectangle (r, 5.0f);
        r = r.reduced (6.0f, 5.0f);

        // LED rojo: 2 dígitos de 7 segmentos con punto decimal (el del BPM, mismo tono)
        auto led = r.removeFromLeft (ledW).reduced (pitch0, pitch0 * 1.5f);
        g.setColour (juce::Colour (0xff120607));
        g.fillRoundedRectangle (led.expanded (pitch0 * 0.6f), 2.0f);
        const auto lit = juce::Colour (0xffff2a1a), off = juce::Colour (0xff3a0d0a);
        const auto digitW = led.getWidth() * 0.36f;
        const int digits[2] = { number >= 10 ? number / 10 : -1, number > 0 ? number % 10 : -1 };
        for (int k = 0; k < 2; ++k)
        {
            const auto cell = juce::Rectangle<float> (led.getX() + led.getWidth() * (0.08f + 0.46f * (float) k), led.getY(), digitW, led.getHeight());
            SevenSegment::draw (g, cell, digits[k] < 0 ? 0 : SevenSegment::digitMask[digits[k]], lit, off, k == 1 ? (edited ? 1 : 0) : -1);
        }

        // LCD sin retroiluminación: gris verdoso reflectante, caracteres oscuros y puntos apagados apenas visibles
        r.removeFromLeft (pitch0);
        g.setColour (juce::Colour (0xffa7ad97));
        g.fillRoundedRectangle (r, 2.0f);
        const auto dotOn = juce::Colour (0xff262a20), dotOff = juce::Colour (0xffa0a690);
        const auto pitch = juce::jmin ((r.getWidth() - 2.0f) / 97.0f, (r.getHeight() - 2.0f) / 17.0f);
        const auto dot = pitch * 0.82f;
        const auto x0 = r.getCentreX() - pitch * 47.5f, y0 = r.getCentreY() - pitch * 7.5f;
        for (int line = 0; line < 2; ++line)
            for (int c = 0; c < 16; ++c)
            {
                const auto code = (unsigned char) (c < lines[line].length() ? lines[line][c] : ' ');
                for (int row = 0; row < 7; ++row)
                {
                    const auto bits = DotMatrix::glyphRow (code, row);
                    for (int col = 0; col < 5; ++col)
                    {
                        g.setColour (((bits >> (4 - col)) & 1) != 0 ? dotOn : dotOff);
                        g.fillRect (x0 + pitch * (float) (c * 6 + col), y0 + pitch * (float) (line * 8 + row), dot, dot);
                    }
                }
            }
    }

private:
    // Parámetros que el DX7 tiene en su memoria de voz (no fmMode: la emulación "Clean" es de KoniSynth)
    static bool isDx7Param (const juce::String& id)
    {
        return id == "fmAlgorithm" || id == "fmFeedback" || id.startsWith ("fmLfo") || id == "fmTranspose" || id == "fmOscSync"
               || id.startsWith ("fmPR") || id.startsWith ("fmPL")
               || (id.length() > 3 && id.startsWith ("fm") && id[2] >= '1' && id[2] <= '6');
    }

    std::vector<float> values() const
    {
        std::vector<float> v;
        for (auto* p : params) v.push_back (p->convertFrom0to1 (p->getValue()));
        return v;
    }

    float value (const juce::String& id) const
    {
        auto* p = processor.apvts.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    // Nota del break point como el DX7: letra, '#' o espacio y octava de Yamaha (C3 = MIDI 60), "A-1".."C8"
    static juce::String yamahaNote (int midi)
    {
        static const char* names[12] = { "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B " };
        const auto octave = midi / 12 - 2;
        return octave < 0 ? juce::String (names[midi % 12]).trimEnd() + juce::String (octave) : juce::String (names[midi % 12]) + juce::String (octave);
    }

    // Línea 2 del modo EDIT para el parámetro 'id' con valor real v (formatos de las figuras del manual)
    juce::String editLine (const juce::String& id, float v) const
    {
        const auto n = juce::roundToInt (v);
        if (id == "fmAlgorithm") return "ALGORITHM SELECT";
        if (id == "fmFeedback")  return "FEEDBACK     =" + juce::String (n);                       // estimada
        if (id == "fmLfoWave")
        {
            static const char* waves[6] = { "TRIANGL", "SAW DWN", "SAW UP", "SQUARE", "SINE", "S/HOLD" }; // solo SINE verificada
            return juce::String ("LFO WAVE=") + waves[juce::jlimit (0, 5, n)];
        }
        if (id == "fmLfoSpeed") return juce::String::formatted ("LFO SPEED   =%2d", n);
        if (id == "fmLfoDelay") return juce::String::formatted ("LFO DELAY   =%2d", n);
        if (id == "fmLfoPmd")   return juce::String::formatted ("LFO PM DEPTH =%2d", n);
        if (id == "fmLfoAmd")   return juce::String::formatted ("LFO AM DEPTH =%2d", n);
        if (id == "fmLfoPms")   return juce::String::formatted ("P MOD SENS.  =%d", n);
        if (id == "fmLfoSync")  return juce::String ("LFO KEY SYNC=") + (n != 0 ? "ON" : "OFF");
        if (id == "fmOscSync")  return juce::String ("OSC KEY SYNC=") + (n != 0 ? "ON" : "OFF");                   // estimada
        if (id == "fmTranspose") return "MIDDLE C = " + yamahaNote (60 + n);                                       // estimada
        if (id.startsWith ("fmPR")) return juce::String::formatted ("P EG RATE %d  =%2d", id[4] - '0', n);         // estimada
        if (id.startsWith ("fmPL")) return juce::String::formatted ("P EG LEVEL %d =%2d", id[4] - '0', n);         // estimada

        const auto op = juce::String::charToString (id[2]);
        const auto suffix = id.substring (3);
        if (suffix == "Coarse" || suffix == "Fine")
        {
            const auto coarse = juce::roundToInt (value ("fm" + op + "Coarse")), fine = juce::roundToInt (value ("fm" + op + "Fine"));
            const auto label = suffix == "Coarse" ? "F COARSE=" : "F FINE  =";
            if (value ("fm" + op + "Fixed") > 0.5f) // estimada: frecuencia fija en Hz, 10^(coarse%4 + fine/100) (FmEngine.h)
            {
                const auto hz = std::pow (10.0, (coarse & 3) + fine / 100.0);
                return label + juce::String (hz, hz >= 1000.0 ? 0 : 1) + "Hz"; // 16 caracteres como mucho
            }
            return label + juce::String::formatted ("%5.2f", (coarse == 0 ? 0.5 : (double) coarse) * (1.0 + fine / 100.0));
        }
        if (suffix == "Detune")    return "OSC DETUNE =" + juce::String (n > 0 ? "+" : "") + juce::String (n); // estimada
        if (suffix == "Fixed")     return juce::String ("OSC MODE=") + (n != 0 ? "FIXED" : "RATIO");           // estimada
        if (suffix == "Level")     return juce::String::formatted ("OUTPUT LEVEL =%2d", n);
        if (suffix == "Vel")       return juce::String::formatted ("KEY VELOCITY = %d", n);
        if (suffix == "RateScale") return juce::String::formatted ("RATE SCALING = %d", n);
        if (suffix == "Ams")       return juce::String::formatted ("A MOD SENS.  =%d", n);
        if (suffix == "Break")     return "BREAK POINT=" + yamahaNote (n);
        if (suffix == "LDepth")    return juce::String::formatted ("L SCALE DEPTH=%2d", n);
        if (suffix == "RDepth")    return juce::String::formatted ("R SCALE DEPTH=%2d", n);
        if (suffix == "LCurve" || suffix == "RCurve")
        {
            static const char* curves[4] = { "-LIN", "-EXP", "+EXP", "+LIN" };
            return juce::String (suffix == "LCurve" ? "L" : "R") + " KEY SCALE=" + curves[juce::jlimit (0, 3, n)];
        }
        if (suffix.length() == 2 && (suffix[0] == 'R' || suffix[0] == 'L'))
            return juce::String::formatted (suffix[0] == 'R' ? "EG RATE %d  =%2d" : "EG LEVEL %d  =%2d", suffix[1] - '0', n);
        return {};
    }

    // Propiedades que cambian con una voz o un preset: la referencia del punto decimal se toma en el siguiente tick (el
    // cambio puede llegar desde otro hilo)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        if (property.toString() == "dx7Voice" || property.toString() == "presetName")
            resetPending = true;
    }

    void timerCallback() override
    {
        const auto now = values();
        int changed = 0, last = -1;
        for (size_t i = 0; i < now.size(); ++i)
            if (std::abs (now[i] - lastSeen[i]) > 1.0e-4f) { ++changed; last = (int) i; }
        // Más de 4 a la vez: carga de voz o preset (no edición): nueva referencia y sin modo EDIT
        if (resetPending.exchange (false) || changed > 4)
        {
            snapshot = now;
            editIndex = -1;
        }
        else if (changed > 0)
        {
            editIndex = last;
            editUntil = juce::Time::getMillisecondCounter() + 2000;
        }
        lastSeen = now;
        if (editIndex >= 0 && juce::Time::getMillisecondCounter() > editUntil)
            editIndex = -1;

        auto isEdited = false;
        for (size_t i = 0; i < now.size(); ++i)
            isEdited = isEdited || std::abs (now[i] - snapshot[i]) > 1.0e-4f;

        const auto voice = processor.apvts.state.getProperty ("dx7Voice").toString();
        const auto voiceNumber = voice.isNotEmpty() ? voice.upToFirstOccurrenceOf ("|", false, false).getIntValue() : 0;
        std::array<juce::String, 2> text;
        if (editIndex >= 0)
        {
            const auto& id = params[(size_t) editIndex]->getParameterID();
            const auto perOperator = id[2] >= '1' && id[2] <= '6';
            text[0] = "ALG" + juce::String (juce::roundToInt (value ("fmAlgorithm"))) + " 111111" + (perOperator ? " OP" + juce::String::charToString (id[2]) : juce::String());
            text[1] = editLine (id, now[(size_t) editIndex]);
        }
        else if (voice.isNotEmpty())
        {
            text[0] = "CARTRIDGE VOICE";
            text[1] = "CRT " + (voiceNumber > 0 ? juce::String (voiceNumber) : juce::String()) + " " + voice.fromFirstOccurrenceOf ("|", false, false);
        }
        else
        {
            text[0] = "INTERNAL VOICE";
            text[1] = "INT  " + processor.apvts.state.getProperty ("presetName").toString();
        }
        if (text != lines || voiceNumber != number || isEdited != edited)
        {
            lines = text;
            number = voiceNumber;
            edited = isEdited;
            repaint();
        }
    }

    KoniSynthProcessor& processor;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> snapshot, lastSeen;
    std::atomic<bool> resetPending { false };
    int editIndex = -1;
    juce::uint32 editUntil = 0;
    std::array<juce::String, 2> lines;
    int number = 0;
    bool edited = false;
};

//==============================================================================
// LED de los 16 botones STEP de la Roland TR-808 (TR-808 Owner's Manual, p. 11): "While the Rhythm is running you will notice
// that the LED indicators on the STEP buttons flash in sequence from the left to the right of the unit". El manual no describe
// un LED de tempo (pp. 3-5: solo los STEP y los de variación A/B). Aquí, una fila de LED rojos pequeños con la luz en el
// paso que da el motor (DrumMachine::currentLed), separados en grupos de 4 como los colores de los botones; todos apagados
// sin patrón. Elección de interfaz: también con los kits CR-78 y Mini Pops (el contador de 16 posiciones es el mismo).
// Colores fijos: no siguen el skin
class DrumStepLeds : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit DrumStepLeds (KoniSynthProcessor& p) : processor (p)
    {
        setTooltip ("Step lights, as on the TR-808: while a pattern runs, the light moves across the 16 steps of the measure.");
        startTimerHz (60);
    }

    void paint (juce::Graphics& g) override
    {
        // Paso de 22 px como mucho, centrado; 4 px más entre grupos de 4
        const auto r = getLocalBounds().toFloat();
        const auto step = juce::jmin (22.0f, (r.getWidth() - 12.0f) / 16.0f);
        const auto d = juce::jlimit (4.0f, 8.0f, juce::jmin (r.getHeight() - 2.0f, step * 0.45f));
        const auto x0 = r.getCentreX() - (16.0f * step + 12.0f) * 0.5f + step * 0.5f;
        for (int k = 0; k < 16; ++k)
        {
            const auto c = juce::Point<float> (x0 + step * (float) k + 4.0f * (float) (k / 4), r.getCentreY());
            const auto led = juce::Rectangle<float> (d, d).withCentre (c);
            if (k == lit)
            {
                g.setColour (juce::Colour (0xffff2a1a).withAlpha (0.3f));
                g.fillEllipse (led.expanded (2.0f));
            }
            g.setColour (k == lit ? juce::Colour (0xffff2a1a) : juce::Colour (0xff3a0d0a));
            g.fillEllipse (led);
        }
    }

private:
    void timerCallback() override
    {
        const auto now = processor.getDrumLed();
        if (now != lit)
        {
            lit = now;
            repaint();
        }
    }

    KoniSynthProcessor& processor;
    int lit = -1;
};

//==============================================================================
// Pantalla del Casio VL-1 (VL-1 Operating Manual; páginas impresas): LCD de 7 segmentos sin retroiluminación, 8 dígitos
// ("Number of digits: 8 digits", p. 32) y una posición más a la izquierda. Contenido, según las figuras del manual:
//  - Notas: "The last three notes pressed will be displayed ... with the last note pressed on the right" (p. 18). Cada nota
//    ocupa 3 posiciones: vacía, octava (segmento d = octava baja "_5", segmento a = alta "‾1", nada = la central) y la nota
//    como número 1-7 (Do-Si) con un ♯ pequeño encima si es tecla negra (figuras: "-_5 3 ‾6", p. 9; "- 1♯ ‾5 _6", p. 18)
//  - Tempo: "[-2]" a la izquierda, de -9 a +9 (p. 14). El "-" de la primera posición es el indicador de tempo, que parpadea
//    (los rayos de la figura de la p. 14); aquí, al pulso del ritmo que da el motor (VL1::rhythmPulse)
// Elecciones de interfaz: el tempo se ve 2 s al cambiarlo; el teclado del VL-1 va de Sol a Si (17 teclas blancas, p. 18) y
// aquí las notas MIDI 60-71 son la octava central, las de debajo la baja y desde la 72 la alta. Estimado: el tempo positivo
// sin signo ("[ 2]"). Sin los puntos de más de 100 notas grabadas (p. 18): KoniSynth no graba notas. Colores fijos: no
// siguen el skin
class Vl1Display : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit Vl1Display (KoniSynthProcessor& p) : processor (p), tempoParam (p.apvts.getRawParameterValue ("vl1Tempo"))
    {
        setTooltip ("The VL-1 display: the last three notes played, the last one on the right (1-7 = C to B; a bar below = "
                    "low octave, above = high octave; a small # = sharp). Changing the rhythm tempo shows it for 2 s. The "
                    "mark on the left blinks with the rhythm.");
        lastTempo = tempo();
        tempoUntil = juce::Time::getMillisecondCounter(); // vencido: sin tempo al abrir
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        // 9 posiciones de h de alto, 0,52·h de ancho y 0,8·h de paso; encima, h·0,3 para el ♯. Panel centrado del mayor
        // tamaño que cabe
        auto r = getLocalBounds().toFloat();
        const auto h = juce::jmin ((r.getHeight() - 10.0f) / 1.5f, (r.getWidth() - 10.0f) / 7.8f);
        if (h < 6.0f)
            return;
        const auto step = 0.8f * h, w = 0.52f * h;
        r = r.withSizeKeepingCentre (7.8f * h + 10.0f, 1.5f * h + 10.0f);
        g.setColour (juce::Colour (0xff15170f)); // marco
        g.fillRoundedRectangle (r, 5.0f);

        // LCD reflectante: gris, segmentos casi negros y los apagados apenas visibles
        const auto glass = r.reduced (5.0f);
        g.setColour (juce::Colour (0xffaab09f));
        g.fillRoundedRectangle (glass, 3.0f);
        const auto on = juce::Colour (0xff1d211a), off = juce::Colour (0xffa5ab9a);
        const auto x0 = glass.getX() + 0.3f * h + (step - w) * 0.5f, y0 = glass.getY() + 0.3f * h;
        for (int k = 0; k < 9; ++k)
        {
            const auto cell = juce::Rectangle<float> (x0 + step * (float) k, y0, w, h);
            SevenSegment::draw (g, cell, cells[(size_t) k], on, off);
            if (sharps[(size_t) k])
                drawSharp (g, { cell.getCentreX() + 0.12f * w, y0 - 0.15f * h }, 0.2f * h, on);
        }
    }

private:
    // ♯ pequeño: dos verticales inclinadas y dos horizontales subiendo a la derecha, de lado 'size' centrado en c
    static void drawSharp (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour colour)
    {
        const auto s = size * 0.5f, t = juce::jmax (1.0f, size * 0.12f);
        g.setColour (colour);
        g.drawLine (c.x - s * 0.3f, c.y + s, c.x - s * 0.1f, c.y - s, t);
        g.drawLine (c.x + s * 0.2f, c.y + s, c.x + s * 0.4f, c.y - s, t);
        g.drawLine (c.x - s * 0.8f, c.y - s * 0.25f, c.x + s * 0.8f, c.y - s * 0.45f, t);
        g.drawLine (c.x - s * 0.8f, c.y + s * 0.45f, c.x + s * 0.8f, c.y + s * 0.25f, t);
    }

    int tempo() const { return juce::roundToInt (tempoParam->load()); }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto t = tempo();
        if (t != lastTempo)
        {
            lastTempo = t;
            tempoUntil = now + 2000;
        }

        std::array<int, 9> c {};
        std::array<bool, 9> s {};
        c[0] = processor.getVl1Pulse() == 0 ? 0 : 0x40; // indicador: fijo sin ritmo, intermitente con él
        if ((int) (tempoUntil - now) > 0) // resta sin signo: válida aunque el contador dé la vuelta
        {
            c[1] = 0x39; // '['
            c[2] = t < 0 ? 0x40 : 0;
            c[3] = SevenSegment::digitMask[std::abs (t) % 10];
            c[4] = 0x0f; // ']'
        }
        else
        {
            static constexpr int degree[12] = { 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7 };
            static constexpr bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
            const auto notes = processor.getVl1Notes();
            for (int n = 0; n < 3; ++n) // n = 0: la última, a la derecha
            {
                const auto code = (int) ((notes >> (8 * n)) & 0xffu);
                if (code == 0)
                    continue;
                const auto midi = code - 1, pos = 8 - 3 * n;
                c[(size_t) pos] = SevenSegment::digitMask[degree[midi % 12]];
                s[(size_t) pos] = black[midi % 12];
                c[(size_t) pos - 1] = midi < 60 ? 0x08 : (midi >= 72 ? 0x01 : 0); // barra de octava: d abajo, a arriba
            }
        }
        if (c != cells || s != sharps)
        {
            cells = c;
            sharps = s;
            repaint();
        }
    }

    KoniSynthProcessor& processor;
    std::atomic<float>* tempoParam;
    int lastTempo = 0;
    juce::uint32 tempoUntil = 0;
    std::array<int, 9> cells {};
    std::array<bool, 9> sharps {};
};

//==============================================================================
// Display del motor Gravis UltraSound. La tarjeta no tenía pantalla: muestra lo que en ella no se veía y cambia el sonido.
// - Mapa de voces del GF1 (14-32): por voz, nota, patch, nivel de la rampa de volumen (12 bits), punto de la envolvente
//   (0-5; S sostenida, R soltada, E acabando con el bucle apagado) y pan (0-15). Un robo de voz destella 300 ms; las voces por
//   encima de las activas salen atenuadas.
// - Pie: voces activas y la frecuencia del chip que dan (617,4 kHz / voces), RAM usada con marcas cada 256 KB, patches
//   residentes, números que suenan con otro patch y profundidad de carga (8/16 bits).
// Lee GusEngine::View (atómicos que escribe el audio) a 30 Hz; colores del skin, como el MIDI Music Player
class GusDisplay : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit GusDisplay (KoniSynthProcessor& p) : processor (p)
    {
        setTooltip ("What the GF1 is doing: each voice with its note, patch, volume level, envelope point (0-5; S held, R released, "
                    "E playing out) and pan (thin line). A stolen voice flashes. Footer: active voices and the chip rate they give, "
                    "card memory in use, resident patches, programs playing a substitute, and the sample depth.");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto ink = Skin::trace();
        const auto lcd = Skin::mono() ? Skin::ramp().bg : Skin::current() == Skin::vintage ? juce::Colour (0xff040a06) : juce::Colour (0xff03080c);
        const auto dim = lcd.interpolatedWith (ink, 0.38f), off = lcd.interpolatedWith (ink, 0.14f);
        const auto hot = Skin::accent(), warn = juce::Colour (0xffff7a59);
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (lcd);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        r = r.reduced (8.0f, 6.0f);

        const auto mono = [] (float h) { return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain); };
        const auto footH = juce::jlimit (16.0f, 22.0f, r.getHeight() * 0.12f);
        auto foot = r.removeFromBottom (footH);
        r.removeFromBottom (4.0f);
        g.setColour (off);
        g.fillRect (r.getX(), r.getBottom() + 1.0f, r.getWidth(), 1.0f);

        // Mapa de voces: 4 columnas × 8 filas (2 × 16 si no cabe)
        const auto cols = r.getWidth() >= 560.0f ? 4 : 2, rows = 32 / cols;
        const auto cellW = r.getWidth() / (float) cols, cellH = r.getHeight() / (float) rows;
        const auto fontH = juce::jlimit (9.0f, 15.0f, cellH * 0.62f);
        g.setFont (mono (fontH));
        const auto now = juce::Time::getMillisecondCounter();
        for (int i = 0; i < 32; ++i)
        {
            const auto& cell = cells[(size_t) i];
            auto c = juce::Rectangle<float> (r.getX() + cellW * (float) (i / rows), r.getY() + cellH * (float) (i % rows), cellW, cellH)
                         .reduced (4.0f, 1.0f);
            const auto inactive = i >= voices;
            juce::Graphics::ScopedSaveState s (g);
            if (inactive) g.setOpacity (0.22f);
            const auto numW = fontH * 1.4f, noteW = fontH * 2.3f, stW = fontH * 0.9f, barW = juce::jmin (64.0f, c.getWidth() * 0.22f);
            g.setColour (dim);
            g.drawText (juce::String (i).paddedLeft ('0', 2), c.removeFromLeft (numW), juce::Justification::centredRight);
            c.removeFromLeft (fontH * 0.4f);
            auto st = c.removeFromRight (stW);
            auto bar = c.removeFromRight (barW).withSizeKeepingCentre (barW, juce::jmin (8.0f, cellH * 0.32f));
            c.removeFromRight (fontH * 0.4f);
            if (inactive) continue;
            if (! cell.sounding)
            {
                g.setColour (dim);
                g.drawText ("free", c.withTrimmedLeft (noteW), juce::Justification::centredLeft);
                continue;
            }
            const auto faded = cell.released ? 0.55f : 1.0f;
            g.setColour (ink.withMultipliedAlpha (faded));
            g.drawText (juce::MidiMessage::getMidiNoteName (cell.note, true, true, 4), c.removeFromLeft (noteW), juce::Justification::centredLeft);
            g.setColour ((now < flashUntil[(size_t) i] ? warn : ink).withMultipliedAlpha (faded));
            g.drawText (cell.patch, c, juce::Justification::centredLeft, true);
            // Nivel: volumen de 12 bits (escala logarítmica del GF1: la barra va en dB) y, debajo, el pan
            g.setColour (off);
            g.fillRect (bar);
            g.setColour (ink);
            g.fillRect (bar.withWidth (bar.getWidth() * (float) cell.level / 4095.0f));
            const auto panY = bar.getBottom() + 2.0f;
            g.setColour (off);
            g.fillRect (bar.getX(), panY, bar.getWidth(), 1.0f);
            g.setColour (hot);
            g.fillRect (bar.getX() + (bar.getWidth() - 3.0f) * (float) cell.pan / 15.0f, panY - 1.0f, 3.0f, 3.0f);
            g.setColour (dim);
            g.drawText (cell.released ? (cell.ending ? "E" : "R") : cell.sustained ? "S" : juce::String (cell.stage), st, juce::Justification::centred);
        }

        // Pie: chip, RAM y carga
        g.setFont (mono (juce::jlimit (9.0f, 14.0f, footH * 0.72f)));
        const auto kHz = 617.4 / juce::jmax (14, voices);
        const auto chip = juce::String ("GF1 ") + juce::String (voices) + " voices  " + juce::String (kHz, 1) + " kHz";
        const auto right = juce::String (usedKB) + " / " + juce::String (ramKB) + " KB  " + juce::String (resident) + " patches  "
                         + juce::String (substituted) + " substituted  " + (eightBit ? "8-bit" : "16-bit") + (table ? "  ULTRAMID.INI" : "");
        const auto chipW = juce::jmin (foot.getWidth() * 0.32f, fontWidth (chip, footH * 0.72f) + 12.0f);
        g.setColour (ink);
        g.drawText (chip, foot.removeFromLeft (chipW), juce::Justification::centredLeft);
        const auto rightW = juce::jmin (foot.getWidth() * 0.62f, fontWidth (right, footH * 0.72f) + 12.0f);
        g.setColour (dim);
        g.drawText (right, foot.removeFromRight (rightW), juce::Justification::centredRight, true);
        auto ram = foot.reduced (6.0f, 0.0f).withSizeKeepingCentre (foot.getWidth() - 12.0f, juce::jmin (9.0f, footH * 0.45f));
        g.setColour (off);
        g.fillRect (ram);
        g.setColour (substituted > 0 ? hot : ink);
        g.fillRect (ram.withWidth (ram.getWidth() * juce::jlimit (0.0f, 1.0f, (float) usedKB / (float) juce::jmax (1, ramKB))));
        g.setColour (dim);
        for (int k = 1; k * 256 < ramKB; ++k) // marcas cada 256 KB
            g.fillRect (ram.getX() + ram.getWidth() * (float) (k * 256) / (float) ramKB, ram.getY() - 3.0f, 1.0f, ram.getHeight() + 6.0f);

        if (names.isEmpty())
        {
            g.setColour (hot);
            g.setFont (mono (juce::jlimit (11.0f, 16.0f, r.getHeight() * 0.08f)));
            g.drawText ("No patches: see Patches...", r, juce::Justification::centred);
        }
        Skin::scanlines (g, getLocalBounds().reduced (3));
    }

private:
    struct Cell
    {
        bool sounding = false, released = false, sustained = false, ending = false;
        int stage = 0, pan = 7, note = 60, level = 0;
        juce::String patch;
        bool operator== (const Cell& o) const
        {
            return sounding == o.sounding && released == o.released && sustained == o.sustained && ending == o.ending && stage == o.stage
                && pan == o.pan && note == o.note && level / 64 == o.level / 64 && patch == o.patch;
        }
    };

    static float fontWidth (const juce::String& text, float h)
    {
        return juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)), text);
    }

    void timerCallback() override
    {
        if (! isShowing()) return;
        if (++namesAge >= 30) { names = processor.getGusPatchNames(); namesAge = 0; } // una vez por segundo: cambian al leer la carpeta
        const auto& v = processor.getGusView();
        bool changed = false;
        const auto now = juce::Time::getMillisecondCounter();
        for (int i = 0; i < 32; ++i)
        {
            const auto st = v.state[(size_t) i].load (std::memory_order_relaxed);
            const auto pl = v.patchLevel[(size_t) i].load (std::memory_order_relaxed);
            Cell c;
            c.sounding = (st & 1u) != 0;
            c.released = (st & 2u) != 0;
            c.sustained = (st & 4u) != 0;
            c.ending = (st & 8u) != 0;
            c.stage = (int) ((st >> 4) & 7u);
            c.pan = (int) ((st >> 8) & 15u);
            c.note = (int) ((st >> 12) & 127u);
            c.level = (int) (pl & 0xFFFu);
            const auto index = (int) (pl >> 12) - 1;
            c.patch = juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
            const auto steals = st >> 20;
            if (steals != lastSteals[(size_t) i]) { lastSteals[(size_t) i] = steals; flashUntil[(size_t) i] = now + 300; changed = true; }
            if (! (c == cells[(size_t) i])) { cells[(size_t) i] = c; changed = true; }
            if (flashUntil[(size_t) i] != 0 && now >= flashUntil[(size_t) i]) { flashUntil[(size_t) i] = 0; changed = true; }
        }
        const auto load = [&changed] (int& to, int value) { if (to != value) { to = value; changed = true; } };
        load (voices, v.voices.load (std::memory_order_relaxed));
        load (ramKB, v.ramKB.load (std::memory_order_relaxed));
        load (usedKB, v.usedKB.load (std::memory_order_relaxed));
        load (resident, v.resident.load (std::memory_order_relaxed));
        load (substituted, v.substituted.load (std::memory_order_relaxed));
        if (const auto e = v.eightBit.load (std::memory_order_relaxed); e != eightBit) { eightBit = e; changed = true; }
        if (const auto t = v.table.load (std::memory_order_relaxed); t != table) { table = t; changed = true; }
        if (changed) repaint();
    }

    KoniSynthProcessor& processor;
    juce::StringArray names;
    int namesAge = 1000;
    std::array<Cell, 32> cells {};
    std::array<juce::uint32, 32> lastSteals {}, flashUntil {};
    int voices = 24, ramKB = 1024, usedKB = 0, resident = 0, substituted = 0;
    bool eightBit = true, table = false;
};

//==============================================================================
// La pantalla del M-VAVE FM-1 (ST7789, 240 × 240 RGB565) tal como la pinta el firmware en el emulador, ampliada con el
// píxel más cercano y con sus colores (no se tiñe con el skin: es la del aparato). Sin firmware en marcha, el estado (dónde
// poner los .fwsc o por qué no arrancó). Pie: velocidad del emulador (1.00x = tiempo real) y cortes
class Fm1Screen : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit Fm1Screen (KoniSynthProcessor& p) : processor (p)
    {
        setTooltip ("The FM-1's own screen, drawn by its firmware running in the emulator. Footer: emulator speed (1.00x = real "
                    "time; below that the sound drops out) and how many dropouts there were. Right-click: Latency (30 ms by "
                    "default). A longer one rides out the emulator's peaks (fast note changes, many voices) at the cost of delay; "
                    "measured: the stock V15 needs about 120 ms with fast jumps between low and high notes.");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        // la imagen arriba, a todo el lado que quepa (escala no entera: con el panel al lado, 240 px a escala 1 quedaba
        // pequeña), y el pie justo debajo
        auto area = getLocalBounds().reduced (4);
        const auto side = std::min (area.getWidth(), area.getHeight() - kFooter);
        const auto screen = juce::Rectangle<int> (side, side).withCentre ({ area.getCentreX(), area.getY() + side / 2 });
        const auto footer = juce::Rectangle<int> (area.getX(), screen.getBottom(), area.getWidth(), kFooter);
        if (view.live && on && hasImage)
        {
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality); // píxeles nítidos
            g.drawImage (image, screen.toFloat());
        }
        else
        {
            g.setColour (juce::Colour (0xff9a9a9a));
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText (view.status.isNotEmpty() ? view.status : juce::String ("Booting..."), screen.reduced (12),
                              juce::Justification::centred, 8);
        }
        g.setColour (juce::Colour (0xffb0b0b0));
        g.setFont (juce::FontOptions (12.0f));
        juce::String info;
        if (view.live)
        {
            info = juce::String (view.speed, 2) + "x real time";
            if (view.underruns > 0)
                info << "   dropouts: " << juce::String ((juce::int64) view.underruns);
        }
        g.drawText (info, footer, juce::Justification::centred);
    }

    static constexpr int kFooter = 18; // el pie (velocidad y cortes)

    // Clic derecho: la latencia del FM-1 (la cola del emulador por delante del audio; ajuste global)
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            return;
        juce::PopupMenu latency;
        const auto now = processor.fm1Latency();
        for (const auto ms : kFm1Latencies)
            latency.addItem (juce::String (ms) + " ms", true, ms == now, [this, ms] { processor.setFm1Latency (ms); });
        juce::PopupMenu m;
        m.addSubMenu ("Latency", latency);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

private:
    void timerCallback() override
    {
        const auto v = processor.getFm1View();
        bool changed = v.status != view.status || v.live != view.live || std::abs (v.speed - view.speed) >= 0.005
                    || v.underruns != view.underruns;
        view = v;
        if (processor.takeFm1Screen (pixels, rev, on))
        {
            const juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < Fm1Engine::kScreen; ++y)
                for (int x = 0; x < Fm1Engine::kScreen; ++x)
                {
                    uint8_t r, gr, b;
                    fm1::rgb565To888 (pixels[(size_t) (y * Fm1Engine::kScreen + x)], r, gr, b);
                    bd.setPixelColour (x, y, juce::Colour (r, gr, b));
                }
            hasImage = true;
            changed = true;
        }
        if (changed)
            repaint();
    }

    KoniSynthProcessor& processor;
    Fm1Engine::View view;
    std::vector<uint16_t> pixels;
    uint64_t rev = 0;
    bool on = false, hasImage = false;
    juce::Image image { juce::Image::RGB, Fm1Engine::kScreen, Fm1Engine::kScreen, true };
};

//==============================================================================
// Un encoder del panel del FM-1 (todos son sin fin en el aparato): arrastrar en vertical (un clic cada 24 px) o la rueda
// (uno por muesca); + = horario. Con marcas de detención, para acertar una opción concreta. Se dibuja con las perillas del
// skin (KoniLookAndFeel::drawRotarySlider, propiedad "endless": sin arco)
class Fm1Encoder : public juce::Component, public juce::SettableTooltipClient
{
public:
    Fm1Encoder (juce::String name, std::function<void (int)> turnFn) : label (std::move (name)), turn (std::move (turnFn))
    {
        shape.getProperties().set ("endless", true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        const auto text = r.removeFromBottom (14);
        const auto side = std::min (r.getWidth(), r.getHeight());
        const auto k = juce::Rectangle<int> (side, side).withCentre (r.getCentre());
        const auto pos = (float) (((clicks % kPerTurn) + kPerTurn) % kPerTurn) / (float) kPerTurn;
        getLookAndFeel().drawRotarySlider (g, k.getX(), k.getY(), k.getWidth(), k.getHeight(), pos, 0.0f,
                                           juce::MathConstants<float>::twoPi, shape);
        // una marca por clic (kPerTurn por vuelta, como gira el dibujo), entre el cuerpo y el borde: la geometría de
        // drawRotarySlider (radio = lado / 2 - 2, cuerpo 0,62 del radio)
        const auto c = k.toFloat().getCentre();
        const auto radius = (float) side * 0.5f - 2.0f, r0 = radius * 0.70f, r1 = radius * 0.92f;
        g.setColour (Skin::white().withAlpha (0.45f));
        for (int i = 0; i < kPerTurn; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / (float) kPerTurn - juce::MathConstants<float>::halfPi;
            g.drawLine (c.x + std::cos (a) * r0, c.y + std::sin (a) * r0, c.x + std::cos (a) * r1, c.y + std::sin (a) * r1, 1.2f);
        }
        g.setColour (Skin::white().withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (label, text, juce::Justification::centred);
    }

    std::function<void()> onPopup; // clic derecho: el menú (MIDI Learn...)

    void mouseDown (const juce::MouseEvent& e) override
    {
        popup = e.mods.isPopupMenu();
        if (popup && onPopup)
            onPopup();
        lastY = e.position.y;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (popup)
            return;
        constexpr float px = 24.0f;
        const auto d = (int) ((lastY - e.position.y) / px);
        if (d != 0)
        {
            lastY -= (float) d * px;
            step (d);
        }
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        wheel += w.deltaY * (w.isReversed ? -1.0f : 1.0f) / 0.05f; // ~0,05 por muesca de rueda en Windows
        auto d = (int) wheel;
        if (d != 0)
        {
            wheel = 0.0f;
            step (d > 0 ? 1 : -1); // uno por muesca
        }
    }

private:
    static constexpr int kPerTurn = 24; // clics por vuelta dibujada (solo el giro del dibujo y sus marcas)
    void step (int d)
    {
        clicks += d;
        turn (d);
        repaint();
    }

    juce::String label;
    std::function<void (int)> turn;
    juce::Slider shape; // (no se muestra: solo lleva las propiedades del dibujo)
    int clicks = 0;
    float lastY = 0.0f, wheel = 0.0f;
    bool popup = false;
};

// Botón y potenciómetro del panel con su menú contextual (el clic derecho no pulsa ni mueve)
// Brillo de un LED del FM-1 a partir de la fracción del tiempo encendido (multiplexado): pleno a 0,08, como bright de
// fm1-live (panel.rs: un LED encendido del todo lo está ~1 tick de cada 11)
inline float fm1LedBright (float f) { return juce::jlimit (0.0f, 1.0f, f / 0.08f); }

struct Fm1Button : juce::TextButton
{
    using juce::TextButton::TextButton;
    std::function<void()> onPopup;
    float led = 0.0f;  // (fm1LedBright) el LED de la tecla, que tiñe el botón como en el aparato
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        juce::TextButton::paintButton (g, over, down);
        if (led > 0.01f)
        {
            const auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (juce::Colour (0xffffa020).withAlpha (0.45f * led));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (juce::Colour (0xffffd080).withAlpha (led));
            g.drawRoundedRectangle (r, 4.0f, 1.5f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onPopup) { onPopup(); return; }
        juce::TextButton::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) juce::TextButton::mouseUp (e);
    }
};

struct Fm1Pot : juce::Slider
{
    std::function<void()> onPopup;
    void mouseDown (const juce::MouseEvent& e) override
    {
        popup = e.mods.isPopupMenu();
        if (popup && onPopup) { onPopup(); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! popup) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! popup) juce::Slider::mouseUp (e); }
    bool popup = false;
};

//==============================================================================
// Las 27 teclas de nota del FM-1 (F3..G5, teclas 14-40 de la matriz) como en fm1-live (panel.rs, keys): 16 blancas en fila
// y 11 negras encima, en los huecos. Se tocan con el ratón, con la velocidad por la altura del clic (1 arriba, 127 abajo,
// key_vel), y se iluminan con su LED: lo que el firmware enciende en cada página (pasos, escalas...). Las 11 de arriba
// llevan impreso en el aparato lo que hacen en el firmware de fábrica (OP1-OP6, PIT, GLD, MONO, POLY; la última, nada);
// las de abajo, sin rótulo
struct Fm1Keys : juce::Component, juce::SettableTooltipClient
{
    std::function<void (int id, int vel)> onDown;
    std::function<void (int id)> onUp;
    std::array<float, 41> leds {};

    struct Key { int id; juce::Rectangle<float> r; bool black; const char* label = ""; };
    // en las unidades de fm1-live (teclas de 62 de ancho, 11 de hueco; blancas de 106 de alto, negras de 94 encima)
    std::vector<Key> layout() const
    {
        constexpr float kw = 62.0f, gap = 11.0f, x0 = 61.0f, y0 = 574.0f, spanW = 15 * (kw + gap) + kw, spanH = 212.0f;
        const auto area = getLocalBounds().toFloat();
        const auto sx = area.getWidth() / spanW, sy = area.getHeight() / spanH;
        const auto rect = [&] (float x, float y, float w, float h)
        {
            return juce::Rectangle<float> ((x - x0) * sx, (y - y0) * sy, w * sx, h * sy);
        };
        std::vector<Key> v;
        static const int whites[] = { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26 };
        for (int i = 0; i < 16; ++i)
            v.push_back ({ 14 + whites[i], rect (x0 + (float) i * (kw + gap), 680.0f, kw, 106.0f), false });
        static const std::pair<int, int> blacks[] = { { 1, 0 }, { 3, 1 }, { 5, 2 }, { 8, 4 }, { 10, 5 }, { 13, 7 }, { 15, 8 },
                                                      { 17, 9 }, { 20, 11 }, { 22, 12 }, { 25, 14 } };
        static const char* const labels[] = { "OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLD", "MONO", "POLY", "" };
        for (size_t i = 0; i < std::size (blacks); ++i)
        {
            const auto [n, after] = blacks[i];
            const auto cx = x0 + (float) (after + 1) * (kw + gap) - gap / 2.0f;
            v.push_back ({ 14 + n, rect (cx - kw / 2.0f, 574.0f, kw, 94.0f), true, labels[i] });
        }
        return v;
    }

    void paint (juce::Graphics& g) override
    {
        for (const auto& k : layout())
        {
            const auto lit = fm1LedBright (leds[(size_t) k.id]);
            const auto heat = k.id == held ? 0.55f : lit * 0.6f;
            const auto base = juce::Colour (0xff5d4a4c), amber = juce::Colour (0xffffa020);
            const auto r = k.r.reduced (0.5f);
            g.setGradientFill (juce::ColourGradient (base.brighter (0.15f).interpolatedWith (amber, heat), r.getX(), r.getY(),
                                                     base.darker (0.25f).interpolatedWith (amber, heat * 0.8f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) / 2.0f);
            g.setColour (juce::Colour (0xff8f7c7e).interpolatedWith (juce::Colour (0xffffe2b0), k.id == held ? 1.0f : lit));
            g.drawRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) / 2.0f, 1.0f);
            if (*k.label != 0)
            {
                // el rótulo impreso: blanco roto sobre la tecla (oscuro si está encendida, para que se lea sobre el ámbar)
                g.setColour (juce::Colour (0xffe8dcd0).interpolatedWith (juce::Colour (0xff3a2a20), juce::jmax (heat, lit) > 0.3f ? 1.0f : 0.0f));
                g.setFont (juce::FontOptions (juce::jlimit (6.0f, 11.0f, r.getHeight() * 0.28f), juce::Font::bold));
                g.drawFittedText (k.label, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            }
        }
    }

    // (las negras primero: están encima de la fila de blancas)
    const Key* hit (juce::Point<float> p) const
    {
        static thread_local std::vector<Key> ks;
        ks = layout();
        for (bool black : { true, false })
            for (const auto& k : ks)
                if (k.black == black && k.r.contains (p))
                    return &k;
        return nullptr;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        if (const auto* k = hit (e.position))
        {
            held = k->id;
            const auto vel = juce::roundToInt (1.0f + 126.0f * juce::jlimit (0.0f, 1.0f, (e.position.y - k->r.getY()) / k->r.getHeight()));
            if (onDown) onDown (held, vel);
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (held >= 0)
        {
            if (onUp) onUp (held);
            held = -1;
            repaint();
        }
    }

    int held = -1;
};

//==============================================================================
// El aparato: la pantalla en el centro y, a los lados, los mandos del panel del FM-1 en su sitio (disposición del dibujo
// de controles de Felucca, como fm1-emu crates/fm1-live/src/panel.rs, MIT): a la izquierda MASTER (potenciómetro, ADC
// canal 4), SELECT, PRESETS, ALGORITHM y OCT- / OCT+; a la derecha K1-K4, dos filas de seis botones y, debajo, las 27
// teclas (Fm1Keys; las notas también llegan por MIDI). Botones y teclas se iluminan con sus LED. Los botones se mantienen
// mientras se pulsan; los encoders mandan clics (Fm1Engine::panel)
class Fm1Device : public juce::Component, private juce::Timer
{
public:
    explicit Fm1Device (KoniSynthProcessor& p) : processor (p), screen (p)
    {
        addAndMakeVisible (screen);
        // botones: (rótulo, tecla de la matriz, ayuda). Los doce de la derecha y, al final, OCT- y OCT+ (0 y 1): la octava del
        // firmware solo mueve sus propias teclas (el MIDI que entra no se transpone); Z / X del teclado del PC también las pulsan
        static const std::array<std::tuple<const char*, int, const char*>, 14> buttons { {
            { "FX", 2, "Effects page" }, { "SCL", 3, "Scale page" }, { "ENV", 4, "Envelope page" }, { "LFO", 5, "LFO page" },
            { "EDIT", 6, "Edit page" }, { "GLO", 7, "Global settings page" }, { "HOME", 8, "Home screen" }, { "SAVE", 9, "Save" },
            { "ARP", 10, "Arpeggiator" }, { "SEQ", 11, "Sequencer" }, { "PLAY", 12, "Play / stop" }, { "REC", 13, "Record" },
            { "OCT-", 0, "Octave down for the firmware (its own keys and what it shows; incoming MIDI notes are not transposed). "
                         "Z on the PC keyboard also presses it" },
            { "OCT+", 1, "Octave up for the firmware (its own keys and what it shows; incoming MIDI notes are not transposed). "
                         "X on the PC keyboard also presses it" } } };
        for (const auto& [name, id, help] : buttons)
        {
            auto* b = keys.add (new Fm1Button (name));
            b->onPopup = [this, b, id = id] { learnMenu (id, b, nullptr); };
            learnTargets.push_back ({ b, id });
            b->setTooltip (juce::String (help) + " (the FM-1's " + name + " button; held while the mouse is down). What it does "
                           "depends on the firmware.");
            b->onStateChange = [this, b, id = id]
            {
                const bool down = b->isDown();
                if (down != held[(size_t) id])
                {
                    held[(size_t) id] = down;
                    processor.fm1Panel ({ fm1::PanelAction::Key, (uint8_t) id, down ? 1 : 0 });
                }
            };
            addAndMakeVisible (b);
        }
        // encoders: (rótulo, encoder de la matriz)
        static const std::array<std::pair<const char*, int>, 7> encs { { { "SELECT", 0 }, { "PRESETS", 6 }, { "ALGORITHM", 1 },
                                                                          { "K1", 2 }, { "K2", 3 }, { "K3", 4 }, { "K4", 5 } } };
        for (const auto& [name, id] : encs)
        {
            auto* e = encoders.add (new Fm1Encoder (name, [this, id = id] (int clicks)
            {
                processor.fm1Panel ({ fm1::PanelAction::Enc, (uint8_t) id, clicks });
            }));
            e->onPopup = [this, e, id = id] { learnMenu (MidiLearn::fm1EncFirst + id, e, nullptr); };
            learnTargets.push_back ({ e, MidiLearn::fm1EncFirst + id });
            e->setTooltip (juce::String ("The FM-1's ") + name + " encoder (endless, in steps): drag up / down (one step every 24 px) "
                           "or one step per mouse-wheel notch. What it does depends on the firmware.");
            addAndMakeVisible (e);
        }
        master.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        master.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        master.setRange (0.0, 1023.0, 1.0);
        master.setValue (kMasterDefault, juce::dontSendNotification);
        master.setDoubleClickReturnValue (true, kMasterDefault);
        master.setTooltip ("The FM-1's MASTER volume pot (10-bit ADC, 0-1023; default 900). Double-click to reset.");
        master.onValueChange = [this] { sendMaster(); };
        master.onPopup = [this] { learnMenu (MidiLearn::fm1Master, &master, [this] { master.setValue (kMasterDefault); }); };
        learnTargets.push_back ({ &master, MidiLearn::fm1Master });
        addAndMakeVisible (master);
        masterLabel.setText ("MASTER", juce::dontSendNotification);
        masterLabel.setJustificationType (juce::Justification::centred);
        masterLabel.setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (masterLabel);
        // dos botones del emulador (no del aparato): PANIC lo reinicia con la memoria guardada; RESET, con la de fábrica
        panicButton.setTooltip ("FM-1 panic: restarts the emulated FM-1 with its saved memory, like switching it off and on. Stops "
                          "anything the firmware keeps playing (loops, sequences, held notes) that the main PANIC can't. Edits "
                          "not stored with SAVE are lost, as on the device. Right-click: MIDI Learn.");
        panicButton.learn = &processor.midiLearn;
        panicButton.action = MidiLearn::actionFm1Panic;
        panicButton.onClick = [this] { processor.fm1Panic(); };
        addAndMakeVisible (panicButton);
        resetButton.setTooltip ("FM-1 reset: restarts the firmware with its factory memory. Its saved memory (<firmware>.fm1save, "
                          "next to the .fwsc) goes to the recycle bin and the preset's copy is cleared. Asks first.");
        resetButton.onClick = [this]
        {
            const auto fw = processor.fm1Loaded();
            overlay = Overlay::create (*this, "FM-1 reset",
                "Restart " + (fw == juce::File() ? juce::String ("the firmware") : fw.getFileName()) + " with its factory memory? "
                "Everything saved on the device with SAVE (presets, songs, settings) is discarded: its "
                + fw.withFileExtension ("fm1save").getFileName() + " goes to the recycle bin.",
                std::nullopt, "Reset", "Cancel", [this] (bool ok, const juce::String&, int)
                {
                    overlay.reset();
                    if (! ok)
                        return;
                    juce::String error;
                    if (! processor.fm1FactoryReset (error) && error.isNotEmpty())
                        overlay = Overlay::create (*this, "FM-1 reset", error, std::nullopt, "OK", {},
                                                   [this] (bool, const juce::String&, int) { overlay.reset(); });
                });
        };
        addAndMakeVisible (resetButton);
        noteKeys.onDown = [this] (int id, int vel)
        {
            processor.fm1Panel ({ fm1::PanelAction::KeyVel, 0, vel });
            processor.fm1Panel ({ fm1::PanelAction::Key, (uint8_t) id, 1 });
        };
        noteKeys.onUp = [this] (int id) { processor.fm1Panel ({ fm1::PanelAction::Key, (uint8_t) id, 0 }); };
        noteKeys.setTooltip ("The FM-1's 27 keys (F3-G5), lit by the firmware's LEDs. Click to play one: the higher on the key, "
                             "the softer. What the keys do and light up depends on the firmware and its current page.");
        addAndMakeVisible (noteKeys);
        startTimerHz (30);
    }

    ~Fm1Device() override
    {
        // un botón o una tecla que se quedara pulsada al cerrar el editor se suelta
        for (size_t i = 0; i < held.size(); ++i)
            if (held[i])
                processor.fm1Panel ({ fm1::PanelAction::Key, (uint8_t) i, 0 });
        if (noteKeys.held >= 0)
            processor.fm1Panel ({ fm1::PanelAction::Key, (uint8_t) noteKeys.held, 0 });
    }

    void paint (juce::Graphics& g) override { g.fillAll (Skin::c (0xff17191c)); }

    // marco ámbar en el mando que espera su CC (MIDI Learn), como los botones de acción (MidiActionButton)
    void paintOverChildren (juce::Graphics& g) override
    {
        const auto waiting = processor.midiLearn.learning.load();
        for (const auto& [c, k] : learnTargets)
            if (waiting == MidiLearn::fm1Base + k)
            {
                g.setColour (Skin::c (0xffffb000));
                g.drawRoundedRectangle (c->getBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
            }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        // la pantalla, lo más grande que quepa a lo alto (con su pie) dejando a los lados su ancho mínimo; lo que sobre de
        // ancho se reparte entre los lados (40 % / 60 %). Arriba del todo: sin margen encima
        constexpr int kGap = 20, kPad = 8; // hueco entre columnas; marco de Fm1Screen (4 px por lado)
        const auto side = std::max (120, std::min (r.getHeight() - Fm1Screen::kFooter - kPad,
                                                   r.getWidth() - kLeftMin - kRightMin - 2 * kGap - kPad));
        const auto extra = std::max (0, r.getWidth() - (side + kPad) - 2 * kGap - kLeftMin - kRightMin);
        auto left = r.removeFromLeft (kLeftMin + extra * 2 / 5);
        r.removeFromLeft (kGap);
        auto right = r.removeFromRight (r.getWidth() - (side + kPad) - kGap);
        screen.setBounds (r.getX(), r.getY(), side + kPad, side + kPad + Fm1Screen::kFooter);
        constexpr int kSpace = 12; // entre mandos
        left.removeFromTop (kSpace); // (separados del marco)
        right.removeFromTop (kSpace);
        // izquierda: MASTER y SELECT; PRESETS y ALGORITHM
        const auto colW = left.getWidth() / 2;
        constexpr int kTools = 26, kOct = 30; // alto de PANIC / RESET y de OCT- / OCT+, abajo
        const auto knob = std::min ({ colW - kSpace, (left.getHeight() - 28 - 4 * kSpace - kTools - kOct) / 2, 100 });
        auto row1 = left.removeFromTop (knob + 14);
        left.removeFromTop (2 * kSpace);
        auto row2 = left.removeFromTop (knob + 14);
        auto masterArea = row1.removeFromLeft (colW).reduced (kSpace / 2, 0);
        masterLabel.setBounds (masterArea.removeFromBottom (14));
        master.setBounds (masterArea);
        encoders[0]->setBounds (row1.reduced (kSpace / 2, 0));
        encoders[1]->setBounds (row2.removeFromLeft (colW).reduced (kSpace / 2, 0));
        encoders[2]->setBounds (row2.reduced (kSpace / 2, 0));
        // abajo a la izquierda: PANIC y RESET del emulador
        auto tools = left.removeFromBottom (kTools);
        panicButton.setBounds (tools.removeFromLeft (colW).reduced (kSpace / 2, 0));
        resetButton.setBounds (tools.reduced (kSpace / 2, 0));
        // encima: OCT- y OCT+
        left.removeFromBottom (kSpace);
        auto oct = left.removeFromBottom (kOct);
        keys[12]->setBounds (oct.removeFromLeft (colW).reduced (kSpace / 2, 0));
        keys[13]->setBounds (oct.reduced (kSpace / 2, 0));
        // derecha: K1-K4 y dos filas de seis botones, con hueco para su rótulo entero
        auto kRow = right.removeFromTop (std::min (right.getWidth() / 4 - kSpace, 84) + 14);
        const auto kw = kRow.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            encoders[3 + i]->setBounds (kRow.removeFromLeft (kw).reduced (kSpace / 2, 0));
        right.removeFromTop (2 * kSpace);
        for (int row = 0; row < 2; ++row)
        {
            auto line = right.removeFromTop (34);
            const auto bw = line.getWidth() / 6;
            for (int i = 0; i < 6; ++i)
                keys[row * 6 + i]->setBounds (line.removeFromLeft (bw).reduced (kSpace / 2, 0));
            right.removeFromTop (kSpace + 4);
        }
        // debajo, las 27 teclas: con la proporción de fm1-live (1157 × 212) en lo que quede, hasta ese alto
        const auto keysW = right.getWidth() - kSpace;
        const auto keysH = juce::jmin (right.getHeight() - kSpace, juce::roundToInt ((float) keysW * 212.0f / 1157.0f));
        noteKeys.setBounds (right.getX() + kSpace / 2, right.getY(), keysW, juce::jmax (0, keysH));
    }

    // anchos mínimos de los lados (Editor: el del cuerpo = los dos + la pantalla a 240 px)
    static constexpr int kLeftMin = 190, kRightMin = 420;

private:
    static constexpr double kMasterDefault = 900.0; // Fm1Periph.cpp: adcValue[4] al encender

    void sendMaster() { processor.fm1Panel ({ fm1::PanelAction::Adc, 4, (int32_t) master.getValue() }); }

    MidiActionButton<juce::TextButton> panicButton { "PANIC" };
    juce::TextButton resetButton { "RESET" };
    std::unique_ptr<Overlay> overlay;

    // al arrancar (o cambiar) el firmware la máquina vuelve a 900: se le manda la posición del mando. MASTER movido por un
    // CC asignado: la perilla lo sigue (sin volver a mandarlo). El marco de MIDI Learn, al cambiar
    void timerCallback() override
    {
        const auto live = processor.getFm1View().live;
        if (live && ! wasLive && master.getValue() != kMasterDefault)
            sendMaster();
        wasLive = live;
        if (const auto v = processor.fm1MasterFromMidi.exchange (-1); v >= 0)
            master.setValue (v, juce::dontSendNotification);
        if (const auto waiting = processor.midiLearn.learning.load(); waiting != shownLearning)
        {
            shownLearning = waiting;
            repaint();
        }
        // los LED: los botones (teclas 0-13 de la matriz) y las 27 teclas
        if (std::array<float, 41> l; processor.fm1TakeLeds (l, ledsRev))
        {
            for (const auto& [c, k] : learnTargets)
                if (k < 14)
                    if (auto* b = dynamic_cast<Fm1Button*> (c); b != nullptr && std::abs (b->led - fm1LedBright (l[(size_t) k])) > 0.02f)
                    {
                        b->led = fm1LedBright (l[(size_t) k]);
                        b->repaint();
                    }
            if (l != noteKeys.leds)
            {
                noteKeys.leds = l;
                noteKeys.repaint();
            }
        }
    }

    // Menú contextual de un mando del panel (MidiLearn::fm1Base + k): MIDI Learn, Clear MIDI y, si tiene, Reset
    void learnMenu (int k, juce::Component* target, std::function<void()> reset)
    {
        auto& learn = processor.midiLearn;
        const auto index = MidiLearn::fm1Base + k;
        const auto cc = learn.ccFor (index);
        juce::PopupMenu m;
        m.addItem ("MIDI Learn", [&learn, index] { learn.learning = index; });
        m.addItem (cc >= 0 ? "Clear MIDI (CC " + juce::String (cc) + ")" : juce::String ("Clear MIDI"), cc >= 0, false,
                   [&learn, index] { learn.clear (index); });
        if (reset)
        {
            m.addSeparator();
            m.addItem ("Reset", std::move (reset));
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
    }

    KoniSynthProcessor& processor;
    Fm1Screen screen;
    juce::OwnedArray<Fm1Button> keys;
    Fm1Keys noteKeys;
    uint64_t ledsRev = 0;
    juce::OwnedArray<Fm1Encoder> encoders;
    Fm1Pot master;
    std::vector<std::pair<juce::Component*, int>> learnTargets; // mando y su k de MidiLearn (fm1Base + k)
    int shownLearning = -1;
    juce::Label masterLabel;
    std::array<bool, 14> held {};
    bool wasLive = false;
};

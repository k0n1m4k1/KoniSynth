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
        const auto w = r.getWidth() / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const auto leadingZero = (i == 0 && digits[0] == 0) || (i == 1 && digits[0] == 0 && digits[1] == 0);
            drawDigit (g, r.withX (r.getX() + w * (float) i).withWidth (w).reduced (3.0f, 0.0f), leadingZero ? -1 : digits[i], lit, off);
        }
        const auto dot = juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getX() + 3.0f * w, r.getBottom() - 2.0f });
        g.setColour (lit);
        g.fillEllipse (dot);

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

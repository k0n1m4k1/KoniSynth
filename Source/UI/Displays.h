// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

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
    // Segmentos a..g (orden clásico); -1 = dígito apagado
    static void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, int d, juce::Colour lit, juce::Colour off)
    {
        static constexpr unsigned char masks[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
        const auto mask = d < 0 ? 0 : masks[d];
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

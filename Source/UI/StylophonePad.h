// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// UI: teclado del Stylophone, en lugar del piano cuando el motor es Stylophone. Se toca con el ratón como con el stylus:
// pulsar toca la placa, arrastrar pasa de placa en placa (con el clic de cada contacto, como el original), soltar levanta el
// stylus. 1968: una fila de 20 placas metálicas desde la nota más grave del registro. Gen X-2: dos filas tipo piano (teclas
// blancas abajo, negras arriba; 25 notas desde C3 + OCTAVE) y la tira de expresión encima, que actúa como en el aparato
// (Filter/Osc desde el centro, LFO solo la mitad derecha). Con Range = Full keyboard el MIDI toca todo el teclado; el pad
// muestra la ventana del registro. Las notas van al procesador por el MidiKeyboardState del teclado en pantalla.
class StylophonePad : public juce::Component, private juce::Timer
{
public:
    explicit StylophonePad (KoniSynthProcessor& p) : koni (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (15);
    }

    ~StylophonePad() override { release(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 6.0f);
        if (genx)
        {
            // Tira de expresión: centro marcado; modo en la etiqueta
            const auto strip = stripArea();
            g.setColour (juce::Colour (0xff2a2a2e));
            g.fillRoundedRectangle (strip, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawVerticalLine (juce::roundToInt (strip.getCentreX()), strip.getY() + 2.0f, strip.getBottom() - 2.0f);
            static const char* modes[3] = { "STRIP: FILTER (wah)", "STRIP: OSC (pitch)", "STRIP: LFO (vibrato, right half)" };
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (modes[juce::jlimit (0, 2, stripMode)], strip.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
            if (stripActive)
            {
                g.setColour (juce::Colour (0xffff5050));
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ stripX, strip.getCentreY() }));
            }
        }
        for (const auto& k : keys())
        {
            const auto active = k.note == touchedNote;
            // Placas doradas (pulsada: más clara)
            const juce::ColourGradient metal (juce::Colour (active ? 0xfffff3c4 : 0xffeccb6e), k.area.getX(), k.area.getY(),
                                              juce::Colour (active ? 0xffd9a94a : 0xff9c7426), k.area.getX(), k.area.getBottom(), false);
            g.setGradientFill (metal);
            g.fillRoundedRectangle (k.area, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (k.area, 3.0f, 1.0f);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::MidiMessage::getMidiNoteName (k.note, true, true, 4), k.area.reduced (2.0f, 3.0f), juce::Justification::centredBottom);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { touch (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { touch (e.position); }
    void mouseUp (const juce::MouseEvent&) override { release(); }

private:
    struct Key { int note; juce::Rectangle<float> area; };

    juce::Rectangle<float> stripArea() const { return getLocalBounds().toFloat().reduced (6.0f).removeFromTop (22.0f); }

    // Placas visibles según el modelo
    std::vector<Key> keys() const
    {
        std::vector<Key> list;
        auto area = getLocalBounds().toFloat().reduced (6.0f);
        if (! genx)
        {
            // 1968: 20 placas en fila (naturales y alteraciones iguales, como la placa de circuito impreso del original)
            const auto w = area.getWidth() / 20.0f;
            for (int k = 0; k < 20; ++k)
                list.push_back ({ lowNote + k, juce::Rectangle<float> (area.getX() + k * w + 1.5f, area.getY() + 6.0f, w - 3.0f, area.getHeight() - 12.0f) });
            return list;
        }
        area.removeFromTop (28.0f); // tira de expresión
        const auto top = area.removeFromTop (area.getHeight() * 0.45f);
        const auto bottom = area.withTrimmedTop (4.0f);
        // Gen X-2: 15 teclas blancas (C..C, dos octavas) abajo y las 10 negras arriba, centradas entre sus blancas
        static const int whiteSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const auto w = bottom.getWidth() / 15.0f;
        for (int i = 0; i < 15; ++i)
        {
            const auto note = lowNote + 12 * (i / 7) + whiteSteps[i % 7];
            list.push_back ({ note, juce::Rectangle<float> (bottom.getX() + i * w + 1.5f, bottom.getY(), w - 3.0f, bottom.getHeight()) });
            const auto degree = i % 7;
            if (i < 14 && degree != 2 && degree != 6) // E-F y B-C no tienen negra
                list.push_back ({ note + 1, juce::Rectangle<float> (bottom.getX() + (i + 0.5f) * w + 3.0f, top.getY(), w - 6.0f, top.getHeight()) });
        }
        return list;
    }

    void touch (juce::Point<float> pos)
    {
        if (genx && stripArea().expanded (0.0f, 2.0f).contains (pos) && touchedNote < 0)
        {
            const auto s = stripArea();
            stripX = juce::jlimit (s.getX(), s.getRight(), pos.x);
            stripActive = true;
            koni.setStyloStrip ((stripX - s.getCentreX()) / (0.5f * s.getWidth()));
            repaint();
            return;
        }
        if (stripActive) return; // el gesto empezó en la tira
        int note = -1;
        for (const auto& k : keys())
            if (k.area.expanded (1.5f, 0.0f).contains (pos)) { note = k.note; break; }
        if (note == touchedNote || note < 0) return; // entre placas: el stylus sigue en la última
        if (touchedNote >= 0) koni.keyboardState.noteOff (1, touchedNote, 0.0f);
        touchedNote = note;
        koni.keyboardState.noteOn (1, note, 0.8f);
        repaint();
    }

    void release()
    {
        if (touchedNote >= 0) koni.keyboardState.noteOff (1, touchedNote, 0.0f);
        touchedNote = -1;
        if (stripActive) koni.setStyloStrip (0.0f); // sin el dedo, la tira vuelve al centro
        stripActive = false;
        repaint();
    }

    void timerCallback() override
    {
        auto& s = koni.apvts;
        const auto g = (int) s.getRawParameterValue ("styloModel")->load() == 1;
        const auto reg = juce::jlimit (0, 2, (int) s.getRawParameterValue ("styloRegister")->load());
        const auto low = g ? 48 + 12 * juce::jlimit (-1, 1, (int) s.getRawParameterValue ("genxOctave")->load())
                           : 45 + 12 * (reg == 2) - 12 * (reg == 1);
        const auto mode = (int) s.getRawParameterValue ("genxStrip")->load();
        if (g != genx || low != lowNote || mode != stripMode)
        {
            if (low != lowNote) release();
            genx = g; lowNote = low; stripMode = mode;
            repaint();
        }
    }

    KoniSynthProcessor& koni;
    bool genx = false, stripActive = false;
    int lowNote = 45, stripMode = 1, touchedNote = -1;
    float stripX = 0.0f;
};

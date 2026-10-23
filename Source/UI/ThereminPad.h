// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// UI: pad del theremin. X = distancia de la mano a la antena de tono (Physical) o tono lineal (Ideal); Y = volumen.
// La rejilla marca dónde suena cada semitono según el modelo activo, incluido el descenso de tono por el
// acoplamiento (frecuencia audible = √(Δf² - Lock²)); la zona sombreada es la de enganche (silencio).
// Pulsar y arrastrar toca; soltar lleva el volumen a 0 (la mano se aleja de la antena).
// Escribe los parámetros como gestos, así el DAW puede grabarlos como automatización.
class ThereminPad : public juce::Component
{
public:
    explicit ThereminPad (APVTS& s)
        : pitch  (*s.getParameter ("thereminPitch"),  [this] (float v) { pitchValue = v;  repaint(); }),
          volume (*s.getParameter ("thereminVolume"), [this] (float v) { volumeValue = v; repaint(); }),
          range  (*s.getParameter ("thereminRange"),  [this] (float v) { rangeValue = v;  repaint(); }),
          model  (*s.getParameter ("thereminModel"),  [this] (float v) { physical = v < 0.5f; repaint(); }),
          fieldP (*s.getParameter ("thereminField"),  [this] (float v) { fieldValue = v;  repaint(); }),
          lock   (*s.getParameter ("thereminLock"),   [this] (float v) { lockValue = v;   repaint(); }),
          zero   (*s.getParameter ("thereminZero"),   [this] (float v) { zeroValue = v;   repaint(); })
    {
        for (auto* a : { &pitch, &volume, &range, &model, &fieldP, &lock, &zero })
            a->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setFont (juce::FontOptions (12.0f));

        const auto topHz = (double) Theremin::lowHz * std::exp2 ((double) rangeValue);
        field.configure (topHz, fieldValue, zeroValue);

        auto xToPixel = [&r] (double x) { return r.getX() + r.getWidth() * (float) x; };

        // Zona de enganche: entre el punto de batido cero y donde el batido alcanza Lock
        if (physical && lockValue > 0.0f)
        {
            const auto xZero = juce::jlimit (0.0, 1.0, ThereminField::xForDistance (zeroValue));
            const auto xLock = field.xForBeat (lockValue);
            if (xLock > xZero)
            {
                g.setColour (juce::Colour (0xffc84242).withAlpha (0.15f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xToPixel (xZero), r.getY(), xToPixel (xLock), r.getBottom()));
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawText ("lock", juce::Rectangle<float>::leftTopRightBottom (xToPixel (xZero), r.getBottom() - 16.0f, xToPixel (xLock), r.getBottom()),
                            juce::Justification::centred);
            }
        }

        // Rejilla: una línea por semitono hasta el tono máximo; naturales más visibles que alteraciones; C con etiqueta
        for (int note = 24; note <= 120; ++note)
        {
            const auto hz = 440.0 * std::exp2 ((note - 69) / 12.0);
            if (hz > topHz * 1.0001)
                break;

            double x;
            if (physical)
                x = field.xForBeat (std::sqrt (hz * hz + (double) lockValue * lockValue)); // batido necesario para oír hz
            else
                x = note < Theremin::lowNote ? -1.0 : (note - Theremin::lowNote) / (12.0 * rangeValue);
            if (x < 0.0)
                continue;

            const auto px = xToPixel (x);
            const auto isC = note % 12 == 0;
            const auto isSharp = juce::MidiMessage::getMidiNoteName (note, true, false, 4).contains ("#");

            g.setColour (juce::Colours::white.withAlpha (isC ? 0.45f : (isSharp ? 0.06f : 0.16f)));
            g.drawVerticalLine (juce::roundToInt (px), r.getY(), r.getBottom());

            if (isC)
                g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 4),
                            juce::Rectangle<float> (px + 3.0f, r.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::left);
        }

        // Lectura del tono estático en la posición del pad (sin glide, LFO ni deriva)
        juce::String reading;
        double hz;
        if (physical)
        {
            const auto beat = std::abs (field.beatAt (ThereminField::distanceForX (pitchValue)));
            hz = beat > lockValue ? std::sqrt (beat * beat - (double) lockValue * lockValue) : 0.0;
            reading << juce::String (ThereminField::distanceForX (pitchValue), 1) << " cm   ";
        }
        else
        {
            hz = Theremin::lowHz * std::exp2 (rangeValue * pitchValue);
        }

        if (hz >= 20.0)
        {
            const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const auto nearest = juce::roundToInt (midi);
            const auto cents = juce::roundToInt (100.0 * (midi - nearest));
            reading << juce::MidiMessage::getMidiNoteName (nearest, true, true, 4)
                    << (cents >= 0 ? " +" : " ") << cents << " ct   " << juce::String (hz, 1) << " Hz";
        }
        else
        {
            reading << (hz > 0.0 ? "sub-audio" : "locked");
        }
        reading << "   vol " << juce::roundToInt (volumeValue * 100.0f) << " %";

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawText (reading, r.reduced (8.0f, 4.0f).removeFromTop (16.0f), juce::Justification::left);

        if (! held && volumeValue <= 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (physical ? "Click & drag: X = hand distance (antenna on the right), Y = volume"
                                 : "Click & drag: X = pitch, Y = volume",
                        r, juce::Justification::centred);
            return;
        }

        const juce::Point<float> c (xToPixel (pitchValue), r.getBottom() - volumeValue * r.getHeight());
        g.setColour (juce::Colour (0xff42a2c8));
        g.drawVerticalLine (juce::roundToInt (c.x), r.getY(), r.getBottom());
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        held = true;
        pitch.beginGesture();
        volume.beginGesture();
        update (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override { update (e); }

    void mouseUp (const juce::MouseEvent&) override
    {
        held = false;
        volume.setValueAsPartOfGesture (0.0f);
        pitch.endGesture();
        volume.endGesture();
    }

private:
    void update (const juce::MouseEvent& e)
    {
        const auto x = juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth());
        const auto y = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / (float) getHeight());
        pitch.setValueAsPartOfGesture (x);
        volume.setValueAsPartOfGesture (y);
    }

    float pitchValue = 0.5f, volumeValue = 0.0f, rangeValue = 3.0f, fieldValue = 10.0f, lockValue = 12.0f, zeroValue = 60.0f;
    bool physical = true, held = false;
    ThereminField field;
    juce::ParameterAttachment pitch, volume, range, model, fieldP, lock, zero;
};

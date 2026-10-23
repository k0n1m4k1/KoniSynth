// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Campo de tono del theremin, modelo físico (lo usan el audio y el pad).
// Constantes de orden de magnitud de un Moog Etherwave: estimadas, no medidas.
//   oscilador variable: f_var(d) = f0·√(C0 / (C0 + C_h(d))),  capacidad de la mano: C_h(d) = Ck / (d + d0)
//   batido audible = f_ref - f_var(d), con f_ref = f_var(dZero): batido 0 a la distancia de afinación
// Ck se ajusta por bisección para que el batido en dMin valga topHz. Todo en double: con f0 = 260 kHz,
// float (ε ≈ 6e-8) daría ~0,016 Hz de resolución en el batido, visible en graves.
struct ThereminField
{
    static constexpr double f0 = 260000.0, C0 = 20.0e-12; // Hz, F
    static constexpr double dMin = 2.0, dMax = 60.0;       // cm: recorrido del pad (x = 1 -> dMin, x = 0 -> dMax)

    void configure (double topHz, double newD0, double newDZero)
    {
        if (topHz == cachedTop && newD0 == d0 && newDZero == dZero)
            return;
        cachedTop = topHz;
        d0 = newD0;
        dZero = newDZero;

        double lo = 0.0, hi = 1.0e-9; // F·cm; beatAt(dMin) crece monótonamente con Ck
        for (int i = 0; i < 60; ++i)
        {
            ck = 0.5 * (lo + hi);
            fRef = varFrequency (dZero);
            (beatAt (dMin) < topHz ? lo : hi) = ck;
        }
    }

    static double distanceForX (double x) { return dMin + (1.0 - x) * (dMax - dMin); }
    static double xForDistance (double d) { return 1.0 - (d - dMin) / (dMax - dMin); }

    double varFrequency (double d) const { return f0 * std::sqrt (C0 / (C0 + ck / (std::max (d, 0.0) + d0))); }
    double beatAt (double d) const       { return fRef - varFrequency (d); }

    // x del pad donde el batido estático vale hz; -1 si no cae en el tramo [dMin, min(dZero, dMax)]
    double xForBeat (double hz) const
    {
        double lo = dMin, hi = std::min (dZero, dMax); // en este tramo el batido decrece con d
        if (hz > beatAt (lo) || hz < beatAt (hi))
            return -1.0;

        for (int i = 0; i < 40; ++i)
        {
            const auto mid = 0.5 * (lo + hi);
            (beatAt (mid) > hz ? lo : hi) = mid;
        }
        return xForDistance (0.5 * (lo + hi));
    }

    double d0 = 10.0, dZero = 60.0, ck = 0.0, fRef = f0, cachedTop = -1.0;
};

// Theremin monofónico con dos modelos:
//  Physical: X = distancia de la mano a la antena (ThereminField). Los dos osciladores de RF se acoplan según la
//    ecuación de Adler, dφ/dt = 2π·Δf - K·sin φ, con K = 2π·Lock: si |Δf| < Lock se enganchan (DC -> silencio tras
//    el pasa-altos); justo por encima el tono cae a √(Δf² - Lock²) y la onda se vuelve pulsante (gruñido grave).
//    Salida del mezclador ≈ cos φ. Deriva: f_ref ± 20 ppm rms a Drift 100 %. Vibrato del LFO: ±1,5 cm de mano.
//    Volumen: respuesta saturante con 20 ms de inercia (fenomenológico, no es un modelo de circuito).
//  Ideal: f = C3 · 2^(range · x) con glide en log2(f); volumen = y². Afinación lineal, sin acoplamiento.
// En ambos, forma = tanh(d·s)/tanh(d), d = 0,1..6 según Tone (d -> 0: sin cambios; d alto: armónicos impares).
struct Theremin
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { rng.setSeed (seed); }

    static constexpr float lowHz = 130.8128f; // C3 (MIDI 48)
    static constexpr int lowNote = 48;

    void prepare (double sr)
    {
        sampleRate = sr;
        rng.setSeedRandomly();
        reset();
    }

    void reset()
    {
        phase = 0.0;
        phi = 0.0;
        lfoPhase = 0.0f;
        amp = 0.0f;
        walk = 0.0f;
        logFreq = -1.0f; // < 0: sin inicializar; el primer bloque salta al tono sin barrido
        distance = -1.0;
        hpState = hpPrev = 0.0f;
    }

    // Además del pad, notas MIDI (controlador o teclado en pantalla): monofónico, última nota; las notas fuera del rango
    // (C3 + Range octavas) se llevan por octavas a él; volumen = velocidad. La mano sigue con su inercia (Glide), y el modelo
    // Physical toca la nota en la distancia que da ese batido (con el acoplamiento). El sonido no cambia: solo la fuente
    // del tono y del volumen. Tocar el pad le devuelve el control.
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        const auto physical = (int) p.thereminModel->load() == 0;
        const auto range = (double) p.thereminRange->load();
        if (physical)
            field.configure (lowHz * std::exp2 (range), (double) p.thereminField->load(), (double) p.thereminZero->load());
        int start = 0;
        auto it = midi.cbegin();
        while (start < out.getNumSamples())
        {
            for (; it != midi.cend() && (*it).samplePosition <= start; ++it)
                handleMidi ((*it).getMessage());
            const auto end = it != midi.cend() ? juce::jlimit (start + 1, out.getNumSamples(), (*it).samplePosition) : out.getNumSamples();
            juce::AudioBuffer<float> segment (out.getArrayOfWritePointers(), out.getNumChannels(), start, end - start);
            // Fuente: el pad (parámetros) o las notas MIDI, la última que se haya usado
            auto x = p.thereminPitch->load(), vol = p.thereminVolume->load();
            if (x != lastPadPitch || vol != lastPadVolume) { midiOwns = false; lastPadPitch = x; lastPadVolume = vol; }
            if (midiOwns)
            {
                vol = numHeld > 0 ? midiVelocity : 0.0f;
                if (numHeld > 0)
                {
                    auto n = held[(size_t) numHeld - 1];
                    const auto top = lowNote + (int) std::floor (12.0 * range);
                    while (n > top) n -= 12;
                    while (n < lowNote) n += 12;
                    const auto hz = 440.0 * std::exp2 ((n - 69) / 12.0);
                    const auto lock = (double) p.thereminLock->load();
                    x = physical ? (float) juce::jlimit (0.0, 1.0, field.xForBeat (std::sqrt (hz * hz + lock * lock)))
                                 : (float) juce::jlimit (0.0, 1.0, (n - lowNote) / (12.0 * range));
                }
            }
            if (physical) renderPhysical (segment, p, x, vol);
            else          renderIdeal (segment, p, x, vol);
            start = end;
        }
    }

private:
    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            int w = 0;
            for (int r = 0; r < numHeld; ++r)
                if (held[(size_t) r] != m.getNoteNumber()) held[(size_t) w++] = held[(size_t) r];
            numHeld = std::min (w, (int) held.size() - 1);
            held[(size_t) numHeld++] = m.getNoteNumber();
            midiVelocity = m.getFloatVelocity();
            midiOwns = true;
        }
        else if (m.isNoteOff())
        {
            int w = 0;
            for (int r = 0; r < numHeld; ++r)
                if (held[(size_t) r] != m.getNoteNumber()) held[(size_t) w++] = held[(size_t) r];
            numHeld = w;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = 0;
        }
    }

    struct Common
    {
        float glideCoef, drive, driveNorm, lfoInc, lfoDepth, drift, walkCoef, walkNorm;
        bool lfoOn;
        int lfoTarget; // el destino Filter no aplica: el theremin no pasa por el ladder
    };

    Common common (const Params& p) const
    {
        const auto sr = (float) sampleRate;
        Common c;
        c.glideCoef = 1.0f - std::exp (-1.0f / (0.001f * p.thereminGlide->load() * sr));
        c.drive     = 0.1f + 5.9f * p.thereminTone->load();
        c.driveNorm = 1.0f / std::tanh (c.drive);
        c.lfoOn     = p.lfoOn->load() > 0.5f;
        c.lfoTarget = (int) p.lfoTarget->load();
        c.lfoDepth  = p.lfoDepth->load();
        c.lfoInc    = p.lfoRate->load() / sr;
        c.drift     = p.drift->load();
        c.walkCoef  = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 0.5f / sr);
        c.walkNorm  = std::sqrt (3.0f) / std::sqrt (c.walkCoef / 2.0f);
        return c;
    }

    float nextLfo (const Common& c)
    {
        const auto lfo = std::sin (juce::MathConstants<float>::twoPi * lfoPhase);
        lfoPhase += c.lfoInc;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;
        return lfo;
    }

    float nextWalk (const Common& c)
    {
        walk += c.walkCoef * (2.0f * rng.nextFloat() - 1.0f - walk);
        return walk * c.walkNorm; // σ ≈ 1
    }

    void renderPhysical (juce::AudioBuffer<float>& out, const Params& p, float pitchX, float volumeY)
    {
        constexpr auto twoPi = juce::MathConstants<double>::twoPi;
        const auto c  = common (p);
        const auto sr = (float) sampleRate;
        const auto dt = 1.0 / sampleRate;

        field.configure (lowHz * std::exp2 ((double) p.thereminRange->load()),
                         (double) p.thereminField->load(), (double) p.thereminZero->load());

        const auto targetDist = ThereminField::distanceForX ((double) pitchX);
        const auto vol        = volumeY;
        const auto targetAmp  = 0.3f * (1.0f - std::exp (-3.0f * vol)) / (1.0f - std::exp (-3.0f));
        const auto ampCoef    = 1.0f - std::exp (-1.0f / (0.020f * sr));
        const auto K          = twoPi * (double) p.thereminLock->load();
        const auto hpCoef     = 1.0f - juce::MathConstants<float>::twoPi * 20.0f / sr; // acoplamiento de audio: pasa-altos 20 Hz

        if (distance < 0.0)
            distance = targetDist;

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            distance += c.glideCoef * (targetDist - distance); // inercia de la mano
            amp      += ampCoef * (targetAmp - amp);

            auto d = distance;
            auto a = amp;

            if (c.lfoOn)
            {
                const auto lfo = nextLfo (c);
                if (c.lfoTarget == 0)      d -= 1.5 * c.lfoDepth * lfo;
                else if (c.lfoTarget == 1) a *= 1.0f - c.lfoDepth * (0.5f - 0.5f * lfo);
            }

            auto fRef = field.fRef;
            if (c.drift > 0.0f)
                fRef *= 1.0 + 20.0e-6 * c.drift * nextWalk (c);

            // Adler integrado con punto medio (RK2)
            const auto w  = twoPi * (fRef - field.varFrequency (d));
            const auto k1 = w - K * std::sin (phi);
            const auto k2 = w - K * std::sin (phi + 0.5 * dt * k1);
            phi = std::remainder (phi + dt * k2, twoPi);

            const auto mixer = std::tanh (c.drive * (float) std::cos (phi)) * c.driveNorm;
            hpState = mixer - hpPrev + hpCoef * hpState;
            hpPrev  = mixer;

            const auto s = hpState * a;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, s);
        }
    }

    void renderIdeal (juce::AudioBuffer<float>& out, const Params& p, float pitchX, float volumeY)
    {
        const auto c  = common (p);
        const auto sr = (float) sampleRate;

        const auto target    = std::log2 (lowHz) + p.thereminRange->load() * pitchX;
        const auto volume    = volumeY;
        const auto targetAmp = 0.3f * volume * volume;
        const auto ampCoef   = 1.0f - std::exp (-1.0f / (0.015f * sr));

        if (logFreq < 0.0f)
            logFreq = target;

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            logFreq += c.glideCoef * (target - logFreq);
            amp     += ampCoef * (targetAmp - amp);

            auto lf = logFreq;
            auto a  = amp;

            if (c.lfoOn)
            {
                const auto lfo = nextLfo (c);
                if (c.lfoTarget == 0)      lf += 2.0f * c.lfoDepth * lfo / 12.0f;
                else if (c.lfoTarget == 1) a  *= 1.0f - c.lfoDepth * (0.5f - 0.5f * lfo);
            }

            if (c.drift > 0.0f)
                lf += c.drift * 4.0f * nextWalk (c) / 1200.0f;
            lf += p.pitchBend.load() / 12.0f; // pitch bend del controlador (solo en el modelo Ideal)

            const auto s = std::tanh (c.drive * (float) std::sin (juce::MathConstants<double>::twoPi * phase)) * c.driveNorm * a;
            phase += std::exp2 (lf) / sr;
            if (phase >= 1.0)
                phase -= 1.0;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, s);
        }
    }

    double sampleRate = 44100.0, phase = 0.0, phi = 0.0, distance = -1.0;
    float lfoPhase = 0.0f, amp = 0.0f, walk = 0.0f, logFreq = -1.0f, hpState = 0.0f, hpPrev = 0.0f;
    ThereminField field;
    juce::Random rng;
    // Notas MIDI: pila de notas pulsadas, velocidad, y si el control lo tienen las notas (hasta que se toque el pad)
    std::array<int, 16> held {};
    int numHeld = 0;
    float midiVelocity = 0.0f, lastPadPitch = -1.0f, lastPadVolume = -1.0f;
    bool midiOwns = false;
};

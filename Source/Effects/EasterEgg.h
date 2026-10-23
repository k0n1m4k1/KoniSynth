// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// Huevo de pascua del About: clic en el retrato -> un loop a 128 BPM que se suma al final de la salida, sin pasar por el
// motor ni por los efectos (no cambia nada del resto). Solo suena mientras el About está abierto: al cerrarlo, stop().
// Sin memoria dinámica: todo el estado es fijo. La UI lee position() para animar el retrato al ritmo.
// Fragmento del drop de The Time (Dirty Bit), Black Eyed Peas: 128 BPM, 4/4 (Hooktheory); el sintetizador alterna D#5 y E5
// en octavas (dato aportado por el usuario) sobre vii(#5) -> i de E menor armónica, D#aug -> Em (Hooktheory). El ritmo de
// los golpes es APROXIMADO (no hay transcripción libre). Tablas: notas MIDI por semicorchea, -1 = silencio, -2 = ligadura.
struct EasterEgg
{
    static constexpr double kBpm = 128.0;
    static constexpr int kSteps = 32; // 2 compases de semicorcheas

    void prepare (double sr) { sampleRate = sr; }

    void start() { wanted.store (true); }
    void stop()  { wanted.store (false); }
    bool isOn() const { return wanted.load(); }
    // Posición del loop en pulsos (negras), para la animación; -1 si no suena
    double position() const { return playing.load() ? beats.load() : -1.0; }

    void render (juce::AudioBuffer<float>& out)
    {
        const auto want = wanted.load();
        if (want && ! playing.load()) restart();
        if (! want) { playing.store (false); return; }
        const auto stepLen = sampleRate * 60.0 / kBpm / 4.0;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            if (sampleInStep <= 0.0)
            {
                triggerStep (step);
                step = (step + 1) % kSteps;
                sampleInStep += stepLen;
            }
            sampleInStep -= 1.0;
            const auto s = 0.5f * nextSample();
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, i, s);
            totalBeats += 1.0 / (sampleRate * 60.0 / kBpm);
        }
        beats.store (totalBeats);
    }

private:
    // Compás 1: D#5 sobre D#aug; compás 2: E5 sobre Em. El lead dobla a la octava inferior (D#4/E4)
    static constexpr int kLead[kSteps] = { 75, -2, -1, 75, -2, -1, 75, -2,   -1, -1, 75, -2,  75, -2, -1, -1,
                                           76, -2, -1, 76, -2, -1, 76, -2,   -1, -1, 76, -2,  76, -2, 75, 76 };
    static constexpr int kBass[kSteps] = { 39, -1, 39, 51, 39, -1, 39, 51,   39, -1, 39, 51,  39, -1, 51, 39,
                                           40, -1, 40, 52, 40, -1, 40, 52,   40, -1, 40, 52,  40, -1, 52, 40 };

    void restart()
    {
        step = 0; sampleInStep = 0.0; totalBeats = 0.0;
        lead = bass = {};
        kickEnv = hatEnv = 0.0f; kickPhase = 0.0;
        lp = 0.0f;
        playing.store (true);
    }

    struct Voice { double phase = 0.0, phase2 = 0.0, phase3 = 0.0, hz = 0.0; float env = 0.0f; bool gate = false; };

    void triggerStep (int s)
    {
        if (s % 4 == 0) { kickEnv = 1.0f; kickPhase = 0.0; } // bombo a negras
        if (s % 4 == 2) hatEnv = 1.0f;                        // charles a contratiempo
        const auto noteOn = [] (Voice& v, int note)
        {
            if (note == -2) return;             // ligadura
            if (note < 0) { v.gate = false; return; }
            v.hz = 440.0 * std::exp2 ((note - 69) / 12.0);
            v.gate = true;
            v.env = 1.0f;
        };
        noteOn (lead, kLead[s]);
        noteOn (bass, kBass[s]);
    }

    static double saw (double ph) { return 2.0 * ph - 1.0; }

    float nextSample()
    {
        const auto sr = sampleRate;
        // Lead: dos sierras desafinadas ±8 ct (tipo "Oddity"), envolvente de puerta con caída lenta
        float y = 0.0f;
        if (lead.env > 1.0e-4f)
        {
            lead.phase += lead.hz * std::exp2 (8.0 / 1200.0) / sr;  lead.phase -= std::floor (lead.phase);
            lead.phase2 += lead.hz * std::exp2 (-8.0 / 1200.0) / sr; lead.phase2 -= std::floor (lead.phase2);
            lead.phase3 += 0.5 * lead.hz / sr; lead.phase3 -= std::floor (lead.phase3); // octava inferior
            y += 0.13f * lead.env * (float) (saw (lead.phase) + saw (lead.phase2) + saw (lead.phase3));
            lead.env *= lead.gate ? 0.99997f : 0.9992f;
        }
        // Bajo: cuadrada con paso bajo de 1 polo a ~600 Hz, notas cortas (bombeo)
        if (bass.env > 1.0e-4f)
        {
            bass.phase += bass.hz / sr; bass.phase -= std::floor (bass.phase);
            const auto sq = bass.phase < 0.5 ? 1.0f : -1.0f;
            lp += (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 600.0 / sr)) * (sq - lp);
            y += 0.30f * bass.env * lp;
            bass.env *= bass.gate ? 0.99985f : 0.999f;
        }
        // Bombo: seno de 150 -> 45 Hz con caída de ~180 ms
        if (kickEnv > 1.0e-4f)
        {
            const auto f = 45.0 + 105.0 * kickEnv * kickEnv;
            kickPhase += f / sr; kickPhase -= std::floor (kickPhase);
            y += 0.55f * kickEnv * (float) std::sin (juce::MathConstants<double>::twoPi * kickPhase);
            kickEnv *= (float) std::exp (-1.0 / (0.06 * sr));
        }
        // Charles: ruido con paso alto (diferencia de primer orden), ~40 ms
        if (hatEnv > 1.0e-4f)
        {
            const auto n = random.nextFloat() * 2.0f - 1.0f;
            y += 0.12f * hatEnv * (n - hatPrev);
            hatPrev = n;
            hatEnv *= (float) std::exp (-1.0 / (0.012 * sr));
        }
        return y;
    }

    double sampleRate = 44100.0;
    std::atomic<bool> wanted { false }, playing { false };
    std::atomic<double> beats { 0.0 };
    int step = 0;
    double sampleInStep = 0.0, totalBeats = 0.0, kickPhase = 0.0;
    Voice lead, bass;
    float kickEnv = 0.0f, hatEnv = 0.0f, hatPrev = 0.0f, lp = 0.0f;
    juce::Random random;
};

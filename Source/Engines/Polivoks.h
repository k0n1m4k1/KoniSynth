// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Formanta Polivoks (1982, V. Kuzmin): duofónico, 2 VCO de 5 formas (32'..2'), el VCO2 modula en FM al VCO1, portamento solo en
// el VCO1, ruido, mezclador que satura el filtro, SVF de 2 polos LP/BP, 2 ADSR con modo de repetición AD, LFO de 4 formas.
// Fuentes (cada dato en ≥2): manual soviético (archive.org synthmanual-polivoks-owners-manual), Sound On Sound (Polivoks Pro),
// Wikipedia, vintagesynth, modularsynthesis.com (VCF de Kuzmin), North Coast Synthesis (SVF), monosounds (Duovox).
// Filtro: integradores = op-amps К140УД12 (µA776) sin condensador externo: su par diferencial y la compensación interna forman un
// integrador OTA-C, dv/dt = (I/Cc)·tanh(vd/2VT): en pequeña señal ω = I/(2VT·Cc) (corte LINEAL en Hz con la corriente, sin
// seguimiento de teclado), en gran señal limitado por slew rate. Normalizado a 2VT: y' = ω·tanh(x) (derivación del agente de
// investigación, no publicada). SVF: bp' = ω·tanh(x - lp - k·bp), lp' = ω·tanh(bp); resonancia sin limitador (autooscila con
// forma casi cuadrada). Entrada atenuada ≈1:48 (sierra de 10 Vpp): el mezclador lleva el filtro a la rodilla de la tanh.
// Rango del corte de fábrica: 100 Hz - 10 kHz. Deriva de los VCO: ±0,5 % en 4 h (≈ ±8,6 cents, especificación del manual).
// Estimado (sin dato del original): tiempos de las envolventes (rango del clon Erica: A 0-3 s, D/R 0-12 s), rango del LFO,
// anchos de los dos pulsos (25 % y 10 %), distorsión de cruce a corriente baja (observada por Kuzmin, sin cifra).
// Calculado a 4× de sobremuestreo (juce::dsp::Oversampling, IIR de media banda) por la no linealidad.
struct PolivoksEngine
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { rng.setSeed (seed); }

    void prepare (double sr)
    {
        sampleRate = sr;
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) kChunk);
        mix.setSize (1, kChunk);
        reset();
    }

    void reset()
    {
        numHeld = 0;
        phase1 = phase2 = 0.0;
        glideHz = 0.0;
        lp = bp = 0.0;
        fEnv = {};
        aEnv = {};
        lfoPhase = 0.0;
        lfoValue = 0.0;
        driftPhase1 = 0.17; driftPhase2 = 0.61;
        if (oversampling) oversampling->reset();
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        const auto numSamples = out.getNumSamples();
        auto midiIt = midi.cbegin();
        for (int start = 0; start < numSamples; start += kChunk)
        {
            const auto n = std::min (kChunk, numSamples - start);
            auto* m = mix.getWritePointer (0);
            for (int i = 0; i < n; ++i)
            {
                for (; midiIt != midi.cend() && (*midiIt).samplePosition <= start + i; ++midiIt)
                    handleMidi ((*midiIt).getMessage());
                m[i] = voiceSample (p, cutoffHz[(size_t) i], amp[(size_t) i]);
            }
            // Filtro a 4×: el corte de cada muestra base se interpola linealmente entre muestras sobremuestreadas
            juce::dsp::AudioBlock<float> block (mix.getArrayOfWritePointers(), 1, (size_t) n);
            auto up = oversampling->processSamplesUp (block);
            auto* u = up.getChannelPointer (0);
            const auto bpMode = (int) p.pvFilterMode->load() == 1;
            const auto k = 2.0 - 2.05 * (double) p.pvReso->load(); // 2 (sin resonancia) .. -0,05 (autooscilación)
            const auto osRate = sampleRate * 4.0;
            for (int i = 0; i < n * 4; ++i)
            {
                const auto base = i >> 2;
                const auto w = juce::MathConstants<double>::twoPi * cutoffHz[(size_t) base] / osRate;
                // Distorsión de cruce de la etapa de salida a corriente baja: zona muerta que crece al bajar el corte (estimada)
                const auto dead = 0.015 * std::min (1.0, 200.0 / cutoffHz[(size_t) base]);
                const auto x = (double) u[i];
                const auto hp = x - lp - k * bp;
                // Raíles de salida de los op-amps: con la entrada saturada, cada integrador sube a ±ω y la autooscilación vale
                // inversamente proporcional al raíl A; A = 1,03 la deja en el corte (calibrado midiendo: el clon autooscila siguiendo el corte)
                bp = juce::jlimit (-kRail, kRail, bp + w * crossover (std::tanh (hp), dead));
                lp = juce::jlimit (-kRail, kRail, lp + w * crossover (std::tanh (bp), dead));
                u[i] = (float) (bpMode ? bp : lp);
            }
            oversampling->processSamplesDown (block);
            for (int i = 0; i < n; ++i)
            {
                const auto y = 0.22f * m[i] * amp[(size_t) i];
                for (int c = 0; c < out.getNumChannels(); ++c)
                    out.addSample (c, start + i, y);
            }
        }
    }

    int latency() const { return oversampling ? (int) std::lround (oversampling->getLatencyInSamples()) : 0; }

private:
    static constexpr int kChunk = 256;
    static constexpr double kRail = 1.03;

    struct Env
    {
        int stage = 0;       // 0 parado, 1 A, 2 D, 3 S, 4 R
        double level = 0.0;
    };

    static double crossover (double y, double dead)
    {
        const auto a = std::abs (y) - dead;
        return a > 0.0 ? std::copysign (a, y) : 0.0;
    }

    // ADSR (exponencial RC) con modo de repetición AD mientras la tecla está pulsada ("многократный")
    void envStep (Env& e, double a, double d, double s, double r, bool loop, bool gate)
    {
        const auto coef = [this] (double t) { return 1.0 - std::exp (-1.0 / (std::max (0.0005, t) * sampleRate)); };
        switch (e.stage)
        {
            case 1: e.level += coef (a) * (1.25 - e.level); if (e.level >= 1.0) { e.level = 1.0; e.stage = 2; } break;
            case 2:
            {
                const auto target = loop ? 0.0 : s;
                e.level += coef (d) * (target - e.level);
                if (loop && e.level < 0.01) e.stage = 1;      // repetición AD
                else if (! loop && std::abs (e.level - s) < 1.0e-4) e.stage = 3;
                break;
            }
            case 3: e.level = s; break;
            case 4: e.level += coef (r) * (0.0 - e.level); if (e.level < 1.0e-5) { e.level = 0.0; e.stage = 0; } break;
            default: break;
        }
        if (! gate && (e.stage == 1 || e.stage == 2 || e.stage == 3)) e.stage = 4;
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            if (numHeld < (int) held.size())
                held[(size_t) numHeld++] = m.getNoteNumber();
            fEnv.stage = aEnv.stage = 1; // disparo múltiple: cada tecla nueva reinicia las envolventes (supuesto)
        }
        else if (m.isNoteOff())
        {
            int j = 0;
            for (int k = 0; k < numHeld; ++k)
                if (held[(size_t) k] != m.getNoteNumber()) held[(size_t) j++] = held[(size_t) k];
            numHeld = j;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = 0;
            if (m.isAllSoundOff()) reset();
        }
    }

    // Formas del VCO: triángulo, sierra, cuadrada, pulso 25 %, pulso 10 % (anchos estimados)
    float vco (int wave, double ph, double dt) const
    {
        switch (wave)
        {
            case 0:  return oscillator (triangle, ph, dt);
            case 1:  return oscillator (saw, ph, dt);
            case 2:  return oscillator (square, ph, dt, 0.5f) / 0.7f;
            case 3:  return oscillator (square, ph, dt, 0.25f) / 0.7f;
            default: return oscillator (square, ph, dt, 0.10f) / 0.7f;
        }
    }

    float voiceSample (const Params& p, double& cutoffOut, float& ampOut)
    {
        const auto gate = numHeld > 0;
        // Duo: la nota más aguda al VCO1 y la más grave al VCO2 (SOS); Mono: las dos la última
        int hi = -1, lo = 128, last = numHeld > 0 ? held[(size_t) numHeld - 1] : lastNote;
        for (int k = 0; k < numHeld; ++k) { hi = std::max (hi, held[(size_t) k]); lo = std::min (lo, held[(size_t) k]); }
        const auto duo = (int) p.pvVoice->load() == 1;
        const auto note1 = numHeld == 0 ? lastNote1 : (duo ? hi : last);
        const auto note2 = numHeld == 0 ? lastNote2 : (duo ? lo : last);
        lastNote1 = note1; lastNote2 = note2;
        if (numHeld > 0) lastNote = last;

        // LFO ("Модулятор"): triángulo, cuadrada, ruido (pulsos caóticos), S&H
        const auto lfoHz = (double) p.pvLfoRate->load();
        lfoPhase += lfoHz / sampleRate;
        const auto wrapped = lfoPhase >= 1.0;
        if (wrapped) lfoPhase -= 1.0;
        switch ((int) p.pvLfoWave->load())
        {
            case 0:  lfoValue = lfoPhase < 0.5 ? 4.0 * lfoPhase - 1.0 : 3.0 - 4.0 * lfoPhase; break;
            case 1:  lfoValue = lfoPhase < 0.5 ? 1.0 : -1.0; break;
            case 2:  lfoValue = rng.nextFloat() < 0.002f * (float) lfoHz ? rng.nextFloat() * 2.0 - 1.0 : lfoValue * 0.999; break;
            default: if (wrapped) lfoValue = rng.nextFloat() * 2.0 - 1.0; break;
        }

        // Deriva de los VCO: dos LFO lentos e independientes, hasta ±0,5 % con Drift al 100 % (especificación de fábrica)
        driftPhase1 += 0.013 / sampleRate; driftPhase2 += 0.009 / sampleRate;
        const auto drift = (double) p.pvDrift->load() * 0.005;
        const auto d1 = 1.0 + drift * std::sin (juce::MathConstants<double>::twoPi * driftPhase1);
        const auto d2 = 1.0 + drift * std::sin (juce::MathConstants<double>::twoPi * driftPhase2);

        static constexpr double footage[5] = { 0.25, 0.5, 1.0, 2.0, 4.0 }; // 32' 16' 8' 4' 2' (8' = nota escrita)
        const auto bend = std::exp2 (p.pitchBend.load() / 12.0) * std::exp2 (p.fineTune->load() / 1200.0);
        const auto f1Target = 440.0 * std::exp2 ((note1 - 69) / 12.0) * KoniTuning::ratio (note1) * footage[juce::jlimit (0, 4, (int) p.pvRange1->load())] * bend;
        const auto f2 = 440.0 * std::exp2 ((note2 - 69) / 12.0 + p.pvDetune->load() / 12.0) * KoniTuning::ratio (note2) * footage[juce::jlimit (0, 4, (int) p.pvRange2->load())] * bend
                        * d2 * std::exp2 (0.5 * p.pvLfoVco2->load() * lfoValue);
        // Portamento RC exponencial, solo en el VCO1
        const auto glide = (double) p.pvGlide->load();
        if (glideHz <= 0.0 || glide <= 0.0005) glideHz = f1Target;
        else glideHz *= std::pow (f1Target / glideHz, 1.0 - std::exp (-1.0 / (glide * sampleRate)));
        // FM del VCO2 sobre el VCO1 (exponencial, hasta ±2 octavas)
        const auto s2 = vco ((int) p.pvWave2->load(), phase2, f2 / sampleRate);
        const auto f1 = glideHz * d1 * std::exp2 (2.0 * p.pvFm->load() * s2 + 0.5 * p.pvLfoVco1->load() * lfoValue);
        const auto s1 = vco ((int) p.pvWave1->load(), phase1, f1 / sampleRate);
        phase1 += f1 / sampleRate; phase1 -= std::floor (phase1);
        phase2 += f2 / sampleRate; phase2 -= std::floor (phase2);

        // Envolventes
        const auto loopF = p.pvFLoop->load() > 0.5f, loopA = p.pvALoop->load() > 0.5f;
        envStep (fEnv, p.pvFAttack->load(), p.pvFDecay->load(), p.pvFSustain->load(), p.pvFRelease->load(), loopF, gate);
        envStep (aEnv, p.pvAAttack->load(), p.pvADecay->load(), p.pvASustain->load(), p.pvARelease->load(), loopA, gate);

        // Corte lineal en Hz: mando + envolvente + LFO + rueda de modulación (el pedal del original), 100 Hz - 10 kHz (extensible)
        cutoffOut = juce::jlimit (20.0, 18000.0, (double) p.pvCutoff->load() + p.pvEnvAmount->load() * fEnv.level
                                                 + 3000.0 * p.pvLfoVcf->load() * lfoValue + 5000.0 * p.modWheel.load());
        ampOut = (float) (aEnv.level * (1.0 + 0.5 * p.pvLfoVca->load() * lfoValue));

        // Mezclador sin limitación: los niveles altos saturan el filtro (entrada ≈1:48 de 10 Vpp -> ±2 en unidades de 2VT)
        const auto noise = rng.nextFloat() * 2.0f - 1.0f;
        return 2.0f * (p.pvMix1->load() * s1 + p.pvMix2->load() * s2 + p.pvNoise->load() * noise);
    }

    double sampleRate = 44100.0, phase1 = 0.0, phase2 = 0.0, glideHz = 0.0, lp = 0.0, bp = 0.0;
    double lfoPhase = 0.0, lfoValue = 0.0, driftPhase1 = 0.17, driftPhase2 = 0.61;
    std::array<int, 16> held {};
    int numHeld = 0, lastNote = 60, lastNote1 = 60, lastNote2 = 60;
    Env fEnv, aEnv;
    std::array<double, kChunk> cutoffHz {};
    std::array<float, kChunk> amp {};
    juce::AudioBuffer<float> mix;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::Random rng;
};

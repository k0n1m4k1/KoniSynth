// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Dubreq Stylophone (1968, versión de módulos blancos): 20 teclas, monofónico, oscilador de relajación con PUT.
// Fuentes: Simon Inns, waitingforfriday.com/?p=334 y github.com/simoninns/stylophone-1968/wiki (Schematic,
// Relaxation-Oscillator). Verificado en esas fuentes: rangos Standard A2–E4, Bass A1–E3, Treble A3–E5 (modelos distintos,
// sin selector de octava); η del PUT 0,48–0,62 según el trimmer; vibrato por red RC de desplazamiento de fase, medido ≈7 Hz;
// salida de línea con paso bajo pasivo de 154 Hz; cadena de afinación en serie con pasos ≈1,060 (ideal 1,0595).
// Estimado (sin dato publicado): profundidad del vibrato, ataque/release, respuesta del altavoz de 75 Ω y prioridad de nota.
// Réplica: scratchpad/stylo_check.py (media 0, salto 2; peor alias a E5 / 48 kHz: -39,9 dB con PolyBLEP, -31,2 sin él).
//
// Gen X-2 (Dubreq, 2025), EXPERIMENTAL. Del manual oficial (stylophone.com, "STYLOPHONE-GENX-2-A3-Instructions.pdf"):
// teclado de dos octavas tipo piano; OCTAVE -1/0/+1; botón X = PWM movida por el LFO; -1/-2 = suboctavas; envolvente ATTACK /
// DECAY sobre el filtro y, con PITCH > 0, también sobre la afinación ("sin attack ni decay = sostenido con el filtro
// abierto"); LFO cuadrado o triangular (RATE, DEPTH) sobre afinación y corte a la vez; paso bajo resonante (CUTOFF, RES);
// delay analógico (TIME, REPEATS, LEVEL); tira de expresión FILT/OSC (bipolar desde el centro) o LFO (solo la mitad derecha).
// Estimado (sin cifras publicadas): rangos de todos los mandos, oscilador de pulso, suboctavas cuadradas por división (0,8
// del nivel), filtro SVF de 2 polos con saturación a la entrada, delay tipo BBD (paso bajo de 3,5 kHz en el lazo), teclas
// C3-C5 (25), tira = rueda de pitch bend.
struct Stylophone
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { random.setSeed (seed); }

    void prepare (double sr)
    {
        sampleRate = sr;
        genDelay.assign ((size_t) (0.5 * sr) + 4, 0.0f);
        genDelayLpCoef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 3500.0 / sr);
        genAmpAttack = (float) std::exp (-1.0 / (0.002 * sr));
        const auto onePole = [sr] (double hz) { return 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * hz / sr); };
        lp154Coef  = onePole (154.0);
        hpSpkCoef  = onePole (250.0);   // altavoz pequeño: 2 polos de paso alto a 250 Hz (estimado)
        lpSpkCoef  = onePole (5000.0);  // y 1 de paso bajo a 5 kHz (estimado)
        releaseCoef = (float) std::exp (-1.0 / (0.003 * sr)); // release τ = 3 ms (estimado: el stylus abre el circuito)
        bendCoef  = 1.0f - (float) std::exp (-1.0 / (0.005 * sr));
        reset();
    }

    void reset()
    {
        numHeld = 0;
        note = -1;
        phase = vibPhase = 0.0;
        amp = 0.0f;
        click = 0.0f;
        lp154 = hp1 = hp1x = hp2 = hp2x = lpSpk = 0.0f;
        genEnv = 0.0; genAttacking = false; genGate = false; genLfoPhase = 0.0;
        genSub1 = genSub2 = 1; genIc1 = genIc2 = 0.0;
        std::fill (genDelay.begin(), genDelay.end(), 0.0f);
        genDelayWrite = 0; genDelayLp = 0.0f; genDelayTime = -1.0;
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        const auto genx = (int) p.styloModel->load() == 1;
        const auto reg = juce::jlimit (0, 2, (int) p.styloRegister->load());
        const auto octave = genx ? juce::jlimit (-1, 1, (int) p.genxOctave->load()) : 0;
        // 1968: A2 (Standard), A1 (Bass), A3 (Treble), 20 teclas. Gen X-2: C3-C5, 25 teclas (estimado), más OCTAVE
        lowNote = genx ? 48 + 12 * octave : 45 + 12 * (reg == 2) - 12 * (reg == 1);
        keyCount = genx ? 25 : 20;
        fullRange = (int) p.styloRange->load() == 1;
        // Ataque: D1 + condensador, más rápido en el Treble (C6 = 47 nF frente a 100 nF / 200 nF). τ base 5 ms (estimado)
        const auto attackTau = 0.005 * (reg == 2 ? 0.47 : reg == 1 ? 2.0 : 1.0);
        const auto attackCoef = genx ? genAmpAttack : (float) std::exp (-1.0 / (attackTau * sampleRate));
        GenX g {};
        if (genx)
        {
            g.pwm = p.genxPwm->load() > 0.5f; g.sub1 = p.genxSub1->load() > 0.5f; g.sub2 = p.genxSub2->load() > 0.5f;
            g.attackStep = 1.0 / (std::max (0.001, (double) p.genxAttack->load()) * sampleRate);
            const auto decay = (double) p.genxDecay->load();
            g.decayCoef = decay < 0.0005 ? 1.0 : std::exp (std::log (0.001) / (decay * sampleRate)); // -60 dB en Decay
            g.pitch = p.genxPitch->load(); g.lfoSquare = (int) p.genxLfoWave->load() == 1;
            g.lfoStep = p.genxLfoRate->load() / sampleRate; g.lfoDepth = p.genxLfoDepth->load();
            g.cutoff = p.genxCutoff->load(); g.res = p.genxRes->load();
            g.delayOn = p.genxDelay->load() > 0.5f; g.time = p.genxTime->load(); g.repeats = p.genxRepeats->load(); g.level = p.genxLevel->load();
            g.strip = (int) p.genxStrip->load();
        }
        const auto vintage = (int) p.styloTuning->load() == 0;
        const auto vibOn = p.styloVibrato->load() > 0.5f;
        const auto vibCents = (double) p.styloVibDepth->load();
        const auto line = (int) p.styloOutput->load() == 1;
        const auto clickLevel = p.styloClick->load();
        const auto tune = (double) p.fineTune->load();
        const auto clickDecay = (float) std::exp (-1.0 / (0.002 * sampleRate)); // ráfaga de 2 ms al tocar una pista

        auto midiIt = midi.cbegin();
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                handleMidi ((*midiIt).getMessage(), clickLevel);

            bendSmoothed += bendCoef * (p.pitchBend.load() - bendSmoothed);
            stripSmoothed += bendCoef * (p.stripTouch.load() - stripSmoothed);
            vibPhase += 7.0 / sampleRate; // 7 Hz medido (W4F)
            if (vibPhase >= 1.0) vibPhase -= 1.0;

            const auto gate = note >= 0;
            amp = gate ? 1.0f + attackCoef * (amp - 1.0f) : amp * releaseCoef;

            float y = 0.0f;
            if (genx)
            {
                y = lastNote >= 0 ? genxSample (g, tune) : 0.0f;
            }
            else if (amp > 1.0e-5f && lastNote >= 0)
            {
                // Tecla de la cadena (0..19). Rango completo: la nota se toca en su octava, con la desafinación de la tecla
                // equivalente del original (los tres modelos son la misma cadena desplazada por octavas)
                const auto key = foldToKeys (lastNote) - lowNote;
                auto cents = tune + (vintage ? keyCents (key) : 0.0) + 100.0 * bendSmoothed;
                if (vibOn)
                    cents += vibCents * std::sin (juce::MathConstants<double>::twoPi * vibPhase);
                const auto dt = 440.0 * std::exp2 ((lastNote - 69) / 12.0 + cents / 1200.0) * KoniTuning::ratio (lastNote) / sampleRate;

                // Tensión del condensador del PUT: carga exponencial hasta η·Vs y descarga instantánea (aleta de tiburón)
                auto s = 2.0 * ((1.0 - std::exp (-phase * kL)) / kEta - kMean) - polyBlep (phase, dt);
                y = amp * (float) s;
                phase += dt;
                if (phase >= 1.0) phase -= 1.0;
            }

            // Clic del stylus sobre la pista metálica (se oye en la grabación de W4F; forma y nivel estimados)
            y += click * (random.nextFloat() * 2.0f - 1.0f);
            click *= clickDecay;

            float o;
            if (line)
            {
                lp154 += lp154Coef * (y - lp154);
                o = 2.2f * lp154; // compensa la atenuación del paso bajo sobre fundamentales de 110–330 Hz
            }
            else
            {
                // Amplificador de 1 transistor con saturación suave + altavoz pequeño
                const auto d = std::tanh (1.4f * y);
                hp1 = (1.0f - hpSpkCoef) * (hp1 + d - hp1x);   hp1x = d;
                hp2 = (1.0f - hpSpkCoef) * (hp2 + hp1 - hp2x); hp2x = hp1;
                lpSpk += lpSpkCoef * (hp2 - lpSpk);
                o = lpSpk;
            }
            o *= 0.25f;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, o);
        }
    }

private:
    static constexpr double kEta = 0.55;                    // centro del rango del trimmer (0,48–0,62)
    static constexpr double kL = 0.79850769621777;          // ln(1/(1-η))
    static constexpr double kMean = 0.56582249510049;       // media de (1-e^(-φL))/η: la forma sale sin DC

    // Desafinación de la cadena de resistencias en serie: +33 ct acumulados de la tecla 0 a la 19, calculados con f ∝ 1/R
    // a partir de los valores del esquemático (+13 ct en la tecla 5, +26 en la 12). Ajuste c(k) = 2,91k - 0,0619k²,
    // referido a la tecla 12 (A3 en Standard): de -26 ct en la tecla 0 a +6,9 ct en la 19
    static double keyCents (int k) { return 2.91 * k - 0.0619 * k * k - 26.0136; }

    // Monofónico; el stylus cortocircuita la cadena hasta la toma tocada: con dos pistas suena la más aguda (inferido)
    void handleMidi (const juce::MidiMessage& m, float clickLevel)
    {
        // La pila guarda las notas tal como llegan y se doblan al usarlas: cambiar Range con una nota pulsada no la cuelga
        if (m.isNoteOn())
        {
            const auto k = m.getNoteNumber();
            if (numHeld < (int) held.size())
                held[(size_t) numHeld++] = k;
            if (fold (k) != note)
                click = clickLevel;
            updateNote();
        }
        else if (m.isNoteOff())
        {
            const auto k = m.getNoteNumber();
            for (int j = 0; j < numHeld; ++j)
                if (held[(size_t) j] == k)
                {
                    held[(size_t) j] = held[(size_t) --numHeld];
                    break;
                }
            updateNote();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = 0;
            updateNote();
        }
    }

    // Notas fuera de las teclas del modelo (20 o 25): se trasladan por octavas a su rango
    int foldToKeys (int n) const
    {
        while (n < lowNote) n += 12;
        while (n > lowNote + keyCount - 1) n -= 12;
        return n;
    }

    // Mandos del Gen X-2 para el bloque
    struct GenX
    {
        bool pwm = false, sub1 = false, sub2 = false, lfoSquare = false, delayOn = false;
        int strip = 1;
        double attackStep = 1.0, decayCoef = 1.0, pitch = 0.0, lfoStep = 0.0, lfoDepth = 0.0, cutoff = 12000.0, res = 0.0;
        double time = 200.0, repeats = 0.4, level = 0.4;
    };

    // Una muestra del Gen X-2: pulso + suboctavas -> SVF resonante (corte movido por envolvente, LFO y tira) -> VCA de puerta
    // -> delay BBD
    float genxSample (const GenX& g, double tune)
    {
        const auto gate = note >= 0;
        if (gate && ! genGate) genAttacking = true; // cada contacto del stylus redispara la envolvente desde su nivel actual
        genGate = gate;
        if (genAttacking)
        {
            genEnv += g.attackStep;
            if (genEnv >= 1.0) { genEnv = 1.0; genAttacking = false; }
        }
        else
        {
            genEnv = g.decayCoef >= 1.0 ? 1.0 : genEnv * g.decayCoef;
        }
        genLfoPhase += g.lfoStep;
        if (genLfoPhase >= 1.0) genLfoPhase -= 1.0;
        const auto lfo = g.lfoSquare ? (genLfoPhase < 0.5 ? 1.0 : -1.0) : 1.0 - 4.0 * std::abs (genLfoPhase - 0.5);
        const auto strip = juce::jlimit (-1.0, 1.0, (double) (bendSmoothed + stripSmoothed)); // la tira del pad o la rueda de pitch bend
        const auto stripFilter = g.strip == 0 ? strip : 0.0, stripOsc = g.strip == 1 ? strip : 0.0;
        const auto stripVib = g.strip == 2 ? std::max (0.0, strip) : 0.0;

        // Afinación: envolvente (PITCH), LFO (±1 semitono a fondo) y tira OSC (±2 semitonos); todo estimado
        const auto semis = lastNote + g.pitch * genEnv + (g.lfoDepth + stripVib) * lfo + 2.0 * stripOsc;
        const auto dt = std::min (0.45, 440.0 * std::exp2 ((semis - 69.0) / 12.0 + tune / 1200.0) * KoniTuning::ratio (lastNote) / sampleRate);

        // Pulso (PWM: 50 % ± 40 % con el LFO) y suboctavas por división, con PolyBLEP en cada flanco
        const auto w = g.pwm ? 0.5 + 0.4 * lfo : 0.5;
        auto x = (phase < w ? 1.0 : -1.0) + polyBlep (phase, dt) - polyBlep (std::fmod (phase - w + 1.0, 1.0), dt);
        const auto b = (double) polyBlep (phase, dt);
        const auto preWrap = phase >= 0.5;
        if (g.sub1) x += 0.8 * (genSub1 + (preWrap ? -genSub1 : genSub1) * b);
        if (g.sub2)
        {
            const auto flips = preWrap ? genSub1 == 1 : genSub1 == -1; // la -2 cambia cuando la -1 baja
            x += 0.8 * (genSub2 + (flips ? (preWrap ? -genSub2 : genSub2) * b : 0.0));
        }
        phase += dt;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            if (genSub1 == 1) genSub2 = -genSub2;
            genSub1 = -genSub1;
        }

        // Filtro: corte de 30 Hz (envolvente a 0) a CUTOFF (envolvente a 1); LFO ±1 octava a fondo; tira FILT ±2 octavas
        auto fc = 30.0 * std::pow (g.cutoff / 30.0, genEnv) * std::exp2 (g.lfoDepth * lfo + 2.0 * stripFilter);
        fc = juce::jlimit (20.0, 0.45 * sampleRate, fc);
        const auto gg = std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
        const auto k = 2.0 - 1.95 * g.res;
        const auto a1 = 1.0 / (1.0 + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
        const auto in = std::tanh (0.6 * x);
        const auto v3 = in - genIc2;
        const auto v1 = a1 * genIc1 + a2 * v3;
        const auto v2 = genIc2 + a2 * genIc1 + a3 * v3;
        genIc1 = 2.0 * v1 - genIc1;
        genIc2 = 2.0 * v2 - genIc2;
        auto y = (float) (v2 * amp * 0.6);

        // Delay BBD: lectura con interpolación lineal, paso bajo en el lazo y saturación de la realimentación
        if (genDelayTime < 0.0) genDelayTime = g.time;
        genDelayTime += 0.001 * (g.time - genDelayTime);
        const auto size = (int) genDelay.size();
        const auto readPos = genDelayWrite - genDelayTime * 0.001 * sampleRate;
        const auto rp = readPos < 0.0 ? readPos + size : readPos;
        const auto i0 = (int) rp, i1 = (i0 + 1) % size;
        const auto fr = (float) (rp - i0);
        const auto wet = genDelay[(size_t) i0] + fr * (genDelay[(size_t) i1] - genDelay[(size_t) i0]);
        genDelayLp += genDelayLpCoef * (wet - genDelayLp);
        genDelay[(size_t) genDelayWrite] = g.delayOn ? y + std::tanh ((float) g.repeats * genDelayLp) : 0.0f;
        genDelayWrite = (genDelayWrite + 1) % size;
        if (g.delayOn) y += (float) g.level * genDelayLp;
        return y;
    }

    // Original: las notas fuera de las 20 teclas se llevan por octavas a su rango. Rango completo: todas suenan donde se tocan
    int fold (int n) const { return fullRange ? n : foldToKeys (n); }

    void updateNote()
    {
        note = -1;
        for (int j = 0; j < numHeld; ++j)
            note = std::max (note, fold (held[(size_t) j]));
        if (note >= 0)
        {
            if (lastNote < 0 || amp < 1.0e-5f)
                phase = 0.0;
            lastNote = note; // al levantar el stylus el release usa la última altura
        }
    }

    double sampleRate = 44100.0, phase = 0.0, vibPhase = 0.0;
    float lp154Coef = 0.0f, hpSpkCoef = 0.0f, lpSpkCoef = 0.0f, releaseCoef = 0.0f, bendCoef = 1.0f, bendSmoothed = 0.0f;
    float amp = 0.0f, click = 0.0f, lp154 = 0.0f, hp1 = 0.0f, hp1x = 0.0f, hp2 = 0.0f, hp2x = 0.0f, lpSpk = 0.0f;
    std::array<int, 16> held {};
    int numHeld = 0, note = -1, lastNote = -1, lowNote = 45;
    bool fullRange = true;
    int keyCount = 20;
    juce::Random random;
    // Gen X-2
    double genEnv = 0.0, genLfoPhase = 0.0, genIc1 = 0.0, genIc2 = 0.0, genDelayTime = -1.0;
    bool genAttacking = false, genGate = false;
    int genSub1 = 1, genSub2 = 1, genDelayWrite = 0;
    std::vector<float> genDelay;
    float genDelayLp = 0.0f, genDelayLpCoef = 0.0f, genAmpAttack = 0.0f, stripSmoothed = 0.0f;
};

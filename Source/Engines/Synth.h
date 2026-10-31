// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// LFO por voz. Formas: seno, triángulo (−1 en fase 0, +1 en ½), sierra descendente, cuadrada, S&H (valor nuevo al
// empezar cada ciclo) y Random (de un valor aleatorio al siguiente con transición de coseno a lo largo del ciclo).
struct VoiceLfo
{
    void reset (juce::Random& rng)
    {
        phase = 0.0;
        prev = 2.0f * rng.nextFloat() - 1.0f;
        next = 2.0f * rng.nextFloat() - 1.0f;
    }

    float process (int shape, double inc, juce::Random& rng)
    {
        const auto p = (float) phase;
        float v;
        switch (shape)
        {
            case 1:  v = 1.0f - 4.0f * std::abs (p - 0.5f); break;
            case 2:  v = 1.0f - 2.0f * p; break;
            case 3:  v = p < 0.5f ? 1.0f : -1.0f; break;
            case 4:  v = next; break;
            case 5:  v = prev + (next - prev) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * p)); break;
            default: v = (float) std::sin (juce::MathConstants<double>::twoPi * phase); break;
        }
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            prev = next;
            next = 2.0f * rng.nextFloat() - 1.0f;
        }
        return v;
    }

    double phase = 0.0;
    float prev = 0.0f, next = 0.0f;
};

// Voz: VCO (con drift) -> VCF (ladder + envolvente AD) -> VCA (ADSR). LFO propio con key sync.
struct SynthVoice : juce::SynthesiserVoice
{
    // Slots: sample de la fuente Sample, de Osc 2 y de Osc 3; wavetable de la fuente Wavetable, de Osc 2 y de Osc 3; receta de Modal
    SynthVoice (const Params& p, const SampleSlot& s, const SampleSlot& s2, const SampleSlot& s3,
                const WavetableSlot& w, const WavetableSlot& w2, const WavetableSlot& w3, const ModalSlot& m)
        : params (p), sampleSlot (s), osc2SampleSlot (s2), osc3SampleSlot (s3), modalSlot (m), wavetableSlot (w), osc2WavetableSlot (w2),
          osc3WavetableSlot (w3) { rng.setSeedRandomly(); }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SynthSound*> (s) != nullptr; }

    void reseed (juce::int64 seed) { rng.setSeed (seed); } // anclaje del transporte (Dsp/RandomClock.h)

    void startNote (int note, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override
    {
        // MPE: bend y CC74 que ya tenía el canal de la nota antes del note-on
        memberBend = (float) (currentPitchWheelPosition - 8192) / 8192.0f;
        for (int ch = 1; ch <= 16; ++ch)
            if (isPlayingChannel (ch))
            {
                mpeTimbre = timbreSmoothed = params.channelTimbre[(size_t) ch - 1].load();
                break;
            }
        const auto sampleRate = getSampleRate();
        const auto drift = params.drift->load();
        // Modo Locked: la nota toma su semilla de la posición de su note-on (Dsp/RandomClock.h); Free: sigue la secuencia libre
        auto& clock = params.randomClock;
        if (clock.locked)
            rng.setSeed ((juce::int64) clock.noteKey (note));

        // Glide: parte de la última nota pulsada (cualquier voz); offset en semitonos que decae exponencialmente
        const auto newHz = juce::MidiMessage::getMidiNoteInHertz (note) * KoniTuning::ratio (note); // microafinación (Dsp/Tuning.h)
        const auto lastHz = params.lastNoteHz.load();
        const auto glideMs = params.glide->load();
        glideSemis = glideMs > 0.0f && lastHz > 0.0 ? (float) (12.0 * std::log2 (lastHz / newHz)) : 0.0f;
        params.lastNoteHz = newHz;

        // Legato (modo Legato y la tecla anterior aún pulsada al robar la voz): solo cambia el tono; envolventes,
        // fases y filtros siguen
        const auto legato = std::exchange (pendingLegato, false) && (int) params.voiceMode->load() == 2;

        // Posición estéreo de la voz (Voice Pan); en legato se conserva la de la nota anterior
        if (! legato)
        {
            const auto amount = params.voicePan->load();
            const auto mode = (int) params.voicePanMode->load();
            const auto pos = mode == 0 ? ((params.panAlternate++ & 1u) ? 1.0f : -1.0f)
                           : mode == 1 ? juce::jlimit (-1.0f, 1.0f, (float) (note - 60) / 24.0f)
                                       : 2.0f * rng.nextFloat() - 1.0f;
            panBase = amount * pos;
            const auto angle = (amount * pos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            voicePanL = juce::MathConstants<float>::sqrt2 * std::cos (angle);
            voicePanR = juce::MathConstants<float>::sqrt2 * std::sin (angle);
        }
        if (legato)
        {
            frequency = newHz;
            keyOctaves = (float) (note - 60) / 12.0f;
            return;
        }
        pitchEnvValue = 1.0f;

        // Redisparo en Mono/Legato (stopNote): tono, velocity y envolventes de la nota nueva sin reiniciar fases, filtro, deriva
        // ni unísono; las envolventes arrancan su ataque desde el nivel que tenían y el nivel de velocity se alcanza en 5 ms
        if (std::exchange (pendingRetrigger, false) && (int) params.voiceMode->load() != 0)
        {
            frequency = newHz;
            keyOctaves = (float) (note - 60) / 12.0f;
            levelTarget = 0.2f * velocity;
            noteVelocity = velocity;
            startSample (note);
            startString (juce::MidiMessage::getMidiNoteInHertz (note) * KoniTuning::ratio (note), true);
            updateEnvelopes();
            adsr.noteOn();
            filterEnv.noteOn();
            return;
        }

        // Unísono: el número de osciladores se fija al pulsar la nota. Con más de uno, fases aleatorias:
        // evita que todos arranquen en fase (pico de nivel y filtrado en peine al inicio) y abre el estéreo.
        // Free Running (Osc Phase): fases y relaciones de la nota anterior antes de reiniciarlas abajo
        // (copia explícita: lanes.phase y lanes.ratio son arrays de C y 'auto' se quedaría con un puntero a los valores que se reinician)
        std::array<double, konisimd::kLanes> prevPhases {}, prevRatios {};
        std::copy (std::begin (lanes.phase), std::end (lanes.phase), prevPhases.begin());
        std::copy (std::begin (lanes.ratio), std::end (lanes.ratio), prevRatios.begin());
        const auto prevOsc2 = osc2Phase, prevSub = subPhase, prevSync = syncPhase;
        numUnison = juce::jlimit (1, maxUnison, (int) params.unisonVoices->load());
        for (int u = 0; u < konisimd::kLanes; ++u) // carriles inactivos: ganancia 0, relación 1 (la ruta AVX2 los calcula igual)
        {
            lanes.ratio[u] = 1.0;
            lanes.gainL[u] = lanes.gainR[u] = 0.0f;
        }
        for (auto& ph : lanes.phase)
            ph = numUnison > 1 ? rng.nextDouble() : 0.0;

        // Drift: fase inicial aleatoria, desafinación estática ±3 cents y tolerancia de cutoff ±10 % (a 100 %)
        if (numUnison == 1)
            lanes.phase[0] = drift > 0.0f ? rng.nextDouble() : 0.0;
        staticCents = drift * 3.0f * (2.0f * rng.nextFloat() - 1.0f);
        cutoffSpread = 1.0f + drift * 0.1f * (2.0f * rng.nextFloat() - 1.0f);

        // Deriva física (personalidad propia del motor Synth, no un aparato concreto): semillas de los procesos 1/f de la voz y
        // de su Osc 2, desafinación fija de Osc 2 respecto a Osc 1 (±2 ct a Drift 100 %) y coeficiente térmico residual de la
        // voz: uniforme ±0,52 ct por octava desde C4 y por unidad de temperatura (σ 0,3; tras la compensación de +3300 ppm/K
        // del convertidor exponencial, AS3310: valor residual estimado, no medido)
        const auto voiceSeed = (juce::uint64) rng.nextInt64();
        voicePink.init (voiceSeed, 0.05, 10);
        // Fuente Random de la matriz, fija durante la nota: hash de la semilla de la voz (sin otra extracción del generador,
        // que desplazaría el resto de la secuencia de la nota)
        noteRandom = (float) KoniRandom::unit (KoniRandom::mix (voiceSeed ^ 0x52414E44ull));
        vcaFeedFactor = (float) KoniRandom::unit (KoniRandom::mix (voiceSeed ^ 0x564341ull)); // VCA Click: signo y cantidad de esta nota
        satL.reset ((int) params.voiceSatMode->load());
        satR.reset ((int) params.voiceSatMode->load());
        osc2Pink.init ((juce::uint64) rng.nextInt64(), 0.05, 10);
        osc2StaticCents = drift * 2.0f * (2.0f * rng.nextFloat() - 1.0f);
        tempco = 0.52f * (2.0f * rng.nextFloat() - 1.0f);
        if ((int) params.voiceTolerance->load() == 1) // Per Voice Card: fijas por voz física (las extracciones de arriba se mantienen)
        {
            const auto card = KoniRandom::key (clock.seed, 0xCA2Dull, (juce::uint64) cardIndex);
            const auto u = [card] (juce::uint64 k) { return (float) KoniRandom::unit (KoniRandom::key (card, k)); };
            staticCents = drift * 3.0f * u (1);
            cutoffSpread = 1.0f + drift * 0.1f * u (2);
            osc2StaticCents = drift * 2.0f * u (3);
            tempco = 0.52f * u (4);
            vcaFeedFactor = u (5);
        }
        keyOctaves = (float) (note - 60) / 12.0f;
        updateDrift (clock.eventOffset, driftCents, osc2DriftCents);
        driftStep = osc2DriftStep = 0.0f;
        driftCountdown = 0;

        lfo1.reset (rng);
        lfo2.reset (rng);
        frequency = newHz;
        keyOctaves = (float) (note - 60) / 12.0f;
        level = levelTarget = 0.2f * velocity; // 0.2 deja margen para 5 voces a velocity 1 sin clipping
        noteVelocity = velocity;
        osc2Phase = drift > 0.0f ? rng.nextDouble() : 0.0;
        subPhase = syncPhase = 0.0;
        // Osc Phase: Auto = lo calculado arriba (aleatoria con Drift, 0 sin él)
        switch ((int) params.oscPhase->load())
        {
            case 1: // Reset
                for (auto& ph : lanes.phase) ph = 0.0;
                osc2Phase = 0.0;
                break;
            case 2: // Free Running: cada oscilador avanza lo que habría girado desde el último render de la voz
            {
                const auto frac = [] (double x) { return x - std::floor (x); };
                const auto elapsed = (double) std::max<juce::int64> (0, clock.timeline + clock.eventOffset - lastRenderEnd);
                for (int u = 0; u < konisimd::kLanes; ++u)
                    lanes.phase[u] = frac (prevPhases[(size_t) u] + elapsed * lastDt * prevRatios[(size_t) u]);
                osc2Phase = frac (prevOsc2 + elapsed * lastDt2);
                subPhase = frac (prevSub + elapsed * lastDt * lastSubRatio);
                syncPhase = frac (prevSync + elapsed * lastDt);
                break;
            }
            case 3: // Random
                for (auto& ph : lanes.phase) ph = rng.nextDouble();
                osc2Phase = rng.nextDouble();
                break;
            default: break;
        }
        pink = {};

        startSample (note);
        startString (juce::MidiMessage::getMidiNoteInHertz (note) * KoniTuning::ratio (note));
        bendSmoothed = params.pitchBend.load();
        modSmoothed  = params.modWheel.load() * params.modWheelDepth->load();
        filter.reset();
        filterR.reset();
        osL = {};
        osR = {};
        for (auto& ph : lanes.modPhase)
            ph = 0.0; // modulador en fase con el principal: timbre de FM repetible nota a nota
        std::fill (std::begin (modLpLane), std::end (modLpLane), 0.0);
        modLpExt = 0.0;
        decimator.reset();
        foldDc = {};
        osc1Blep.reset();
        complexBlep.reset();
        monoBlep.reset();
        adsr.setSampleRate (sampleRate);
        filterEnv.setSampleRate (sampleRate);
        updateEnvelopes();
        adsr.noteOn();
        filterEnv.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        stringGate = false; // Bow y E-Bow: el arco o el electroimán se retiran y la cuerda sigue con su Decay
        if (allowTailOff)
        {
            adsr.noteOff();
            filterEnv.noteOff();
        }
        else if ((int) params.voiceMode->load() == 2 && isKeyDown())
        {
            // Robo en modo Legato con la tecla aún pulsada: llegará startNote de la nota nueva sin renderizar entre
            // medias. Las envolventes no se reinician; si no llega (panic), renderNextBlock las reinicia.
            pendingLegato = true;
            clearCurrentNote();
        }
        else if ((int) params.voiceMode->load() != 0 && adsr.isActive())
        {
            // Robo en Mono/Legato de la voz que aún suena (en su release: notas sin solapar, como al arrastrar por el
            // teclado). Antes se cortaba a 0 y se reiniciaban filtro y fases: un escalón (clic medido de +25 dB sobre los
            // saltos propios de la sierra). Como en un monofónico analógico, la nota nueva redispara las envolventes desde
            // su nivel actual con el filtro y los osciladores en marcha. Si no llega (panic), renderNextBlock las reinicia
            pendingRetrigger = true;
            clearCurrentNote();
        }
        else
        {
            adsr.reset();
            filterEnv.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int value) override { memberBend = (float) (value - 8192) / 8192.0f; } // bend del canal de la nota (MPE)
    void controllerMoved (int cc, int value) override { if (cc == 74) mpeTimbre = (float) value / 127.0f; } // timbre MPE (canal de la nota)
    void aftertouchChanged (int value) override      { polyPressure = (float) value / 127.0f; }
    void channelPressureChanged (int value) override { channelPressure = (float) value / 127.0f; }

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
        {
            // legato o redisparo pendiente que no llegó (panic, cambio de modo)
            if (std::exchange (pendingLegato, false) | std::exchange (pendingRetrigger, false))
            {
                adsr.reset();
                filterEnv.reset();
            }
            return;
        }

        updateEnvelopes(); // los potenciómetros se leen una vez por bloque
        publishSourcePeaks();

        const auto sampleRate = getSampleRate();
        const auto wave       = waveFromChoice ((int) params.wave->load()); // mezclas: opciones 4..9 -> códigos 6..11
        const auto drift      = params.drift->load();
        const auto lfoOn      = params.lfoOn->load() > 0.5f;
        const auto pitchEnvAmount = params.pitchEnvAmount->load();
        const auto pitchEnvCoef = std::exp (-1.0f / (0.001f * params.pitchEnvDecay->load() * (float) sampleRate));
        const auto glideMs = params.glide->load();
        const auto glideCoef = glideMs > 0.0f ? std::exp (-1.0f / (0.001f * glideMs * (float) sampleRate)) : 0.0f;
        bendTarget  = params.pitchBend.load();
        modTarget   = params.modWheel.load() * params.modWheelDepth->load();
        wheelTarget = params.modWheel.load();
        const auto mpe = params.mpeOn->load() > 0.5f;
        const auto memberBendSemis = mpe ? memberBend * params.mpeBendRange->load() : 0.0f;
        matrix.load (params);
        const auto bendRangeNow = (float) params.bendRange->load();
        const auto driveBase = params.filterDrive->load();
        controlCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        // VCA analógico: feedthrough = 0,05 · Click · (signo y 0,5..1 por voz) · nivel de control; fuga constante (no depende de
        // la velocidad) de 0,2 · 10^(dB/20). Acoplamiento de salida del feedthrough: paso alto de un polo a 10 Hz
        const auto vcaLeakDb = params.vcaLeak->load();
        const auto vcaLeakGain = vcaLeakDb > -99.5f ? 0.2f * juce::Decibels::decibelsToGain (vcaLeakDb) : 0.0f;
        const auto vcaFeed = 0.05f * params.vcaClick->load() * (vcaFeedFactor >= 0.0f ? 1.0f : -1.0f) * (0.5f + 0.5f * std::abs (vcaFeedFactor));
        const auto vcaAnalog = vcaLeakGain > 0.0f || vcaFeed != 0.0f || vcaHpfY != 0.0f;
        const auto vcaHpfCoef = 1.0f - juce::MathConstants<float>::twoPi * 10.0f / (float) sampleRate;
        // Saturación por voz (Dsp/VoiceSaturator.h): ganancia y compensación interpoladas a lo largo del bloque (sin escalones)
        const auto satDriveDb = params.voiceSatDrive->load();
        const auto satMode = (int) params.voiceSatMode->load();
        const auto satOn = satDriveDb >= 0.05f || satWet > 0.0f;
        const auto satPre = (int) params.voiceSatPos->load() == 1; // Pre-filter: antes del filtro
        const auto satWetTarget = juce::jlimit (0.0f, 1.0f, satDriveDb / 3.0f);
        const auto satGTarget = (double) juce::Decibels::decibelsToGain (satDriveDb);
        const auto satOutTarget = VoiceSat::makeup (satMode, satGTarget);
        if (satOn && satGain <= 0.0) { satGain = satGTarget; satOut = satOutTarget; }
        const auto satStepDiv = 1.0 / juce::jmax (1, num);
        const auto satGStep = (satGTarget - satGain) * satStepDiv, satOutStep = (satOutTarget - satOut) * satStepDiv;
        const auto satWetStep = (satWetTarget - satWet) * (float) satStepDiv;
        const auto lfoTarget  = (int) params.lfoTarget->load(); // 0 vibrato, 1 tremolo, 2 filter
        const auto lfoDepth   = params.lfoDepth->load();
        const auto lfoInc     = params.lfoHz ((int) params.lfoSync->load(), params.lfoRate->load()) / sampleRate;
        const auto lfoShape   = (int) params.lfoShape->load();
        const auto lfo2On     = params.lfo2On->load() > 0.5f;
        const auto lfo2Target = (int) params.lfo2Target->load(); // 0 vibrato, 1 tremolo, 2 filtro, 3 PWM, 4 tono de Osc 2
        const auto lfo2Shape  = (int) params.lfo2Shape->load();
        const auto lfo2Depth  = params.lfo2Depth->load();
        const auto lfo2Inc    = params.lfoHz ((int) params.lfo2Sync->load(), params.lfo2Rate->load()) / sampleRate;
        const auto fineSemis  = params.fineTune->load() / 100.0f;
        const auto atFilter   = params.atFilter->load(), atVibrato = params.atVibrato->load(), atAmp = params.atAmp->load();
        const auto usePressure = atFilter > 0.0f || atVibrato > 0.0f || atAmp > 0.0f;

        const auto filterOn   = params.filterOn->load() > 0.5f;
        const auto filterMode = (int) params.filterMode->load();
        const auto filterSpread = params.filterSpread->load();
        // Calidad del ladder (solo LP 24, LP 12, BP 12 y HP 12; los demás modelos no se sobremuestrean). No cambia mientras
        // suene alguna voz (KoniSynth.cpp la aplica en silencio)
        const auto ladderQuality = filterMode < VoiceFilters::svfLp ? params.ladderQuality.load (std::memory_order_relaxed) : 0;
        // k = 4 es el umbral lineal (Reso ≈ 91 %); hasta 4,4 la oscilación crece hasta que la acota tanh
        // (medido en réplica numérica: pico 0,29, afinación < 0,1 Hz de error a 440 Hz–5 kHz)
        const auto k          = 4.4f * params.resonance->load();
        const auto inputComp  = 1.0f + 0.5f * k;                             // compensa en parte la caída de graves con resonancia
        const auto driveGain  = juce::Decibels::decibelsToGain (26.0f * params.filterDrive->load()); // 0..26 dB
        const auto driveNorm  = 1.0f / std::sqrt (driveGain);                // -13 dB a drive máximo: no dispara el nivel
        // Clean Drive: mezcla lineal -> tanh según el Drive (0 % lineal, 25 % o más = la etapa de siempre); continuo en el Drive
        const auto cleanDrive = params.filterCleanDrive->load() > 0.5f;
        const auto cleanWet   = juce::jmin (1.0f, 4.0f * params.filterDrive->load());
        // Ruta AVX2 solo con 3+ osciladores de unísono: con 1–2, los 8 carriles fijos cuestan más que el escalar
        // AVX2: el pasa-bajos del modulador interno (estado por carril) solo está en la ruta escalar
        const auto simd = numUnison >= 3 && simdActive() && ! (params.complexOn->load() > 0.5f && params.modLp->load() < 19999.0f
                                                                && (int) params.complexModSource->load() == 0);

        // Velocity -> cantidad de envolvente de filtro: Vel 100 % escala la envolvente por la velocity (0..1)
        const auto velAmount  = params.velToFilter->load();
        const auto envOctaves = 5.0f * params.filterEnv->load() * (1.0f - velAmount + velAmount * noteVelocity);

        // Pestañas de fuentes: con Solo solo suena esa fuente; si no, cada una según su Mute (sin Mute ni Solo, ×1,0: bit a bit igual)
        const auto solo = params.soloSource.load (std::memory_order_relaxed);
        const auto unmuted = [solo] (int source, const std::atomic<float>* mute)
        {
            return solo >= 0 ? (solo == source ? 1.0f : 0.0f) : (mute->load() > 0.5f ? 0.0f : 1.0f);
        };
        // Factores de Mute/Solo de las fuentes con destino en la matriz: se aplican también tras la modulación (si no, la matriz
        // devolvería el nivel a una fuente silenciada)
        const auto osc1Gate = unmuted (Params::srcOsc1, params.osc1Mute);
        const auto osc2Gate = unmuted (Params::srcOsc2, params.osc2Mute);
        const auto osc3Gate = unmuted (Params::srcOsc3, params.osc3Mute);
        const auto noiseGate = unmuted (Params::srcNoise, params.noiseMute);
        mix.osc1Level  = params.osc1Level->load() * osc1Gate;
        mix.pulseWidth = params.pulseWidth->load();
        mix.osc1Tri.set (params.osc1TriSym->load(), params.osc1TriRound->load());
        mix.osc2Tri.set (params.osc2TriSym->load(), params.osc2TriRound->load());
        mix.osc3Tri.set (params.osc3TriSym->load(), params.osc3TriRound->load());
        const auto tri1 = mix.osc1Tri.active && wave == triangle; // el oscilador complejo no la usa
        mix.pwmDepth   = params.pwmDepth->load();
        mix.osc2Wave   = osc2WaveFromChoice ((int) params.osc2Wave->load());
        mix.osc2Ratio  = std::exp2 ((double) params.osc2Octave->load()
                                    + ((double) params.osc2Semi->load() + (double) params.osc2Fine->load() / 100.0) / 12.0);
        mix.osc2Level  = params.osc2Level->load() * osc2Gate;
        mix.osc2Sync   = params.osc2Sync->load() > 0.5f;
        mix.subLevel   = params.subLevel->load() * osc3Gate;
        mix.subRatio   = osc3Ratio();
        // Mismos códigos que Osc 2 (opciones 0..9 = waveFromChoice, bit a bit; 10 Wavetable, 11 Sample)
        mix.subWave    = osc2WaveFromChoice ((int) params.osc3Wave->load());
        mix.osc3Sync   = params.osc3Sync->load() > 0.5f;
        mix.noiseLevel = params.noiseLevel->load() * noiseGate;
        mix.noiseColor = (int) params.noiseColor->load();
        const auto sampleLevel = params.sampleLevel->load() * unmuted (Params::srcSample, params.sampleMute);
        const auto stringLevel = params.stringLevel->load() * unmuted (Params::srcString, params.stringMute);
        const auto wtLevel = params.wtLevel->load() * unmuted (Params::srcWavetable, params.wtMute);
        const auto wtPositionBase = params.wtPosition->load();
        const auto* blockWavetable = wtLevel > 0.0f ? wavetableSlot.current.load (std::memory_order_acquire) : nullptr;
        // Osc 2 en modo Wavetable: su propia tabla, con la fase de Osc 2 y su posición (sin tabla: Osc 2 calla)
        const auto osc2WtPositionBase = params.osc2WtPosition->load();
        mix.osc2Table = mix.osc2Wave == wavetableWave ? osc2WavetableSlot.current.load (std::memory_order_acquire) : nullptr;
        // Osc 2 en modo Sample: su propio fichero (sin fichero: Osc 2 calla)
        mix.osc2Sample = mix.osc2Wave == sampleWave ? osc2SampleSlot.current.load (std::memory_order_acquire) : nullptr;
        // Osc 3, igual: su tabla y su posición (la matriz la mueve por muestra, Osc 3 WT Position) y su fichero; sin ellos, Osc 3 calla
        mix.osc3Table = mix.subWave == wavetableWave ? osc3WavetableSlot.current.load (std::memory_order_acquire) : nullptr;
        mix.osc3WtPos = juce::jlimit (0.0f, 1.0f, params.osc3WtPosition->load());
        mix.osc3Sample = mix.subWave == sampleWave ? osc3SampleSlot.current.load (std::memory_order_acquire) : nullptr;
        updateStringParams();
        sample.loop = params.sampleLoop->load() > 0.5f;
        osc2Sample.loop = params.osc2SampleLoop->load() > 0.5f;
        osc3Sample.loop = params.osc3SampleLoop->load() > 0.5f;
        const auto* blockSample = sampleSlot.current.load (std::memory_order_acquire);
        const auto baseCutoff = params.cutoff->load() * cutoffSpread
                              * std::exp2 (params.filterKey->load() * keyOctaves);
        const auto maxCutoff  = 0.45f * (float) sampleRate;

        complex.on        = params.complexOn->load() > 0.5f;
        complex.wave      = wave;
        complex.modWave   = waveFromChoice ((int) params.modWave->load());
        complex.ratio     = params.modRatio->load();
        complex.fmIndex   = params.fmIndex->load();
        complex.am        = params.amDepth->load();
        complex.timbre    = params.timbre->load();
        complex.symmetry  = params.symmetry->load();
        complex.timbreMod = params.timbreMod->load();
        complex.sync      = params.modSync->load() > 0.5f;
        complex.modSource = (int) params.complexModSource->load(); // 0 interno, 1 Osc 2, 2 Osc 3, 3 ruido
        {
            // Pasa-bajos de un polo del modulador a 2·fs: c = 1 - e^(-2π·fc / (2·fs)); arriba del todo, apagado (c = 0)
            const auto fc = params.modLp->load();
            complex.modLpCoef = fc >= 19999.0f ? 0.0 : 1.0 - std::exp (-juce::MathConstants<double>::twoPi * fc / (2.0 * getSampleRate()));
        }
        const auto externalMod = complex.on && complex.modSource != 0;
        const auto noiseModulates = externalMod && complex.modSource == 3;
        // Osc 2 u Osc 3 de modulador: sus fases avanzan aunque no suenen (el ruido no tiene fase)
        const auto keepOscPhases = externalMod && ! noiseModulates;
        const auto fmIndexBase = complex.fmIndex; // la matriz modula alrededor de estos valores
        const auto timbreBase = complex.timbre;

        // Unísono: desafinación lineal simétrica ±Detune cents y panorama equal-power ±Spread
        // (×√2: el oscilador centrado queda a ganancia 1 por canal). Suma normalizada por 1/√N:
        // osciladores desafinados se suman en potencia, no en amplitud.
        const auto detune = params.unisonDetune->load();
        const auto spread = params.unisonSpread->load();
        const auto stereo = numUnison > 1 && spread > 0.0f;
        const auto unisonNorm = 1.0f / std::sqrt ((float) numUnison);
        for (int u = 0; u < numUnison; ++u)
        {
            const auto pos = numUnison > 1 ? 2.0f * (float) u / (float) (numUnison - 1) - 1.0f : 0.0f; // -1..1
            lanes.ratio[(size_t) u] = std::exp2 (detune * pos / 1200.0);
            const auto angle = (spread * pos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            lanes.gainL[(size_t) u] = juce::MathConstants<float>::sqrt2 * std::cos (angle) * unisonNorm;
            lanes.gainR[(size_t) u] = juce::MathConstants<float>::sqrt2 * std::sin (angle) * unisonNorm;
        }

        while (--num >= 0)
        {
            auto freq = frequency;
            const auto ampEnvValue = adsr.getNextSample();
            const auto filterEnvValue = filterEnv.getNextSample(); // también fuente de la matriz (antes solo con el filtro encendido)
            if (level != levelTarget) // solo tras un redisparo con otra velocity (polo de 5 ms, como la rueda)
                level += controlCoef * (levelTarget - level);
            auto amp  = level * ampEnvValue;
            float lfoOctaves = 0.0f, lfoValue = 0.0f;

            // Pitch bend y rueda: suavizados con un polo de 5 ms (el MIDI llega con resolución de bloque)
            bendSmoothed += controlCoef * (bendTarget - bendSmoothed);
            modSmoothed  += controlCoef * (modTarget - modSmoothed);
            wheelSmoothed += controlCoef * (wheelTarget - wheelSmoothed);
            timbreSmoothed += controlCoef * (mpeTimbre - timbreSmoothed);
            memberBendSmoothed += controlCoef * (memberBendSemis - memberBendSmoothed);
            auto semis = bendSmoothed + memberBendSmoothed + glideSemis + pitchEnvAmount * pitchEnvValue + fineSemis;
            glideSemis *= glideCoef;
            pitchEnvValue *= pitchEnvCoef;

            // Aftertouch suavizado (5 ms, como la rueda)
            if (usePressure || matrix.uses[ModMatrix::srcAftertouch])
                pressureSmoothed += controlCoef * (juce::jmax (polyPressure, channelPressure) - pressureSmoothed);
            const auto pressureVibrato = usePressure ? atVibrato * pressureSmoothed : 0.0f;
            if (usePressure)
                amp *= 1.0f + atAmp * pressureSmoothed;

            // LFO 1 (también fuente del PWM, de la rueda de modulación y del vibrato por aftertouch)
            if (lfoOn || modSmoothed > 1.0e-4f || mix.pwmDepth > 0.0f || pressureVibrato > 1.0e-4f || matrix.uses[ModMatrix::srcLfo1])
            {
                const auto lfo = lfo1.process (lfoShape, lfoInc, rng);
                lfoValue = lfo;
                semis += (modSmoothed + pressureVibrato) * lfo; // rueda / aftertouch: vibrato ±1 st
                if (lfoOn)
                {
                    if (lfoTarget == 0)      semis += 2.0f * lfoDepth * lfo;
                    else if (lfoTarget == 1) amp *= 1.0f - lfoDepth * (0.5f - 0.5f * lfo);
                    else                     lfoOctaves = 2.0f * lfoDepth * lfo;
                }
            }

            // LFO 2: mismos alcances que el 1 (±2 st, tremolo total, ±2 oct); PWM ±0,45 de ancho; Osc 2 ±2 st
            float pwmFromLfo2 = 0.0f, osc2Semis = 0.0f, wtPosMod = 0.0f;
            if (usePressure)
                lfoOctaves += 3.0f * atFilter * pressureSmoothed;
            float lfo2Value = 0.0f;
            if (lfo2On || matrix.uses[ModMatrix::srcLfo2])
                lfo2Value = lfo2.process (lfo2Shape, lfo2Inc, rng);
            if (lfo2On)
            {
                const auto l2 = lfo2Value;
                switch (lfo2Target)
                {
                    case 0:  semis += 2.0f * lfo2Depth * l2; break;
                    case 1:  amp *= 1.0f - lfo2Depth * (0.5f - 0.5f * l2); break;
                    case 2:  lfoOctaves += 2.0f * lfo2Depth * l2; break;
                    case 3:  pwmFromLfo2 = 0.45f * lfo2Depth * l2; break;
                    case 5:  wtPosMod = 0.5f * lfo2Depth * l2; break;
                    default: osc2Semis = 2.0f * lfo2Depth * l2; break;
                }
            }
            // Matriz de modulación (Dsp/ModMatrix.h): fuentes de esta voz -> desplazamientos por destino
            std::array<float, ModMatrix::numDestinations> mtx {};
            if (matrix.active())
            {
                std::array<float, ModMatrix::numSources> src {};
                src[ModMatrix::srcLfo1] = lfoValue;
                src[ModMatrix::srcLfo2] = lfo2Value;
                src[ModMatrix::srcFilterEnv] = filterEnvValue;
                src[ModMatrix::srcAmpEnv] = ampEnvValue;
                src[ModMatrix::srcVelocity] = noteVelocity;
                src[ModMatrix::srcKeyTrack] = juce::jlimit (-1.0f, 1.0f, keyOctaves / 5.0f);
                src[ModMatrix::srcModWheel] = wheelSmoothed;
                src[ModMatrix::srcAftertouch] = pressureSmoothed;
                src[ModMatrix::srcTimbre] = timbreSmoothed;
                // Random Fade f: el desvío aleatorio de la nota se escala con 1 - f + f·envolvente de filtro (0..1). Con f = 1, ataque
                // 0 y sustain 0 empieza entero y llega a 0 con el decay: cada nota sale de un sitio distinto y converge en el suyo
                const auto fade = params.mtxRandomFade->load();
                src[ModMatrix::srcRandom] = noteRandom * (1.0f - fade + fade * filterEnvValue);
                src[ModMatrix::srcPitchBend] = mpe ? memberBend
                                                   : bendRangeNow > 0.0f ? juce::jlimit (-1.0f, 1.0f, bendSmoothed / bendRangeNow) : 0.0f;
                matrix.compute (src, mtx);
                lastMtx = mtx;
                lastSrc = src;
                semis += mtx[ModMatrix::dstPitch];
                osc2Semis += mtx[ModMatrix::dstOsc2Pitch];
                lfoOctaves += mtx[ModMatrix::dstCutoff];
                wtPosMod += mtx[ModMatrix::dstWtPosition];
                amp *= juce::jmax (0.0f, 1.0f + mtx[ModMatrix::dstVolume]);
            }
            if (semis != 0.0f)
                freq *= std::exp2 (semis / 12.0);

            if (drift > 0.0f)
            {
                // Deriva evaluada cada 64 muestras (1,3 ms a 48 kHz) en el instante siguiente, con rampa lineal entre medias
                if (--driftCountdown <= 0)
                {
                    driftCountdown = driftHop;
                    float nextVoice, nextOsc2;
                    updateDrift (start + driftHop, nextVoice, nextOsc2);
                    driftStep = (nextVoice - driftCents) / (float) driftHop;
                    osc2DriftStep = (nextOsc2 - osc2DriftCents) / (float) driftHop;
                }
                driftCents += driftStep;
                osc2DriftCents += osc2DriftStep;
                freq *= std::exp2 ((staticCents + drift * driftCents) / 1200.0);
            }

            const auto dt = freq / sampleRate;
            lastDt = dt;
            float left = 0.0f, right = 0.0f;

            // PWM desde el LFO (cualquier destino): ancho ±pwmDepth alrededor de Pulse Width, acotado a 5–95 %
            const auto pw = juce::jlimit (0.05f, 0.95f, mix.pulseWidth + mix.pwmDepth * lfoValue + pwmFromLfo2 + mtx[ModMatrix::dstPulseWidth]);
            complex.pw = pw;
            if (matrix.targets[ModMatrix::dstFmIndex]) complex.fmIndex = fmIndexBase + mtx[ModMatrix::dstFmIndex];
            if (matrix.targets[ModMatrix::dstTimbre])  complex.timbre = juce::jlimit (0.0f, 1.0f, timbreBase + mtx[ModMatrix::dstTimbre]);
            // Osc 2: deriva propia (desafinación fija y 1/f) además de seguir a Osc 1, y su posición WT. Antes de Osc 1: el
            // Complex puede usarlo como modulador (mismos valores que lee renderMonoSources más abajo)
            const auto osc2Drift = drift > 0.0f ? std::exp2 ((osc2StaticCents + drift * osc2DriftCents) / 1200.0) : 1.0;
            const auto osc2Mul = (osc2Semis != 0.0f ? std::exp2 (osc2Semis / 12.0) : 1.0) * osc2Drift;
            if (mix.osc2Table != nullptr)
                mix.osc2WtPos = juce::jlimit (0.0f, 1.0f, osc2WtPositionBase + mtx[ModMatrix::dstOsc2WtPosition]);
            // Osc 3 desde la matriz, solo en esta muestra (se restauran tras renderMonoSources): afinación y posición WT. Antes del
            // Complex, que lo lee como modulador (externalModulator usa mix.subRatio y mix.osc3WtPos)
            const auto savedSubRatio = mix.subRatio;
            const auto savedOsc3WtPos = mix.osc3WtPos;
            if (matrix.targets[ModMatrix::dstOsc3Pitch])
                mix.subRatio = savedSubRatio * std::exp2 (mtx[ModMatrix::dstOsc3Pitch] / 12.0);
            if (matrix.targets[ModMatrix::dstOsc3WtPosition])
                mix.osc3WtPos = juce::jlimit (0.0f, 1.0f, savedOsc3WtPos + mtx[ModMatrix::dstOsc3WtPosition]);
            // Ruido de modulador: una muestra del generador de la voz (su color), retenida en las dos submuestras del Complex; la
            // salida de Noise de renderMonoSources usa la misma (el generador avanza una vez por muestra)
            if (noiseModulates)
            {
                noiseMod = nextNoise (dt);
                noiseModReady = true;
            }

            if (complex.on)
            {
                // Oscilador complejo a 2x: dos submuestras (FM, AM, sync y wavefolder a 2·fs) y diezmado por FIR
                // de media banda. Medido en réplica (folder, Timbre 100 %, seno 1,27 kHz): aliasing audible
                // -53 dB a 1x -> -88 dB a 2x. Latencia del FIR: 15 muestras a 2·fs = 0,16 ms a 48 kHz.
                for (int sub = 0; sub < 2; ++sub)
                {
                    float subL = 0.0f, subR = 0.0f, firstMod = 0.0f;
                    double ph0[konisimd::kLanes], mp0[konisimd::kLanes];
                    std::copy (std::begin (lanes.phase), std::end (lanes.phase), ph0);
                    std::copy (std::begin (lanes.modPhase), std::end (lanes.modPhase), mp0);
                    konisimd::ComplexArgs args { complex.wave, complex.modWave, complex.ratio, complex.fmIndex,
                                                 (double) complex.am, (double) complex.pw, complex.sync };
                    if (externalMod)
                    {
                        externalModulator (sub, dt, osc2Mul, pw, args);
                        if (complex.modLpCoef > 0.0) // Mod LP sobre el modulador externo (un solo valor para todos los carriles)
                        {
                            modLpExt += complex.modLpCoef * (args.extMod - modLpExt);
                            args.extMod = modLpExt;
                        }
                    }
                    if (simd)
                    {
                        konisimd::complexStepAvx2 (lanes, args, 0.5 * dt, subL, subR, firstMod);
                    }
                    else
                    {
                        for (size_t u = 0; u < (size_t) numUnison; ++u)
                        {
                            float mod;
                            const auto s = complexSample (u, 0.5 * dt * (numUnison == 1 ? 1.0 : lanes.ratio[u]), mod, args);
                            if (u == 0)
                                firstMod = mod;
                            subL += s * (numUnison == 1 ? 1.0f : lanes.gainL[u]);
                            subR += s * (numUnison == 1 ? 1.0f : lanes.gainR[u]);
                        }
                    }

                    // minBLEP a 2·fs: la cola de los saltos anteriores y luego los saltos de este paso (vuelta, flancos y
                    // reinicio del sync), escalados por la AM y por la ganancia de cada carril
                    complexBlep.pop (subL, subR);
                    for (int u = 0; u < numUnison; ++u)
                    {
                        // Instante de la vuelta del modulador dentro de la submuestra: interno, desde su fase inicial; externo,
                        // desde lo recorrido tras la vuelta (extPhaseEnd / extDtM submuestras)
                        const auto dtM = args.external ? args.extDtM : 0.5 * dt * lanes.ratio[u] * complex.ratio;
                        const auto wrapped = args.external ? args.extWrapped : mp0[u] + dtM >= 1.0;
                        const auto at = args.external ? juce::jlimit (0.0, 1.0, 1.0 - args.extPhaseEnd / dtM) : (1.0 - mp0[u]) / dtM;
                        const auto amGain = (1.0f - complex.am) + complex.am * (float) lanes.mod[u];
                        const auto gL = (numUnison == 1 ? 1.0f : lanes.gainL[u]) * amGain;
                        const auto gR = (numUnison == 1 ? 1.0f : lanes.gainR[u]) * amGain;
                        MinBlep::syncedStep (complexBlep, complex.wave, ph0[u], lanes.inc[u], complex.sync && wrapped,
                                             wrapped ? at : 1.0, complex.pw, gL, gR);
                    }

                    // Wavefolder (Timbre) con modulación de timbre desde el modulador del primer oscilador
                    const auto t = juce::jlimit (0.0f, 1.0f, complex.timbre + complex.timbreMod * 0.5f * firstMod);
                    if (t > 0.0f)
                    {
                        subL = fold (subL, t);
                        subR = numUnison > 1 ? fold (subR, t) : subL;
                    }
                    decimator.push (subL, subR);
                }

                decimator.output (left, right);
                left  = blockDc (left, foldDc[0]);
                right = numUnison > 1 ? blockDc (right, foldDc[1]) : left;
            }
            else if (numUnison == 1)
            {
                left = right = oscillator (wave, lanes.phase[0], dt, pw, false);
                if (tri1)
                    left = right = mix.osc1Tri (left);
            }
            else if (simd)
            {
                double ph0[konisimd::kLanes];
                std::copy (std::begin (lanes.phase), std::end (lanes.phase), ph0);
                if (tri1)
                    konisimd::unisonStepAvx2 (lanes, wave, dt, (double) pw, left, right, mix.osc1Tri.a, mix.osc1Tri.r, mix.osc1Tri.m);
                else
                    konisimd::unisonStepAvx2 (lanes, wave, dt, (double) pw, left, right); // avanza las fases
                osc1Blep.pop (left, right);
                for (int u = 0; u < numUnison; ++u)
                    MinBlep::segment (osc1Blep, wave, ph0[u], dt * lanes.ratio[u], 0.0, 1.0, pw, lanes.gainL[u], lanes.gainR[u]);
            }
            else
            {
                for (size_t u = 0; u < (size_t) numUnison; ++u)
                {
                    auto s = oscillator (wave, lanes.phase[u], dt * lanes.ratio[u], pw, false);
                    if (tri1)
                        s = mix.osc1Tri (s);
                    left  += s * lanes.gainL[u];
                    right += s * lanes.gainR[u];
                }
            }

            if (! complex.on && ! simd)
                osc1Blep.pop (left, right);

            // Mezclador: Osc 1 (con unísono/complejo, estéreo) + Osc 2, sub, ruido y sample (mono, al centro)
            const auto osc1Gain = matrix.targets[ModMatrix::dstOsc1Level] ? juce::jlimit (0.0f, 1.0f, mix.osc1Level + mtx[ModMatrix::dstOsc1Level]) * osc1Gate
                                                                          : mix.osc1Level;
            left  *= osc1Gain;
            right *= osc1Gain;
            tapSource (Params::srcOsc1, std::max (std::abs (left), std::abs (right)));
            const auto savedOsc2Level = mix.osc2Level, savedSubLevel = mix.subLevel, savedNoiseLevel = mix.noiseLevel; // la matriz los mueve solo en esta muestra
            if (matrix.targets[ModMatrix::dstOsc2Level])  mix.osc2Level = juce::jlimit (0.0f, 1.0f, savedOsc2Level + mtx[ModMatrix::dstOsc2Level]) * osc2Gate;
            if (matrix.targets[ModMatrix::dstOsc3Level])  mix.subLevel = juce::jlimit (0.0f, 1.0f, savedSubLevel + mtx[ModMatrix::dstOsc3Level]) * osc3Gate;
            if (matrix.targets[ModMatrix::dstNoiseLevel]) mix.noiseLevel = juce::jlimit (0.0f, 1.0f, savedNoiseLevel + mtx[ModMatrix::dstNoiseLevel]) * noiseGate;
            auto monoSources = renderMonoSources (dt, pw, osc2Mul, keepOscPhases);
            noiseModReady = false; // sin usar (Noise a 0 o renderMonoSources sin trabajo): la muestra siguiente calcula otra
            mix.osc2Level = savedOsc2Level;
            mix.subLevel = savedSubLevel;
            mix.noiseLevel = savedNoiseLevel;
            mix.subRatio = savedSubRatio;
            mix.osc3WtPos = savedOsc3WtPos;
            if (sampleLevel > 0.0f)
                monoSources += tapSource (Params::srcSample, sampleLevel * sample.render (dt, blockSample));
            if (stringLevel > 0.0f && stringActive)
                monoSources += tapSource (Params::srcString, stringLevel * renderString (dt));
            if (blockWavetable != nullptr)
                monoSources += tapSource (Params::srcWavetable, wtLevel * renderWavetable (*blockWavetable, dt, juce::jlimit (0.0f, 1.0f, wtPositionBase + wtPosMod)));
            left  += monoSources;
            right += monoSources;

            if (satOn && satPre) // saturación de esta voz antes del filtro (Pre-filter): el filtro suaviza lo que añade
            {
                satGain += satGStep; satOut += satOutStep; satWet += satWetStep;
                left  = satL.process (satMode, left, satGain, satOut, satWet, vcaHpfCoef);
                right = satR.process (satMode, right, satGain, satOut, satWet, vcaHpfCoef);
            }

            if (filterOn)
            {
                const auto fc = juce::jlimit (20.0f, maxCutoff,
                                              baseCutoff * std::exp2 (envOctaves * filterEnvValue + lfoOctaves));
                auto kNow = k, compNow = inputComp, driveNow = driveGain, normNow = driveNorm, wetNow = cleanWet; // resonancia y drive (matriz)
                if (matrix.targets[ModMatrix::dstResonance])
                {
                    kNow = juce::jlimit (0.0f, 4.4f, k + mtx[ModMatrix::dstResonance]);
                    compNow = 1.0f + 0.5f * kNow;
                }
                if (matrix.targets[ModMatrix::dstDrive])
                {
                    driveNow = juce::Decibels::decibelsToGain (26.0f * juce::jlimit (0.0f, 1.0f, driveBase + mtx[ModMatrix::dstDrive]));
                    normNow = 1.0f / std::sqrt (driveNow);
                    wetNow = juce::jmin (1.0f, 4.0f * juce::jlimit (0.0f, 1.0f, driveBase + mtx[ModMatrix::dstDrive]));
                }
                const auto g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);

                // Escala 0,5 al entrar y x2 al salir: con drive 0 la saturación apenas actúa (tanh(0,5) = 0,46).
                // En estéreo cada canal tiene su ladder (mismo coeficiente g: el coste extra es un process())
                // compNow compensa la pérdida de la banda pasante del ladder con resonancia; los demás modelos no la tienen
                const auto comp = filterMode < VoiceFilters::svfLp ? compNow : 1.0f;
                auto runFilter = [&] (VoiceFilters::VoiceFilter& f, float x)
                {
                    auto in = std::tanh (driveNow * 0.5f * x) * normNow;
                    if (cleanDrive)
                        in = 0.5f * x + wetNow * (in - 0.5f * x);
                    return 2.0f * f.process (comp * in, g, kNow, filterMode, fc, (float) sampleRate, filterSpread);
                };
                if (ladderQuality == 0) // Classic: la ruta original, sin cambios
                {
                    left  = runFilter (filter, left);
                    right = stereo ? runFilter (filterR, right) : left;
                }
                else
                {
                    // Balanced (2x + ADAA en la tanh de entrada) y High Quality (4x): drive + ladder a N·fs entre el interpolador
                    // y el diezmador IIR de media banda de VoiceSat (4x = dos etapas 2x). g se calcula a la tasa interna, así que la
                    // respuesta sigue la bilineal a N·fs: por encima del corte deja pasar más agudos que Classic (p. ej. LP 24 a
                    // 8 kHz: +3,4 dB a 12 kHz y +11,6 dB a 16 kHz, medido). Retardo de grupo del par IIR a 48 kHz: 3,6 → 11,6
                    // muestras (2x) y 5,4 → 13,8 (4x) de 0,1 a 20 kHz; no se comunica como latencia
                    const auto factor = ladderQuality == 1 ? 2 : 4;
                    const auto adaa = ladderQuality == 1;
                    const auto rateOs = (float) sampleRate * (float) factor;
                    const auto gOs = std::tan (juce::MathConstants<float>::pi * fc / rateOs);
                    auto stage = [&] (VoiceFilters::VoiceFilter& f, LadderOs& o, double xs)
                    {
                        const auto x = (float) xs;
                        const auto a = (double) (driveNow * 0.5f * x);
                        float sat, linear;
                        if (adaa) // ADAA1 (VoiceSaturator.h): (F(a) - F(a')) / (a - a'), F = log cosh
                        {
                            const auto F = VoiceSat::logCosh (a), d = a - o.ax;
                            sat = (float) (std::abs (d) > 1.0e-5 ? (F - o.af) / d : std::tanh (0.5 * (a + o.ax)));
                            o.ax = a; o.af = F;
                            // Rama lineal de Clean Drive alineada con la ADAA: en su límite lineal la ADAA da la media de dos
                            // muestras (retardo de media muestra y cos(πf/N·fs)); la rama lineal hace lo mismo, a la tasa interna
                            linear = 0.25f * (x + o.xPrev);
                            o.xPrev = x;
                        }
                        else
                        {
                            sat = std::tanh ((float) a);
                            linear = 0.5f * x;
                        }
                        auto in = sat * normNow;
                        if (cleanDrive)
                            in = linear + wetNow * (in - linear);
                        return (double) (2.0f * f.process (comp * in, gOs, kNow, filterMode, fc, rateOs, filterSpread));
                    };
                    auto runOs = [&] (VoiceFilters::VoiceFilter& f, LadderOs& o, float x)
                    {
                        const auto [e, od] = o.u1.process ((double) x);
                        if (factor == 2)
                        {
                            const auto s0 = stage (f, o, e);
                            const auto s1 = stage (f, o, od);
                            return (float) o.d1.process (s0, s1);
                        }
                        const auto [e0, o0] = o.u2.process (e);
                        const auto [e1, o1] = o.u2.process (od);
                        const auto s0 = stage (f, o, e0);
                        const auto s1 = stage (f, o, o0);
                        const auto s2 = stage (f, o, e1);
                        const auto s3 = stage (f, o, o1);
                        const auto a = o.d2.process (s0, s1);
                        const auto b = o.d2.process (s2, s3);
                        return (float) o.d1.process (a, b);
                    };
                    left  = runOs (filter, osL, left);
                    right = stereo ? runOs (filterR, osR, right) : left;
                }
            }

            if (satOn && ! satPre) // saturación de esta voz, tras el filtro y antes del VCA (Post-filter)
            {
                satGain += satGStep; satOut += satOutStep; satWet += satWetStep;
                left = satL.process (satMode, left, satGain, satOut, satWet, vcaHpfCoef);
                if (filterOn && ! stereo) { right = left; satR = satL; } // filtro mono: R = L (mitad de coste; R sigue al día)
                else right = satR.process (satMode, right, satGain, satOut, satWet, vcaHpfCoef);
            }

            auto panL = voicePanL, panR = voicePanR;
            if (matrix.targets[ModMatrix::dstPan])
            {
                const auto angle = (juce::jlimit (-1.0f, 1.0f, panBase + mtx[ModMatrix::dstPan]) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                panL = juce::MathConstants<float>::sqrt2 * std::cos (angle);
                panR = juce::MathConstants<float>::sqrt2 * std::sin (angle);
            }
            if (vcaAnalog) // VCA Click / Leak (rama aparte: apagados, la salida no cambia ni un bit)
            {
                const auto feedIn = vcaFeed * level * ampEnvValue;
                vcaHpfY = feedIn - vcaHpfX + vcaHpfCoef * vcaHpfY;
                vcaHpfX = feedIn;
                if (std::abs (vcaHpfY) < 1.0e-9f && vcaFeed == 0.0f) vcaHpfY = vcaHpfX = 0.0f; // fin de la cola: vuelve a la rama normal
                const auto ampLeak = amp + vcaLeakGain * (1.0f - ampEnvValue);
                if (out.getNumChannels() >= 2)
                {
                    out.addSample (0, start, left * ampLeak * panL + vcaHpfY);
                    out.addSample (1, start, right * ampLeak * panR + vcaHpfY);
                }
                else
                    out.addSample (0, start, 0.5f * (left + right) * ampLeak + vcaHpfY);
            }
            else if (out.getNumChannels() >= 2)
            {
                out.addSample (0, start, left * amp * panL);
                out.addSample (1, start, right * amp * panR);
            }
            else
            {
                out.addSample (0, start, 0.5f * (left + right) * amp);
            }

            ++start;
            if (! complex.on && ! simd) // modo complejo: avanzan en complexSample (FM y sync); AVX2: dentro de la ruta SIMD
            {
                for (size_t u = 0; u < (size_t) numUnison; ++u)
                {
                    MinBlep::segment (osc1Blep, wave, lanes.phase[u], dt * (numUnison == 1 ? 1.0 : lanes.ratio[u]), 0.0, 1.0, pw,
                                      numUnison == 1 ? 1.0f : lanes.gainL[u], numUnison == 1 ? 1.0f : lanes.gainR[u]);
                    lanes.phase[u] += dt * (numUnison == 1 ? 1.0 : lanes.ratio[u]);
                    lanes.phase[u] -= std::floor (lanes.phase[u]); // [0, 1) aunque el paso supere un ciclo
                }
            }

            // Con VCA Leak la voz sigue sonando a ese nivel hasta que se reutiliza; con VCA Click, hasta que el paso alto del
            // feedthrough se apaga (cortarlo antes sería un clic digital)
            if (! adsr.isActive() && vcaLeakGain <= 0.0f && std::abs (vcaHpfY) < 1.0e-6f)
            {
                vcaHpfX = vcaHpfY = 0.0f;
                clearCurrentNote();
                break;
            }
        }
        lastRenderEnd = params.randomClock.timeline + start; // Free Running: instante en que la voz dejó de avanzar
        if (matrix.active()) // anillos de modulación de la UI: valores de esta voz al final del bloque
        {
            for (size_t d = 0; d < lastMtx.size(); ++d)
                params.liveMod[d].store (lastMtx[d], std::memory_order_relaxed);
            params.liveModTick.fetch_add (1, std::memory_order_relaxed);
            const auto card = (size_t) juce::jlimit (0, Params::liveVoices - 1, cardIndex); // un punto por voz en los mandos
            for (size_t d = 0; d < lastMtx.size(); ++d)
                params.liveModVoice[card][d].store (lastMtx[d], std::memory_order_relaxed);
            params.liveVoiceTick[card].fetch_add (1, std::memory_order_relaxed);
            for (size_t s = 0; s < lastSrc.size(); ++s) // lámparas piloto: valor de cada fuente
                params.liveSrc[s].store (lastSrc[s], std::memory_order_relaxed);
        }
    }

private:
    struct ComplexParams
    {
        bool on = false, sync = false;
        int wave = 0, modWave = 0, modSource = 0; // modSource: 0 modulador interno, 1 Osc 2, 2 Osc 3
        double ratio = 1.0;
        double fmIndex = 0.0;
        float am = 0.0f, timbre = 0.0f, symmetry = 0.0f, timbreMod = 0.0f;
        float pw = 0.5f; // ancho de pulso actual (PWM), también para el principal en modo complejo
        double modLpCoef = 0.0; // pasa-bajos del modulador (Mod LP) a 2·fs; 0 = apagado
    };
    double modLpLane[konisimd::kLanes] {}; // estado del Mod LP del modulador interno, por carril del unísono
    double modLpExt = 0.0;                  // y del externo (Osc 2 u Osc 3)

    // Oscilador complejo (estilo Buchla 259) para el oscilador u del unísono. dtP = incremento de fase del principal.
    //  FM lineal que atraviesa cero: f = f_p + I·f_m·m(t) -> dt = dtP + I·dtM·m; con dt < 0 la fase retrocede.
    //  AM / ring: s·((1 - a) + a·m); a = 1 es modulación en anillo.
    //  Sync duro: al reiniciarse el modulador, la fase del principal vuelve a 0 más el tiempo transcurrido desde el
    //  reinicio (modPhase / dtM muestras), para no cuantizar el instante de sync a la muestra.
    //  Modulador externo (args.external, Osc 2 u Osc 3): su valor, incremento y vuelta llegan dados (externalModulator) y
    //  la fase interna del modulador no avanza.
    float complexSample (size_t u, double dtP, float& modOut, const konisimd::ComplexArgs& args)
    {
        const auto dtM = args.external ? args.extDtM : dtP * complex.ratio;
        auto m = args.external ? (float) args.extMod : oscillator (complex.modWave, lanes.modPhase[u], dtM);
        if (! args.external && complex.modLpCoef > 0.0) // Mod LP del modulador interno, por carril
        {
            modLpLane[u] += complex.modLpCoef * ((double) m - modLpLane[u]);
            m = (float) modLpLane[u];
        }
        modOut = m;

        const auto dtInst = dtP + complex.fmIndex * dtM * (double) m;
        auto s = oscillator (complex.wave, lanes.phase[u], std::abs (dtInst), complex.pw, false);
        s *= (1.0f - complex.am) + complex.am * m;
        lanes.inc[u] = dtInst;
        lanes.mod[u] = m;

        auto wrapped = args.extWrapped;
        auto modEnd = args.extPhaseEnd;
        if (! args.external)
        {
            lanes.modPhase[u] += dtM;
            wrapped = lanes.modPhase[u] >= 1.0;
            lanes.modPhase[u] -= std::floor (lanes.modPhase[u]); // Ratio 16 en notas altas: más de un ciclo por muestra
            modEnd = lanes.modPhase[u];
        }

        lanes.phase[u] += dtInst;
        lanes.phase[u] -= std::floor (lanes.phase[u]); // envuelve en ambos sentidos
        if (complex.sync && wrapped)
        {
            lanes.phase[u] = modEnd / dtM * std::abs (dtInst); // > 1 si la portadora da más de un ciclo por muestra
            lanes.phase[u] -= std::floor (lanes.phase[u]);
        }

        return s;
    }

    // Modulador externo del Complex (Osc 2 u Osc 3) en la submuestra sub (0, 1) de esta muestra. Se lee de la trayectoria de
    // fase del oscilador en el instante τ = sub/2 (y τ + 1/2 para su vuelta), sin tocar su estado: renderMonoSources la avanza
    // después, en la misma muestra. Osc 2 con su sync: se reinicia en f = (1 - syncPhase)/dt y desde ahí su fase es
    // (τ - f)·dt2 (en τ = 1, el syncPhase/dt·dt2 de renderMonoSources). El valor es la forma de onda evaluada a 2·fs con
    // PolyBLEP (como el modulador interno), no la salida minBLEP del oscilador audible.
    //  Osc 2 y Osc 3 siguen el mismo código, cada uno con su fase, su incremento, su sync, su forma y su tabla o su sample (la
    //  cuadrada de Osc 3 va al 50 %). Ruido (modSource 3): el valor de noiseMod, retenido en las dos submuestras; incremento
    //  nominal el de la nota (escala del índice FM, como un modulador interno a Ratio 1) y sin vuelta (Mod Sync no actúa).
    void externalModulator (int sub, double dt, double osc2Mul, float pw, konisimd::ComplexArgs& a) const
    {
        a.external = true;
        if (complex.modSource == 3)
        {
            a.extMod = (double) noiseMod;
            a.extDtM = 0.5 * dt;
            a.extWrapped = false;
            a.extPhaseEnd = 0.0;
            return;
        }
        const auto osc2 = complex.modSource == 1;
        const auto t0 = 0.5 * sub, t1 = t0 + 0.5;
        const auto inc = osc2 ? dt * mix.osc2Ratio * osc2Mul // = dt2 de renderMonoSources
                              : dt * mix.subRatio;           // = dt3
        const auto phase = osc2 ? osc2Phase : subPhase;
        const auto syncs = (osc2 ? mix.osc2Sync : mix.osc3Sync) && syncPhase + dt >= 1.0;
        const auto f = (1.0 - syncPhase) / dt;
        const auto unwrapped = [&] (double t) { return syncs && t >= f ? (t - f) * inc : phase + t * inc; };
        const auto u0 = unwrapped (t0), u1 = unwrapped (t1);
        const auto reset = syncs && t0 < f && f <= t1;
        const auto p0 = u0 - std::floor (u0);
        a.extDtM = 0.5 * inc;
        a.extWrapped = reset || std::floor (u1) > std::floor (u0);
        a.extPhaseEnd = u1 - std::floor (u1);
        const auto wave = osc2 ? mix.osc2Wave : mix.subWave;
        if (wave == sampleWave)
        {
            // Sample: el cursor en el instante τ (tras el reinicio del sync, desde Start), con su final, su bucle y el fundido de
            // Length; la fase nominal del oscilador sigue dando el incremento (escala del índice FM) y la vuelta (Mod Sync)
            a.extMod = 0.0;
            const auto& r = osc2 ? osc2Sample : osc3Sample;
            const auto* blockData = osc2 ? mix.osc2Sample : mix.osc3Sample;
            if (blockData != nullptr && r.data == blockData)
            {
                const auto afterReset = syncs && t0 >= f;
                const auto at = afterReset ? r.loopStart + (t0 - f) * inc * r.data->sampleRate / r.rootHz
                                           : r.pos + t0 * inc * r.data->sampleRate / r.rootHz;
                const auto p = (afterReset ? ! r.cut : r.playing) ? r.place (at) : -1.0;
                if (p >= 0.0)
                    a.extMod = r.valueAt (p) * r.fade();
            }
        }
        else if (wave != wavetableWave)
            a.extMod = oscillator (wave, p0, a.extDtM, osc2 ? pw : 0.5f, true);
        else
        {
            const auto* table = osc2 ? mix.osc2Table : mix.osc3Table;
            a.extMod = table != nullptr ? readWavetable (*table, p0, a.extDtM, osc2 ? mix.osc2WtPos : mix.osc3WtPos) : 0.0;
        }
    }

    // Wavefolder senoidal: f(v) = sin(a·v), v = x + Symmetry/2, a = π/2·(1 + 9·Timbre). Se evalúa a 2·fs.
    // Mezcla con la entrada en el primer 10 % de Timbre (sin salto al activarlo).
    // ADAA1 descartado tras medir: con entradas con saltos (sierra) empeoraba el aliasing (-27 dB) al promediar
    // a través de la discontinuidad.
    float fold (float x, float timbreAmount) const
    {
        const auto a = juce::MathConstants<float>::halfPi * (1.0f + 9.0f * timbreAmount);
        const auto folded = std::sin (a * (x + 0.5f * complex.symmetry));
        const auto w = juce::jmin (1.0f, timbreAmount * 10.0f);
        return x + w * (folded - x);
    }

    // Bloqueo de DC a 10 Hz tras el folder (Symmetry pliega de forma asimétrica y genera DC)
    struct DcState { float x = 0.0f, y = 0.0f; };

    float blockDc (float x, DcState& st) const
    {
        const auto coef = 1.0f - juce::MathConstants<float>::twoPi * 10.0f / (float) getSampleRate();
        st.y = x - st.x + coef * st.y;
        st.x = x;
        return st.y;
    }

    // Diezmador 2:1 estéreo: FIR de media banda de 31 coeficientes (ventana de Kaiser, β = 7), fase lineal.
    // Ganancia DC 1; los coeficientes pares respecto al centro (salvo él) son 0 -> 16 productos por canal.
    struct Decimator
    {
        static constexpr int taps = 31;

        static const std::array<float, taps>& coefficients() { return halfbandCoefficients(); }

        void reset() { history = {}; pos = 0; }

        void push (float l, float r)
        {
            history[0][(size_t) pos] = l;
            history[1][(size_t) pos] = r;
            pos = (pos + 1) % taps;
        }

        void output (float& l, float& r) const
        {
            const auto& h = coefficients();
            float accL = 0.0f, accR = 0.0f;
            for (int k = 0; k < taps; ++k)
            {
                const auto c = h[(size_t) k];
                if (c == 0.0f) continue;
                const auto idx = (size_t) ((pos - 1 - k + taps) % taps);
                accL += c * history[0][idx];
                accR += c * history[1][idx];
            }
            l = accL;
            r = accR;
        }

        std::array<std::array<float, taps>, 2> history {};
        int pos = 0;
    };

    struct MixParams
    {
        float osc1Level = 1.0f, pulseWidth = 0.5f, pwmDepth = 0.0f, osc2Level = 0.0f, subLevel = 0.0f, noiseLevel = 0.0f;
        int osc2Wave = saw, subWave = square; // subWave: forma de Osc 3 (códigos de osc2WaveFromChoice, como osc2Wave)
        double osc2Ratio = 1.0, subRatio = 0.5;
        bool osc2Sync = false, osc3Sync = false;
        int noiseColor = 0; // 0 blanco, 1 rosa, 2 LFSR largo, 3 LFSR corto
        const WavetableData* osc2Table = nullptr; // modo Wavetable: tabla del bloque y posición de esta muestra
        float osc2WtPos = 0.0f;
        const SampleData* osc2Sample = nullptr; // modo Sample: fichero del bloque
        const WavetableData* osc3Table = nullptr; // Osc 3, igual (posición por bloque)
        float osc3WtPos = 0.0f;
        const SampleData* osc3Sample = nullptr;
        TriShape osc1Tri, osc2Tri, osc3Tri;     // forma del triángulo (TriSym, TriRound); inactiva a 0 %
    };
    static constexpr int wavetableWave = 4; // osc2Wave / osc3Wave = Wavetable (código del motor; opción 10, osc2WaveFromChoice)
    static constexpr int sampleWave = 5;    // osc2Wave / osc3Wave = Sample (opción 11)

    // Fuentes mono del mezclador. dt = incremento de fase de Osc 1 (con vibrato y drift), así Osc 2 y el sub los siguen.
    //  Osc 2: dt·2^(oct + (semi + fine/100)/12); sync duro a la fundamental de Osc 1 (fase de referencia sin unísono ni FM),
    //    reiniciando con la fracción de muestra transcurrida desde el reinicio.
    //  Osc 3: dt·2^(oct + (semi + fine/100)/12) (osc3Ratio); seno, triángulo, sierra o cuadrada al 50 % (por defecto, la cuadrada
    //    una octava por debajo: el sub de siempre), las mezclas, o su propia tabla o su propio sample; mismo sync que Osc 2 (osc3Sync). minBLEP en sierra y
    //    cuadrada, como Osc 1 y Osc 2.
    //  Ruido: blanco uniforme (RMS 0,577) o rosa (filtro "refined" de Paul Kellet, musicdsp.org); medido en réplica:
    //    plano ±0,2 dB por banda de octava de 125 Hz a 4 kHz (-0,7 dB a 8 kHz); ×0,3277 iguala su RMS al del blanco.
    // keepPhases: Osc 2 u Osc 3 modulan el Complex; sus fases avanzan aunque su nivel sea 0 (o estén en Mute o fuera del Solo).
    //  Con el ruido de modulador, la salida de Noise usa el valor ya calculado (noiseModReady)
    // Medidores del Mixer: pico de cada fuente en este bloque (tras su nivel, antes del filtro y sin las colas minBLEP) y
    // publicación al empezar el bloque siguiente, como máximo entre voces (Params::sourcePeak)
    float tapSource (int k, float v)
    {
        srcPeak[(size_t) k] = std::max (srcPeak[(size_t) k], std::abs (v));
        return v;
    }
    void publishSourcePeaks()
    {
        for (size_t k = 0; k < srcPeak.size(); ++k)
        {
            if (srcPeak[k] <= 0.0f) continue;
            auto& shared = params.sourcePeak[k];
            auto seen = shared.load (std::memory_order_relaxed);
            while (srcPeak[k] > seen && ! shared.compare_exchange_weak (seen, srcPeak[k], std::memory_order_relaxed)) {}
            srcPeak[k] = 0.0f;
        }
    }
    std::array<float, 7> srcPeak {};

    float renderMonoSources (double dt, float pw, double osc2Mul, bool keepPhases)
    {
        float out = 0.0f, unused = 0.0f;
        monoBlep.pop (out, unused); // cola minBLEP de Osc 2 y sub (saltos de muestras anteriores)
        if (mix.osc2Level <= 0.0f && mix.subLevel <= 0.0f && mix.noiseLevel <= 0.0f && ! keepPhases)
        {
            syncPhase += dt; // mantiene la referencia de sync en fase aunque Osc 2 esté a 0
            syncPhase -= std::floor (syncPhase);
            return out;
        }

        const auto dt2 = dt * mix.osc2Ratio * osc2Mul;
        lastDt2 = dt2;
        lastSubRatio = mix.subRatio;

        const auto osc2Table = mix.osc2Wave == wavetableWave; // Osc 2 en modo Wavetable (mix.osc2Table puede faltar: calla)
        const auto osc2Sampled = mix.osc2Wave == sampleWave;  // Osc 2 en modo Sample (mix.osc2Sample puede faltar: calla)
        if (mix.osc2Level > 0.0f)
        {
            if (! osc2Table && ! osc2Sampled)
            {
                const auto s = oscillator (mix.osc2Wave, osc2Phase, dt2, pw, false);
                out += tapSource (Params::srcOsc2, mix.osc2Level * (mix.osc2Tri.active && mix.osc2Wave == triangle ? mix.osc2Tri (s) : s));
            }
            else if (osc2Table && mix.osc2Table != nullptr)
                out += tapSource (Params::srcOsc2, mix.osc2Level * readWavetable (*mix.osc2Table, osc2Phase, dt2, mix.osc2WtPos));
        }
        // Osc 2 Sample (y Osc 3 Sample, abajo): avanza siempre que se llega aquí (también a nivel 0, en Mute o como modulador del
        // Complex), como su fase. Con sync, el salto entre el valor en el instante del reinicio (syncAt = (1 - syncPhase)/dt, la
        // misma referencia para Osc 2 y Osc 3) y el de Start se mide antes de avanzar y se corrige con minBLEP más abajo
        const auto syncNow = syncPhase + dt >= 1.0;
        const auto syncAt = syncNow ? (1.0 - syncPhase) / dt : 1.0;
        const auto osc2SampleJump = osc2Sampled ? sampleSyncJump (osc2Sample, mix.osc2Sample, mix.osc2Level, mix.osc2Sync, syncNow, dt2, syncAt) : 0.0f;
        if (osc2Sampled)
            out += tapSource (Params::srcOsc2, mix.osc2Level * osc2Sample.render (dt2, mix.osc2Sample));

        // Osc 3: mismas formas que Osc 2 (la cuadrada va siempre al 50 %: no sigue al mando PW, como el Sub), su tabla y su sample
        const auto dt3 = dt * mix.subRatio;
        const auto osc3Table = mix.subWave == wavetableWave; // mix.osc3Table puede faltar: calla
        const auto osc3Sampled = mix.subWave == sampleWave;  // mix.osc3Sample puede faltar: calla
        if (mix.subLevel > 0.0f)
        {
            if (! osc3Table && ! osc3Sampled)
            {
                const auto s = oscillator (mix.subWave, subPhase, dt3, 0.5f, false);
                out += tapSource (Params::srcOsc3, mix.subLevel * (mix.osc3Tri.active && mix.subWave == triangle ? mix.osc3Tri (s) : s));
            }
            else if (osc3Table && mix.osc3Table != nullptr)
                out += tapSource (Params::srcOsc3, mix.subLevel * readWavetable (*mix.osc3Table, subPhase, dt3, mix.osc3WtPos));
        }
        const auto osc3SampleJump = osc3Sampled ? sampleSyncJump (osc3Sample, mix.osc3Sample, mix.subLevel, mix.osc3Sync, syncNow, dt3, syncAt) : 0.0f;
        if (osc3Sampled)
            out += tapSource (Params::srcOsc3, mix.subLevel * osc3Sample.render (dt3, mix.osc3Sample));

        if (mix.noiseLevel > 0.0f)
        {
            const auto n = noiseModReady ? noiseMod : nextNoise (dt); // con el ruido de modulador, el mismo valor (renderNextBlock)
            noiseModReady = false;
            out += tapSource (Params::srcNoise, mix.noiseLevel * n);
        }

        // Saltos de este paso para el minBLEP: Osc 2 y Osc 3 (con su reinicio si la referencia da la vuelta)
        {
            const auto syncWraps = syncPhase + dt >= 1.0;
            if (mix.osc2Level > 0.0f && ! osc2Table && ! osc2Sampled)
                MinBlep::syncedStep (monoBlep, mix.osc2Wave, osc2Phase, dt2, mix.osc2Sync && syncWraps,
                                     syncWraps ? (1.0 - syncPhase) / dt : 1.0, pw, mix.osc2Level, mix.osc2Level);
            // Wavetable: la tabla ya está limitada en banda (mipmaps); solo el reinicio del sync es un salto: tabla(0) - tabla(fase
            // en el instante del reinicio), con la misma posición y el mismo nivel de mipmap
            if (mix.osc2Level > 0.0f && osc2Table && mix.osc2Table != nullptr && mix.osc2Sync && syncWraps)
                tableSyncJump (*mix.osc2Table, osc2Phase, dt, dt2, mix.osc2WtPos, mix.osc2Level);
            // Sample: el mismo tratamiento (el fichero ya está limitado en banda por su nivel diezmado)
            if (osc2SampleJump != 0.0f)
                monoBlep.add (syncAt, osc2SampleJump * mix.osc2Level, osc2SampleJump * mix.osc2Level);
            // Osc 3: igual. Sin sync, syncedStep es el segment (0, 1) de siempre (bit a bit)
            if (mix.subLevel > 0.0f && ! osc3Table && ! osc3Sampled)
                MinBlep::syncedStep (monoBlep, mix.subWave, subPhase, dt3, mix.osc3Sync && syncWraps,
                                     syncWraps ? (1.0 - syncPhase) / dt : 1.0, 0.5, mix.subLevel, mix.subLevel);
            if (mix.subLevel > 0.0f && osc3Table && mix.osc3Table != nullptr && mix.osc3Sync && syncWraps)
                tableSyncJump (*mix.osc3Table, subPhase, dt, dt3, mix.osc3WtPos, mix.subLevel);
            if (osc3SampleJump != 0.0f)
                monoBlep.add (syncAt, osc3SampleJump * mix.subLevel, osc3SampleJump * mix.subLevel);
        }

        // Avance de fases (después de leer: mismo convenio que Osc 1)
        syncPhase += dt;
        const auto syncWrapped = syncPhase >= 1.0;
        syncPhase -= std::floor (syncPhase);
        osc2Phase += dt2;
        osc2Phase -= std::floor (osc2Phase);
        if (mix.osc2Sync && syncWrapped)
        {
            osc2Phase = syncPhase / dt * dt2; // > 1 si Osc 2 da más de un ciclo por muestra (por encima de fs)
            osc2Phase -= std::floor (osc2Phase);
            if (osc2Sampled) // de nuevo desde Start, en el instante exacto: lo recorrido tras el reinicio
                osc2Sample.restart (syncPhase / dt * dt2, mix.osc2Sample);
        }
        subPhase += dt3;
        subPhase -= std::floor (subPhase);
        if (mix.osc3Sync && syncWrapped)
        {
            subPhase = syncPhase / dt * dt3;
            subPhase -= std::floor (subPhase);
            if (osc3Sampled)
                osc3Sample.restart (syncPhase / dt * dt3, mix.osc3Sample);
        }

        return out;
    }

    // Sync de un oscilador en modo Wavetable: salto tabla(0) - tabla(fase en el instante del reinicio) al minBLEP
    void tableSyncJump (const WavetableData& table, double phase, double dt, double dtOsc, float position, float gain)
    {
        const auto f = (1.0 - syncPhase) / dt;
        auto ta = phase + dtOsc * f;
        ta -= std::floor (ta);
        const auto jump = readWavetable (table, 0.0, dtOsc, position) - readWavetable (table, ta, dtOsc, position);
        monoBlep.add (f, jump * gain, jump * gain);
    }

    // Una muestra del generador de ruido de la voz con su color (salida de Noise y modulador Noise del Complex)
    float nextNoise (double dt)
    {
        const auto w = 2.0f * rng.nextFloat() - 1.0f;
        float n = w;
        if (mix.noiseColor >= 2)
        {
            // LFSR de 15 bits (nesdev APU_Noise): realimentación bit0 XOR bit1 (largo) o bit0 XOR bit6 (corto), desplazamiento a la
            // derecha y realimentación en el bit 14. Salida ±1 según el bit 0 (×0,577: mismo RMS que el blanco uniforme)
            const auto shortMode = mix.noiseColor == 3;
            lfsrPhase += dt * (shortMode ? 93.0 : 8.0);
            if (lfsrPhase >= 64.0) // tope de trabajo por muestra: a esa velocidad la salida ya es ruido sin periodo audible
                lfsrPhase = 63.0 + (lfsrPhase - std::floor (lfsrPhase));
            while (lfsrPhase >= 1.0)
            {
                lfsrPhase -= 1.0;
                const auto fb = (lfsr ^ (lfsr >> (shortMode ? 6 : 1))) & 1u;
                lfsr = (lfsr >> 1) | (fb << 14);
            }
            n = (lfsr & 1u) ? 0.577f : -0.577f;
        }
        else if (mix.noiseColor == 1)
        {
            pink[0] = 0.99886f * pink[0] + w * 0.0555179f;
            pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
            pink[2] = 0.96900f * pink[2] + w * 0.1538520f;
            pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
            pink[4] = 0.55000f * pink[4] + w * 0.5329522f;
            pink[5] = -0.7616f * pink[5] - w * 0.0168980f;
            n = 0.3277f * (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362f);
            pink[6] = w * 0.115926f;
        }
        return n;
    }

    void updateEnvelopes()
    {
        const auto analogCurve = (int) params.envCurve->load() == 1;
        adsr.setParameters ({ params.attack->load(), params.decay->load(), params.sustain->load(), params.release->load() }, analogCurve);

        // Envolvente de filtro: ADSR completa, con la misma curva que la de amplitud
        const auto fDecay = params.filterDecay->load();
        filterEnv.setParameters ({ params.filterAttack->load(), fDecay, params.filterSustain->load(), params.filterRelease->load() }, analogCurve);
    }

    static constexpr int maxUnison = 7;

    // Reproductor de un sample: la fuente Sample y Osc 2 en modo Sample, cada uno con su fichero. Lee a
    // f / f_raíz · fs_archivo / fs_host muestras por muestra, desde el nivel diezmado elegido al pulsar la nota (relación ≤ 1,06
    // en ese nivel: margen de +1 semitono para vibrato y drift). Si el sample cambia durante la nota, esa nota deja de leerlo
    // (nunca usa un puntero retirado).
    struct SampleReader
    {
        const SampleData* data = nullptr;
        bool playing = false, loop = false;
        bool cut = false; // terminó el fundido de Length: el sync de Osc 2 ya no lo reanuda
        int levelIndex = 0, cutoffSamples = -1, fadeSamples = 480, elapsed = 0;
        double pos = 0.0, loopStart = 0.0, rootHz = 261.63;

        // hz: frecuencia de la nota al pulsarla (la de Osc 2, con su afinación, para su sample)
        void start (const SampleData* d, double hz, int root, float startFraction, float lengthMs, double hostRate)
        {
            data = d;
            playing = data != nullptr;
            cut = false;
            if (! playing)
                return;

            rootHz = juce::MidiMessage::getMidiNoteInHertz (juce::jlimit (0, 127, root));
            const auto ratio = hz / rootHz * data->sampleRate / hostRate;
            levelIndex = juce::jlimit (0, SampleData::maxLevels - 1, (int) std::ceil (std::log2 (juce::jmax (1.0e-9, ratio * 1.06))));
            loopStart = juce::jlimit (0.0, (double) data->length() - 2.0, startFraction * (double) data->length());
            pos = loopStart;
            cutoffSamples = lengthMs >= 0.5f ? (int) (0.001 * lengthMs * hostRate) : -1;
            fadeSamples = (int) (0.010 * hostRate);
            elapsed = 0;
        }

        // Valor en la posición p (muestras del nivel 0), en el nivel diezmado de la nota
        float valueAt (double p) const
        {
            const auto scale = 1.0 / (double) (1 << levelIndex);
            return data->read (levelIndex, p * scale);
        }

        // Fin y comienzo del bucle: el bucle propio del formato (SoundFont, BRR) si lo trae y empieza tras Start; si no, de
        // Start al final
        void bounds (double& end, double& loopFrom) const
        {
            const auto native = loop && data->loopStart >= 0 && data->loopStart >= (int) loopStart;
            end = native ? (double) data->loopEnd : (double) data->length();
            loopFrom = native ? (double) data->loopStart : loopStart;
        }

        // Muestra actual y avance; dt = f / fs_host de lo que suena (la nota, u Osc 2 con su afinación y deriva)
        float render (double dt, const SampleData* blockData)
        {
            if (! playing || blockData != data)
            {
                playing = false;
                return 0.0f;
            }

            auto s = valueAt (pos);
            if (cutoffSamples >= 0) // modo LA: fundido lineal de 10 ms tras Length
            {
                const auto over = ++elapsed - cutoffSamples;
                if (over >= fadeSamples)
                {
                    playing = false;
                    cut = true;
                    return 0.0f;
                }
                if (over > 0)
                    s *= 1.0f - (float) over / (float) fadeSamples;
            }
            pos += dt * data->sampleRate / rootHz;
            double end, loopFrom;
            bounds (end, loopFrom);
            if (pos >= end)
            {
                if (loop && end - loopFrom > 1.0)
                    pos = loopFrom + std::fmod (pos - loopFrom, end - loopFrom);
                else
                    playing = false;
            }
            return s;
        }

        // Sin tocar el estado (modulador del Complex, salto del sync): la posición p ya con el final y el bucle aplicados,
        // o -1 si ahí ya no suena
        double place (double p) const
        {
            double end, loopFrom;
            bounds (end, loopFrom);
            if (p < end)
                return p;
            return loop && end - loopFrom > 1.0 ? loopFrom + std::fmod (p - loopFrom, end - loopFrom) : -1.0;
        }

        // Ganancia del fundido de Length en la muestra actual (la que aplicará render)
        float fade() const
        {
            const auto over = elapsed + 1 - cutoffSamples;
            return cutoffSamples < 0 || over <= 0 ? 1.0f : juce::jmax (0.0f, 1.0f - (float) over / (float) fadeSamples);
        }

        bool canRestart (const SampleData* blockData) const { return data != nullptr && blockData == data && ! cut; }

        // Sync de Osc 2: vuelve a Start más lo recorrido desde el reinicio (phaseSinceReset = tiempo · f / fs_host)
        void restart (double phaseSinceReset, const SampleData* blockData)
        {
            if (! canRestart (blockData))
                return;
            const auto p = place (loopStart + phaseSinceReset * data->sampleRate / rootHz);
            playing = p >= 0.0;
            if (playing)
                pos = p;
        }
    };

    // Sync de un oscilador en modo Sample (Osc 2 u Osc 3): salto entre el valor en el instante del reinicio (syncAt, con la
    // referencia dando la vuelta en esta muestra: wraps) y el de Start, medido antes de avanzar el sample y corregido con minBLEP;
    // 0 sin reinicio. Sin sync el sample suena tal cual
    static float sampleSyncJump (const SampleReader& r, const SampleData* blockData, float level, bool sync, bool wraps,
                                 double dtOsc, double syncAt)
    {
        if (! (level > 0.0f && sync && wraps))
            return 0.0f;
        const auto before = r.playing && r.data == blockData ? r.place (r.pos + syncAt * dtOsc * r.data->sampleRate / r.rootHz) : -1.0;
        const auto after = r.canRestart (blockData) ? r.valueAt (r.loopStart) : 0.0f;
        return r.fade() * (after - (before >= 0.0 ? r.valueAt (before) : 0.0f));
    }

    void startSample (int note)
    {
        const auto hz = juce::MidiMessage::getMidiNoteInHertz (note) * KoniTuning::ratio (note);
        sample.start (sampleSlot.current.load (std::memory_order_acquire), hz, (int) params.sampleRoot->load(),
                      params.sampleStart->load(), params.sampleLength->load(), getSampleRate());
        // Osc 2: el nivel diezmado se elige con su afinación al pulsar (Oct, Semi y Fine). La deriva, el LFO 2 y la matriz lo
        // mueven después, como el vibrato a la fuente Sample: por encima del margen de +1 semitono el nivel no se cambia (aliasing)
        const auto osc2Ratio = std::exp2 ((double) params.osc2Octave->load()
                                          + ((double) params.osc2Semi->load() + (double) params.osc2Fine->load() / 100.0) / 12.0);
        osc2Sample.start (osc2SampleSlot.current.load (std::memory_order_acquire), hz * osc2Ratio, (int) params.osc2SampleRoot->load(),
                          params.osc2SampleStart->load(), params.osc2SampleLength->load(), getSampleRate());
        // Osc 3, igual, con su relación (la de mix.subRatio)
        osc3Sample.start (osc3SampleSlot.current.load (std::memory_order_acquire), hz * osc3Ratio(), (int) params.osc3SampleRoot->load(),
                          params.osc3SampleStart->load(), params.osc3SampleLength->load(), getSampleRate());
    }

    // Relación de Osc 3 con Osc 1: 2^oct · 2^((semi + fine/100)/12). 2^oct es exacto y el producto por una potencia de 2 también:
    // con Semi y Fine a 0 da 0,5 (Oct -1, el sub de siempre) o 0,25 bit a bit
    double osc3Ratio() const
    {
        return std::exp2 ((double) params.osc3Octave->load())
             * std::exp2 (((double) params.osc3Semi->load() + (double) params.osc3Fine->load() / 100.0) / 12.0);
    }

    // Parámetros de la fuente Math Models que se recalculan por bloque con la frecuencia base de la nota (renderNextBlock y
    // renderMathPreview)
    void updateStringParams()
    {
        const auto sampleRate = getSampleRate();
        // Filtro de lazo (1 - S) + S·z^-1 y ρ, por bloque con la frecuencia base (vibrato y bend lo mueven < 1 %).
        // g = ganancia por periodo para caer 60 dB en Decay. S = ½ (promedio clásico) con ρ = g / cos(ω/2) si basta;
        // en agudos, donde cos(ω/2) < g, "decay stretching" (Jaffe y Smith): S tal que |H(ω0)| = g y ρ = 1, con
        // |H|² = 1 - 4·S·(1 - S)·sin²(ω/2). Réplica (ks_check3.py): T60 exacto hasta 659 Hz, -1 % a 1,3 kHz,
        // -13 % a 2,6 kHz (antes 1,40 s en vez de 3 s a 1,3 kHz); afinación ±0,2 ct de 41 Hz a 4,2 kHz.
        // Con Harmonic, la cuerda suena n veces más aguda (ver startString): pérdidas y afinación con esa frecuencia
        const auto fS = frequency * (double) stringHarm;
        const auto g = std::pow (10.0, -3.0 / (fS * juce::jmax (0.05f, params.stringDecay->load())));
        const auto w0 = juce::MathConstants<double>::twoPi * fS / sampleRate;
        const auto s2 = std::pow (std::sin (0.5 * w0), 2.0);
        const auto c = (1.0 - g * g) / juce::jmax (1.0e-12, s2);
        if (c >= 1.0)
        {
            stringS = 0.5f;
            stringRho = (float) juce::jmin (0.99999, g / std::cos (0.5 * w0));
        }
        else
        {
            stringS = (float) (0.5 * (1.0 - std::sqrt (1.0 - c)));
            stringRho = 0.99999f;
        }
        // Damping: H(z) = (1 - a) / (1 - a·z^-1) en el lazo; ρ /= |H(ω0)| y su retardo de fase a la fundamental,
        // τ = atan (a·sin ω0 / (1 - a·cos ω0)) / ω0, se descuenta del retardo (Decay y afinación de la fundamental intactos)
        // Esquina relativa a la nota: fc = f0·2^(6·(1 - Damping)) (64·f0 .. f0): la pérdida relativa por periodo de
        // cada armónico es la misma en todo el teclado
        const auto damping = params.stringDamping->load();
        stringLpA = damping > 0.0f ? (float) std::exp (-juce::MathConstants<double>::twoPi * fS * std::exp2 (6.0 * (1.0 - damping)) / sampleRate) : 0.0f;
        stringLpTau = 0.0;
        if (stringLpA > 0.0f)
        {
            // Límite: ρ ≤ 0,99999 (ganancia en continua 1: con ρ > 1 la continua crecería en el lazo). Si |H(ω0)| no deja
            // compensar, a se reduce a la raíz de (1-m²)·a² - 2·(1 - m²·cos ω0)·a + (1-m²) = 0 con m = ρ/0,99999
            auto a = (double) stringLpA;
            auto mag = (1.0 - a) / std::sqrt (1.0 - 2.0 * a * std::cos (w0) + a * a);
            const auto mMin = (double) stringRho / 0.99999;
            if (mag < mMin && mMin < 1.0)
            {
                const auto m2 = mMin * mMin, A = 1.0 - m2, Bh = 1.0 - m2 * std::cos (w0);
                a = (Bh - std::sqrt (juce::jmax (0.0, Bh * Bh - A * A))) / A;
                mag = mMin;
            }
            else if (mMin >= 1.0)
                a = 0.0, mag = 1.0;
            stringLpA = (float) a;
            stringRho = (float) juce::jmin (0.99999, (double) stringRho / mag);
            stringLpTau = a > 0.0 ? std::atan2 (a * std::sin (w0), 1.0 - a * std::cos (w0)) / w0 : 0.0;
        }
        stringPickupPos = params.stringPickup->load();

        // Buzz (puente de sitar, solo Noise Burst y Pick): profundidad del perfil del puente al final del tramo en diferencias
        // finitas, del 2,5 % (Buzz mínimo) al 0,5 % (Buzz 100 %) de la amplitud de la excitación (0,8). Medido en el prototipo
        // (tramo del 1 %): 3 % apenas cambia el timbre (centroide 457 -> 397 Hz) y 1 % da un zumbido estable (~950 Hz)
        buzzDepth = 0.8f * (0.025f - 0.02f * params.stringBuzz->load());

        // Stiff y Tension (modificadores de Noise Burst, Pick, Bow, Hammer y E-Bow; a 0, el lazo es el de siempre, bit a bit).
        // Stiff: inarmonicidad B = 10^(-5 + 2·Stiff), de 1e-5 a 1e-3 (pianos: de ~1e-5 en el centro a 1e-3 en los agudos y
        // los bordones; Rauhala y Välimäki, fig. 1); el coeficiente se resuelve al cambiar B o si la nota se mueve más de un 3 %
        const auto stringModel = stringMode <= excEbow;
        const auto stiff = params.stringStiff->load();
        stiffOn = stringModel && stiff > 0.0f;
        stiffTau = 0.0;
        if (stiffOn)
        {
            const auto B = std::pow (10.0, -5.0 + 2.0 * (double) stiff);
            if (std::abs (fS / juce::jmax (1.0, stiffSolvedF) - 1.0) > 0.03 || B != stiffSolvedB)
            {
                stiffA = (float) stiffSolve (fS, B, sampleRate);
                stiffSolvedF = fS;
                stiffSolvedB = B;
            }
            stiffTau = -(double) stiffStages * stiffPhase ((double) stiffA, w0) / w0;
        }
        // Tension: el lazo dura N/(1 + δ), δ = Tension·0,06·velocidad²·P/P_máx, con P la potencia integrada y P_máx su
        // máximo desde el inicio de la nota: a 100 % y velocidad 127 el ataque sube un 6 % (+101 ct) con cualquier
        // excitación, y el tono baja a medida que la cuerda se apaga. Adaptado: el artículo usa la potencia absoluta
        // (g_p = 10 con su lazo de 90 muestras y una amplitud de 1); aquí, con la absoluta, el triángulo de Pick (Position
        // 13 %) daba +301 ct al 30 % (medido) y el ruido de Noise Burst aún más. Integración: un polo en la frecuencia de
        // la nota (estimado: el artículo no da a_p; deja pasar la oscilación de 2f a -7 dB, la que crea los armónicos que faltan)
        const auto tension = params.stringTension->load();
        tensionOn = stringModel && tension > 0.0f;
        if (tensionOn)
        {
            tensionGain = (double) tension * 0.06 * (double) noteVelocity * (double) noteVelocity;
            tensionPole = std::exp (-w0);
        }

        // Guía de dos tramos (Bow, Hammer, E-Bow): pérdidas concentradas en el puente con un paso bajo de un polo
        // (1 - p)·k / (1 - p·z^-1); p desde Brightness (0,95 a 0 %, 0 a 100 %; el polo fijo de STK, 0,658 a 48 kHz, equivale a
        // 31 %) y k tal que |H(ω0)| = g, la misma caída por periodo que Decay da a la cuerda pulsada (k <= 0,99999 en
        // continua). Su retardo de fase a la fundamental se descuenta de la guía: afinación exacta en todo el teclado
        if (stringMode == excModal)
        {
            // Modal: Decay escala todas las τ (x1 con el valor por defecto, 3 s); las τ no dependen de la nota. Un modo por
            // encima de 0,45·fs calla (su senoide se reflejaría por debajo de Nyquist)
            const auto scale = (double) params.stringDecay->load() / 3.0;
            for (int m = 0; m < modalCount; ++m)
            {
                modalMul[(size_t) m] = (float) std::exp (-1.0 / ((double) modalTau[(size_t) m] * scale * sampleRate));
                modalAudible[(size_t) m] = modalRatio[(size_t) m] * frequency < 0.45 * sampleRate;
            }
        }
        else if (stringMode == excMallet)
        {
            // Mallet: cada modo es un oscilador de masa 1 con fuerza, discretizado como SDT (SDTResonators.c, updateMode):
            // r = e^(-dt/τ), b1 = r·sinc(ωdt)·dt², a1 = -2r·cos(ωdt), a2 = r², v = b0v·p + b1v·p0. τ de la receta, por Decay/3 s
            // como Modal. Si ω cambia (bend, vibrato), la posición se escala por ω_antes/ω (SDT: energía constante) y la
            // anterior se recalcula desde la velocidad. Un modo por encima de 0,45·fs calla
            const auto dt = 1.0 / sampleRate;
            const auto scale = (double) params.stringDecay->load() / 3.0;
            for (int k = 0; k < malletCount; ++k)
            {
                auto& m = malletModes[(size_t) k];
                const auto w = juce::MathConstants<double>::twoPi * m.ratio * frequency;
                if (w * dt >= std::acos (-0.9995) || w > juce::MathConstants<double>::twoPi * 0.45 * sampleRate)
                {
                    m.b1 = m.a1 = m.a2 = m.b0v = m.b1v = 0.0;
                    m.p0 = m.p1 = m.v = 0.0;
                    m.omega = 0.0;
                    continue;
                }
                const auto damp = 1.0 / juce::jmax (1.0e-4, m.tau * scale), r = std::exp (-damp * dt);
                const auto wt = w * dt, sinc = std::sin (wt) / wt, tsinc = sinc * dt;
                m.b1 = r * sinc * dt * dt;
                m.a1 = -2.0 * r * std::cos (wt);
                m.a2 = r * r;
                m.b0v = std::cos (wt) / tsinc - damp;
                m.b1v = -r / tsinc;
                if (m.omega > 0.0 && m.omega != w)
                    m.p0 *= m.omega / w;
                m.p1 = (m.v - m.b0v * m.p0) / m.b1v;
                m.omega = w;
            }
            // Baqueta (Hunt-Crossley; Avanzini y Rocchesso, DAFx 2001): f = k·x^α·(1 + μ·ẋ), α = 2,8. Hardness fija el tiempo de
            // contacto tc contra una superficie rígida a velocidad 1, de 10 ms a 0,3 ms (logarítmico; al menos 8 muestras: el
            // esquema explícito necesita varias muestras de contacto), y de ahí k: compresión máxima x_m = tc/(2·I), con
            // I = ∫0^1 (1 - ξ^(α+1))^(-1/2) dξ = √π·Γ(1 + 1/(α+1))/Γ(1/2 + 1/(α+1)) (choque de Hertz), y
            // k = (α+1)·m/(2·x_m^(α+1)): Hardness no depende de Mass. Mass, de 0,01 a 10 veces la masa modal (con 0,3 o más,
            // rebota varias veces y apaga los modos); Damp, μ de 0 a 1,5 (restitución ≈ 1 - 2/3·μ·v)
            constexpr double alpha = 2.8;
            static const double integral = std::sqrt (juce::MathConstants<double>::pi) * std::tgamma (1.0 + 1.0 / (alpha + 1.0))
                                           / std::tgamma (0.5 + 1.0 / (alpha + 1.0));
            const auto tc = juce::jmax (8.0 * dt, 0.01 * std::pow (0.03, (double) params.malletHardness->load()));
            const auto xm = tc / (2.0 * integral);
            malletMass = 0.01 * std::pow (1000.0, (double) params.malletMass->load());
            malletK = (alpha + 1.0) * malletMass / (2.0 * std::pow (xm, alpha + 1.0));
            malletMu = 1.5 * (double) params.malletDamp->load();
            // Salida: Σ A_k·ω_k·p_k / (2·m) (con un golpe ideal, impulso 2·m·v, cada modo suena con la amplitud A_k de la
            // receta a velocidad 1), por la normalización de la receta y el nivel calibrado
            malletScale = (double) malletNorm * malletOutputGain / (2.0 * malletMass);
        }
        else if (stringMode == excBanded)
        {
            // Banded (STK BandedWG, Essl y Cook 1999): paso banda de radio 1 - π·32/fs (32 Hz de ancho) en cada modo. Adaptado:
            //  - Retardo fraccionario d = fs/(r·f) - 1 - τ, con τ el retardo de fase del paso banda en su centro (STK: entero,
            //    (int)(fs/(r·f)), y 1 muestra más por leer lastOut: el modo cae fuera del centro y pierde ganancia; prototipo a
            //    48 kHz, cuenco en C6: +12,5 ct en STK, -0,1 ct así).
            //  - Ganancia por vuelta g^(3 s/Decay), con g <= 0,9995 (el cuenco tiene modos de g = 1: con el modo centrado no
            //    caerían nunca), dividida por |BP(centro)| (STK: ~1, menos en graves por los ceros en 0 y fs/2).
            //  - Sin el límite de STK de 1568 Hz: un modo calla si pasa de 0,45·fs o si su retardo baja de 1 muestra
            windRateScale = (float) (44100.0 / sampleRate);
            const auto radius = 1.0 - juce::MathConstants<double>::pi * 32.0 / sampleRate;
            const auto decayScale = 3.0 / juce::jmax (0.05, (double) params.stringDecay->load());
            bandActive = 0;
            for (int k = 0; k < bandCount; ++k)
            {
                auto& m = bandModes[(size_t) k];
                const auto fm = frequency * (double) m.ratio;
                m.on = bandModeOn (fm, sampleRate);
                if (! m.on)
                    continue;
                ++bandActive;
                const auto th = juce::MathConstants<double>::twoPi * fm / sampleRate;
                const auto a1 = -2.0 * radius * std::cos (th), a2 = radius * radius, b0 = 0.5 - 0.5 * a2;
                const auto z = std::polar (1.0, -th);
                const auto h = b0 * (1.0 - z * z) / (1.0 + a1 * z + a2 * z * z);
                m.a1 = (float) a1;
                m.a2 = (float) a2;
                m.b0 = (float) b0;
                m.tau = -std::arg (h) / th;
                m.g = (float) juce::jmin (1.0, std::pow (juce::jmin (0.9995, (double) m.gain0), decayScale) / std::abs (h));
            }
            bandActive = juce::jmax (1, bandActive);
            bandSlope = 10.0f - 9.0f * params.stringBowPressure->load(); // STK: Bow Pressure, slope = 10 - 9·presión
        }
        else if (stringMode == excShaker)
        {
            // Shaker (STK Shakers, PhISEM de Cook 1997). Las constantes de STK son por muestra a 44,1 kHz; adaptado: caídas,
            // probabilidad de choque y radios se escalan para durar lo mismo a cualquier frecuencia de muestreo, y las
            // resonancias siguen la nota (C4 = el instrumento de STK) por Tone, x4^(Tone - 0,5) (STK: ModWheel)
            const auto& kind = shakerKinds()[(size_t) shakerKind];
            const auto rate = 44100.0 / sampleRate;
            const auto shake = (double) params.shakerShake->load(); // STK ModFrequency: decaimiento del sistema
            shakeSys = (float) std::pow ((double) kind.systemDecay + 2.0 * (shake - 0.5) * (double) kind.decayScale
                                                                     * (1.0 - (double) kind.systemDecay), rate);
            shakeSnd = (float) std::pow ((double) kind.soundDecay, rate);
            // Objects: el número de piezas x1/4 a x4 (STK FootControl: 2·n·base + 1,1); al menos 1,1 (ln N > 0)
            const auto objects = juce::jmax (1.1, (double) kind.objects * std::pow (4.0, 2.0 * (double) params.shakerObjects->load() - 1.0));
            shakeChance = (float) (objects * rate);
            shakeGain = (float) (std::log (objects) * (double) kind.gain / objects); // STK: currentGain = ln(N)·baseGain/N
            const auto tone = frequency / 261.63 * std::pow (4.0, (double) params.shakerTone->load() - 0.5);
            for (int i = 0; i < kind.count; ++i)
            {
                shakeHz[(size_t) i] = (float) juce::jmin (0.45 * sampleRate, (double) kind.freq[i] * tone);
                shakeR[(size_t) i] = (float) std::pow ((double) kind.radius[i], rate);
                if (! kind.varies[i] || shakeFresh) // las que varían toman su frecuencia en cada choque (renderShaker)
                    shakeA1[(size_t) i] = -2.0f * shakeR[(size_t) i]
                                          * std::cos (juce::MathConstants<float>::twoPi * shakeHz[(size_t) i] / (float) sampleRate);
            }
            shakeFresh = false;
        }
        else if (isWindMode (stringMode))
        {
            // Viento (STK Clarinet, Flute, Brass; Cook y Scavone): sus tasas de envolvente son por muestra a 44,1 kHz (la
            // frecuencia por defecto de STK); se escalan para durar lo mismo a cualquier frecuencia de muestreo
            windRateScale = (float) (44100.0 / sampleRate);
            // Reed: Guillemain, Kergomard y Voinier (JASA 118, 2005). Pérdidas del tubo cilíndrico (R = 7 mm) en un filtro
            // b0 / (1 - a1·z^-1) ajustado a las dos primeras resonancias, ecuaciones (15) y (16) con
            // |F(w)²|² = exp(-2·α·c·sqrt(w/2)·L), α = 2/(R·c^1,5)·(sqrt(lv) + (Cp/Cv - 1)·sqrt(lt)), L = c/(4·f)
            {
                constexpr double sound = 340.0, radius = 7.0e-3; // c (m/s) y radio del artículo
                const auto alpha = 2.0 / (radius * std::pow (sound, 1.5)) * (std::sqrt (4.0e-8) + 0.4 * std::sqrt (5.6e-8));
                const auto len = sound / (4.0 * frequency);
                const auto loss = [&] (double w) { return std::exp (-2.0 * alpha * sound * std::sqrt (0.5 * w) * len); };
                const auto w1 = sound * juce::MathConstants<double>::pi * 0.5 / len, w2 = 3.0 * w1;
                const auto F1 = loss (w1), F2 = loss (w2);
                const auto c1 = std::cos (w1 / sampleRate), c2 = std::cos (w2 / sampleRate);
                const auto A12 = F1 * c1 - F2 * c2, F12 = F1 - F2;
                const auto root = std::sqrt (juce::jmax (0.0, A12 * A12 - F12 * F12));
                const auto a1 = (A12 - root) / F12;
                reedA1 = (float) a1;
                reedB0 = (float) (std::sqrt (juce::jmax (0.0, 2.0 * F1 * F2 * (c1 - c2) * (A12 - root))) / F12); // (16)
                reedTauLoss = std::atan2 (a1 * std::sin (w0), 1.0 - a1 * std::cos (w0)) / w0;
            }
            // Embocadura ζ de 0,15 a 0,45 (el artículo: 0,2-0,6 en un clarinete; 0,25 por defecto, el mejor ajuste)
            reedZeta = 0.15f + 0.3f * params.reedEmbouchure->load();
            // Presión γ por velocidad: 0,34 (pp) a 0,45 (ff), ajustado a las grabaciones de Iowa MIS en C4 (pp, mf, ff). Por
            // encima de 300-380 Hz, al menos 0,40: con γ bajo el modelo salta al tercer modo (medido en G4 y C5)
            const auto floorGamma = 0.34 + 0.06 * juce::jlimit (0.0, 1.0, (frequency - 300.0) / 80.0);
            reedGamma = (float) juce::jmax (floorGamma, 0.34 + 0.11 * (double) noteVelocity);
            // Ruido de soplo del clarinete sobre γ: ±5 %·Noise (2,5 % por defecto; γ está cerca del umbral, 1/3: un 20 % como el
            // de STK lo apagaría a ratos)
            reedNoise = 0.05f * params.reedNoise->load();
            {
                // Caña de GKV (19): a0a = fe²/ωr² + fe·qr/(2ωr), b1a = 1/a0a, a1a = (2fe²/ωr² - 1)/a0a,
                // a2a = (fe·qr/(2ωr) - fe²/ωr²)/a0a, con fr = 2205 Hz y qr = 0,6 (ver renderWind)
                const auto wr = juce::MathConstants<double>::twoPi * 2205.0, qr = 0.6, fe = sampleRate;
                const auto k2 = fe * fe / (wr * wr), kq = fe * qr / (2.0 * wr), a0a = k2 + kq;
                reedB1 = (float) (1.0 / a0a);
                reedXa1 = (float) ((2.0 * k2 - 1.0) / a0a);
                reedXa2 = (float) ((kq - k2) / a0a);
            }
            reedLpA = (float) std::exp (-juce::MathConstants<double>::twoPi * 1500.0 / sampleRate);
            fluteTiltA = (float) std::exp (-juce::MathConstants<double>::twoPi * 1000.0 / sampleRate);
            brassRadA = reedLpA; // también 1,5 kHz
            // Afinación (medida en el prototipo): la caña alarga el tubo, -0,062 ct/Hz (-16 ct en C4, -34 en C5): una longitud
            // fija de 1,8·10^-5 s (0,86 muestras a 48 kHz). La flauta abierta, -25 ct en C4: se ajusta midiendo en el plugin
            reedTuneSamples = 1.8e-5 * sampleRate;
            fluteTuneSamples = 0.0;
            // Nivel a lo largo del teclado: la presión radiada (derivada en Reed, paso alto de la campana en Brass) crece con la
            // nota: medido, RMS +6,4 dB/octava (Reed) y +7 dB/octava (Brass) de C2 a C6, ∝ f^1,0 y f^1,15. Se compensa con
            // (261,63 Hz / f)^k para un nivel tocable en todo el teclado (un instrumento real es más flojo en graves), x1/16 a x16
            const auto k = stringMode == excReed ? 1.0 : stringMode == excBrass ? 1.15 : 0.0;
            windLevel = (float) juce::jlimit (1.0 / 16.0, 16.0, std::pow (261.63 / frequency, k));
            fluteNoise = 0.4f * params.fluteNoise->load();             // STK: noiseGain = 0,4·Noise (0,15 por defecto)
            fluteJetRatio = 0.08f + 0.48f * params.fluteJet->load();   // STK: jetRatio = 0,08 + 0,48·Jet Delay (0,32 por defecto)
            // Retardos de fase a la nota (w0) del paso bajo de un polo de la flauta (polo 0,7 - 0,1·22050/fs, STK) y del bloqueo
            // de DC (1 - z^-1)/(1 - 0,99·z^-1) de flauta y metal (negativo: adelanta la fase, más en graves)
            flutePole = (float) (0.7 - 0.1 * 22050.0 / sampleRate);
            const auto p = (double) flutePole;
            fluteTauLp = std::atan2 (p * std::sin (w0), 1.0 - p * std::cos (w0)) / w0;
            const auto dcPhase = std::arg (std::complex<double> (1.0 - std::cos (w0), std::sin (w0))
                                           / std::complex<double> (1.0 - 0.99 * std::cos (w0), 0.99 * std::sin (w0)));
            windTauDc = -dcPhase / w0;
            // Metal: resonador del labio en 0,965·f·4^(2·Lip - 1) (STK Brass, Lip Tension, con la sintonía de abajo). Adaptado
            // (derivación y medida en docs/engines/synth.md):
            //  - Q constante = 15 (STK: radio 0,997 fijo, un ancho de banda de 46 Hz: en graves el labio apenas es selectivo y
            //    arrastra el tono hasta +110 ct; con Q 15, de C2 a C6 queda en +22..+58 ct, casi constante).
            //  - Apertura estática constante: la ganancia del labio g hace g·M/(1 - 2r·cos ω + r²) = 0,7 con M = 0,24 (soplo
            //    de velocidad 0,8). STK fija g = 0,03, y como 1 - 2r·cos ω + r² ≈ r·ω², bajo ≈ 650 Hz de labio (a 48 kHz) el
            //    soplo deja el labio abierto del todo (área saturada en 1, da/dl = 0) y no oscila: medido, mudo de C2 a C4.
            //  - Labio a 0,965·f (-62 ct): el labio que se abre hacia fuera suena por encima de sus resonancias; medido en
            //    el prototipo, de C4 a C6 ±3 ct y de C2 a C3 ±21 ct (STK: +16 ct en C5, +27 ct en C6 y mudo debajo).
            const auto lipHz = juce::jlimit (1.0, 0.45 * sampleRate,
                                             0.965 * frequency * std::pow (4.0, 2.0 * params.brassLip->load() - 1.0));
            const auto wl = juce::MathConstants<double>::twoPi * lipHz / sampleRate;
            const auto r = std::exp (-juce::MathConstants<double>::pi * lipHz / (15.0 * sampleRate));
            brassA1 = (float) (-2.0 * r * std::cos (wl));
            brassA2 = (float) (r * r);
            brassLipGain = (float) (0.7 * (1.0 - 2.0 * r * std::cos (wl) + r * r) / 0.24);
            brassSlide = 0.5f + params.brassSlide->load(); // STK Slide Length: el tubo de 0,5 a 1,5 veces
        }
        else if (isGuideMode (stringMode))
        {
            const auto p = 0.95 * (1.0 - (double) params.stringBright->load());
            const auto mag = (1.0 - p) / std::sqrt (1.0 - 2.0 * p * std::cos (w0) + p * p);
            guideP = (float) p;
            guideB0 = (float) ((1.0 - p) * juce::jmin (0.99999, g / mag));
            guideTau = std::atan2 (p * std::sin (w0), 1.0 - p * std::cos (w0)) / w0;
            // Posición del arco, el martillo o la bobina; con Harmonic, relativa al tramo que vibra (1/n de la cuerda)
            guideBeta = juce::jlimit (0.02f, 0.5f, params.stringPick->load() * (float) stringHarm);
            bowSlope = 5.0f - 4.0f * params.stringBowPressure->load();                 // Bow: slope = 5 - 4·presión (STK Bowed)
            hammerK = std::pow (10.0f, 6.0f + 4.0f * params.stringHammerFelt->load()); // Hammer: rigidez del fieltro (renderGuide)
            ebowGain = 0.5f + 1.5f * params.stringEbowDrive->load();                    // E-Bow: fuerza del electroimán
            bowVelTarget = stringGate ? 0.03f + 0.2f * noteVelocity : 0.0f; // STK: maxVelocity = 0,03 + 0,2·amplitud
            bowAttackCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate)); // el arco acelera en 20 ms (estimado)
        }
    }

public:
    // Diálogo Formula...: la fuente Math Models sola (sin Level, envolvente, filtro ni efectos) con los parámetros actuales, nota
    // f0 a velocidad velocity; la tecla se suelta en la muestra gateSamples (Bow y E-Bow). Solo en una voz propia del diálogo
    // (con su frecuencia de muestreo), nunca en una del sintetizador. Noise Burst usa siempre la misma semilla
    void renderMathPreview (float* out, int numSamples, double f0, float velocity, int gateSamples)
    {
        frequency = f0;
        noteVelocity = velocity;
        rng.setSeed (0x5EED);
        stringActive = false;
        startString (f0, false, true);
        for (int i = 0; i < numSamples; i += 32) // bloques de 32 muestras, como un búfer corto del anfitrión
        {
            if (i >= gateSamples) stringGate = false;
            updateStringParams();
            for (int j = i; j < juce::jmin (numSamples, i + 32); ++j)
                out[j] = renderString (f0 / getSampleRate());
        }
    }

private:
    // Cuerda pulsada: Karplus-Strong extendido (Jaffe y Smith 1983; ccrma.stanford.edu/~jos/pasp, EKS).
    //  Excitación (al pulsar): un periodo de ruido blanco -> pasa-bajos de nivel dinámico R = e^(-π·L/fs),
    //  L = 200 Hz·2^(7·Brightness) -> peine de posición de púa 1 - z^(-round(β·N)) -> normalizado a pico 0,8.
    //  Lazo: retardo leído con Hermite (afinación fraccionaria continua: pitch bend y vibrato sin saltos) + filtro
    //  (1 - S) + S·z^-1 (amortiguación, S muestras de retardo) × ρ. Retardo total = fs/f -> d = fs/f - 1 - S.
    //  S y ρ: ver renderNextBlock (ganancia por periodo a la fundamental para caer 60 dB en Decay segundos).
    void startString (double f0, bool retrigger = false, bool preview = false)
    {
        const auto wasGuide = stringActive && isGuideMode (stringMode);
        const auto oldMode = stringMode;
        const auto wasActive = stringActive;
        const auto oldHarm = stringHarm;
        stringActive = preview || params.stringLevel->load() > 0.0f; // la vista previa suena aunque Level esté a 0
        if (! stringActive)
            return;
        stringMode = (int) params.stringExcite->load();
        stringHarm = 1 + (int) params.stringHarmonic->load();
        stringGate = true;

        // Modal: copia la receta publicada (la de la nota no cambia si se edita mientras suena) y arranca los modos en fase 0
        if (stringMode == excModal)
        {
            stringHarm = 1; // Harmonic, Buzz y Position no actúan
            modalCount = 0;
            if (const auto* recipe = modalSlot.current.load (std::memory_order_acquire))
            {
                modalCount = recipe->count;
                for (int k = 0; k < modalCount; ++k)
                {
                    modalRatio[(size_t) k] = recipe->ratio[(size_t) k];
                    modalTau[(size_t) k] = recipe->tau[(size_t) k];
                    modalEnv[(size_t) k] = recipe->amp[(size_t) k] * recipe->norm;
                    modalPhase[(size_t) k] = 0.0;
                    modalMul[(size_t) k] = 0.0f; // se calcula en el bloque, antes de la primera muestra
                    modalAudible[(size_t) k] = false;
                }
            }
            modalAge = 0;
            modalFade = juce::jmax (1, (int) std::round (0.0015 * getSampleRate()));
            juce::ignoreUnused (f0);
            return;
        }

        // Mallet: los modos de la receta en reposo y la baqueta tocando la superficie a la velocidad de la nota. En un
        // redisparo de Mono/Legato con la receta sonando, los modos siguen y la baqueta vuelve a golpear
        if (stringMode == excMallet)
        {
            stringHarm = 1;
            const auto* recipe = modalSlot.current.load (std::memory_order_acquire);
            const auto keep = retrigger && wasActive && oldMode == stringMode && recipe != nullptr && recipe->count == malletCount;
            malletCount = recipe != nullptr ? recipe->count : 0;
            malletNorm = recipe != nullptr ? recipe->norm : 1.0f;
            for (int k = 0; k < malletCount; ++k)
            {
                auto& m = malletModes[(size_t) k];
                if (! keep)
                    m = {};
                m.ratio = recipe->ratio[(size_t) k];
                m.amp = recipe->amp[(size_t) k];
                m.tau = recipe->tau[(size_t) k];
            }
            auto surface = 0.0;
            for (int k = 0; k < malletCount; ++k)
                surface += malletModes[(size_t) k].p0;
            malletY = surface;
            // Velocidad de golpe 0,2 + 0,8·velocidad (como el suelo de Hammer): con v = velocidad, de 30 a 127 el nivel
            // cambiaba 26,5 dB (medido en A3: el contacto lento de una baqueta blanda atenúa también el modo 1)
            malletV = 0.2 + 0.8 * (double) noteVelocity;
            juce::ignoreUnused (f0);
            return;
        }

        // Banded: STK vacía las guías y los filtros en cada noteOn (setFrequency) y golpea (pluck) o empieza a frotar
        if (stringMode == excBanded)
        {
            stringHarm = 1; // Harmonic, Buzz y Position no actúan
            const auto sr = getSampleRate();
            const auto shape = juce::jlimit (0, 3, (int) params.bandedShape->load());
            const auto& preset = bandPresets()[(size_t) shape];
            bandCount = preset.count;
            bandBowing = (int) params.bandedExcite->load() == 1;
            bandGain = bandBowing ? bandBowGains[shape] : bandStrikeGains[shape];
            // Tramos: 2 periodos de cada modo + 4 (un bend de una octava abajo cabe); si no caben, se reducen en proporción
            auto need = 0.0;
            for (int k = 0; k < bandCount; ++k)
                need += 2.0 * sr / (f0 * (double) preset.ratio[k]) + 4.0;
            const auto fit = juce::jmin (1.0, (double) bandSize / need);
            auto base = 0;
            for (int k = 0; k < bandCount; ++k)
            {
                auto& m = bandModes[(size_t) k];
                m = {};
                m.ratio = preset.ratio[k];
                m.gain0 = preset.gain[k];
                m.excite = preset.excite[k];
                m.base = base;
                m.size = juce::jlimit (4, juce::jmax (4, bandSize - base), (int) ((2.0 * sr / (f0 * (double) m.ratio) + 4.0) * fit));
                base = juce::jmin (bandSize - 4, base + m.size);
            }
            std::fill (bandBuffer.begin(), bandBuffer.begin() + juce::jmax (bandUsed, base), 0.0f);
            bandUsed = base;
            // Golpe (STK pluck): cada guía recibe excitación·a/n durante floor(L/Lmin) muestras, con L el retardo del modo y
            // Lmin el del más agudo que suena. Al frotar, un golpe de 0,1·velocidad (el contacto del arco; adaptado: desde
            // reposo el arco de STK tarda 1,65 s en llegar a -6 dB de su nivel estable, así 0,95 s; prototipo, Tuned Bar en C4)
            auto active = 0;
            auto lMin = 1.0e9;
            for (int k = 0; k < bandCount; ++k)
                if (const auto fm = f0 * (double) bandModes[(size_t) k].ratio; bandModeOn (fm, sr))
                {
                    ++active;
                    lMin = juce::jmin (lMin, sr / fm);
                }
            const auto amp = bandBowing ? 0.1f * noteVelocity : noteVelocity;
            // Baqueta (adaptado; solo al golpear las barras, Uniform y Tuned Bar): cada modo pesa
            // ((1 + (f_k/fc)²) / (1 + (f_1/fc)²))^-4, un paso bajo de orden 8 normalizado al modo 1, con
            // fc = 2012 Hz·1,2306^((v - 80)/40), v = velocidad MIDI. Ajustado por mínimos cuadrados a los modos 4 y 10 de los golpes
            // de un vibráfono (Iowa MIS, C4-B4: mediana -25,8 y -83,9 dB del modo 1 en mf, -19,4 y -72,5 en ff; STK, -11,8 y
            // -25,2 a cualquier velocidad), suponiendo mf = velocidad 80 y ff = 120: fc 2012 Hz (mf) y 2476 Hz (ff); error
            // mediano de -0,2/+4,7 dB (mf) y -4,0/-0,3 dB (ff), y ±7-13 dB de una nota a otra (la posición del golpe frente a
            // los nodos de cada modo, que no se modela). STK llena todas las guías por igual: suena 13 dB más brillante en el modo 4
            const auto mallet = ! bandBowing && shape <= 1;
            const auto fc = 2012.0 * std::pow (2476.0 / 2012.0, (127.0 * (double) noteVelocity - 80.0) / 40.0);
            const auto ref1 = 1.0 + std::pow (f0 * (double) bandModes[0].ratio / fc, 2.0);
            for (int k = 0; k < bandCount && active > 0; ++k)
            {
                auto& m = bandModes[(size_t) k];
                const auto fm = f0 * (double) m.ratio;
                if (! bandModeOn (fm, sr))
                    continue;
                const auto weight = mallet ? (float) std::pow ((1.0 + (fm / fc) * (fm / fc)) / ref1, -4.0) : 1.0f;
                const auto v = m.excite * amp * weight / (float) active;
                for (int j = juce::jmin (m.size, (int) (sr / fm / lMin)); --j >= 0;)
                {
                    bandBuffer[(size_t) (m.base + m.write)] = v;
                    m.write = m.write + 1 == m.size ? 0 : m.write + 1;
                }
            }
            // Arco (STK startBowing): velocidad máxima 0,03 + 0,1·a, ataque a·0,001 por muestra, cae a 0,9 (ADSR de 5 ms; el
            // mismo ritmo que el decaimiento de Flute en windBreath, 20/fs por muestra) y, al soltar, (1 - 0,5)·0,005 por
            // muestra (STK noteOff con amplitud 0,5: no hay velocidad de soltar)
            windEnv = 0.0f;
            windStage = bandBowing ? windAttack : windIdle;
            windMaxP = 0.03f + 0.1f * noteVelocity; windTarget = 1.0f; windAttackRate = 0.001f * noteVelocity;
            windSustain = 0.9f; windReleaseRate = 0.0025f;
            return;
        }

        // Shaker: STK noteOn suma a·0,1 a la energía (máximo 1); en un redisparo de Mono/Legato la sacudida continúa
        if (stringMode == excShaker)
        {
            stringHarm = 1;
            const auto kind = juce::jlimit (0, (int) shakerKinds().size() - 1, (int) params.shakerType->load());
            if (! (retrigger && wasActive && oldMode == stringMode && shakerKind == kind))
            {
                shakeEnergy = shakeLevel = shakeEq1 = shakeEq2 = 0.0f;
                shakeY1.fill (0.0f);
                shakeY2.fill (0.0f);
                stringHp = stringHpPrev = 0.0f;
                stringHpCoef = 1.0f - juce::MathConstants<float>::twoPi * 20.0f / (float) getSampleRate();
            }
            shakerKind = kind;
            shakeEnergy = juce::jmin (1.0f, shakeEnergy + 0.1f * noteVelocity);
            shakeFresh = true;
            juce::ignoreUnused (f0);
            return;
        }

        // Viento: el tubo arranca en reposo y el soplo sube con la envolvente de STK (noteOn de cada instrumento, con
        // amplitud = velocidad). En un redisparo de Mono/Legato del mismo instrumento sonando, el tubo sigue (cambio de
        // digitación sin dejar de soplar) y el soplo vuelve a subir hacia el valor de la nota nueva
        if (isWindMode (stringMode))
        {
            stringHarm = 1; // Harmonic, Buzz y Position no actúan
            if (! (retrigger && wasActive && oldMode == stringMode && windStage != windIdle))
            {
                stringBuffer.fill (0.0f);
                guideBuffer.fill (0.0f);
                stringWrite = guideWrite = 0;
                windLastOut = windZ1 = windY1 = windY2 = windDcX = windDcY = brassRad1 = brassRad2 = 0.0f;
                windEnv = 0.0f;
                stringHp = stringHpPrev = 0.0f;
                stringHpCoef = 1.0f - juce::MathConstants<float>::twoPi * 20.0f / (float) getSampleRate();
            }
            const auto a = noteVelocity;
            windOutGain = a + 0.001f; // STK Flute: outputGain = amplitud + 0,001 (Brass no lo usa)
            switch (stringMode)
            {
                case excReed:  // γ sube en 0,5 ms (el "aumento brusco" del artículo, sección IV.E) y baja en 20 ms al soltar;
                               // la velocidad decide γ (reedGamma), no la envolvente. Tasas a 44,1 kHz como las de STK
                    windMaxP = 1.0f; windTarget = 1.0f; windAttackRate = 1.0f / (0.0005f * 44100.0f); windSustain = 1.0f;
                    windReleaseRate = 1.0f / (0.02f * 44100.0f); break;
                case excFlute: // Flute::noteOn: ADSR (5 ms, 10 ms, 0,8, 10 ms), ataque 0,02·a. Presión (1 + 0,3·a) / 0,8 (STK:
                               // 1,1 + 0,2·a): con más presión o un chorro de 1,95 periodos el modelo tiene dos regímenes y
                               // el ruido elige uno (medido: centroide 1,66 o 2,73·f0 según la semilla); así, uno solo
                    windMaxP = (1.0f + 0.3f * a) / 0.8f; windTarget = 1.0f; windAttackRate = 0.02f * a; windSustain = 0.8f;
                    windReleaseRate = 0.02f * a;
                    // Inclinación de timbre por velocidad, calibrada (no física): y + s·paso alto 1 kHz, s = -6,3 + 17,1·a
                    // (-0,9 a 8). El chorro polinómico de STK no aclara el timbre al soplar más; con s, centroide 1,14·f0 en pp,
                    // 2,81 en mf y 3,39 en ff frente a 1,09, 2,78 y 3,52 de una flauta (Iowa MIS, C4; prototipo)
                    fluteTilt = juce::jlimit (-0.9f, 8.0f, -6.3f + 17.1f * a);
                    break;
                default:       // Brass::noteOn: ADSR (5 ms, 1 ms, 1, 10 ms), ataque 0,001·a, maxPressure = a
                    windMaxP = a; windTarget = 1.0f; windAttackRate = 0.001f * a; windSustain = 1.0f;
                    windReleaseRate = 0.005f * a; break;
            }
            windStage = windAttack;
            juce::ignoreUnused (f0);
            return;
        }

        // Bow, Hammer, E-Bow: la cuerda arranca en reposo y la excitación actúa en renderGuide. En un redisparo de Mono/Legato
        // con la cuerda sonando no se vacía (un cambio de dedo con el arco apoyado; un martillo vuelve a golpear la cuerda que
        // ya vibra)
        if (isGuideMode (stringMode))
        {
            if (! (retrigger && wasGuide && oldHarm == stringHarm))
            {
                stringBuffer.fill (0.0f);
                guideBuffer.fill (0.0f);
                stringWrite = guideWrite = 0;
                bowVel = guideLp = hammerStringY = 0.0f;
                stiffX.fill (0.0f);
                stiffY.fill (0.0f);
                tensionState = tensionDelta = tensionPeak = 0.0;
                stringHp = stringHpPrev = 0.0f;
                if (std::exchange (stringPickupUsed, false)) pickupBuffer.fill (0.0f);
                stringHpCoef = 1.0f - juce::MathConstants<float>::twoPi * 20.0f / (float) getSampleRate();
                ebowSeed = true; // E-Bow: un ruido mínimo de partida (la bobina amplifica la vibración que ya hay)
            }
            hammerY = hammerStringY;                                 // el martillo parte tocando la cuerda
            hammerV = 0.5f + 4.5f * noteVelocity;                    // velocidad de impacto (escala propia)
            hammerAway = 0;
            juce::ignoreUnused (f0);
            return;
        }

        const auto sr = getSampleRate();
        const auto period = juce::jlimit (4, stringSize - 8, (int) std::round (sr / f0));
        const auto L = juce::jmin (0.45 * sr, 200.0 * std::exp2 (7.0 * params.stringBright->load()));
        const auto R = (float) std::exp (-juce::MathConstants<double>::pi * L / sr);
        const auto pick = juce::jlimit (1, period - 1, (int) std::round (params.stringPick->load() * period));

        std::array<float, stringSize> burst {};
        float lp = 0.0f, peak = 1.0e-9f;
        if ((int) params.stringExcite->load() == 1)
        {
            // Pick: desplazamiento inicial triangular con el vértice en la púa (armónicos ∝ sin(π·k·β)/k²); el paso bajo de
            // Brightness redondea el vértice, en las dos direcciones (sin retardo neto). No gasta números aleatorios
            for (int n = 0; n < period; ++n)
                burst[(size_t) n] = n < pick ? (float) n / (float) pick : (float) (period - n) / (float) (period - pick);
            for (int pass = 0; pass < 2; ++pass)
            {
                lp = burst[(size_t) (pass == 0 ? 0 : period - 1)];
                for (int i = 0; i < period; ++i)
                {
                    auto& b = burst[(size_t) (pass == 0 ? i : period - 1 - i)];
                    lp += (1.0f - R) * (b - lp);
                    b = lp;
                }
            }
        }
        else
        {
            for (int n = 0; n < period; ++n)
            {
                lp += (1.0f - R) * (2.0f * rng.nextFloat() - 1.0f - lp);
                burst[(size_t) n] = lp;
            }
            for (int n = period - 1; n >= pick; --n) // peine de posición de púa (de atrás adelante: usa valores sin modificar)
                burst[(size_t) n] -= burst[(size_t) (n - pick)];
        }
        float mean = 0.0f; // sin DC: el peine de púa no la anula en una ráfaga finita
        for (int n = 0; n < period; ++n)
            mean += burst[(size_t) n] / (float) period;
        for (int n = 0; n < period; ++n)
        {
            burst[(size_t) n] -= mean;
            peak = juce::jmax (peak, std::abs (burst[(size_t) n]));
        }

        stringBuffer.fill (0.0f);
        stringWrite = 0;
        // Buzz: tramo del puente en diferencias finitas, J puntos (1 % de la media longitud de onda, 2..256), en reposo. Un
        // puente de sitar mide del orden de 1 cm sobre unos 90 cm de cuerda (estimado). Medido en el prototipo: con un tramo del
        // 12 % el brillo caía de 3495 a 232 Hz en 0,5 s (un barrido de "disparo"); con el 1 %, estable. La excitación entra
        // entera en el lazo (lo que caería en el tramo, un 1 %, se pierde)
        stringBuzzOn = params.stringBuzz->load() > 0.0f;
        if (stringBuzzOn)
        {
            const auto halfPeriod = 0.5 * sr / (f0 * stringHarm);
            buzzJ = juce::jlimit (2, buzzMax, (int) std::round (0.01 * halfPeriod));
            std::fill (buzzY.begin(), buzzY.end(), 0.0f);
            std::fill (buzzYPrev.begin(), buzzYPrev.end(), 0.0f);
            buzzA = buzzBPrev = 0.0f;
        }
        if (stringHarm == 1)
        {
            for (int n = 0; n < period; ++n)
                pushString (0.8f * burst[(size_t) n] / peak);
        }
        else
        {
            // Armónico: un dedo en el nodo 1/n deja solo los modos k·n. En una cuerda ideal eso equivale a una cuerda n veces
            // más corta cuyo estado inicial es la media de los n desplazamientos de la cuerda entera (la media de n copias
            // desplazadas un periodo/n anula todo armónico que no sea múltiplo de n). Derivación propia; sin renormalizar: el
            // armónico pierde la energía de los modos que el dedo apaga, como en la cuerda real
            const auto sub = juce::jmax (4, (int) std::round ((double) period / stringHarm));
            for (int n = 0; n < sub; ++n)
            {
                float sum = 0.0f;
                for (int j = 0; j < stringHarm; ++j)
                    sum += burst[(size_t) ((n + (int) std::round ((double) j * period / stringHarm)) % period)];
                pushString (0.8f * sum / ((float) stringHarm * peak));
            }
        }
        stringPrev = stringHp = stringHpPrev = stringLp = 0.0f;
        stiffX.fill (0.0f);
        stiffY.fill (0.0f);
        tensionState = tensionDelta = tensionPeak = 0.0;
        if (std::exchange (stringPickupUsed, false)) pickupBuffer.fill (0.0f); // 32 KB: solo si la pastilla se usó
        stringHpCoef = 1.0f - juce::MathConstants<float>::twoPi * 20.0f / (float) sr;
    }

    void pushString (float x)
    {
        stringBuffer[(size_t) stringWrite] = x;
        stringWrite = (stringWrite + 1) & (stringSize - 1);
    }

    // Stiff: fase continua de un pasa todo (a + z^-1)/(1 + a·z^-1), φ(ω) = -ω + 2·atan(a·sin ω / (1 + a·cos ω)) (1 + a·cos ω > 0
    // con |a| < 1), y su retardo de grupo (1 - a²)/(1 + 2a·cos ω + a²)
    static double stiffPhase (double a, double w) { return -w + 2.0 * std::atan (a * std::sin (w) / (1.0 + a * std::cos (w))); }

    // Coeficiente de la cascada para una cuerda de frecuencia f0 e inarmonicidad B: el parcial k (12, o el último bajo 0,45·fs)
    // del lazo cae en k·f0·sqrt(1 + B·k²) (Fletcher 1962), con el retardo del lazo compensado en f0. Adaptado: Rauhala y
    // Välimäki dan una fórmula cerrada (ajustada a un piano, M = 8); medida en el prototipo (stiff_proto.py), acierta los parciales
    // 1-12 a ±1,8 ct hasta C4 con B <= 1e-4, pero con B = 1e-3 estira el 12 solo 80 de 116 ct y sobre C5 da a > 0 (los parciales
    // bajan: -27 ct). Aquí a se resuelve por bisección (el parcial sube al bajar a), y para cada a el parcial por Newton
    // protegido (Φ(ω) = ω·L - M·φ(ω), creciente, con L = N + M·φ(ω0)/ω0: el intervalo se estrecha y, si Newton se sale, se
    // biseca). Límite: el retardo de la cascada en la nota, como mucho el 80 % del periodo (L > 0). Prototipo (stiff_solve.py,
    // M = 16): parciales 1-24 a ±0,2-0,7 ct con B = 1e-5, ±6-8 ct con 1e-4 y ±97-100 ct con 1e-3 (de 394 ct de estiramiento
    // en k = 24). Coste: ~30 x 5 evaluaciones de fase, al pulsar o al cambiar B
    static double stiffSolve (double f0, double B, double sr)
    {
        constexpr auto twoPi = juce::MathConstants<double>::twoPi, pi = juce::MathConstants<double>::pi;
        const auto M = (double) stiffStages, n = sr / f0, w0 = twoPi / n;
        const auto k = (double) juce::jlimit (2, 12, (int) (0.45 * sr / f0));
        const auto target = k * f0 * std::sqrt (1.0 + B * k * k);
        const auto partial = [&] (double a) // frecuencia (Hz) del parcial k del lazo con coeficiente a
        {
            const auto L = n + M * stiffPhase (a, w0) / w0;
            auto wl = 0.0, wh = pi, w = juce::jmin (0.999 * pi, twoPi * k / n);
            for (int i = 0; i < 30; ++i)
            {
                const auto f = w * L - M * stiffPhase (a, w) - twoPi * k;
                if (std::abs (f) < 1.0e-10) // convergido (sin esta salida, con f = 0 el paso se rechazaba y saltaba al centro)
                    break;
                (f > 0.0 ? wh : wl) = w;
                const auto c = std::cos (w);
                const auto next = w - f / (L + M * (1.0 - a * a) / (1.0 + 2.0 * a * c + a * a));
                w = next > wl && next < wh ? next : 0.5 * (wl + wh);
                if (wh - wl < 1.0e-12)
                    break;
            }
            return w * sr / twoPi;
        };
        const auto delayAt = [&] (double a) { return -M * stiffPhase (a, w0) / w0; }; // retardo de la cascada en la nota
        auto lo = -0.999, hi = 0.0;
        if (delayAt (lo) > 0.8 * n)
        {
            auto a1 = lo, a2 = 0.0;
            for (int i = 0; i < 30; ++i)
            {
                const auto mid = 0.5 * (a1 + a2);
                (delayAt (mid) > 0.8 * n ? a1 : a2) = mid;
            }
            lo = a2;
        }
        if (partial (lo) < target)
            return lo; // ni la dispersión máxima permitida llega
        for (int i = 0; i < 30; ++i)
        {
            const auto mid = 0.5 * (lo + hi);
            (partial (mid) > target ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    float stiffProcess (float x)
    {
        for (int i = 0; i < stiffStages; ++i)
        {
            const auto y = stiffA * x + stiffX[(size_t) i] - stiffA * stiffY[(size_t) i];
            stiffX[(size_t) i] = x;
            stiffY[(size_t) i] = y;
            x = y;
        }
        return x;
    }

    // Tension (Välimäki, Tolonen y Karjalainen, ICASSP 1999, sección 3.2, lazo único): la elongación de la cuerda es una
    // potencia de las pendientes, P = Σ (s(k) + s(N-1-k))², sumando la muestra k del lazo con la simétrica, y se integra con un
    // polo antes de acortar el lazo. Aquí el lazo lleva desplazamiento: s(k) = b(k) - b(k+1). Suma dispersa (su sección 3.2):
    // una pareja de cada m (a lo sumo 32 parejas por muestra) y por m. En las guías (Bow, Hammer, E-Bow), ondas de velocidad
    // en dos tramos: adaptado, P = Σ v² de los dos (la energía que lleva la cuerda; sin el término cruzado de las parejas),
    // a lo sumo 64 muestras por muestra
    // length: muestras del lazo (cuerda pulsada) o del tramo de la cejuela (guías); bridge: las del tramo del puente (guías) o -1
    double tensionPower (int length, int bridge) const
    {
        const auto pairs = juce::jmax (1, length / 2);
        const auto m = juce::jmax (1, (pairs + 31) / 32);
        auto p = 0.0;
        if (bridge >= 0)
        {
            const auto mg = juce::jmax (1, (length + bridge + 63) / 64);
            for (int k = 0; k < length; k += mg)
            {
                const auto a = stringBuffer[(size_t) ((stringWrite - 1 - k) & (stringSize - 1))];
                p += (double) (a * a);
            }
            for (int k = 0; k < bridge; k += mg)
            {
                const auto b = guideBuffer[(size_t) ((guideWrite - 1 - k) & (stringSize - 1))];
                p += (double) (b * b);
            }
            return p * mg;
        }
        auto at = [this] (int k) { return stringBuffer[(size_t) ((stringWrite - 1 - k) & (stringSize - 1))]; };
        for (int k = 0; k < pairs; k += m)
        {
            const auto s = (at (k) - at (k + 1)) + (at (length - 2 - k) - at (length - 1 - k));
            p += (double) (s * s);
        }
        return p * m;
    }

    // Acortamiento relativo del lazo por la tensión de esta muestra (0 sin Tension)
    double tensionStep (int length, int bridge)
    {
        if (! tensionOn)
            return 0.0;
        const auto p = tensionPower (length, bridge);
        tensionState = p + tensionPole * (tensionState - p);
        tensionPeak = juce::jmax (tensionPeak, tensionState);
        return tensionDelta = tensionPeak > 1.0e-12 ? juce::jmin (0.5, tensionGain * tensionState / tensionPeak) : 0.0;
    }

    // Lectura con Hermite de una guía: valor escrito hace 'delay' muestras (>= 2) en un búfer circular de stringSize
    static float readGuide (const std::array<float, 8192>& buf, int write, double delay)
    {
        const auto k = delay - 1.0; // at(0) = la última escrita = hace 1 muestra (se lee antes de escribir)
        const auto i = (int) k;
        const auto fr = (float) (k - i);
        auto at = [&] (int n) { return buf[(size_t) ((write - 1 - n) & 8191)]; };
        const auto xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * fr + c2) * fr + c1) * fr + x0;
    }

    // Buzz: puente de sitar (jawari) según Siddiq (2012). La cuerda roza un puente curvo: los J puntos junto al puente se
    // calculan en diferencias finitas con Δx = c·Δt (su ec. 9: y(n+1) = y(x+1) + y(x-1) - y(n-1)) y un perfil que la cuerda no
    // puede atravesar, p(k) = -profundidad·(k/J)², 0 en el apoyo. Adaptaciones (docs/engines/synth.md):
    //  - Siddiq resuelve así toda la cuerda; aquí solo el tramo del puente: el resto es el lazo de siempre (Decay, Damping y la
    //    afinación fraccionaria; en diferencias finitas la longitud sería entera: -16 ct en A4). Unión con el lazo, derivada de la
    //    identidad de D'Alembert y = y+ + y- (Smith, PASP, apéndice E): la onda que sale hacia la cejuela es b(n) = y_J(n) - a(n),
    //    con a(n) la que vuelve del lazo; el vecino de fuera del tramo es y_J+1(n) = b(n-1) + a(n+1).
    //  - Al chocar se fija solo la posición nueva del punto (la de Siddiq pierde la energía cinética fijando también la
    //    anterior; medido en el prototipo, eso hace diverger el esquema en el modo de Nyquist).
    //  - Tramo sin pérdidas ni rigidez (Siddiq: sus ec. 11-13); las pérdidas están en el lazo.
    // Salida: fuerza sobre el puente, proporcional a y_1 - y_0 = y_1 (su sección 3.6)
    float renderPluckBuzz (double dt)
    {
        const auto j = buzzJ;
        const auto period = 1.0 / dt;
        const auto delta = tensionStep ((int) period - 2 * j, -1);
        const auto d = juce::jlimit (3.0, (double) stringSize - 8.0, period / (1.0 + delta) - 2.0 * j - stringS - stringLpTau - stiffTau);
        // a(n+1): la onda que salió hace D muestras, con las pérdidas del lazo y la inversión de la cejuela
        const auto r = readGuide (stringBuffer, stringWrite, d - 1.0);
        auto v = stringRho * ((1.0f - stringS) * r + stringS * stringPrev);
        stringPrev = r;
        if (stiffOn)
            v = stiffProcess (v);
        if (stringLpA > 0.0f)
        {
            stringLp += (1.0f - stringLpA) * (v - stringLp);
            v = stringLp;
        }
        const auto aNext = -v;
        const auto yOut = buzzBPrev + aNext; // y_J+1(n)
        std::array<float, buzzMax + 2> yNew;
        yNew[0] = 0.0f;
        for (int k = 1; k <= j; ++k)
        {
            const auto right = k == j ? yOut : buzzY[(size_t) (k + 1)];
            auto y = right + buzzY[(size_t) (k - 1)] - buzzYPrev[(size_t) k];
            const auto x = (float) k / (float) j;
            const auto floor = -buzzDepth * x * x; // perfil del puente
            yNew[(size_t) k] = y < floor ? floor : y;
        }
        const auto b = buzzY[(size_t) j] - buzzA; // b(n) = y_J(n) - a(n)
        pushString (b);
        buzzBPrev = b;
        buzzA = aNext;
        const auto out = buzzY[1];
        std::copy (buzzY.begin(), buzzY.begin() + j + 1, buzzYPrev.begin());
        std::copy (yNew.begin(), yNew.begin() + j + 1, buzzY.begin());
        return buzzOutputGain * out;
    }

    // Guía de dos tramos con la excitación entre ellos (ondas de velocidad; Smith 1986, estructura de Bowed de STK): tramo del
    // puente β·N (ida y vuelta, con las pérdidas en el puente) y tramo de la cejuela (1 - β)·N, N = fs/f - τ del paso bajo. Cada
    // extremo refleja con signo cambiado. En la unión entra v_nueva hacia los dos lados; la velocidad de la cuerda allí es
    // vs + v_nueva, con vs = suma de las dos ondas que llegan. Salida: la onda que llega al puente
    float renderGuide (double dt)
    {
        // Tension: la potencia se mide con los tramos sin acortar y acorta los dos en proporción; Stiff, en el puente
        const auto nominal = 1.0 / dt - guideTau - stiffTau;
        const auto delta = tensionOn ? tensionStep ((int) (nominal * (1.0 - (double) guideBeta)), (int) (nominal * (double) guideBeta)) : 0.0;
        const auto total = juce::jlimit (4.0, (double) stringSize - 8.0, nominal / (1.0 + delta));
        const auto bridgeLen = juce::jlimit (2.0, total - 2.0, total * (double) guideBeta);
        const auto bridgeOut = readGuide (guideBuffer, guideWrite, bridgeLen);
        const auto neckOut = readGuide (stringBuffer, stringWrite, total - bridgeLen);
        guideLp = guideB0 * bridgeOut + guideP * guideLp;
        const auto bridgeRefl = -(stiffOn ? stiffProcess (guideLp) : guideLp), nutRefl = -neckOut;
        const auto vs = bridgeRefl + nutRefl;
        auto newVel = 0.0f;
        auto gain = 1.0f;
        if (stringMode == excBow)
        {
            // McIntyre, Schumacher y Woodhouse (1983): fricción del arco. Δv = v_arco - vs; v_nueva = Δv·T(Δv),
            // T(x) = min(0,98, max(0,01, (|slope·x| + 0,75)^-4)) (BowTable de STK): con Δv pequeña el arco agarra la cuerda,
            // con Δv grande resbala; de ahí el ciclo de Helmholtz
            // Al soltar la tecla el arco se levanta en ese instante (sin contacto, la cuerda vibra libre con su Decay). Medido:
            // con una rampa de velocidad y el arco apoyado, la cuerda se frenaba (-38 dB en 0,6 s en vez de -12 dB con Decay 3 s)
            bowVel = stringGate ? bowVel + bowAttackCoef * (bowVelTarget - bowVel) : 0.0f;
            if (bowVel > 1.0e-5f)
            {
                const auto dv = bowVel - vs;
                const auto t = std::abs (dv * bowSlope) + 0.75f;
                newVel = dv * juce::jlimit (0.01f, 0.98f, 1.0f / (t * t * t * t));
            }
            gain = bowOutputGain;
        }
        else if (stringMode == excHammer)
        {
            // Hall (1992): fieltro como muelle no lineal, F = K·x^p (x = compresión), p = 2,5 (Russell: 2,2-3,5 en martillos
            // usados, 2-3 el rango preferido). Martillo de masa m = 0,001 contra una cuerda de impedancia Z = 1 a cada lado
            // (escala propia): v_nueva = F/(2Z). Euler explícito; con K <= 1e10 es estable a 44,1 kHz (1e12 diverge).
            // Contacto medido en el prototipo a velocidad 0,8: 5,2 ms (K = 1e6) a 1,5 ms (K = 1e10), más corto cuanto más
            // fuerte el golpe, como en el piano. Tras 20 ms sin contacto y alejándose, el martillo deja de calcularse
            if (hammerAway < hammerAwayMax)
            {
                const auto ts = (float) (1.0 / getSampleRate());
                const auto x = hammerY - hammerStringY;
                const auto force = x > 0.0f ? hammerK * std::pow (x, 2.5f) : 0.0f;
                newVel = 0.5f * force;
                hammerV -= force / hammerMass * ts;
                hammerY += hammerV * ts;
                hammerStringY += (vs + newVel) * ts;
                hammerAway = force > 0.0f || hammerV > 0.0f ? 0 : hammerAway + 1;
            }
            gain = hammerOutputGain;
        }
        else // E-Bow
        {
            // Patente de Heet (1978): una bobina capta la vibración y otra empuja la cuerda con ella, amplificada. Modelo
            // propio: v_nueva = 0,05·G·0,1·tanh(vs / 0,1) (amplificador que satura) mientras se mantiene la tecla. Arranca de
            // un ruido mínimo y crece: en el prototipo, 50 % del nivel final en 810 ms (G mínima) a 350 ms (G máxima)
            if (std::exchange (ebowSeed, false))
                newVel = 1.0e-4f * (2.0f * rng.nextFloat() - 1.0f);
            if (stringGate)
                newVel += 0.005f * ebowGain * std::tanh (vs * 10.0f);
            gain = ebowOutputGain;
        }
        stringBuffer[(size_t) stringWrite] = bridgeRefl + newVel; // hacia la cejuela
        stringWrite = (stringWrite + 1) & (stringSize - 1);
        guideBuffer[(size_t) guideWrite] = nutRefl + newVel;       // hacia el puente
        guideWrite = (guideWrite + 1) & (stringSize - 1);
        return gain * bridgeOut;
    }

    // Math Models > Modal (Adrien 1991): Σ A_k·e^(-t/τ_k)·sin(2π·r_k·f·t) por acumuladores de fase (siguen el bend, el vibrato y
    // la deriva muestra a muestra) y caídas multiplicativas; entrada lineal de 1,5 ms. Sin la salida de 12 ms de los WAV
    // renderizados (era el final del fichero) y sin bloqueo de continua: así en C4 coincide con el sample que sustituye
    float renderModal (double dt)
    {
        auto s = 0.0f;
        for (int k = 0; k < modalCount; ++k)
        {
            auto& ph = modalPhase[(size_t) k];
            ph += (double) modalRatio[(size_t) k] * dt;
            ph -= std::floor (ph);
            if (modalAudible[(size_t) k])
                s += modalEnv[(size_t) k] * (float) std::sin (juce::MathConstants<double>::twoPi * ph);
            modalEnv[(size_t) k] *= modalMul[(size_t) k];
        }
        const auto w = modalAge < modalFade ? (float) modalAge / (float) modalFade : 1.0f;
        ++modalAge;
        return s * w;
    }

    static constexpr int bandMaxModes = 12;
    struct BandPreset { int count; float ratio[bandMaxModes], gain[bandMaxModes], excite[bandMaxModes]; };
    // Shaker (renderShaker): instrumento de STK Shakers con hasta 5 resonancias
    struct ShakerKind
    {
        float objects, gain, soundDecay, systemDecay, decayScale, vary;
        int count;
        float freq[5], radius[5], level[5];
        bool varies[5];
        float eq[3];
    };
    // Math Models > Mallet (Avanzini y Rocchesso 2001; esquema de SDT, SDTInteractor_dsp): la fuerza se calcula con las posiciones
    // y velocidades de la muestra anterior (explícita, sin iterar), empuja los modos (+f, todos con ganancia 1 en el punto de
    // golpe) y frena la baqueta (-f/m). La superficie es Σ p_k; la compresión x = y_baqueta - superficie
    float renderMallet()
    {
        const auto dt = 1.0 / getSampleRate();
        auto surface = 0.0, surfaceV = 0.0;
        for (int k = 0; k < malletCount; ++k)
        {
            surface += malletModes[(size_t) k].p0;
            surfaceV += malletModes[(size_t) k].v;
        }
        auto force = 0.0;
        if (const auto x = malletY - surface; x > 0.0)
        {
            force = malletK * std::pow (x, 2.8) * (1.0 + malletMu * (malletV - surfaceV));
            force = std::isfinite (force) ? juce::jmax (0.0, force) : 0.0; // nunca atrae (Hunt-Crossley: f >= 0 al rebotar)
        }
        malletV -= force / malletMass * dt;
        malletY += malletV * dt;
        auto out = 0.0;
        for (int k = 0; k < malletCount; ++k)
        {
            auto& m = malletModes[(size_t) k];
            if (m.omega <= 0.0)
                continue;
            const auto p = m.b1 * force - m.a1 * m.p0 - m.a2 * m.p1;
            m.v = m.b0v * p + m.b1v * m.p0;
            m.p1 = m.p0;
            m.p0 = p;
            out += m.amp * m.omega * p;
        }
        return (float) (out * malletScale);
    }

    // Presets de STK BandedWG::setPreset (Essl; MIT-STK): relación de cada modo, ganancia por vuelta y excitación.
    // Uniform Bar 0,9^(k+1); Tuned Bar y Glass Harmonica 0,999^(k+1); Tibetan Bowl, los 12 modos medidos (ICMC 2002)
    static const std::array<BandPreset, 4>& bandPresets()
    {
        static const std::array<BandPreset, 4> presets {{
            { 4, { 1.0f, 2.756f, 5.404f, 8.933f }, { 0.9f, 0.81f, 0.729f, 0.6561f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { 4, { 1.0f, 4.0198391420f, 10.7184986595f, 18.0697050938f }, { 0.999f, 0.998001f, 0.997003f, 0.996006f },
              { 1.0f, 1.0f, 1.0f, 1.0f } },
            { 5, { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f }, { 0.999f, 0.998001f, 0.997003f, 0.996006f, 0.995010f },
              { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } },
            { 12, { 0.996108344f, 1.0038916562f, 2.979178f, 2.99329767f, 5.704452f, 5.704452f, 8.9982f, 9.01549726f, 12.83303f,
                    12.807382f, 17.2808219f, 21.97602739726f },
              { 0.999925960128219f, 0.999925960128219f, 0.999982774366897f, 0.999982774366897f, 1.0f, 1.0f, 1.0f, 1.0f,
                0.999965497558225f, 0.999965497558225f, 1.0f, 1.0f },
              { 1.1900357f, 1.1900357f, 1.0914886f, 1.0914886f, 4.2995041f, 4.2995041f, 4.0063034f, 4.0063034f, 0.7063034f,
                0.7063034f, 5.7063034f, 5.7063034f } } }};
        return presets;
    }

    // Un modo suena si cabe bajo 0,45·fs y su guía tiene al menos 1 muestra (STK: longitud > 2)
    static bool bandModeOn (double fm, double sr) { return fm < 0.45 * sr && sr / fm - 1.0 >= 1.0; }

    // Math Models > Banded (Essl y Cook 1999; STK BandedWG::tick): cada modo k es una guía de retardo d_k con un paso banda en
    // su frecuencia; con el arco, la diferencia entre su velocidad y la suma de las guías pasa por la tabla de fricción (BowTable,
    // offset 0: (|s·x| + 0,75)^-4 entre 0,01 y 0,98) y entra en todas. Salida: 4·Σ paso bandas (STK)
    float renderBanded (double dt)
    {
        auto input = 0.0f;
        if (bandBowing)
        {
            auto velIn = 0.0f;
            for (int k = 0; k < bandCount; ++k)
                if (bandModes[(size_t) k].on)
                    velIn += 0.999f * bandModes[(size_t) k].last; // STK: baseGain 0,999, integrationConstant 0
            const auto x = windBreath() - velIn;
            const auto q = std::abs (x * bandSlope) + 0.75f, q2 = q * q;
            input = x * juce::jlimit (0.01f, 0.98f, 1.0f / (q2 * q2)) / (float) bandActive;
        }
        auto s = 0.0f;
        for (int k = 0; k < bandCount; ++k)
        {
            auto& m = bandModes[(size_t) k];
            if (! m.on)
                continue;
            const auto u = input + m.g * m.last;
            const auto y = m.b0 * (u - m.x2) - m.a1 * m.y1 - m.a2 * m.y2;
            m.x2 = m.x1; m.x1 = u;
            m.y2 = m.y1; m.y1 = y;
            bandBuffer[(size_t) (m.base + m.write)] = y;
            m.write = m.write + 1 == m.size ? 0 : m.write + 1;
            // Lectura lineal a d muestras de la última escrita (0 = la última): vuelta de 1 + d + τ muestras = fs/(r·f)
            const auto d = juce::jlimit (0.0, (double) m.size - 3.0, 1.0 / (dt * (double) m.ratio) - 1.0 - m.tau);
            const auto i = (int) d;
            const auto fr = (float) (d - i);
            auto i0 = m.write - 1 - i;
            if (i0 < 0) i0 += m.size;
            const auto i1 = i0 == 0 ? m.size - 1 : i0 - 1;
            const auto a = bandBuffer[(size_t) (m.base + i0)], b = bandBuffer[(size_t) (m.base + i1)];
            m.last = a + fr * (b - a);
            s += y;
        }
        return 4.0f * s * bandGain;
    }

    // Instrumentos de STK Shakers (Cook; MIT-STK): piezas, ganancia, caída del sonido y del sistema, escala de Shake, variación
    // de frecuencia por choque, resonancias (Hz, radio a 44,1 kHz, nivel, si varían) y FIR final (b0, b1, b2)
    static const std::array<ShakerKind, 6>& shakerKinds()
    {
        static const std::array<ShakerKind, 6> kinds {{
            { 25.0f, 4.0f, 0.95f, 0.999f, 0.97f, 0.0f, 1, { 3200.0f }, { 0.96f }, { 1.0f }, { false }, { 1.0f, -1.0f, 0.0f } },   // Maraca
            { 512.0f, 8.0f, 0.96f, 0.997f, 0.97f, 0.0f, 1, { 3000.0f }, { 0.7f }, { 1.0f }, { false }, { 1.0f, -1.0f, 0.0f } },   // Cabasa
            { 64.0f, 4.0f, 0.96f, 0.999f, 0.94f, 0.0f, 1, { 5500.0f }, { 0.6f }, { 1.0f }, { false }, { 1.0f, 0.0f, -1.0f } },    // Sekere
            { 32.0f, 1.0f, 0.95f, 0.9985f, 0.95f, 0.05f, 3, { 2300.0f, 5600.0f, 8100.0f }, { 0.96f, 0.99f, 0.99f },             // Tambourine
              { 0.1f, 0.8f, 1.0f }, { false, true, true }, { 1.0f, 0.0f, -1.0f } },
            { 32.0f, 1.0f, 0.97f, 0.9994f, 0.9f, 0.03f, 5, { 2500.0f, 5300.0f, 6500.0f, 8300.0f, 9800.0f },                     // Sleigh Bells
              { 0.99f, 0.99f, 0.99f, 0.99f, 0.99f }, { 1.0f, 1.0f, 1.0f, 0.5f, 0.3f }, { true, true, true, true, true },
              { 1.0f, 0.0f, -1.0f } },
            { 1.2f, 0.4f, 0.9f, 0.9999f, 0.7f, 0.2f, 3, { 2800.0f, 2240.0f, 3360.0f }, { 0.995f, 0.995f, 0.995f },              // Bamboo Chimes
              { 1.0f, 1.0f, 1.0f }, { true, true, true }, { 1.0f, 0.0f, 0.0f } } }};
        return kinds;
    }

    // Math Models > Shaker (PhISEM, Cook 1997; STK Shakers::tick): la energía de la sacudida cae en cada muestra; un choque ocurre
    // con probabilidad N/1024 y suma la energía al nivel del sonido, que entra como impulso en las resonancias y cae deprisa.
    // Adaptado: STK devuelve 0 en cuanto la energía baja de 0,001 (corta la cola de las resonancias: un clic); aquí suenan hasta
    // apagarse. Al soltar la tecla la energía pasa a 0 (STK noteOff). Choques con el generador de la voz (sembrado)
    float renderShaker()
    {
        const auto& kind = shakerKinds()[(size_t) shakerKind];
        if (! stringGate)
            shakeEnergy = 0.0f;
        shakeEnergy *= shakeSys;
        auto input = 0.0f;
        if (shakeEnergy >= 0.001f && rng.nextFloat() * 1024.0f < shakeChance)
        {
            shakeLevel += shakeEnergy;
            input = shakeLevel;
            if (kind.vary > 0.0f)
                for (int i = 0; i < kind.count; ++i)
                    if (kind.varies[i])
                        shakeA1[(size_t) i] = -2.0f * shakeR[(size_t) i]
                                              * std::cos (juce::MathConstants<float>::twoPi * shakeHz[(size_t) i]
                                                          * (1.0f + kind.vary * (2.0f * rng.nextFloat() - 1.0f)) / (float) getSampleRate());
        }
        shakeLevel *= shakeSnd;
        auto s = 0.0f;
        for (int i = 0; i < kind.count; ++i)
        {
            const auto r = shakeR[(size_t) i];
            const auto y = input * kind.level[i] * shakeGain - shakeA1[(size_t) i] * shakeY1[(size_t) i] - r * r * shakeY2[(size_t) i];
            shakeY2[(size_t) i] = shakeY1[(size_t) i];
            shakeY1[(size_t) i] = y;
            s += y;
        }
        const auto out = kind.eq[0] * s + kind.eq[1] * shakeEq1 + kind.eq[2] * shakeEq2;
        shakeEq2 = shakeEq1;
        shakeEq1 = s;
        return out * shakerGains[shakerKind];
    }

    float renderString (double dt)
    {
        if (stringMode == excModal)
            return renderModal (dt);
        if (stringMode == excBanded)
            return renderBanded (dt); // sin continua: los pasos banda tienen un cero en 0 Hz
        if (stringMode == excMallet)
            return renderMallet();    // sin continua: la salida es Σ A_k·ω_k·p_k de osciladores sin componente en 0 Hz
        if (isWindMode (stringMode) || stringMode == excShaker)
        {
            // Bloqueo de DC a 20 Hz: el soplo entra en el tubo como presión continua (STK Clarinet no la quita); los impulsos
            // del Shaker son positivos, y el FIR de Bamboo Chimes (1, 0, 0) no quita su continua
            const auto w = stringMode == excShaker ? renderShaker() : renderWind (dt);
            stringHp = w - stringHpPrev + stringHpCoef * stringHp;
            stringHpPrev = w;
            return stringHp;
        }
        const auto dts = dt * (double) stringHarm; // Harmonic: la cuerda suena n veces más aguda
        const auto y = isGuideMode (stringMode) ? renderGuide (dts) : stringBuzzOn ? renderPluckBuzz (dts) : renderPluck (dts);
        auto out = y;
        if (stringPickupPos > 0.004f) // pastilla: peine 0,5·(y[n] - y[n - γ·N]) (N = periodo), lectura lineal
        {
            stringPickupUsed = true;
            pickupBuffer[(size_t) pickupWrite] = y;
            const auto back = juce::jlimit (1.0, (double) stringSize - 2.0, (double) stringPickupPos / dts);
            const auto j = (int) back;
            const auto fj = (float) (back - j);
            const auto p0 = pickupBuffer[(size_t) ((pickupWrite - j) & (stringSize - 1))];
            const auto p1 = pickupBuffer[(size_t) ((pickupWrite - j - 1) & (stringSize - 1))];
            pickupWrite = (pickupWrite + 1) & (stringSize - 1);
            out = 0.5f * (y - (p0 + fj * (p1 - p0)));
        }
        // Bloqueo de DC a 20 Hz: la continua circula en el lazo casi sin pérdidas (ganancia ρ por periodo)
        stringHp = out - stringHpPrev + stringHpCoef * stringHp;
        stringHpPrev = out;
        return stringHp;
    }

    // Lectura con interpolación lineal (STK DelayL) a 'delay' muestras de la última escrita (0 = la última)
    template <size_t Size>
    static float readLinear (const std::array<float, Size>& buffer, int write, double delay)
    {
        const auto d = juce::jlimit (0.0, (double) Size - 3.0, delay);
        const auto i = (int) d;
        const auto fr = (float) (d - i);
        const auto a = buffer[(size_t) ((write - 1 - i) & (int) (Size - 1))];
        const auto b = buffer[(size_t) ((write - 2 - i) & (int) (Size - 1))];
        return a + fr * (b - a);
    }

    // Envolvente de soplo de STK (Envelope / ADSR lineales): sube a windTarget, cae a windSustain y, al soltar la tecla, baja a 0
    float windBreath()
    {
        if (! stringGate && windStage != windIdle && windStage != windRelease)
            windStage = windRelease;
        const auto k = windRateScale;
        switch (windStage)
        {
            case windAttack:
                windEnv += windAttackRate * k;
                if (windEnv >= windTarget) { windEnv = windTarget; windStage = windEnv > windSustain ? windDecay : windSustained; }
                break;
            case windDecay: // Flute: de 1 a 0,8 en 10 ms (STK: ADSR::setDecayTime, ya en segundos)
                windEnv -= 0.2f / (0.01f * (float) getSampleRate());
                if (windEnv <= windSustain) { windEnv = windSustain; windStage = windSustained; }
                break;
            case windRelease:
                windEnv -= windReleaseRate * k;
                if (windEnv <= 0.0f) { windEnv = 0.0f; windStage = windIdle; }
                break;
            default: break;
        }
        return windMaxP * windEnv;
    }

    // Viento (Reed, Flute, Brass): los tick() de STK Clarinet, Flute y Brass (Cook y Scavone, MIT-STK) con su retardo
    // recalculado en cada muestra desde dt (bend y vibrato). Adaptado: sin el vibrato propio de STK (lo hacen el LFO y la
    // matriz del Synth); ruido del generador de la voz (sembrado); tasas escaladas a 44,1 kHz (windBreath)
    float renderWind (double dt)
    {
        auto breath = windBreath();
        switch (stringMode)
        {
            case excReed:
            {
                juce::ignoreUnused (windOutGain);
                // Guillemain, Kergomard y Voinier 2005, esquema explícito (26)-(30) con bc0 = 1. pe (presión en la boquilla)
                // en stringBuffer y ue (flujo) en guideBuffer, al mismo índice. Retardo D = fs/(2f) fraccional (el artículo
                // usa un entero), menos el retardo de fase del filtro de pérdidas y la corrección de longitud de la caña
                const auto gamma = reedGamma * breath * (1.0f + reedNoise * (2.0f * rng.nextFloat() - 1.0f));
                const auto pe1 = stringBuffer[(size_t) ((stringWrite - 1) & (stringSize - 1))];
                const auto ue1 = guideBuffer[(size_t) ((guideWrite - 1) & (stringSize - 1))];
                // Caña (19): oscilador de una masa, fr = 2205 Hz y qr = 0,6 (artículo, fig. 14: 2205 Hz y 0,3; con 0,3 el
                // modelo chilla en el tercer modo en C4 con ataques lentos; 0,6 es el mejor ajuste a las grabaciones)
                const auto x = reedB1 * pe1 + reedXa1 * windZ1 + reedXa2 * windY2;
                windY2 = windZ1;
                windZ1 = x;
                const auto D = juce::jmax (2.0, 0.5 / dt - reedTauLoss - reedTuneSamples) - 1.0; // muestras desde la última
                const auto peD = readLinear (stringBuffer, stringWrite, D);
                const auto ueD = readLinear (guideBuffer, guideWrite, D);
                const auto V = -reedA1 * ue1 - reedB0 * ueD + reedA1 * pe1 - reedB0 * peD; // (17) sin el término ue(n)
                const auto opening = 1.0f - gamma + x;
                const auto W = opening > 0.0f ? reedZeta * opening : 0.0f; // (28)
                const auto gv = gamma - V;
                const auto ue = 0.5f * (gv >= 0.0f ? 1.0f : -1.0f) * (-W * W + W * std::sqrt (W * W + 4.0f * std::abs (gv))); // (29)
                const auto pe = ue + V;                                                                              // (30)
                pushString (pe);
                guideBuffer[(size_t) guideWrite] = ue;
                guideWrite = (guideWrite + 1) & (stringSize - 1);
                // Presión exterior (13): diferencia de pe + ue; el artículo avisa de que exagera los agudos: paso bajo de un
                // polo a 1,5 kHz (corte de la fila de agujeros de un clarinete, orden de magnitud; mejor ajuste a las grabaciones)
                const auto sum = pe + ue;
                const auto ext = sum - windLastOut;
                windLastOut = sum;
                windY1 = (1.0f - reedLpA) * ext + reedLpA * windY1;
                return windY1 * windLevel * reedOutputGain;
            }
            case excFlute:
            {
                // Flute::tick. STK sintoniza el tubo a 2/3 de la nota y el chorro a 0,32 de él: el lazo del tubo mide 1,5
                // periodos y el del chorro, 1,5·1,32 = 1,98: el segundo modo sale +17 ct alto, más el adelanto del bloqueo de DC
                // (medido: +18..+33 ct de C3 a C6). Chorro hasta 2 periodos exactos con el bloqueo de DC, escalado por Jet / 0,32
                // (en su valor por defecto, exacto); medido en el plugin de C2 a C6, -21..+2 ct; bajo C4 no suena bien (como una
                // flauta)
                breath += breath * fluteNoise * (2.0f * rng.nextFloat() - 1.0f);
                const auto period = 1.0 / dt;
                // Adaptado (ajuste a las grabaciones de Iowa MIS): tubo abierto por los dos extremos, un periodo con dos
                // inversiones (STK: 1,5 periodos con una, un tubo cerrado sin armónicos pares: H2 a -26 dB frente a 0 dB en
                // una flauta real en C4 mf). El chorro sigue sumando 2 periodos con el tubo (primer modo, medido)
                const auto D = period - 1.0 - fluteTauLp - fluteTuneSamples;
                const auto jetDelay = juce::jmax (0.0, (2.0 * period - 1.0 - D - fluteTauLp - windTauDc) * (double) fluteJetRatio / 0.32);
                windY1 = (1.0f - flutePole) * windLastOut + flutePole * windY1; // OnePole
                const auto temp = -windY1;
                guideBuffer[(size_t) guideWrite] = breath - 0.5f * temp; // jetReflection 0,5
                guideWrite = (guideWrite + 1) & (stringSize - 1);
                const auto jet = readLinear (guideBuffer, guideWrite, jetDelay);
                const auto j = juce::jlimit (-1.0f, 1.0f, jet * (jet * jet - 1.0f)); // JetTable: x·(x² - 1)
                windDcY = j - windDcX + 0.99f * windDcY;                           // PoleZero::setBlockZero (0,99)
                windDcX = j;
                pushString (windDcY - 0.5f * temp);                                 // endReflection 0,5, invertida (extremo abierto)
                windLastOut = readLinear (stringBuffer, stringWrite, D);
                const auto y = 0.3f * windLastOut;
                windZ1 = (1.0f - fluteTiltA) * y + fluteTiltA * windZ1; // paso bajo de 1 kHz; y - windZ1 = paso alto
                // La inclinación sube el nivel (medido: pico x4,3 en mf y x5,4 en ff); 1/(1 + 0,6·s) lo devuelve al de sin ella
                return (y + fluteTilt * (y - windZ1)) / (1.0f + 0.6f * juce::jmax (0.0f, fluteTilt)) * windOutGain * fluteOutputGain;
            }
            default:
            {
                // Brass::tick. Retardo con interpolación pasa-todo (STK DelayA): D = N + α, α en [0,5, 1,5),
                // y[n] = c·x[n - N] + x[n - N - 1] - c·y[n - 1], c = (1 - α) / (1 + α)
                const auto mouth = 0.3f * breath;
                const auto bore = 0.85f * windLastOut;
                const auto lip = brassLipGain * (mouth - bore) - brassA1 * windY1 - brassA2 * windY2; // BiQuad (ganancia: arriba)
                windY2 = windY1;
                windY1 = lip;
                const auto area = juce::jmin (1.0f, lip * lip);
                const auto in = area * mouth + (1.0f - area) * bore;
                windDcY = in - windDcX + 0.99f * windDcY;
                windDcX = in;
                pushString (windDcY);
                // Tubo de dos periodos exactos con el bloqueo de DC (STK: 2·fs/f + 3, "fudge correction")
                const auto D = juce::jlimit (1.0, (double) stringSize - 4.0, (2.0 / dt - 1.0 - windTauDc) * (double) brassSlide);
                const auto n = (int) std::floor (D - 0.5);
                const auto alpha = (float) (D - n);
                const auto c = (1.0f - alpha) / (1.0f + alpha);
                const auto x0 = stringBuffer[(size_t) ((stringWrite - 1 - n) & (stringSize - 1))];
                const auto x1 = stringBuffer[(size_t) ((stringWrite - 2 - n) & (stringSize - 1))];
                windLastOut = c * x0 + x1 - c * windLastOut;
                // Radiación de la campana: deja salir los agudos y refleja los graves; paso alto de 2º orden (dos de un polo) a
                // 1,5 kHz. STK da la presión dentro del tubo: en C4 mf, H1 0 dB y centroide 2,05·f0 frente a H1 -22 dB y 5,12·f0
                // de una trompeta (Iowa MIS); con la radiación, 4,70·f0 (medido en el prototipo)
                const auto h1 = windLastOut - (brassRad1 = (1.0f - brassRadA) * windLastOut + brassRadA * brassRad1);
                const auto h2 = h1 - (brassRad2 = (1.0f - brassRadA) * h1 + brassRadA * brassRad2);
                return h2 * windLevel * brassOutputGain;
            }
        }
    }

    // Noise Burst y Pick: Karplus-Strong extendido en un solo lazo (startString)
    float renderPluck (double dt)
    {
        // Tension acorta el lazo (sube la nota) con la elongación; Stiff descuenta el retardo de su cascada en la nota
        const auto period = 1.0 / dt;
        const auto delta = tensionStep ((int) period, -1);
        const auto d = juce::jlimit (1.0, (double) stringSize - 8.0, period / (1.0 + delta) - 1.0 - stringS - stringLpTau - stiffTau);
        const auto i = (int) d;
        const auto fr = (float) (d - i);
        auto at = [this] (int k) { return stringBuffer[(size_t) ((stringWrite - 1 - k) & (stringSize - 1))]; };
        const auto xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        const auto y = ((c3 * fr + c2) * fr + c1) * fr + x0;

        auto v = stringRho * ((1.0f - stringS) * y + stringS * stringPrev);
        if (stiffOn)
            v = stiffProcess (v);
        if (stringLpA > 0.0f) // Damping (ver renderNextBlock)
        {
            stringLp += (1.0f - stringLpA) * (v - stringLp);
            v = stringLp;
        }
        pushString (v);
        stringPrev = y;
        return y;
    }

    // Wavetable: nivel de mipmap L = ceil(log2(1024 / armónicos permitidos)), con armónicos permitidos = 0,5/dt (el más alto bajo
    // Nyquist). Interpolación lineal entre los dos fotogramas vecinos de Position y dentro de cada tabla (tablas de 2048 + guarda).
    float renderWavetable (const WavetableData& wt, double dt, float position)
    {
        const auto v = readWavetable (wt, wtPhase, dt, position);
        wtPhase += dt;
        wtPhase -= std::floor (wtPhase);
        return v;
    }

    // Lectura sin estado (fase dada): la usan la fuente Wavetable (wtPhase) y Osc 2 en modo Wavetable (osc2Phase)
    static float readWavetable (const WavetableData& wt, double phase, double dt, float position)
    {
        const auto allowed = juce::jmax (1.0, 0.5 / juce::jmax (1.0e-9, dt));
        const auto mip = juce::jlimit (0, WavetableData::numLevels - 1, (int) std::ceil (std::log2 (1024.0 / allowed)));
        const auto framePos = position * (float) (wt.numFrames - 1);
        const auto f0 = juce::jmin (wt.numFrames - 1, (int) framePos);
        const auto f1 = juce::jmin (wt.numFrames - 1, f0 + 1);
        const auto ff = framePos - (float) f0;
        // Contrato: fase en [0, 1). Se normaliza aquí también (FM, sync y extremos de afinación no deben salir de la tabla)
        auto ph = phase - std::floor (phase);
        if (! (ph >= 0.0 && ph < 1.0))
            ph = 0.0; // NaN o redondeo a 1
        const auto x = ph * WavetableData::frameSize;
        const auto i = juce::jlimit (0, WavetableData::frameSize - 1, (int) x); // a[i + 1] cae como mucho en la guarda
        const auto fr = (float) (x - i);
        const auto* a = wt.table (f0, mip);
        const auto* b = wt.table (f1, mip);
        const auto va = a[i] + fr * (a[i + 1] - a[i]);
        const auto vb = b[i] + fr * (b[i + 1] - b[i]);
        return va + ff * (vb - va);
    }

    // Stiff (rigidez): cascada de pasa todo de primer orden iguales en el lazo (Van Duyne y Smith 1994; Rauhala y Välimäki,
    // DAFx 2006), coeficiente stiffA y su retardo de fase en la nota (descontado del lazo). Tension: potencia del lazo integrada
    // (Välimäki, Tolonen y Karjalainen, ICASSP 1999), tensionDelta = acortamiento relativo del lazo
    static constexpr int stiffStages = 16;
    std::array<float, stiffStages> stiffX {}, stiffY {};
    float stiffA = 0.0f;
    double stiffTau = 0.0, stiffSolvedF = 0.0, stiffSolvedB = 0.0;
    bool stiffOn = false, tensionOn = false;
    double tensionGain = 0.0, tensionPole = 0.0, tensionState = 0.0, tensionDelta = 0.0, tensionPeak = 0.0;
    static constexpr int stringSize = 8192; // potencia de 2: 8192 muestras = 5,9 Hz a 48 kHz (más grave que cualquier nota MIDI útil)
    std::array<float, stringSize> stringBuffer {};
    int stringWrite = 0;
    float stringPrev = 0.0f, stringRho = 0.99f, stringS = 0.5f, stringHp = 0.0f, stringHpPrev = 0.0f, stringHpCoef = 0.997f;
    float stringLpA = 0.0f, stringLp = 0.0f, stringPickupPos = 0.0f; // Damping y pastilla
    double stringLpTau = 0.0;
    std::array<float, stringSize> pickupBuffer {};
    int pickupWrite = 0;
    bool stringPickupUsed = false;
    bool stringActive = false;
    // Excitación de la nota (índices de stringExcite), armónico (1 = cuerda al aire), tecla pulsada (Bow, E-Bow) y Buzz
    static constexpr int excNoise = 0, excPick = 1, excBow = 2, excHammer = 3, excEbow = 4, excModal = 5;
    static constexpr int excReed = 6, excFlute = 7, excBrass = 8, excBanded = 9, excShaker = 10, excMallet = 11;
    static bool isGuideMode (int m) { return m >= excBow && m <= excEbow; }
    static bool isWindMode (int m) { return m >= excReed && m <= excBrass; }
    // Viento (renderWind): envolvente de soplo, estados de los filtros y parámetros por bloque
    enum WindStage { windIdle, windAttack, windDecay, windSustained, windRelease };
    WindStage windStage = windIdle;
    float windEnv = 0.0f, windTarget = 1.0f, windSustain = 1.0f, windAttackRate = 0.0f, windReleaseRate = 0.0f, windMaxP = 0.0f;
    float windRateScale = 1.0f, windOutGain = 1.0f;
    float windLastOut = 0.0f, windZ1 = 0.0f, windY1 = 0.0f, windY2 = 0.0f, windDcX = 0.0f, windDcY = 0.0f;
    float reedNoise = 0.2f, fluteNoise = 0.15f, fluteJetRatio = 0.32f, flutePole = 0.65f;
    float reedA1 = 0.0f, reedB0 = 0.0f, reedZeta = 0.25f, reedGamma = 0.4f, reedLpA = 0.0f;
    float reedB1 = 0.0f, reedXa1 = 0.0f, reedXa2 = 0.0f;           // caña, ecuación (19)
    double reedTauLoss = 0.0, reedTuneSamples = 0.0, fluteTuneSamples = 0.0;
    float brassRadA = 0.0f, brassRad1 = 0.0f, brassRad2 = 0.0f, windLevel = 1.0f, fluteTilt = 0.0f, fluteTiltA = 0.0f;
    double fluteTauLp = 0.0, windTauDc = 0.0;
    float brassA1 = 0.0f, brassA2 = 0.0f, brassLipGain = 0.03f, brassSlide = 1.0f;
    // Nivel de salida de cada instrumento de viento, como bowOutputGain: pico en A3 a velocidad 100 igual al de Pick (0,125,
    // medido con KoniBench): Reed 0,0062 sin ajuste (con windLevel x1,19), x20; Flute 0,088 (con la inclinación), x1,42; Brass 0,016 (con windLevel x1,22), x7,7
    static constexpr float reedOutputGain = 20.0f, fluteOutputGain = 1.42f, brassOutputGain = 7.7f;
    // Modal (renderModal): modos copiados de la receta al pulsar
    std::array<float, ModalRecipe::maxModes> modalRatio {}, modalTau {}, modalEnv {}, modalMul {};
    std::array<double, ModalRecipe::maxModes> modalPhase {};
    std::array<bool, ModalRecipe::maxModes> modalAudible {};
    int modalCount = 0, modalAge = 0, modalFade = 72;
    // Mallet (renderMallet): modos de la receta Modal como osciladores con fuerza (SDT) y la baqueta; en double (desplazamientos
    // de ~1e-6 y ω de ~1e4 rad/s)
    struct MalletMode
    {
        double p0 = 0.0, p1 = 0.0, v = 0.0;                          // posición actual y anterior, velocidad
        double b1 = 0.0, a1 = 0.0, a2 = 0.0, b0v = 0.0, b1v = 0.0;    // por bloque
        double ratio = 1.0, amp = 0.0, tau = 0.1, omega = 0.0;
    };
    std::array<MalletMode, ModalRecipe::maxModes> malletModes {};
    int malletCount = 0;
    double malletY = 0.0, malletV = 0.0, malletK = 0.0, malletMass = 0.1, malletMu = 0.3, malletScale = 0.0;
    float malletNorm = 1.0f;
    // Pico en A3 a velocidad 100 cerca de 0,125 (medido con KoniBench, Soft Key y mandos por defecto: 0,0891 sin ajuste). Con la
    // baqueta por defecto (1,7 ms) los agudos bajan: C6 -8,3 dB y C7 -17,5 dB respecto a A3 (el contacto dura más que su periodo)
    static constexpr double malletOutputGain = 1.4;
    // Banded (renderBanded): un modo por guía, cada una en su tramo de bandBuffer (16384 muestras: con el cuenco, 3,5 periodos
    // por nota con margen x2 para el bend, cabe hasta 13,7 Hz a 48 kHz; por debajo, el margen se reduce)
    static constexpr int bandSize = 16384;
    struct BandMode
    {
        int base = 0, size = 4, write = 0;
        float ratio = 1.0f, gain0 = 0.0f, excite = 1.0f;          // del preset
        float g = 0.0f, a1 = 0.0f, a2 = 0.0f, b0 = 0.0f;          // por bloque
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f, last = 0.0f;
        double tau = 0.0;
        bool on = false;
    };
    std::array<float, bandSize> bandBuffer {};
    std::array<BandMode, bandMaxModes> bandModes {};
    int bandCount = 0, bandActive = 1, bandUsed = 0;
    bool bandBowing = false;
    float bandSlope = 5.5f;
    // Shaker (renderShaker): instrumento elegido y estado
    int shakerKind = 0;
    float shakeEnergy = 0.0f, shakeLevel = 0.0f, shakeSys = 0.999f, shakeSnd = 0.95f, shakeChance = 25.0f, shakeGain = 0.5f;
    float shakeEq1 = 0.0f, shakeEq2 = 0.0f;
    bool shakeFresh = false; // nota nueva: el bloque fija también las resonancias que varían
    std::array<float, 5> shakeHz {}, shakeR {}, shakeA1 {}, shakeY1 {}, shakeY2 {};
    // Nivel de salida, para que el pico en A3 a velocidad 100 se parezca al de Pick (0,125, medido con KoniBench). Banded, por
    // forma (Uniform, Tuned, Glass, Bowl): golpe sin ajuste 0,0039, 0,0094 (con la baqueta), 0,0051 y 0,0160; arco 0,063 (Uniform no se
    // midió: como Tuned), 0,063, 0,085 y 0,057. Shaker, por instrumento, media de C3, A3, C4 y C5 (los choques son al azar):
    // 0,0145, 0,0173, 0,0127, 0,0136, 0,0328 y 0,0075
    static constexpr float bandStrikeGains[4] = { 32.2f, 13.25f, 24.5f, 7.8f }, bandBowGains[4] = { 2.0f, 2.0f, 1.48f, 2.19f };
    static constexpr float shakerGains[6] = { 8.6f, 7.2f, 9.8f, 9.2f, 3.8f, 16.7f };
    float bandGain = 1.0f;
    int stringMode = excNoise, stringHarm = 1;
    bool stringGate = false;
    // Buzz: tramo del puente en diferencias finitas (renderPluckBuzz)
    static constexpr int buzzMax = 256;
    std::array<float, buzzMax + 2> buzzY {}, buzzYPrev {};
    int buzzJ = 2;
    float buzzDepth = 0.048f, buzzA = 0.0f, buzzBPrev = 0.0f;
    bool stringBuzzOn = false;
    static constexpr float buzzOutputGain = 2.1f; // medido: pico de Pick en C#3 0,129 sin Buzz y 0,060 con él
    // Guía de dos tramos (renderGuide): el tramo de la cejuela usa stringBuffer y el del puente, guideBuffer
    std::array<float, stringSize> guideBuffer {};
    int guideWrite = 0;
    float guideLp = 0.0f, guideB0 = 0.0f, guideP = 0.0f, guideBeta = 0.13f;
    double guideTau = 0.0;
    float bowVel = 0.0f, bowVelTarget = 0.0f, bowSlope = 3.0f, bowAttackCoef = 0.001f;
    float hammerK = 1.0e8f, hammerV = 0.0f, hammerY = 0.0f, hammerStringY = 0.0f;
    static constexpr float hammerMass = 0.001f;
    int hammerAway = 0;
    static constexpr int hammerAwayMax = 960; // 20 ms a 48 kHz (a otras frecuencias, el mismo número de muestras)
    float ebowGain = 1.25f;
    bool ebowSeed = false;
    // Nivel de salida de cada modo, para que a velocidad 100, Excite 50 % y A3 su pico se parezca al de Pick (0,125, medido con
    // KoniBench): Bow 0,091 sin ajuste, x1,4; Hammer 0,448, x0,28; E-Bow 0,144 (de 0,044 a 0,144 según la nota), x1
    static constexpr float bowOutputGain = 1.4f, hammerOutputGain = 0.28f, ebowOutputGain = 1.0f;

    const Params& params;
    const SampleSlot& sampleSlot;
    const SampleSlot& osc2SampleSlot;
    const SampleSlot& osc3SampleSlot;
    const ModalSlot& modalSlot;
    const WavetableSlot& wavetableSlot;
    const WavetableSlot& osc2WavetableSlot;
    const WavetableSlot& osc3WavetableSlot;
    double wtPhase = 0.0;
    float bendTarget = 0.0f, modTarget = 0.0f, bendSmoothed = 0.0f, modSmoothed = 0.0f, controlCoef = 1.0f;
    float glideSemis = 0.0f, pitchEnvValue = 0.0f;
    bool pendingLegato = false;
    bool pendingRetrigger = false; // Mono/Legato: voz robada mientras sonaba; la nota nueva la redispara sin cortarla
    float levelTarget = 0.0f;      // nivel por velocity; tras un redisparo, 'level' lo alcanza en 5 ms (sin escalón)
    float polyPressure = 0.0f, channelPressure = 0.0f, pressureSmoothed = 0.0f;
    unsigned lfsr = 1u; // 15 bits, nunca 0 (estado bloqueado del LFSR)
    double lfsrPhase = 0.0;
    SampleReader sample, osc2Sample, osc3Sample; // fuente Sample, Osc 2 y Osc 3 en modo Sample
    // Complex con el ruido de modulador (complexModSource = Noise): valor de ruido de esta muestra, calculado antes del Complex;
    // noiseModReady: renderMonoSources lo usa también como salida de Noise (un solo valor por muestra, como sin modulador)
    float noiseMod = 0.0f;
    bool noiseModReady = false;
    VoiceLfo lfo1, lfo2;
    double frequency = 440.0;
    konisimd::UnisonLanes lanes {}; // fases, relaciones y ganancias del unísono (8 carriles; usa maxUnison = 7)
    ComplexParams complex;
    MixParams mix;
    double osc2Phase = 0.0, subPhase = 0.0, syncPhase = 0.0;
    std::array<float, 7> pink {};
    float noteVelocity = 1.0f;
    Decimator decimator;
    std::array<DcState, 2> foldDc {};
    // Colas de corrección minBLEP (Dsp/MinBlep.h): Osc 1 a fs, Osc 1 en modo complejo a 2·fs (antes del diezmado), Osc 2 + sub
    MinBlep::Ring osc1Blep { MinBlep::table1x() }, complexBlep { MinBlep::table2x() }, monoBlep { MinBlep::table1x() };
    int numUnison = 1;
    float level = 0.0f, keyOctaves = 0.0f;
    float vcaFeedFactor = 0.0f, vcaHpfX = 0.0f, vcaHpfY = 0.0f; // VCA Click: factor de la voz y paso alto del feedthrough
    VoiceSat::Channel satL, satR; // saturación por voz (estado ADAA y bloqueador de continua del modo Diode)
    double satGain = 0.0, satOut = 1.0;
    float satWet = 0.0f;
    float staticCents = 0.0f, cutoffSpread = 1.0f;
    // Deriva (updateDrift): cents por unidad de Drift, valor actual y paso de la rampa
    static constexpr int driftHop = 128; // 2,7 ms a 48 kHz (la deriva corta hacia 10 Hz)
    KoniRandom::Pink voicePink, osc2Pink;
    float tempco = 0.0f, osc2StaticCents = 0.0f, driftCents = 0.0f, osc2DriftCents = 0.0f, driftStep = 0.0f, osc2DriftStep = 0.0f;
    int driftCountdown = 0;

    // Deriva en cents por unidad de Drift en el instante t (s, de la línea de tiempo; Dsp/RandomClock.h):
    //  voz: 1/f propia, σ 3 ct, 10 bandas desde 0,05 Hz (la de 25,6 Hz corta hacia ~10 Hz: el ruido de valor de una banda f
    //    concentra su energía por debajo de ~f/2; medido en réplica, pendiente -1,2 de 0,02 a 1 Hz con 8 bandas y -2,9 por encima);
    //  común a todas las voces de la instancia (fuente y temperatura): 1/f, σ 2 ct, 0,005-0,64 Hz;
    //  error de escala térmico: tempco de la voz · octavas desde C4 · temperatura común (1/f, σ 1, 0,002-0,032 Hz);
    //  Osc 2: 1/f propia, σ 2 ct, mismas 10 bandas.
    // Con Drift al 20 % (por defecto): σ de la voz + común = √(0,6² + 0,4²) = 0,72 ct (antes: 0,8 ct de ruido blanco filtrado a 0,5 Hz)
    // offset: muestra del bloque actual (puede pasar del final: la parte común se extrapola con la misma recta)
    void updateDrift (int offset, float& voiceCents, float& osc2Cents) const
    {
        const auto& clock = params.randomClock;
        const auto t = clock.seconds (offset);
        voiceCents = (float) (3.0 * voicePink.at (t) + 2.0 * clock.common (offset) + tempco * keyOctaves * clock.temperature (offset));
        osc2Cents = (float) (2.0 * osc2Pink.at (t));
    }
    float voicePanL = 1.0f, voicePanR = 1.0f; // Voice Pan: ganancias equal-power (1, 1 = centro)
    // Matriz de modulación: ranuras del bloque y fuentes propias de la voz
    ModMatrix::Block matrix;
    std::array<float, ModMatrix::numDestinations> lastMtx {};
    std::array<float, ModMatrix::numSources> lastSrc {}; // valores de las fuentes en la última muestra (lámparas de la UI)
public:
    int cardIndex = 0; // voz física (Tolerances: Per Voice Card)
private:
    juce::int64 lastRenderEnd = 0;
    double lastDt = 0.0, lastDt2 = 0.0, lastSubRatio = 0.5;
    float wheelTarget = 0.0f, wheelSmoothed = 0.0f, mpeTimbre = 0.0f, timbreSmoothed = 0.0f, noteRandom = 0.0f, panBase = 0.0f;
    float memberBend = 0.0f, memberBendSmoothed = 0.0f; // MPE: bend del canal de la nota (-1..1) y en semitonos suavizado
    juce::Random rng;
    SynthEnvelope adsr, filterEnv; // lineal o analógica RC (Dsp/AnalogEnvelope.h)
    VoiceFilters::VoiceFilter filter, filterR; // ladder y demás modelos (Dsp/Filters.h); filterR solo con unísono en estéreo
    // Sobremuestreo del ladder (Balanced / High Quality) por canal: interpoladores y diezmadores de las dos etapas 2x y la
    // memoria de la ADAA; se vacían en cada note-on
    struct LadderOs
    {
        VoiceSat::Upsampler2x u1, u2;
        VoiceSat::Downsampler2x d1, d2;
        double ax = 0.0, af = 0.0;
        float xPrev = 0.0f;
    };
    LadderOs osL, osR;
};

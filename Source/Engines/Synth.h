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
    SynthVoice (const Params& p, const SampleSlot& s, const SampleSlot& s2, const WavetableSlot& w)
        : params (p), sampleSlot (s), osc2SampleSlot (s2), wavetableSlot (w) { rng.setSeedRandomly(); }

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
            static std::atomic<unsigned> alternate { 0 };
            const auto amount = params.voicePan->load();
            const auto mode = (int) params.voicePanMode->load();
            const auto pos = mode == 0 ? ((alternate++ & 1u) ? 1.0f : -1.0f)
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
        level = 0.2f * velocity; // 0.2 deja margen para 5 voces a velocity 1 sin clipping
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
            if (std::exchange (pendingLegato, false)) // legato pendiente que no llegó (panic, cambio de modo)
            {
                adsr.reset();
                filterEnv.reset();
            }
            return;
        }

        updateEnvelopes(); // los potenciómetros se leen una vez por bloque

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
        const auto simd = numUnison >= 3 && simdActive();

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
        const auto noiseGate = unmuted (Params::srcNoise, params.noiseMute);
        mix.osc1Level  = params.osc1Level->load() * osc1Gate;
        mix.pulseWidth = params.pulseWidth->load();
        mix.osc1Tri.set (params.osc1TriSym->load(), params.osc1TriRound->load());
        mix.osc2Tri.set (params.osc2TriSym->load(), params.osc2TriRound->load());
        mix.osc3Tri.set (params.osc3TriSym->load(), params.osc3TriRound->load());
        const auto tri1 = mix.osc1Tri.active && wave == triangle; // el oscilador complejo no la usa
        mix.pwmDepth   = params.pwmDepth->load();
        mix.osc2Wave   = (int) params.osc2Wave->load();
        mix.osc2Ratio  = std::exp2 ((double) params.osc2Octave->load()
                                    + ((double) params.osc2Semi->load() + (double) params.osc2Fine->load() / 100.0) / 12.0);
        mix.osc2Level  = params.osc2Level->load() * osc2Gate;
        mix.osc2Sync   = params.osc2Sync->load() > 0.5f;
        mix.subLevel   = params.subLevel->load() * unmuted (Params::srcOsc3, params.osc3Mute);
        // Osc 3 (el Sub ampliado): octava base -1/-2 y Semi/Fine encima. Con Semi 0 y Fine 0, exp2(0) = 1 exacto: la relación es la
        // de siempre (0,5 o 0,25) bit a bit
        mix.subRatio   = ((int) params.subOctave->load() == 0 ? 0.5 : 0.25)
                       * std::exp2 (((double) params.osc3Semi->load() + (double) params.osc3Fine->load() / 100.0) / 12.0);
        mix.subWave    = waveFromChoice ((int) params.osc3Wave->load());
        mix.noiseLevel = params.noiseLevel->load() * noiseGate;
        mix.noiseColor = (int) params.noiseColor->load();
        const auto sampleLevel = params.sampleLevel->load() * unmuted (Params::srcSample, params.sampleMute);
        const auto stringLevel = params.stringLevel->load() * unmuted (Params::srcString, params.stringMute);
        const auto wtLevel = params.wtLevel->load() * unmuted (Params::srcWavetable, params.wtMute);
        const auto wtPositionBase = params.wtPosition->load();
        const auto* blockWavetable = wtLevel > 0.0f ? wavetableSlot.current.load (std::memory_order_acquire) : nullptr;
        // Osc 2 en modo Wavetable: la misma tabla, con la fase de Osc 2 y su propia posición (sin tabla: Osc 2 calla)
        const auto osc2WtPositionBase = params.osc2WtPosition->load();
        mix.osc2Table = mix.osc2Wave == wavetableWave ? wavetableSlot.current.load (std::memory_order_acquire) : nullptr;
        // Osc 2 en modo Sample: su propio fichero (sin fichero: Osc 2 calla)
        mix.osc2Sample = mix.osc2Wave == sampleWave ? osc2SampleSlot.current.load (std::memory_order_acquire) : nullptr;
        {
            // Filtro de lazo (1 - S) + S·z^-1 y ρ, por bloque con la frecuencia base (vibrato y bend lo mueven < 1 %).
            // g = ganancia por periodo para caer 60 dB en Decay. S = ½ (promedio clásico) con ρ = g / cos(ω/2) si basta;
            // en agudos, donde cos(ω/2) < g, "decay stretching" (Jaffe y Smith): S tal que |H(ω0)| = g y ρ = 1, con
            // |H|² = 1 - 4·S·(1 - S)·sin²(ω/2). Réplica (ks_check3.py): T60 exacto hasta 659 Hz, -1 % a 1,3 kHz,
            // -13 % a 2,6 kHz (antes 1,40 s en vez de 3 s a 1,3 kHz); afinación ±0,2 ct de 41 Hz a 4,2 kHz.
            const auto g = std::pow (10.0, -3.0 / (frequency * juce::jmax (0.05f, params.stringDecay->load())));
            const auto w0 = juce::MathConstants<double>::twoPi * frequency / sampleRate;
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
            stringLpA = damping > 0.0f ? (float) std::exp (-juce::MathConstants<double>::twoPi * frequency * std::exp2 (6.0 * (1.0 - damping)) / sampleRate) : 0.0f;
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
        }
        sample.loop = params.sampleLoop->load() > 0.5f;
        osc2Sample.loop = params.osc2SampleLoop->load() > 0.5f;
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
        complex.modSource = (int) params.complexModSource->load(); // 0 interno, 1 Osc 2, 2 Osc 3
        const auto externalMod = complex.on && complex.modSource != 0;
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
                src[ModMatrix::srcRandom] = noteRandom;
                src[ModMatrix::srcPitchBend] = mpe ? memberBend
                                                   : bendRangeNow > 0.0f ? juce::jlimit (-1.0f, 1.0f, bendSmoothed / bendRangeNow) : 0.0f;
                matrix.compute (src, mtx);
                lastMtx = mtx;
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
                        externalModulator (sub, dt, osc2Mul, pw, args);
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
            const auto savedOsc2Level = mix.osc2Level, savedNoiseLevel = mix.noiseLevel; // la matriz los mueve solo en esta muestra
            if (matrix.targets[ModMatrix::dstOsc2Level])  mix.osc2Level = juce::jlimit (0.0f, 1.0f, savedOsc2Level + mtx[ModMatrix::dstOsc2Level]) * osc2Gate;
            if (matrix.targets[ModMatrix::dstNoiseLevel]) mix.noiseLevel = juce::jlimit (0.0f, 1.0f, savedNoiseLevel + mtx[ModMatrix::dstNoiseLevel]) * noiseGate;
            auto monoSources = renderMonoSources (dt, pw, osc2Mul, externalMod);
            mix.osc2Level = savedOsc2Level;
            mix.noiseLevel = savedNoiseLevel;
            if (sampleLevel > 0.0f)
                monoSources += sampleLevel * sample.render (dt, blockSample);
            if (stringLevel > 0.0f && stringActive)
                monoSources += stringLevel * renderString (dt);
            if (blockWavetable != nullptr)
                monoSources += wtLevel * renderWavetable (*blockWavetable, dt, juce::jlimit (0.0f, 1.0f, wtPositionBase + wtPosMod));
            left  += monoSources;
            right += monoSources;

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

            if (satOn) // saturación de esta voz, tras el filtro y antes del VCA
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
                    if (lanes.phase[u] >= 1.0)
                        lanes.phase[u] -= 1.0;
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
    };

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
        const auto m = args.external ? (float) args.extMod : oscillator (complex.modWave, lanes.modPhase[u], dtM);
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
            if (wrapped)
                lanes.modPhase[u] -= 1.0;
            modEnd = lanes.modPhase[u];
        }

        lanes.phase[u] += dtInst;
        lanes.phase[u] -= std::floor (lanes.phase[u]); // envuelve en ambos sentidos
        if (complex.sync && wrapped)
            lanes.phase[u] = modEnd / dtM * std::abs (dtInst);

        return s;
    }

    // Modulador externo del Complex (Osc 2 u Osc 3) en la submuestra sub (0, 1) de esta muestra. Se lee de la trayectoria de
    // fase del oscilador en el instante τ = sub/2 (y τ + 1/2 para su vuelta), sin tocar su estado: renderMonoSources la avanza
    // después, en la misma muestra. Osc 2 con su sync: se reinicia en f = (1 - syncPhase)/dt y desde ahí su fase es
    // (τ - f)·dt2 (en τ = 1, el syncPhase/dt·dt2 de renderMonoSources). El valor es la forma de onda evaluada a 2·fs con
    // PolyBLEP (como el modulador interno), no la salida minBLEP del oscilador audible.
    void externalModulator (int sub, double dt, double osc2Mul, float pw, konisimd::ComplexArgs& a) const
    {
        const auto t0 = 0.5 * sub, t1 = t0 + 0.5;
        double inc, u0, u1;
        bool reset = false;
        const auto syncs = mix.osc2Sync && syncPhase + dt >= 1.0;
        const auto f = (1.0 - syncPhase) / dt;
        if (complex.modSource == 1)
        {
            inc = dt * mix.osc2Ratio * osc2Mul; // = dt2 de renderMonoSources
            const auto unwrapped = [&] (double t) { return syncs && t >= f ? (t - f) * inc : osc2Phase + t * inc; };
            u0 = unwrapped (t0);
            u1 = unwrapped (t1);
            reset = syncs && t0 < f && f <= t1;
        }
        else
        {
            inc = dt * mix.subRatio;
            u0 = subPhase + t0 * inc;
            u1 = subPhase + t1 * inc;
        }
        const auto p0 = u0 - std::floor (u0);
        a.external = true;
        a.extDtM = 0.5 * inc;
        a.extWrapped = reset || std::floor (u1) > std::floor (u0);
        a.extPhaseEnd = u1 - std::floor (u1);
        if (complex.modSource == 2)
            a.extMod = oscillator (mix.subWave, p0, a.extDtM, 0.5f, true);
        else if (mix.osc2Wave == sampleWave)
        {
            // Sample: el cursor en el instante τ (tras el reinicio del sync, desde Start), con su final, su bucle y el fundido de
            // Length; la fase nominal de Osc 2 sigue dando el incremento (escala del índice FM) y la vuelta (Mod Sync)
            a.extMod = 0.0;
            const auto& r = osc2Sample;
            if (mix.osc2Sample != nullptr && r.data == mix.osc2Sample)
            {
                const auto afterReset = syncs && t0 >= f;
                const auto at = afterReset ? r.loopStart + (t0 - f) * inc * r.data->sampleRate / r.rootHz
                                           : r.pos + t0 * inc * r.data->sampleRate / r.rootHz;
                const auto p = (afterReset ? ! r.cut : r.playing) ? r.place (at) : -1.0;
                if (p >= 0.0)
                    a.extMod = r.valueAt (p) * r.fade();
            }
        }
        else if (mix.osc2Wave != wavetableWave)
            a.extMod = oscillator (mix.osc2Wave, p0, a.extDtM, pw, true);
        else
            a.extMod = mix.osc2Table != nullptr ? readWavetable (*mix.osc2Table, p0, a.extDtM, mix.osc2WtPos) : 0.0;
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
        int osc2Wave = saw, subWave = square; // subWave: forma de Osc 3
        double osc2Ratio = 1.0, subRatio = 0.5;
        bool osc2Sync = false;
        int noiseColor = 0; // 0 blanco, 1 rosa, 2 LFSR largo, 3 LFSR corto
        const WavetableData* osc2Table = nullptr; // modo Wavetable: tabla del bloque y posición de esta muestra
        float osc2WtPos = 0.0f;
        const SampleData* osc2Sample = nullptr; // modo Sample: fichero del bloque
        TriShape osc1Tri, osc2Tri, osc3Tri;     // forma del triángulo (TriSym, TriRound); inactiva a 0 %
    };
    static constexpr int wavetableWave = 4; // osc2Wave = Wavetable (Layout.h: tras Sine, Triangle, Saw y Square)
    static constexpr int sampleWave = 5;    // osc2Wave = Sample

    // Fuentes mono del mezclador. dt = incremento de fase de Osc 1 (con vibrato y drift), así Osc 2 y el sub los siguen.
    //  Osc 2: dt·2^(oct + (semi + fine/100)/12); sync duro a la fundamental de Osc 1 (fase de referencia sin unísono ni FM),
    //    reiniciando con la fracción de muestra transcurrida desde el reinicio.
    //  Sub / Osc 3: -1 o -2 octavas más Semi (±24) y Fine; seno, triángulo, sierra o cuadrada al 50 % (por defecto, la cuadrada
    //    del Sub). minBLEP en sierra y cuadrada, como Osc 1 y Osc 2.
    //  Ruido: blanco uniforme (RMS 0,577) o rosa (filtro "refined" de Paul Kellet, musicdsp.org); medido en réplica:
    //    plano ±0,2 dB por banda de octava de 125 Hz a 4 kHz (-0,7 dB a 8 kHz); ×0,3277 iguala su RMS al del blanco.
    // keepPhases: Osc 2 u Osc 3 modulan el Complex; sus fases avanzan aunque su nivel sea 0 (o estén en Mute o fuera del Solo)
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
                out += mix.osc2Level * (mix.osc2Tri.active && mix.osc2Wave == triangle ? mix.osc2Tri (s) : s);
            }
            else if (osc2Table && mix.osc2Table != nullptr)
                out += mix.osc2Level * readWavetable (*mix.osc2Table, osc2Phase, dt2, mix.osc2WtPos);
        }
        // Osc 2 Sample: avanza siempre que se llega aquí (también a nivel 0, en Mute o como modulador del Complex), como su fase.
        // Con sync, el salto entre el valor en el instante del reinicio (f = (1 - syncPhase)/dt) y el de Start se mide antes de
        // avanzar y se corrige con minBLEP más abajo; sin sync el sample suena tal cual
        float osc2SampleJump = 0.0f;
        const auto osc2SyncAt = syncPhase + dt >= 1.0 ? (1.0 - syncPhase) / dt : 1.0;
        if (osc2Sampled)
        {
            if (mix.osc2Level > 0.0f && mix.osc2Sync && syncPhase + dt >= 1.0)
            {
                const auto before = osc2Sample.playing && osc2Sample.data == mix.osc2Sample
                                  ? osc2Sample.place (osc2Sample.pos + osc2SyncAt * dt2 * osc2Sample.data->sampleRate / osc2Sample.rootHz) : -1.0;
                const auto after = osc2Sample.canRestart (mix.osc2Sample) ? osc2Sample.valueAt (osc2Sample.loopStart) : 0.0f;
                osc2SampleJump = osc2Sample.fade() * (after - (before >= 0.0 ? osc2Sample.valueAt (before) : 0.0f));
            }
            out += mix.osc2Level * osc2Sample.render (dt2, mix.osc2Sample);
        }
        if (mix.subLevel > 0.0f) // Osc 3: la cuadrada va siempre al 50 % (no sigue al mando PW, como el Sub)
        {
            const auto s = oscillator (mix.subWave, subPhase, dt * mix.subRatio, 0.5f, false);
            out += mix.subLevel * (mix.osc3Tri.active && mix.subWave == triangle ? mix.osc3Tri (s) : s);
        }
        if (mix.noiseLevel > 0.0f)
        {
            const auto w = 2.0f * rng.nextFloat() - 1.0f;
            float n = w;
            if (mix.noiseColor >= 2)
            {
                // LFSR de 15 bits (nesdev APU_Noise): realimentación bit0 XOR bit1 (largo) o bit0 XOR bit6 (corto), desplazamiento a la
                // derecha y realimentación en el bit 14. Salida ±1 según el bit 0 (×0,577: mismo RMS que el blanco uniforme)
                const auto shortMode = mix.noiseColor == 3;
                lfsrPhase += dt * (shortMode ? 93.0 : 8.0);
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
            out += mix.noiseLevel * n;
        }

        // Saltos de este paso para el minBLEP: Osc 2 (con su reinicio si la referencia da la vuelta) y sub
        {
            const auto syncWraps = syncPhase + dt >= 1.0;
            if (mix.osc2Level > 0.0f && ! osc2Table && ! osc2Sampled)
                MinBlep::syncedStep (monoBlep, mix.osc2Wave, osc2Phase, dt2, mix.osc2Sync && syncWraps,
                                     syncWraps ? (1.0 - syncPhase) / dt : 1.0, pw, mix.osc2Level, mix.osc2Level);
            // Wavetable: la tabla ya está limitada en banda (mipmaps); solo el reinicio del sync es un salto: tabla(0) - tabla(fase
            // en el instante del reinicio), con la misma posición y el mismo nivel de mipmap
            if (mix.osc2Level > 0.0f && osc2Table && mix.osc2Table != nullptr && mix.osc2Sync && syncWraps)
            {
                const auto f = (1.0 - syncPhase) / dt;
                auto ta = osc2Phase + dt2 * f;
                ta -= std::floor (ta);
                const auto jump = readWavetable (*mix.osc2Table, 0.0, dt2, mix.osc2WtPos)
                                - readWavetable (*mix.osc2Table, ta, dt2, mix.osc2WtPos);
                monoBlep.add (f, jump * mix.osc2Level, jump * mix.osc2Level);
            }
            // Sample: el mismo tratamiento (el fichero ya está limitado en banda por su nivel diezmado)
            if (osc2SampleJump != 0.0f)
                monoBlep.add (osc2SyncAt, osc2SampleJump * mix.osc2Level, osc2SampleJump * mix.osc2Level);
            if (mix.subLevel > 0.0f)
                MinBlep::segment (monoBlep, mix.subWave, subPhase, dt * mix.subRatio, 0.0, 1.0, 0.5, mix.subLevel, mix.subLevel);
        }

        // Avance de fases (después de leer: mismo convenio que Osc 1)
        syncPhase += dt;
        const auto syncWrapped = syncPhase >= 1.0;
        syncPhase -= std::floor (syncPhase);
        osc2Phase += dt2;
        osc2Phase -= std::floor (osc2Phase);
        if (mix.osc2Sync && syncWrapped)
        {
            osc2Phase = syncPhase / dt * dt2;
            if (osc2Sampled) // de nuevo desde Start, en el instante exacto: lo recorrido tras el reinicio
                osc2Sample.restart (syncPhase / dt * dt2, mix.osc2Sample);
        }
        subPhase += dt * mix.subRatio;
        subPhase -= std::floor (subPhase);

        return out;
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
    }

    // Cuerda pulsada: Karplus-Strong extendido (Jaffe y Smith 1983; ccrma.stanford.edu/~jos/pasp, EKS).
    //  Excitación (al pulsar): un periodo de ruido blanco -> pasa-bajos de nivel dinámico R = e^(-π·L/fs),
    //  L = 200 Hz·2^(7·Brightness) -> peine de posición de púa 1 - z^(-round(β·N)) -> normalizado a pico 0,8.
    //  Lazo: retardo leído con Hermite (afinación fraccionaria continua: pitch bend y vibrato sin saltos) + filtro
    //  (1 - S) + S·z^-1 (amortiguación, S muestras de retardo) × ρ. Retardo total = fs/f -> d = fs/f - 1 - S.
    //  S y ρ: ver renderNextBlock (ganancia por periodo a la fundamental para caer 60 dB en Decay segundos).
    void startString (double f0)
    {
        stringActive = params.stringLevel->load() > 0.0f;
        if (! stringActive)
            return;

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
        for (int n = 0; n < period; ++n)
            pushString (0.8f * burst[(size_t) n] / peak);
        stringPrev = stringHp = stringHpPrev = stringLp = 0.0f;
        if (std::exchange (stringPickupUsed, false)) pickupBuffer.fill (0.0f); // 32 KB: solo si la pastilla se usó
        stringHpCoef = 1.0f - juce::MathConstants<float>::twoPi * 20.0f / (float) sr;
    }

    void pushString (float x)
    {
        stringBuffer[(size_t) stringWrite] = x;
        stringWrite = (stringWrite + 1) & (stringSize - 1);
    }

    float renderString (double dt)
    {
        const auto d = juce::jlimit (1.0, (double) stringSize - 8.0, 1.0 / dt - 1.0 - stringS - stringLpTau);
        const auto i = (int) d;
        const auto fr = (float) (d - i);
        auto at = [this] (int k) { return stringBuffer[(size_t) ((stringWrite - 1 - k) & (stringSize - 1))]; };
        const auto xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        const auto y = ((c3 * fr + c2) * fr + c1) * fr + x0;

        auto v = stringRho * ((1.0f - stringS) * y + stringS * stringPrev);
        if (stringLpA > 0.0f) // Damping (ver renderNextBlock)
        {
            stringLp += (1.0f - stringLpA) * (v - stringLp);
            v = stringLp;
        }
        pushString (v);
        stringPrev = y;
        auto out = y;
        if (stringPickupPos > 0.004f) // pastilla: peine 0,5·(y[n] - y[n - γ·N]) (N = periodo), lectura lineal
        {
            stringPickupUsed = true;
            pickupBuffer[(size_t) pickupWrite] = y;
            const auto back = juce::jlimit (1.0, (double) stringSize - 2.0, (double) stringPickupPos / dt);
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
        const auto x = phase * WavetableData::frameSize;
        const auto i = (int) x;
        const auto fr = (float) (x - i);
        const auto* a = wt.table (f0, mip);
        const auto* b = wt.table (f1, mip);
        const auto va = a[i] + fr * (a[i + 1] - a[i]);
        const auto vb = b[i] + fr * (b[i + 1] - b[i]);
        return va + ff * (vb - va);
    }

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

    const Params& params;
    const SampleSlot& sampleSlot;
    const SampleSlot& osc2SampleSlot;
    const WavetableSlot& wavetableSlot;
    double wtPhase = 0.0;
    float bendTarget = 0.0f, modTarget = 0.0f, bendSmoothed = 0.0f, modSmoothed = 0.0f, controlCoef = 1.0f;
    float glideSemis = 0.0f, pitchEnvValue = 0.0f;
    bool pendingLegato = false;
    float polyPressure = 0.0f, channelPressure = 0.0f, pressureSmoothed = 0.0f;
    unsigned lfsr = 1u; // 15 bits, nunca 0 (estado bloqueado del LFSR)
    double lfsrPhase = 0.0;
    SampleReader sample, osc2Sample; // fuente Sample y Osc 2 en modo Sample
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

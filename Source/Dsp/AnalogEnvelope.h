// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE). No es una cabecera independiente.
#pragma once

//==============================================================================
// Envolvente ADSR analógica de curvas RC, según el CEM3310 / AS3310 (datasheet AS3310 v4, características típicas):
//   - ataque: carga RC hacia una asíntota Vz = 7,0 V con pico Vp = 5 V (relación 1,4; rango 6,5-7,5 / 4,7-5,5 V); al llegar
//     al pico pasa a decay. Con relación r, el pico llega en t = τ·ln(r / (r - 1)): τ_A = A / ln(3,5) = A / 1,2528, así que
//     el ataque dura exactamente el tiempo del mando;
//   - decay hacia el sustain y release hacia 0: RC puras ("True RC Envelope Shape").
// El datasheet no define cómo se mide un "tiempo" de decay/release: aquí T = tiempo hasta -40 dB del recorrido
// (τ = T / ln 100), elección de diseño (no medida).
// Reactivar durante una fase: el ataque parte del nivel actual (el condensador no se descarga), como en el circuito.
// Parámetros modificables en cualquier momento (coeficientes por bloque, sin saltos de nivel).
struct AnalogEnvelope
{
    static constexpr double asymptoteRatio = 1.4;   // Vz / Vp típico del AS3310
    static constexpr float silence = 1.0e-5f;       // -100 dB: fin del release

    void setSampleRate (double sr) { fs = sr; }

    void setParameters (float attack, float decay, float sustain, float release)
    {
        const auto coef = [this] (double tau) { return (float) std::exp (-1.0 / (std::max (tau, 1.0e-6) * fs)); };
        aCoef = coef ((double) attack / std::log (asymptoteRatio / (asymptoteRatio - 1.0)));
        dCoef = coef ((double) decay / std::log (100.0));
        rCoef = coef ((double) release / std::log (100.0));
        sustainLevel = juce::jlimit (0.0f, 1.0f, sustain);
    }

    void noteOn() { state = State::attack; }
    void noteOff() { if (state != State::idle) state = State::release; }
    void reset() { state = State::idle; value = 0.0f; }
    bool isActive() const { return state != State::idle; }

    float getNextSample()
    {
        switch (state)
        {
            case State::attack:
                value = (float) asymptoteRatio + (value - (float) asymptoteRatio) * aCoef;
                if (value >= 1.0f) { value = 1.0f; state = State::decay; }
                break;
            case State::decay: // el sustain es la asíntota: sigue sus cambios con la misma constante
                value = sustainLevel + (value - sustainLevel) * dCoef;
                break;
            case State::release:
                value *= rCoef;
                if (value < silence) { value = 0.0f; state = State::idle; }
                break;
            case State::idle:
                break;
        }
        return value;
    }

private:
    enum class State { idle, attack, decay, release };
    double fs = 48000.0;
    float aCoef = 0.0f, dCoef = 0.0f, rCoef = 0.0f, sustainLevel = 1.0f, value = 0.0f;
    State state = State::idle;
};

// Envolvente del motor Synth: lineal (juce::ADSR, la de siempre) o analógica RC según Env Curve. Corren las dos a la vez
// (coste de unas pocas operaciones por muestra) para poder cambiar de curva con la nota sonando
struct SynthEnvelope
{
    void setSampleRate (double sr) { linear.setSampleRate (sr); analog.setSampleRate (sr); }
    void setParameters (const juce::ADSR::Parameters& p, bool analogCurve)
    {
        linear.setParameters (p);
        analog.setParameters (p.attack, p.decay, p.sustain, p.release);
        useAnalog = analogCurve;
    }
    void noteOn() { linear.noteOn(); analog.noteOn(); }
    void noteOff() { linear.noteOff(); analog.noteOff(); }
    void reset() { linear.reset(); analog.reset(); }
    bool isActive() const { return useAnalog ? analog.isActive() : linear.isActive(); }
    float getNextSample()
    {
        const auto l = linear.getNextSample(), a = analog.getNextSample();
        return useAnalog ? a : l;
    }

private:
    juce::ADSR linear;
    AnalogEnvelope analog;
    bool useAnalog = false;
};

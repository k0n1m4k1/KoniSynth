// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Altavoz rotatorio Leslie 122/147. Velocidades (HammondWiki citado en diyAudio + Wikipedia): bocina 48 rpm (chorale) /
// 400 rpm (tremolo), tambor 40 / 340 rpm; crossover pasivo de 800 Hz, 12 dB/oct (Henricksen 1981 + NMC eMuseum). El tambor es
// sobre todo modulación de amplitud entre 200 y 800 Hz (Henricksen + Pekonen et al., DAFx-11). Doppler de la bocina con línea
// de retardo modulada: Δt = (r/c)·cos(θ - φ) (Smith et al., DAFx-02), r = 0,19 m (media de 0,192 setBfree y 0,18 CCRMA: valores
// de emuladores) -> ±40 cents a 400 rpm; tambor r = 0,2 m. Dos micrófonos virtuales a ±90° (estimado). Profundidad de amplitud
// α = 0,9 (Pekonen; perceptual). Rampas: bocina ≈1 s para subir y 1,5 s para bajar, tambor ≈5 / 7 s (DIY y foro: fuentes
// débiles), modeladas como un polo con τ = rampa/3. Previo de válvulas: saturación tanh (sin curva publicada: estimada).
struct LeslieSpeaker
{
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto* b : { &lowLp, &lowLp2 }) b->set (1, 800.0, 0.70710678, sr);
        for (auto* b : { &highHp, &highHp2 }) b->set (2, 800.0, 0.70710678, sr);
        drum200.set (2, 200.0, 0.70710678, sr);
        const auto maxDelay = (int) std::ceil (0.004 * sr) + 4;
        for (auto& l : lines) l.assign ((size_t) maxDelay, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto* b : { &lowLp, &lowLp2, &highHp, &highHp2, &drum200 }) b->clear();
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        write = 0;
        hornAngle = drumAngle = 0.0;
    }

    // speed: 0 parado (freno), 1 chorale, 2 tremolo
    void process (juce::AudioBuffer<float>& buffer, int speed, float drive, float mix)
    {
        const auto hornTarget = speed == 2 ? 400.0 : speed == 1 ? 48.0 : 0.0;   // rpm
        const auto drumTarget = speed == 2 ? 340.0 : speed == 1 ? 40.0 : 0.0;
        const auto hornTau = hornTarget > hornRpm ? 1.0 / 3.0 : 1.5 / 3.0;
        const auto drumTau = drumTarget > drumRpm ? 5.0 / 3.0 : 7.0 / 3.0;
        const auto hornCoef = 1.0 - std::exp (-1.0 / (hornTau * sampleRate));
        const auto drumCoef = 1.0 - std::exp (-1.0 / (drumTau * sampleRate));
        const auto numCh = buffer.getNumChannels();
        const auto size = (int) lines[0].size();
        const auto twoPi = juce::MathConstants<double>::twoPi;
        const auto preGain = 1.0f + 6.0f * drive;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            hornRpm += hornCoef * (hornTarget - hornRpm);
            drumRpm += drumCoef * (drumTarget - drumRpm);
            hornAngle += twoPi * hornRpm / 60.0 / sampleRate;
            drumAngle += twoPi * drumRpm / 60.0 / sampleRate;
            if (hornAngle > twoPi) hornAngle -= twoPi;
            if (drumAngle > twoPi) drumAngle -= twoPi;

            float mono = 0.0f;
            for (int c = 0; c < numCh; ++c) mono += buffer.getSample (c, i);
            mono /= (float) numCh;
            const auto x = std::tanh (preGain * mono) / std::tanh (preGain); // previo de válvulas (estimado)
            const auto low = lowLp2.process (lowLp.process (x));
            const auto high = highHp2.process (highHp.process (x));
            const auto lowMod = drum200.process (low);                     // 200-800 Hz: la parte que el tambor modula
            const auto lowFixed = low - lowMod;
            lines[0][(size_t) write] = (float) high;
            lines[1][(size_t) write] = (float) lowMod;

            float outs[2];
            for (int m = 0; m < 2; ++m)
            {
                const auto phi = m == 0 ? -juce::MathConstants<double>::halfPi : juce::MathConstants<double>::halfPi;
                const auto ch = std::cos (hornAngle - phi), cd = std::cos (drumAngle - phi);
                // Doppler: retardo base 2 ms ± r/c
                const auto hornDelay = (0.002 + 0.19 / 343.0 * ch) * sampleRate;
                const auto drumDelay = (0.002 + 0.20 / 343.0 * cd) * sampleRate;
                const auto h = read (lines[0], size, hornDelay) * (1.0 - 0.45 * (1.0 - ch));  // α = 0,9: 1 - α(1 - cos)/2
                const auto d = read (lines[1], size, drumDelay) * (1.0 - 0.45 * (1.0 - cd));
                outs[m] = (float) (h + d + lowFixed);
            }
            write = (write + 1) % size;
            for (int c = 0; c < numCh; ++c)
            {
                const auto w = numCh == 1 ? 0.5f * (outs[0] + outs[1]) : outs[c & 1];
                const auto dry = buffer.getSample (c, i);
                buffer.setSample (c, i, dry + mix * (w - dry));
            }
        }
    }

private:
    double read (const std::vector<float>& l, int size, double delay) const
    {
        auto pos = write - delay;
        while (pos < 0.0) pos += size;
        const auto i0 = (int) pos;
        const auto f = pos - i0;
        const auto a = l[(size_t) (i0 % size)], b = l[(size_t) ((i0 + 1) % size)];
        return a + f * (b - a);
    }

    double sampleRate = 44100.0, hornAngle = 0.0, drumAngle = 0.0, hornRpm = 48.0, drumRpm = 40.0;
    Biquad lowLp, lowLp2, highHp, highHp2, drum200;
    std::array<std::vector<float>, 2> lines;
    int write = 0;
};

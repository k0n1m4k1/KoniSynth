// Extraído como cabecera: se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (usa BlepBuffer).
#pragma once

//==============================================================================
// Paso de la tasa del chip a la del host como un DAC sin sobremuestreo: cada muestra se retiene hasta la siguiente (ZOH) y
// la forma escalonada se reconstruye con escalones de banda limitada (BlepBuffer, como en el DX7 y los chips de 8 bits), como
// si se grabara la salida analógica. Conserva lo que la retención tiene y un sinc de bajada quita: la caída sinc propia
// (-3,9 dB a Nyquist del chip) y las imágenes por encima de él hasta la Nyquist del host. Supuesto: DAC con retención, lo
// habitual en la electrónica de consumo de la época; no medido en estas consolas. Misma interfaz que SincResampler.
// Latencia: 15,5 muestras del host (0,32 ms a 48 kHz).
struct ZohResampler
{
    static constexpr int kChunk = 32;

    void prepare (double inRate, double outRate)
    {
        period = outRate / inRate; // muestras del host por muestra del chip
        for (auto& b : blep) b.prepare (kChunk);
        reset();
    }

    void reset()
    {
        for (auto& b : blep) b.reset();
        last = { 0.0f, 0.0f };
        nextChip = 0.0;
        pos = kChunk;
    }

    template <typename Produce>
    float next (Produce&& produce)
    {
        return nextStereo ([&] { const auto s = produce(); return std::array<float, 2> { s, s }; })[0];
    }

    template <typename Produce>
    std::array<float, 2> nextStereo (Produce&& produce)
    {
        if (pos == kChunk)
        {
            // Muestras del chip que caen en este trozo: escalón en su instante exacto (con fracción de muestra del host)
            while (nextChip < (double) kChunk)
            {
                const auto s = produce();
                for (int c = 0; c < 2; ++c)
                    if (s[(size_t) c] != last[(size_t) c])
                    {
                        blep[(size_t) c].addDelta (nextChip, s[(size_t) c] - last[(size_t) c]);
                        last[(size_t) c] = s[(size_t) c];
                    }
                nextChip += period;
            }
            nextChip -= (double) kChunk;
            blep[0].read (kChunk, out[0].data());
            blep[1].read (kChunk, out[1].data());
            pos = 0;
        }
        const std::array<float, 2> y { out[0][(size_t) pos], out[1][(size_t) pos] };
        ++pos;
        return y;
    }

private:
    std::array<BlepBuffer, 2> blep;
    std::array<std::array<float, kChunk>, 2> out {};
    std::array<float, 2> last { 0.0f, 0.0f };
    double period = 1.0, nextChip = 0.0;
    int pos = kChunk;
};

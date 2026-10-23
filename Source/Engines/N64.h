// Motor N64: la consola no tiene chip de sonido; el RSP mezcla por microcódigo y la AI saca PCM de 16 bits estéreo. Se
// emula la cadena de la librería de audio de Nintendo (libultra, descompilación decompals/ultralib) y del microcódigo
// (mupen64plus-rsp-hle, alist.c / audio.c):
//  - Samples VADPCM: tramas de 9 bytes = 16 muestras (escala + índice de predictor, 16 nibbles), predictor de orden 2 con
//    libro de 8×2 vectores (Q11) y la suma de residuos de la trama (adpcm_compute_residuals). El libro se deriva de los
//    coeficientes del predictor (respuesta a la muestra anterior y a la anterior a ella); el codificador es propio:
//    4 predictores (sin predicción, LPC global y 2 centros k-means) y búsqueda de escala en lazo cerrado.
//  - Remuestreador del RSP: 4 taps × 64 fases (RESAMPLE_LUT del microcódigo), tono Q16.16, razón máxima 1,99996 (libultra
//    MAX_RATIO): una nota que pida más de una octava por encima de la tasa del sample se queda ahí, como en la consola.
//  - Envolvente de libultra: rampas exponenciales de volumen (env.c, _getRate: (destino/actual)^(1/n), mínimo 1/32767):
//    ataque hasta Attack Vol, decay hasta Decay Vol, release a 0. Paneo y envío al efecto con la tabla eqpower[128].
//  - Efectos de libultra (drvrnew.c, reverb.c): Small Room, Big Room, Echo, Chorus y Flange con sus parámetros exactos:
//    una línea de retardo con secciones (entrada, salida, fb, ff, ganancia, chorus, paso bajo de 1 polo); entrada 0,707·(L+R)
//    y salida del efecto mono (se copia de R a L).
// Salida a 22,05 / 32 / 44,1 kHz (tasa de la AI) y remuestreo a la tasa del host (SincResampler, ≤ 0,36 ms a 22 kHz).
// Fuentes: los 6 samples propios (Dsp/WaveSources.h) a la tasa elegida o el sample del usuario (panel Sample).
#pragma once

struct N64Engine
{
    static constexpr int kVoices = 16;

    void prepare (double sr, int maxBlock)
    {
        hostRate = sr;
        (void) maxBlock;
        outRateIndex = -1; // se configura en el primer render
        reset();
    }

    void reset()
    {
        for (auto& v : voice) v = {};
        fxLine.assign (fxLine.size(), 0.0f);
        fxWrite = 0;
        for (auto& s : fxSection) s.reset();
        resampler.reset();
        zoh.reset();
        age = 0;
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, const SampleData* userSample, int rootNote)
    {
        readParams (p);
        configureRates();
        if (sourceRateIndex != builtRateIndex || adpcmOn != builtAdpcm)
            buildSources();
        prepareUserSource (userSample, p);
        userRootNote = rootNote;
        configureFx();
        bendSmoothed += 0.2f * (p.pitchBend.load() - bendSmoothed);
        tuneCents = (double) p.fineTune->load() + 100.0 * bendSmoothed;
        for (auto& v : voice)
            if (v.note >= 0)
                v.pitch = pitchFor (v.note, v.source);

        const auto smooth = (int) p.n64Dac->load() == 1;
        if (smooth != wasSmooth) { wasSmooth = smooth; resampler.reset(); zoh.reset(); }
        auto midiIt = midi.cbegin();
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                handleMidi ((*midiIt).getMessage());
            const auto y = smooth ? resampler.nextStereo ([this] { return aiSample(); }) : zoh.nextStereo ([this] { return aiSample(); });
            if (out.getNumChannels() >= 2)
            {
                out.addSample (0, i, y[0]);
                out.addSample (1, i, y[1]);
            }
            else if (out.getNumChannels() == 1)
            {
                out.addSample (0, i, 0.5f * (y[0] + y[1]));
            }
        }
    }

private:
    //--------------------------------------------------------------------------------------------------------------------------
    // Tablas del hardware y de libultra
    static const int16_t* resampleLut()
    {
        // RESAMPLE_LUT del microcódigo de audio (mupen64plus-rsp-hle, audio.c): 64 fases × 4 taps, Q15
        static const int16_t lut[256] = {
            0x0c39, 0x66ad, 0x0d46, -0x0021, 0x0b39, 0x6696, 0x0e5f, -0x0028, 0x0a44, 0x6669, 0x0f83, -0x0030, 0x095a, 0x6626, 0x10b4, -0x0038,
            0x087d, 0x65cd, 0x11f0, -0x0041, 0x07ab, 0x655e, 0x1338, -0x004a, 0x06e4, 0x64d9, 0x148c, -0x0054, 0x0628, 0x643f, 0x15eb, -0x005f,
            0x0577, 0x638f, 0x1756, -0x006a, 0x04d1, 0x62cb, 0x18cb, -0x0076, 0x0435, 0x61f3, 0x1a4c, -0x0082, 0x03a4, 0x6106, 0x1bd7, -0x008f,
            0x031c, 0x6007, 0x1d6c, -0x009c, 0x029f, 0x5ef5, 0x1f0b, -0x00aa, 0x022a, 0x5dd0, 0x20b3, -0x00b8, 0x01be, 0x5c9a, 0x2264, -0x00c6,
            0x015b, 0x5b53, 0x241e, -0x00d4, 0x0101, 0x59fc, 0x25e0, -0x00e2, 0x00ae, 0x5896, 0x27a9, -0x00f0, 0x0063, 0x5720, 0x297a, -0x00fe,
            0x001f, 0x559d, 0x2b50, -0x010c, -0x001e, 0x540d, 0x2d2c, -0x0118, -0x0054, 0x5270, 0x2f0d, -0x0125, -0x0084, 0x50c7, 0x30f3, -0x0130,
            -0x00ad, 0x4f14, 0x32dc, -0x013a, -0x00d2, 0x4d57, 0x34c8, -0x0143, -0x00f1, 0x4b91, 0x36b6, -0x014a, -0x010b, 0x49c2, 0x38a5, -0x0150,
            -0x0121, 0x47ed, 0x3a95, -0x0154, -0x0132, 0x4611, 0x3c85, -0x0155, -0x0140, 0x4430, 0x3e74, -0x0154, -0x014a, 0x424a, 0x4060, -0x0151,
            -0x0151, 0x4060, 0x424a, -0x014a, -0x0154, 0x3e74, 0x4430, -0x0140, -0x0155, 0x3c85, 0x4611, -0x0132, -0x0154, 0x3a95, 0x47ed, -0x0121,
            -0x0150, 0x38a5, 0x49c2, -0x010b, -0x014a, 0x36b6, 0x4b91, -0x00f1, -0x0143, 0x34c8, 0x4d57, -0x00d2, -0x013a, 0x32dc, 0x4f14, -0x00ad,
            -0x0130, 0x30f3, 0x50c7, -0x0084, -0x0125, 0x2f0d, 0x5270, -0x0054, -0x0118, 0x2d2c, 0x540d, -0x001e, -0x010c, 0x2b50, 0x559d, 0x001f,
            -0x00fe, 0x297a, 0x5720, 0x0063, -0x00f0, 0x27a9, 0x5896, 0x00ae, -0x00e2, 0x25e0, 0x59fc, 0x0101, -0x00d4, 0x241e, 0x5b53, 0x015b,
            -0x00c6, 0x2264, 0x5c9a, 0x01be, -0x00b8, 0x20b3, 0x5dd0, 0x022a, -0x00aa, 0x1f0b, 0x5ef5, 0x029f, -0x009c, 0x1d6c, 0x6007, 0x031c,
            -0x008f, 0x1bd7, 0x6106, 0x03a4, -0x0082, 0x1a4c, 0x61f3, 0x0435, -0x0076, 0x18cb, 0x62cb, 0x04d1, -0x006a, 0x1756, 0x638f, 0x0577,
            -0x005f, 0x15eb, 0x643f, 0x0628, -0x0054, 0x148c, 0x64d9, 0x06e4, -0x004a, 0x1338, 0x655e, 0x07ab, -0x0041, 0x11f0, 0x65cd, 0x087d,
            -0x0038, 0x10b4, 0x6626, 0x095a, -0x0030, 0x0f83, 0x6669, 0x0a44, -0x0028, 0x0e5f, 0x6696, 0x0b39, -0x0021, 0x0d46, 0x66ad, 0x0c39 };
        return lut;
    }

    static int eqpower (int i)
    {
        // libultra env.c: eqpower[128] (Q15), paneo y mezcla seco/efecto a potencia constante
        static const int16_t table[128] = {
            32767, 32764, 32757, 32744, 32727, 32704, 32677, 32644, 32607, 32564, 32517, 32464, 32407, 32344, 32277, 32205,
            32127, 32045, 31958, 31866, 31770, 31668, 31561, 31450, 31334, 31213, 31087, 30957, 30822, 30682, 30537, 30388,
            30234, 30075, 29912, 29744, 29572, 29395, 29214, 29028, 28838, 28643, 28444, 28241, 28033, 27821, 27605, 27385,
            27160, 26931, 26698, 26461, 26220, 25975, 25726, 25473, 25216, 24956, 24691, 24423, 24151, 23875, 23596, 23313,
            23026, 22736, 22442, 22145, 21845, 21541, 21234, 20924, 20610, 20294, 19974, 19651, 19325, 18997, 18665, 18331,
            17993, 17653, 17310, 16965, 16617, 16266, 15913, 15558, 15200, 14840, 14477, 14113, 13746, 13377, 13006, 12633,
            12258, 11881, 11503, 11122, 10740, 10357, 9971, 9584, 9196, 8806, 8415, 8023, 7630, 7235, 6839, 6442,
            6044, 5646, 5246, 4845, 4444, 4042, 3640, 3237, 2833, 2429, 2025, 1620, 1216, 810, 405, 0 };
        return table[juce::jlimit (0, 127, i)];
    }

    static int clamp16 (int v) { return juce::jlimit (-32768, 32767, v); }

    //--------------------------------------------------------------------------------------------------------------------------
    // VADPCM
    struct Source
    {
        std::vector<uint8_t> frames;                  // tramas de 9 bytes (o PCM de 16 bits si ADPCM está apagado)
        std::vector<int16_t> pcm;                     // fuente sin comprimir (modo ADPCM apagado)
        std::array<std::array<int16_t, 16>, 4> book {}; // por predictor: book1[8] (muestra -2), book2[8] (muestra -1), Q11
        bool adpcm = true, loop = true;
        double rootHz = 1000.0, rate = 22050.0;
        size_t numSamples() const { return adpcm ? frames.size() / 9 * 16 : pcm.size(); }
    };

    // Libro de un predictor x[n] = c1·x[n-1] + c2·x[n-2]: respuesta a x[-2] = 1 (book1) y a x[-1] = 1 (book2) en 8 pasos
    static void makeBook (double c1, double c2, std::array<int16_t, 16>& entry)
    {
        double a1 = 0.0, a2 = 1.0, b1 = 1.0, b2 = 0.0; // (x[-1], x[-2]) de cada respuesta
        for (int i = 0; i < 8; ++i)
        {
            const auto g = c1 * a1 + c2 * a2, h = c1 * b1 + c2 * b2;
            entry[(size_t) i] = (int16_t) clamp16 ((int) std::lround (g * 2048.0));
            entry[(size_t) (8 + i)] = (int16_t) clamp16 ((int) std::lround (h * 2048.0));
            a2 = a1; a1 = g;
            b2 = b1; b1 = h;
        }
    }

    // Media trama de 8 muestras (adpcm_compute_residuals del microcódigo); residuo r[i] = nibble·2^min(escala, 12)
    static void decodeHalf (const std::array<int16_t, 16>& book, const int* r, int l1, int l2, int* y)
    {
        for (int i = 0; i < 8; ++i)
        {
            int acc = r[i] * 2048 + book[(size_t) i] * l1 + book[(size_t) (8 + i)] * l2;
            for (int j = 0; j < i; ++j)
                acc += book[(size_t) (8 + (i - 1 - j))] * r[j];
            y[i] = clamp16 (acc >> 11);
        }
    }

    // Predictor de orden 2 por mínimos cuadrados sobre x (con sus 2 muestras previas); estable (polos dentro del círculo)
    static std::pair<double, double> lpc2 (const int* x, int n)
    {
        double r00 = 0, r01 = 0, r11 = 0, p0 = 0, p1 = 0;
        for (int i = 2; i < n; ++i)
        {
            const double a = x[i - 1], b = x[i - 2], t = x[i];
            r00 += a * a; r01 += a * b; r11 += b * b; p0 += t * a; p1 += t * b;
        }
        const auto det = r00 * r11 - r01 * r01;
        if (std::abs (det) < 1.0e-6 * (r00 * r11 + 1.0))
            return { r00 > 0 ? juce::jlimit (-1.9, 1.9, p0 / r00) : 0.0, 0.0 };
        auto c1 = (p0 * r11 - p1 * r01) / det, c2 = (p1 * r00 - p0 * r01) / det;
        c2 = juce::jlimit (-0.99, 0.99, c2);
        c1 = juce::jlimit (-(1.0 - c2) * 0.999, (1.0 - c2) * 0.999, c1);
        return { c1, c2 };
    }

    static Source encode (const std::vector<int>& input, bool loop, double rootHz, double rate, bool adpcm)
    {
        Source src;
        src.rootHz = rootHz;
        src.rate = rate;
        src.loop = loop;
        src.adpcm = adpcm;
        auto x = input;
        x.resize ((x.size() + 15) / 16 * 16, 0);
        if (x.empty()) x.assign (16, 0);
        if (! adpcm)
        {
            src.pcm.resize (x.size());
            for (size_t i = 0; i < x.size(); ++i) src.pcm[i] = (int16_t) clamp16 (x[i]);
            return src;
        }
        // Predictores: 0 = sin predicción; 1 = LPC de todo el sample; 2-3 = k-means de los LPC por trama (ponderados por energía)
        const auto numFrames = (int) x.size() / 16;
        std::vector<std::pair<double, double>> frameCoef ((size_t) numFrames);
        std::vector<double> frameEnergy ((size_t) numFrames);
        std::vector<int> win (18);
        for (int f = 0; f < numFrames; ++f)
        {
            for (int k = 0; k < 18; ++k)
            {
                const auto idx = f * 16 - 2 + k;
                win[(size_t) k] = idx >= 0 ? x[(size_t) idx] : 0;
            }
            frameCoef[(size_t) f] = lpc2 (win.data(), 18);
            double e = 0; for (int k = 2; k < 18; ++k) e += (double) win[(size_t) k] * win[(size_t) k];
            frameEnergy[(size_t) f] = e;
        }
        std::array<std::pair<double, double>, 4> coef { std::pair<double, double> { 0.0, 0.0 }, lpc2 (x.data(), (int) x.size()), {}, {} };
        coef[2] = { coef[1].first * 0.8, coef[1].second * 0.8 };
        coef[3] = { coef[1].first * 1.1, coef[1].second };
        for (int it = 0; it < 8; ++it)
        {
            std::array<double, 4> w {}, s1 {}, s2 {};
            for (int f = 0; f < numFrames; ++f)
            {
                const auto& c = frameCoef[(size_t) f];
                int best = 2;
                double bestD = 1e300;
                for (int k = 2; k < 4; ++k)
                {
                    const auto d = std::pow (c.first - coef[(size_t) k].first, 2) + std::pow (c.second - coef[(size_t) k].second, 2);
                    if (d < bestD) { bestD = d; best = k; }
                }
                w[(size_t) best] += frameEnergy[(size_t) f] + 1.0;
                s1[(size_t) best] += (frameEnergy[(size_t) f] + 1.0) * c.first;
                s2[(size_t) best] += (frameEnergy[(size_t) f] + 1.0) * c.second;
            }
            for (int k = 2; k < 4; ++k)
                if (w[(size_t) k] > 0) coef[(size_t) k] = { s1[(size_t) k] / w[(size_t) k], s2[(size_t) k] / w[(size_t) k] };
        }
        for (int k = 0; k < 4; ++k)
            makeBook (coef[(size_t) k].first, coef[(size_t) k].second, src.book[(size_t) k]);

        // Tramas: mejor (predictor, escala) por error cuadrático, simulando el decodificador
        int last[16] {};
        for (int f = 0; f < numFrames; ++f)
        {
            const auto* target = &x[(size_t) f * 16];
            double bestErr = 1e300;
            int bestPred = 0, bestScale = 0, bestNib[16] {}, bestOut[16] {};
            for (int pred = 0; pred < 4; ++pred)
                for (int scale = 0; scale <= 12; ++scale)
                {
                    const auto& book = src.book[(size_t) pred];
                    int nib[16], r[16], out[16];
                    double err = 0;
                    int l1 = last[14], l2 = last[15];
                    for (int half = 0; half < 2 && err < bestErr; ++half)
                    {
                        for (int i = 0; i < 8; ++i)
                        {
                            // Predicción con los residuos ya elegidos de esta media trama
                            int acc = book[(size_t) i] * l1 + book[(size_t) (8 + i)] * l2;
                            for (int j = 0; j < i; ++j)
                                acc += book[(size_t) (8 + (i - 1 - j))] * r[half * 8 + j];
                            const auto pred1 = acc >> 11;
                            auto n = juce::jlimit (-8, 7, (int) std::lround ((double) (target[half * 8 + i] - pred1) / (1 << scale)));
                            // Vecino si da menos error tras el recorte a 16 bits
                            int bestN = n, bestV = 0;
                            double bestE = 1e300;
                            for (const auto cand : { n - 1, n, n + 1 })
                            {
                                if (cand < -8 || cand > 7) continue;
                                const auto v = clamp16 ((cand * (1 << scale) * 2048 + acc) >> 11);
                                const auto e = std::pow ((double) (v - target[half * 8 + i]), 2);
                                if (e < bestE) { bestE = e; bestN = cand; bestV = v; }
                            }
                            nib[half * 8 + i] = bestN;
                            r[half * 8 + i] = bestN * (1 << scale);
                            out[half * 8 + i] = bestV;
                            err += bestE;
                        }
                        l1 = out[half * 8 + 6];
                        l2 = out[half * 8 + 7];
                    }
                    if (err < bestErr)
                    {
                        bestErr = err; bestPred = pred; bestScale = scale;
                        std::copy (nib, nib + 16, bestNib);
                        std::copy (out, out + 16, bestOut);
                    }
                }
            src.frames.push_back ((uint8_t) ((bestScale << 4) | bestPred));
            for (int k = 0; k < 16; k += 2)
                src.frames.push_back ((uint8_t) (((bestNib[k] & 15) << 4) | (bestNib[k + 1] & 15)));
            std::copy (bestOut, bestOut + 16, last);
        }
        return src;
    }

    //--------------------------------------------------------------------------------------------------------------------------
    struct Voice
    {
        int note = -1, source = 0;
        bool gate = false, active = false;
        long long age = 0;
        // Decodificador
        size_t frame = 0;
        int decoded[16] {}, last[16] {}, pos = 16; // pos: siguiente muestra de 'decoded'
        bool ended = false;
        // Remuestreador: 4 muestras de entrada más recientes y acumulador Q16
        std::array<int, 4> hist {};
        uint32_t accu = 0, pitch = 0x10000;
        // Envolvente (libultra): volumen actual y rampa exponencial hacia el destino
        double vol = 1.0, target = 1.0, ratio = 1.0;
        int remaining = 0, stage = 0; // 0 ataque, 1 decay, 2 sostenido, 3 release
        double peak = 32767.0;
        int panL = 23026, panR = 23026;
    };

    void readParams (const Params& p)
    {
        sourceIndex = juce::jlimit (0, kNumWaveSources, (int) p.n64Source->load());
        sourceRateIndex = juce::jlimit (0, 4, (int) p.n64SourceRate->load());
        newOutRateIndex = juce::jlimit (0, 2, (int) p.n64OutputRate->load());
        adpcmOn = p.n64Adpcm->load() > 0.5f;
        attackMs = p.n64Attack->load(); decayMs = p.n64Decay->load(); releaseMs = p.n64Release->load();
        attackVol = (int) p.n64AttackVolume->load(); decayVol = (int) p.n64DecayVolume->load();
        volume = (int) p.n64Volume->load();
        pan = (int) p.n64Pan->load();
        spread = p.n64Spread->load();
        fxType = juce::jlimit (0, 5, (int) p.n64Fx->load());
        fxMix = (int) p.n64FxMix->load();
        velocityOn = p.n64Velocity->load() > 0.5f;
    }

    void configureRates()
    {
        if (newOutRateIndex == outRateIndex)
            return;
        outRateIndex = newOutRateIndex;
        static const double rates[3] = { 22050.0, 32000.0, 44100.0 };
        outRate = rates[outRateIndex];
        resampler.prepare (outRate, hostRate);
        zoh.prepare (outRate, hostRate);
        builtFx = -1; // los coeficientes del chorus dependen de la tasa
        for (auto& v : voice) v = {};
    }

    static double sourceRate (int index)
    {
        static const double rates[5] = { 8000.0, 11025.0, 16000.0, 22050.0, 32000.0 };
        return rates[juce::jlimit (0, 4, index)];
    }

    void buildSources()
    {
        builtRateIndex = sourceRateIndex;
        builtAdpcm = adpcmOn;
        const auto rate = sourceRate (sourceRateIndex);
        for (int k = 0; k < kNumWaveSources; ++k)
        {
            const auto w = makeWaveSource (k, rate);
            sources[(size_t) k] = encode (w.pcm, true, w.rootHz, rate, adpcmOn);
        }
        userSource = nullptr; // fuerza recodificar el del usuario a la nueva tasa
    }

    // Sample del usuario: región del panel Sample a la tasa de fuente elegida; máximo 5 s
    void prepareUserSource (const SampleData* sample, const Params& p)
    {
        const auto start = p.sampleStart->load(), lengthMs = p.sampleLength->load();
        const auto loop = p.sampleLoop->load() > 0.5f;
        const auto length = sample != nullptr ? sample->length() : 0;
        if (sample == userSource && length == userSourceLength && start == userStart && lengthMs == userLengthMs && loop == userLoop
            && sourceRateIndex == userRateIndex && adpcmOn == userAdpcm)
            return;
        userSource = sample; userSourceLength = length; userStart = start; userLengthMs = lengthMs; userLoop = loop;
        userRateIndex = sourceRateIndex; userAdpcm = adpcmOn;
        sources[kNumWaveSources] = {};
        sources[kNumWaveSources].frames.clear();
        if (sample == nullptr || length < 16)
            return;
        const auto rate = sourceRate (sourceRateIndex);
        const auto ratio = sample->sampleRate / rate;
        const auto level = juce::jlimit (0, SampleData::maxLevels - 1, (int) std::floor (std::log2 (std::max (1.0, ratio))));
        const auto scale = ratio / std::exp2 ((double) level);
        const auto first = (double) start * length / std::exp2 ((double) level);
        auto count = (int) ((1.0 - start) * length / ratio);
        if (lengthMs > 0.0f)
            count = std::min (count, (int) (lengthMs / 1000.0 * rate));
        count = std::min (count, (int) (5.0 * rate));
        std::vector<int> pcm ((size_t) std::max (0, count));
        for (int i = 0; i < count; ++i)
            pcm[(size_t) i] = clamp16 ((int) std::lround (24000.0 * sample->read (level, first + i * scale)));
        sources[kNumWaveSources] = encode (pcm, loop, 0.0, rate, adpcmOn);
    }

    uint32_t pitchFor (int note, int source) const
    {
        const auto& src = sources[(size_t) source];
        const auto rootHz = source == kNumWaveSources ? 440.0 * std::exp2 ((userRootNote - 69) / 12.0) : src.rootHz;
        const auto f = 440.0 * std::exp2 ((note - 69) / 12.0 + tuneCents / 1200.0) * KoniTuning::ratio (note);
        const auto ratio = std::min (1.99996, f / rootHz * src.rate / outRate); // MAX_RATIO de libultra
        return (uint32_t) std::lround (ratio * 65536.0);
    }

    // Rampa exponencial de libultra hacia 'target' (volumen 0..32767, mínimo 1) en 'ms' milisegundos
    void ramp (Voice& v, double target, double ms) const
    {
        v.target = std::max (1.0, target);
        v.vol = std::max (1.0, v.vol);
        v.remaining = std::max (0, (int) std::lround (ms * 0.001 * outRate));
        v.ratio = v.remaining > 0 ? std::pow (v.target / v.vol, 1.0 / v.remaining) : 1.0;
        if (v.remaining == 0) v.vol = v.target;
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            int target = -1;
            for (int k = 0; k < kVoices; ++k)
                if (voice[(size_t) k].note == m.getNoteNumber()) target = k;
            if (target < 0)
            {
                long long best = std::numeric_limits<long long>::max();
                for (int pass = 0; pass < 2 && target < 0; ++pass)
                    for (int k = 0; k < kVoices; ++k)
                        if ((pass == 1 || ! voice[(size_t) k].gate) && voice[(size_t) k].age < best)
                        {
                            best = voice[(size_t) k].age;
                            target = k;
                        }
            }
            auto& v = voice[(size_t) target];
            v = Voice {};
            v.note = m.getNoteNumber();
            v.source = sourceIndex;
            v.gate = v.active = ! sources[(size_t) v.source].frames.empty() || ! sources[(size_t) v.source].pcm.empty();
            v.age = ++age;
            v.pitch = pitchFor (v.note, v.source);
            // Volumen del canal × velocidad (driver de KoniSynth) × Attack Vol; paneo con eqpower (0 = izquierda, 127 = derecha)
            v.peak = 32767.0 * volume / 127.0 * (velocityOn ? m.getFloatVelocity() : 1.0f);
            const auto voicePan = juce::jlimit (0, 127, pan + (int) std::lround (spread * 63.0f * ((age & 1) ? 1.0f : -1.0f)));
            v.panL = eqpower (voicePan);
            v.panR = eqpower (127 - voicePan);
            v.vol = 1.0;
            ramp (v, v.peak * attackVol / 127.0, attackMs);
        }
        else if (m.isNoteOff())
        {
            for (auto& v : voice)
                if (v.note == m.getNoteNumber() && v.gate)
                {
                    v.gate = false;
                    v.stage = 3;
                    ramp (v, 0.0, releaseMs);
                }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voice)
            {
                v.gate = false;
                v.stage = 3;
                ramp (v, 0.0, m.isAllSoundOff() ? 0.0 : releaseMs);
                if (m.isAllSoundOff()) v.active = false;
            }
        }
    }

    int nextSourceSample (Voice& v)
    {
        const auto& src = sources[(size_t) v.source];
        if (! src.adpcm)
        {
            if (v.ended || src.pcm.empty()) return 0;
            const auto s = src.pcm[v.frame++];
            if (v.frame >= src.pcm.size()) { if (src.loop) v.frame = 0; else v.ended = true; }
            return s;
        }
        if (v.pos >= 16)
        {
            if (v.ended || src.frames.empty()) return 0;
            const auto* f = &src.frames[v.frame * 9];
            const auto scale = std::min (12, f[0] >> 4);
            const auto& book = src.book[(size_t) (f[0] & 3)];
            int r[16];
            for (int k = 0; k < 8; ++k)
            {
                const auto hi = f[1 + k] >> 4, lo = f[1 + k] & 15;
                r[2 * k] = (hi >= 8 ? hi - 16 : hi) * (1 << scale);
                r[2 * k + 1] = (lo >= 8 ? lo - 16 : lo) * (1 << scale);
            }
            decodeHalf (book, r, v.last[14], v.last[15], v.decoded);
            decodeHalf (book, r + 8, v.decoded[6], v.decoded[7], v.decoded + 8);
            std::copy (v.decoded, v.decoded + 16, v.last);
            v.pos = 0;
            if (++v.frame >= src.frames.size() / 9)
            {
                if (src.loop) { v.frame = 0; std::fill (std::begin (v.last), std::end (v.last), 0); } // bucle al inicio (estado 0)
                else v.ended = true;
            }
        }
        return v.decoded[v.pos++];
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // Efectos de libultra (drvrnew.c): por sección entrada, salida, fbcoef, ffcoef, ganancia, chorus rate, chorus depth, filtro
    struct FxSection
    {
        int input = 0, output = 0;
        float fb = 0, ff = 0, gain = 0;
        bool chorus = false, lowpass = false;
        double rsinc = 0, rsgain = 0, rsval = 1.0, readOffset = 0;
        float lpCoef = 0, lpGain = 1, lpState = 0;
        void reset() { rsval = 1.0; readOffset = 0; lpState = 0; }
    };

    void configureFx()
    {
        if (fxType == builtFx)
            return;
        builtFx = fxType;
        constexpr int ms = 40; // macro 'ms' de libultra: (s32) 44.1 & ~7 = 40 muestras
        struct Row { int input, output, fb, ff, gain, rate, depth, filter; };
        static const std::vector<Row> rooms[6] = {
            {},
            { { 0, 54 * ms, 9830, -9830, 0, 0, 0, 0 }, { 19 * ms, 38 * ms, 3276, -3276, 0x3fff, 0, 0, 0 }, { 0, 60 * ms, 5000, 0, 0, 0, 0, 0x5000 } },
            { { 0, 66 * ms, 9830, -9830, 0, 0, 0, 0 }, { 22 * ms, 54 * ms, 3276, -3276, 0x3fff, 0, 0, 0 },
              { 66 * ms, 91 * ms, 3276, -3276, 0x3fff, 0, 0, 0 }, { 0, 94 * ms, 8000, 0, 0, 0, 0, 0x5000 } },
            { { 0, 179 * ms, 12000, 0, 0x7fff, 0, 0, 0 } },
            { { 0, 5 * ms, 0x4000, 0, 0x7fff, 7600, 700, 0 } },
            { { 0, 5 * ms, 0, 0x5fff, 0x7fff, 380, 500, 0 } } };
        static const int lengths[6] = { 1, 100 * ms, 100 * ms, 200 * ms, 20 * ms, 20 * ms };
        fxLine.assign ((size_t) lengths[fxType], 0.0f);
        fxWrite = 0;
        numSections = 0;
        for (const auto& row : rooms[fxType])
        {
            auto& s = fxSection[(size_t) numSections++];
            s = {};
            s.input = row.input; s.output = row.output;
            s.fb = (int16_t) row.fb / 32768.0f; s.ff = (int16_t) row.ff / 32768.0f; s.gain = (int16_t) row.gain / 32768.0f;
            if (row.rate != 0)
            {
                s.chorus = true;
                s.rsinc = row.rate / 1000.0 * 2.0 / outRate;                       // RANGE = 2
                s.rsgain = row.depth / 173123.404906676 * (row.output - row.input);  // centésimas de cent -> muestras
            }
            if (row.filter != 0)
            {
                // _init_lpfilter: fc = fc·16384 >> 15 (Q14), ganancia = 16384 - fc: y = g·x + c·y[-1]
                const auto fc = (row.filter * 16384) >> 15;
                s.lowpass = true;
                s.lpCoef = fc / 16384.0f;
                s.lpGain = (16384 - fc) / 16384.0f;
            }
        }
    }

    float lineAt (double pos) const // lectura con la interpolación de 4 taps del RSP
    {
        const auto n = (int) fxLine.size();
        auto base = (int) std::floor (pos);
        const auto frac = pos - base;
        const auto* lut = resampleLut() + (((int) (frac * 65536.0) & 0xfc00) >> 8);
        float acc = 0.0f;
        for (int k = 0; k < 4; ++k)
            acc += fxLine[(size_t) (((base - 1 + k) % n + n) % n)] * lut[k] / 32768.0f;
        return acc;
    }

    float processFx (float in)
    {
        const auto n = (int) fxLine.size();
        if (numSections == 0 || n <= 1)
            return 0.0f;
        const auto wrap = [n] (int i) { return (size_t) (((i % n) + n) % n); };
        fxLine[(size_t) fxWrite] = in;
        float out = 0.0f;
        for (int k = 0; k < numSections; ++k)
        {
            auto& s = fxSection[(size_t) k];
            const auto inPos = wrap (fxWrite - s.input), outPos = wrap (fxWrite - s.output);
            auto b1 = fxLine[inPos];
            float b2;
            if (s.chorus)
            {
                // Triángulo de libultra (_doModFunc) -> razón de lectura 1 - delta/longitud (cuantizada a 1/32768)
                s.rsval += s.rsinc;
                if (s.rsval > 2.0) s.rsval -= 4.0;
                const auto delta = s.rsgain * (std::abs (s.rsval) - 1.0);
                const auto fratio = 1.0 - std::floor (delta / (s.output - s.input) * 32768.0) / 32768.0;
                s.readOffset += fratio - 1.0;
                b2 = lineAt ((double) fxWrite - s.output + s.readOffset);
            }
            else
            {
                b2 = fxLine[outPos];
            }
            if (s.ff != 0.0f)
            {
                b2 += s.ff * b1;
                if (! s.chorus && ! s.lowpass) fxLine[outPos] = b2;
            }
            if (s.fb != 0.0f)
            {
                b1 += s.fb * b2;
                fxLine[inPos] = b1;
            }
            if (s.lowpass)
                b2 = s.lpState = s.lpGain * b2 + s.lpCoef * s.lpState;
            if (! s.chorus)
                fxLine[outPos] = b2;
            if (s.gain != 0.0f)
                out += s.gain * b2;
        }
        fxWrite = (fxWrite + 1) % n;
        return out;
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // Una muestra a la tasa de la AI
    std::array<float, 2> aiSample()
    {
        const auto dry = eqpower (fxMix), wet = eqpower (127 - fxMix);
        double dryL = 0, dryR = 0, aux = 0;
        const auto* lut = resampleLut();
        for (auto& v : voice)
        {
            if (! v.active)
                continue;
            // Remuestreador del RSP: salida con las 4 muestras más recientes y la fase del acumulador
            const auto* l = lut + ((v.accu & 0xfc00) >> 8);
            const auto s = clamp16 ((v.hist[0] * l[0] + v.hist[1] * l[1] + v.hist[2] * l[2] + v.hist[3] * l[3]) >> 15);
            v.accu += v.pitch;
            for (auto adv = v.accu >> 16; adv > 0; --adv)
                v.hist = { v.hist[1], v.hist[2], v.hist[3], nextSourceSample (v) };
            v.accu &= 0xffff;
            // Envolvente
            if (v.remaining > 0)
            {
                v.vol *= v.ratio;
                if (--v.remaining == 0)
                {
                    v.vol = v.target;
                    if (v.stage == 0) { v.stage = 1; ramp (v, v.peak * decayVol / 127.0, decayMs); }
                    else if (v.stage == 1) { v.stage = 2; }
                    else if (v.stage == 3) { v.active = false; continue; }
                }
            }
            const auto amp = s * v.vol / 32768.0;
            const auto l2 = amp * v.panL / 32768.0, r2 = amp * v.panR / 32768.0;
            dryL += l2 * dry / 32768.0;
            dryR += r2 * dry / 32768.0;
            aux += (l2 + r2) * wet / 32768.0;
        }
        // Efecto: entrada 0,707·(auxL + auxR), salida mono a los dos lados
        const auto fx = processFx ((float) (0.70710678 * aux));
        return { (float) (juce::jlimit (-32768.0, 32767.0, dryL + fx) / 32768.0),
                 (float) (juce::jlimit (-32768.0, 32767.0, dryR + fx) / 32768.0) };
    }

    double hostRate = 44100.0, outRate = 32000.0;
    SincResampler resampler; // DAC "Smooth"
    ZohResampler zoh;        // DAC "Hold" (por defecto)
    bool wasSmooth = false;
    std::array<Source, kNumWaveSources + 1> sources {};
    std::array<Voice, kVoices> voice {};
    std::vector<float> fxLine = std::vector<float> (1, 0.0f);
    std::array<FxSection, 4> fxSection {};
    int fxWrite = 0, numSections = 0, builtFx = -1;
    int sourceIndex = 0, sourceRateIndex = 3, builtRateIndex = -1, outRateIndex = -1, newOutRateIndex = 1;
    bool adpcmOn = true, builtAdpcm = true, velocityOn = true;
    float attackMs = 5, decayMs = 0, releaseMs = 200, spread = 0;
    int attackVol = 127, decayVol = 127, volume = 100, pan = 64, fxType = 0, fxMix = 0;
    double tuneCents = 0.0;
    float bendSmoothed = 0.0f;
    int userRootNote = 60;
    long long age = 0;
    const SampleData* userSource = nullptr;
    int userSourceLength = -1, userRateIndex = -1;
    float userStart = -1.0f, userLengthMs = -1.0f;
    bool userLoop = false, userAdpcm = true;
};

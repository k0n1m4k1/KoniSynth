// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
struct SynthSound : juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Filtro ladder de 4 polos tipo Moog en forma TPT/ZDF (Zavalishin, "The Art of VA Filter Design", cap. 5).
// Cada etapa: y = G·x + s/(1+g), G = g/(1+g), g = tan(π·fc/fs). El lazo se resuelve linealmente:
//   y4 = (G⁴·x + S) / (1 + k·G⁴),  S = (G³·s1 + G²·s2 + G·s3 + s4) / (1+g)
// y después se satura u = tanh(x - k·y4): la resonancia queda acotada y con k >= 4 autooscila sin explotar.
// Salidas mezcladas al estilo Oberheim Xpander a partir de u, y1..y4.
struct Ladder
{
    enum Mode { lp24, lp12, bp12, hp12 };

    void reset() { s = {}; }

    float process (float x, float g, float k, int mode)
    {
        const auto G   = g / (1.0f + g);
        const auto inv = 1.0f / (1.0f + g);
        const auto G2 = G * G, G3 = G2 * G, G4 = G3 * G;

        const auto S  = inv * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
        const auto y4 = (G4 * x + S) / (1.0f + k * G4);
        const auto u  = std::tanh (x - k * y4);

        auto stage = [G] (float in, float& st)
        {
            const auto v = G * (in - st);
            const auto y = v + st;
            st = y + v;
            return y;
        };

        const auto y1 = stage (u, s[0]);
        const auto y2 = stage (y1, s[1]);
        const auto y3 = stage (y2, s[2]);
        const auto out4 = stage (y3, s[3]);

        switch (mode)
        {
            case lp12: return y2;
            case bp12: return 2.0f * (y1 - y2);
            case hp12: return u - 2.0f * y1 + y2;
            default:   return out4;
        }
    }

    std::array<float, 4> s {};
};

// FIR de media banda de 31 coeficientes (ventana de Kaiser, β = 7), fase lineal, ganancia DC 1. Lo usan el diezmador
// del oscilador complejo y los niveles diezmados del sample.
static const std::array<float, 31>& halfbandCoefficients()
{
    static const auto h = []
    {
        constexpr int taps = 31;
        std::array<float, taps> c {};
        const auto m = (taps - 1) / 2.0;
        const auto beta = 7.0;
        auto i0 = [] (double x) // Bessel I0 por serie
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
            return sum;
        };
        double total = 0.0;
        for (int n = 0; n < taps; ++n)
        {
            const auto k = n - m;
            const auto sinc = k == 0.0 ? 0.5 : std::sin (juce::MathConstants<double>::pi * k / 2.0) / (juce::MathConstants<double>::pi * k);
            c[(size_t) n] = (float) (sinc * i0 (beta * std::sqrt (1.0 - (k / m) * (k / m))) / i0 (beta));
            total += c[(size_t) n];
        }
        for (auto& v : c) v = (float) (v / total); // ganancia DC exactamente 1
        return c;
    }();
    return h;
}

// Sample cargado (WAV/AIFF/FLAC...): mono (media de canales), normalizado a pico 1, máx. 60 s.
// levels[L] = versión diezmada 2^L veces (FIR de media banda centrado, fase cero): al transponer hacia arriba la voz
// lee el nivel con relación de lectura ≤ 1,06, así los armónicos por encima de Nyquist están filtrados antes de leer.
struct SampleData
{
    static constexpr int maxLevels = 5; // nivel 4 = 1/16: hasta +4 octavas sin aliasing por transposición
    std::array<std::vector<float>, maxLevels> levels;
    double sampleRate = 44100.0;
    juce::String name;
    bool truncated = false;
    // Datos del formato de origen (Import/SampleImport.h): bucle nativo en muestras del nivel 0 (-1 = sin bucle), nota raíz
    // (-1 = desconocida) y, si venía en BRR (.brr, .spc), los bloques originales para que el motor SNES los toque sin recodificar
    int loopStart = -1, loopEnd = -1, rootKey = -1;
    std::vector<uint8_t> brr;
    int brrLoopBlock = -1;

    static std::unique_ptr<SampleData> fromFile (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Unsupported or unreadable audio file: " + file.getFileName();
            return nullptr;
        }

        auto data = std::make_unique<SampleData>();
        data->sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        data->name = file.getFileName();
        const auto maxLength = (juce::int64) (60.0 * data->sampleRate);
        const auto length = (int) juce::jmin (reader->lengthInSamples, maxLength);
        data->truncated = reader->lengthInSamples > maxLength;
        if (length < 4)
        {
            error = "The file is empty: " + file.getFileName();
            return nullptr;
        }

        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, length);
        reader->read (&buffer, 0, length, 0, true, channels > 1);

        auto& mono = data->levels[0];
        mono.resize ((size_t) length);
        for (int i = 0; i < length; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                s += buffer.getSample (ch, i);
            mono[(size_t) i] = s / (float) channels;
        }
        data->buildLevels();
        return data;
    }

    // Normaliza el nivel 0 a pico 1 y genera los niveles diezmados
    void buildLevels()
    {
        auto& mono = levels[0];
        float peak = 0.0f;
        for (const auto s : mono)
            peak = juce::jmax (peak, std::abs (s));
        if (peak > 0.0f)
            for (auto& s : mono)
                s /= peak;

        const auto& h = halfbandCoefficients();
        for (int L = 1; L < maxLevels; ++L)
        {
            const auto& src = levels[(size_t) L - 1];
            auto& dst = levels[(size_t) L];
            dst.resize (src.size() / 2);
            for (size_t n = 0; n < dst.size(); ++n)
            {
                float acc = 0.0f;
                for (int k = 0; k < 31; ++k)
                {
                    const auto idx = (long long) (2 * n) + k - 15; // centrado: fase cero
                    if (h[(size_t) k] != 0.0f && idx >= 0 && idx < (long long) src.size())
                        acc += h[(size_t) k] * src[(size_t) idx];
                }
                dst[n] = acc;
            }
        }
    }

    // Hermite de 4 puntos (Catmull-Rom) en el nivel L; pos en muestras de ese nivel. Fuera de rango: borde repetido.
    float read (int L, double pos) const
    {
        const auto& d = levels[(size_t) L];
        const auto n = (int) d.size();
        const auto i = (int) pos;
        const auto f = (float) (pos - i);
        auto at = [&] (int k) { return d[(size_t) juce::jlimit (0, n - 1, k)]; };
        const auto xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    int length() const { return (int) levels[0].size(); }
};

// Intercambio de datos (sample, wavetable) entre hilos sin bloqueos en audio: el hilo de audio solo lee 'current'. Lo sustituido
// se retira y se libera cuando el audio ha completado 2 bloques desde el cambio (blocksDone, que incrementa el final de
// processBlock): ningún bloque que pudiera tener el puntero viejo sigue en curso. Sin audio, se libera al destruir.
// La UI lee con withCurrent(), bajo el mismo cerrojo que collect(): el dato no puede liberarse mientras lo dibuja.
template <typename T>
struct DataSlot
{
    std::atomic<const T*> current { nullptr };
    std::atomic<juce::uint64> blocksDone { 0 };

    void replace (std::unique_ptr<T> data)
    {
        const juce::ScopedLock sl (lock);
        auto* old = current.exchange (data.get());
        if (data != nullptr)
            owned.push_back (std::move (data));
        if (old != nullptr)
            retired.push_back ({ old, blocksDone.load() });
        collect();
    }

    // Hilo de mensajes (temporizador) o el que cargue
    void collect()
    {
        const juce::ScopedLock sl (lock);
        const auto done = blocksDone.load();
        for (auto it = retired.begin(); it != retired.end();)
        {
            if (done >= it->epoch + 2)
            {
                const auto* p = it->data;
                owned.erase (std::remove_if (owned.begin(), owned.end(), [p] (const auto& u) { return u.get() == p; }), owned.end());
                it = retired.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

    template <typename Fn>
    void withCurrent (Fn&& fn) const
    {
        const juce::ScopedLock sl (lock);
        fn (current.load());
    }

private:
    struct Retired { const T* data; juce::uint64 epoch; };
    juce::CriticalSection lock;
    std::vector<std::unique_ptr<T>> owned;
    std::vector<Retired> retired;
};

using SampleSlot = DataSlot<SampleData>;

// Wavetable: fotogramas de 2048 muestras (formato de las wavetables de Serum: WAV de n × 2048 muestras, estándar de facto que
// leen otros sintetizadores; kvraudio.com, forum.vital.audio). Por fotograma, 11 niveles limitados en banda por FFT: el nivel L
// conserva los armónicos 1..1024 >> L (sin DC). La voz elige el nivel cuyo armónico más alto queda bajo Nyquist: sin aliasing por
// transposición. Cada tabla lleva una muestra de guarda (copia de la 0) para la interpolación lineal. Pico global normalizado a 1.
struct WavetableData
{
    static constexpr int frameSize = 2048, numLevels = 11, maxFrames = 256;
    int numFrames = 0;
    std::vector<float> data;
    juce::String name;

    const float* table (int frame, int level) const
    {
        return data.data() + ((size_t) frame * numLevels + (size_t) level) * (frameSize + 1);
    }

    // frames: numFrames × 2048 muestras consecutivas
    static std::unique_ptr<WavetableData> fromFrames (const std::vector<float>& frames, int count, const juce::String& name)
    {
        auto wt = std::make_unique<WavetableData>();
        wt->numFrames = juce::jlimit (1, maxFrames, count);
        wt->name = name;
        wt->data.assign ((size_t) wt->numFrames * numLevels * (frameSize + 1), 0.0f);

        juce::dsp::FFT fft (11); // 2^11 = 2048
        std::vector<float> spectrum ((size_t) frameSize * 2), work ((size_t) frameSize * 2);
        float globalPeak = 1.0e-9f;
        for (int f = 0; f < wt->numFrames; ++f)
        {
            const auto* src = frames.data() + (size_t) f * frameSize;
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);
            std::copy (src, src + frameSize, spectrum.begin());
            fft.performRealOnlyForwardTransform (spectrum.data());
            spectrum[0] = spectrum[1] = 0.0f; // sin DC

            float scale = 1.0f;
            for (int level = 0; level < numLevels; ++level)
            {
                const auto limit = 1024 >> level;
                work = spectrum;
                for (int k = limit + 1; k <= frameSize / 2; ++k)
                    work[(size_t) (2 * k)] = work[(size_t) (2 * k + 1)] = 0.0f;
                fft.performRealOnlyInverseTransform (work.data());

                if (level == 0) // escala de la inversa medida sobre el nivel completo (independiente del motor de FFT)
                {
                    float peakIn = 1.0e-9f, peakOut = 1.0e-9f;
                    for (int i = 0; i < frameSize; ++i)
                    {
                        peakIn = juce::jmax (peakIn, std::abs (src[i]));
                        peakOut = juce::jmax (peakOut, std::abs (work[(size_t) i]));
                    }
                    scale = peakIn / peakOut;
                }
                auto* dst = const_cast<float*> (wt->table (f, level));
                for (int i = 0; i < frameSize; ++i)
                {
                    dst[i] = work[(size_t) i] * scale;
                    if (level == 0)
                        globalPeak = juce::jmax (globalPeak, std::abs (dst[i]));
                }
                dst[frameSize] = dst[0];
            }
        }
        for (auto& v : wt->data)
            v /= globalPeak;
        return wt;
    }

    // Tablas incluidas (64 fotogramas), generadas en el dominio del tiempo; los mipmaps quitan el aliasing de las formas ingenuas.
    //  0 Basic Shapes: seno -> triángulo -> sierra -> cuadrada (interpolación entre las 4 formas)
    //  1 PWM Sweep: pulso con ciclo de 50 % a 2 %
    //  2 Harmonic Sweep: suma aditiva de los armónicos 1..k de una sierra (1/h), k de 1 a 256 en escala exponencial
    //  3 Sync Sweep: sierra esclava sincronizada, relación de 1 a 8
    //  4 Formant Sweep: armónicos con una campana gaussiana (σ = 1,5 armónicos) cuyo centro recorre los armónicos 1..40
    static std::unique_ptr<WavetableData> builtIn (int index)
    {
        static const char* names[] = { "Basic Shapes", "PWM Sweep", "Harmonic Sweep", "Sync Sweep", "Formant Sweep" };
        constexpr int count = 64;
        std::vector<float> frames ((size_t) count * frameSize);
        const auto twoPi = juce::MathConstants<double>::twoPi;
        for (int f = 0; f < count; ++f)
        {
            const auto t = (double) f / (count - 1);
            auto* out = frames.data() + (size_t) f * frameSize;
            for (int i = 0; i < frameSize; ++i)
            {
                const auto ph = (double) i / frameSize;
                double v = 0.0;
                switch (index)
                {
                    case 1: v = ph < 0.5 - 0.48 * t ? 1.0 : -1.0; break;
                    case 2:
                    {
                        const auto harmonics = (int) std::round (std::pow (256.0, t));
                        for (int h = 1; h <= harmonics; ++h)
                            v += std::sin (twoPi * h * ph) / h;
                        break;
                    }
                    case 3: { const auto ratio = 1.0 + 7.0 * t; v = 2.0 * std::fmod (ph * ratio, 1.0) - 1.0; break; }
                    case 4:
                    {
                        const auto centre = 1.0 + 39.0 * t;
                        for (int h = 1; h <= 64; ++h)
                            v += std::exp (-0.5 * std::pow ((h - centre) / 1.5, 2.0)) * std::sin (twoPi * h * ph);
                        break;
                    }
                    default:
                    {
                        const auto sine = std::sin (twoPi * ph);
                        const auto tri  = 1.0 - 4.0 * std::abs (ph - 0.5);
                        const auto saw  = 2.0 * ph - 1.0;
                        const auto sq   = ph < 0.5 ? 1.0 : -1.0;
                        const auto seg = t * 3.0;
                        const auto k = juce::jmin (2, (int) seg);
                        const auto u = seg - k;
                        const double shapes[4] = { sine, tri, saw, sq };
                        v = (1.0 - u) * shapes[k] + u * shapes[k + 1];
                        break;
                    }
                }
                out[i] = (float) v;
            }
        }
        return fromFrames (frames, count, names[juce::jlimit (0, 4, index)]);
    }

    // Tamaño de fotograma declarado en el chunk "clm " de un WAV (el que escribe Serum: texto ASCII "<!>2048 10000000 ...",
    // tamaño en decimal tras "<!>"; formato según Surge XT, src/common/WAVFileSupport.cpp: export_wt_wav_portable lo escribe
    // así y load_wt_wav_portable lo lee, aunque solo acepta 2048). 0 si no es WAV, no tiene el chunk o el tamaño no es válido
    static int clmFrameSize (const juce::File& file)
    {
        juce::FileInputStream in (file);
        char id[4] {};
        if (! in.openedOk() || in.read (id, 4) != 4 || std::memcmp (id, "RIFF", 4) != 0)
            return 0;
        in.readInt(); // tamaño del RIFF
        if (in.read (id, 4) != 4 || std::memcmp (id, "WAVE", 4) != 0)
            return 0;
        while (in.read (id, 4) == 4)
        {
            const auto size = (juce::int64) (juce::uint32) in.readInt(); // little-endian, como todo RIFF
            const auto next = in.getPosition() + size + (size & 1);      // los chunks se alinean a 2 bytes
            if (std::memcmp (id, "clm ", 4) == 0)
            {
                char data[256] {};
                const auto got = in.read (data, (int) juce::jmin<juce::int64> (size, (juce::int64) sizeof (data) - 1));
                const auto text = juce::String::fromUTF8 (data, juce::jmax (0, got));
                const auto n = text.startsWith ("<!>") ? text.substring (3).getIntValue() : 0;
                return n >= 16 && n <= 16384 ? n : 0;
            }
            if (! in.setPosition (next))
                return 0;
        }
        return 0;
    }

    // Un ciclo de n muestras a 2048. Potencia de 2: por FFT (exacto para una señal periódica: los armónicos 1..min(n, 2048)/2 - 1
    // se conservan tal cual; de más de 2048 se descartan los que no caben). Si no, interpolación lineal
    static std::vector<float> resampleCycle (const float* src, int n)
    {
        std::vector<float> out ((size_t) frameSize);
        if (juce::isPowerOfTwo (n))
        {
            const auto order = juce::roundToInt (std::log2 ((double) n));
            juce::dsp::FFT fwd (order), inv (11);
            std::vector<float> a ((size_t) n * 2, 0.0f), b ((size_t) frameSize * 2, 0.0f);
            std::copy (src, src + n, a.begin());
            fwd.performRealOnlyForwardTransform (a.data());
            const auto bins = juce::jmin (n, frameSize) / 2; // sin el bin de Nyquist del más corto
            const auto gain = (float) frameSize / (float) n;  // misma amplitud tras la inversa normalizada (1/N)
            for (int k = 0; k < bins; ++k)
            {
                b[(size_t) (2 * k)] = a[(size_t) (2 * k)] * gain;
                b[(size_t) (2 * k + 1)] = a[(size_t) (2 * k + 1)] * gain;
            }
            inv.performRealOnlyInverseTransform (b.data());
            std::copy (b.begin(), b.begin() + frameSize, out.begin());
            return out;
        }
        for (int i = 0; i < frameSize; ++i)
        {
            const auto pos = (double) i * n / frameSize;
            const auto k = (int) pos;
            const auto fr = (float) (pos - k);
            out[(size_t) i] = src[k] + fr * (src[(k + 1) % n] - src[k]);
        }
        return out;
    }

    // WAV/AIFF/FLAC: n fotogramas (máx. 256) de 2048 muestras, o del tamaño que declare el chunk "clm " (clmFrameSize; cada
    // fotograma se remuestrea a 2048). Sin chunk: menos de 2048 muestras, o una longitud que no es múltiplo de 2048 sin llegar
    // a 2 fotogramas, es un único ciclo, remuestreado linealmente a 2048. Canales promediados.
    static std::unique_ptr<WavetableData> fromFile (const juce::File& file, juce::String& error)
    {
        const auto clm = clmFrameSize (file);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Unsupported or unreadable audio file: " + file.getFileName();
            return nullptr;
        }
        const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) juce::jmax (frameSize, clm) * maxFrames);
        if (length < 16)
        {
            error = "The file is too short to be a wavetable: " + file.getFileName();
            return nullptr;
        }
        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, length);
        reader->read (&buffer, 0, length, 0, true, channels > 1);
        std::vector<float> mono ((size_t) length);
        for (int i = 0; i < length; ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                v += buffer.getSample (ch, i);
            mono[(size_t) i] = v / (float) channels;
        }

        if (clm > 0 && clm != frameSize && length >= clm)
        {
            const auto count = length / clm;
            std::vector<float> frames;
            frames.reserve ((size_t) count * frameSize);
            for (int f = 0; f < count; ++f)
            {
                const auto cycle = resampleCycle (mono.data() + (size_t) f * clm, clm);
                frames.insert (frames.end(), cycle.begin(), cycle.end());
            }
            return fromFrames (frames, count, file.getFileName());
        }
        if (length >= 2 * frameSize || length % frameSize == 0)
        {
            const auto count = length / frameSize;
            mono.resize ((size_t) count * frameSize);
            return fromFrames (mono, count, file.getFileName());
        }
        std::vector<float> cycle ((size_t) frameSize);
        for (int i = 0; i < frameSize; ++i)
        {
            const auto pos = (double) i * length / frameSize;
            const auto k = (int) pos;
            const auto fr = (float) (pos - k);
            cycle[(size_t) i] = mono[(size_t) k] + fr * (mono[(size_t) ((k + 1) % length)] - mono[(size_t) k]);
        }
        return fromFrames (cycle, 1, file.getFileName());
    }
};

using WavetableSlot = DataSlot<WavetableData>;

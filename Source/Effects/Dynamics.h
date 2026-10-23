// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, Eq). No es una cabecera independiente.
#pragma once

//==============================================================================
// Dinámica: modelador de transitorios y compresor (bloque DYNAMICS, tras el EQ) y limitador (final de OUTPUT, tras el Gain).
// Todo en estéreo enlazado (un solo detector con el máximo de los canales: la imagen no se desplaza al comprimir).
namespace Dyn
{
inline float dbOf (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); } // suelo -120 dB
inline float gainOf (float db) { return std::pow (10.0f, db / 20.0f); }
// Coeficiente de un filtro de un polo con constante de tiempo ms (63 % del salto en ms)
inline float coefFor (double ms, double fs) { return ms <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * fs)); }

// Curva estática: reducción de ganancia (dB, >= 0) para un nivel de detector x (dB). Rodilla suave cuadrática de ancho
// knee centrada en el umbral (Giannoulis, Massberg y Reiss, "Digital Dynamic Range Compressor Design", JAES 2012, ec. 4).
// Continua en ambos bordes de la rodilla: en d = ±knee/2 vale 0 y slope·knee/2.
inline float gainReductionDb (float x, float thr, float ratio, float knee)
{
    const auto slope = 1.0f - 1.0f / ratio;
    const auto d = x - thr;
    if (2.0f * d <= -knee) return 0.0f;
    if (knee > 0.0f && 2.0f * std::abs (d) < knee)
    {
        const auto t = d + 0.5f * knee;
        return slope * t * t / (2.0f * knee);
    }
    return slope * d;
}
} // namespace Dyn

//==============================================================================
// Modelador de transitorios (diseño de envolvente diferencial, el del SPL Transient Designer: independiente del nivel,
// sin umbral). Dos pares de detectores de pico sobre max|x|, en dB:
//   ataque:  rápido (0,5 ms / 30 ms) - lento (25 ms / 30 ms) > 0 solo durante el ataque de cada nota;
//   sostén:  lento (0,5 ms / 400 ms) - rápido (0,5 ms / 40 ms) > 0 solo durante la caída.
// ganancia (dB) = Attack · min(dif. ataque, 12) + Sustain · min(dif. sostén, 12), con Attack y Sustain en [-1, 1]:
// ±12 dB como máximo en cada uno. Latencia 0.
struct TransientShaper
{
    void prepare (double sr)
    {
        fs = sr;
        aFast = Dyn::coefFor (0.5, fs); aSlow = Dyn::coefFor (25.0, fs); rAtk = Dyn::coefFor (30.0, fs);
        rSusFast = Dyn::coefFor (40.0, fs); rSusSlow = Dyn::coefFor (400.0, fs);
        reset();
    }

    void reset() { fast = slow = susFast = susSlow = floorDb; }

    void process (juce::AudioBuffer<float>& b, float attack, float sustain)
    {
        const auto chans = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < chans; ++ch) peak = std::max (peak, std::abs (b.getSample (ch, i)));
            const auto x = std::max (floorDb, Dyn::dbOf (peak));
            follow (fast, x, aFast, rAtk);
            follow (slow, x, aSlow, rAtk);
            follow (susFast, x, aFast, rSusFast);
            follow (susSlow, x, aFast, rSusSlow);
            const auto db = attack * juce::jlimit (0.0f, 12.0f, fast - slow) + sustain * juce::jlimit (0.0f, 12.0f, susSlow - susFast);
            const auto g = Dyn::gainOf (db);
            for (int ch = 0; ch < chans; ++ch) b.setSample (ch, i, b.getSample (ch, i) * g);
        }
    }

private:
    static void follow (float& e, float x, float a, float r) { e = x > e ? a * e + (1.0f - a) * x : r * e + (1.0f - r) * x; }

    static constexpr float floorDb = -80.0f; // por debajo, silencio: sin él la diferencia desde -120 dB sería enorme
    double fs = 48000.0;
    float aFast = 0, aSlow = 0, rAtk = 0, rSusFast = 0, rSusSlow = 0;
    float fast = floorDb, slow = floorDb, susFast = floorDb, susSlow = floorDb;
};

//==============================================================================
// Compresor de tres caracteres (modelos de comportamiento, no circuitos medidos):
//   VCA  - alimentación hacia delante, detector de pico, la curva y los tiempos tal cual (dbx 160 / SSL de bus: preciso).
//   Opto - detector cuadrático (RMS, 5 ms), rodilla mínima de 12 dB y liberación dependiente del programa: la constante
//          crece hasta ×10 con la compresión sostenida (memoria de 2 s al cargar, 5 s al descargar), como la célula T4 del
//          LA-2A (liberación rápida tras picos cortos, lenta tras compresión larga).
//   FET  - realimentado: el detector mira la salida ya comprimida (topología del 1176), así la relación efectiva es más
//          suave y el ataque puede bajar a 20 µs. Con la llave externa (Audio In) pasa a alimentación hacia delante.
// Detector con suavizado de ramas en el dominio logarítmico (Giannoulis 2012, "smooth branching"): ataque y liberación
// independientes. Filtro paso alto Butterworth de 12 dB/oct en la cadena lateral (SC HPF; 20 Hz = apagado): los graves no
// bombean. Llave externa: la entrada de audio (ducking: la voz o un bombo agachan el sintetizador). Mix = compresión
// en paralelo (seco + comprimido). Latencia 0.
struct Compressor
{
    enum Mode { vca = 0, opto, fet };

    struct Settings
    {
        int mode = vca;
        float thresholdDb = -18.0f, ratio = 4.0f, attackMs = 10.0f, releaseMs = 150.0f, kneeDb = 6.0f, makeupDb = 0.0f,
              mix = 1.0f, scHpf = 20.0f;
        bool external = false;
    };

    void prepare (double sr) { fs = sr; reset(); }

    void reset()
    {
        env = 0.0f; rms = 0.0f; memory = 0.0f; lastGain = 1.0f;
        for (auto& z : hp) z = { 0.0, 0.0 };
    }

    // key: entrada de audio en mono (llave externa), numSamples muestras
    void process (juce::AudioBuffer<float>& b, const float* key, const Settings& s)
    {
        const auto chans = juce::jmin (2, b.getNumChannels());
        const auto hpOn = s.scHpf > 20.5f;
        const auto hpc = Eq::highPass (s.scHpf, 0.70710678, fs);
        const auto feedback = s.mode == fet && ! s.external;
        const auto knee = s.mode == opto ? std::max (s.kneeDb, 12.0f) : s.kneeDb;
        const auto cA = Dyn::coefFor (std::max (s.attackMs, 0.02f), fs);
        const auto cRms = Dyn::coefFor (5.0, fs);
        const auto cMemUp = Dyn::coefFor (2000.0, fs), cMemDown = Dyn::coefFor (5000.0, fs);
        const auto makeup = Dyn::gainOf (s.makeupDb);
        auto cR = Dyn::coefFor (s.releaseMs, fs);
        float grMax = 0.0f, lvMax = -120.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float level = 0.0f;
            const auto nIn = s.external ? 1 : chans;
            for (int ch = 0; ch < nIn; ++ch)
            {
                auto v = (double) (s.external ? key[i] : b.getSample (ch, i));
                if (hpOn)
                {
                    auto& z = hp[(size_t) ch];
                    const auto y = hpc.b0 * v + z.first;
                    z.first = hpc.b1 * v - hpc.a1 * y + z.second;
                    z.second = hpc.b2 * v - hpc.a2 * y;
                    v = y;
                }
                level = std::max (level, (float) std::abs (v));
            }
            if (feedback) level *= lastGain;
            if (s.mode == opto)
            {
                rms = cRms * rms + (1.0f - cRms) * level * level;
                level = std::sqrt (2.0f * rms); // ×√2: con un seno, el mismo nivel que el detector de pico
                const auto tgt = std::min (env / 12.0f, 1.0f); // 12 dB de reducción sostenida = memoria llena
                memory += (1.0f - (tgt > memory ? cMemUp : cMemDown)) * (tgt - memory);
                cR = Dyn::coefFor (s.releaseMs * (1.0f + 9.0f * memory), fs);
            }
            const auto xDb = Dyn::dbOf (level);
            const auto target = Dyn::gainReductionDb (xDb, s.thresholdDb, s.ratio, knee);
            env = target > env ? cA * env + (1.0f - cA) * target : cR * env + (1.0f - cR) * target;
            lastGain = Dyn::gainOf (-env);
            const auto g = (1.0f - s.mix) + s.mix * lastGain * makeup;
            for (int ch = 0; ch < chans; ++ch) b.setSample (ch, i, b.getSample (ch, i) * g);
            grMax = std::max (grMax, env);
            lvMax = std::max (lvMax, xDb);
        }
        meterGr.store (grMax, std::memory_order_relaxed);
        meterLevel.store (lvMax, std::memory_order_relaxed);
    }

    std::atomic<float> meterGr { 0.0f }, meterLevel { -120.0f }; // para la UI: reducción máxima y nivel del detector del bloque

private:
    double fs = 48000.0;
    float env = 0.0f, rms = 0.0f, memory = 0.0f, lastGain = 1.0f;
    std::array<std::pair<double, double>, 2> hp {};
};

//==============================================================================
// Limitador de pico de muestra al final de la cadena (tras Gain y Mono): |salida| <= techo, garantizado.
//   Sin anticipación (Off, latencia 0): la ganancia baja en la misma muestra que el pico (puede añadir algo de distorsión
//   en picos rápidos, es el precio de la latencia 0) y se recupera con la liberación.
//   Con anticipación L muestras: ganancia requerida g = techo / pico, mínimo deslizante sobre L + 1 muestras, liberación
//   (cae al instante, sube exponencial) y media móvil de L muestras: la ganancia llega a su valor en una rampa de L muestras
//   que termina justo cuando el pico (retrasado L) sale. Cada valor de la media es <= g del pico (todos sus términos lo
//   son), así que el techo se cumple sin recortar. Latencia L (se comunica al host).
// Es pico de muestra, no "true peak" (los picos entre muestras de la reconstrucción pueden superar el techo ~0,5-3 dB).
struct Limiter
{
    static constexpr double maxLookaheadMs = 5.0;

    void prepare (double sr)
    {
        fs = sr;
        size = (int) std::ceil (maxLookaheadMs * 0.001 * fs) + 2;
        for (auto& d : delay) d.assign ((size_t) size, 0.0f);
        minVal.assign ((size_t) size, 1.0f); minIdx.assign ((size_t) size, 0);
        box.assign ((size_t) size, 1.0f);
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        head = tail = 0; n = 0; pos = 0; boxSum = 0.0; rel = 1.0f; lastL = -1;
    }

    static int lookaheadSamples (double ms, double fs) { return (int) std::round (ms * 0.001 * fs); }

    void process (juce::AudioBuffer<float>& b, float ceilingDb, float releaseMs, int L)
    {
        L = juce::jlimit (0, size - 2, L);
        if (L != lastL) { reset(); lastL = L; boxSum = (double) L; }
        const auto chans = juce::jmin (2, b.getNumChannels());
        const auto ceil = Dyn::gainOf (ceilingDb);
        const auto cR = Dyn::coefFor (releaseMs, fs);
        float grMax = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i, ++n)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < chans; ++ch) peak = std::max (peak, std::abs (b.getSample (ch, i)));
            const auto req = peak > ceil ? ceil / peak : 1.0f;
            float g;
            if (L == 0)
            {
                rel = req < rel ? req : cR * rel + (1.0f - cR) * req;
                g = rel;
                for (int ch = 0; ch < chans; ++ch) b.setSample (ch, i, b.getSample (ch, i) * g);
            }
            else
            {
                // mínimo deslizante (cola monótona creciente): O(1) amortizado
                while (tail != head && minVal[(size_t) prev (tail)] >= req) tail = prev (tail);
                minVal[(size_t) tail] = req; minIdx[(size_t) tail] = n; tail = next (tail);
                while (minIdx[(size_t) head] <= n - (L + 1)) head = next (head);
                const auto held = minVal[(size_t) head];
                rel = held < rel ? held : cR * rel + (1.0f - cR) * held;
                boxSum += (double) rel - (double) box[(size_t) (pos % L)];
                box[(size_t) (pos % L)] = rel;
                g = (float) (boxSum / L);
                const auto wr = pos % L; // línea de retardo de L muestras: se lee lo escrito hace L muestras y se sobrescribe
                for (int ch = 0; ch < chans; ++ch)
                {
                    auto& d = delay[(size_t) ch];
                    const auto x = b.getSample (ch, i);
                    const auto out = d[(size_t) wr];
                    d[(size_t) wr] = x;
                    // la media acumulada en doble puede pasarse ~1e-12: el jlimit lo absorbe (no recorta nada audible)
                    b.setSample (ch, i, juce::jlimit (-ceil, ceil, out * g));
                }
                ++pos;
            }
            grMax = std::max (grMax, -Dyn::dbOf (g));
        }
        meterGr.store (grMax, std::memory_order_relaxed);
    }

    std::atomic<float> meterGr { 0.0f };

private:
    int next (int k) const { return k + 1 == size ? 0 : k + 1; }
    int prev (int k) const { return k == 0 ? size - 1 : k - 1; }

    double fs = 48000.0;
    int size = 2, head = 0, tail = 0, lastL = -1;
    juce::int64 n = 0, pos = 0;
    double boxSum = 0.0;
    float rel = 1.0f;
    std::array<std::vector<float>, 2> delay;
    std::vector<float> minVal, box;
    std::vector<juce::int64> minIdx;
};

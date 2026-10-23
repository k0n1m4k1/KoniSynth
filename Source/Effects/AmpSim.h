// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, juce_dsp, Eq). No es una cabecera independiente.
#pragma once

//==============================================================================
// Ampli de válvulas + pantalla (bloque AMP, tras COLOR y antes de DYNAMICS).
//   Previo: etapas de triodo (12AX7) de comportamiento: ganancia, función de transferencia asimétrica (conducción de rejilla
//     suave en positivo, corte en negativo con más margen: armónicos pares) y condensador de acoplo (paso alto de 1.er orden).
//   Tone stack: el circuito TMB de Fender/Marshall con los valores de componentes de cada ampli; función de transferencia
//     de 3.er orden en forma cerrada de D. T. Yeh y J. O. Smith, "Discretization of the '59 Fender Bassman Tone Stack",
//     DAFx-06 (graves con potenciómetro logarítmico: l = e^{3,4·(L-1)}), discretizada por transformada bilineal.
//     Verificada contra un análisis nodal del circuito: 0,00000 dB de diferencia (scratchpad tonestack_mna2.py).
//     Valores de componentes: tonestacks.lib del proyecto Guitarix / Faust (solo los datos; el código es propio).
//   Etapa de potencia push-pull: saturación simétrica con "sag" (caída de la fuente: el margen baja con la envolvente de la
//     salida, más con rectificador de válvula) y Presence (estante de agudos en el lazo de realimentación negativa).
//   Las partes no lineales van sobremuestreadas ×4 (semibanda IIR polifásica: latencia de pocas muestras, se comunica).
//   Pantalla: aproximaciones con filtros de la respuesta típica de cada tipo de caja (no medidas de una caja concreta) o la
//   IR del usuario (convolución de latencia 0, juce::dsp::Convolution).
// Etapas y sag por modelo: elección de comportamiento a partir de la estructura conocida de cada esquema (número de etapas
// antes/después del tone stack, rectificador), no medidas de un ampli real.
namespace Amp
{
struct Stack { double c1, c2, c3, r1, r2, r3, r4; };

struct Model
{
    const char* name;
    Stack stack;
    int stagesPre, stagesPost; // etapas de triodo antes y después del tone stack
    double stageDb;            // ganancia de cada etapa con Gain al 100 %
    double sag;                // 0 = rectificador de estado sólido, 1 = de válvula y fuente blanda
};

// Valores de tone stack: tonestacks.lib (Guitarix / Faust), comprobados nombre a nombre
inline const std::array<Model, 6>& models()
{
    static const std::array<Model, 6> m { {
        { "Bassman 59",  { 250e-12, 20e-9,  20e-9,  250e3, 1e6,   25e3, 56e3 },  2, 0, 30.0, 0.9 }, // 5F6-A: GZ34, stack tras el seguidor
        { "Twin 69",     { 120e-12, 100e-9, 47e-9,  250e3, 250e3, 10e3, 100e3 }, 1, 1, 26.0, 0.2 }, // AA270: stack tras V1a, rectif. sólido
        { "JCM800",      { 470e-12, 22e-9,  22e-9,  220e3, 1e6,   22e3, 33e3 },  3, 0, 32.0, 0.3 }, // 2203: tres etapas en cascada
        { "Mark",        { 250e-12, 100e-9, 47e-9,  250e3, 250e3, 25e3, 100e3 }, 1, 3, 30.0, 0.2 }, // Mesa Mark: stack temprano, lead después
        { "SLO-100",     { 470e-12, 20e-9,  20e-9,  250e3, 1e6,   25e3, 47e3 },  4, 0, 30.0, 0.3 }, // Soldano: cuatro etapas antes del stack
        { "VL501",       { 470e-12, 22e-9,  22e-9,  250e3, 1e6,   25e3, 32e3 },  3, 0, 33.0, 0.3 }, // Ampeg VL501
    } };
    return m;
}

// Tone stack: coeficientes analógicos de Yeh y Smith y bilineal (c = 2·fs) a un filtro de 3.er orden normalizado
struct StackCoeffs { double b[4] {}, a[4] {}; };

inline StackCoeffs stackCoeffs (const Stack& k, double t, double m, double bassKnob, double fs)
{
    const auto C1 = k.c1, C2 = k.c2, C3 = k.c3, R1 = k.r1, R2 = k.r2, R3 = k.r3, R4 = k.r4;
    const auto l = std::exp ((bassKnob - 1.0) * 3.4);
    const auto b1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
    const auto b2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4) - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3) + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                  + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4) + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                  + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
    const auto b3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                  + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4) + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                  + t*l*C1*C2*C3*R1*R2*R4;
    const auto a0 = 1.0;
    const auto a1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4) + m*C3*R3 + l*(C1*R2 + C2*R2);
    const auto a2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3) + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                  - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3) + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                  + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4 + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
    const auto a3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                  + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3 - C1*C2*C3*R1*R3*R4) + l*C1*C2*C3*R1*R2*R4 + C1*C2*C3*R1*R3*R4;
    // s = c·(1 - z⁻¹)/(1 + z⁻¹), multiplicando por (1 + z⁻¹)³: coeficientes de z⁰..z⁻³
    const auto c = 2.0 * fs, c2 = c * c, c3 = c2 * c;
    auto bil = [&] (double x0, double x1, double x2, double x3, double* out)
    {
        out[0] = x0 + x1*c + x2*c2 + x3*c3;
        out[1] = 3*x0 + x1*c - x2*c2 - 3*x3*c3;
        out[2] = 3*x0 - x1*c - x2*c2 + 3*x3*c3;
        out[3] = x0 - x1*c + x2*c2 - x3*c3;
    };
    StackCoeffs r;
    bil (0.0, b1, b2, b3, r.b);
    bil (a0, a1, a2, a3, r.a);
    const auto n = r.a[0];
    for (int i = 0; i < 4; ++i) { r.b[i] /= n; r.a[i] /= n; }
    return r;
}

// Etapa de triodo: asimétrica (positivo: tanh, satura a 1; negativo: margen 1,6 y curva más suave). Pendiente 1 en el origen
inline double triode (double v) { return v >= 0.0 ? std::tanh (v) : 1.6 * std::tanh (v / 1.6); }

// Antialiasing por antiderivada de 1.er orden (Parker, Zavalishin y Le Bivic, "Reducing the Aliasing of Nonlinear
// Waveshaping Using Continuous-Time Convolution", DAFx-16): y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), F' = f.
// Con |dx| pequeño, f en el punto medio. Retrasa media muestra (a 4·fs: 1/8 de muestra del host).
// log cosh estable: |x| + log1p(e^{-2|x|}) - ln 2 (sin desbordamiento para |x| grande)
inline double logCosh (double x) { const auto a = std::abs (x); return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531; }
inline double triodeAntiderivative (double v) { return v >= 0.0 ? logCosh (v) : 2.56 * logCosh (v / 1.6); } // F continua y F' = triode

struct Adaa
{
    double x1 = 0.0, f1 = 0.0; // entrada anterior y su antiderivada
    template <typename Fn, typename AntiFn>
    double process (double x, Fn fn, AntiFn anti)
    {
        const auto f = anti (x);
        const auto dx = x - x1;
        const auto y = std::abs (dx) > 1.0e-6 ? (f - f1) / dx : fn (0.5 * (x + x1));
        x1 = x; f1 = f;
        return y;
    }
};

// Tipos de pantalla: aproximación con biquads (paso alto con resonancia de la caja, picos y valles, paso bajo del cono)
enum Cab { cabOff = 0, cab1x12Open, cab2x12Open, cab4x12Closed, cab1x15Bass, cab8x10Bass, cabUserIr };

inline int cabSections (int type, double fs, std::array<Eq::Coeffs, 8>& out)
{
    using namespace Eq;
    int n = 0;
    auto add = [&] (Coeffs c) { out[(size_t) n++] = c; };
    switch (type)
    {
        case cab1x12Open:   add (highPass (75, 0.9, fs));  add (peak (120, 3, 1.2, fs));  add (peak (500, -3, 1.0, fs));
                            add (peak (2200, 4, 1.5, fs)); add (lowPass (5500, butterQ (0, 2), fs)); add (lowPass (5500, butterQ (1, 2), fs)); break;
        case cab2x12Open:   add (highPass (70, 1.0, fs));  add (peak (110, 3, 1.2, fs));  add (peak (600, -2, 1.0, fs));
                            add (peak (2500, 3, 1.2, fs)); add (lowPass (5000, butterQ (0, 2), fs)); add (lowPass (5000, butterQ (1, 2), fs)); break;
        case cab4x12Closed: add (highPass (80, 1.4, fs));  add (peak (400, -4, 1.2, fs)); add (peak (1200, 2, 1.0, fs));
                            add (peak (2500, 5, 1.6, fs)); add (peak (7000, -6, 2.0, fs));
                            add (lowPass (5000, butterQ (0, 2), fs)); add (lowPass (5000, butterQ (1, 2), fs)); break;
        case cab1x15Bass:   add (highPass (45, 1.1, fs));  add (peak (80, 3, 1.0, fs));   add (peak (700, -3, 1.0, fs));
                            add (peak (2000, 2, 1.2, fs)); add (lowPass (3500, butterQ (0, 2), fs)); add (lowPass (3500, butterQ (1, 2), fs)); break;
        case cab8x10Bass:   add (highPass (50, 1.2, fs));  add (peak (70, 4, 1.0, fs));   add (peak (300, -2, 1.0, fs));
                            add (peak (1500, 4, 1.2, fs)); add (lowPass (4000, butterQ (0, 2), fs)); add (lowPass (4000, butterQ (1, 2), fs)); break;
        default: break;
    }
    return n;
}
} // namespace Amp

struct AmpSim
{
    struct Settings
    {
        int model = 0, cab = Amp::cab4x12Closed;
        float gain = 0.5f, bass = 0.5f, mid = 0.5f, treble = 0.5f, presence = 0.3f, master = 0.5f, sag = 0.5f, outputDb = 0.0f,
              cabMix = 1.0f;
    };

    static constexpr int factorLog2 = 2; // ×4

    void prepare (double sr, int maxBlock)
    {
        fs = sr;
        blockMax = juce::jmax (1, maxBlock);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, factorLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                                          true, true);
        oversampling->initProcessing ((size_t) juce::jmax (1, maxBlock));
        convolution.prepare ({ sr, (juce::uint32) juce::jmax (1, maxBlock), 2 });
        dry.setSize (2, juce::jmax (1, maxBlock));
        reset();
    }

    void reset()
    {
        if (oversampling != nullptr) oversampling->reset();
        convolution.reset();
        for (auto& ch : chan) ch = {};
        primed = false;
    }

    // Latencia (muestras a la tasa del host) del sobremuestreo; la convolución es de latencia 0
    int latency() const { return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0; }

    // IR del usuario (hilo de mensajes): la convolución la carga en segundo plano y la cambia sin cortes
    void setUserIr (juce::AudioBuffer<float>&& ir, double irRate)
    {
        convolution.loadImpulseResponse (std::move (ir), irRate, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
        hasIr.store (true);
    }
    void clearUserIr() { hasIr.store (false); }

    // Bloques mayores que el anunciado en prepare: por trozos (el sobremuestreo y la convolución tienen ese tamaño máximo)
    void process (juce::AudioBuffer<float>& buffer, const Settings& s)
    {
        const auto chans = juce::jmin (2, buffer.getNumChannels());
        if (oversampling == nullptr || chans == 0) return;
        juce::dsp::AudioBlock<float> all (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) buffer.getNumSamples());
        for (int off = 0; off < buffer.getNumSamples(); off += blockMax)
            processChunk (all.getSubBlock ((size_t) off, (size_t) juce::jmin (blockMax, buffer.getNumSamples() - off)), s);
    }

private:
    void processChunk (juce::dsp::AudioBlock<float> block, const Settings& s)
    {
        const auto chans = (int) block.getNumChannels();
        const auto n = (int) block.getNumSamples();
        const auto& model = Amp::models()[(size_t) juce::jlimit (0, (int) Amp::models().size() - 1, s.model)];
        if (! primed) { cur = s; primed = true; }

        // --- Parte no lineal a 4·fs ---
        auto up = oversampling->processSamplesUp (block);
        const auto fsOs = fs * (double) (1 << factorLog2);
        const auto nOs = (int) up.getNumSamples();
        constexpr int chunk = 32;
        const auto k = 1.0 - std::exp (-(double) chunk / (0.020 * fsOs)); // suavizado de 20 ms
        const auto hpA = std::exp (-juce::MathConstants<double>::twoPi * 12.0 / fsOs); // acoplo entre etapas: 12 Hz
        const auto envA = std::exp (-1.0 / (0.010 * fsOs)), envR = std::exp (-1.0 / (0.150 * fsOs)); // sag: 10 ms / 150 ms
        for (int start = 0; start < nOs; start += chunk)
        {
            smooth (s, k);
            const auto stageGain = juce::Decibels::decibelsToGain (model.stageDb * (0.25 + 0.75 * (double) cur.gain)) * 0.25;
            const auto sc = Amp::stackCoeffs (model.stack, cur.treble, cur.mid, cur.bass, fsOs);
            const auto pres = Eq::highShelf (3000.0, 8.0 * (double) cur.presence, fsOs);
            const auto powerDrive = juce::Decibels::decibelsToGain (-6.0 + 24.0 * (double) cur.master);
            const auto sagDepth = 0.45 * (double) cur.sag * model.sag;
            const auto out = juce::Decibels::decibelsToGain ((double) cur.outputDb) / std::sqrt (powerDrive);
            const auto len = juce::jmin (chunk, nOs - start);
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* x = up.getChannelPointer ((size_t) ch) + start;
                auto& st = chan[(size_t) ch];
                for (int i = 0; i < len; ++i)
                {
                    double v = x[i];
                    auto stages = [&] (int count, int first)
                    {
                        for (int g = first; g < first + count; ++g)
                        {
                            v = st.adaa[(size_t) g].process (v * stageGain, Amp::triode, Amp::triodeAntiderivative);
                            auto& h = st.hp[(size_t) g]; // paso alto de 1.er orden (acoplo): y = a·(y₁ + x - x₁)
                            const auto y = hpA * (h.second + v - h.first);
                            h.first = v; h.second = y; v = y;
                        }
                    };
                    stages (model.stagesPre, 0);
                    // tone stack (forma directa II transpuesta, doble precisión)
                    const auto y = sc.b[0] * v + st.ts[0];
                    st.ts[0] = sc.b[1] * v - sc.a[1] * y + st.ts[1];
                    st.ts[1] = sc.b[2] * v - sc.a[2] * y + st.ts[2];
                    st.ts[2] = sc.b[3] * v - sc.a[3] * y;
                    v = y * 4.0; // el stack pierde ~12 dB en la zona media (como el real): lo recupera la etapa siguiente
                    stages (model.stagesPost, model.stagesPre);
                    // potencia: Presence y saturación con sag
                    const auto pv = pres.b0 * v + st.pz.first;
                    st.pz.first = pres.b1 * v - pres.a1 * pv + st.pz.second;
                    st.pz.second = pres.b2 * v - pres.a2 * pv;
                    const auto head = 1.0 - sagDepth * std::min (st.env, 1.0);
                    const auto o = head * st.adaaPower.process (pv * powerDrive / head, [] (double u) { return std::tanh (u); }, Amp::logCosh);
                    const auto a = std::abs (o) > st.env ? envA : envR;
                    st.env = a * st.env + (1.0 - a) * std::abs (o);
                    x[i] = (float) (o * out);
                }
            }
        }
        oversampling->processSamplesDown (block);

        // --- Pantalla a fs ---
        if (s.cab == Amp::cabOff || s.cabMix <= 0.0f) return;
        if (s.cabMix < 1.0f)
            for (int ch = 0; ch < chans; ++ch) dry.copyFrom (ch, 0, block.getChannelPointer ((size_t) ch), n);
        if (s.cab == Amp::cabUserIr)
        {
            if (! hasIr.load()) return;
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            convolution.process (ctx);
        }
        else
        {
            std::array<Eq::Coeffs, 8> c;
            const auto ns = Amp::cabSections (s.cab, fs, c);
            if (s.cab != lastCab) { for (auto& ch : chan) ch.cab = {}; lastCab = s.cab; }
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* x = block.getChannelPointer ((size_t) ch);
                auto& st = chan[(size_t) ch].cab;
                for (int i = 0; i < n; ++i)
                {
                    double v = x[i];
                    for (int q = 0; q < ns; ++q)
                    {
                        const auto& cf = c[(size_t) q];
                        auto& z = st[(size_t) q];
                        const auto y = cf.b0 * v + z.first;
                        z.first = cf.b1 * v - cf.a1 * y + z.second;
                        z.second = cf.b2 * v - cf.a2 * y;
                        v = y;
                    }
                    x[i] = (float) v;
                }
            }
        }
        if (s.cabMix < 1.0f)
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* x = block.getChannelPointer ((size_t) ch);
                const auto* d = dry.getReadPointer (ch);
                for (int i = 0; i < n; ++i) x[i] = s.cabMix * x[i] + (1.0f - s.cabMix) * d[i];
            }
    }

    void smooth (const Settings& t, double k)
    {
        auto lin = [k] (float& c, float v) { c += (float) ((v - c) * k); };
        lin (cur.gain, t.gain); lin (cur.bass, t.bass); lin (cur.mid, t.mid); lin (cur.treble, t.treble);
        lin (cur.presence, t.presence); lin (cur.master, t.master); lin (cur.sag, t.sag); lin (cur.outputDb, t.outputDb);
    }

    struct Channel
    {
        std::array<std::pair<double, double>, 4> hp {}; // acoplos de hasta 4 etapas
        std::array<double, 3> ts {};
        std::pair<double, double> pz {};
        double env = 0.0;
        std::array<Amp::Adaa, 4> adaa {};
        Amp::Adaa adaaPower;
        std::array<std::pair<double, double>, 8> cab {};
    };

    double fs = 48000.0;
    int blockMax = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::Convolution convolution;
    juce::AudioBuffer<float> dry;
    std::array<Channel, 2> chan {};
    Settings cur;
    bool primed = false;
    int lastCab = -1;
    std::atomic<bool> hasIr { false };
};

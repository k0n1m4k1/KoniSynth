// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Selección de la ruta SIMD (global para todas las instancias del proceso).
// Activa = la CPU tiene AVX2 && la autoprueba de arranque pasó && el usuario no la ha desactivado en Settings.
struct SimdStatus
{
    std::atomic<bool> cpuHasAvx2 { false }, selfTestPassed { false }, userEnabled { true }, tested { false };
    std::atomic<double> maxError { 0.0 };
};

static SimdStatus& simdStatus()
{
    static SimdStatus status;
    return status;
}

static bool simdActive()
{
    auto& s = simdStatus();
    return s.cpuHasAvx2.load() && s.selfTestPassed.load() && s.userEnabled.load();
}

// Configuración global del plugin (no va en presets ni en el proyecto): %APPDATA%\KoniSynth\config.settings
static std::unique_ptr<juce::PropertiesFile> openConfig()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "config";
    options.filenameSuffix = ".settings";
    options.folderName = "KoniSynth";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}

//==============================================================================
// Osciladores. phase en [0, 1), dt = f / fs (incremento de fase por muestra).

// PolyBLEP: corrige la discontinuidad de saw/square para reducir aliasing.
static float polyBlep (double t, double dt)
{
    if (t < dt)       { t /= dt;               return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

enum Wave { sine, triangle, saw, square };

// pw: ancho de pulso de la cuadrada (0,5 = cuadrada simétrica). El pulso de ancho d tiene media 2d - 1:
// se resta para que PWM no mueva el DC (que el filtro y la válvula verían como un desplazamiento lento).
// blep = false: forma ingenua (el motor Synth corrige los saltos con minBLEP, Dsp/MinBlep.h)
static float oscillator (int wave, double t, double dt, float pw = 0.5f, bool blep = true)
{
    switch (wave)
    {
        case triangle: return (float) (1.0 - 4.0 * std::abs (t - 0.5));
        case saw:      return (float) (2.0 * t - 1.0) - (blep ? polyBlep (t, dt) : 0.0f);
        case square:
        {
            const auto d = (double) pw;
            auto pulse = t < d ? 1.0f : -1.0f;
            if (blep)
                pulse += polyBlep (t, dt) - polyBlep (std::fmod (t + 1.0 - d, 1.0), dt);
            return 0.7f * (pulse - (float) (2.0 * d - 1.0)); // 0.7: RMS 1 -> ~0.7, como el seno
        }
        default:       return (float) std::sin (juce::MathConstants<double>::twoPi * t);
    }
}

//==============================================================================
// Referencia escalar sobre UnisonLanes: misma semántica que la voz (bucle del unísono y complexSample).
static void unisonStepScalar (konisimd::UnisonLanes& l, int wave, double dt, float pw, float& left, float& right)
{
    left = right = 0.0f;
    for (int u = 0; u < konisimd::kLanes; ++u)
    {
        const auto s = oscillator (wave, l.phase[u], dt * l.ratio[u], pw, false);
        left  += s * l.gainL[u];
        right += s * l.gainR[u];
        l.phase[u] += dt * l.ratio[u];
        if (l.phase[u] >= 1.0)
            l.phase[u] -= 1.0;
    }
}

static void complexStepScalar (konisimd::UnisonLanes& l, const konisimd::ComplexArgs& a, double dtP,
                               float& left, float& right, float& firstMod)
{
    left = right = 0.0f;
    for (int u = 0; u < konisimd::kLanes; ++u)
    {
        const auto dtPLane = dtP * l.ratio[u];
        const auto dtM = dtPLane * a.modRatio;
        const auto m = oscillator (a.modWave, l.modPhase[u], dtM);
        if (u == 0)
            firstMod = m;
        const auto dtInst = dtPLane + a.fmIndex * dtM * (double) m;
        auto s = oscillator (a.wave, l.phase[u], std::abs (dtInst), (float) a.pw, false);
        s *= (float) (1.0 - a.am) + (float) a.am * m;
        l.inc[u] = dtInst;
        l.mod[u] = m;
        left  += s * l.gainL[u];
        right += s * l.gainR[u];

        l.modPhase[u] += dtM;
        const auto wrapped = l.modPhase[u] >= 1.0;
        if (wrapped)
            l.modPhase[u] -= 1.0;
        l.phase[u] += dtInst;
        l.phase[u] -= std::floor (l.phase[u]);
        if (a.sync && wrapped)
            l.phase[u] = l.modPhase[u] / dtM * std::abs (dtInst);
    }
}

struct FmAlgorithms
{
    static constexpr std::uint8_t flags[32][6] = {
        { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 }, { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 },
        { 0xc1, 0x11, 0x94, 0x01, 0x11, 0x14 }, { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 }, { 0xc1, 0x94, 0x01, 0x14, 0x01, 0x14 },
        { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 },
        { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 }, { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 },
        { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 }, { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 }, { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 },
        { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 }, { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 }, { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 },
        { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 }, { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 },
        { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 }, { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 }, { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 },
        { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 }, { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 }, { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 },
        { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 }, { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 }, { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 },
        { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 }, { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 },
    };

    // Texto para la UI: conexiones y portadoras (p. ej. "6>5>4>3  2>1 | out 1,3 | fb 6")
    static juce::String describe (int alg)
    {
        juce::StringArray buses[3];
        juce::StringArray edges, carriers;
        int fbIn = 0, fbOut = 0;
        for (int i = 0; i < 6; ++i)
        {
            const auto f = flags[alg][i];
            const auto op = juce::String (6 - i);
            const auto in = (f >> 4) & 3, out = f & 3;
            if (f & 0x40) fbIn = 6 - i;
            if (f & 0x80) fbOut = 6 - i;
            if (in != 0)
                for (const auto& m : buses[in])
                    edges.add (m + ">" + op);
            if (out == 0) carriers.add (op);
            else if (f & 4) buses[out].add (op);
            else buses[out] = juce::StringArray (op);
        }
        carriers.sort (false);
        return edges.joinIntoString (" ") + "  |  out " + carriers.joinIntoString (",")
               + "  |  fb " + (fbOut == fbIn ? juce::String (fbIn) : juce::String (fbOut) + ">" + juce::String (fbIn));
    }
};

// sin(2π·t) en float con el polinomio de la ruta AVX2 (Taylor hasta a^11 tras plegar a [-π/2, π/2]; error < 6e-8 + redondeo)
static float sin2piPoly (float t)
{
    auto x = t - std::floor (t + 0.5f); // redondeo al entero más cercano (los empates exactos no importan)
    if (std::abs (x) > 0.25f)
        x = std::copysign (0.5f, x) - x;
    const auto a = x * 6.283185307179586f, a2 = a * a;
    auto p = -1.0f / 39916800.0f;
    p = p * a2 + 1.0f / 362880.0f;
    p = p * a2 - 1.0f / 5040.0f;
    p = p * a2 + 1.0f / 120.0f;
    p = p * a2 - 1.0f / 6.0f;
    p = p * a2 + 1.0f;
    return p * a;
}

// Núcleo FM escalar: un carril por voz. Los carriles sin ganancia ni rampa (voces libres) se saltan
static void fmRenderScalar (konisimd::FmLanes& l, const konisimd::FmArgs& a, int numSamples, float* out)
{
    bool live[konisimd::kLanes];
    for (int u = 0; u < konisimd::kLanes; ++u)
    {
        live[u] = false;
        for (int k = 0; k < 6; ++k)
            live[u] = live[u] || l.amp[k][u] != 0.0f || l.ampStep[k][u] != 0.0f;
    }
    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int u = 0; u < konisimd::kLanes; ++u)
        {
            if (! live[u])
                continue;
            float bus[3] = { 0.0f, 0.0f, 0.0f };
            for (int k = 0; k < 6; ++k)
            {
                const int f = a.flags[k], in = (f >> 4) & 3, ob = f & 3;
                auto mod = in != 0 ? bus[in] : 0.0f;
                if (k == a.fbDest && a.fbScale > 0.0f)
                    mod += (l.y1[a.fbSrc][u] + l.y2[a.fbSrc][u]) * (0.5f * a.fbScale);
                const auto y = l.amp[k][u] * sin2piPoly (l.phase[k][u] + mod);
                l.y2[k][u] = l.y1[k][u];
                l.y1[k][u] = y;
                const auto ph = l.phase[k][u] + l.inc[k][u] * (a.fixed[k] ? 1.0f : a.pitchMul);
                l.phase[k][u] = ph - std::floor (ph);
                l.amp[k][u] += l.ampStep[k][u];
                bus[ob] = (ob == 0 || (f & 4) != 0) ? bus[ob] + y : y;
            }
            sum += bus[0];
        }
        out[i] = sum;
    }
}

// Autoprueba (una vez por proceso, en el primer prepareToPlay): compara AVX2 con la referencia escalar en
// 4 ondas × {unísono, complejo, complejo + sync} × 2000 pasos desde estados aleatorios idénticos. Tolerancia 1e-4
// (el seno AVX2 es un polinomio con error < 6e-8; el resto son diferencias de redondeo float/double).
// Con la variable de entorno KONISYNTH_SIMD_BENCH, mide además ambas rutas y escribe
// %APPDATA%\KoniSynth\simd-selftest.txt (permite medir dentro de un host sin un ejecutable propio).
static void runSimdSelfTest()
{
    auto& status = simdStatus();
    if (status.tested.exchange (true))
        return;

    status.cpuHasAvx2 = juce::SystemStats::hasAVX2();
    if (! status.cpuHasAvx2.load())
        return;

    juce::Random rng (12345);
    auto randomLanes = [&rng]
    {
        konisimd::UnisonLanes l {};
        for (int u = 0; u < konisimd::kLanes; ++u)
        {
            l.phase[u] = rng.nextDouble();
            l.modPhase[u] = rng.nextDouble();
            l.ratio[u] = 0.98 + 0.04 * rng.nextDouble();
            l.gainL[u] = 0.5f * rng.nextFloat();
            l.gainR[u] = 0.5f * rng.nextFloat();
        }
        return l;
    };

    const auto dt = 440.0 / 48000.0;
    const auto pw = 0.3f;
    double maxErr = 0.0;

    for (int wave = 0; wave < 4; ++wave)
    {
        for (int mode = 0; mode < 3; ++mode) // 0 unísono, 1 complejo, 2 complejo + sync
        {
            auto a = randomLanes();
            auto b = a;
            const konisimd::ComplexArgs args { wave, (wave + 1) % 4, 1.37, 1.5, 0.3, (double) pw, mode == 2 };

            for (int i = 0; i < 2000; ++i)
            {
                float l1, r1, l2, r2, m1 = 0.0f, m2 = 0.0f;
                if (mode == 0)
                {
                    unisonStepScalar (a, wave, dt, pw, l1, r1);
                    konisimd::unisonStepAvx2 (b, wave, dt, (double) pw, l2, r2);
                }
                else
                {
                    complexStepScalar (a, args, 0.5 * dt, l1, r1, m1);
                    konisimd::complexStepAvx2 (b, args, 0.5 * dt, l2, r2, m2);
                }
                maxErr = juce::jmax (maxErr, (double) std::abs (l1 - l2), (double) std::abs (r1 - r2), (double) std::abs (m1 - m2));
            }
        }
    }

    // FM 6-Op: 32 algoritmos × 64 muestras, 8 carriles con fases, ganancias y salidas aleatorias
    auto randomFm = [&rng] (konisimd::FmLanes& l)
    {
        for (int k = 0; k < 6; ++k)
            for (int u = 0; u < konisimd::kLanes; ++u)
            {
                l.phase[k][u] = rng.nextFloat();
                l.inc[k][u] = 0.001f + 0.05f * rng.nextFloat();
                l.amp[k][u] = 2.0f * rng.nextFloat();
                l.ampStep[k][u] = 1.0e-3f * (rng.nextFloat() - 0.5f);
                l.y1[k][u] = rng.nextFloat() - 0.5f;
                l.y2[k][u] = rng.nextFloat() - 0.5f;
            }
    };
    for (int alg = 0; alg < 32; ++alg)
    {
        konisimd::FmArgs args {};
        for (int k = 0; k < 6; ++k)
        {
            args.flags[k] = FmAlgorithms::flags[alg][k];
            if (args.flags[k] & 0x40) args.fbDest = k;
            if (args.flags[k] & 0x80) args.fbSrc = k;
            args.fixed[k] = k == 2 && alg % 5 == 0;
        }
        args.fbScale = 0.25f;
        args.pitchMul = 1.01f;
        alignas (32) konisimd::FmLanes la;
        randomFm (la);
        auto lb = la;
        float o1[16], o2[16];
        for (int b = 0; b < 4; ++b)
        {
            fmRenderScalar (la, args, 16, o1);
            konisimd::fmRenderAvx2 (lb, args, 16, o2);
            for (int i = 0; i < 16; ++i)
                maxErr = juce::jmax (maxErr, (double) std::abs (o1[i] - o2[i]) / juce::jmax (1.0, (double) std::abs (o1[i])));
        }
    }

    status.maxError = maxErr;
    status.selfTestPassed = maxErr < 1.0e-4;

    if (juce::SystemStats::getEnvironmentVariable ("KONISYNTH_SIMD_BENCH", {}).isEmpty())
        return;

    // Medida: 400 000 pasos por ruta (≈ 8,3 s de audio a 48 kHz para una voz con 8 osciladores)
    auto timeIt = [] (auto&& fn)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        fn();
        return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    };

    constexpr int steps = 400000;
    volatile float sink = 0.0f;
    juce::String report;
    report << "CPU AVX2: yes\nself-test max error: " << juce::String (maxErr, 9)
           << (status.selfTestPassed.load() ? " (passed)\n" : " (FAILED)\n");

    for (int wave = 0; wave < 4; ++wave)
    {
        auto la = randomLanes(), lb = la;
        const auto tScalar = timeIt ([&] { float l, r; for (int i = 0; i < steps; ++i) { unisonStepScalar (la, wave, dt, pw, l, r); sink = sink + l; } });
        const auto tSimd   = timeIt ([&] { float l, r; for (int i = 0; i < steps; ++i) { konisimd::unisonStepAvx2 (lb, wave, dt, (double) pw, l, r); sink = sink + l; } });
        const konisimd::ComplexArgs args { wave, 0, 1.37, 1.5, 0.3, (double) pw, false };
        auto ca = randomLanes(), cb = ca;
        const auto cScalar = timeIt ([&] { float l, r, m; for (int i = 0; i < steps; ++i) { complexStepScalar (ca, args, 0.5 * dt, l, r, m); sink = sink + l; } });
        const auto cSimd   = timeIt ([&] { float l, r, m; for (int i = 0; i < steps; ++i) { konisimd::complexStepAvx2 (cb, args, 0.5 * dt, l, r, m); sink = sink + l; } });
        report << "wave " << wave << ": unison scalar " << juce::String (1.0e9 * tScalar / steps, 1) << " ns/step, AVX2 "
               << juce::String (1.0e9 * tSimd / steps, 1) << " ns/step (x" << juce::String (tScalar / tSimd, 2) << ")"
               << " | complex scalar " << juce::String (1.0e9 * cScalar / steps, 1) << " ns, AVX2 "
               << juce::String (1.0e9 * cSimd / steps, 1) << " ns (x" << juce::String (cScalar / cSimd, 2) << ")\n";
    }

    {
        konisimd::FmArgs args {};
        for (int k = 0; k < 6; ++k)
            args.flags[k] = FmAlgorithms::flags[0][k];
        args.fbSrc = args.fbDest = 0;
        args.fbScale = 0.25f;
        args.pitchMul = 1.0f;
        alignas (32) konisimd::FmLanes fa;
        randomFm (fa);
        auto fb = fa;
        float o[16];
        constexpr int blocks = steps / 16;
        const auto fScalar = timeIt ([&] { for (int i = 0; i < blocks; ++i) { fmRenderScalar (fa, args, 16, o); sink = sink + o[0]; } });
        const auto fSimd   = timeIt ([&] { for (int i = 0; i < blocks; ++i) { konisimd::fmRenderAvx2 (fb, args, 16, o); sink = sink + o[0]; } });
        report << "FM 6-Op (8 voices x 6 ops, algorithm 1): scalar " << juce::String (1.0e9 * fScalar / (blocks * 16), 1)
               << " ns/sample, AVX2 " << juce::String (1.0e9 * fSimd / (blocks * 16), 1) << " ns/sample (x"
               << juce::String (fScalar / fSimd, 2) << ")\n";
    }

    juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("KoniSynth").getChildFile ("simd-selftest.txt").replaceWithText (report);
}

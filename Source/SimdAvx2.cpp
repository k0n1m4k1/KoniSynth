// Ruta AVX2 de los osciladores del unísono. Compilado con /arch:AVX2 y /GL- (ver CMakeLists.txt).
// Solo intrínsecos y funciones con enlace interno: nada de cabeceras con funciones inline compartidas (JUCE, <cmath>,
// <algorithm>), para que el enlazador no pueda elegir una copia AVX2 de una función común.
// Réplica de la ruta escalar de KoniSynth.cpp (oscillator(), complexSample()); la autoprueba de arranque compara ambas.
#include "SimdUnison.h"

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
#include <immintrin.h>

namespace konisimd
{
namespace
{
enum { kSine = 0, kTriangle = 1, kSaw = 2, kSquare = 3 }; // los mismos códigos que Wave (Dsp/OscillatorCore.h); mezclas: kWaveMix

inline __m256d set1 (double v) { return _mm256_set1_pd (v); }

inline __m256d frac (__m256d x) { return _mm256_sub_pd (x, _mm256_floor_pd (x)); }

inline __m256d absd (__m256d x) { return _mm256_andnot_pd (set1 (-0.0), x); }

// PolyBLEP: igual que polyBlep() escalar (la rama t < dt tiene prioridad)
inline __m256d polyBlep (__m256d t, __m256d dt)
{
    const auto one = set1 (1.0);
    const auto a  = _mm256_div_pd (t, dt);
    const auto ra = _mm256_sub_pd (_mm256_sub_pd (_mm256_add_pd (a, a), _mm256_mul_pd (a, a)), one);
    const auto b  = _mm256_div_pd (_mm256_sub_pd (t, one), dt);
    const auto rb = _mm256_add_pd (_mm256_add_pd (_mm256_mul_pd (b, b), _mm256_add_pd (b, b)), one);
    const auto mA = _mm256_cmp_pd (t, dt, _CMP_LT_OQ);
    const auto mB = _mm256_cmp_pd (t, _mm256_sub_pd (one, dt), _CMP_GT_OQ);
    return _mm256_blendv_pd (_mm256_blendv_pd (_mm256_setzero_pd(), rb, mB), ra, mA);
}

// sin(2π·t): reducción a x ∈ [-0,5, 0,5], plegado a [-0,25, 0,25] (sin(π - a) = sin a) y Taylor hasta a^11 en
// [-π/2, π/2]: error del término siguiente (π/2)^13/13! ≈ 5,7e-8.
inline __m256d sin2pi (__m256d t)
{
    auto x = _mm256_sub_pd (t, _mm256_round_pd (t, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
    const auto sign = _mm256_and_pd (x, set1 (-0.0));
    const auto halfSigned = _mm256_or_pd (set1 (0.5), sign);
    const auto fold = _mm256_cmp_pd (absd (x), set1 (0.25), _CMP_GT_OQ);
    x = _mm256_blendv_pd (x, _mm256_sub_pd (halfSigned, x), fold);

    const auto a = _mm256_mul_pd (x, set1 (6.283185307179586));
    const auto a2 = _mm256_mul_pd (a, a);
    auto p = set1 (-1.0 / 39916800.0);
    p = _mm256_add_pd (_mm256_mul_pd (p, a2), set1 (1.0 / 362880.0));
    p = _mm256_add_pd (_mm256_mul_pd (p, a2), set1 (-1.0 / 5040.0));
    p = _mm256_add_pd (_mm256_mul_pd (p, a2), set1 (1.0 / 120.0));
    p = _mm256_add_pd (_mm256_mul_pd (p, a2), set1 (-1.0 / 6.0));
    p = _mm256_add_pd (_mm256_mul_pd (p, a2), set1 (1.0));
    return _mm256_mul_pd (p, a);
}

// Igual que oscillator() escalar. 'wave' es uniforme en los carriles.
inline __m256d oscillator (int wave, __m256d t, __m256d dt, double pw, bool blep)
{
    if (wave >= kFirstMixWave && wave <= kLastMixWave) // mezclas: sierra, triángulo y pulso alto en t >= 1 - d
    {
        const auto& m = kWaveMix[wave - kFirstMixWave];
        const auto sawPart = _mm256_sub_pd (_mm256_add_pd (t, t), set1 (1.0));
        const auto triPart = _mm256_sub_pd (set1 (1.0), _mm256_mul_pd (set1 (4.0), absd (_mm256_sub_pd (t, set1 (0.5)))));
        const auto high = _mm256_cmp_pd (t, set1 (1.0 - pw), _CMP_GE_OQ);
        const auto pulsePart = _mm256_sub_pd (_mm256_blendv_pd (set1 (-1.0), set1 (1.0), high), set1 (2.0 * pw - 1.0));
        auto v = _mm256_add_pd (_mm256_add_pd (_mm256_mul_pd (set1 (m.saw), sawPart), _mm256_mul_pd (set1 (m.tri), triPart)),
                                _mm256_mul_pd (set1 (m.pulse), pulsePart));
        if (blep)
        {
            v = _mm256_sub_pd (v, _mm256_mul_pd (set1 (m.saw + m.pulse), polyBlep (t, dt)));
            v = _mm256_add_pd (v, _mm256_mul_pd (set1 (m.pulse), polyBlep (frac (_mm256_add_pd (t, set1 (pw))), dt)));
        }
        return v;
    }
    switch (wave)
    {
        case kTriangle:
            return _mm256_sub_pd (set1 (1.0), _mm256_mul_pd (set1 (4.0), absd (_mm256_sub_pd (t, set1 (0.5)))));
        case kSaw:
        {
            const auto naive = _mm256_sub_pd (_mm256_add_pd (t, t), set1 (1.0));
            return blep ? _mm256_sub_pd (naive, polyBlep (t, dt)) : naive;
        }
        case kSquare:
        {
            const auto d = set1 (pw);
            const auto high = _mm256_cmp_pd (t, d, _CMP_LT_OQ);
            auto pulse = _mm256_blendv_pd (set1 (-1.0), set1 (1.0), high);
            if (blep)
            {
                pulse = _mm256_add_pd (pulse, polyBlep (t, dt));
                pulse = _mm256_sub_pd (pulse, polyBlep (frac (_mm256_add_pd (t, set1 (1.0 - pw))), dt));
            }
            return _mm256_mul_pd (set1 (0.7), _mm256_sub_pd (pulse, set1 (2.0 * pw - 1.0)));
        }
        default:
            return sin2pi (t);
    }
}

inline double horizontalSum (__m256d v)
{
    const auto lo = _mm256_castpd256_pd128 (v);
    const auto hi = _mm256_extractf128_pd (v, 1);
    const auto s = _mm_add_pd (lo, hi);
    return _mm_cvtsd_f64 (_mm_add_sd (s, _mm_unpackhi_pd (s, s)));
}
// sin(2π·t) en float: misma reducción y mismo polinomio que sin2pi() (y que sin2piPoly() escalar en KoniSynth.cpp)
inline __m256 sin2piPs (__m256 t)
{
    auto x = _mm256_sub_ps (t, _mm256_round_ps (t, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
    const auto sign = _mm256_and_ps (x, _mm256_set1_ps (-0.0f));
    const auto halfSigned = _mm256_or_ps (_mm256_set1_ps (0.5f), sign);
    const auto fold = _mm256_cmp_ps (_mm256_andnot_ps (_mm256_set1_ps (-0.0f), x), _mm256_set1_ps (0.25f), _CMP_GT_OQ);
    x = _mm256_blendv_ps (x, _mm256_sub_ps (halfSigned, x), fold);

    const auto a = _mm256_mul_ps (x, _mm256_set1_ps (6.283185307179586f));
    const auto a2 = _mm256_mul_ps (a, a);
    auto p = _mm256_set1_ps (-1.0f / 39916800.0f);
    p = _mm256_add_ps (_mm256_mul_ps (p, a2), _mm256_set1_ps (1.0f / 362880.0f));
    p = _mm256_add_ps (_mm256_mul_ps (p, a2), _mm256_set1_ps (-1.0f / 5040.0f));
    p = _mm256_add_ps (_mm256_mul_ps (p, a2), _mm256_set1_ps (1.0f / 120.0f));
    p = _mm256_add_ps (_mm256_mul_ps (p, a2), _mm256_set1_ps (-1.0f / 6.0f));
    p = _mm256_add_ps (_mm256_mul_ps (p, a2), _mm256_set1_ps (1.0f));
    return _mm256_mul_ps (p, a);
}
} // namespace

void unisonStepAvx2 (UnisonLanes& lanes, int wave, double dt, double pw, float& left, float& right, double triA, double triR, double triM)
{
    auto accL = _mm256_setzero_pd(), accR = _mm256_setzero_pd();
    const auto one = set1 (1.0);
    const auto shaped = wave == kTriangle && (triA != 0.0 || triR != 0.0);

    for (int h = 0; h < kLanes; h += 4)
    {
        auto phase = _mm256_load_pd (lanes.phase + h);
        const auto inc = _mm256_mul_pd (set1 (dt), _mm256_load_pd (lanes.ratio + h));
        auto s = oscillator (wave, phase, inc, pw, false);
        if (shaped) // TriShape: y = (1 - r)·x + r·sin(π/2·x); y + a·(y² - m). sin(π/2·x) = sin(2π·x/4)
        {
            const auto y = _mm256_add_pd (_mm256_mul_pd (set1 (1.0 - triR), s), _mm256_mul_pd (set1 (triR), sin2pi (_mm256_mul_pd (s, set1 (0.25)))));
            s = _mm256_add_pd (y, _mm256_mul_pd (set1 (triA), _mm256_sub_pd (_mm256_mul_pd (y, y), set1 (triM))));
        }

        accL = _mm256_add_pd (accL, _mm256_mul_pd (s, _mm256_cvtps_pd (_mm_load_ps (lanes.gainL + h))));
        accR = _mm256_add_pd (accR, _mm256_mul_pd (s, _mm256_cvtps_pd (_mm_load_ps (lanes.gainR + h))));

        // Avance después de leer (como la ruta escalar): phase += inc; si phase >= 1, phase -= 1
        phase = _mm256_add_pd (phase, inc);
        phase = _mm256_sub_pd (phase, _mm256_and_pd (_mm256_cmp_pd (phase, one, _CMP_GE_OQ), one));
        _mm256_store_pd (lanes.phase + h, phase);
    }

    left  = (float) horizontalSum (accL);
    right = (float) horizontalSum (accR);
}

void complexStepAvx2 (UnisonLanes& lanes, const ComplexArgs& args, double dtP, float& left, float& right, float& firstMod)
{
    auto accL = _mm256_setzero_pd(), accR = _mm256_setzero_pd();
    const auto one = set1 (1.0);

    for (int h = 0; h < kLanes; h += 4)
    {
        const auto dtPLane = _mm256_mul_pd (set1 (dtP), _mm256_load_pd (lanes.ratio + h));
        const auto dtM = args.external ? set1 (args.extDtM) : _mm256_mul_pd (dtPLane, set1 (args.modRatio));
        auto modPhase = _mm256_load_pd (lanes.modPhase + h);
        auto phase = _mm256_load_pd (lanes.phase + h);

        // El modulador interno usa siempre ancho 0,5 (con PolyBLEP); el externo llega ya evaluado
        const auto m = args.external ? set1 (args.extMod) : oscillator (args.modWave, modPhase, dtM, 0.5, true);
        if (h == 0)
            firstMod = (float) _mm_cvtsd_f64 (_mm256_castpd256_pd128 (m));

        // FM lineal que atraviesa cero; AM / ring
        const auto dtInst = _mm256_add_pd (dtPLane, _mm256_mul_pd (_mm256_mul_pd (set1 (args.fmIndex), dtM), m));
        const auto absInst = absd (dtInst);
        auto s = oscillator (args.wave, phase, absInst, args.pw, false);
        _mm256_store_pd (lanes.inc + h, dtInst);
        _mm256_store_pd (lanes.mod + h, m);
        s = _mm256_mul_pd (s, _mm256_add_pd (set1 (1.0 - args.am), _mm256_mul_pd (set1 (args.am), m)));

        accL = _mm256_add_pd (accL, _mm256_mul_pd (s, _mm256_cvtps_pd (_mm_load_ps (lanes.gainL + h))));
        accR = _mm256_add_pd (accR, _mm256_mul_pd (s, _mm256_cvtps_pd (_mm_load_ps (lanes.gainR + h))));

        // Avance de fases, envoltura en ambos sentidos y sync con la fracción de muestra (modulador externo: su vuelta y su
        // fase final llegan dadas, y la fase interna no avanza)
        __m256d wrapped;
        if (args.external)
        {
            wrapped = args.extWrapped ? _mm256_cmp_pd (one, one, _CMP_EQ_OQ) : _mm256_setzero_pd();
            modPhase = set1 (args.extPhaseEnd);
        }
        else
        {
            modPhase = _mm256_add_pd (modPhase, dtM);
            wrapped = _mm256_cmp_pd (modPhase, one, _CMP_GE_OQ);
            modPhase = _mm256_sub_pd (modPhase, _mm256_and_pd (wrapped, one));
            _mm256_store_pd (lanes.modPhase + h, modPhase);
        }
        phase = frac (_mm256_add_pd (phase, dtInst));
        if (args.sync)
            phase = _mm256_blendv_pd (phase, _mm256_mul_pd (_mm256_div_pd (modPhase, dtM), absInst), wrapped);

        _mm256_store_pd (lanes.phase + h, phase);
    }

    left  = (float) horizontalSum (accL);
    right = (float) horizontalSum (accR);
}
void fmRenderAvx2 (FmLanes& l, const FmArgs& a, int numSamples, float* out)
{
    const auto zero = _mm256_setzero_ps();
    const auto fbHalf = _mm256_set1_ps (0.5f * a.fbScale);
    for (int i = 0; i < numSamples; ++i)
    {
        __m256 bus[3] = { zero, zero, zero };
        for (int k = 0; k < 6; ++k)
        {
            const int f = a.flags[k], in = (f >> 4) & 3, ob = f & 3;
            auto mod = in != 0 ? bus[in] : zero;
            if (k == a.fbDest && a.fbScale > 0.0f)
                mod = _mm256_add_ps (mod, _mm256_mul_ps (_mm256_add_ps (_mm256_load_ps (l.y1[a.fbSrc]), _mm256_load_ps (l.y2[a.fbSrc])), fbHalf));
            const auto amp = _mm256_load_ps (l.amp[k]);
            auto phase = _mm256_load_ps (l.phase[k]);
            const auto y = _mm256_mul_ps (amp, sin2piPs (_mm256_add_ps (phase, mod)));
            _mm256_store_ps (l.y2[k], _mm256_load_ps (l.y1[k]));
            _mm256_store_ps (l.y1[k], y);
            phase = _mm256_add_ps (phase, _mm256_mul_ps (_mm256_load_ps (l.inc[k]), _mm256_set1_ps (a.fixed[k] ? 1.0f : a.pitchMul)));
            _mm256_store_ps (l.phase[k], _mm256_sub_ps (phase, _mm256_floor_ps (phase)));
            _mm256_store_ps (l.amp[k], _mm256_add_ps (amp, _mm256_load_ps (l.ampStep[k])));
            bus[ob] = (ob == 0 || (f & 4) != 0) ? _mm256_add_ps (bus[ob], y) : y;
        }
        // Suma horizontal de las 8 voces
        auto h = _mm_add_ps (_mm256_castps256_ps128 (bus[0]), _mm256_extractf128_ps (bus[0], 1));
        h = _mm_add_ps (h, _mm_movehl_ps (h, h));
        h = _mm_add_ss (h, _mm_shuffle_ps (h, h, 1));
        out[i] = _mm_cvtss_f32 (h);
    }
}
} // namespace konisimd

#else
// Sin x86 (Apple Silicon, ARM): no hay AVX2. El plugin no llama nunca a estas funciones (juce::SystemStats::hasAVX2() es
// falso y se usa la ruta escalar); existen para enlazar
namespace konisimd
{
void unisonStepAvx2 (UnisonLanes&, int, double, double, float& left, float& right, double, double, double) { left = right = 0.0f; }
void complexStepAvx2 (UnisonLanes&, const ComplexArgs&, double, float& left, float& right, float& firstMod) { left = right = firstMod = 0.0f; }
void fmRenderAvx2 (FmLanes&, const FmArgs&, int numSamples, float* out) { for (int i = 0; i < numSamples; ++i) out[i] = 0.0f; }
} // namespace konisimd
#endif

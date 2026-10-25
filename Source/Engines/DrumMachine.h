// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Voces de caja de ritmos analógica: Roland TR-808 (1980) y CR-78 (1978), más el kit del Omnichord.
// Frecuencias y decays: tablas de ajuste de los service notes de la TR-808 (p. 14) y la CR-78 (p. 15); decay = tiempo hasta
// 1/10 de amplitud -> τ = T/ln 10. Platos de la 808: 6 cuadradas (205,3 / 304,4 / 369,6 / 522,7 / 540 / 800 Hz), bandas de
// 3440 y 7100 Hz y paso alto de 10,5 kHz (Werner, Abel y Smith, ICMC-SMC 2014). Bombo: clic de ~6 ms en que la frecuencia
// sube más de una octava (Werner et al., DAFx-14).
// Filtros y envolventes de ruido CALCULADOS desde los esquemas de los service notes (análisis nodal con los valores de R y C;
// validado frente a los pasabanda del platillo de Werner et al.: 3484/7194 Hz calculados frente a 3440/7100 publicados, 1-1,3 %):
//  808 SD: paso alto Sallen-Key 2750 Hz Q 0,73 (C66 = C67 = 1,8 nF, R201 = 22k, R202 = 47k) con la carga real -> banda hasta
//  17,9 kHz; "snappy" de 2 polos (τ 14,9 y 0,7 ms: 10 % a 25 ms). CP: pasabanda MFB 1271 Hz Q 1,48 (IC21); cuatro dientes a 0,
//  11, 22 y 30 ms (Fig. 13 del manual), caída de cada uno τ 2,1-3,2 ms y el último τ 33 ms; "reverb" de 2 polos (12 y 75 ms).
//  MA: paso alto 1,06 kHz + Sallen-Key 10,6 kHz Q 2,27; ataque τ 15,5 ms durante ~26 ms y caída τ 3,3 ms. OH: pasabanda 7,1 kHz
//  (Q 5,6) + Sallen-Key 7,83 kHz Q 2,51 + paso alto 8,68 kHz; CH: Sallen-Key 11,75 kHz Q 2,51 + paso alto 10,3 kHz. RS: Sallen-Key
//  589 Hz Q 2,87. CB: pasabanda MFB 874 Hz Q 4,5 (frisnit, de oído: 850 Hz Q 4,25). CR-78: hats, platillo y maracas por un tanque
//  LC de 9,10 kHz (L3 = 45 mH, C521 = 6,8 nF; Q no calculable: 3, estimado); caja: banda 6,4-10,5 kHz; güiro: tren de clics a
//  125 / 77 Hz (tabla del manual) sobre un tanque de 6,13 kHz, sin ruido.
// Estimado (no publicado): curva de tono de los toms y del bombo, nivel relativo de las voces, Q de los tanques de la CR-78.
// Los resonadores en doble T se modelan como senoides amortiguadas (respuesta al impulso de un pasabanda de Q alto).
enum class DrumVoice
{
    BD808, SD808, LT808, MT808, HT808, LC808, MC808, HC808, RS808, CL808, CP808, MA808, CB808, CY808, OH808, CH808,
    BD78, SD78, RS78, HH78, CY78, M78, C78, HB78, LB78, LC78, CB78, TB78, GU78S, GU78L, MB78,
    BDMP, SN1MP, SN2MP, HHMP, CY1MP, CY2MP, TAMP, CBMP, COMP, SBMP, LBMP, CLMP, RSMP, GUMP, QUMP,
    count
};

struct DrumKit
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { random.setSeed (seed); }

    struct Settings { double tune = 1.0, decay = 1.0; float tone = 0.5f, snappy = 0.5f; };

    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& v : voices)
            v = {};
    }

    void reset()
    {
        for (auto& v : voices)
            v.active = false;
    }

    // Choke: el charles cerrado corta el abierto (Q23 en la 808)
    void trigger (DrumVoice id, float velocity, const Settings& s)
    {
        if (id == DrumVoice::CH808)
            voices[(size_t) DrumVoice::OH808].active = false;
        auto& v = voices[(size_t) id];
        v = {};
        v.active = true;
        v.velocity = velocity;
        v.settings = s;
        setupFilters (id, v);
    }

    float render()
    {
        float sum = 0.0f;
        for (size_t k = 0; k < voices.size(); ++k)
            if (voices[k].active)
                sum += voices[k].velocity * renderVoice ((DrumVoice) k, voices[k]);
        return sum;
    }

private:
    struct Voice
    {
        bool active = false;
        float velocity = 1.0f;
        Settings settings;
        double t = 0.0;                       // segundos desde el disparo
        std::array<double, 6> phase {};
        std::array<Biquad, 4> filter;
        float lp = 0.0f, pink = 0.0f;
    };

    // Senoide amortiguada: frecuencia f (con curva de tono), T = tiempo hasta 1/10
    static double ring (Voice& v, int k, double f, double decayT, double sr)
    {
        v.phase[(size_t) k] += f / sr;
        if (v.phase[(size_t) k] >= 1.0) v.phase[(size_t) k] -= 1.0;
        return std::sin (juce::MathConstants<double>::twoPi * v.phase[(size_t) k]) * decay (v.t, decayT);
    }

    static double decay (double t, double decayT) { return std::exp (-t * 2.302585 / decayT); }

    double squareOsc (Voice& v, int k, double f)
    {
        const auto dt = f / sampleRate;
        v.phase[(size_t) k] += dt;
        if (v.phase[(size_t) k] >= 1.0) v.phase[(size_t) k] -= 1.0;
        return oscillator (square, v.phase[(size_t) k], dt);
    }

    double noise() { return random.nextFloat() * 2.0 - 1.0; }

    // Suma de las 6 cuadradas del metal de la 808 (afinación de fábrica; Tune las desplaza juntas)
    double metal808 (Voice& v)
    {
        static constexpr double f[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        double s = 0.0;
        for (int k = 0; k < 6; ++k)
            s += squareOsc (v, k, f[k] * v.settings.tune);
        return s / 6.0;
    }

    void setupFilters (DrumVoice id, Voice& v)
    {
        const auto sr = sampleRate;
        auto& f = v.filter;
        switch (id)
        {
            case DrumVoice::SD808: f[0].set (2, 2750.0, 0.73, sr); f[1].set (1, 17900.0, 0.707, sr); break;
            case DrumVoice::SD78:  f[0].set (2, 2000.0, 0.707, sr); f[1].set (1, 12000.0, 0.707, sr); break; // grabación real (Freesound)
            case DrumVoice::CP808: f[0].set (0, 1271.0, 1.48, sr); break;
            case DrumVoice::MA808: f[0].set (2, 1060.0, 0.5, sr); f[1].set (2, 10600.0, 2.27, sr); break;
            case DrumVoice::RS808: f[0].set (2, 589.0, 2.87, sr); break;
            case DrumVoice::CB808: f[0].set (0, 874.0, 4.5, sr); break;
            case DrumVoice::CB78:  f[0].set (0, 800.0, 3.0, sr); break;                   // CR-78: ajustado a la grabación real
            case DrumVoice::CY808: f[0].set (0, 3440.0, 1.2, sr); f[1].set (0, 7100.0, 5.6, sr); f[2].set (2, 10500.0, 1.5, sr);
                                   f[3].set (2, 3440.0, 0.707, sr); break; // paso alto de la banda baja: 808 real y TR-08 sin energía < 1 kHz
            case DrumVoice::OH808: f[0].set (0, 7100.0, 5.6, sr); f[1].set (2, 7830.0, 2.51, sr); f[2].set (2, 8680.0, 0.5, sr); break;
            case DrumVoice::CH808: f[0].set (0, 7100.0, 5.6, sr); f[1].set (2, 11750.0, 2.51, sr); f[2].set (2, 10300.0, 0.5, sr); break;
            case DrumVoice::HH78:
            case DrumVoice::CY78:
            case DrumVoice::M78:   f[0].set (0, 9100.0, 3.0, sr); f[1].set (2, 6000.0, 0.707, sr); break; // tanque LC + paso alto (grabación)
            case DrumVoice::TB78:  f[0].set (0, 4300.0, 4.0, sr); f[1].set (0, 4300.0, 4.0, sr); break; // pico estrecho de la grabación real (2 pasabanda)
            case DrumVoice::GU78S:
            case DrumVoice::GU78L: f[0].set (0, 6130.0, 8.0, sr); break;                  // tanque L4/C532
            case DrumVoice::MB78:  f[0].set (2, 3000.0, 0.7, sr); break;
            // Mini Pops 7: frecuencias de los resonadores leídas en el esquema "MP-7 over-all circuit diagram" (Tubbutec)
            case DrumVoice::SN1MP: f[0].set (0, 5500.0, 1.5, sr); f[1].set (2, 2000.0, 0.7, sr); break;
            case DrumVoice::SN2MP: f[0].set (0, 5500.0, 1.0, sr); f[1].set (2, 1500.0, 0.7, sr); break;
            case DrumVoice::HHMP:  f[0].set (0, 5600.0, 4.0, sr); break;
            case DrumVoice::CY1MP:
            case DrumVoice::CY2MP: f[0].set (2, 5600.0, 2.0, sr); break;
            case DrumVoice::TAMP:  f[0].set (0, 4600.0, 3.0, sr); f[1].set (2, 3000.0, 0.7, sr); break;
            case DrumVoice::GUMP:  f[0].set (0, 4100.0, 3.0, sr); break;
            case DrumVoice::QUMP:  f[0].set (0, 2700.0, 4.0, sr); break;
            default: break;
        }
    }

    float renderVoice (DrumVoice id, Voice& v)
    {
        const auto sr = sampleRate;
        const auto t = v.t;
        const auto tu = v.settings.tune, dc = v.settings.decay;
        const auto tone = (double) v.settings.tone, snappy = (double) v.settings.snappy;
        double y = 0.0, life = 1.0; // life: decay más largo de la voz, para apagarla
        auto tom = [&] (double f0, double decayT, double blip)
        {
            // Diodos: a gran amplitud la frecuencia sube y vuelve (curva estimada: +blip, τ 20 ms)
            const auto f = f0 * tu * (1.0 + blip * std::exp (-t / 0.020));
            life = decayT * dc;
            return ring (v, 0, f, life, sr);
        };

        switch (id)
        {
            case DrumVoice::BD808:
            {
                // 56 Hz, T 50/300/800 ms (300 por defecto). Clic: +1,2 oct durante ~6 ms; "pitch sigh" lento (DAFx-14).
                // La saturación alarga la cola: T interno 240 ms -> 300 ms medidos en la salida (scratchpad/drum_measure.py)
                const auto f = 56.0 * tu * (1.0 + 1.3 * std::exp (-t / 0.0015) + 0.04 * std::exp (-t / 0.1));
                life = 0.24 * dc;
                const auto body = ring (v, 0, f, life, sr);
                y = std::tanh ((1.2 + tone) * body);
                break;
            }
            case DrumVoice::SD808:
            {
                // Dos doble T a 238 y 476 Hz (T 60 ms); Tone mezcla las dos; Snappy: ruido con paso alto
                const auto lo = ring (v, 0, 238.0 * tu, 0.06 * dc, sr), hi = ring (v, 1, 476.0 * tu, 0.045 * dc, sr);
                // Snappy: dos polos (τ 14,9 y 0,7 ms) -> 10 % a los 25 ms
                const auto snapEnv = 1.05 * std::exp (-t / (0.0149 * dc)) - 0.05 * std::exp (-t / 0.0007);
                const auto n = v.filter[1].process (v.filter[0].process (noise())) * snapEnv;
                life = 0.07 * dc;
                y = 0.8 * ((1.0 - 0.6 * tone) * lo + (0.4 + 0.6 * tone) * hi) + 1.6 * snappy * n;
                break;
            }
            case DrumVoice::LT808:
            {
                y = tom (90.0, 0.2, 0.25);
                v.pink += 0.05f * ((float) noise() - v.pink); // ruido rosado aproximado (un polo), solo en LT
                y += 0.15 * v.pink * decay (t, 0.25 * dc);
                life = 0.25 * dc;
                break;
            }
            case DrumVoice::MT808: y = tom (135.0, 0.13, 0.25); break;
            case DrumVoice::HT808: y = tom (185.0, 0.10, 0.25); break;
            case DrumVoice::LC808: y = tom (185.0, 0.18, 0.12); break;
            case DrumVoice::MC808: y = tom (280.0, 0.10, 0.12); break;
            case DrumVoice::HC808: y = tom (400.0, 0.08, 0.12); break;
            case DrumVoice::RS808:
            {
                // 455 + 1667 Hz, T 10 ms; el VCA "swing" añade armónicos (aquí: saturación) y paso alto
                // T interno 6,5 ms: la saturación lo lleva a los 10 ms de la tabla. Sallen-Key de 589 Hz Q 2,87 (resonante)
                const auto s = ring (v, 0, 455.0 * tu, 0.0052 * dc, sr) + ring (v, 1, 1667.0 * tu, 0.0052 * dc, sr);
                y = 0.5 * v.filter[0].process (std::tanh (3.0 * s));
                life = 0.01 * dc;
                break;
            }
            case DrumVoice::CL808: y = ring (v, 0, 2500.0 * tu, 0.025 * dc, sr); life = 0.025 * dc; break;
            case DrumVoice::CP808:
            {
                // Dientes a 0, 11 y 22 ms (τ 2,6 ms, media de 2,1-3,2) y el cuarto a 30 ms con τ 33 ms; "reverb" de 2 polos
                // (12 y 75 ms) por otro VCA, a nivel bajo (mezcla estimada)
                const auto tooth = [t] (double at, double tau) { return t >= at ? std::exp (-(t - at) / tau) : 0.0; };
                const auto teeth = std::max ({ tooth (0.0, 0.0026), tooth (0.011, 0.0026), tooth (0.022, 0.0026), tooth (0.030, 0.033 * dc) });
                const auto reverb = 0.35 * (std::exp (-t / (0.075 * dc)) - std::exp (-t / 0.012));
                y = 3.5 * v.filter[0].process (noise()) * std::max (teeth, reverb);
                life = 0.03 + 0.15 * dc;
                break;
            }
            case DrumVoice::MA808:
            {
                // Ataque τ 15,5 ms mientras conduce Q67 (~26 ms) y caída τ 3,3 ms (esquema)
                const auto rise = 1.0 - std::exp (-std::min (t, 0.026) / 0.0155);
                const auto env = t < 0.026 ? rise : rise * std::exp (-(t - 0.026) / (0.0033 * dc));
                y = 0.8 * v.filter[1].process (v.filter[0].process (noise())) * env;
                life = 0.026 + 0.008 * dc;
                break;
            }
            case DrumVoice::CB808:
            case DrumVoice::CB78:
            {
                // 540 + 800 Hz (808) / 555 + 800 Hz (CR-78); caída brusca inicial y cola (T total 50 / 60 ms)
                const auto lo = id == DrumVoice::CB808 ? 540.0 : 555.0;
                const auto s = squareOsc (v, 0, lo * tu) + squareOsc (v, 1, 800.0 * tu);
                const auto tail = (id == DrumVoice::CB808 ? 0.025 : 0.029) * dc; // medido: T 50 / 60 ms
                y = (id == DrumVoice::CB808 ? 2.0 : 1.2) * v.filter[0].process (s) * (0.6 * std::exp (-t / 0.003) + 0.4 * std::exp (-t / tail));
                life = 4.0 * tail;
                break;
            }
            case DrumVoice::CY808:
            {
                // Tres bandas con envolventes distintas: la baja sigue Decay, las altas son más cortas. T interno de la baja
                // 1,1 s: el batido de las cuadradas hunde la cola y la salida mide los 800 ms de la tabla
                const auto m = metal808 (v);
                const auto low = v.filter[3].process (v.filter[0].process (m)) * decay (t, 1.1 * dc);
                const auto mid = v.filter[1].process (m) * decay (t, 0.4 * dc);
                const auto high = v.filter[2].process (m) * decay (t, 1.1 * dc); // las grabaciones conservan el brillo > 1,5 s
                // Pesos ajustados al espectro de una 808 real y de una TR-08 (máximo en 8 kHz); 0,41 conserva el RMS anterior
                y = 0.41 * (low + 0.5 * mid + 8.0 * (0.5 + tone) * high);
                life = 1.1 * dc;
                break;
            }
            case DrumVoice::OH808:
            case DrumVoice::CH808:
            {
                const auto T = (id == DrumVoice::OH808 ? 0.535 : 0.066) * dc; // medidos en la salida: 450 / 50 ms (batido)
                const auto gain = id == DrumVoice::OH808 ? 15.0 : 37.0; // niveles igualados (los filtros resonantes atenúan)
                y = gain * v.filter[2].process (v.filter[1].process (v.filter[0].process (metal808 (v)))) * decay (t, T);
                life = T;
                break;
            }
            case DrumVoice::BD78:
            {
                // 62,5 Hz, T 100 ms; golpe de tono corto (estimado, más suave que en la 808)
                const auto f = 62.5 * tu * (1.0 + 0.6 * std::exp (-t / 0.002));
                life = 0.081 * dc; // T medido en la salida (con saturación): 100 ms
                y = std::tanh ((1.2 + tone) * ring (v, 0, f, life, sr));
                break;
            }
            case DrumVoice::SD78:
            {
                // 340 Hz, T 60 ms; el ruido ocupa unos 20 ms (figura de la p. 15)
                const auto drum = ring (v, 0, 340.0 * tu, 0.06 * dc, sr);
                const auto n = v.filter[1].process (v.filter[0].process (noise())) * decay (t, 0.02 + 0.06 * snappy * dc);
                y = 0.2 * drum + 1.6 * n; // grabación real: el ruido domina y sube hasta 8 kHz
                life = 0.09 * dc;
                break;
            }
            case DrumVoice::RS78:  y = ring (v, 0, 1480.0 * tu, 0.005 * dc, sr); life = 0.006 * dc; break;
            case DrumVoice::C78:   y = ring (v, 0, 2630.0 * tu, 0.018 * dc, sr); life = 0.018 * dc; break;
            // Bongos y conga: la grabación de un CR-78 real cae a -20 dB en 22-24 ms y 80 ms (la tabla del manual da 40 y 150)
            case DrumVoice::HB78:  y = tom (600.0, 0.023, 0.05); break;
            case DrumVoice::LB78:  y = tom (400.0, 0.023, 0.05); break;
            case DrumVoice::LC78:  y = tom (208.0, 0.08, 0.05); break;
            case DrumVoice::HH78:  y = 2.4 * v.filter[1].process (v.filter[0].process (noise())) * decay (t, 0.05 * dc); life = 0.05 * dc; break; // grabación: 47 ms
            case DrumVoice::CY78:
            {
                // Grabación real: -20 dB a 304 ms y -40 dB solo 72 ms después -> caída de T 0,35 s y cierre rápido (τ 15,6 ms) tras ella
                const auto T = 0.35 * dc;
                const auto env = t < T ? decay (t, T) : 0.1 * std::exp (-(t - T) / 0.0156);
                y = 2.1 * v.filter[1].process (v.filter[0].process (noise())) * env;
                life = T + 0.08;
                break;
            }
            case DrumVoice::M78:   y = 2.2 * v.filter[1].process (v.filter[0].process (noise())) * decay (t, 0.02 * dc); life = 0.02 * dc; break;
            case DrumVoice::TB78:
            {
                // Ruido en banda alta con modulación de "sonajas" (≈35 Hz, estimado), T 220 ms
                const auto jingle = 0.6 + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 35.0 * t);
                y = 4.0 * v.filter[1].process (v.filter[0].process (noise())) * jingle * decay (t, 0.22 * dc);
                life = 0.22 * dc;
                break;
            }
            case DrumVoice::GU78S:
            case DrumVoice::GU78L:
            {
                // Güiro: astable que golpea un tanque LC de 6,13 kHz a 125 Hz (corto) / 77 Hz (largo): tren de clics resonantes,
                // sin ruido (esquema VG-11A). Duraciones estimadas
                const auto rate = id == DrumVoice::GU78S ? 125.0 : 77.0;
                const auto len = id == DrumVoice::GU78S ? 0.10 : 0.30;
                v.phase[0] += rate * tu / sr;
                const auto click = v.phase[0] >= 1.0 ? 1.0 : 0.0;
                if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
                y = 12.0 * v.filter[0].process (t < len * dc ? click : 0.0);
                life = len * dc;
                break;
            }
            case DrumVoice::MB78:
            {
                // Metal beat: tres osciladores a 6170 / 5620 / 4080 Hz, T 50 ms
                const auto s = squareOsc (v, 0, 6170.0 * tu) + squareOsc (v, 1, 5620.0 * tu) + squareOsc (v, 2, 4080.0 * tu);
                y = 0.6 * v.filter[0].process (s) * decay (t, 0.05 * dc);
                life = 0.05 * dc;
                break;
            }
            // Mini Pops 7 (1966): resonadores LC y ruido filtrado, sin circuitos integrados. Frecuencias del esquema (Tubbutec);
            // decays no publicados: estimados. La asignación 860 / 4600 Hz del cencerro no está verificada
            // Decays de bombo, caja 1 y platillo 1 medidos en una Mini Pops 45 real (Freesound 267593/267613/267612; circuito hermano)
            case DrumVoice::BDMP:  y = std::tanh (1.3 * ring (v, 0, 65.0 * tu, 0.10 * dc, sr)); life = 0.10 * dc; break;
            case DrumVoice::SN1MP:
            case DrumVoice::SN2MP:
            {
                const auto T = (id == DrumVoice::SN1MP ? 0.12 : 0.25) * dc;
                const auto tick = t < 0.001 ? 1.0 - t / 0.001 : 0.0; // "tick" del ataque
                y = 2.0 * v.filter[1].process (v.filter[0].process (noise())) * decay (t, T) * (0.5 + snappy) + 0.6 * tick;
                life = T;
                break;
            }
            case DrumVoice::HHMP:  y = 2.5 * v.filter[0].process (noise()) * decay (t, 0.06 * dc); life = 0.06 * dc; break;
            case DrumVoice::CY1MP:
            case DrumVoice::CY2MP:
            {
                if (id == DrumVoice::CY1MP)
                {
                    // Dos tramos (medido): -20 dB a los 70 ms y cola hasta -40 dB a ~530 ms
                    y = 1.4 * v.filter[0].process (noise()) * (decay (t, 0.05 * dc) + 0.1 * decay (t, 0.46 * dc));
                    life = 0.5 * dc;
                    break;
                }
                const auto T = 0.9 * dc;
                y = 1.4 * v.filter[0].process (noise()) * decay (t, T);
                life = T;
                break;
            }
            case DrumVoice::TAMP:
            {
                const auto jingle = 0.6 + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 30.0 * t);
                y = 2.5 * v.filter[1].process (v.filter[0].process (noise())) * jingle * decay (t, 0.2 * dc);
                life = 0.2 * dc;
                break;
            }
            case DrumVoice::CBMP:  // dos osciladores, cada uno con su decay
                y = 0.9 * ring (v, 0, 860.0 * tu, 0.12 * dc, sr) + 0.5 * ring (v, 1, 4600.0 * tu, 0.04 * dc, sr);
                life = 0.12 * dc;
                break;
            case DrumVoice::COMP:
                y = ring (v, 0, 195.0 * tu, 0.18 * dc, sr) + 0.3 * ring (v, 1, 390.0 * tu, 0.05 * dc, sr);
                life = 0.18 * dc;
                break;
            case DrumVoice::SBMP:  y = ring (v, 0, 600.0 * tu, 0.06 * dc, sr); life = 0.06 * dc; break;
            case DrumVoice::LBMP:  y = ring (v, 0, 400.0 * tu, 0.08 * dc, sr); life = 0.08 * dc; break;
            case DrumVoice::CLMP:  y = ring (v, 0, 2200.0 * tu, 0.04 * dc, sr); life = 0.04 * dc; break;
            case DrumVoice::RSMP:  y = ring (v, 0, 860.0 * tu, 0.025 * dc, sr); life = 0.025 * dc; break;
            case DrumVoice::GUMP:
            case DrumVoice::QUMP:
            {
                // Trenes de pulsos filtrados: güiro con puerta (65 pulsos/s durante 150 ms) y quijada que se frena
                // (de 25 pulsos/s, sonajero de 400 ms). Velocidades y duraciones estimadas
                const auto guiro = id == DrumVoice::GUMP;
                const auto len = (guiro ? 0.15 : 0.4) * dc;
                const auto rate = (guiro ? 65.0 : 25.0 * (1.0 - 0.5 * t / len)) * tu;
                v.phase[0] += rate / sr;
                const auto pulse = v.phase[0] >= 1.0 ? 1.0 : 0.0;
                if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
                const auto env = guiro ? 1.0 : decay (t, len);
                y = t < len ? 6.0 * v.filter[0].process (pulse) * env : v.filter[0].process (0.0);
                life = len;
                break;
            }
            case DrumVoice::count: break;
        }

        v.t += 1.0 / sr;
        if (v.t > life * 2.0 + 0.005) // 2T = -40 dB
            v.active = false;
        return 0.5f * (float) y;
    }

    double sampleRate = 44100.0;
    std::array<Voice, (size_t) DrumVoice::count> voices;
    juce::Random random;
};

// Instrumento de patrón -> voz de cada kit (-1: el kit no lo tiene)
// Parámetro drumKit (por año en la UI: Mini Pops 7, CR-78, TR-808) -> kit interno (0 TR-808, 1 CR-78, 2 Mini Pops 7)
static int kitFromParam (float value) { return 2 - juce::jlimit (0, 2, (int) value); }

static int slotVoice (int kit, DrumSlot slot)
{
    using V = DrumVoice;
    static constexpr V none = V::count;
    static const V k808[] = { V::BD808, V::SD808, V::RS808, V::CP808, V::CH808, V::OH808, V::CY808, V::CB808,
                              V::LT808, V::HT808, V::LC808, V::MA808, V::CL808, none, none };
    static const V k78[]  = { V::BD78, V::SD78, V::RS78, none, V::HH78, V::CY78, V::CY78, V::CB78,
                              V::LB78, V::HB78, V::LC78, V::M78, V::C78, V::TB78, V::GU78S };
    // Mini Pops 7: CP -> Snare 2, MA -> quijada (no tiene maracas)
    static const V kMP[]  = { V::BDMP, V::SN1MP, V::RSMP, V::SN2MP, V::HHMP, V::CY1MP, V::CY2MP, V::CBMP,
                              V::LBMP, V::SBMP, V::COMP, V::QUMP, V::CLMP, V::TAMP, V::GUMP };
    const auto i = (size_t) slot;
    if (i >= std::size (k808))
        return -1;
    const auto v = kit == 0 ? k808[i] : kit == 1 ? k78[i] : kMP[i];
    return v == none ? -1 : (int) v;
}

// Teclado -> voz, con el mapa de percusión General MIDI (notas 35..81)
static int gmVoice (int kit, int note)
{
    using V = DrumVoice;
    if (kit == 2) // Mini Pops 7. La quijada va en 58 (Vibraslap: la versión moderna de la quijada)
        switch (note)
        {
            case 35: case 36: return (int) V::BDMP;
            case 37:          return (int) V::RSMP;
            case 38:          return (int) V::SN1MP;
            case 39: case 40: return (int) V::SN2MP;
            case 42: case 44: return (int) V::HHMP;
            case 46:          return (int) V::CY1MP;
            case 49: case 51: case 52: case 55: case 57: case 59: return (int) V::CY2MP;
            case 54:          return (int) V::TAMP;
            case 56:          return (int) V::CBMP;
            case 58:          return (int) V::QUMP;
            case 60:          return (int) V::SBMP;
            case 61:          return (int) V::LBMP;
            case 62: case 63: case 64: return (int) V::COMP;
            case 73: case 74: return (int) V::GUMP;
            case 75:          return (int) V::CLMP;
            default:          return -1;
        }
    switch (note)
    {
        case 35: case 36: return (int) (kit == 0 ? V::BD808 : V::BD78);
        case 37:          return (int) (kit == 0 ? V::RS808 : V::RS78);
        case 38: case 40: return (int) (kit == 0 ? V::SD808 : V::SD78);
        case 39:          return kit == 0 ? (int) V::CP808 : -1;
        case 42: case 44: return (int) (kit == 0 ? V::CH808 : V::HH78);
        case 41: case 43: case 45: return (int) (kit == 0 ? V::LT808 : V::LC78);
        case 47: case 48: return kit == 0 ? (int) V::MT808 : -1;
        case 50:          return kit == 0 ? (int) V::HT808 : -1;
        case 46:          return (int) (kit == 0 ? V::OH808 : V::HH78);
        case 49: case 51: case 52: case 55: case 57: case 59: return (int) (kit == 0 ? V::CY808 : V::CY78);
        case 54:          return kit == 1 ? (int) V::TB78 : -1;
        case 56:          return (int) (kit == 0 ? V::CB808 : V::CB78);
        case 60:          return kit == 1 ? (int) V::HB78 : -1;
        case 61:          return kit == 1 ? (int) V::LB78 : -1;
        case 62:          return (int) (kit == 0 ? V::HC808 : V::HB78);
        case 63:          return (int) (kit == 0 ? V::MC808 : V::LB78);
        case 64:          return (int) (kit == 0 ? V::LC808 : V::LC78);
        case 70:          return (int) (kit == 0 ? V::MA808 : V::M78);
        case 73:          return kit == 1 ? (int) V::GU78S : -1;
        case 74:          return kit == 1 ? (int) V::GU78L : -1;
        case 75:          return (int) (kit == 0 ? V::CL808 : V::C78);
        case 81:          return kit == 1 ? (int) V::MB78 : -1;
        default:          return -1;
    }
}

//==============================================================================
// Motor caja de ritmos: kit TR-808 o CR-78 tocado desde el teclado (mapa GM) y/o un patrón con reloj propio o del host
struct DrumMachine
{
    void prepare (double sr) { sampleRate = sr; kit.prepare (sr); reset(); }
    void reseed (juce::int64 seed) { kit.reseed (seed); }
    void reset() { kit.reset(); clock.restart(); pattern = pattern2 = -2; ledStep.store (-1, std::memory_order_relaxed); }

    // LED encendido de la fila de pasos (se lee desde la UI): posición en el compás del último paso, 0..15; -1 parado
    int currentLed() const noexcept { return ledStep.load (std::memory_order_relaxed); }
    // Pasos disparados desde el inicio del patrón (-1 parado): el editor de ritmos marca el paso step % numSteps
    long long currentStep() const noexcept { return stepCount.load (std::memory_order_relaxed); }

    // Patrón por índice del parámetro (0 = Off ya restado): de fábrica y, a continuación, las ranuras User 1-8 del banco de
    // usuario (Engines/UserRhythms.h). Ranura vacía o sin banco: nullptr (no suena)
    static const RhythmPattern* patternAt (int index, const UserRhythms::Bank* userBank)
    {
        const auto factory = (int) drumPatterns().size();
        if (index < 0) return nullptr;
        if (index < factory) return &drumPatterns()[(size_t) index];
        return userBank != nullptr ? userBank->pattern (index - factory) : nullptr;
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, const StepClock::Transport& transport,
                 const UserRhythms::Bank* userBank)
    {
        const auto kitIndex = kitFromParam (p.drumKit->load());
        DrumKit::Settings s;
        s.tune = std::exp2 (p.drumTune->load() / 12.0);
        s.decay = std::exp2 (2.0 * p.drumDecay->load()); // -1..1 -> ×0,25..×4
        s.tone = p.drumTone->load();
        s.snappy = p.drumSnappy->load();
        const auto accent = p.drumAccent->load();
        const auto continuousVelocity = p.drumVelocity->load() > 0.5f;

        const auto newPattern = (int) p.drumPattern->load() - 1; // 0 = Off
        const auto newPattern2 = (int) p.drumPattern2->load() - 1;
        if (newPattern != pattern || newPattern2 != pattern2)
        {
            pattern = newPattern;
            pattern2 = newPattern2;
            clock.restart();
        }

        // Pasos del patrón dentro del bloque. Acento ('X'): 0,75 + 0,5·Accent (1 con Accent al 50 %)
        struct Hit { int sample, voice; float velocity; };
        std::array<Hit, 64> hits;
        int numHits = 0;
        // Contador de 16 posiciones por compás, como el de 32 pasos (2 compases) del Mini Pops 7 (manual de Bucket Pops,
        // p. 5): los ritmos de vals saltan las posiciones 4-5 y 8-9; los de tresillo, una de cada 4 y van a 3 pasos por
        // tiempo. Con dos patrones se suman los golpes (matriz de diodos = OR) y los saltos (supuesto: OR, no publicado).
        // Así, Slow Rock + Rock suena con el Rock recorrido a saltos, que es el efecto de los ritmos de Oxygène.
        const std::array<const RhythmPattern*, 2> active { patternAt (pattern, userBank), patternAt (pattern2, userBank) };
        std::array<unsigned, 2> ownSkips {};
        unsigned skips = 0;
        auto stepsPerBeat = 4;
        for (size_t k = 0; k < 2; ++k)
            if (active[k] != nullptr)
            {
                ownSkips[k] = skipMask (*active[k]);
                skips |= ownSkips[k];
                if (active[k]->stepsPerBeat == 3)
                    stepsPerBeat = 3;
            }
        std::array<int, 16> playable {};
        int numPlayable = 0;
        for (int c = 0; c < 16; ++c)
            if ((skips >> c & 1u) == 0)
                playable[(size_t) numPlayable++] = c;
        const auto anyPattern = active[0] != nullptr || active[1] != nullptr;
        if (anyPattern && numPlayable > 0)
        {
            clock.process (out.getNumSamples(), sampleRate, (double) p.drumTempo->load(), stepsPerBeat,
                           stepsPerBeat == 4 ? (double) p.drumSwing->load() : 0.0, transport,
                [&] (int sample, long long step)
                {
                    const auto cycle = step / numPlayable;
                    const auto c = playable[(size_t) (step % numPlayable)];
                    for (size_t k = 0; k < 2; ++k)
                    {
                        if (active[k] == nullptr)
                            continue;
                        const auto& pat = *active[k];
                        // Índice propio: posición entre las que el patrón no salta por sí mismo
                        int rank = 0;
                        for (int q = 0; q < c; ++q)
                            rank += (ownSkips[k] >> q & 1u) == 0 ? 1 : 0;
                        const auto own = 16 - juce::countNumberOfBits (ownSkips[k]);
                        const auto idx = (size_t) ((cycle * own + rank) % pat.numSteps);
                        for (const auto& [slot, lane] : pat.lanes)
                            if (const auto vel = stepVelocity (lane[idx]); vel > 0.0f)
                                if (const auto voice = slotVoice (kitIndex, slot); voice >= 0 && numHits < (int) hits.size())
                                    hits[(size_t) numHits++] = { sample, voice, vel < 1.0f ? vel : 0.75f + 0.5f * accent };
                    }
                });
        }

        auto midiIt = midi.cbegin();
        int hit = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
            {
                const auto m = (*midiIt).getMessage();
                if (m.isNoteOn())
                    if (const auto voice = gmVoice (kitIndex, m.getNoteNumber()); voice >= 0)
                    {
                        // Como el aparato (por defecto): nivel normal (0,75, el "x" de los patrones) o acento desde velocidad
                        // 100 (la 808 tiene un único nivel de acento global). Drum Velocity: continua (127 = acento)
                        const auto vel = m.getFloatVelocity();
                        const auto level = continuousVelocity ? (vel >= 0.99f ? 0.75f + 0.5f * accent : vel)
                                                              : (m.getVelocity() >= 100 ? 0.75f + 0.5f * accent : 0.75f);
                        kit.trigger ((DrumVoice) voice, level, s);
                    }
                if (m.isAllSoundOff())
                    kit.reset();
            }
            for (; hit < numHits && hits[(size_t) hit].sample <= i; ++hit)
                kit.trigger ((DrumVoice) hits[(size_t) hit].voice, hits[(size_t) hit].velocity, s);

            const auto y = kit.render();
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, y);
        }
        // Luz que recorre los pasos: posición entre las que suenan (un compás de 12 pasos usa los LED 1-12, como la TR-808
        // con PRE-SCALE de tresillo). Parado sin patrón o con el transporte del host detenido
        const auto running = anyPattern && numPlayable > 0 && clock.lastFired >= 0;
        ledStep.store (running ? (int) (clock.lastFired % numPlayable) : -1, std::memory_order_relaxed);
        stepCount.store (running ? clock.lastFired : -1, std::memory_order_relaxed);
    }

    // Posiciones (de 16) que salta cada tipo de patrón: 12 pasos a 3 por tiempo = tresillo; 12 a 4 por tiempo = vals
    static unsigned skipMask (const RhythmPattern& pat)
    {
        if (pat.numSteps == 12 && pat.stepsPerBeat == 3) return (1u << 3) | (1u << 7) | (1u << 11) | (1u << 15);
        if (pat.numSteps == 12 && pat.stepsPerBeat == 4) return (1u << 4) | (1u << 5) | (1u << 8) | (1u << 9);
        return 0;
    }

    double sampleRate = 44100.0;
    DrumKit kit;
    StepClock clock;
    int pattern = -2, pattern2 = -2;
    std::atomic<int> ledStep { -1 };
    std::atomic<long long> stepCount { -1 };
};

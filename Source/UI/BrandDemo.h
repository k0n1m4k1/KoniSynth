// Modo DEMO del display: el bucle de escaparate de las pletinas de coche de los 90 (pantalla gráfica de matriz de puntos) con
// el logo "KoniSynth". Matriz de 24 filas de puntos cuadrados (2 px de punto + 1 de separación a tamaño nativo), en el tono
// del fósforo del estilo del display; los puntos apagados se ven al 8 % y un halo tenue ilumina las separaciones.
// Secuencia de 11 animaciones, 39 s por vuelta, sin fundidos (corte seco, como los aparatos):
//   vuelo 3D, balanceo 3D, shooter en cabina, bandera, carrera, rodillo, plasma, explosión, fuego, sine scroller y zoom.
// El logo 3D es una nube de 3996 vóxeles (fuente DotMatrix 5×7 de UI/Displays.h, 3×3 submuestras por píxel de fuente y
// 4 capas de extrusión), proyectada en perspectiva con z-buffer, niebla por profundidad y cara frontal más brillante.
// Híbrido con el audio: en silencio (todas las bandas < -60 dB durante 2 s) es un bucle autónomo; sonando, las 48 bandas del
// analizador (20 Hz - 20 kHz) modulan cada animación (golpes, nivel, graves, centroide) y la nota MIDI más aguda pulsada
// mueve el coche y la mira. Solo UI: no toca el audio. Determinista (semilla fija). Se incluye desde KoniSynth.cpp (mismo
// TU), tras UI/Displays.h (DotMatrix) y antes de UI/Visualizer.h.
#pragma once

class BrandDemo
{
public:
    static constexpr int numBands = 48, rows = 24;

    BrandDemo() { buildLogo(); }

    // Un fotograma a 30 Hz: bandas en dB, nota MIDI más aguda pulsada (-1 ninguna), columnas de la matriz
    void update (const std::array<float, numBands>& bandsDb, int highestNote, float dt, int columns)
    {
        if (columns != cols)
        {
            cols = juce::jmax (60, columns);
            level.assign ((size_t) (rows * cols), 0.0f);
            zbuf.assign ((size_t) (rows * cols), 0.0f);
            heat.assign ((size_t) ((rows + 2) * cols), 0.0f);
        }
        analyse (bandsDb, highestNote, dt);

        // Paso de la secuencia
        clock += dt;
        if (clock >= steps[(size_t) step].seconds)
        {
            clock = 0.0f;
            frame = 0;
            step = (step + 1) % (int) steps.size();
            enemies.clear();
            sceneStart();
        }
        std::fill (level.begin(), level.end(), 0.0f);
        switch (steps[(size_t) step].scene)
        {
            case flyIn:    sceneFly(); break;
            case sway:     sceneSway(); break;
            case cockpit:  sceneCockpit(); break;
            case flag:     sceneFlag(); break;
            case racer:    sceneRacer(); break;
            case roller:   sceneRoll(); break;
            case plasma:   scenePlasma(); break;
            case explode:  sceneExplode(); break;
            case fire:     sceneFire(); break;
            case scroller: sceneScroller(); break;
            default:       sceneZoom(); break;
        }
        ++frame;
    }

    // Pinta la matriz en un lienzo de 3 px por punto (punto 2×2 + separación) y lo escala al área sin suavizado
    void paint (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink)
    {
        if (cols <= 0)
            return;
        const int w = cols * 3, h = rows * 3;
        if (canvas.getWidth() != w || canvas.getHeight() != h)
            canvas = juce::Image (juce::Image::RGB, w, h, false);
        const auto bg = juce::Colour (0xff05070d);
        const float br = bg.getFloatRed(), bgG = bg.getFloatGreen(), bb = bg.getFloatBlue();
        const float ir = ink.getFloatRed(), ig = ink.getFloatGreen(), ib = ink.getFloatBlue();
        const auto mix = [&] (float l, float base) // base · fondo + l · fósforo
        {
            const auto q = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, (int) (v * 255.0f)); };
            return juce::Colour (q (br * base + (ir - br) * l), q (bgG * base + (ig - bgG) * l), q (bb * base + (ib - bb) * l));
        };
        juce::Image::BitmapData px (canvas, juce::Image::BitmapData::writeOnly);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const auto l = juce::jlimit (0.08f, 1.0f, at (c, r));
                // Halo: media de los vecinos derecho e inferior, al 25 %, en las separaciones
                const auto halo = 0.25f * 0.5f * (at (juce::jmin (cols - 1, c + 1), r) + at (c, juce::jmin (rows - 1, r + 1)));
                const auto dot = mix (l, 1.0f), gap = mix (juce::jmax (halo, 0.0f), 0.6f);
                for (int y = 0; y < 3; ++y)
                    for (int x = 0; x < 3; ++x)
                        px.setPixelColour (c * 3 + x, r * 3 + y, (x < 2 && y < 2) ? dot : gap);
            }
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (1.0f); // drawImage usa la opacidad del último color fijado
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (canvas, area, juce::RectanglePlacement::stretchToFit);
    }

private:
    enum Scene { flyIn, sway, cockpit, flag, racer, roller, plasma, explode, fire, scroller, zoom };
    struct Step { Scene scene; float seconds; };
    // Orden elegido: logo y juego alternados, el shooter en tercer lugar; los juegos duran el doble
    const std::array<Step, 11> steps { { { flyIn, 3.0f }, { sway, 3.0f }, { cockpit, 6.0f }, { flag, 3.0f }, { racer, 6.0f },
                                         { roller, 3.0f }, { plasma, 3.0f }, { explode, 3.0f }, { fire, 3.0f },
                                         { scroller, 3.0f }, { zoom, 3.0f } } };
    struct Vox { float x, y, z; bool front; };
    struct Star { float x, y, z; };
    struct Enemy { float x, y, z; int boom; };

    int cols = 0, step = 0, frame = 0;
    float clock = 0.0f;
    std::vector<float> level, zbuf, heat;
    std::vector<Vox> logo, scratch;
    std::vector<Star> stars;
    std::vector<Enemy> enemies;
    juce::Image canvas;
    juce::Random rng { 0x5eed };

    // Rasgos del audio (0..1), ya ponderados por react (0 en silencio, 1 sonando; transición de ~0,5 s)
    std::array<float, numBands> avgDb {}, energyBand {};
    float react = 0.0f, quiet = 0.0f, hit = 0.0f, loud = 0.0f, bass = 0.0f, centroid = 0.5f;
    int note = -1;
    // Estado de escena
    float roll = 0.0f, rollTarget = 0.0f, burst = 0.0f, raceZ = 0.0f, shield = 1.0f, energy = 1.0f;

    float& at (int c, int r) { return level[(size_t) (r * cols + c)]; }
    float at (int c, int r) const { return level[(size_t) (r * cols + c)]; }
    void plot (float x, float y, float v, int maxRow = rows)
    {
        const auto c = juce::roundToInt (x), r = juce::roundToInt (y);
        if (c >= 0 && c < cols && r >= 0 && r < maxRow)
            at (c, r) = juce::jmax (at (c, r), v);
    }
    static float ease (float t) { t = juce::jlimit (0.0f, 1.0f, t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
    float seconds() const { return clock; }
    float sceneT() const { return clock / steps[(size_t) step].seconds; }

    //--------------------------------------------------------------------------------------------------------------------------
    // Audio: golpe (suma de subidas sobre la media de cada banda, como el arcade), nivel, graves (≤ 150 Hz) y centroide
    void analyse (const std::array<float, numBands>& db, int highestNote, float dt)
    {
        float rise = 0.0f, sum = 0.0f, low = 0.0f, moment = 0.0f;
        bool silent = true;
        for (int b = 0; b < numBands; ++b)
        {
            const auto d = db[(size_t) b];
            silent = silent && d < -60.0f;
            const auto e = juce::jlimit (0.0f, 1.0f, (d + 60.0f) / 60.0f);
            energyBand[(size_t) b] = e;
            if (d - avgDb[(size_t) b] > 0.0f) rise += (d - avgDb[(size_t) b]) * e;
            avgDb[(size_t) b] += 0.12f * (d - avgDb[(size_t) b]);
            sum += e;
            moment += e * (float) b;
            if (b < 14) low += e; // banda 13: hasta 20·1000^(14/48) ≈ 150 Hz
        }
        quiet = silent ? quiet + dt : 0.0f;
        react += ((quiet < 2.0f ? 1.0f : 0.0f) - react) * juce::jmin (1.0f, dt * 2.0f);
        hit = react * juce::jlimit (0.0f, 1.0f, rise / 120.0f);
        loud = react * sum / numBands;
        bass = react * low / 14.0f;
        centroid = sum > 0.01f ? 0.5f + react * (moment / sum / (numBands - 1) - 0.5f) : 0.5f;
        note = highestNote;
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // Logo 3D
    void buildLogo()
    {
        static constexpr const char* text = "KoniSynth";
        constexpr int sub = 3, depth = 4, len = 9;
        constexpr float logoW = len * 6 - 1;
        for (int k = 0; k < len; ++k)
            for (int r = 0; r < 7; ++r)
            {
                const auto bits = DotMatrix::glyphRow ((unsigned char) text[k], r);
                for (int c = 0; c < 5; ++c)
                    if (bits & (1 << (4 - c)))
                        for (int sx = 0; sx < sub; ++sx)
                            for (int sy = 0; sy < sub; ++sy)
                                for (int d = 0; d < depth; ++d)
                                    logo.push_back ({ (float) (k * 6 + c) + ((float) sx + 0.5f) / sub - logoW * 0.5f,
                                                      3.5f - ((float) r + ((float) sy + 0.5f) / sub),
                                                      ((float) d - (depth - 1) * 0.5f) * 0.4f, d == 0 });
            }
        for (int i = 0; i < 400; ++i)
            stars.push_back ({ rng.nextFloat() * 160.0f - 80.0f, rng.nextFloat() * 24.0f - 12.0f, rng.nextFloat() * 160.0f });
    }

    // Luz por vóxel según la orientación: cara frontal 0,35-1,0 según mire a la cámara, laterales de la extrusión al 40 %
    static float shade (const Vox& v, float ax, float ay)
    {
        return v.front ? 0.35f + 0.65f * std::abs (std::cos (ay) * std::cos (ax)) : 0.4f;
    }

    // Gira (x, y, z), traslada y proyecta con z-buffer. light(i) da el brillo del vóxel i
    template <typename Light>
    void renderVoxels (const std::vector<Vox>& pts, float ax, float ay, float az, float ox, float oy, float oz, Light light)
    {
        constexpr float cam = 40.0f, focal = 3.0f * cam; // el logo (53 columnas de fuente) ocupa ~160 puntos a z = cam
        std::fill (zbuf.begin(), zbuf.end(), 1.0e9f);
        const float ca = std::cos (ax), sa = std::sin (ax), cb = std::cos (ay), sb = std::sin (ay), cc = std::cos (az), sc = std::sin (az);
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const auto& p = pts[i];
            // Rx, después Ry y Rz (como la maqueta: R = Rz·Ry·Rx)
            const auto y1 = p.y * ca - p.z * sa, z1 = p.y * sa + p.z * ca;
            const auto x2 = p.x * cb + z1 * sb, z2 = -p.x * sb + z1 * cb;
            const auto x3 = x2 * cc - y1 * sc, y3 = x2 * sc + y1 * cc;
            const auto z = z2 + oz + cam;
            if (z <= 1.0f)
                continue;
            const auto c = juce::roundToInt ((float) cols * 0.5f + (x3 + ox) * focal / z);
            const auto r = juce::roundToInt (rows * 0.5f - (y3 + oy) * focal / z);
            if (c < 0 || c >= cols || r < 0 || r >= rows)
                continue;
            const auto idx = (size_t) (r * cols + c);
            if (z < zbuf[idx])
            {
                zbuf[idx] = z;
                const auto fog = juce::jlimit (0.15f, 1.0f, 1.4f - (z - cam) / 60.0f);
                level[idx] = light (i) * fog;
            }
        }
    }

    void renderLogo (float ax, float ay, float az, float oz)
    {
        renderVoxels (logo, ax, ay, az, 0.0f, 0.0f, oz, [&] (size_t i) { return shade (logo[i], ax, ay); });
    }

    void sceneStart()
    {
        rollTarget = roll = 0.0f;
        burst = 0.0f;
        std::fill (heat.begin(), heat.end(), 0.0f);
    }

    void drawStars (float speed, float bright, int maxRow, float rollAngle = 0.0f, int every = 1)
    {
        const auto cr = std::cos (rollAngle), sr = std::sin (rollAngle);
        const auto cy = (float) maxRow * 0.5f;
        for (size_t i = 0; i < stars.size(); i += (size_t) every)
        {
            auto& s = stars[i];
            s.z -= speed;
            if (s.z < 1.0f) s.z += 160.0f;
            const auto px = s.x * 60.0f / s.z, py = s.y * 60.0f / s.z;
            plot ((float) cols * 0.5f + px * cr - py * sr * 3.0f, cy - (px * sr / 3.0f + py * cr), // 3: pantalla apaisada
                  juce::jlimit (0.12f, bright, 1.2f - s.z / 140.0f), maxRow);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // 1) Vuelo desde el fondo girando 1,25 vueltas en Y y frenando de frente; estrellas en warp (los golpes las aceleran)
    void sceneFly()
    {
        const auto t = ease (seconds() / 2.2f);
        renderLogo (0.0f, (1.0f - t) * 2.5f * juce::MathConstants<float>::pi, 0.0f, (1.0f - t) * 260.0f);
        drawStars (4.0f * (1.0f + 3.0f * hit), 0.8f, rows);
    }

    // 2) Balanceo ±55° en Y y ±25° en X; el nivel del audio escala la amplitud (en silencio, la amplitud entera)
    void sceneSway()
    {
        const auto u = seconds(), amp = 1.0f - 0.5f * react + loud;
        const auto twoPi = juce::MathConstants<float>::twoPi;
        renderLogo (0.45f * amp * std::sin (twoPi * u / 1.5f), 0.95f * amp * std::sin (twoPi * u / 3.0f), 0.0f, 0.0f);
    }

    // 3) Bandera: onda en profundidad que recorre el logo; el centroide marca su frecuencia y el nivel su altura
    void sceneFlag()
    {
        const auto u = seconds();
        const auto k = 0.25f * (0.6f + 0.8f * centroid), amp = 6.0f * (1.0f - 0.5f * react + loud);
        scratch = logo;
        std::vector<float> light (logo.size());
        for (size_t i = 0; i < scratch.size(); ++i)
        {
            const auto ph = k * logo[i].x - juce::MathConstants<float>::twoPi * 1.2f * u;
            scratch[i].z += amp * std::sin (ph);
            scratch[i].y += 0.8f * std::sin (ph * 0.5f);
            light[i] = logo[i].front ? 0.55f + 0.45f * std::cos (ph) : 0.35f;
        }
        renderVoxels (scratch, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, [&] (size_t i) { return light[i]; });
    }

    // 4) Rodillo: una vuelta sobre X como un cuentakilómetros; cada golpe fuerte añade un cuarto de vuelta
    void sceneRoll()
    {
        if (hit > 0.35f && rollTarget - roll < 0.1f)
            rollTarget += juce::MathConstants<float>::halfPi;
        roll += (rollTarget - roll) * 0.25f;
        const auto a = ease (sceneT()) * juce::MathConstants<float>::twoPi + roll;
        renderLogo (a, 0.0f, 0.0f, 0.0f);
    }

    // 5) Explosión de los vóxeles en 3D y reensamblado; un golpe fuerte la dispara de nuevo
    void sceneExplode()
    {
        if (scratch.size() != logo.size() || frame == 0)
        {
            scratch = logo;
            juce::Random dir (0x5eed + 1);
            for (auto& v : scratch) // dirección por vóxel (gaussiana aproximada: suma de 3 uniformes)
            {
                const auto gauss = [&dir] { return (dir.nextFloat() + dir.nextFloat() + dir.nextFloat() - 1.5f) * 2.0f; };
                v = { gauss() * 30.0f, gauss() * 12.0f, gauss() * 60.0f, v.front };
            }
        }
        burst = juce::jmax (burst * 0.9f, hit > 0.5f ? hit : 0.0f);
        const auto s = std::sin (juce::MathConstants<float>::pi * sceneT());
        const auto k = juce::jmax (s * s, burst);
        const auto ay = 0.6f * std::sin (juce::MathConstants<float>::twoPi * sceneT());
        std::vector<Vox> moved (logo.size());
        for (size_t i = 0; i < logo.size(); ++i)
            moved[i] = { logo[i].x + scratch[i].x * k, logo[i].y + scratch[i].y * k, logo[i].z + scratch[i].z * k, logo[i].front };
        renderVoxels (moved, 0.0f, ay, 0.0f, 0.0f, 0.0f, 0.0f, [&] (size_t i) { return shade (logo[i], 0.0f, ay) * (1.0f - 0.3f * k); });
    }

    // 6) Zoom: el logo se acerca hasta atravesar la cámara y renace al fondo; un golpe adelanta el paso
    void sceneZoom()
    {
        const auto t = juce::jmin (1.0f, sceneT() + 0.15f * hit);
        const auto z = t < 0.6f ? 180.0f * (1.0f - t) - 35.0f : 180.0f * std::pow (1.0f - (t - 0.6f) / 0.4f, 3.0f);
        renderVoxels (logo, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, z, [&] (size_t i) { return logo[i].front ? 1.0f : 0.4f; });
    }

    // Máscara 2D del logo de frente (plasma y fuego)
    std::vector<bool> logoMask()
    {
        renderVoxels (logo, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, [] (size_t) { return 1.0f; });
        std::vector<bool> m (level.size());
        for (size_t i = 0; i < level.size(); ++i) { m[i] = level[i] > 0.0f; level[i] = 0.0f; }
        return m;
    }

    // 7) Plasma de la demoscene (suma de 4 senos) dentro del logo; fuera, al 12 %. Los graves aceleran y contrastan
    void scenePlasma()
    {
        const auto mask = logoMask();
        const auto u = seconds() * (1.0f + 2.0f * bass);
        const auto contrast = 0.65f + 0.35f * bass;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const auto x = (float) c, y = (float) r;
                auto p = (std::sin (x / 9.0f + u * 2.1f) + std::sin (y / 4.0f + u * 1.3f) + std::sin ((x + y * 3.0f) / 14.0f + u * 1.7f)
                          + std::sin (std::hypot (x - (float) cols * 0.5f - 60.0f * std::sin (u), y - rows * 0.5f) / 6.0f - u * 3.0f)) / 4.0f;
                p = 0.5f + 0.5f * p;
                at (c, r) = mask[(size_t) (r * cols + c)] ? (1.0f - contrast) + contrast * p : 0.12f * p;
            }
    }

    // 8) Fuego de la demoscene: el calor nace abajo y en el logo y sube con difusión y enfriamiento aleatorio. Sonando, cada
    //    banda calienta su tramo de la base (graves a la izquierda)
    void sceneFire()
    {
        const auto mask = logoMask();
        const auto H = rows + 2;
        const auto ht = [this] (int c, int r) -> float& { return heat[(size_t) (r * cols + c)]; };
        for (int c = 0; c < cols; ++c)
        {
            const auto band = energyBand[(size_t) juce::jmin (numBands - 1, c * numBands / cols)];
            const auto seed = rng.nextFloat() > 0.3f ? 0.6f + 0.4f * rng.nextFloat() : 0.0f;
            ht (c, H - 1) = (1.0f - react) * seed + react * band * (0.7f + 0.3f * rng.nextFloat());
        }
        const auto logoHeat = 0.9f * juce::jmin (1.0f, sceneT() * 3.0f);
        for (int r = 0; r < H - 1; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const auto below = ht (c, r + 1) + ht ((c + cols - 1) % cols, r + 1) + ht ((c + 1) % cols, r + 1) + ht (c, r);
                auto v = below / 4.0f - 0.09f * rng.nextFloat();
                if (r < rows && mask[(size_t) (r * cols + c)])
                    v = juce::jmax (v, logoHeat * (0.7f + 0.3f * rng.nextFloat()));
                ht (c, r) = juce::jlimit (0.0f, 1.0f, v);
            }
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const auto l = std::pow (ht (c, r), 1.3f);
                at (c, r) = mask[(size_t) (r * cols + c)] ? juce::jmax (l, 0.55f) : l;
            }
    }

    // 9) Sine scroller sobre 3 barras de cobre; sonando, cada barra brilla con su zona (graves, medios, agudos)
    void sceneScroller()
    {
        static constexpr const char* msg = "  KONISYNTH  *  15 ENGINES  *  60s TO 90s SYNTH LAB  *  ";
        static const int len = (int) std::strlen (msg);
        const auto u = seconds();
        for (int k = 0; k < 3; ++k)
        {
            float zone = 0.0f;
            for (int b = k * 16; b < k * 16 + 16; ++b) zone += energyBand[(size_t) b] / 16.0f;
            const auto cy = rows * 0.5f + (rows * 0.5f - 3.0f) * std::sin (u * 2.4f + (float) k * 2.1f);
            const auto bright = 0.45f * (1.0f - react) + react * (0.2f + 0.8f * zone);
            for (int r = 0; r < rows; ++r)
            {
                const auto v = bright * juce::jlimit (0.0f, 1.0f, 1.0f - std::abs ((float) r - cy) / 2.5f);
                for (int c = 0; c < cols; ++c) at (c, r) = juce::jmax (at (c, r), v);
            }
        }
        const auto scroll = u * 70.0f;
        for (int x = 0; x < cols; ++x)
        {
            const auto fc = (int) ((float) x + scroll) / 2;                      // columna de fuente (2 puntos de ancho)
            const auto ch = (unsigned char) msg[(fc / 6) % len];
            const auto col = fc % 6;
            if (col == 5) continue;
            const auto base = rows * 0.5f - 7.0f + 5.0f * std::sin ((float) x / 18.0f + u * 4.0f);
            for (int r = 0; r < 7; ++r)
                if (DotMatrix::glyphRow (ch, r) & (1 << (4 - col)))
                    for (int dy = 0; dy < 2; ++dy)
                        plot ((float) x, base + (float) (r * 2 + dy), 1.0f);
        }
    }

    // Texto pequeño (fuente 5×7) en la matriz
    void text (const juce::String& s, int x0, int y0, float v)
    {
        for (int k = 0; k < s.length(); ++k)
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 5; ++c)
                    if (DotMatrix::glyphRow ((unsigned char) s[k], r) & (1 << (4 - c)))
                        plot ((float) (x0 + k * 6 + c), (float) (y0 + r), v);
    }

    // 10) Carrera en pseudo-3D con vista trasera: cada fila bajo el horizonte (fila 5) está a z = 60 / (y - 5); la curva se
    //     acumula hacia el fondo; bandas del arcén, postes, montañas con paralaje. El coche (26 × 10) se abre en la curva, se
    //     inclina, frena con los pilotos y echa humo al derrapar. Sonando: el nivel da la velocidad, el centroide tuerce la
    //     pista y la nota MIDI más aguda mueve el coche de carril
    void sceneRacer()
    {
        constexpr int horizon = 5;
        const auto u = seconds();
        const auto autoCurve = 1.6f * std::sin (u * 0.9f) + 0.6f * std::sin (u * 2.3f);
        const auto curve = autoCurve + react * (centroid - 0.5f) * 3.0f;
        raceZ += 4.0f * (1.0f - 0.4f * react + 1.2f * loud);
        for (int c = 0; c < cols; ++c) // montañas
        {
            const auto x = (float) c + curve * 40.0f;
            const auto hm = 2.5f + 1.5f * std::sin (x / 23.0f) + 0.6f * std::sin (x / 7.0f);
            for (int r = juce::jmax (0, (int) ((float) horizon - hm)); r < horizon; ++r) at (c, r) = 0.22f;
        }
        float dx = 0.0f, xoff = 0.0f;
        const auto lane = note >= 0 ? juce::jlimit (-1.0f, 1.0f, (float) (note - 60) / 24.0f) * 40.0f : 0.0f;
        for (int r = rows - 1; r > horizon; --r)
        {
            const auto z = 60.0f / (float) (r - horizon);
            dx += curve * 0.012f * (float) (rows - r);
            xoff += dx;
            const auto half = 300.0f / z; // medio ancho de la calzada: ~90 puntos abajo, ~5 en el horizonte
            const auto cx = (float) cols * 0.5f + xoff - curve * 12.0f - lane;
            const auto stripe = ((int) ((z + raceZ) / 8.0f) & 1) != 0;
            for (int c = 0; c < cols; ++c)
            {
                const auto d = std::abs ((float) c - cx) / half;
                at (c, r) = d < 1.0f ? (d < 0.03f && stripe ? 0.75f : 0.12f) : d < 1.12f ? (stripe ? 0.95f : 0.35f) : (stripe ? 0.13f : 0.05f);
            }
            if ((int) ((z + raceZ) / 3.0f) % 12 == 0) // postes
                for (int side = -1; side <= 1; side += 2)
                    for (int hgt = 0; hgt < juce::jmax (1, (int) (30.0f / z)); ++hgt)
                        if (r - hgt > horizon) plot (cx + (float) side * half * 1.5f, (float) (r - hgt), 0.8f);
        }
        static const char* car[10] = { ".########################.", "..##..................##..", "....##################....",
                                       "...#oooooooooooooooooo#...", "..##oooooooooooooooooo##..", ".##@@@####======####@@@##.",
                                       "##@@@@####======####@@@@##", "##########::::::##########", "wwwww................wwwww",
                                       "wwwww................wwwww" };
        const auto lean = juce::jlimit (-1.0f, 1.0f, curve / 1.5f);
        const auto brake = std::abs (curve) > 1.2f;
        const auto x0 = cols / 2 - 13 + juce::roundToInt (-curve * 6.0f);
        for (int r = 0; r < 10; ++r)
        {
            const auto sh = juce::roundToInt (lean * 3.0f * (float) (9 - r) / 9.0f);
            for (int c = 0; c < 26; ++c)
            {
                const auto ch = car[r][c];
                if (ch == '.') continue;
                const auto v = ch == '#' ? 0.75f : ch == 'o' ? 0.28f : ch == '@' ? (brake ? 1.0f : 0.55f) : ch == '=' ? 0.45f
                             : ch == ':' ? 0.2f : (((r + frame / 2) & 1) != 0 ? 0.6f : 0.3f); // w: rodadura que gira
                const auto cc = x0 + c + sh, rr = rows - 10 + r;
                if (cc >= 0 && cc < cols) at (cc, rr) = v;
            }
            if (std::abs (lean) > 0.3f && r >= 2 && r <= 7) // lateral por fuera de la curva
                plot ((float) (x0 + sh + (lean < 0.0f ? 26 : -1)), (float) (rows - 10 + r), 0.5f);
        }
        if (std::abs (curve) > 1.0f)
            for (int k = 0; k < 10; ++k) // humo de la rueda exterior
                plot ((float) (x0 + (curve > 0.0f ? 2 : 23) + rng.nextInt (9) - 4), (float) (rows - 1 - rng.nextInt (4)), 0.35f);
        else if (frame % 8 < 2)
            for (int ex : { x0 + 11, x0 + 14 }) plot ((float) ex, (float) (rows - 3), 0.5f); // escape
        text ("KONISYNTH", 2, 0, 0.7f);
        text (juce::String (juce::roundToInt (180.0f + 60.0f * std::abs (std::sin (u)) + 80.0f * loud)).paddedLeft (' ', 3) + "KM",
              cols - 6 * 5 - 2, 0, 0.7f);
    }

    // 11) Shooter en cabina (primera persona): cúpula con montantes, alabeo que gira el espacio, mira con fijado, láser de los
    //     dos cañones y consola (radar, escudo, energía, avisos). Sonando, los ataques por banda lanzan los enemigos (posición
    //     según la banda) y la nota MIDI más aguda inclina la nave
    void sceneCockpit()
    {
        constexpr int dash = 5, view = rows - dash;
        const auto u = seconds();
        const auto rollA = 0.22f * std::sin (u * 0.8f) + (note >= 0 ? (float) (note - 60) / 24.0f * 0.2f : 0.0f);
        const auto cr = std::cos (rollA), sr = std::sin (rollA);
        const auto cx = (float) cols * 0.5f, cy = view * 0.5f;
        const auto proj = [&] (float x, float y, float z, float& X, float& Y)
        {
            const auto px = x * 60.0f / z, py = y * 60.0f / z;
            X = cx + px * cr - py * sr * 3.0f;
            Y = cy - (px * sr / 3.0f + py * cr);
        };
        drawStars (3.0f, 0.5f, view, rollA, 2);
        // Enemigos: cada 20 fotogramas en silencio; sonando, con los golpes (posición horizontal según el centroide)
        if ((react < 0.5f && frame % 20 == 0) || (react >= 0.5f && hit > 0.3f && enemies.size() < 8 && frame % 4 == 0))
            enemies.push_back ({ react >= 0.5f ? (centroid - 0.5f) * 100.0f : rng.nextFloat() * 80.0f - 40.0f,
                                 rng.nextFloat() * 10.0f - 5.0f, 150.0f, 0 });
        Enemy* target = nullptr;
        float tx = 0.0f, ty = 0.0f;
        int tsize = 1;
        for (auto& e : enemies)
        {
            e.z -= 2.2f;
            float ex, ey;
            proj (e.x, e.y, e.z, ex, ey);
            const auto size = juce::jlimit (1, 8, (int) (420.0f / e.z));
            if (e.boom == 0)
            {
                for (int a = -size; a <= size; ++a)
                    for (int b = -size / 2; b <= size / 2; ++b)
                        if (std::abs (a) + std::abs (b) * 2 <= size) plot (ex + (float) a, ey + (float) b, 0.85f, view);
                if (e.z < 50.0f && target == nullptr) { target = &e; tx = ex; ty = ey; tsize = size; }
            }
            else if (e.boom < 12) // explosión: anillo que se abre
            {
                for (int k = 0; k < 16; ++k)
                {
                    const auto ang = (float) k / 16.0f * juce::MathConstants<float>::twoPi;
                    plot (ex + std::cos (ang) * (float) e.boom * 1.5f, ey + std::sin (ang) * (float) e.boom * 0.5f, 1.0f - (float) e.boom / 12.0f, view);
                }
                ++e.boom;
            }
        }
        for (int d : { -6, -5, -4, 4, 5, 6 }) plot (cx + (float) d, cy, 0.7f, view); // mira
        for (int d : { -2, 2 }) plot (cx, cy + (float) d, 0.7f, view);
        if (target != nullptr)
        {
            if (frame % 4 < 2) // recuadro de fijado
            {
                for (int a = -tsize - 3; a <= tsize + 3; ++a)
                {
                    plot (tx + (float) a, ty - (float) (tsize / 2 + 2), 0.9f, view);
                    plot (tx + (float) a, ty + (float) (tsize / 2 + 2), 0.9f, view);
                }
                for (int b = -tsize / 2 - 2; b <= tsize / 2 + 2; ++b)
                {
                    plot (tx - (float) (tsize + 3), ty + (float) b, 0.9f, view);
                    plot (tx + (float) (tsize + 3), ty + (float) b, 0.9f, view);
                }
            }
            if (frame % 6 < 3) // láser desde los cañones de las esquinas inferiores del parabrisas
            {
                for (float gx : { 20.0f, (float) cols - 21.0f })
                    for (int k = 0; k < 60; ++k)
                    {
                        const auto f = (float) k / 59.0f;
                        plot (gx + (tx - gx) * f, (float) (view - 1) + (ty - (float) (view - 1)) * f, 0.95f - 0.4f * f, view);
                    }
                energy = juce::jmax (0.2f, energy - 0.01f);
            }
            if (frame % 6 == 2) target->boom = 1;
        }
        else
            energy = juce::jmin (1.0f, energy + 0.004f);
        enemies.erase (std::remove_if (enemies.begin(), enemies.end(), [] (const Enemy& e) { return e.z <= 5.0f || e.boom >= 12; }), enemies.end());
        for (const auto& e : enemies)
            if (e.z < 12.0f && e.boom == 0) shield = juce::jmax (0.1f, shield - 0.05f); // roce: baja el escudo
        shield = juce::jmin (1.0f, shield + 0.002f);
        // Cúpula: travesaño superior y montantes diagonales
        for (int c = 0; c < cols; ++c) at (c, 0) = juce::jmax (at (c, 0), 0.35f);
        for (int k = 0; k < 9; ++k)
            for (int x : { (int) ((float) k * 2.2f), cols - 1 - (int) ((float) k * 2.2f) })
            {
                plot ((float) x, (float) k, 0.55f, view);
                plot ((float) (x + 1), (float) k, 0.55f, view);
            }
        // Consola: borde, radar (elipse 21 × 3 con los enemigos), barras de escudo y energía, 6 avisos
        for (int c = 0; c < cols; ++c)
        {
            at (c, view) = 0.5f;
            for (int r = view + 1; r < rows; ++r) at (c, r) = 0.07f;
        }
        const auto rx = cols / 2, ry = view + 3;
        for (int k = 0; k < 48; ++k)
        {
            const auto ang = (float) k / 48.0f * juce::MathConstants<float>::twoPi;
            plot ((float) rx + 10.0f * std::cos (ang), (float) ry + 1.4f * std::sin (ang), 0.45f);
        }
        for (const auto& e : enemies)
            if (e.boom == 0 && ((frame / 3) & 1) != 0)
            {
                const auto by = juce::roundToInt ((float) ry + 1.4f - e.z / 150.0f * 2.8f);
                if (by > view) plot ((float) rx + e.x / 4.0f, (float) by, 1.0f);
            }
        plot ((float) rx, (float) ry, 0.8f);
        const auto bar = [this] (int row, float v, int x0)
        {
            for (int k = 0; k < 60; ++k) if (x0 + k < cols) at (x0 + k, row) = k < (int) (v * 60.0f) ? 0.8f : 0.15f;
        };
        bar (view + 2, shield, 40); bar (view + 4, energy, 40);
        bar (view + 2, energy, cols - 100); bar (view + 4, shield, cols - 100);
        const int lights[6] = { 8, 14, 20, cols - 21, cols - 15, cols - 9 };
        for (int k = 0; k < 6; ++k)
        {
            const auto on = (target != nullptr && (k == 0 || k == 5)) || ((frame / 10 + k) % 3 == 0);
            for (int r = view + 2; r < view + 4; ++r)
                for (int x = lights[k]; x < lights[k] + 3; ++x) if (x >= 0 && x < cols) at (x, r) = on ? 0.9f : 0.2f;
        }
    }
};

// Modo ARCADE del display: un matamarcianos horizontal en pixel art que juega solo con la música. Las 48 bandas del analizador
// (20 Hz - 20 kHz, en dB) generan los enemigos: un ataque en una banda (subida de 6 dB sobre su media) lanza, a la altura de esa
// banda, rocas (graves), aliens (medios) o destellos (agudos), con tamaño y velocidad según la energía. La nave persigue al
// enemigo más cercano y dispara; un golpe general (suma de subidas de todas las bandas) dispara una ráfaga triple.
// Se pinta en un lienzo de baja resolución (píxel = 2-4 px de pantalla según el alto) escalado sin suavizado. Solo UI: no toca
// el audio. Determinista (semilla fija). Se incluye desde KoniSynth.cpp (mismo TU), antes de UI/Visualizer.h.
#pragma once

class ArcadeGame
{
public:
    static constexpr int numBands = 48;

    // Un fotograma: bandas en dB, dt en segundos, tamaño del lienzo en píxeles de juego
    void update (const std::array<float, numBands>& bandsDb, float dt, int w, int h)
    {
        if (w != width || h != height)
            resize (w, h);
        t += dt;
        const auto sx = (float) width / 240.0f; // velocidades relativas al ancho: igual de rápido a cualquier tamaño

        // Ataques por banda y golpe general
        float hit = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            const auto db = bandsDb[(size_t) b];
            const auto rise = db - avgDb[(size_t) b];
            avgDb[(size_t) b] += 0.12f * (db - avgDb[(size_t) b]);
            cooldown[(size_t) b] = juce::jmax (0.0f, cooldown[(size_t) b] - dt);
            const auto energy = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            if (rise > 0.0f) hit += rise * energy;
            if (rise > 6.0f && energy > 0.25f && cooldown[(size_t) b] <= 0.0f && enemies.size() < 40)
            {
                spawn (b, energy, sx);
                cooldown[(size_t) b] = 0.25f;
            }
        }

        // Nave: hacia el enemigo más cercano por delante; deriva suave sin enemigos
        const Enemy* target = nullptr;
        for (const auto& e : enemies)
            if (e.x > shipX + 8.0f && (target == nullptr || e.x < target->x))
                target = &e;
        const auto wantY = target != nullptr ? target->y : (float) height * 0.5f + std::sin (t * 1.3f) * (float) height * 0.25f;
        const auto maxStep = 45.0f * dt * (float) height / 35.0f;
        shipY += juce::jlimit (-maxStep, maxStep, wantY - shipY);
        shipY = juce::jlimit (3.0f, (float) height - 3.0f, shipY);
        fireCooldown -= dt;
        if (target != nullptr && std::abs (target->y - shipY) < 3.0f && fireCooldown <= 0.0f)
        {
            shoot (0.0f, sx);
            fireCooldown = 0.22f;
        }
        burstCooldown -= dt;
        if (hit > 40.0f && burstCooldown <= 0.0f && target != nullptr)
        {
            shoot (-30.0f, sx); shoot (0.0f, sx); shoot (30.0f, sx);
            burstCooldown = 0.35f;
        }

        // Movimiento
        for (auto& s : stars) { s.x -= s.speed * sx * dt; if (s.x < 0.0f) { s.x += (float) width; s.y = (float) rng.nextInt (height); } }
        for (auto& e : enemies)
        {
            e.x -= e.speed * dt;
            if (e.kind == alien) e.y = e.baseY + std::sin (t * 4.0f + e.phase) * 2.0f;
        }
        for (auto& b : bullets) { b.x += 170.0f * sx * dt; b.y += b.vy * dt; }
        for (auto& p : particles) { p.x += p.vx * dt; p.y += p.vy * dt; p.life -= dt; }

        // Impactos
        for (auto& b : bullets)
            for (auto& e : enemies)
                if (b.alive && e.hp > 0 && std::abs (b.x - e.x) <= (float) e.size + 1.0f && std::abs (b.y - e.y) <= (float) e.size)
                {
                    b.alive = false;
                    if (--e.hp <= 0)
                    {
                        explode (e);
                        score += e.kind == rock ? 10 * e.size : e.kind == alien ? 50 : 100;
                    }
                }
        enemies.erase (std::remove_if (enemies.begin(), enemies.end(), [] (const Enemy& e) { return e.hp <= 0 || e.x < -8.0f; }), enemies.end());
        bullets.erase (std::remove_if (bullets.begin(), bullets.end(), [this] (const Bullet& b) { return ! b.alive || b.x > (float) width; }), bullets.end());
        particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }), particles.end());
    }

    // style 0: paleta arcade de color; 1 y 2: monocromo en el tono del fósforo (ink)
    void paint (juce::Graphics& g, juce::Rectangle<float> area, int style, juce::Colour ink)
    {
        if (width <= 0 || height <= 0)
            return;
        if (canvas.getWidth() != width || canvas.getHeight() != height)
            canvas = juce::Image (juce::Image::ARGB, width, height, true);
        const auto col = [style, ink] (juce::uint32 argb)
        {
            const juce::Colour c (argb);
            if (style == 0) return c;
            const auto l = 0.2126f * c.getFloatRed() + 0.7152f * c.getFloatGreen() + 0.0722f * c.getFloatBlue();
            return ink.withMultipliedBrightness (0.25f + 0.85f * l).withAlpha (c.getFloatAlpha());
        };
        canvas.clear (canvas.getBounds(), style == 0 ? juce::Colour (0xff05070d) : ink.withMultipliedBrightness (0.06f));
        const auto put = [this] (int x, int y, juce::Colour c) { if (x >= 0 && y >= 0 && x < width && y < height) canvas.setPixelAt (x, y, c); };

        for (const auto& s : stars)
            put ((int) s.x, (int) s.y, col (s.speed > 11.0f ? 0xff9aa4b8 : 0xff4a5366));
        for (const auto& e : enemies)
            drawEnemy (e, put, col);
        for (const auto& b : bullets)
        {
            put ((int) b.x, (int) b.y, col (0xffffe066));
            put ((int) b.x - 1, (int) b.y, col (0xffffb030));
        }
        // Nave (8 × 5), mirando a la derecha, con la llama del motor que parpadea
        static const char* ship[5] = { ".##.....", "####o...", "########", "####o...", ".##....." };
        const auto ox = (int) shipX - 4, oy = (int) std::round (shipY) - 2;
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 8; ++x)
                if (ship[y][x] != '.')
                    put (ox + x, oy + y, col (ship[y][x] == 'o' ? 0xffffffff : 0xff3ad6ff));
        const auto flame = ((int) (t * 20.0f) & 1) != 0;
        put (ox - 1, oy + 2, col (flame ? 0xffffe066 : 0xffff7a1a));
        if (flame) put (ox - 2, oy + 2, col (0xffff7a1a));
        for (const auto& p : particles)
            put ((int) p.x, (int) p.y, col (p.life > 0.35f ? 0xffffe066 : p.life > 0.18f ? p.colour : 0xff9a2a10));
        drawScore (put, col);

        juce::Graphics::ScopedSaveState save (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality); // píxeles nítidos (vecino más próximo)
        g.drawImage (canvas, area, juce::RectanglePlacement::stretchToFit);
    }

private:
    enum Kind { rock, alien, sparkle };
    struct Enemy { float x, y, baseY, speed, phase; int size, hp; Kind kind; juce::uint32 colour; std::array<juce::uint8, 81> mask {}; };
    struct Bullet { float x, y, vy; bool alive = true; };
    struct Particle { float x, y, vx, vy, life; juce::uint32 colour; };
    struct Star { float x, y, speed; };

    void resize (int w, int h)
    {
        width = w; height = h;
        shipX = 6.0f;
        shipY = (float) h * 0.5f;
        stars.clear();
        for (int i = 0; i < w * h / 60; ++i)
            stars.push_back ({ (float) rng.nextInt (w), (float) rng.nextInt (h), 4.0f + 12.0f * rng.nextFloat() });
        enemies.clear(); bullets.clear(); particles.clear();
    }

    void spawn (int band, float energy, float sx)
    {
        Enemy e {};
        e.kind = band < 12 ? rock : band < 36 ? alien : sparkle;
        e.size = e.kind == rock ? 2 + juce::roundToInt (energy * 2.0f) : e.kind == alien ? 3 : 1;
        e.hp = e.kind == rock ? 1 + e.size / 2 : 1;
        e.x = (float) width + 6.0f;
        // Graves abajo, agudos arriba (como el analizador girado)
        const auto margin = (float) e.size + 1.0f;
        e.baseY = e.y = (float) height - margin - ((float) band / (numBands - 1)) * ((float) height - 2.0f * margin);
        e.speed = (e.kind == rock ? 18.0f + 20.0f * energy : e.kind == alien ? 26.0f + 30.0f * energy : 40.0f + 40.0f * energy) * sx;
        e.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        static const juce::uint32 alienColours[2] = { 0xff6cff5a, 0xffff4fd8 };
        e.colour = e.kind == rock ? 0xff8a6a4a : e.kind == alien ? alienColours[band & 1] : 0xffd8f6ff;
        if (e.kind == rock) // contorno irregular: radio con ruido por ángulo
        {
            const auto r = (float) e.size;
            for (int y = -4; y <= 4; ++y)
                for (int x = -4; x <= 4; ++x)
                {
                    const auto a = std::atan2 ((float) y, (float) x);
                    const auto edge = r * (0.8f + 0.25f * std::sin (a * 3.0f + e.phase) + 0.15f * rng.nextFloat());
                    if (std::hypot ((float) x, (float) y) <= edge)
                        e.mask[(size_t) ((y + 4) * 9 + x + 4)] = (juce::uint8) (x + y < 0 ? 2 : 1); // 2 = cara iluminada
                }
        }
        enemies.push_back (e);
    }

    void shoot (float vy, float sx) { bullets.push_back ({ shipX + 4.0f, std::round (shipY), vy * sx }); }

    void explode (const Enemy& e)
    {
        const auto n = 8 + 3 * e.size;
        for (int i = 0; i < n; ++i)
        {
            const auto a = rng.nextFloat() * juce::MathConstants<float>::twoPi, v = 20.0f + 45.0f * rng.nextFloat();
            particles.push_back ({ e.x, e.y, std::cos (a) * v - e.speed * 0.3f, std::sin (a) * v, 0.35f + 0.35f * rng.nextFloat(), e.colour });
        }
    }

    template <typename Put, typename Col>
    void drawEnemy (const Enemy& e, Put& put, Col& col) const
    {
        const auto cx = (int) std::round (e.x), cy = (int) std::round (e.y);
        if (e.kind == rock)
        {
            for (int y = -4; y <= 4; ++y)
                for (int x = -4; x <= 4; ++x)
                    if (const auto m = e.mask[(size_t) ((y + 4) * 9 + x + 4)])
                        put (cx + x, cy + y, col (m == 2 ? 0xffa8875f : 0xff5c4632));
        }
        else if (e.kind == alien)
        {
            static const char* frames[2][5] = { { "..#.#..", ".#####.", "##.#.##", "#######", "#.#.#.#" },
                                                { "..#.#..", ".#####.", "##.#.##", "#######", ".#...#." } };
            const auto& f = frames[((int) (t * 4.0f + e.phase) & 1)];
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 7; ++x)
                    if (f[y][x] == '#')
                        put (cx - 3 + x, cy - 2 + y, col (e.colour));
        }
        else
        {
            const auto on = ((int) (t * 10.0f + e.phase) & 1) != 0;
            put (cx, cy, col (0xffffffff));
            if (on) { put (cx - 1, cy, col (e.colour)); put (cx + 1, cy, col (e.colour)); put (cx, cy - 1, col (e.colour)); put (cx, cy + 1, col (e.colour)); }
        }
    }

    // Marcador: 6 dígitos de 3 × 5 en la esquina superior derecha
    template <typename Put, typename Col>
    void drawScore (Put& put, Col& col) const
    {
        static const char* digits[10] = { "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
                                          "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111" };
        auto value = score % 1000000;
        for (int d = 5; d >= 0; --d)
        {
            const auto digit = value % 10;
            value /= 10;
            const auto x0 = width - 2 - (6 - d) * 4 + 1;
            for (int i = 0; i < 15; ++i)
                if (digits[digit][i] == '1')
                    put (x0 + i % 3, 1 + i / 3, col (0xffd0d6e0));
        }
    }

    int width = 0, height = 0, score = 0;
    float t = 0.0f, shipX = 6.0f, shipY = 10.0f, fireCooldown = 0.0f, burstCooldown = 0.0f;
    std::array<float, numBands> avgDb {}, cooldown {};
    std::vector<Enemy> enemies;
    std::vector<Bullet> bullets;
    std::vector<Particle> particles;
    std::vector<Star> stars;
    juce::Random rng { 0xA4CADE };
    juce::Image canvas;
};

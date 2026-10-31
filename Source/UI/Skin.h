// Skins de la interfaz: Vintage 60s (por defecto: paneles negro mate, nogal, rótulos crema y acentos ámbar), Modern (los
// colores de siempre, antes "Default") y fósforo monocromo, como los monitores de terminal de los 70/80:
// Green (P1, emisión de pico ≈ 525 nm) y Amber (P3, ≈ 602 nm); los sRGB elegidos son aproximaciones visuales, no colorimetría.
// Cada color literal de la UI pasa por Skin::c() / Skin::map(): en los monocromos se conserva su luminancia (pesos Rec. 709
// sobre los valores sRGB, sin linealizar: aproximación) y se pinta en la rampa del fósforo: negro -> fondo tintado, gris medio ->
// fósforo a media intensidad, blanco -> fósforo "caliente" casi blanco. La jerarquía de contraste de la interfaz se mantiene sin
// tocar cada componente; el alfa es el del color original. Ajuste global (todas las instancias): clave "skin" de %APPDATA%/KoniSynth/config.settings.
// Extraído como cabecera: se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación.
#pragma once

namespace Skin
{
enum Id { standard = 0, green, amber, metal, vintage, count }; // el orden es el valor guardado en config.settings

inline const char* name (int id)
{
    static const char* names[] = { "Modern", "Phosphor Green", "Phosphor Amber", "Brushed Metal", "Vintage 60s" };
    return names[juce::jlimit (0, (int) count - 1, id)];
}

// Valor guardado en config.settings (clave "skin"). Si aún no está, se migra el skin.txt de versiones anteriores y se borra.
// Sin valor guardado: Vintage 60s (quien ya eligió un skin conserva el suyo)
inline int load()
{
    auto config = openConfig();
    const auto legacy = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("KoniSynth/skin.txt");
    if (! config->containsKey ("skin") && legacy.existsAsFile())
    {
        config->setValue ("skin", legacy.loadFileAsString().trim().getIntValue());
        if (config->saveIfNeeded())
            legacy.deleteFile();
    }
    return juce::jlimit (0, (int) count - 1, config->getIntValue ("skin", vintage));
}

inline std::atomic<int>& currentRef()
{
    static std::atomic<int> id { load() };
    return id;
}

inline int current() { return currentRef().load (std::memory_order_relaxed); }
inline bool mono() { return current() == green || current() == amber; } // Brushed Metal conserva los colores de Default

inline void set (int id)
{
    id = juce::jlimit (0, (int) count - 1, id);
    currentRef().store (id);
    if (auto config = openConfig())
        config->setValue ("skin", id); // PropertiesFile guarda al destruirse
}

// Fondo tintado, fósforo pleno y núcleo saturado ("caliente")
struct Ramp { juce::Colour bg, phosphor, hot; };
inline Ramp ramp()
{
    if (current() == green) return { juce::Colour (0xff050d07), juce::Colour (0xff33ff66), juce::Colour (0xffd8ffe2) };
    return { juce::Colour (0xff0d0802), juce::Colour (0xffffb000), juce::Colour (0xfffff0cc) };
}

// Vintage 60s: los grises (S < 0,25) pierden el tinte azulado y toman uno cálido (tono 32°, saturación del 10 % en los
// oscuros al 14 % en el blanco, que queda crema #f2e6d0 aprox.); los azules saturados (tono 180-235°: acento, flechas,
// pestaña elegida) pasan a ámbar (tono 36°) con su saturación y brillo. El resto (LED rojos, verdes, amarillos) no cambia
inline juce::Colour vintageMap (juce::Colour c)
{
    const auto h = c.getHue(), s = c.getSaturation(), v = c.getBrightness();
    if (s < 0.25f)
        return juce::Colour::fromHSV (32.0f / 360.0f, 0.10f + 0.04f * v, v * (0.92f + 0.03f * v), c.getFloatAlpha());
    if (h >= 180.0f / 360.0f && h <= 235.0f / 360.0f)
        return juce::Colour::fromHSV (36.0f / 360.0f, juce::jmin (1.0f, s * 1.1f), juce::jmin (1.0f, v * 1.05f), c.getFloatAlpha());
    return c;
}

inline juce::Colour map (juce::Colour c)
{
    if (current() == vintage)
        return vintageMap (c);
    if (! mono())
        return c;
    const auto r = ramp();
    // Luminancia, o el brillo de los colores saturados si es mayor: un LED rojo encendido (Rec. 709 ≈ 0,27) debe seguir
    // luciendo; el granate del apagado (V ≈ 0,23) queda tenue. Grises: S = 0, manda la luminancia
    const auto y = juce::jmax (0.2126f * c.getFloatRed() + 0.7152f * c.getFloatGreen() + 0.0722f * c.getFloatBlue(),
                               0.8f * c.getBrightness() * c.getSaturation());
    // Curva y^1,4: los grises de panel (y ≈ 0,15) quedan al ≈ 8 % del fósforo, casi negro como en una terminal; texto y LED arriba
    const auto t = std::pow (juce::jlimit (0.0f, 1.0f, y), 1.4f);
    auto out = r.bg.interpolatedWith (r.phosphor, juce::jmin (1.0f, t / 0.85f));
    if (t > 0.85f)
        out = out.interpolatedWith (r.hot, 0.6f * (t - 0.85f) / 0.15f);
    return out.withAlpha (c.getFloatAlpha());
}

inline juce::Colour c (juce::uint32 argb) { return map (juce::Colour (argb)); }
inline juce::Colour white() { return map (juce::Colours::white); }
inline juce::Colour black() { return map (juce::Colours::black); }

// Líneas de barrido de los displays (monocromos y Vintage 60s): una línea oscura cada 3 px, 18 % de opacidad (10 % en Vintage)
inline void scanlines (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! mono() && current() != vintage)
        return;
    g.setColour (juce::Colours::black.withAlpha (mono() ? 0.18f : 0.10f));
    for (auto y = area.getY(); y < area.getBottom(); y += 3)
        g.fillRect (area.getX(), y, area.getWidth(), 1);
}

// Acento de los mandos (arco de valor de las perillas, relleno de los faders): azul en Default, azul acero en Brushed Metal y
// el fósforo en los monocromos. El cuerpo de las perillas no cambia (tono original en todos los skins)
inline juce::Colour accent()
{
    switch (current())
    {
        case green: return juce::Colour (0xff33ff66);
        case amber: return juce::Colour (0xffffb000);
        case metal: return juce::Colour (0xff8fb8d8);
        case vintage: return juce::Colour (0xffe0a040);
        default:    return juce::Colour (0xff42a2c8);
    }
}

// Traza de las pantallas (display de onda): azul claro en Default, acero claro en Brushed Metal y el fósforo en los monocromos
inline juce::Colour trace()
{
    switch (current())
    {
        case green: return juce::Colour (0xff7dffab);
        case amber: return juce::Colour (0xffffc040);
        case metal: return juce::Colour (0xffbfe0ff);
        case vintage: return juce::Colour (0xff7dffab); // fósforo verde, como el de los osciloscopios de laboratorio de los 60
        default:    return juce::Colour (0xff7fd4ff);
    }
}

// Pantalla con bisel: marco de 4 px con luz arriba y sombra abajo, cristal hundido (degradado radial casi negro, tintado en
// los monocromos) y líneas de barrido en los monocromos. El contenido se dibuja encima y glass() va al final
inline void screen (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f)
{
    g.setGradientFill (juce::ColourGradient (map (juce::Colour (0xff4a525c)), 0.0f, r.getY(), map (juce::Colour (0xff15181c)), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    const auto glassArea = r.reduced (4.0f);
    const auto tint = trace();
    g.setGradientFill (juce::ColourGradient (tint.interpolatedWith (juce::Colours::black, 0.9f), glassArea.getCentreX(), glassArea.getCentreY(),
                                             juce::Colour (0xff020304), glassArea.getX(), glassArea.getY(), true));
    g.fillRoundedRectangle (glassArea, juce::jmax (2.0f, radius - 3.0f));
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (glassArea, juce::jmax (2.0f, radius - 3.0f), 1.5f);
    scanlines (g, glassArea.toNearestInt());
}

// Grano de tubo (Vintage 60s): mosaico de 128 × 128 con semilla fija, cada píxel blanco con alfa aleatorio de 0 a 6 %
inline const juce::Image& grainTile()
{
    static juce::Image img;
    if (img.isNull())
    {
        img = juce::Image (juce::Image::ARGB, 128, 128, true);
        juce::Random rng (0xC47);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                img.setPixelAt (x, y, juce::Colours::white.withAlpha (0.06f * rng.nextFloat() * rng.nextFloat()));
    }
    return img;
}

// Ruido y glitches de tubo en las pantallas (solo Vintage 60s; solo dibujo, no toca el audio ni la aleatoriedad sembrada).
// Grano: grainTile desplazado a saltos cada 50 ms, así se mueve en las pantallas que se repintan (osciloscopio, displays
// animados) y queda quieto en las que no. Glitch: en cada ventana de 4 s, un evento de 120 ms en un instante pseudoaleatorio
// (hash del número de ventana): una banda clara de 2 px en la traza al 10 % y una oscura de 3 px debajo, a una altura también
// pseudoaleatoria. Coste: un relleno en mosaico por repintado y, durante el glitch, dos rectángulos
inline void crtNoise (juce::Graphics& g, juce::Rectangle<float> a, float radius)
{
    if (current() != vintage || a.isEmpty())
        return;
    const auto now = juce::Time::getMillisecondCounter();
    const juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (a, radius);
    g.reduceClipRegion (clip);
    const auto step = now / 50u;
    g.setTiledImageFill (grainTile(), (int) a.getX() - (int) ((step * 37u) % 128u), (int) a.getY() - (int) ((step * 71u) % 128u), 1.0f);
    g.fillRect (a);
    const auto h = (now / 4000u + 1u) * 2654435761u; // hash de Knuth del número de ventana
    const auto start = (h >> 8) % 3880u, within = now % 4000u;
    if (within >= start && within < start + 120u)
    {
        const auto y = a.getY() + a.getHeight() * (float) ((h >> 20) % 1000u) / 1000.0f;
        g.setColour (trace().withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (a.getX(), y, a.getWidth(), 2.0f));
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (a.getX(), y + 2.0f, a.getWidth(), 3.0f));
    }
}

// Cristal sobre una pantalla: viñeta (tubo curvo), reflejo diagonal del 13 % al 0 % en el primer tercio y un brillo de borde.
// Coste: dos degradados y un trazo por repintado
inline void glass (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f)
{
    const auto a = r.reduced (4.0f);
    const auto rr = juce::jmax (2.0f, radius - 3.0f);
    crtNoise (g, a, rr); // Vintage 60s: grano y glitches, bajo la viñeta y el reflejo
    const auto c = a.getCentre();
    juce::ColourGradient vignette (juce::Colours::transparentBlack, c.x, c.y, juce::Colours::black.withAlpha (0.55f), a.getX(), a.getY(), true);
    vignette.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (vignette);
    g.fillRoundedRectangle (a, rr);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.13f), a.getX(), a.getY(), juce::Colours::transparentWhite,
                                a.getX() + a.getWidth() * 0.35f, a.getY() + a.getHeight() * 0.65f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (a, rr);
    juce::Path edge;
    edge.startNewSubPath (a.getX() + 6.0f, a.getCentreY());
    edge.quadraticTo (a.getX() + a.getWidth() * 0.22f, a.getY() + 4.0f, a.getX() + a.getWidth() * 0.65f, a.getY() + 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.strokePath (edge, juce::PathStrokeType (1.5f));
}

// Placa de circuito (solo Phosphor Green): pistas a 0°/45°/90° sobre una rejilla de 8 px que acaban en pads o vías, y huellas
// de chips con sus patillas, en el color del fósforo al 7-13 %. Mosaico de 512 × 512 con semilla fija, dibujado una vez; cada
// elemento se dibuja también desplazado ±512 px para que no haya cortes en los bordes del mosaico
inline const juce::Image& circuitTile()
{
    static juce::Image img;
    if (img.isValid())
        return img;
    constexpr int size = 512, grid = 8;
    img = juce::Image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const auto copper = juce::Colour (0xff33ff66);
    juce::Random rng (0xB0A2D);
    const auto wrapped = [&g] (auto&& draw)
    {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            {
                juce::Graphics::ScopedSaveState state (g);
                g.addTransform (juce::AffineTransform::translation ((float) (dx * size), (float) (dy * size)));
                draw();
            }
    };
    const auto gridPoint = [&rng] { return juce::Point<float> ((float) (grid * rng.nextInt (size / grid)), (float) (grid * rng.nextInt (size / grid))); };

    for (int k = 0; k < 7; ++k) // chips: contorno y patillas arriba y abajo cada 6 px
    {
        const auto w = (float) (grid * rng.nextInt ({ 4, 9 })), h = (float) (grid * rng.nextInt ({ 3, 6 }));
        const auto at = gridPoint();
        wrapped ([&]
        {
            g.setColour (copper.withAlpha (0.07f));
            g.drawRect (juce::Rectangle<float> (at.x, at.y, w, h), 1.0f);
            g.setColour (copper.withAlpha (0.11f));
            for (auto px = at.x + 4.0f; px < at.x + w - 2.0f; px += 6.0f)
            {
                g.fillRect (px - 1.5f, at.y - 5.0f, 3.0f, 4.0f);
                g.fillRect (px - 1.5f, at.y + h + 1.0f, 3.0f, 4.0f);
            }
        });
    }

    static const int dirs[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
    for (int k = 0; k < 90; ++k) // pistas: 2-4 tramos con giros de 45°; 1 de cada 6, ancha (alimentación)
    {
        const auto start = gridPoint();
        auto end = start;
        auto d = rng.nextInt (4) * 2; // empieza en horizontal o vertical
        juce::Path p;
        p.startNewSubPath (start);
        for (int s = rng.nextInt ({ 2, 5 }); s > 0; --s)
        {
            end += juce::Point<float> ((float) dirs[d][0], (float) dirs[d][1]) * (float) (grid * rng.nextInt ({ 2, 10 }));
            p.lineTo (end);
            d = (d + (rng.nextBool() ? 1 : 7)) % 8;
        }
        const auto width = rng.nextInt (6) == 0 ? 3.0f : 1.5f;
        const auto via = rng.nextBool();
        wrapped ([&]
        {
            g.setColour (copper.withAlpha (0.09f));
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            g.setColour (copper.withAlpha (0.13f));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (start));
            if (via) g.drawEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (end), 1.5f);
            else     g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre (end));
        });
    }
    return img;
}

// Vetas de acero cepillado (Brushed Metal): cada fila con su brillo (±5 %) y grano fino a lo largo de ella; mosaico de
// 512 × 64 con semilla fija, generado una vez
inline const juce::Image& brushedTile()
{
    static juce::Image streaks;
    if (streaks.isNull())
    {
        streaks = juce::Image (juce::Image::ARGB, 512, 64, true);
        juce::Random rng (0xB0A5);
        for (int y = 0; y < 64; ++y)
        {
            const auto row = rng.nextFloat() - 0.5f;
            for (int x = 0; x < 512; ++x)
            {
                const auto v = 0.75f * row + 0.25f * (rng.nextFloat() - 0.5f);
                streaks.setPixelAt (x, y, v > 0.0f ? juce::Colours::white.withAlpha (0.10f * v) : juce::Colours::black.withAlpha (-0.14f * v));
            }
        }
    }
    return streaks;
}

// Veta de nogal (Vintage 60s): bandas de crecimiento (seno de y, deformado por una ondulación lenta a lo largo de x),
// fibra (ruido por fila, suavizado) y grano fino por píxel, entre nogal oscuro #2a170b y claro #6b4325. Mosaico de 512 × 256
// con semilla fija, generado una vez; todas las frecuencias son múltiplos enteros del periodo, así que empalma sin costura
inline const juce::Image& walnutTile()
{
    static juce::Image img;
    if (img.isNull())
    {
        constexpr int w = 512, h = 256;
        img = juce::Image (juce::Image::RGB, w, h, false);
        juce::Random rng (0x3A1D);
        std::array<float, h> fibre {};
        for (auto& f : fibre) f = rng.nextFloat();
        for (int pass = 0; pass < 3; ++pass) // suavizado circular de la fibra (3 pasadas de media de 3)
        {
            auto prev = fibre;
            for (int y = 0; y < h; ++y)
                fibre[(size_t) y] = (prev[(size_t) ((y + h - 1) % h)] + prev[(size_t) y] + prev[(size_t) ((y + 1) % h)]) / 3.0f;
        }
        const auto dark = juce::Colour (0xff2a170b), light = juce::Colour (0xff6b4325);
        const auto twoPi = juce::MathConstants<float>::twoPi;
        for (int x = 0; x < w; ++x)
        {
            const auto warp = 9.0f * std::sin (twoPi * 2.0f * (float) x / (float) w + 1.3f) + 3.5f * std::sin (twoPi * 7.0f * (float) x / (float) w);
            for (int y = 0; y < h; ++y)
            {
                const auto yy = (float) y + warp;
                const auto ring = 0.5f + 0.5f * std::sin (twoPi * 9.0f * yy / (float) h + 1.6f * std::sin (twoPi * 2.0f * yy / (float) h));
                const auto row = (int) std::floor (yy);
                const auto fr = yy - (float) row;
                const auto fib = fibre[(size_t) (((row % h) + h) % h)] * (1.0f - fr) + fibre[(size_t) ((((row + 1) % h) + h) % h)] * fr;
                const auto t = juce::jlimit (0.0f, 1.0f, 0.45f * ring * ring * ring + 0.4f * fib + 0.15f * rng.nextFloat());
                img.setPixelAt (x, y, dark.interpolatedWith (light, t));
            }
        }
    }
    return img;
}

// Cara de los paneles de sección (KoniLookAndFeel::drawGroupComponentOutline), arriba y abajo del degradado vertical.
// Default: el gris de siempre. Brushed Metal: acero claro (L ≈ 45 % arriba), con vetas y brillo aparte. Green: verde
// grisáceo (S ≈ 0,33) y más claro que el mapeo de la rampa (#0c2a16, S ≈ 0,71). Amber: ámbar tostado más claro que el
// mapeo (#312102)
// Material de las piezas que se tocan (perillas, tapas de fader, teclas de dos posiciones, bisel de los LED de casilla):
// metal oscuro en Default y Brushed Metal; aluminio claro en Phosphor Green; topacio (gema ámbar translúcida, la luz la
// atraviesa y sale por el lado opuesto) en Phosphor Amber; baquelita negra brillante con tapa de aluminio torneado en Vintage
// 60s (como los mandos de los equipos de los 60). Los bloques de Vintage son negro mate: la baquelita se separa de ellos con
// un canto gris cálido y el brillo de su borde, no con la sombra (que sobre negro no se ve). Colores fijos (sin c()): es el
// material, no un tinte del skin
enum class Material { darkMetal, aluminium, topaz, bakelite };
inline Material material()
{
    switch (current())
    {
        case green:   return Material::aluminium;
        case amber:   return Material::topaz;
        case vintage: return Material::bakelite;
        default:      return Material::darkMetal;
    }
}

struct PanelFace { juce::Colour top, bottom; };
inline PanelFace panelFace()
{
    switch (current())
    {
        case metal: return { juce::Colour (0xff6e7782), juce::Colour (0xff4b535d) };
        case green: return { juce::Colour (0xff2a3f31), juce::Colour (0xff1f2f25) };
        case amber: return { juce::Colour (0xff4d3816), juce::Colour (0xff36270e) };
        // Aluminio anodizado en negro grafito, apenas cálido (placa de módulo de los 60; vetas y brillo en el marco, Widgets.h).
        // Algo más claro que el negro mate anterior (#1e1c1a): la baquelita de los mandos se distingue de él
        case vintage: return { juce::Colour (0xff2e2c29), juce::Colour (0xff1b1a18) };
        default:    return { juce::Colour (0xff353b43), juce::Colour (0xff282d34) };
    }
}

// Fondo de ventana. Default: el color plano de siempre. Fósforo: fondo tintado con textura de tubo: grano (ruido de ±4 % por
// píxel, mosaico de 128 × 128 generado una vez por skin con semilla fija), una línea de barrido cada 3 px, brillo central
// tenue y viñeta hacia los bordes; en Green, además, la placa de circuito (circuitTile). Coste: uno o dos rellenos en mosaico
// y dos degradados por repintado del fondo
inline void paintBackground (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour plain)
{
    if (current() == vintage)
    {
        // Nogal (walnutTile) con luz cenital tenue y viñeta hacia los bordes
        g.setTiledImageFill (walnutTile(), area.getX(), area.getY(), 1.0f);
        g.fillRect (area);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), 0.0f, (float) area.getY(),
                                                 juce::Colours::black.withAlpha (0.22f), 0.0f, (float) area.getBottom(), false));
        g.fillRect (area);
        return;
    }
    if (current() == metal)
    {
        // Acero cepillado: base algo más clara que Default, vetas horizontales (brushedTile) y luz cenital (más claro arriba,
        // más oscuro abajo)
        g.setColour (plain.brighter (0.25f));
        g.fillRect (area);
        g.setTiledImageFill (brushedTile(), area.getX(), area.getY(), 1.0f);
        g.fillRect (area);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, (float) area.getY(),
                                                 juce::Colours::black.withAlpha (0.18f), 0.0f, (float) area.getBottom(), false));
        g.fillRect (area);
        return;
    }
    if (! mono())
    {
        g.setColour (plain);
        g.fillRect (area);
        return;
    }
    const auto r = ramp();
    g.setColour (map (plain));
    g.fillRect (area);
    static juce::Image tile[count];
    auto& img = tile[current()];
    if (img.isNull())
    {
        img = juce::Image (juce::Image::ARGB, 128, 128, true);
        juce::Random rng (0x5EED);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
            {
                const auto n = rng.nextFloat();
                auto px = n < 0.5f ? juce::Colours::black.withAlpha (0.08f * (0.5f - n)) : r.phosphor.withAlpha (0.08f * (n - 0.5f));
                if (y % 3 == 0)
                    px = px.overlaidWith (juce::Colours::black.withAlpha (0.14f));
                img.setPixelAt (x, y, px);
            }
    }
    g.setTiledImageFill (img, area.getX(), area.getY(), 1.0f);
    g.fillRect (area);
    if (current() == green) // placa de circuito bajo el brillo y la viñeta
    {
        g.setTiledImageFill (circuitTile(), area.getX(), area.getY(), 1.0f);
        g.fillRect (area);
    }
    const auto c = area.toFloat().getCentre();
    const auto radius = 0.75f * std::hypot ((float) area.getWidth(), (float) area.getHeight());
    g.setGradientFill (juce::ColourGradient (r.phosphor.withAlpha (0.07f), c, r.phosphor.withAlpha (0.0f), c.translated (0.0f, radius * 0.5f), true));
    g.fillRect (area);
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, c, juce::Colours::black.withAlpha (0.45f),
                                             c.translated (radius * 0.66f, 0.0f), true));
    g.fillRect (area);
}

// Cara de un diálogo (PanelDialog, Overlay): rectángulo redondeado del color plano; en Brushed Metal, el mismo acero cepillado
// que la ventana (paintBackground recortado al rectángulo). Los monocromos conservan el color plano mapeado
inline void dialogFace (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour plain)
{
    if (current() != metal)
    {
        g.setColour (plain);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    const juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    paintBackground (g, r.getSmallestIntegerContainer(), plain);
}

// Esquema de LookAndFeel_V4 (fondos, contornos, texto, rellenos y menús de los componentes de JUCE) en el skin actual
inline juce::LookAndFeel_V4::ColourScheme scheme()
{
    auto s = juce::LookAndFeel_V4::getDarkColourScheme();
    for (int i = 0; i < juce::LookAndFeel_V4::ColourScheme::numColours; ++i)
    {
        const auto id = (juce::LookAndFeel_V4::ColourScheme::UIColour) i;
        s.setUIColour (id, map (s.getUIColour (id)));
    }
    return s;
}
} // namespace Skin

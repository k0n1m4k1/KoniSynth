// Skins de la interfaz: Default (los colores de siempre) y fósforo monocromo, como los monitores de terminal de los 70/80:
// Green (P1, emisión de pico ≈ 525 nm) y Amber (P3, ≈ 602 nm); los sRGB elegidos son aproximaciones visuales, no colorimetría.
// Cada color literal de la UI pasa por Skin::c() / Skin::map(): en los monocromos se conserva su luminancia (pesos Rec. 709
// sobre los valores sRGB, sin linealizar: aproximación) y se pinta en la rampa del fósforo: negro -> fondo tintado, gris medio ->
// fósforo a media intensidad, blanco -> fósforo "caliente" casi blanco. La jerarquía de contraste de la interfaz se mantiene sin
// tocar cada componente; el alfa es el del color original. Ajuste global (todas las instancias): clave "skin" de %APPDATA%/KoniSynth/config.settings.
// Extraído como cabecera: se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación.
#pragma once

namespace Skin
{
enum Id { standard = 0, green, amber, metal, count }; // el orden es el valor guardado en config.settings

inline const char* name (int id)
{
    static const char* names[] = { "Default", "Phosphor Green", "Phosphor Amber", "Brushed Metal" };
    return names[juce::jlimit (0, (int) count - 1, id)];
}

// Valor guardado en config.settings (clave "skin"). Si aún no está, se migra el skin.txt de versiones anteriores y se borra
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
    return juce::jlimit (0, (int) count - 1, config->getIntValue ("skin", standard));
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

inline juce::Colour map (juce::Colour c)
{
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

// Líneas de barrido de los displays (solo en los monocromos): una línea oscura cada 3 px, 18 % de opacidad
inline void scanlines (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! mono())
        return;
    g.setColour (juce::Colours::black.withAlpha (0.18f));
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
        default:    return juce::Colour (0xff42a2c8);
    }
}

// Fondo de ventana. Default: el color plano de siempre. Fósforo: fondo tintado con textura de tubo: grano (ruido de ±4 % por
// píxel, mosaico de 128 × 128 generado una vez por skin con semilla fija), una línea de barrido cada 3 px, brillo central
// tenue y viñeta hacia los bordes. Coste: un relleno en mosaico y dos degradados por repintado del fondo
inline void paintBackground (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour plain)
{
    if (current() == metal)
    {
        // Acero cepillado: base algo más clara que Default, vetas horizontales (cada fila con su brillo, ±5 %, y grano fino
        // a lo largo de ella; mosaico de 512 × 64 con semilla fija) y luz cenital (más claro arriba, más oscuro abajo)
        g.setColour (plain.brighter (0.25f));
        g.fillRect (area);
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
        g.setTiledImageFill (streaks, area.getX(), area.getY(), 1.0f);
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
    const auto c = area.toFloat().getCentre();
    const auto radius = 0.75f * std::hypot ((float) area.getWidth(), (float) area.getHeight());
    g.setGradientFill (juce::ColourGradient (r.phosphor.withAlpha (0.07f), c, r.phosphor.withAlpha (0.0f), c.translated (0.0f, radius * 0.5f), true));
    g.fillRect (area);
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, c, juce::Colours::black.withAlpha (0.45f),
                                             c.translated (radius * 0.66f, 0.0f), true));
    g.fillRect (area);
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

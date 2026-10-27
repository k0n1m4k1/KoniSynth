// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// UI: configuración global (ventana emergente desde el botón Settings). La casilla AVX2 se guarda en
// %APPDATA%\KoniSynth\config.settings y se aplica al instante en todas las instancias del proceso.
// UI: panel grande y modal dentro del editor (sobre el contenido escalado): oscurece el fondo, muestra un cuerpo con título
// y una X de cierre en la esquina superior derecha. Lo posee el editor; la X o Escape lo cierran de forma diferida (nunca
// dentro de su propio onClick).
class PanelDialog : public juce::Component
{
public:
    PanelDialog (const juce::String& t, std::unique_ptr<juce::Component> b, int w, int h, std::function<void()> onClose)
        : title (t), body (std::move (b)), panelW (w), panelH (h), closeFn (std::move (onClose))
    {
        setWantsKeyboardFocus (true);
        close.onClick = [this] { requestClose(); };
        close.setTooltip ("Close this panel (Esc).");
        addAndMakeVisible (close);
        addAndMakeVisible (*body);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Skin::black().withAlpha (0.6f));
        const auto p = panel().toFloat();
        g.setColour (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.08f));
        g.fillRoundedRectangle (p, 8.0f);
        g.setColour (Skin::c (0xff42a2c8).withAlpha (0.6f));
        g.drawRoundedRectangle (p, 8.0f, 1.2f);
        g.setColour (Skin::white());
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText (title, panel().reduced (16, 10).removeFromTop (28), juce::Justification::left);
    }

    void resized() override
    {
        auto r = panel().reduced (16, 10);
        auto top = r.removeFromTop (28);
        close.setBounds (top.removeFromRight (28).translated (6, 0)); // 28 × 28 px, a 10 px de los cantos del panel
        r.removeFromTop (8);
        body->setBounds (r);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { requestClose(); return true; }
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panel().contains (e.getPosition()))
            requestClose(); // clic fuera del panel
    }

private:
    juce::Rectangle<int> panel() const { return getLocalBounds().withSizeKeepingCentre (panelW, panelH); }

    void requestClose()
    {
        juce::MessageManager::callAsync ([safe = SafePointer<PanelDialog> (this)]
        {
            if (auto* d = safe.getComponent())
            {
                auto fn = d->closeFn; // copia: fn destruye el panel
                if (fn) fn();
            }
        });
    }

    juce::String title;
    std::unique_ptr<juce::Component> body;
    int panelW, panelH;
    std::function<void()> closeFn;
    // X de cierre: aspa de 2 px en un cuadrado de 10 px; al pasar el ratón, fondo rojo tenue y aspa blanca
    struct CloseCross : juce::Button
    {
        CloseCross() : juce::Button ("Close") {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            if (over || down)
            {
                g.setColour (juce::Colour (0xffd04040).withAlpha (down ? 0.75f : 0.55f));
                g.fillRoundedRectangle (r, 5.0f);
            }
            const auto x = r.withSizeKeepingCentre (10.0f, 10.0f);
            g.setColour (Skin::white().withAlpha (over || down ? 1.0f : 0.7f));
            g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 2.0f);
            g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, 2.0f);
        }
    };
    CloseCross close;
};

// UI: cuerpo del diálogo About (dentro de un PanelDialog). Retrato con inclinación tipo tarjeta holográfica que sigue al
// ratón: escala + shear afín (aproximación de perspectiva), brillo especular radial y sombra opuesta a la inclinación.
// Timer a 60 Hz solo mientras el componente se muestra (existe solo con el About abierto); nada toca el hilo de audio.
// La bandera se dibuja con Graphics: Segoe UI Emoji no trae glifos de bandera (U+1F1EA U+1F1F8 sale como "ES").
// Huevo de pascua: clic en el retrato (cursor de mano) -> loop de Effects/EasterEgg.h y el retrato baila a 128 BPM (pulso
// en cada negra, giro alterno, salto en las semicorcheas y destello de color). Al cerrar el About (o al ocultarse) se para.
class AboutBody : public juce::Component, private juce::Timer
{
public:
    explicit AboutBody (EasterEgg& e) : egg (e)
    {
        avatar = juce::ImageCache::getFromMemory (BinaryData::aboutavatar_jpg, BinaryData::aboutavatar_jpgSize);
    }

    ~AboutBody() override { egg.stop(); }

    void visibilityChanged() override { updateTimer(); if (! isShowing()) egg.stop(); }
    void parentHierarchyChanged() override { updateTimer(); if (! isShowing()) egg.stop(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (linkBounds.contains (e.position)) // el autor lleva al repositorio
        {
            juce::URL (repositoryUrl).launchInDefaultBrowser();
            return;
        }
        if (! cardBounds().contains (e.position)) return;
        if (egg.isOn()) egg.stop(); else egg.start();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto link = linkBounds.contains (e.position);
        setMouseCursor (cardBounds().contains (e.position) || link ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (link != overLink) { overLink = link; repaint(); }
        setTarget (e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override { setTarget (e.position); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; if (overLink) { overLink = false; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto card = cardBounds();
        const auto c = card.getCentre();
        // Inclinación: tiltX > 0 = ratón a la derecha (borde derecho hacia el espectador)
        // Baile del huevo de pascua (beat = posición del loop en negras; < 0 sin sonar)
        float pulse = 0.0f, spin = 0.0f, hop = 0.0f;
        if (beat >= 0.0)
        {
            const auto frac = (float) (beat - std::floor (beat));
            const auto sixteenth = (float) (beat * 4.0 - std::floor (beat * 4.0));
            pulse = std::exp (-7.0f * frac);                                         // golpe en cada negra
            spin = ((int) std::floor (beat) % 2 == 0 ? 1.0f : -1.0f) * 0.10f * pulse; // giro alterno ±0,1 rad
            hop = ((int) std::floor (beat * 4.0) % 2 == 1 ? -6.0f : 0.0f) * (1.0f - sixteenth); // salto a contratiempo
        }
        const auto tf = juce::AffineTransform::translation (-c.x, -c.y)
                            .scaled (1.0f - 0.10f * std::abs (tiltX) + 0.14f * pulse, 1.0f - 0.10f * std::abs (tiltY) + 0.14f * pulse)
                            .sheared (0.10f * tiltY, 0.12f * tiltX)
                            .rotated (spin)
                            .translated (c.x, c.y - 6.0f * lift + hop);

        juce::Path moved;
        moved.addRoundedRectangle (card, 14.0f);
        moved.applyTransform (tf);

        // Sombra: desplazada en sentido contrario a la inclinación, más difusa al "levantarse"
        juce::Path shadowPath (moved);
        shadowPath.applyTransform (juce::AffineTransform::translation (-10.0f * tiltX, 12.0f - 6.0f * tiltY + 6.0f * lift));
        juce::DropShadow (Skin::black().withAlpha (0.55f), 18, {}).drawForPath (g, shadowPath);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (moved);
            g.addTransform (tf);
            if (avatar.isValid())
            {
                g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
                g.drawImage (avatar, card, juce::RectanglePlacement::stretchToFit);
            }
            else // marcador si el recurso no carga
            {
                g.setColour (Skin::c (0xff2a3a48));
                g.fillRect (card);
                g.setColour (Skin::white().withAlpha (0.6f));
                g.setFont (juce::FontOptions (60.0f, juce::Font::bold));
                g.drawText ("K", card, juce::Justification::centred);
            }
            // Barrido holográfico diagonal: se desplaza con la inclinación
            const auto sweep = card.getWidth() * (0.5f + 0.6f * tiltX + 0.3f * tiltY);
            juce::ColourGradient sheen (juce::Colours::transparentWhite, card.getX() + sweep - 90.0f, card.getY(),
                                        juce::Colours::transparentWhite, card.getX() + sweep + 90.0f, card.getBottom(), false);
            sheen.addColour (0.42, Skin::c (0xff6fd6ff).withAlpha (0.10f));
            sheen.addColour (0.50, Skin::white().withAlpha (0.22f));
            sheen.addColour (0.58, Skin::c (0xffff7adf).withAlpha (0.10f));
            g.setGradientFill (sheen);
            g.fillRect (card);
            // Brillo especular radial en el punto "más cercano" a la luz (sigue al ratón)
            const juce::Point<float> spot (c.x + tiltX * card.getWidth() * 0.45f, c.y + tiltY * card.getHeight() * 0.45f);
            juce::ColourGradient spec (Skin::white().withAlpha (0.18f + 0.22f * lift), spot,
                                       juce::Colours::transparentWhite, spot.translated (card.getWidth() * 0.6f, 0.0f), true);
            g.setGradientFill (spec);
            g.fillRect (card);
            if (beat >= 0.0) // destello de color en cada golpe, el tono rota cada compás
            {
                const auto hue = (float) std::fmod (beat / 4.0, 1.0);
                g.setColour (Skin::map (juce::Colour::fromHSV (hue, 0.9f, 1.0f, 0.35f * pulse)));
                g.fillRect (card);
            }
        }
        g.setColour (Skin::white().withAlpha (0.35f + 0.3f * lift));
        g.strokePath (moved, juce::PathStrokeType (1.5f));

        // Texto (columna derecha)
        auto r = getLocalBounds().toFloat().withTrimmedLeft (card.getRight() + 36.0f).withTrimmedTop (18.0f);
        g.setColour (Skin::white());
        g.setFont (juce::FontOptions (38.0f, juce::Font::bold));
        g.drawText ("KoniSynth", r.removeFromTop (46.0f), juce::Justification::centredLeft);
        g.setColour (Skin::c (0xff42a2c8));
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("Version " + versionString() + "  -  2026", r.removeFromTop (24.0f), juce::Justification::centredLeft);
        r.removeFromTop (18.0f);

        // "Author: " y el nombre como enlace al repositorio (subrayado; más claro con el ratón encima)
        g.setColour (Skin::white().withAlpha (0.92f));
        const juce::FontOptions authorFont (18.0f);
        const juce::String prefix ("Author: "), handle ("@k0n1m4k1");
        auto authorRow = r.removeFromTop (28.0f);
        g.setFont (authorFont);
        const auto prefixW = juce::GlyphArrangement::getStringWidth (juce::Font (authorFont), prefix);
        const auto handleW = juce::GlyphArrangement::getStringWidth (juce::Font (authorFont), handle);
        g.drawText (prefix, authorRow, juce::Justification::centredLeft);
        linkBounds = { authorRow.getX() + prefixW, authorRow.getY(), handleW, authorRow.getHeight() };
        g.setColour (overLink ? Skin::white() : Skin::c (0xff42a2c8));
        g.drawText (handle, linkBounds, juce::Justification::centredLeft);
        g.fillRect (linkBounds.getX(), linkBounds.getCentreY() + 10.0f, handleW, 1.0f);
        drawSpanishFlag (g, { linkBounds.getRight() + 10.0f, authorRow.getCentreY() - 10.0f, 30.0f, 20.0f });
        r.removeFromTop (14.0f);

        g.setColour (Skin::white().withAlpha (0.8f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("License: GNU AGPLv3 (see LICENSE and THIRD-PARTY-NOTICES)", r.removeFromTop (22.0f), juce::Justification::centredLeft);
        g.setColour (Skin::white().withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Copyright (c) 2026 @k0n1m4k1", r.removeFromTop (20.0f), juce::Justification::centredLeft);
    }

private:
    static inline const juce::String repositoryUrl { "https://github.com/k0n1m4k1/KoniSynth" };
    juce::Rectangle<float> linkBounds; // del nombre del autor, calculado al pintar
    bool overLink = false;

    static juce::String versionString()
    {
       #ifdef KONI_VERSION_LABEL
        return KONI_VERSION_LABEL;
       #else
        return JucePlugin_VersionString;
       #endif
    }

    // Bandera de España: franjas roja-amarilla-roja 1:2:1 (proporción 3:2), #AA151B y #F1BF00
    static void drawSpanishFlag (juce::Graphics& g, juce::Rectangle<float> f)
    {
        const auto band = f.getHeight() / 4.0f;
        g.setColour (Skin::c (0xffaa151b));
        g.fillRect (f);
        g.setColour (Skin::c (0xfff1bf00));
        g.fillRect (f.withTrimmedTop (band).withTrimmedBottom (band));
        g.setColour (Skin::black().withAlpha (0.35f));
        g.drawRect (f, 1.0f);
    }

    juce::Rectangle<float> cardBounds() const
    {
        const auto side = juce::jmin (220.0f, (float) getHeight() - 40.0f);
        return { 24.0f, ((float) getHeight() - side) * 0.5f, side, side };
    }

    void setTarget (juce::Point<float> p)
    {
        const auto card = cardBounds().expanded (60.0f);
        hovering = card.contains (p);
        if (hovering)
        {
            targetX = juce::jlimit (-1.0f, 1.0f, (p.x - card.getCentreX()) / (card.getWidth() * 0.5f));
            targetY = juce::jlimit (-1.0f, 1.0f, (p.y - card.getCentreY()) / (card.getHeight() * 0.5f));
        }
    }

    void updateTimer()
    {
        if (isShowing()) { if (! isTimerRunning()) startTimerHz (60); }
        else stopTimer();
    }

    void timerCallback() override
    {
        if (! isShowing()) { stopTimer(); return; }
        phase += 1.0f / 60.0f;
        // Sin ratón encima: balanceo suave (0,25 de amplitud, periodo ~6 s) para que la tarjeta no quede muerta
        const auto tx = hovering ? targetX : 0.25f * std::sin (phase * 1.05f);
        const auto ty = hovering ? targetY : 0.15f * std::sin (phase * 0.7f + 1.0f);
        tiltX += (tx - tiltX) * 0.15f;
        tiltY += (ty - tiltY) * 0.15f;
        lift  += ((hovering ? 1.0f : 0.0f) - lift) * 0.12f;
        beat = egg.position();
        repaint();
    }

    EasterEgg& egg;
    double beat = -1.0;
    juce::Image avatar;
    float tiltX = 0.0f, tiltY = 0.0f, targetX = 0.0f, targetY = 0.0f, lift = 0.0f, phase = 0.0f;
    bool hovering = false;
};

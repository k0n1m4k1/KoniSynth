// Display superior: osciloscopio, analizador de espectro, espectrograma, goniómetro, vúmetros y ARCADE (UI/Arcade.h), sobre el búfer estéreo que
// el procesador rellena (scopeLeft/scopeRight, sin bloqueos; no añade latencia al audio). Clic: siguiente modo; clic
// derecho: elegir modo o desacoplar a una ventana propia (redimensionable, doble clic = pantalla completa).
// Se incluye desde KoniSynth.cpp (mismo TU), tras UI/Displays.h.
#pragma once

class Visualizer : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum Mode { scope, spectrum, spectrogram, goniometer, meters, arcade, demo, numModes }; // solo al final: el proyecto guarda el índice

    Visualizer (KoniSynthProcessor& p, std::function<void()> onDetach = {}) : processor (p), detach (std::move (onDetach))
    {
        setTooltip ("Display: click for the next view (scope, spectrum, spectrogram, stereo goniometer, VU meters, arcade: a pixel-art "
                    "shooter played by the music, demo: the brand loop of a 90s car stereo display, driven by the music). Right-click: choose a view, the style and the bezel, or open it in its own window.");
        startTimerHz (30);
    }

    void setMode (int m) { mode = juce::jlimit (0, numModes - 1, m); spectroImage = {}; repaint(); if (onModeChange) onModeChange (mode, style); }
    int getMode() const { return mode; }
    // Estilo: 0 Modern, 1 fósforo verde (monitor monocromo P1), 2 fósforo ámbar (P3): monocromo con barrido, halo y viñeta
    void setStyle (int st) { style = juce::jlimit (0, 2, st); spectroImage = {}; repaint(); if (onModeChange) onModeChange (mode, style); }
    int getStyle() const { return style; }
    std::function<void (int, int)> onModeChange;
    // Marco de la pantalla: Auto (el material sigue al skin y al estilo, ver material()), latón (steampunk) o sin marco
    enum Bezel { bezelAuto, bezelBrass, bezelNone, numBezels };
    void setBezel (int b) { bezel = juce::jlimit (0, numBezels - 1, b); repaint(); if (onBezelChange) onBezelChange (bezel); }
    int getBezel() const { return bezel; }
    std::function<void (int)> onBezelChange;
    void setShowAnnunciators (bool show) { showAnnunciators = show; repaint(); }

    // Rótulo temporal sobre cualquier vista (p. ej. cómo tocar el preset recién cargado): título y línea, visible
    // captionSeconds y desvanecido en el último segundo. Texto vacío = quitarlo
    void showCaption (const juce::String& title, const juce::String& line)
    {
        captionTitle = title;
        captionLine = line;
        captionUntil = line.isEmpty() ? 0.0 : juce::Time::getMillisecondCounterHiRes() + captionSeconds * 1000.0;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            static const char* names[numModes] = { "Oscilloscope", "Spectrum analyzer", "Spectrogram", "Stereo goniometer", "VU meters",
                                                   "Arcade (pixel shooter)", "Demo (brand loop)" };
            for (int k = 0; k < numModes; ++k)
                m.addItem (k + 1, names[k], true, k == mode);
            m.addSeparator();
            static const char* styles[3] = { "Style: Modern", "Style: Green phosphor", "Style: Amber phosphor" };
            for (int k = 0; k < 3; ++k)
                m.addItem (200 + k, styles[k], true, k == style);
            m.addSeparator();
            static const char* bezels[numBezels] = { "Bezel: Auto (follows the skin)", "Bezel: Brass (steampunk)", "Bezel: None" };
            for (int k = 0; k < numBezels; ++k)
                m.addItem (300 + k, bezels[k], true, k == bezel);
            if (detach)
            {
                m.addSeparator();
                m.addItem (100, "Open in its own window");
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                             [safe = juce::Component::SafePointer<Visualizer> (this)] (int id)
                             {
                                 if (safe == nullptr || id == 0) return;
                                 if (id == 100) { if (safe->detach) safe->detach(); }
                                 else if (id >= 300) safe->setBezel (id - 300);
                                 else if (id >= 200) safe->setStyle (id - 200);
                                 else safe->setMode (id - 1);
                             });
        }
        else if (const auto ann = annunciatorAt (e.position); ann >= 0 || annunciatorArea.contains (e.position))
        {
            // Columna de anunciadores: solo informativa (tooltip); un clic en ella no cambia de vista
        }
        else if (! e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1)
        {
            setMode ((mode + 1) % numModes);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto framed = bezel != bezelNone;
        if (framed)
            r = paintBezel (g, r);
        g.setColour (style == 0 ? juce::Colours::black.withAlpha (framed ? 0.85f : 0.6f) : ink().withMultipliedBrightness (0.08f).withAlpha (1.0f));
        g.fillRoundedRectangle (r, framed ? screenRadius : 6.0f);
        const auto full = r;
        r = r.reduced (6.0f);
        annunciatorArea = {};
        if (showAnnunciators)
        {
            annunciatorArea = r.removeFromLeft (annunciatorWidth);
            paintAnnunciators (g, annunciatorArea);
            r.removeFromLeft (6.0f);
        }
        switch (mode)
        {
            case scope:       paintScope (g, r); break;
            case spectrum:    paintSpectrum (g, r); break;
            case spectrogram: paintSpectrogram (g, r); break;
            case goniometer:  paintGoniometer (g, r); break;
            case arcade:      g.setOpacity (1.0f); game.paint (g, r, style, ink()); break;
            case demo:        brandDemo.paint (g, r, ink()); break;
            default:          paintMeters (g, r); break;
        }
        static const char* labels[numModes] = { "SCOPE", "SPECTRUM", "SPECTROGRAM", "GONIOMETER", "VU", "", "" };
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (labels[mode], r.removeFromTop (14.0f).removeFromLeft (120.0f), juce::Justification::topLeft);
        paintCaption (g, full.reduced (10.0f));
        if (style != 0)
            paintCrt (g, full);
        else
            Skin::scanlines (g, full.toNearestInt()); // skins de fósforo: barrido también en el estilo normal
        if (framed)
            paintGlass (g, full);
    }

    juce::String getTooltip() override
    {
        if (const auto i = annunciatorAt (getMouseXYRelative().toFloat()); i >= 0)
            return annunciatorTip (i);
        return juce::SettableTooltipClient::getTooltip();
    }

private:
    // Anunciadores: columna a la izquierda de la pantalla con el estado de la instancia, como los de un display de hardware
    // (acrónimo tenue = apagado, encendido = activo). 16 en 4 columnas x 4 filas; se leen en cada repintado (30 Hz).
    // Solo en el display superior y su ventana: otras instancias (el espectro del EQ) los ocultan con setShowAnnunciators
    static constexpr float annunciatorWidth = 140.0f;
    bool showAnnunciators = true;
    float annunciatorSpace() const { return showAnnunciators ? annunciatorWidth + 6.0f : 0.0f; }
    static constexpr int annunciatorCount = 16, annunciatorColumns = 4;
    juce::Rectangle<float> annunciatorArea;
    juce::uint32 lastMidiBlocks = 0;
    double midiFlashUntil = 0.0;
    // Orden de pantalla, por filas: afinación y voces; juego; salida y entrada; MIDI y transporte
    enum { annScl, annMts, annMpe, annMono, annArp, annSeq, annHold, annVel, annReverb, annLimiter, annStereo, annInput,
           annMidi, annLearn, annPlay, annRec };

    // Motores que ignoran la microafinación (Dsp/Tuning.h): Theremin (sin notas), Drum Machine (la tecla elige el
    // instrumento), ANS y MT-32 (Munt afina por su cuenta)
    bool engineTunes() const
    {
        const auto e = (int) processor.apvts.getRawParameterValue ("engine")->load();
        return e != 1 && e != 5 && e != 9 && e != 14;
    }
    bool param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load() > 0.5f; }
    bool engineIs (int e) const { return (int) processor.apvts.getRawParameterValue ("engine")->load() == e; }

    // 0 apagado, 1 encendido, 0,5 encendido pero sin efecto en el motor elegido
    float annunciatorLevel (int i) const
    {
        const auto tunes = engineTunes() ? 1.0f : 0.5f;
        switch (i)
        {
            case annScl:    return processor.tuning.hasScala() ? tunes : 0.0f;
            case annMts:    return processor.tuning.mtsActive() ? tunes : 0.0f;
            case annArp:    return param ("arpOn") ? 1.0f : 0.0f;
            case annSeq:    return param ("seqOn") ? 1.0f : 0.0f;
            case annHold:   return processor.sustainDown.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
            case annMpe:    return param ("mpeOn") ? 1.0f : 0.0f;
            case annMono:   return processor.apvts.getRawParameterValue ("voiceMode")->load() > 0.5f ? 1.0f : 0.0f;
            case annLearn:  return processor.midiLearn.learning.load() >= 0 ? 1.0f : 0.0f;
            case annVel:    return param ("drumVelocity") ? (engineIs (5) ? 1.0f : 0.5f) : 0.0f;
            case annReverb: return param ("ambientOn") ? 1.0f : 0.0f;
            case annLimiter: return param ("limOn") ? 1.0f : 0.0f;
            case annInput:  return param ("vocoderOn") || processor.apvts.getRawParameterValue ("inputMonitor")->load() > 0.0f ? 1.0f : 0.0f;
            case annPlay:   return processor.sessionTransport.isPlaying() ? 1.0f : 0.0f;
            case annStereo: return param ("monoOut") ? 0.0f : 1.0f;
            case annMidi:   return juce::Time::getMillisecondCounterHiRes() < midiFlashUntil ? 1.0f : 0.0f;
            default:        return processor.sessionTransport.isRecording() ? 1.0f : 0.0f;
        }
    }

    static const char* annunciatorLabel (int i)
    {
        static const char* labels[annunciatorCount] = { "SCL", "MTS", "MPE", "MONO", "ARP", "SEQ", "HOLD", "VEL",
                                                        "REV", "LIM", "ST", "IN", "MIDI", "LRN", "PLAY", "REC" };
        return labels[i];
    }

    juce::String annunciatorTip (int i) const
    {
        const auto ignored = engineTunes() ? juce::String() : juce::String (" The selected engine ignores microtuning.");
        switch (i)
        {
            case annScl:    return "SCL: a Scala scale is loaded (" + (processor.tuning.hasScala() ? processor.tuning.getScaleName() : juce::String ("none"))
                                   + "). Saved with the project, not with presets." + ignored;
            case annMts:    return "MTS: the tuning comes from an MTS-ESP master or MIDI Tuning Standard SysEx"
                                   + (processor.tuning.mtsActive() ? " (" + processor.tuning.mtsScaleName() + ")." : juce::String (".")) + ignored;
            case annArp:    return "ARP: arpeggiator on.";
            case annSeq:    return "SEQ: step sequencer on.";
            case annHold:   return "HOLD: sustain pedal (CC 64) held down.";
            case annMpe:    return "MPE: MIDI Polyphonic Expression on.";
            case annMono:   return "MONO: one voice at a time (Voice Mode Mono or Legato).";
            case annLearn:  return "LRN: MIDI Learn is waiting for a controller.";
            case annVel:    return "VEL: Drum Machine continuous velocity (off: normal and accent levels only)."
                                   + juce::String (engineIs (5) ? "" : " Half lit: only the Drum Machine uses it.");
            case annReverb: return "REV: reverb on.";
            case annLimiter: return "LIM: output limiter on.";
            case annInput:  return "IN: audio input in use (Input Monitor above 0 % or the vocoder on).";
            case annPlay:   return "PLAY: the session transport is playing.";
            case annStereo: return "ST: stereo output (off: Mono Output).";
            case annMidi:   return "MIDI: lights up while MIDI arrives from the host or a controller.";
            default:        return "REC: the session transport is recording.";
        }
    }

    // Celda del anunciador i y el anunciador bajo un punto (-1 si ninguno)
    juce::Rectangle<float> annunciatorCell (int i) const
    {
        const auto rows = (annunciatorCount + annunciatorColumns - 1) / annunciatorColumns;
        const auto w = annunciatorArea.getWidth() / (float) annunciatorColumns, h = annunciatorArea.getHeight() / (float) rows;
        return { annunciatorArea.getX() + w * (float) (i % annunciatorColumns), annunciatorArea.getY() + h * (float) (i / annunciatorColumns), w, h };
    }
    int annunciatorAt (juce::Point<float> p) const
    {
        for (int i = 0; i < annunciatorCount; ++i)
            if (annunciatorCell (i).contains (p))
                return i;
        return -1;
    }

    void paintAnnunciators (juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (const auto n = processor.midiInBlocks.load (std::memory_order_relaxed); n != lastMidiBlocks)
        {
            lastMidiBlocks = n;
            midiFlashUntil = juce::Time::getMillisecondCounterHiRes() + 150.0; // destello de 150 ms por cada llegada
        }
        g.setColour (ink().withAlpha (0.18f));
        g.drawVerticalLine (juce::roundToInt (area.getRight() + 3.0f), area.getY(), area.getBottom());
        const auto fontH = juce::jlimit (8.0f, 12.0f, annunciatorCell (0).getHeight() * 0.55f); // 12 px con la fila de 100 px
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), fontH, juce::Font::bold));
        for (int i = 0; i < annunciatorCount; ++i)
        {
            const auto cell = annunciatorCell (i).reduced (2.0f, 2.0f);
            const auto level = annunciatorLevel (i);
            const auto colour = i == annRec ? inkHot() : ink();
            if (level > 0.0f)
            {
                g.setColour (colour.withAlpha (0.18f * level));
                g.fillRoundedRectangle (cell, 3.0f);
            }
            g.setColour (colour.withAlpha (level > 0.0f ? 0.2f + 0.8f * level : 0.4f)); // apagado: tenue pero legible
            g.drawText (annunciatorLabel (i), cell, juce::Justification::centred, false);
        }
    }

    // Color principal según el estilo
    juce::Colour ink() const
    {
        return style == 1 ? juce::Colour (0xff41ff6a) : style == 2 ? juce::Colour (0xffffb000) : juce::Colour (0xff42a2c8);
    }
    juce::Colour inkHot() const { return style == 0 ? juce::Colour (0xffff3030) : ink().brighter (0.6f); }

    static constexpr double captionSeconds = 8.0;
    juce::String captionTitle, captionLine;
    double captionUntil = 0.0;

    // Rótulo abajo a la derecha, en el color del estilo, sobre una banda oscura para leerse encima de la traza
    void paintCaption (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto remaining = (captionUntil - juce::Time::getMillisecondCounterHiRes()) / 1000.0;
        if (remaining <= 0.0 || captionLine.isEmpty())
            return;
        const auto alpha = (float) juce::jmin (1.0, remaining); // desvanecido en el último segundo
        const juce::Font titleFont (juce::FontOptions (13.0f, juce::Font::bold)), lineFont (juce::FontOptions (15.0f, juce::Font::bold));
        const auto line = juce::String::fromUTF8 ("\xe2\x96\xb6 ") + captionLine; // ▶
        const auto w = juce::jmin (r.getWidth(), 24.0f + juce::jmax (juce::GlyphArrangement::getStringWidth (titleFont, captionTitle),
                                                                     juce::GlyphArrangement::getStringWidth (lineFont, line)));
        auto box = r.removeFromBottom (44.0f).removeFromRight (w);
        g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
        g.fillRoundedRectangle (box, 5.0f);
        box = box.reduced (12.0f, 4.0f);
        g.setColour (ink().withAlpha (0.75f * alpha));
        g.setFont (titleFont);
        g.drawText (captionTitle, box.removeFromTop (16.0f), juce::Justification::centredLeft, true);
        g.setColour (ink().brighter (0.3f).withAlpha (alpha));
        g.setFont (lineFont);
        g.drawText (line, box, juce::Justification::centredLeft, true);
    }

    // Trazo con halo (fósforo): pasada ancha y tenue, después la fina
    void glowStroke (juce::Graphics& g, const juce::Path& path, float width) const
    {
        if (style != 0)
        {
            g.setColour (ink().withAlpha (0.18f));
            g.strokePath (path, juce::PathStrokeType (width * 4.0f));
        }
        g.setColour (ink());
        g.strokePath (path, juce::PathStrokeType (width));
    }

    // Grosor del marco proporcional al alto (5-12 px), igual en todos los materiales: la pantalla no cambia de tamaño
    static float bezelWidth (juce::Rectangle<float> r) { return juce::jlimit (5.0f, 12.0f, r.getHeight() * 0.09f); }
    static juce::Rectangle<float> bezelScreen (juce::Rectangle<float> r) { return r.reduced (bezelWidth (r)); }
    static constexpr float screenRadius = 9.0f; // esquinas del cristal con marco: más redondas, como un tubo

    // Material del marco. Auto: acero cepillado con el skin Brushed Metal; carcasa de monitor de terminal con los estilos de
    // fósforo; grafito con el estilo Modern. Los dos plásticos pasan por Skin::c (se tiñen con los skins de fósforo)
    enum class Material { brass, graphite, terminal, steel };
    Material material() const
    {
        if (bezel == bezelBrass) return Material::brass;
        if (Skin::current() == Skin::metal) return Material::steel;
        return style != 0 ? Material::terminal : Material::graphite;
    }

    // Devuelve la pantalla (el hueco del marco)
    juce::Rectangle<float> paintBezel (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto m = material();
        if (m == Material::brass)
            return paintBrass (g, r);

        const auto bw = bezelWidth (r);
        // Degradado vertical (luz cenital), arista superior clara y contorno oscuro
        juce::Colour top, bottom;
        auto lip = 0.18f; // brillo de la arista superior
        switch (m)
        {
            case Material::steel:    top = juce::Colour (0xffd2d6da); bottom = juce::Colour (0xff6c7076); lip = 0.6f; break;
            case Material::terminal: top = Skin::c (0xff3b3935);      bottom = Skin::c (0xff161513); break;
            default:                 top = Skin::c (0xff3d434b);      bottom = Skin::c (0xff131519); lip = 0.25f; break;
        }
        g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 8.0f);
        if (m == Material::steel)
        {
            // Vetas horizontales del cepillado: una línea por píxel con su brillo (±6 %), semilla fija
            juce::Graphics::ScopedSaveState save (g);
            juce::Path outer;
            outer.addRoundedRectangle (r, 8.0f);
            g.reduceClipRegion (outer);
            juce::Random rng (0xB0A5);
            for (auto y = r.getY(); y < r.getBottom(); y += 1.0f)
            {
                const auto v = rng.nextFloat() - 0.5f;
                g.setColour (v > 0.0f ? juce::Colours::white.withAlpha (0.12f * v) : juce::Colours::black.withAlpha (-0.16f * v));
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
            }
        }
        else if (m == Material::terminal)
        {
            // Plástico mate: un brillo ancho y muy tenue en la mitad superior
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0.0f, r.getY(), juce::Colours::transparentWhite,
                                                     0.0f, r.getCentreY(), false));
            g.fillRoundedRectangle (r, 8.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (lip));
        g.drawLine (r.getX() + 8.0f, r.getY() + 0.8f, r.getRight() - 8.0f, r.getY() + 0.8f, 1.2f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        // Chaflán interior: el marco baja hacia el cristal (oscuro arriba, luz en el labio inferior)
        const auto screen = r.reduced (bw);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRoundedRectangle (screen.expanded (2.0f), screenRadius + 2.0f);
        g.setColour (juce::Colours::white.withAlpha (m == Material::steel ? 0.35f : 0.12f));
        g.drawLine (screen.getX() + screenRadius, screen.getBottom() + 1.6f, screen.getRight() - screenRadius, screen.getBottom() + 1.6f, 1.0f);

        if (m == Material::steel) // tornillos de estrella en las esquinas
            for (auto c : { juce::Point<float> (r.getX() + bw * 0.5f, r.getY() + bw * 0.5f), juce::Point<float> (r.getRight() - bw * 0.5f, r.getY() + bw * 0.5f),
                            juce::Point<float> (r.getX() + bw * 0.5f, r.getBottom() - bw * 0.5f), juce::Point<float> (r.getRight() - bw * 0.5f, r.getBottom() - bw * 0.5f) })
            {
                const auto d = bw * 0.55f;
                const auto head = juce::Rectangle<float> (d, d).withCentre (c);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeef0f2), head.getX(), head.getY(), juce::Colour (0xff50545a), head.getRight(), head.getBottom(), false));
                g.fillEllipse (head);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.drawLine (c.x - d * 0.3f, c.y, c.x + d * 0.3f, c.y, 1.0f);
                g.drawLine (c.x, c.y - d * 0.3f, c.x, c.y + d * 0.3f, 1.0f);
            }
        return screen;
    }

    // Marco de latón: degradado de latón con bisel (arista clara arriba, oscura abajo), remaches en las esquinas y cada ~140 px
    // en los lados largos y rebaje oscuro alrededor de la pantalla. Devuelve la pantalla
    juce::Rectangle<float> paintBrass (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto bw = bezelWidth (r);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8c874), 0.0f, r.getY(), juce::Colour (0xff6e4d12), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 8.0f);
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), r.getX(), 0.0f, juce::Colours::white.withAlpha (0.0f), r.getRight(), 0.0f, false);
        sheen.addColour (0.3, juce::Colours::white.withAlpha (0.18f)); // brillo de latón pulido
        sheen.addColour (0.7, juce::Colours::black.withAlpha (0.12f));
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xfffff2c0).withAlpha (0.7f));
        g.drawLine (r.getX() + 8.0f, r.getY() + 0.8f, r.getRight() - 8.0f, r.getY() + 0.8f, 1.2f); // arista superior
        g.setColour (juce::Colour (0xff2e1f05));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        const auto screen = r.reduced (bw);
        g.setColour (juce::Colour (0xff1c1204)); // rebaje
        g.fillRoundedRectangle (screen.expanded (1.5f), screenRadius + 1.5f);
        g.setColour (juce::Colour (0xffd9b25a).withAlpha (0.5f));
        g.drawLine (screen.getX() + 4.0f, screen.getBottom() + 1.8f, screen.getRight() - 4.0f, screen.getBottom() + 1.8f, 1.0f); // luz en el labio inferior
        const auto rivet = [&g, bw] (float x, float y)
        {
            const auto d = bw * 0.55f;
            const auto c = juce::Rectangle<float> (d, d).withCentre ({ x, y });
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillEllipse (c.translated (0.4f, 0.8f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0b8), c.getX() + d * 0.3f, c.getY() + d * 0.25f, juce::Colour (0xff5a3c0c),
                                                     c.getRight(), c.getBottom(), true));
            g.fillEllipse (c);
        };
        const auto inset = bw * 0.5f;
        const auto n = juce::jmax (1, juce::roundToInt ((r.getWidth() - 2.0f * inset) / 140.0f));
        for (int k = 0; k <= n; ++k)
        {
            const auto x = r.getX() + inset + (r.getWidth() - 2.0f * inset) * (float) k / (float) n;
            rivet (x, r.getY() + inset);
            rivet (x, r.getBottom() - inset);
        }
        return screen;
    }

    // Cristal de monitor (lente CRT) sobre la pantalla con marco, de fuera a dentro:
    //  1. Barril: viñeta radial que oscurece las esquinas (la curvatura del tubo aleja la luz de los bordes)
    //  2. Sombra del marco sobre el cristal: 14 px arriba, 10 px a los lados, 8 px abajo
    //  3. Reflejo curvo de ventana: banda clara bajo una elipse más ancha que la pantalla, en el tercio superior
    //  4. Punto de brillo arriba a la izquierda y arista inferior del cristal iluminada
    static void paintGlass (juce::Graphics& g, juce::Rectangle<float> screen)
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path glass;
        glass.addRoundedRectangle (screen, screenRadius);
        g.reduceClipRegion (glass);
        const auto x = screen.getX(), y = screen.getY(), w = screen.getWidth(), h = screen.getHeight();
        const auto c = screen.getCentre();

        juce::ColourGradient barrel (juce::Colours::transparentBlack, c, juce::Colours::black.withAlpha (0.42f), screen.getTopLeft(), true);
        barrel.addColour (0.6, juce::Colours::transparentBlack);
        g.setGradientFill (barrel);
        g.fillRect (screen);

        const auto edge = [&g] (juce::Rectangle<float> band, juce::Point<float> from, juce::Point<float> to, float alpha)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (alpha), from, juce::Colours::transparentBlack, to, false));
            g.fillRect (band);
        };
        const auto t = juce::jmin (14.0f, h * 0.2f), s = juce::jmin (10.0f, w * 0.1f), b = juce::jmin (8.0f, h * 0.12f);
        edge ({ x, y, w, t }, { 0.0f, y }, { 0.0f, y + t }, 0.55f);
        edge ({ x, y, s, h }, { x, 0.0f }, { x + s, 0.0f }, 0.35f);
        edge ({ x + w - s, y, s, h }, { x + w, 0.0f }, { x + w - s, 0.0f }, 0.35f);
        edge ({ x, y + h - b, w, b }, { 0.0f, y + h }, { 0.0f, y + h - b }, 0.25f);

        juce::Path window; // reflejo: interior de una elipse de 1,6·w × 1,5·h centrada sobre la pantalla
        window.addEllipse (juce::Rectangle<float> (w * 1.6f, h * 1.5f).withCentre ({ x + w * 0.42f, y - h * 0.48f }));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.11f), 0.0f, y, juce::Colours::white.withAlpha (0.0f), 0.0f, y + h * 0.3f, false));
        g.fillPath (window);

        const auto spot = juce::Rectangle<float> (w * 0.16f, h * 0.22f).withCentre ({ x + w * 0.13f, y + h * 0.2f });
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.08f), spot.getCentre(), juce::Colours::transparentWhite,
                                                 { spot.getRight(), spot.getCentreY() }, true));
        g.fillEllipse (spot);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawLine (x + screenRadius, y + h - 1.0f, x + w - screenRadius, y + h - 1.0f, 1.0f);
    }

    // Pantalla CRT: líneas de barrido cada 3 px y viñeta oscura en los bordes
    void paintCrt (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        for (auto y = r.getY(); y < r.getBottom(); y += 3.0f)
            g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                                 juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(), true));
        g.fillRoundedRectangle (r, 6.0f);
    }

    static constexpr int historySize = 2048, displaySize = 1024, fftOrder = 11, fftSize = 1 << fftOrder;
    static constexpr int numBars = 48;

    void timerCallback() override
    {
        // Últimas historySize muestras de cada canal, de la más antigua a la más reciente
        const auto end = processor.scopeWritePos.load (std::memory_order_acquire);
        constexpr auto mask = KoniSynthProcessor::scopeSize - 1;
        for (int i = 0; i < historySize; ++i)
        {
            const auto idx = (size_t) ((end - historySize + i) & mask);
            left[(size_t) i] = processor.scopeLeft[idx].load (std::memory_order_relaxed);
            right[(size_t) i] = processor.scopeRight[idx].load (std::memory_order_relaxed);
        }
        if (mode == spectrum || mode == spectrogram || mode == arcade || mode == demo)
            computeSpectrum();
        if (mode == demo) // matriz de 24 filas; columnas según el ancho, con puntos cuadrados
        {
            const auto screen = (bezel != bezelNone ? bezelScreen (getLocalBounds().toFloat()) : getLocalBounds().toFloat()).reduced (6.0f)
                                    .withTrimmedLeft (annunciatorSpace());
            int highest = -1; // nota más aguda pulsada (teclado en pantalla, host o controlador)
            for (int n = 127; n >= 0 && highest < 0; --n)
                if (processor.keyboardState.isNoteOnForChannels (0xffff, n))
                    highest = n;
            brandDemo.update (bars, highest, 1.0f / 30.0f,
                              (int) (screen.getWidth() / juce::jmax (1.0f, screen.getHeight() / (float) BrandDemo::rows)));
        }
        if (mode == arcade) // lienzo de juego: píxel de 2-4 px de pantalla según el alto (unos 35 píxeles de juego de alto)
        {
            const auto screen = (bezel != bezelNone ? bezelScreen (getLocalBounds().toFloat()) : getLocalBounds().toFloat()).reduced (6.0f)
                                    .withTrimmedLeft (annunciatorSpace()); // a la derecha de los anunciadores
            const auto px = juce::jmax (2, (int) (screen.getHeight() / 34.0f));
            game.update (bars, 1.0f / 30.0f, juce::jmax (16, (int) (screen.getWidth() / (float) px)), juce::jmax (8, (int) (screen.getHeight() / (float) px)));
        }
        if (mode == meters)
            updateMeters();
        if (mode == spectrogram)
            addSpectrogramColumn();
        repaint();
    }

    //--------------------------------------------------------------------------------------------------------------------------
    void paintScope (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // Disparo en el último cruce ascendente que deja displaySize muestras por delante; autoescala al pico de la ventana
        std::array<float, historySize> mono {};
        for (int i = 0; i < historySize; ++i) mono[(size_t) i] = 0.5f * (left[(size_t) i] + right[(size_t) i]);
        int start = historySize - displaySize;
        for (int i = historySize - displaySize; i > 0; --i)
            if (mono[(size_t) i - 1] <= 0.0f && mono[(size_t) i] > 0.0f) { start = i; break; }
        float peak = 0.0f;
        for (int i = 0; i < displaySize; ++i) peak = juce::jmax (peak, std::abs (mono[(size_t) (start + i)]));
        const auto scale = 0.9f / juce::jmax (peak, 0.01f);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        juce::Path path;
        for (int i = 0; i < displaySize; ++i)
        {
            const auto x = r.getX() + r.getWidth() * (float) i / (float) (displaySize - 1);
            const auto y = r.getCentreY() - mono[(size_t) (start + i)] * scale * r.getHeight() * 0.5f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        glowStroke (g, path, 1.5f);
    }

    // FFT de 2048 puntos con ventana de Hann sobre (L+R)/2; magnitudes en dB relativas a una senoidal a fondo de escala
    void computeSpectrum()
    {
        std::array<float, 2 * fftSize> buf {};
        for (int i = 0; i < fftSize; ++i)
        {
            const auto w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
            buf[(size_t) i] = 0.5f * (left[(size_t) i] + right[(size_t) i]) * w;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < fftSize / 2; ++k)
            magDb[(size_t) k] = juce::Decibels::gainToDecibels (buf[(size_t) k] / (fftSize * 0.25f), -120.0f); // Hann: ganancia 0,5
        // Barras logarítmicas 20 Hz - 20 kHz con caída de 24 dB/s y marca de pico que cae a 12 dB/s
        const auto sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        for (int b = 0; b < numBars; ++b)
        {
            const auto f0 = 20.0 * std::pow (1000.0, (double) b / numBars), f1 = 20.0 * std::pow (1000.0, (double) (b + 1) / numBars);
            const auto k0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 / sr * fftSize)), k1 = juce::jlimit (k0, fftSize / 2 - 1, (int) (f1 / sr * fftSize));
            float v = -120.0f;
            for (int k = k0; k <= k1; ++k) v = juce::jmax (v, magDb[(size_t) k]);
            bars[(size_t) b] = juce::jmax (v, bars[(size_t) b] - 24.0f / 30.0f);
            peaks[(size_t) b] = juce::jmax (bars[(size_t) b], peaks[(size_t) b] - 12.0f / 30.0f);
        }
    }

    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto w = r.getWidth() / numBars;
        const auto toY = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f); }; // -72..0 dB
        for (int b = 0; b < numBars; ++b)
        {
            const auto x = r.getX() + b * w;
            const auto y = toY (bars[(size_t) b]);
            g.setGradientFill (juce::ColourGradient (inkHot(), x, r.getY(), ink(), x, r.getBottom(), false));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, y, w - 2.0f, r.getBottom() - y));
            g.setColour (style == 0 ? juce::Colours::white.withAlpha (0.8f) : inkHot());
            g.fillRect (juce::Rectangle<float> (x + 1.0f, toY (peaks[(size_t) b]) - 1.0f, w - 2.0f, 2.0f));
        }
    }

    // Columna nueva a la derecha: color = nivel (-96..0 dB) en escala logarítmica de frecuencia
    void addSpectrogramColumn()
    {
        const auto h = juce::jmax (1, getHeight() - 12), w = juce::jmax (1, getWidth() - 12);
        if (spectroImage.isNull() || spectroImage.getWidth() != w || spectroImage.getHeight() != h)
            spectroImage = juce::Image (juce::Image::RGB, w, h, true);
        spectroImage.moveImageSection (0, 0, 2, 0, w - 2, h);
        const auto sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        for (int y = 0; y < h; ++y)
        {
            const auto f = 20.0 * std::pow (1000.0, 1.0 - (double) y / h);
            const auto k = juce::jlimit (1, fftSize / 2 - 1, (int) (f / sr * fftSize));
            const auto t = juce::jlimit (0.0f, 1.0f, (magDb[(size_t) k] + 96.0f) / 96.0f);
            const auto c = style != 0 ? juce::Colours::black.interpolatedWith (ink(), t).interpolatedWith (inkHot(), juce::jmax (0.0f, t - 0.8f) * 5.0f) // monocromo
                         : t < 0.33f ? juce::Colour (0xff000000).interpolatedWith (juce::Colour (0xff1040c0), t / 0.33f)
                         : t < 0.66f ? juce::Colour (0xff1040c0).interpolatedWith (juce::Colour (0xff40d0e0), (t - 0.33f) / 0.33f)
                                     : juce::Colour (0xff40d0e0).interpolatedWith (juce::Colour (0xffffff60), (t - 0.66f) / 0.34f);
            spectroImage.setPixelAt (w - 2, y, c);
            spectroImage.setPixelAt (w - 1, y, c);
        }
    }

    void paintSpectrogram (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! spectroImage.isNull())
        {
            g.setOpacity (1.0f); // drawImage usa la opacidad del último color fijado (p. ej. la de un anunciador apagado)
            g.drawImage (spectroImage, r);
        }
    }

    // Goniómetro: M = (L+R)/√2 en vertical, S = (L-R)/√2 en horizontal; barra de correlación (-1 fase invertida, +1 mono)
    void paintGoniometer (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto bar = r.removeFromBottom (8.0f);
        const auto side = juce::jmin (r.getWidth(), r.getHeight());
        const auto box = r.withSizeKeepingCentre (side, side);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawLine (box.getCentreX(), box.getY(), box.getCentreX(), box.getBottom());
        g.drawLine (box.getX(), box.getCentreY(), box.getRight(), box.getCentreY());
        float peak = 0.01f;
        double sll = 0, srr = 0, slr = 0;
        for (int i = historySize - displaySize; i < historySize; ++i)
        {
            const auto l = left[(size_t) i], rr = right[(size_t) i];
            peak = juce::jmax (peak, std::abs (l), std::abs (rr));
            sll += l * l; srr += rr * rr; slr += l * rr;
        }
        const auto scale = 0.45f * side / (peak * juce::MathConstants<float>::sqrt2);
        g.setColour (ink().withAlpha (0.55f));
        for (int i = historySize - displaySize; i < historySize; ++i)
        {
            const auto l = left[(size_t) i], rr = right[(size_t) i];
            const auto x = box.getCentreX() + (l - rr) * scale, y = box.getCentreY() - (l + rr) * scale;
            g.fillRect (x, y, 1.5f, 1.5f);
        }
        const auto corr = (sll > 1e-12 && srr > 1e-12) ? (float) (slr / std::sqrt (sll * srr)) : 1.0f;
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRect (bar);
        g.setColour (style != 0 ? (corr < 0.0f ? inkHot() : ink()) : corr < 0.0f ? juce::Colour (0xffff3030) : juce::Colour (0xff40d070));
        const auto cx = bar.getCentreX(), x1 = cx + corr * bar.getWidth() * 0.5f;
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x1), bar.getY(), juce::jmax (cx, x1), bar.getBottom()));
    }

    // Vúmetros: RMS con integración de 300 ms (balística VU) y 0 VU = -18 dBFS (convención de alineación EBU R68); LED de pico
    void updateMeters()
    {
        const auto coef = std::exp (-1.0f / (0.3f * 30.0f)); // 300 ms a 30 fps
        for (int c = 0; c < 2; ++c)
        {
            const auto& x = c == 0 ? left : right;
            double s = 0; float pk = 0;
            for (int i = historySize - 1600; i < historySize; ++i) { s += x[(size_t) i] * x[(size_t) i]; pk = juce::jmax (pk, std::abs (x[(size_t) i])); }
            const auto rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (s / 1600.0), -100.0f);
            vu[(size_t) c] = coef * vu[(size_t) c] + (1.0f - coef) * (rmsDb + 18.0f); // en VU
            peakHold[(size_t) c] = pk > 0.89f ? 30 : juce::jmax (0, peakHold[(size_t) c] - 1); // > -1 dBFS: LED 1 s
        }
    }

    void paintMeters (juce::Graphics& g, juce::Rectangle<float> r)
    {
        for (int c = 0; c < 2; ++c)
        {
            auto m = (c == 0 ? r.removeFromLeft (r.getWidth() * 0.5f) : r).reduced (6.0f, 2.0f);
            const auto face = style == 0 ? juce::Colour (0xfff0e6c8) : ink().withMultipliedBrightness (0.12f);
            const auto needle = style == 0 ? juce::Colours::black : ink();
            g.setColour (face);
            g.fillRoundedRectangle (m, 4.0f);
            // Geometría limitada por ancho y alto: el arco (±0,75 rad) cabe en la esfera con cualquier proporción de ventana
            const auto radius = juce::jmin (m.getWidth() * 0.5f / std::sin (0.75f) * 0.9f, m.getHeight() * 1.25f);
            const auto pivot = juce::Point<float> (m.getCentreX(), m.getY() + m.getHeight() * 0.12f + radius);
            const auto angleFor = [] (float vuValue) { return juce::jmap (juce::jlimit (-20.0f, 3.0f, vuValue), -20.0f, 3.0f, -0.75f, 0.75f); };
            g.setFont (juce::FontOptions (juce::jlimit (10.0f, 22.0f, m.getHeight() * 0.12f)));
            for (const auto mark : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, 0.0f, 3.0f })
            {
                const auto a = angleFor (mark);
                const auto p1 = pivot.getPointOnCircumference (radius * 0.92f, a), p2 = pivot.getPointOnCircumference (radius, a);
                g.setColour (mark > 0.0f ? (style == 0 ? juce::Colour (0xffc02020) : inkHot()) : needle);
                g.drawLine ({ p1, p2 }, 1.2f);
            }
            const auto a = angleFor (vu[(size_t) c]);
            g.setColour (needle);
            // Aguja: de la parte visible de la esfera al arco
            g.drawLine ({ pivot.getPointOnCircumference (juce::jmax (0.0f, radius - (m.getBottom() - m.getY()) * 0.85f), a),
                          pivot.getPointOnCircumference (radius * 0.98f, a) }, juce::jlimit (1.6f, 3.0f, m.getHeight() * 0.012f));
            g.setColour (needle.withAlpha (0.7f));
            g.drawText (c == 0 ? "L   VU" : "R   VU", m.reduced (6.0f, 3.0f), juce::Justification::bottomLeft);
            // LED de pico (rojo encendido/apagado, como el resto)
            const auto ledSize = juce::jlimit (9.0f, 18.0f, m.getHeight() * 0.08f);
            const auto led = juce::Rectangle<float> (ledSize, ledSize).withPosition (m.getRight() - ledSize - 6.0f, m.getY() + 5.0f);
            g.setColour (peakHold[(size_t) c] > 0 ? juce::Colour (0xffff3030) : juce::Colour (0xff5a1e1e));
            g.fillEllipse (led);
        }
    }

    KoniSynthProcessor& processor;
    std::function<void()> detach;
    int mode = scope, style = 0, bezel = bezelAuto;
    ArcadeGame game;
    BrandDemo brandDemo;
    std::array<float, historySize> left {}, right {};
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize / 2> magDb {};
    std::array<float, numBars> bars {}, peaks {};
    juce::Image spectroImage;
    std::array<float, 2> vu { -20.0f, -20.0f };
    std::array<int, 2> peakHold {};
};

// Ventana propia del display (Open in its own window): redimensionable; doble clic = pantalla completa
struct VisualizerWindow : PinnableWindow
{
    VisualizerWindow (KoniSynthProcessor& p, int mode, int style, int bezelMode, std::function<void()> onClose)
        : PinnableWindow ("KoniSynth Display", juce::Colour (0xff1b1f24), juce::DocumentWindow::allButtons), view (p), closed (std::move (onClose))
    {
        view.setMode (mode);
        view.setStyle (style);
        view.setBezel (bezelMode);
        view.setSize (900, 360);
        setContentNonOwned (&view, true);
        setResizable (true, true);
        setResizeLimits (300, 140, 8000, 5000);
        centreWithSize (getWidth(), getHeight());
        view.addMouseListener (this, false);
        setVisible (true);
    }
    ~VisualizerWindow() override { view.removeMouseListener (this); }
    void closeButtonPressed() override { if (closed) closed(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { setFullScreen (! isFullScreen()); }
    Visualizer view;
    std::function<void()> closed;
};

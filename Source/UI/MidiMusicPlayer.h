// Se incluye desde KoniSynth.cpp (mismo TU), tras TransportPanel.h; usa KoniSynthProcessor, SessionTransport, PlayAlongSong,
// Skin y Settings/Config.h. No es una cabecera independiente.
#pragma once

//==============================================================================
// MIDI Music Player: reproductor flotante del .mid del transporte, con aspecto de reproductor de PC de los 90 y los colores
// del skin (Skin::panelFace, accent, trace, ramp y el material de los botones, como las teclas y los faders).
// - Ventana propia sin barra nativa (windowIsTemporary: no sale en la barra de tareas y al pulsarla no desactiva la
//   principal). Encima de las demás mientras la aplicación está en primer plano. Arrastrada a menos de snapPx de un borde
//   exterior de la ventana principal, se imanta y desde entonces se mueve con ella; siempre entera dentro de la pantalla.
// - Tres alturas: completa, con la lista de partes (PL) y en una línea (doble clic en el título).
// - Volumen: ganancia de toda la salida (KoniSynthProcessor::playerVolume), curva cuadrática (71 % = -6 dB).
// - Tempo del .mid de 25 a 150 %, con imán en el 100 % (±4 %); el LCD muestra el BPM del instante (mapa de tempo × tempo).
// - Bucle A-B: SET A y SET B en la posición actual, CLEAR lo quita.
// - Lista de partes: "All tracks" (suena todo) o una pista lógica, que pasa a ser tuya (Transport/PlayAlong.h); en cada
//   fila, M (mute) y S (solo).
// - GM>MT-32: convierte un .mid de General MIDI para el MT-32 (SessionTransport::setGmToMt32); el transporte lo activa al
//   cargar un fichero sin SysEx del MT-32.
// - Multitimbral: con un .mid de varios canales y un motor que puede tocarlos (MT-32, Gravis, OPL3, caja de ritmos), un chip avisa si
//   su interruptor (Multitimbral, o Ch 10 en la caja de ritmos) está apagado y lo enciende con un clic (un paso de deshacer). No se
//   enciende solo: el ajuste es del preset.
// - Posición, imán, línea, lista, volumen y los 8 ficheros recientes se guardan en config.settings (claves midiPlayer*).
// Todo pintado a mano (sin ListBox, cuyo paint recibe filas fuera de rango); tooltips propios por zona.
class MidiMusicPlayer : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    std::function<void()> onPartSelected, onClose;
    std::function<juce::Rectangle<int>()> mainBounds; // ventana principal en coordenadas de pantalla

    explicit MidiMusicPlayer (KoniSynthProcessor& p) : koni (p), transport (p.sessionTransport)
    {
        setOpaque (true);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        if (auto cfg = openConfig())
        {
            shade = cfg->getBoolValue ("midiPlayerShade", false);
            showList = cfg->getBoolValue ("midiPlayerList", true);
            visibleRows = juce::jlimit (minRows, maxRows, cfg->getIntValue ("midiPlayerRows", 5));
        }
        setSize (width, heightFor());
        startTimerHz (30);
    }

    ~MidiMusicPlayer() override { savePlacement(); }

    // Tras addToDesktop: posición guardada (imantada: relativa a la ventana principal). Siempre entera dentro de la pantalla
    // de la principal: si no cabe donde estaba (p. ej. imantada a la derecha y ahora la principal está maximizada), se pega al
    // primer lado libre (derecha, izquierda, abajo, arriba) o, si no hay ninguno, flota dentro en su esquina superior derecha
    void restorePlacement()
    {
        const auto main = mainBounds ? mainBounds() : juce::Rectangle<int> (100, 100, 800, 600);
        auto cfg = openConfig();
        juce::Point<int> at { main.getRight(), main.getY() };
        isDocked = true;
        if (cfg != nullptr && cfg->containsKey ("midiPlayerX"))
        {
            isDocked = cfg->getBoolValue ("midiPlayerDocked", true);
            at = isDocked ? main.getPosition() + juce::Point<int> (cfg->getIntValue ("midiPlayerDockX"), cfg->getIntValue ("midiPlayerDockY"))
                          : juce::Point<int> (cfg->getIntValue ("midiPlayerX"), cfg->getIntValue ("midiPlayerY"));
        }
        at = fitOnScreen (main, at);
        dockOffset = at - main.getPosition();
        setTopLeftPosition (at);
        lastMain = main;
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const auto pal = palette();
        const auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (pal.top, 0.0f, 0.0f, pal.bottom, 0.0f, r.getBottom(), false));
        g.fillRect (r);
        if (Skin::current() == Skin::metal) // aluminio cepillado: vetas verticales tenues
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            for (int x = 0; x < getWidth(); x += 3) g.drawVerticalLine (x, 0.0f, r.getBottom());
        }
        if (shade)
            paintShade (g, pal);
        else
        {
            paintTitle (g, pal);
            paintTime (g, pal);
            paintMarquee (g, pal);
            paintChips (g, pal);
            paintSliderRow (g, pal, volumeRow, "VOL", volumePosition(), juce::String (juce::roundToInt (volumePosition() * 100.0f)) + " %", false);
            paintSliderRow (g, pal, tempoRow, "TEMPO", (transport.getTempoScale() - minTempo) / (maxTempo - minTempo),
                            juce::String (juce::roundToInt (transport.getTempoScale() * 100.0f)) + " %", true);
            paintSeek (g, pal);
            for (const auto& b : buttons)
                paintButton (g, pal, b);
            if (showList)
            {
                paintList (g, pal);
                // Agarre: tres trazos diagonales en la esquina
                const auto grip = gripArea().toFloat().reduced (2.0f);
                g.setColour (pal.sub);
                for (int k = 1; k <= 3; ++k)
                {
                    const auto d = (float) k * grip.getWidth() / 3.0f;
                    g.drawLine (grip.getRight() - d, grip.getBottom(), grip.getRight(), grip.getBottom() - d, 1.0f);
                }
            }
        }
        g.setColour (pal.line);
        g.drawRect (r, 1.0f);
    }

    void resized() override
    {
        buttons.clear();
        auto r = getLocalBounds();
        if (shade)
        {
            titleArea = r;
            auto right = r.reduced (3, 2);
            closeBox = right.removeFromRight (12);
            right.removeFromRight (4);
            shadeBox = right.removeFromRight (12);
            right.removeFromRight (8);
            for (const auto id : { bStop, bPause, bPlay })
                buttons.push_back ({ id, right.removeFromRight (20).reduced (1, 0) });
            return;
        }
        titleArea = r.removeFromTop (titleH);
        {
            auto t = titleArea.reduced (4, 3);
            closeBox = t.removeFromRight (12);
            t.removeFromRight (4);
            shadeBox = t.removeFromRight (12);
        }
        auto body = r.removeFromTop (mainH - titleH).reduced (pad);
        auto top = body.removeFromTop (timeH);
        timeArea = top.removeFromLeft (164);
        top.removeFromLeft (pad);
        marqueeArea = top.removeFromTop (17);
        top.removeFromTop (4);
        chipsArea = top.removeFromTop (14);
        top.removeFromTop (5);
        volumeRow = top.removeFromTop (14);
        top.removeFromTop (5);
        tempoRow = top.removeFromTop (14);
        body.removeFromTop (pad);
        seekArea = body.removeFromTop (seekH);
        body.removeFromTop (pad);
        auto row = body.removeFromTop (btnH);
        for (const auto id : { bPlay, bPause, bStop, bEject })
            buttons.push_back ({ id, row.removeFromLeft (34).withTrimmedRight (3) });
        row.removeFromLeft (8);
        buttons.push_back ({ bModePlay, row.removeFromLeft (54).withTrimmedRight (3) });
        buttons.push_back ({ bModeWait, row.removeFromLeft (54).withTrimmedRight (3) });
        buttons.push_back ({ bModeFollow, row.removeFromLeft (66).withTrimmedRight (3) });
        buttons.push_back ({ bList, row.removeFromRight (34) });
        row.removeFromRight (6);
        buttons.push_back ({ bClear, row.removeFromRight (48) });
        row.removeFromRight (3);
        buttons.push_back ({ bSetB, row.removeFromRight (46) });
        row.removeFromRight (3);
        buttons.push_back ({ bSetA, row.removeFromRight (46) });
        if (showList)
        {
            auto list = r.reduced (pad, 0).withTrimmedBottom (pad);
            listHeader = list.removeFromTop (18);
            buttons.push_back ({ bRecent, listHeader.removeFromRight (64).reduced (0, 2) });
            listHeader.removeFromRight (4);
            buttons.push_back ({ bGm, listHeader.removeFromRight (74).reduced (0, 2) });
            listArea = list;
        }
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        dragging = none;
        if (closeBox.expanded (2).contains (p)) { pressed = -2; return; }
        if (shadeBox.expanded (2).contains (p)) { pressed = -3; return; }
        if (gripArea().contains (p)) { dragging = resizeList; dragRows = visibleRows; return; }
        if (! shade && multiChip.contains (p)) { turnOnMulti(); return; }
        for (const auto& b : buttons)
            if (b.area.contains (p)) { pressed = b.id; repaint(); return; }
        if (! shade && sliderTrack (volumeRow).expanded (0, 4).contains (p)) { dragging = volume; mouseDrag (e); return; }
        if (! shade && sliderTrack (tempoRow).expanded (0, 4).contains (p)) { dragging = tempo; mouseDrag (e); return; }
        if (! shade && seekArea.contains (p) && song != nullptr) { dragging = seek; mouseDrag (e); return; }
        if (! shade && showList && listArea.contains (p)) { clickRow (p); return; }
        dragging = window;
        dragStart = getScreenPosition();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto along = [&e] (juce::Rectangle<int> track)
        { return juce::jlimit (0.0f, 1.0f, (float) (e.x - track.getX()) / (float) juce::jmax (1, track.getWidth())); };
        if (dragging == volume)
            setVolumePosition (along (sliderTrack (volumeRow)));
        else if (dragging == tempo)
        {
            auto t = std::round ((minTempo + (maxTempo - minTempo) * along (sliderTrack (tempoRow))) * 100.0f) / 100.0f;
            if (std::abs (t - 1.0f) <= 0.04f) t = 1.0f; // imán en el 100 %
            transport.setTempoScale (t);
        }
        else if (dragging == seek)
            seekPreview = along (seekArea);
        else if (dragging == window)
            moveTo (dragStart + e.getOffsetFromDragStart());
        else if (dragging == resizeList)
        {
            // Filas enteras, de 3 a 30, sin pasar del borde inferior de la pantalla
            auto rows = juce::jlimit (minRows, maxRows, dragRows + juce::roundToInt ((float) e.getDistanceFromDragStartY() / (float) rowH));
            if (mainBounds)
                while (rows > minRows && getScreenY() + mainH + 18 + rowH * rows + pad > screenArea (mainBounds()).getBottom()) --rows;
            if (rows != visibleRows)
            {
                visibleRows = rows;
                if (song != nullptr)
                    listScroll = juce::jlimit (0, juce::jmax (0, (int) song->tracks.size() + 1 - visibleRows), listScroll);
                setSize (width, heightFor());
            }
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging == seek && seekPreview >= 0.0 && song != nullptr)
            transport.seekMidi (seekPreview * song->length);
        if (dragging == window || dragging == resizeList)
            savePlacement();
        seekPreview = -1.0;
        dragging = none;
        const auto p = e.getPosition();
        const auto id = pressed;
        pressed = 0;
        if (id == -2 && closeBox.expanded (2).contains (p)) { if (onClose) onClose(); return; }
        if (id == -3 && shadeBox.expanded (2).contains (p)) { toggleShade(); return; }
        for (const auto& b : buttons)
            if (b.id == id && b.area.contains (p))
                press (id);
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (gripArea().contains (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (! shade && sliderTrack (tempoRow).expanded (0, 4).contains (p))      transport.setTempoScale (1.0f);
        else if (! shade && sliderTrack (volumeRow).expanded (0, 4).contains (p)) setVolumePosition (1.0f);
        else if (titleArea.contains (p) && ! closeBox.expanded (2).contains (p) && ! shadeBox.expanded (2).contains (p)) toggleShade();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (showList && ! shade && listArea.contains (e.getPosition()) && song != nullptr)
        {
            const auto rows = (int) song->tracks.size() + 1;
            listScroll = juce::jlimit (0, juce::jmax (0, rows - visibleRows), listScroll + (w.deltaY < 0 ? 1 : -1));
            repaint();
        }
    }

    // Tooltip de la zona bajo el ratón
    juce::String getTooltip() override
    {
        const auto p = getMouseXYRelative();
        for (const auto& b : buttons)
            if (b.area.contains (p))
                switch (b.id)
                {
                    case bPlay:  return "Play the MIDI file.";
                    case bPause: return "Pause.";
                    case bStop:  return "Stop and go back to the start (with the count-in when you play a part).";
                    case bEject: return "Load a MIDI file (.mid).";
                    case bModePlay:   return "Play: the file keeps its tempo; notes of your part you miss turn red.";
                    case bModeWait:   return "Wait: the file stops at each note or chord of your part until you play it.";
                    case bModeFollow: return "Follow: like Wait, and if you play ahead the file jumps with you.";
                    case bSetA:  return "Loop start: the current position.";
                    case bSetB:  return "Loop end: the current position. Playback jumps back to A when it gets here.";
                    case bClear: return "Remove the loop.";
                    case bList:  return "Show or hide the list of parts.";
                    case bRecent: return "The last 8 MIDI files you opened.";
                    case bGm:    return "GM to MT-32: plays a General MIDI file on the Roland MT-32 engine (turn its Multitimbral on). Programs are "
                                        "mapped to MT-32 patches, panning is flipped and the MT-32 is set up like a GM module (parts on "
                                        "channels 1-8, rhythm on 10; channels 9 and 11-16 stay silent). On by default for files without "
                                        "MT-32 SysEx.";
                    default: break;
                }
        if (! shade && multiChip.contains (p))
            return "This MIDI file uses " + juce::String (songChannels()) + " channels, but the engine plays them as one part "
                   "(notes of different channels collide and drums play as the melody sound). Click to turn on "
                   + juce::String (multiSwitch().label) + (koniEngine() == 13 && (int) koni.apvts.getRawParameterValue ("oplModel")->load() != 1
                                                           ? " (this also switches the OPL model to OPL3, the only one with it)." : ".");
        if (closeBox.expanded (2).contains (p)) return "Close the player.";
        if (gripArea().contains (p)) return "Drag up or down to show fewer or more parts.";
        if (shadeBox.expanded (2).contains (p)) return shade ? "Full player." : "One-line player (also double-click the title).";
        if (! shade && sliderTrack (volumeRow).expanded (0, 4).contains (p))
            return "Volume of the whole KoniSynth output (100 % = unchanged, 71 % = -6 dB). Double-click: 100 %.";
        if (! shade && sliderTrack (tempoRow).expanded (0, 4).contains (p))
            return "Tempo of the MIDI file, 25 to 150 %; it sticks at 100 %. Double-click: 100 %.";
        if (! shade && seekArea.contains (p)) return "Position: drag to jump. The shaded zone is the loop.";
        if (! shade && showList && listArea.contains (p))
        {
            if (const auto box = rowBoxAt (p); box == 1) return "Mute this track.";
            else if (box == 2) return "Solo: only the tracks in solo play (and your part, which you play).";
            return "All tracks: listen to the whole file. A track: it becomes your part (muted; your notes go to its channel and the display follows it).";
        }
        return {};
    }

private:
    // Interruptor multitímbrico del motor elegido: parámetro, si está encendido, textos del chip
    struct MultiSwitch { const char* id = nullptr; bool on = false; const char* onText = ""; const char* offText = ""; const char* label = ""; };
    int koniEngine() const { return juce::roundToInt (koni.apvts.getRawParameterValue ("engine")->load()); }
    MultiSwitch multiSwitch() const
    {
        const auto on = [this] (const char* id) { return koni.apvts.getRawParameterValue (id)->load() > 0.5f; };
        switch (koniEngine())
        {
            case 14: return { "mt32Multi", on ("mt32Multi"), "MT-32 MULTITIMBRAL", "MT-32 SINGLE", "MULTITIMBRAL" };
            case 15: return { "gusMulti", on ("gusMulti"), "GUS MULTITIMBRAL", "GUS SINGLE", "MULTITIMBRAL" };
            case 13: return { "oplGm", on ("oplGm") && (int) koni.apvts.getRawParameterValue ("oplModel")->load() == 1, "OPL MULTITIMBRAL", "OPL SINGLE", "MULTITIMBRAL" };
            case 5:  return { "drumCh10", on ("drumCh10"), "DRUMS CH 10", "DRUMS ALL CH", "CH 10" };
            default: return {};
        }
    }
    // Canales MIDI distintos que usa el .mid
    int songChannels() const
    {
        if (song == nullptr) return 0;
        std::bitset<17> used;
        for (const auto& t : song->tracks) used.set ((size_t) juce::jlimit (1, 16, t.channel));
        return (int) used.count();
    }
    // Enciende el interruptor (un paso de deshacer). OPL: Multitimbral solo existe en el OPL3, así que también pasa a ese modelo
    void turnOnMulti()
    {
        const auto sw = multiSwitch();
        if (sw.id == nullptr) return;
        const auto set = [this] (const char* id, float real)
        {
            if (auto* p = koni.apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (real));
                p->endChangeGesture();
            }
        };
        koni.undoHistory.beginGroup();
        if (koniEngine() == 13) set ("oplModel", 1.0f);
        set (sw.id, 1.0f);
        koni.undoHistory.endGroup();
        repaint();
    }
    mutable juce::Rectangle<int> multiChip;
    mutable int multiChipIndex = -1;

    enum ButtonId { bPlay = 1, bPause, bStop, bEject, bModePlay, bModeWait, bModeFollow, bSetA, bSetB, bClear, bList, bRecent, bGm };
    struct Button { int id; juce::Rectangle<int> area; };
    enum Drag { none, window, volume, tempo, seek, resizeList };

    static constexpr int width = 510, titleH = 18, pad = 6, timeH = 70, seekH = 12, btnH = 26;
    static constexpr int mainH = titleH + pad + timeH + pad + seekH + pad + btnH + pad; // 150 px
    static constexpr int rowH = 18, shadeH = 18, minRows = 3, maxRows = 30, gripSize = 12;
    int visibleRows = 5; // filas de la lista: se cambia con el agarre de la esquina (3-30) y se guarda (midiPlayerRows)
    int listH() const { return 18 + rowH * visibleRows + pad; }
    static constexpr int snapPx = 10;
    static constexpr float minTempo = 0.25f, maxTempo = 1.5f;

    int heightFor() const { return shade ? shadeH : mainH + (showList ? listH() : 0); }
    // Agarre de la esquina inferior derecha (con la lista a la vista): estira la lista en vertical
    juce::Rectangle<int> gripArea() const
    {
        return showList && ! shade ? getLocalBounds().removeFromBottom (gripSize).removeFromRight (gripSize) : juce::Rectangle<int>();
    }

    // Volumen: posición del mando p (0-1) → ganancia p² (cuadrática: 71 % ≈ -6 dB, 50 % = -12 dB)
    float volumePosition() const { return std::sqrt (juce::jlimit (0.0f, 1.0f, koni.playerVolume.load())); }
    void setVolumePosition (float p)
    {
        p = juce::jlimit (0.0f, 1.0f, p);
        koni.playerVolume.store (p * p);
        if (auto cfg = openConfig()) cfg->setValue ("midiPlayerVolume", (double) (p * p));
    }

    //==========================================================================
    juce::Rectangle<int> screenArea (juce::Rectangle<int> main) const
    {
        const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (main);
        return d != nullptr ? d->userBounds.getSmallestIntegerContainer() : main;
    }

    juce::Point<int> fitOnScreen (juce::Rectangle<int> main, juce::Point<int> at)
    {
        const auto area = screenArea (main);
        const auto w = getWidth(), h = getHeight();
        if (area.contains (juce::Rectangle<int> (at.x, at.y, w, h)))
            return at;
        const juce::Point<int> sides[4] = { { main.getRight(), main.getY() }, { main.getX() - w, main.getY() },
                                            { main.getX(), main.getBottom() }, { main.getX(), main.getY() - h } };
        for (const auto& c : sides)
            if (area.contains (juce::Rectangle<int> (c.x, c.y, w, h)))
            {
                isDocked = true;
                return c;
            }
        isDocked = false;
        return juce::Rectangle<int> (main.getRight() - w - 24, main.getY() + 40, w, h).constrainedWithin (area).getPosition();
    }

    //==========================================================================
    // Colores del skin
    struct Palette { juce::Colour top, bottom, accent, ink, dim, lcd, text, sub, line, title; };
    static Palette palette()
    {
        const auto face = Skin::panelFace();
        const auto ink = Skin::trace();
        const auto lcd = Skin::mono() ? Skin::ramp().bg : Skin::current() == Skin::vintage ? juce::Colour (0xff040a06) : juce::Colour (0xff03080c);
        const auto metal = Skin::current() == Skin::metal;
        return { face.top, face.bottom, Skin::accent(), ink, lcd.interpolatedWith (ink, 0.38f), lcd,
                 metal ? juce::Colour (0xffeef2f6) : Skin::c (0xffc9d1d9), metal ? juce::Colour (0xffc3ccd5) : Skin::c (0xff8b949e),
                 metal ? juce::Colour (0xff8a939d) : Skin::c (0xff4d5661), metal ? juce::Colour (0xffe3e9ef) : Skin::c (0xffaab4be) };
    }

    static void paintLcd (juce::Graphics& g, const Palette& pal, juce::Rectangle<float> r)
    {
        g.setColour (pal.lcd);
        g.fillRect (r);
        g.setColour (juce::Colours::black);
        g.drawRect (r, 1.0f);
        g.setColour (pal.dim.withAlpha (0.7f));
        g.drawRect (r.reduced (1.0f), 1.0f);
    }
    static void scanlines (juce::Graphics& g, juce::Rectangle<int> r)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (r);
        Skin::scanlines (g, r); // monocromos y Vintage 60s
    }
    static juce::String clockText (double pos)
    {
        const auto secs = (int) std::floor (std::abs (pos));
        return juce::String (pos < 0.0 ? "-" : "") + juce::String (secs / 60).paddedLeft ('0', 2) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
    }

    void paintWindowButtons (juce::Graphics& g, const Palette& pal) const
    {
        g.setColour (pal.line);
        g.drawRect (shadeBox.toFloat(), 1.0f);
        g.drawRect (closeBox.toFloat(), 1.0f);
        g.setColour (pal.title);
        if (shade) g.drawRect (shadeBox.toFloat().reduced (2.5f), 1.0f);                 // ampliar
        else g.drawHorizontalLine (shadeBox.getBottom() - 3, (float) shadeBox.getX() + 2.0f, (float) shadeBox.getRight() - 2.0f); // una línea
        const auto c = closeBox.toFloat().reduced (3.0f);
        g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.2f);
        g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.2f);
    }

    void paintTitle (juce::Graphics& g, const Palette& pal) const
    {
        const auto r = titleArea.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (auto y = r.getY() + 2.0f; y < r.getBottom() - 1.0f; y += 3.0f)
            g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);
        g.setColour (pal.title);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold).withKerningFactor (0.12f));
        g.drawText ("MIDI MUSIC PLAYER", r, juce::Justification::centred);
        paintWindowButtons (g, pal);
    }

    void paintTime (juce::Graphics& g, const Palette& pal)
    {
        const auto r = timeArea.toFloat();
        paintLcd (g, pal, r);
        const auto pos = currentSeconds();
        g.setColour (pal.ink);
        juce::Path state;
        const auto sx = r.getX() + 8.0f, sy = r.getY() + 13.0f;
        if (transport.isPlaying()) state.addTriangle (sx, sy, sx, sy + 12.0f, sx + 10.0f, sy + 6.0f);
        else { state.addRectangle (sx, sy, 3.5f, 12.0f); state.addRectangle (sx + 6.0f, sy, 3.5f, 12.0f); }
        g.fillPath (state);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 28.0f, juce::Font::bold));
        g.drawText (clockText (pos), juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 3.0f, 100.0f, 34.0f), juce::Justification::centredLeft);
        // Mini espectro de la salida: 10 bandas
        const auto bw = 3.0f, gap = 1.0f;
        auto x = r.getRight() - 6.0f - (float) numBands * (bw + gap);
        for (int b = 0; b < numBands; ++b, x += bw + gap)
        {
            const auto h = 28.0f * juce::jlimit (0.0f, 1.0f, (bands[(size_t) b] + 60.0f) / 60.0f);
            g.fillRect (juce::Rectangle<float> (x, r.getY() + 6.0f + 28.0f - h, bw, h));
        }
        // Estado: tu parte y el modo
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        const auto t = transport.getSelectedTrack();
        static const char* modes[3] = { "PLAY", "WAIT", "FOLLOW" };
        juce::String part = "ALL TRACKS";
        if (song != nullptr && t >= 0 && t < (int) song->tracks.size())
            part = (song->tracks[(size_t) t].drums ? juce::String ("DRUMS") : song->tracks[(size_t) t].name.toUpperCase().substring (0, 10))
                   + " ch" + juce::String (song->tracks[(size_t) t].channel);
        const auto mode = transport.isWaiting() ? juce::String ("WAITING") : juce::String (modes[juce::jlimit (0, 2, transport.getPracticeMode())]);
        g.setColour (pal.ink);
        g.drawText (part, juce::Rectangle<float> (r.getX() + 7.0f, r.getBottom() - 29.0f, r.getWidth() - 14.0f, 12.0f), juce::Justification::centredLeft, true);
        g.setColour (pal.dim.brighter (0.5f));
        g.drawText (mode, juce::Rectangle<float> (r.getX() + 7.0f, r.getBottom() - 16.0f, r.getWidth() - 14.0f, 12.0f), juce::Justification::centredLeft, true);
        scanlines (g, timeArea);
    }

    void paintMarquee (juce::Graphics& g, const Palette& pal)
    {
        paintLcd (g, pal, marqueeArea.toFloat());
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (marqueeArea.reduced (2, 1));
            const juce::Font font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
            g.setFont (font);
            g.setColour (pal.ink);
            const auto w = juce::GlyphArrangement::getStringWidth (font, marquee) + 40.0f;
            if (marqueeOffset > w) marqueeOffset -= w;
            const auto y = marqueeArea.toFloat();
            for (int k = 0; k < 2; ++k)
                g.drawText (marquee, juce::Rectangle<float> (y.getX() + 4.0f - marqueeOffset + (float) k * w, y.getY(), w, y.getHeight()), juce::Justification::centredLeft, false);
        }
        scanlines (g, marqueeArea);
    }

    void paintChips (juce::Graphics& g, const Palette& pal) const
    {
        juce::StringArray chips;
        const auto engine = juce::roundToInt (koni.apvts.getRawParameterValue ("engine")->load());
        if (song != nullptr)
            chips.add (juce::String (song->bpmAt (juce::jmax (0.0, currentSeconds())) * transport.getTempoScale(), 1) + " BPM");
        // Interruptor multitímbrico del motor: el chip avisa (con acento) si el .mid usa varios canales y está apagado; un clic lo
        // enciende (multiChip)
        const auto sw = multiSwitch();
        multiChipIndex = -1;
        if (sw.id == nullptr)
            chips.add ("ONE SOUND");
        else
        {
            if (! sw.on && songChannels() > 1) multiChipIndex = chips.size();
            chips.add (sw.on ? sw.onText : (songChannels() > 1 ? juce::String (sw.offText) + ": CLICK FOR " + sw.label : juce::String (sw.offText)));
        }
        if (engine == 14 && song != nullptr && transport.getGmToMt32())
        {
            int silent = 0; // con GM>MT-32 el MT-32 escucha los canales 1-8 y 10
            for (const auto& tr : song->tracks) silent += tr.channel == 9 || tr.channel > 10 ? 1 : 0;
            if (silent > 0) chips.add (juce::String (silent) + " SILENT");
        }
        if (song != nullptr && transport.getSelectedTrack() >= 0)
        {
            int hits = 0, done = 0;
            const auto t = transport.getSelectedTrack();
            for (int i = 0; i < song->numNotes (t); ++i)
                if (const auto st = song->statusOf (t, i); st != PlayAlongSong::pending) { ++done; hits += st == PlayAlongSong::hit ? 1 : 0; }
            chips.add ("HITS " + juce::String (hits) + "/" + juce::String (done));
        }
        if (loopActive()) chips.add ("LOOP " + clockText (transport.getLoopA()) + "-" + clockText (transport.getLoopB()));
        else if (loopStart >= 0.0) chips.add ("A " + clockText (loopStart));
        const juce::Font font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
        g.setFont (font);
        auto x = (float) chipsArea.getX();
        multiChip = {};
        for (int i = 0; i < chips.size(); ++i)
        {
            const auto& c = chips[i];
            const auto w = juce::GlyphArrangement::getStringWidth (font, c) + 8.0f;
            const auto box = juce::Rectangle<float> (x, (float) chipsArea.getY(), w, (float) chipsArea.getHeight());
            if (box.getRight() > (float) chipsArea.getRight()) break;
            const auto warn = i == multiChipIndex; // aviso: fondo de acento, se puede pulsar
            g.setColour (warn ? pal.accent : pal.lcd);
            g.fillRect (box);
            g.setColour (warn ? pal.accent.brighter (0.4f) : pal.dim);
            g.drawRect (box, 1.0f);
            g.setColour (warn ? juce::Colours::black : pal.ink);
            g.drawText (c, box, juce::Justification::centred);
            if (warn) multiChip = box.toNearestInt();
            x += w + 4.0f;
        }
    }

    // Fila de mando: rótulo, pista con relleno de acento y tirador, valor. Tempo: marca en el 100 %
    static juce::Rectangle<int> sliderTrack (juce::Rectangle<int> row) { return row.withTrimmedLeft (44).withTrimmedRight (42).reduced (0, 3); }
    void paintSliderRow (juce::Graphics& g, const Palette& pal, juce::Rectangle<int> row, const char* label, float value,
                         const juce::String& text, bool unityMark) const
    {
        g.setColour (pal.sub);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (label, row.withWidth (42), juce::Justification::centredLeft);
        g.drawText (text, row.withTrimmedLeft (row.getWidth() - 40), juce::Justification::centredRight);
        const auto track = sliderTrack (row).toFloat();
        if (unityMark)
        {
            const auto x = track.getX() + track.getWidth() * (1.0f - minTempo) / (maxTempo - minTempo);
            g.setColour (pal.sub.withAlpha (0.7f));
            g.drawVerticalLine (juce::roundToInt (x), track.getY() - 3.0f, track.getBottom() + 3.0f);
        }
        paintBar (g, pal, track, value, -1.0f, -1.0f, -1.0f);
    }

    // Barra con tirador del material del skin. a < 0: relleno de acento hasta el valor; b > a >= 0: zona del bucle sombreada;
    // solo a >= 0 (b < 0): marca A del bucle a medias
    void paintBar (juce::Graphics& g, const Palette& pal, juce::Rectangle<float> r, float value, float a, float b, float markA) const
    {
        g.setColour (pal.lcd);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        if (b > a && a >= 0.0f)
        {
            const auto zone = juce::Rectangle<float>::leftTopRightBottom (r.getX() + r.getWidth() * a, r.getY(), r.getX() + r.getWidth() * b, r.getBottom());
            g.setColour (pal.accent.withAlpha (0.25f));
            g.fillRect (zone);
            g.setColour (pal.accent);
            g.drawVerticalLine ((int) zone.getX(), r.getY() - 2.0f, r.getBottom() + 2.0f);
            g.drawVerticalLine ((int) zone.getRight(), r.getY() - 2.0f, r.getBottom() + 2.0f);
        }
        else if (a < 0.0f && markA < 0.0f)
        {
            g.setColour (pal.accent.withAlpha (0.85f));
            g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, value)), r.getHeight() * 0.5f);
        }
        if (markA >= 0.0f)
        {
            const auto x = r.getX() + r.getWidth() * markA;
            g.setColour (pal.accent);
            g.drawVerticalLine (juce::roundToInt (x), r.getY() - 3.0f, r.getBottom() + 3.0f);
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText ("A", juce::Rectangle<float> (x + 2.0f, r.getY() - 1.0f, 8.0f, r.getHeight() + 2.0f), juce::Justification::centredLeft);
        }
        const auto thumb = juce::Rectangle<float> (14.0f, r.getHeight() + 6.0f).withCentre ({ r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, value), r.getCentreY() });
        paintCap (g, thumb, false, false);
    }

    void paintSeek (juce::Graphics& g, const Palette& pal) const
    {
        const auto len = song != nullptr ? juce::jmax (0.001, song->length) : 1.0;
        const auto v = seekPreview >= 0.0 ? seekPreview : juce::jlimit (0.0, 1.0, juce::jmax (0.0, currentSeconds()) / len);
        const auto r = seekArea.toFloat().reduced (0.0f, 2.0f);
        if (loopActive())
            paintBar (g, pal, r, (float) v, (float) (transport.getLoopA() / len), (float) (transport.getLoopB() / len), -1.0f);
        else
            paintBar (g, pal, r, (float) v, 0.0f, 0.0f, loopStart >= 0.0 ? (float) (loopStart / len) : -1.0f);
    }

    // Tapa del material del skin (Skin::material), como las teclas de dos posiciones (Widgets.h, KeySwitch)
    static void paintCap (juce::Graphics& g, juce::Rectangle<float> r, bool over, bool down)
    {
        const auto mat = Skin::material();
        const auto topaz = mat == Skin::Material::topaz, bake = mat == Skin::Material::bakelite, light = mat == Skin::Material::aluminium;
        if (down) r = r.translated (0.0f, 1.0f);
        if (bake)       g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff47453f : 0xff3a3936), 0.0f, r.getY(), juce::Colour (0xff070707), 0.0f, r.getBottom(), false));
        else if (topaz) g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xffe4a448 : 0xffd08a30), 0.0f, r.getY(), juce::Colour (0xff3e1c04), 0.0f, r.getBottom(), false));
        else if (light) g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xffeef1f4 : 0xffdde1e5), 0.0f, r.getY(), juce::Colour (0xff7f868e), 0.0f, r.getBottom(), false));
        else            g.setGradientFill (juce::ColourGradient (Skin::c (over ? 0xff5a616b : 0xff515861), 0.0f, r.getY(), Skin::c (0xff23272d), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        if (topaz)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc45a).withAlpha (0.4f), r.getCentreX(), r.getBottom() - 2.0f,
                                                     juce::Colour (0xffffc45a).withAlpha (0.0f), r.getCentreX(), r.getCentreY() - 2.0f, false));
            g.fillRoundedRectangle (r, 3.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (light || topaz ? 0.5f : bake ? 0.28f : 0.16f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
        g.setColour (bake ? juce::Colour (0xff6a655d) : juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
    static juce::Colour capInk()
    {
        const auto mat = Skin::material();
        return mat == Skin::Material::aluminium ? juce::Colour (0xff1a1f24) : mat == Skin::Material::topaz ? juce::Colour (0xfffff0c8) : juce::Colour (0xffe6edf3);
    }

    bool loopActive() const { return transport.getLoopB() > transport.getLoopA() && transport.getLoopA() >= 0.0; }

    bool isOn (int id) const
    {
        switch (id)
        {
            case bModePlay:   return transport.getPracticeMode() == SessionTransport::practicePlay;
            case bModeWait:   return transport.getPracticeMode() == SessionTransport::practiceWait;
            case bModeFollow: return transport.getPracticeMode() == SessionTransport::practiceFollow;
            case bSetA:       return loopStart >= 0.0 || loopActive();
            case bSetB:       return loopActive();
            case bGm:         return transport.getGmToMt32();
            case bList:       return showList;
            case bPlay:       return transport.isPlaying();
            default:          return false;
        }
    }

    static void glyph (juce::Path& p, int id, juce::Point<float> c, float s)
    {
        switch (id)
        {
            case bPlay:  p.addTriangle (c.x - s * 0.4f, c.y - s * 0.55f, c.x - s * 0.4f, c.y + s * 0.55f, c.x + s * 0.55f, c.y); break;
            case bPause: p.addRectangle (c.x - s * 0.45f, c.y - s * 0.5f, s * 0.3f, s); p.addRectangle (c.x + s * 0.15f, c.y - s * 0.5f, s * 0.3f, s); break;
            case bStop:  p.addRectangle (c.x - s * 0.45f, c.y - s * 0.45f, s * 0.9f, s * 0.9f); break;
            case bEject: p.addTriangle (c.x - s * 0.55f, c.y + s * 0.1f, c.x + s * 0.55f, c.y + s * 0.1f, c.x, c.y - s * 0.55f);
                         p.addRectangle (c.x - s * 0.55f, c.y + s * 0.3f, s * 1.1f, s * 0.22f); break;
            default: break;
        }
    }

    void paintButton (juce::Graphics& g, const Palette& pal, const Button& b) const
    {
        const auto r = b.area.toFloat();
        const auto over = isMouseOver (true) && b.area.contains (getMouseXYRelative());
        const auto down = pressed == b.id;
        if (b.id == bGm) // interruptor: LED rojo y texto, sin tecla (como los interruptores de los bloques)
        {
            const auto lit = isOn (b.id);
            const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 6.0f, r.getCentreY() });
            if (lit) { g.setColour (juce::Colour (0xffff3030).withAlpha (0.35f)); g.fillEllipse (led.expanded (2.5f)); }
            g.setColour (lit ? juce::Colour (0xffff5a4a) : juce::Colour (0xff3a1414));
            g.fillEllipse (led);
            g.setColour (over ? pal.text : pal.sub);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("GM>MT-32", r.withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
            return;
        }
        paintCap (g, r, over, down);
        const auto on = isOn (b.id);
        const auto rr = down ? r.translated (0.0f, 1.0f) : r;
        if (on && (b.id == bModePlay || b.id == bModeWait || b.id == bModeFollow || b.id == bSetA || b.id == bSetB || b.id == bList || b.id == bGm)) // contorno de acento
        {
            g.setColour (pal.accent.withAlpha (0.9f));
            g.drawRoundedRectangle (rr.reduced (0.5f), 3.0f, 1.2f);
        }
        const auto ink = b.id == bPlay && on ? pal.accent : capInk();
        g.setColour (ink);
        juce::Path p;
        glyph (p, b.id, rr.getCentre(), juce::jmin (rr.getWidth(), rr.getHeight()) * 0.46f);
        if (! p.isEmpty())
        {
            g.fillPath (p);
            return;
        }
        auto text = rr;
        if (b.id >= bModePlay && b.id <= bModeFollow) // LED
        {
            const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ rr.getX() + 9.0f, rr.getCentreY() });
            if (on) { g.setColour (pal.accent.withAlpha (0.35f)); g.fillEllipse (led.expanded (2.5f)); }
            g.setColour (on ? pal.accent : juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (led);
            text = text.withTrimmedLeft (14.0f);
            g.setColour (ink);
        }
        const auto label = b.id == bModePlay ? "PLAY" : b.id == bModeWait ? "WAIT" : b.id == bModeFollow ? "FOLLOW" : b.id == bList ? "PL"
                         : b.id == bRecent ? "RECENT" : b.id == bGm ? "GM>MT-32" : b.id == bSetA ? "SET A" : b.id == bSetB ? "SET B" : "CLEAR";
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (label, text, juce::Justification::centred, false);
    }

    void paintList (juce::Graphics& g, const Palette& pal) const
    {
        g.setColour (pal.title);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold).withKerningFactor (0.1f));
        g.drawText ("PARTS - CLICK ONE TO PLAY IT YOURSELF", listHeader, juce::Justification::centredLeft);
        paintLcd (g, pal, listArea.toFloat());
        const juce::Font font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
        g.setFont (font);
        if (song == nullptr)
        {
            g.setColour (pal.dim.brighter (0.5f));
            g.drawText ("No MIDI file: press the eject button", listArea, juce::Justification::centred);
            scanlines (g, listArea);
            return;
        }
        const auto rows = (int) song->tracks.size() + 1;
        const auto sel = transport.getSelectedTrack() + 1;
        for (int k = 0; k < visibleRows; ++k)
        {
            const auto row = listScroll + k;
            if (row >= rows) break;
            const auto r = juce::Rectangle<int> (listArea.getX() + 2, listArea.getY() + 2 + k * rowH, listArea.getWidth() - 4, rowH);
            if (row == sel)
            {
                g.setColour (pal.accent.withAlpha (0.28f));
                g.fillRect (r);
            }
            juce::String left, right;
            if (row == 0)
                left = "0. All tracks (listen)";
            else
            {
                const auto t = row - 1;
                const auto& tr = song->tracks[(size_t) t];
                left << row << ". " << tr.name.substring (0, 12) << " - ch " << tr.channel << " - "
                     << (tr.drums ? juce::String ("drums") : tr.program >= 0 ? juce::String (PlayAlongSong::gmProgramName (tr.program)) : juce::String ("no program"));
                if (t == song->suggested) left << " *";
                right << song->numNotes (t);
                if (! tr.drums && song->numNotes (t) > 0)
                    right << " - " << juce::MidiMessage::getMidiNoteName (tr.low, true, true, 4) << "-" << juce::MidiMessage::getMidiNoteName (tr.high, true, true, 4);
            }
            const auto silenced = row > 0 && transport.isTrackSilenced (row - 1);
            g.setColour ((row == sel ? pal.text : pal.ink).withAlpha (silenced ? 0.4f : 1.0f));
            g.drawText (left, r.reduced (6, 0).withTrimmedRight (96 + boxesW), juce::Justification::centredLeft, true);
            g.setColour ((row == sel ? pal.sub : pal.dim.brighter (0.4f)).withAlpha (silenced ? 0.5f : 1.0f));
            g.drawText (right, r.reduced (6, 0).withTrimmedRight (boxesW), juce::Justification::centredRight, false);
            if (row > 0) // M y S
                for (int k2 = 1; k2 <= 2; ++k2)
                {
                    const auto box = rowBox (r, k2).toFloat();
                    const auto on = k2 == 1 ? transport.isTrackMuted (row - 1) : transport.isTrackSoloed (row - 1);
                    g.setColour (on ? pal.accent.withAlpha (0.85f) : pal.dim.withAlpha (0.5f));
                    if (on) g.fillRect (box); else g.drawRect (box, 1.0f);
                    g.setColour (on ? pal.lcd : pal.ink);
                    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                    g.drawText (k2 == 1 ? "M" : "S", box, juce::Justification::centred);
                    g.setFont (font);
                }
        }
        if (rows > visibleRows) // posición del scroll
        {
            const auto h = (float) listArea.getHeight() * (float) visibleRows / (float) rows;
            const auto y = (float) listArea.getY() + ((float) listArea.getHeight() - h) * (float) listScroll / (float) (rows - visibleRows);
            g.setColour (pal.dim.brighter (0.3f));
            g.fillRect (juce::Rectangle<float> ((float) listArea.getRight() - 4.0f, y, 2.0f, h));
        }
        scanlines (g, listArea);
    }

    void paintShade (juce::Graphics& g, const Palette& pal) const
    {
        juce::String text = clockText (currentSeconds());
        if (song != nullptr)
        {
            text << "  " << transport.getSourceName().upToLastOccurrenceOf (".", false, false);
            if (const auto t = transport.getSelectedTrack(); t >= 0 && t < (int) song->tracks.size())
                text << "  " << song->tracks[(size_t) t].name << " ch" << song->tracks[(size_t) t].channel;
        }
        g.setColour (pal.ink);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        g.drawText (text, juce::Rectangle<int>::leftTopRightBottom (8, 0, buttons.empty() ? getWidth() : buttons.back().area.getX() - 6, getHeight()),
                    juce::Justification::centredLeft, true);
        for (const auto& b : buttons)
        {
            g.setColour (b.id == bPlay && transport.isPlaying() ? pal.accent : pal.ink);
            juce::Path p;
            glyph (p, b.id, b.area.toFloat().getCentre(), juce::jmin (b.area.getWidth(), b.area.getHeight()) * 0.55f);
            g.fillPath (p);
        }
        paintWindowButtons (g, pal);
    }

    //==========================================================================
    double currentSeconds() const
    {
        return transport.getSelectedTrack() >= 0 ? transport.practicePosition() : transport.positionSeconds();
    }

    void press (int id)
    {
        switch (id)
        {
            case bPlay:  transport.play(); break;
            case bPause: transport.pause(); break;
            case bStop:  transport.stop(); break;
            case bEject: chooseFile(); break;
            case bRecent: showRecent(); break;
            case bGm:    transport.setGmToMt32 (! transport.getGmToMt32()); break;
            case bModePlay:   transport.setPracticeMode (SessionTransport::practicePlay); break;
            case bModeWait:   transport.setPracticeMode (SessionTransport::practiceWait); break;
            case bModeFollow: transport.setPracticeMode (SessionTransport::practiceFollow); break;
            case bSetA: // inicio aquí; con un bucle que acaba después, se mueve su inicio
            {
                const auto a = juce::jmax (0.0, currentSeconds());
                if (loopActive() && transport.getLoopB() > a + 0.1) transport.setLoop (a, transport.getLoopB());
                else { transport.clearLoop(); loopStart = a; }
                break;
            }
            case bSetB: // fin aquí: con A antes, bucle (o se mueve su fin)
            {
                const auto b = currentSeconds();
                const auto a = loopStart >= 0.0 ? loopStart : loopActive() ? transport.getLoopA() : -1.0;
                if (a >= 0.0 && b > a + 0.1) { transport.setLoop (a, b); loopStart = -1.0; }
                break;
            }
            case bClear: loopStart = -1.0; transport.clearLoop(); break;
            case bList:  showList = ! showList; resizeKeepingDock(); break;
            default: break;
        }
        repaint();
    }

    // Casillas M (1) y S (2) a la derecha de cada fila
    static constexpr int boxesW = 34;
    static juce::Rectangle<int> rowBox (juce::Rectangle<int> row, int which)
    {
        return juce::Rectangle<int> (row.getRight() - 6 - (which == 1 ? 32 : 15), row.getY() + 2, 14, row.getHeight() - 4);
    }
    int rowAt (juce::Point<int> p) const { return listScroll + (p.y - listArea.getY() - 2) / rowH; }
    int rowBoxAt (juce::Point<int> p) const
    {
        if (song == nullptr) return 0;
        const auto row = rowAt (p);
        if (row < 1 || row > (int) song->tracks.size()) return 0;
        const auto r = juce::Rectangle<int> (listArea.getX() + 2, listArea.getY() + 2 + (row - listScroll) * rowH, listArea.getWidth() - 4, rowH);
        return rowBox (r, 1).expanded (1).contains (p) ? 1 : rowBox (r, 2).expanded (1).contains (p) ? 2 : 0;
    }

    void clickRow (juce::Point<int> p)
    {
        if (song == nullptr) return;
        const auto row = rowAt (p);
        if (row < 0 || row > (int) song->tracks.size()) return;
        if (const auto box = rowBoxAt (p); box != 0)
        {
            if (box == 1) transport.setTrackMute (row - 1, ! transport.isTrackMuted (row - 1));
            else          transport.setTrackSolo (row - 1, ! transport.isTrackSoloed (row - 1));
            repaint();
            return;
        }
        transport.selectTrack (row - 1);
        if (row > 0 && onPartSelected) onPartSelected();
        repaint();
    }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI file", lastFolder(), "*.mid;*.midi;*.smf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<MidiMusicPlayer> (this)] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult() != juce::File())
                    safe->load (fc.getResult());
            });
    }

    void load (const juce::File& f)
    {
        const auto error = transport.loadMidi (f);
        if (error.isNotEmpty())
        {
            loadError = "*** " + error + " ***";
            return;
        }
        loadError.clear();
        // Recientes: los maxRecent últimos, el más nuevo primero
        auto list = recentFiles();
        list.removeString (f.getFullPathName());
        list.insert (0, f.getFullPathName());
        list.removeEmptyStrings();
        while (list.size() > maxRecent) list.remove (list.size() - 1);
        if (auto cfg = openConfig()) cfg->setValue ("midiPlayerRecent", list.joinIntoString ("\n"));
        timerCallback();
    }

    // Ficheros recientes (config.settings, clave midiPlayerRecent: rutas separadas por saltos de línea)
    static constexpr int maxRecent = 8;
    static juce::StringArray recentFiles()
    {
        auto cfg = openConfig();
        return juce::StringArray::fromLines (cfg != nullptr ? cfg->getValue ("midiPlayerRecent") : juce::String());
    }

    juce::File lastFolder() const
    {
        const auto list = recentFiles();
        return list.isEmpty() || list[0].isEmpty() ? SessionTransport::recordingsFolder() : juce::File (list[0]).getParentDirectory();
    }

    void showRecent()
    {
        juce::PopupMenu m;
        const auto list = recentFiles();
        for (int k = 0; k < list.size(); ++k)
            if (list[k].isNotEmpty())
                m.addItem (k + 1, juce::File (list[k]).getFileName(), juce::File (list[k]).existsAsFile());
        if (m.getNumItems() == 0) m.addItem (-1, "No recent files", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = SafePointer<MidiMusicPlayer> (this), list] (int id)
                         {
                             if (safe != nullptr && id > 0 && id <= list.size())
                                 safe->load (juce::File (list[id - 1]));
                         });
    }

    void toggleShade()
    {
        shade = ! shade;
        resizeKeepingDock();
    }

    void resizeKeepingDock()
    {
        setSize (width, heightFor());
        if (mainBounds) setTopLeftPosition (fitOnScreen (mainBounds(), getScreenPosition()));
        savePlacement();
        repaint();
    }

    //==========================================================================
    // Arrastre con imán: a menos de snapPx de un borde exterior de la principal (y solapando en el otro eje) se pega a él;
    // pegada, guarda su desplazamiento respecto a la principal y la sigue (timerCallback)
    void moveTo (juce::Point<int> at)
    {
        isDocked = false;
        if (mainBounds)
        {
            const auto m = mainBounds();
            const auto w = getWidth(), h = getHeight();
            const auto overlapY = at.y < m.getBottom() && at.y + h > m.getY();
            const auto overlapX = at.x < m.getRight() && at.x + w > m.getX();
            if (overlapY && std::abs (at.x - m.getRight()) < snapPx)      { at.x = m.getRight(); isDocked = true; }
            else if (overlapY && std::abs (at.x + w - m.getX()) < snapPx) { at.x = m.getX() - w; isDocked = true; }
            if (overlapX && std::abs (at.y - m.getBottom()) < snapPx)     { at.y = m.getBottom(); isDocked = true; }
            else if (overlapX && std::abs (at.y + h - m.getY()) < snapPx) { at.y = m.getY() - h; isDocked = true; }
            if (isDocked) // alineado con la esquina si está cerca
            {
                if (std::abs (at.y - m.getY()) < snapPx) at.y = m.getY();
                if (std::abs (at.x - m.getX()) < snapPx) at.x = m.getX();
                dockOffset = at - m.getPosition();
            }
            lastMain = m;
        }
        setTopLeftPosition (at);
    }

    void savePlacement() const
    {
        if (auto cfg = openConfig())
        {
            cfg->setValue ("midiPlayerX", getScreenX());
            cfg->setValue ("midiPlayerY", getScreenY());
            cfg->setValue ("midiPlayerDocked", isDocked);
            cfg->setValue ("midiPlayerDockX", dockOffset.x);
            cfg->setValue ("midiPlayerDockY", dockOffset.y);
            cfg->setValue ("midiPlayerShade", shade);
            cfg->setValue ("midiPlayerList", showList);
            cfg->setValue ("midiPlayerRows", visibleRows);
        }
    }

    //==========================================================================
    void timerCallback() override
    {
        // Fichero nuevo: lista desde arriba y sin bucle a medias
        if (const auto s = transport.getSong(); s != song)
        {
            song = s;
            listScroll = 0;
            loopStart = -1.0;
            marqueeOffset = 0.0f;
        }
        juce::String m;
        if (loadError.isNotEmpty())
            m = loadError;
        else if (song != nullptr)
        {
            m = "*** " + transport.getSourceName() + " - " + juce::String ((int) song->tracks.size()) + " parts - "
                + juce::String (juce::roundToInt (song->bpmAt (0.0))) + " BPM";
            if (const auto t = transport.getSelectedTrack(); t >= 0 && t < (int) song->tracks.size())
                m << " - your part: " << song->describe (t);
            m << " ***";
        }
        else
            m = "*** No MIDI file loaded - press the eject button ***";
        if (m != marquee) { marquee = m; marqueeOffset = 0.0f; }
        marqueeOffset += 1.0f; // 30 px/s
        updateSpectrum();
        // Imán: sigue a la principal sin salirse de la pantalla (si no cabe, se despega y flota dentro)
        if (isDocked && mainBounds && dragging != window)
            if (const auto mb = mainBounds(); mb != lastMain)
            {
                const auto at = fitOnScreen (mb, mb.getPosition() + dockOffset);
                if (isDocked) dockOffset = at - mb.getPosition();
                setTopLeftPosition (at);
                lastMain = mb;
            }
        // Encima de las demás solo mientras la aplicación (o el DAW) está en primer plano
        const auto front = juce::Process::isForegroundProcess();
        if (front != isAlwaysOnTop()) setAlwaysOnTop (front);
        repaint();
    }

    // Espectro de la salida (anillo del display): 512 muestras, Hann, 10 bandas logarítmicas de 60 Hz a 16 kHz, en dB
    // relativos a una senoidal de fondo de escala; caída de 60 dB/s (2 dB por refresco a 30 Hz)
    void updateSpectrum()
    {
        const auto end = koni.scopeWritePos.load (std::memory_order_acquire);
        constexpr auto mask = KoniSynthProcessor::scopeSize - 1;
        for (int i = 0; i < fftSize; ++i)
        {
            const auto idx = (size_t) ((end - fftSize + i) & mask);
            fftData[(size_t) i] = 0.5f * (koni.scopeLeft[idx].load (std::memory_order_relaxed) + koni.scopeRight[idx].load (std::memory_order_relaxed));
        }
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        hann.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        const auto fs = koni.getSampleRate() > 0.0 ? koni.getSampleRate() : 48000.0;
        for (int b = 0; b < numBands; ++b)
        {
            const auto f0 = 60.0 * std::pow (16000.0 / 60.0, (double) b / numBands), f1 = 60.0 * std::pow (16000.0 / 60.0, (double) (b + 1) / numBands);
            const auto k0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 * fftSize / fs)), k1 = juce::jlimit (k0 + 1, fftSize / 2, (int) (f1 * fftSize / fs) + 1);
            float peak = 0.0f;
            for (int k = k0; k < k1; ++k) peak = juce::jmax (peak, fftData[(size_t) k]);
            const auto db = juce::Decibels::gainToDecibels (peak / (fftSize * 0.25f), -100.0f);
            bands[(size_t) b] = juce::jmax (db, bands[(size_t) b] - 2.0f);
        }
    }

    KoniSynthProcessor& koni;
    SessionTransport& transport;
    std::shared_ptr<const PlayAlongSong> song;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tips { this, 700 }; // la del editor no alcanza a esta ventana (otro peer)

    static constexpr int fftOrder = 9, fftSize = 1 << fftOrder, numBands = 10;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> hann { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, numBands> bands { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };

    juce::Rectangle<int> titleArea, closeBox, shadeBox, timeArea, marqueeArea, chipsArea, volumeRow, tempoRow, seekArea, listHeader, listArea;
    std::vector<Button> buttons;
    juce::String marquee, loadError;
    float marqueeOffset = 0.0f;
    bool shade = false, showList = true, isDocked = false;
    int pressed = 0, listScroll = 0;
    Drag dragging = none;
    int dragRows = 5;
    juce::Point<int> dragStart, dockOffset;
    juce::Rectangle<int> lastMain;
    double seekPreview = -1.0, loopStart = -1.0;
};

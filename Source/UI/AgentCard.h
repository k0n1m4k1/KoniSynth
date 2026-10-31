// Se incluye desde KoniSynth.cpp (mismo TU), antes de UI/Editor.h. Usa Skin, Widgets (MidiActionButton) y Agent/AgentSession.h.
#pragma once

//==============================================================================
// Tarjeta central de la sesión del agente (Agent/AgentSession.h): flota sobre el editor SIN oscurecer el fondo (el resto
// de la UI sigue usable) y se arrastra por cualquier punto que no sea un botón. Solo existe a la vista mientras hay sesión
// abierta, control del agente o un aviso; no reserva sitio en la barra ni en los bloques.
//  - Sesión: lámpara ámbar (fija si el agente espera; parpadea con el control; roja grabando), tiempo restante, pantalla
//    de fósforo con la tarea y la toma, y Take back control (con MIDI Learn: action:revoke).
//  - Has tocado: "Take back control?" con Let it continue y una barra de 8 s (sin respuesta, el agente sigue).
//  - Avisos: texto y barra de 6 s; se cierran solos (× los cierra antes). Sin OK.
class AgentCard : public juce::Component, private juce::Timer
{
public:
    AgentCard (AgentSession& s, MidiLearn& learn) : session (s)
    {
        take.learn = &learn;
        take.action = MidiLearn::actionRevoke;
        take.setTooltip ("Take back control: cancels the agent's notes, stops its take (kept) and closes the agent session. "
                         "Right-click: MIDI Learn (any button of your controller).");
        take.onClick = [this] { session.revoke ("Control taken back, agent session closed."); refresh(); };
        keep.setTooltip ("Let the agent go on. It will not ask again for 30 s while you keep playing.");
        keep.onClick = [this] { session.keep(); refresh(); };
        close.setTooltip ("Close this notice.");
        close.onClick = [this] { session.dismissNotice(); refresh(); };
        for (auto* b : { static_cast<juce::Button*> (&take), static_cast<juce::Button*> (&keep), static_cast<juce::Button*> (&close) })
        {
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addChildComponent (*b);
        }
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        startTimerHz (10);
    }

    // El editor la recoloca al cambiar de tamaño: centrada (42 % del alto) mientras no se haya arrastrado
    void place()
    {
        if (auto* parent = getParentComponent())
        {
            const auto area = parent->getLocalBounds();
            if (! moved)
                setTopLeftPosition (area.getCentreX() - getWidth() / 2, juce::roundToInt (area.getHeight() * 0.42f) - getHeight() / 2);
            else
                setTopLeftPosition (juce::jlimit (0, juce::jmax (0, area.getWidth() - getWidth()), getX()),
                                    juce::jlimit (0, juce::jmax (0, area.getHeight() - getHeight()), getY()));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); // sombra proyectada
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 8.0f);
        const auto face = Skin::panelFace();
        g.setGradientFill (juce::ColourGradient (face.top, 0.0f, r.getY(), face.bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 8.0f, r.getRight() - 8.0f);

        // Cabecera: lámpara, título y tiempo restante
        auto area = getLocalBounds().reduced (12, 10);
        auto head = area.removeFromTop (18);
        const auto control = session.inControl.load();
        const auto rec = session.isRecording();
        const auto lamp = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ (float) head.getX() + 6.0f, (float) head.getCentreY() });
        const auto lit = mode == Mode::notice ? 0.0f : control && ! rec ? (blink ? 1.0f : 0.45f) : 1.0f;
        const auto colour = mode == Mode::notice ? juce::Colour (0xff8a8580) : rec ? juce::Colour (0xffff4030) : juce::Colour (0xffe0a040);
        if (lit > 0.0f)
        {
            g.setColour (colour.withAlpha (0.35f * lit));
            g.fillEllipse (lamp.expanded (3.0f));
        }
        g.setColour (colour.withMultipliedBrightness (0.5f + 0.5f * lit));
        g.fillEllipse (lamp);
        head.removeFromLeft (20);
        g.setColour (Skin::white());
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
        g.drawText (mode == Mode::notice ? "AGENT" : "AGENT SESSION", head, juce::Justification::centredLeft);
        if (mode != Mode::notice && session.sessionOpen())
        {
            const auto left = juce::roundToInt (session.secondsLeft());
            g.setColour (Skin::white().withAlpha (0.6f));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
            g.drawText (juce::String::formatted ("%d:%02d left", left / 60, left % 60), head.withTrimmedRight (mode == Mode::notice ? 22 : 0),
                        juce::Justification::centredRight);
        }
        area.removeFromTop (8);

        if (mode != Mode::notice)
        {
            // Pantalla de fósforo: tarea y estado
            const auto screen = area.removeFromTop (42).toFloat();
            Skin::screen (g, screen, 4.0f);
            g.setColour (Skin::accent());
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
            auto text = screen.reduced (8.0f, 4.0f);
            const auto dot = juce::String::fromUTF8 (" \xc2\xb7 "); // punto medio
            g.drawText (control ? session.getTask() : "Session open" + dot + "agent idle", text.removeFromTop (17.0f),
                        juce::Justification::centredLeft, true);
            const auto t = juce::roundToInt (session.takeSeconds());
            g.setColour (Skin::accent().withAlpha (0.7f));
            g.drawText (rec ? "REC" + dot + session.getPart() + dot + juce::String::formatted ("%02d:%02d", t / 60, t % 60)
                            : control ? juce::String ("IN CONTROL") : juce::String ("WAITING FOR A TASK"),
                        text, juce::Justification::centredLeft, true);
            Skin::glass (g, screen, 4.0f);
            area.removeFromTop (8);
        }
        if (mode != Mode::session)
        {
            // Pregunta o aviso, con su barra de tiempo
            const auto msg = mode == Mode::asking ? juce::String ("You played. Take back control?") : session.noticeText();
            g.setColour (Skin::white());
            g.setFont (juce::FontOptions (13.0f, mode == Mode::asking ? juce::Font::bold : juce::Font::plain));
            g.drawFittedText (msg, area.removeFromTop (mode == Mode::asking ? 20 : 54).withTrimmedRight (mode == Mode::notice ? 22 : 0),
                              juce::Justification::topLeft, 4);
            area.removeFromTop (6);
            const auto bar = area.removeFromTop (3).toFloat();
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (bar, 1.5f);
            g.setColour (Skin::accent());
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) (mode == Mode::asking ? session.askFraction() : session.noticeFraction())), 1.5f);
        }
    }

    void resized() override
    {
        auto buttons = getLocalBounds().reduced (12, 10).removeFromBottom (28);
        take.setBounds (buttons.removeFromRight (150));
        buttons.removeFromRight (8);
        keep.setBounds (buttons.removeFromRight (120));
        close.setBounds (getWidth() - 34, 8, 22, 22);
    }

    void mouseDown (const juce::MouseEvent& e) override { dragger.startDraggingComponent (this, e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        dragger.dragComponent (this, e, nullptr);
        moved = true;
        place(); // dentro del editor
    }

private:
    enum class Mode { hidden, session, asking, notice };

    void timerCallback() override
    {
        blink = ! blink || session.isRecording();
        refresh();
    }

    void refresh()
    {
        const auto notice = session.noticeText().isNotEmpty();
        const auto next = session.isAsking() ? Mode::asking
                        : session.inControl.load() || (session.sessionOpen() && ! notice) ? Mode::session
                        : notice ? Mode::notice : Mode::hidden;
        if (next != mode)
        {
            mode = next;
            // Alto según lo que muestra: cabecera 18 + pantalla 42 + mensaje + barra + botones 28, con márgenes
            const auto h = mode == Mode::session ? 20 + 18 + 8 + 42 + 8 + 28
                         : mode == Mode::asking  ? 20 + 18 + 8 + 42 + 8 + 20 + 6 + 3 + 8 + 28
                         : 20 + 18 + 8 + 54 + 6 + 3;
            setSize (420, h);
            take.setVisible (mode == Mode::session || mode == Mode::asking);
            keep.setVisible (mode == Mode::asking);
            close.setVisible (mode == Mode::notice);
            setVisible (mode != Mode::hidden);
            place();
            if (isVisible())
                toFront (false);
        }
        if (isVisible())
            repaint();
    }

    AgentSession& session;
    MidiActionButton<juce::TextButton> take { "Take back control" };
    juce::TextButton keep { "Let it continue" }, close { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::ComponentDragger dragger;
    Mode mode = Mode::hidden;
    bool moved = false, blink = true;
};

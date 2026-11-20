// Rueda del ratón sobre los mandos: solo cambia un valor cuando el ratón lleva 300 ms quieto encima de ese mando (mando
// "armado"); antes, la rueda pasa al panel (Component::mouseWheelMove la sube hasta el Viewport) y desplaza la vista. Así,
// desplazar el panel con la rueda no cambia parámetros al pasar por encima de un mando.
// - Mandos: todo juce::Slider y los componentes con la propiedad "wheelGate" (displays con rueda: BPM, encoder del FM-1,
//   Q del ecualizador, pasos del secuenciador), que consultan armed() en su mouseWheelMove.
// - Sondeo a 30 Hz del componente bajo el ratón (no depende de que llegue un mouseEnter tras desplazar el panel bajo un
//   ratón quieto); un oyente global de la rueda reinicia la espera mientras la rueda desplaza el panel.
// - El mando armado lleva la propiedad "wheelArmed" (KoniLookAndFeel intensifica su halo de hover) y, si es un Slider,
//   setScrollWheelEnabled (true). El LookAndFeel, al dibujar cada Slider, deja la rueda apagada si no está armado: un
//   Slider que aún no ha pasado por aquí tampoco la acepta.
// Un único vigilante para todas las ventanas del plugin (editor, bloques desacoplados, diálogos) y todas sus instancias
// en el proceso: retain() / release() por editor. Hilo de mensajes.
struct WheelGate : private juce::MouseListener, private juce::Timer
{
    static constexpr double armMs = 300.0;

    static void retain()
    {
        auto& g = slot();
        if (g == nullptr)
        {
            g.reset (new WheelGate());
            juce::Desktop::getInstance().addGlobalMouseListener (g.get());
            g->startTimerHz (30);
        }
        ++g->users;
    }
    static void release()
    {
        auto& g = slot();
        if (g == nullptr || --g->users > 0)
            return;
        juce::Desktop::getInstance().removeGlobalMouseListener (g.get());
        g->stopTimer();
        g->setTarget (nullptr);
        g.reset();
    }

    // ¿La rueda puede cambiar ya el valor de este mando?
    static bool armed (const juce::Component& c) { return c.getProperties().getWithDefault (armedProp, false); }

    // Para los mandos propios (no Slider) que responden a la rueda: entran en la espera
    static void enable (juce::Component& c) { c.getProperties().set (gateProp, true); }

    static inline const juce::Identifier gateProp { "wheelGate" }, armedProp { "wheelArmed" };

private:
    WheelGate() = default;
    static std::unique_ptr<WheelGate>& slot() { static std::unique_ptr<WheelGate> g; return g; }

    // El mando que contiene a c (el propio c, o el Slider del que c es la caja de texto)
    static juce::Component* controlFor (juce::Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (dynamic_cast<juce::Slider*> (c) != nullptr || c->getProperties().contains (gateProp))
                return c;
        return nullptr;
    }

    static void setArmed (juce::Component& c, bool on)
    {
        if (armed (c) == on)
            return;
        c.getProperties().set (armedProp, on);
        if (auto* s = dynamic_cast<juce::Slider*> (&c))
            s->setScrollWheelEnabled (on);
        c.repaint();
    }

    void setTarget (juce::Component* c)
    {
        if (target.getComponent() == c)
            return;
        if (auto* old = target.getComponent())
        {
            setArmed (*old, false);
            old->repaint(); // quita el halo de hover
        }
        target = c;
        lastActivity = juce::Time::getMillisecondCounterHiRes();
        if (c != nullptr)
            c->repaint(); // halo de hover
    }

    void timerCallback() override
    {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        setTarget (controlFor (source.getComponentUnderMouse()));
        const auto pos = source.getScreenPosition();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        auto* c = target.getComponent();
        if (c == nullptr || armed (*c))
        {
            lastPos = pos;
            return;
        }
        if (pos.getDistanceFrom (lastPos) > 1.0f) // movido: la espera vuelve a empezar
            lastActivity = now;
        lastPos = pos;
        if (now - lastActivity >= armMs)
            setArmed (*c, true);
    }

    // La rueda sin armar desplaza el panel: mientras se usa, la espera vuelve a empezar
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override
    {
        if (auto* c = target.getComponent(); c != nullptr && ! armed (*c))
            lastActivity = juce::Time::getMillisecondCounterHiRes();
    }

    juce::Component::SafePointer<juce::Component> target;
    juce::Point<float> lastPos;
    double lastActivity = 0.0;
    int users = 0;
};

// Viewport del panel con la rueda suave: cada muesca desplaza lo mismo que juce::Viewport (deltaY · 14 · 16 px; en Windows,
// 120/256 · 224 = 105 px por muesca, juce_Windowing_windows.cpp:2799 y juce_Viewport.cpp:562) pero animado al refresco de la
// pantalla con una aproximación exponencial (tau = 50 ms: el 95 % en 150 ms). Las muescas seguidas suman al destino. La
// rueda de un trackpad (isSmooth), con modificadores o sin barra vertical, va como en JUCE. Si otro mueve la vista (la barra,
// ir a un bloque), la animación se detiene
struct SmoothViewport : juce::Viewport
{
    SmoothViewport() : vblank (this, [this] (double t) { tick (t); }) {}

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.eventComponent != this || w.isSmooth || w.deltaY == 0.0f || e.mods.isAnyModifierKeyDown()
            || ! getVerticalScrollBar().isVisible() || getViewedComponent() == nullptr)
        {
            active = false;
            return juce::Viewport::mouseWheelMove (e, w);
        }
        if (! active)
            y = target = (double) getViewPositionY();
        const auto maxY = (double) juce::jmax (0, getViewedComponent()->getHeight() - getViewHeight());
        target = juce::jlimit (0.0, maxY, target - (double) w.deltaY * 14.0 * 16.0);
        lastTime = 0.0;
        active = true;
    }

private:
    void tick (double now)
    {
        if (! active)
            return;
        if (getViewPositionY() != shownY && lastTime > 0.0) // otro ha movido la vista
        {
            active = false;
            return;
        }
        const auto dt = lastTime > 0.0 ? juce::jlimit (0.0, 0.1, now - lastTime) : 1.0 / 60.0;
        lastTime = now;
        y += (target - y) * (1.0 - std::exp (-dt / 0.05));
        if (std::abs (target - y) < 0.5)
        {
            y = target;
            active = false;
        }
        shownY = juce::roundToInt (y);
        setViewPosition (getViewPositionX(), shownY);
        shownY = getViewPositionY(); // tras el recorte de JUCE
    }

    double y = 0.0, target = 0.0, lastTime = 0.0;
    int shownY = 0;
    bool active = false;
    juce::VBlankAttachment vblank; // al final: se destruye primero
};

// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, APVTS). No es una cabecera independiente.
#pragma once

// Deshacer / rehacer de lo que hace el usuario: cada gesto sobre un parámetro (arrastre de perilla, clic, selector,
// Set value, Reset) es un paso; una carga de preset es un solo paso (parámetros + propiedades de texto + nombre).
// También las propiedades de estado seguidas (macros, nombre del preset). No entran: automatización del host, MIDI
// (CC, MIDI learn) ni los destinos movidos por las macros de interpretación (no abren gesto).
// Todo en el hilo de mensajes salvo parameterValueChanged, que llega de cualquier hilo y no hace nada.
// Un estado nuevo del host (setStateInformation: abrir proyecto) borra el historial (valueTreeRedirected).
// Coste: un paso de perilla = 12 bytes; tope de 200 pasos.
class UndoHistory : private juce::AudioProcessorParameter::Listener, private juce::ValueTree::Listener
{
public:
    static constexpr size_t maxSteps = 200;

    ~UndoHistory() override { detach(); }

    void attach (juce::AudioProcessor& processor, APVTS& s, std::vector<juce::Identifier> trackedProperties)
    {
        apvts = &s;
        tracked = std::move (trackedProperties);
        params = processor.getParameters();
        for (auto* p : params)
            p->addListener (this);
        s.state.addListener (this);
        refreshCache();
    }

    void detach()
    {
        for (auto* p : params)
            p->removeListener (this);
        params.clear();
        if (apvts != nullptr)
            apvts->state.removeListener (this);
        apvts = nullptr;
    }

    // Agrupa en un solo paso todo lo que ocurra entre begin y end (anidable): carga de preset
    void beginGroup() { ++depth; }
    void endGroup()
    {
        if (depth > 0 && --depth == 0 && ! pending.empty())
        {
            undoStack.push_back (std::move (pending));
            pending = {};
            if (undoStack.size() > maxSteps)
                undoStack.erase (undoStack.begin());
            redoStack.clear();
            changed();
        }
    }

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    juce::String undoName() const { return canUndo() ? describe (undoStack.back()) : juce::String(); }
    juce::String redoName() const { return canRedo() ? describe (redoStack.back()) : juce::String(); }

    void undo() { move (undoStack, redoStack, true); }
    void redo() { move (redoStack, undoStack, false); }

    void clear() { undoStack.clear(); redoStack.clear(); pending.clear(); gestureStart.clear(); changed(); }

    std::function<void()> onChange; // la UI refresca los botones

private:
    struct Change
    {
        int param = -1;              // índice del parámetro, o -1 si es una propiedad
        float before = 0.0f, after = 0.0f;
        juce::Identifier prop;
        juce::var propBefore, propAfter;
    };
    using Step = std::vector<Change>;

    void parameterValueChanged (int, float) override {} // cualquier hilo: nada

    void parameterGestureChanged (int index, bool starting) override
    {
        if (applying || index < 0 || index >= params.size())
            return;
        if (starting)
        {
            gestureStart[index] = params[index]->getValue();
            beginGroup();
            return;
        }
        if (const auto it = gestureStart.find (index); it != gestureStart.end())
        {
            const auto after = params[index]->getValue();
            if (std::abs (after - it->second) > 1.0e-7f)
                record ({ index, it->second, after, {}, {}, {} });
            gestureStart.erase (it);
            endGroup();
        }
    }

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id) override
    {
        if (apvts == nullptr || tree != apvts->state || std::find (tracked.begin(), tracked.end(), id) == tracked.end())
            return;
        const auto before = cache[id.toString()];
        const auto after = tree.getProperty (id);
        cache.set (id.toString(), after);
        if (applying || before == after)
            return;
        beginGroup();
        record ({ -1, 0.0f, 0.0f, id, before, after });
        endGroup();
    }

    void valueTreeRedirected (juce::ValueTree&) override // estado nuevo del host
    {
        refreshCache();
        clear();
    }

    void record (Change c)
    {
        // Dentro de un mismo paso, dos cambios del mismo parámetro: se conserva el primer "antes" y el último "después"
        for (auto& p : pending)
            if (p.param == c.param && p.prop == c.prop)
            {
                p.after = c.after;
                p.propAfter = c.propAfter;
                return;
            }
        pending.push_back (std::move (c));
    }

    void move (std::vector<Step>& from, std::vector<Step>& to, bool backwards)
    {
        if (from.empty() || apvts == nullptr)
            return;
        auto step = std::move (from.back());
        from.pop_back();
        applying = true;
        auto applyOne = [&] (const Change& c)
        {
            if (c.param >= 0)
            {
                auto* p = params[c.param];
                p->beginChangeGesture();
                p->setValueNotifyingHost (backwards ? c.before : c.after);
                p->endChangeGesture();
            }
            else
            {
                const auto& v = backwards ? c.propBefore : c.propAfter;
                apvts->state.setProperty (c.prop, v, nullptr);
                cache.set (c.prop.toString(), v);
            }
        };
        if (backwards)
            for (auto it = step.rbegin(); it != step.rend(); ++it) applyOne (*it);
        else
            for (const auto& c : step) applyOne (c);
        applying = false;
        to.push_back (std::move (step));
        changed();
    }

    juce::String describe (const Step& s) const
    {
        if (s.size() > 1)
            return "Preset / " + juce::String ((int) s.size()) + " changes";
        const auto& c = s.front();
        return c.param >= 0 ? params[c.param]->getName (40) : c.prop.toString();
    }

    void refreshCache()
    {
        cache.clear();
        if (apvts != nullptr)
            for (const auto& id : tracked)
                cache.set (id.toString(), apvts->state.getProperty (id));
    }

    void changed() { if (onChange) onChange(); }

    APVTS* apvts = nullptr;
    juce::Array<juce::AudioProcessorParameter*> params;
    std::vector<juce::Identifier> tracked;
    juce::NamedValueSet cache;
    std::map<int, float> gestureStart;
    std::vector<Step> undoStack, redoStack;
    Step pending;
    int depth = 0;
    bool applying = false;
};

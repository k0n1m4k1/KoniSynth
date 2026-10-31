// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// UI: tooltip de un parámetro = ayuda (paramHelp) + valor por defecto. Regla del proyecto: todo parámetro con control
// en la UI tiene ayuda; en Debug, un parámetro sin entrada en paramHelp dispara un jassert.
static juce::String paramTooltip (APVTS& s, const juce::String& id)
{
    auto* p = s.getParameter (id);
    const auto help = paramHelp (id);
    jassert (help.isNotEmpty()); // añadir la entrada en paramHelp()
    if (p == nullptr)
        return help;
    auto def = p->getText (p->getDefaultValue(), 32);
    if (const auto label = p->getLabel(); label.isNotEmpty())
        def << " " << label;
    return help + "\nDefault: " + def;
}

// UI: menú contextual de un parámetro (clic derecho sobre su control): MIDI Learn, Clear MIDI, escribir el valor exacto
// y restablecer el valor por defecto. Se registra como MouseListener del control (perilla, slider, interruptor o
// selector). paramSource (opcional): el parámetro se decide al abrir el menú (LED de BPM: el reloj del motor activo)
// Botón de acción con MIDI learn (PANIC, REC, STOP, PLAY): clic derecho = MIDI Learn / Clear MIDI, con las mismas
// palabras que el menú de los parámetros; mientras espera un CC, marco ámbar (lo repinta quien observa 'learning')
template <typename ButtonType>
struct MidiActionButton : ButtonType
{
    using ButtonType::ButtonType;
    MidiLearn* learn = nullptr;
    int action = -1; // MidiLearn::Action; -1 = sin MIDI learn

    bool isLearning() const { return learn != nullptr && action >= 0 && learn->learning.load() == MidiLearn::actionBase + action; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || learn == nullptr || action < 0)
            return ButtonType::mouseDown (e);
        const auto index = MidiLearn::actionBase + action;
        const auto cc = learn->ccFor (index);
        juce::PopupMenu m;
        m.addItem ("MIDI Learn", [l = learn, index] { l->learning = index; });
        m.addItem (cc >= 0 ? "Clear MIDI (CC " + juce::String (cc) + ")" : juce::String ("Clear MIDI"), cc >= 0, false, [l = learn, index] { l->clear (index); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || learn == nullptr || action < 0)
            ButtonType::mouseUp (e);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (isLearning())
        {
            g.setColour (Skin::c (0xffffb000));
            g.drawRoundedRectangle (this->getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
        }
    }
};

// Propiedad con el ID del parámetro que ParamMenu pone en su mando: collectParamIds encuentra así los parámetros de un bloque
// o de un diálogo recorriendo sus componentes
inline const juce::Identifier paramIdTag { "koniParamId" };

struct ParamMenu : juce::MouseListener
{
    ParamMenu (APVTS& s, const juce::String& id, juce::Component& target)
        : param (s.getParameter (id)),
          koni (dynamic_cast<KoniSynthProcessor&> (s.processor)),
          learn (koni.midiLearn),
          component (&target)
    {
        target.addMouseListener (this, true);
        target.getProperties().set (paramIdTag, id);
    }

    ~ParamMenu() override
    {
        if (auto* c = component.getComponent())
            c->removeMouseListener (this);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (paramSource)
            param = paramSource();
        if (! e.mods.isPopupMenu() || param == nullptr)
            return;

        const auto index = param->getParameterIndex();
        const auto cc = learn.ccFor (index);
        juce::PopupMenu menu;
        menu.addItem (1, "MIDI Learn");
        menu.addItem (2, cc >= 0 ? "Clear MIDI (CC " + juce::String (cc) + ")" : juce::String ("Clear MIDI"), cc >= 0);
        menu.addSeparator();
        // Valor exacto según el tipo: con etiquetas (selector, LFO Shape, MIDI Ch "Omni"...) = submenú con cada opción;
        // numérico = campo con número, unidad y rango; interruptor = nada (un clic ya lo cambia)
        const auto steps = discreteSteps (*param);
        if (steps.size() > 0)
        {
            juce::PopupMenu values;
            const auto now = param->getValue();
            for (int i = 0; i < steps.size(); ++i)
            {
                const auto v = (float) i / (float) (steps.size() - 1);
                values.addItem (100 + i, steps[i], true, std::abs (v - now) < 0.5f / (float) (steps.size() - 1));
            }
            menu.addSubMenu ("Set value", values);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (param) == nullptr)
            menu.addItem (4, "Set value...");
        menu.addItem (3, "Reset to default (" + param->getText (param->getDefaultValue(), 32) + ")");
        // Modulaciones de la matriz que mueven este mando (UI/MatrixUi.h): quitar una o todas
        const auto destination = MatrixUi::destinationOf (param->getParameterID());
        const auto modSlots = destination > 0 ? MatrixUi::slotsFor (koni.apvts, destination) : std::vector<int>();
        if (! modSlots.empty())
        {
            menu.addSeparator();
            for (const auto slot : modSlots)
            {
                const auto src = (int) MatrixUi::slotValue (koni.apvts, "mtxSrc", slot);
                const auto amt = MatrixUi::slotValue (koni.apvts, "mtxAmt", slot);
                menu.addItem (300 + slot, "Remove modulation: " + ModMatrix::sourceNames()[src] + " ("
                                              + (amt > 0.0f ? "+" : "") + juce::String (juce::roundToInt (amt * 100.0f)) + " %)");
            }
            if (modSlots.size() > 1)
                menu.addItem (320, "Remove all modulation");
        }
        // Macros de interpretación (Params/PerfMacros.h): asignar este parámetro a una (las macros no se asignan entre sí)
        const auto id = param->getParameterID();
        if (! PerfMacros::isMacroParam (id))
        {
            const auto current = koni.perfMacros.macroOf (id);
            juce::PopupMenu assign;
            for (int m = 0; m < PerfMacros::numMacros; ++m)
                assign.addItem (200 + m, juce::String (m + 1) + "  " + koni.perfMacros.name (m), true, m == current);
            menu.addSeparator();
            menu.addSubMenu ("Assign to macro", assign);
            if (current >= 0)
                menu.addItem (210, "Remove from " + koni.perfMacros.name (current));
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (component.getComponent()),
            [this, p = param, l = &learn, index, safe = component, modSlots] (int result)
            {
                if (safe == nullptr)
                    return;
                if (result >= 300 && result < 300 + ModMatrix::numSlots)
                    MatrixUi::removeSlot (koni.apvts, result - 300);
                else if (result == 320)
                    for (const auto slot : modSlots) MatrixUi::removeSlot (koni.apvts, slot);
                else if (result >= 200 && result < 200 + PerfMacros::numMacros)
                    koni.perfMacros.assign (result - 200, p->getParameterID());
                else if (result == 210)
                    koni.perfMacros.remove (p->getParameterID());
                else if (result >= 100)
                {
                    const auto n = p->getNumSteps();
                    p->beginChangeGesture();
                    p->setValueNotifyingHost ((float) (result - 100) / (float) juce::jmax (1, n - 1));
                    p->endChangeGesture();
                }
                else if (result == 4)
                    askValue (*p, *safe);
                else if (result == 1)
                    l->learning = index;
                else if (result == 2)
                    l->clear (index);
                else if (result == 3)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->getDefaultValue());
                    p->endChangeGesture();
                }
            });
    }

    // Número del texto mostrado y su unidad: "2.50 kHz" -> ("2.50", "kHz"); "50 %" -> ("50", "%"); con prefijo de
    // escala ("x1.00", "I 0.00") -> ("1.00", ""). Etiquetas y notas ("Full", "Off (sustain)", "C4", "A"): número vacío
    static std::pair<juce::String, juce::String> splitUnit (const juce::String& text)
    {
        const auto t = text.trim();
        const auto isNum = [] (juce::juce_wchar c) { return juce::CharacterFunctions::isDigit (c) || c == '-' || c == '+' || c == '.'; };
        int i = 0;
        while (i < t.length() && ! isNum (t[i]))
            ++i;
        const auto prefix = t.substring (0, i);
        if (i == t.length() || (prefix.isNotEmpty() && prefix != "x" && ! prefix.endsWithChar (' ')))
            return {};
        int j = i;
        while (j < t.length() && isNum (t[j]))
            ++j;
        return { t.substring (i, j), t.substring (j).trim() };
    }

    // Pasos de un parámetro discreto que se eligen mejor en una lista: con etiquetas (selector, "Sine"/"Free", "Omni"/"1"..)
    // hasta 64 pasos; numéricos (Duty 12.5..75 %) hasta 16. Vacío = campo numérico (continuo o entero largo) o interruptor
    static juce::StringArray discreteSteps (juce::RangedAudioParameter& p)
    {
        if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
            return {};
        const auto n = p.getNumSteps();
        if (n < 2 || n > 64)
            return {};
        juce::StringArray texts;
        bool labelled = dynamic_cast<juce::AudioParameterChoice*> (&p) != nullptr;
        for (int i = 0; i < n; ++i)
        {
            texts.add (p.getText ((float) i / (float) (n - 1), 64));
            labelled = labelled || splitUnit (texts[i]).first.isEmpty(); // empieza por letra: es una etiqueta
        }
        return labelled || n <= 16 ? texts : juce::StringArray();
    }

    // Campo de valor: solo el número (la unidad va fuera del campo) y el rango a la vista. Valor actual sin número
    // ("Full", "Off (sustain)", "C4"): el campo trae ese texto y la unidad sale del extremo numérico del rango. Lo escrito
    // con unidad o letras ("2 kHz", "C#3") va tal cual al parser del parámetro; sin número no cambia nada, salvo en
    // parámetros de etiquetas (vocal "E"). Fuera de rango: se ajusta al límite
    void askValue (juce::RangedAudioParameter& p, juce::Component& anchor)
    {
        const auto current = p.getCurrentValueAsText();
        auto [number, unit] = splitUnit (current);
        const auto labelled = number.isEmpty();
        if (labelled)
            unit = splitUnit (p.getText (1.0f, 32)).second.isNotEmpty() ? splitUnit (p.getText (1.0f, 32)).second
                                                                        : splitUnit (p.getText (0.0f, 32)).second;
        const auto range = p.getText (0.0f, 32) + "  to  " + p.getText (1.0f, 32);
        overlay = Overlay::create (anchor, p.getName (64), "Value" + (unit.isNotEmpty() ? " in " + unit : juce::String())
                                   + "   (range " + range + ")", labelled ? current : number, "Set", "Cancel",
            [this, pp = &p, u = unit, labelled] (bool ok, const juce::String& text, int)
            {
                const auto t = text.trim();
                const auto hasDigits = t.containsAnyOf ("0123456789");
                if (ok && t.isNotEmpty() && (hasDigits || labelled))
                {
                    const auto hasUnit = t.containsAnyOf ("%abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ#");
                    const auto full = hasUnit || u.isEmpty() ? t : t + " " + u;
                    pp->beginChangeGesture();
                    pp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, pp->getValueForText (full)));
                    pp->endChangeGesture();
                }
                juce::MessageManager::callAsync ([safe = juce::WeakReference<ParamMenu> (this)] { if (auto* m = safe.get()) m->overlay.reset(); });
            });
    }

    juce::RangedAudioParameter* param;
    KoniSynthProcessor& koni;
    MidiLearn& learn;
    juce::Component::SafePointer<juce::Component> component;
    std::function<juce::RangedAudioParameter*()> paramSource;
    std::unique_ptr<Overlay> overlay;
    JUCE_DECLARE_WEAK_REFERENCEABLE (ParamMenu)
};

// Iconos de forma de onda para las elecciones de onda (desplegables, perillas por pasos y teclas): un ciclo dibujado por
// nombre de opción. Las formas del Synth salen de oscillator() (Dsp/OscillatorCore.h, sin BLEP, PW 50 %) con sus mezclas
// medidas; las de OPL siguen las 8 formas del YMF262; S&H, Random y Noise usan valores fijos (el icono no cambia).
// Sin icono (texto como hasta ahora): opciones que no son una forma (Wavetable, Sample, Strings, Organ...) y las de ancho
// no documentado (Pulse 1 / Pulse 2 del Polivoks)
namespace WaveGlyph
{
    // Valor en t ∈ [0, 1) de la forma de 'name'; false si no tiene icono
    static bool sample (juce::String name, double t, double& v)
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        static constexpr double steps[] = { 0.55, -0.35, 0.9, -0.8, 0.15, 0.7, -0.6, -0.1 }; // S&H y Random
        static constexpr double noise[] = { 0.2, -0.7, 0.9, -0.1, 0.5, -0.9, 0.3, 0.8, -0.4, 0.1, -0.6, 0.7, -0.2, 0.95, -0.8, 0.4,
                                            -0.3, 0.6, -0.95, 0.15, 0.85, -0.5, 0.05, -0.75 };
        name = name.trim();
        if (name.length() > 2 && juce::CharacterFunctions::isDigit (name[0]) && name[1] == ' ') // OPL: "3 Quarter sine"
            name = name.substring (2);
        name = name.upToFirstOccurrenceOf (" (OPL3)", false, false);
        static const std::pair<const char*, int> synth[] = { { "Sine", sine }, { "Triangle", triangle }, { "Saw", saw }, { "Saw Up", saw },
            { "Square", square }, { "Tri-Saw", triSaw }, { "Saw+Pulse", sawPulse }, { "Saw+Tri", sawTri }, { "Tri+Pulse", triPulse },
            { "Saw+Tri+Pulse", sawTriPulse }, { "Saw+Pulse (Juno)", sawPulseJuno } };
        for (const auto& [text, wave] : synth)
            if (name == text) { v = oscillator (wave, t, 0.0, 0.5f, false); return true; }
        const auto s = std::sin (twoPi * t);
        if (name == "Saw Down")      v = 1.0 - 2.0 * t;
        else if (name == "Pulse 25 %") v = t < 0.25 ? 1.0 : -1.0;
        else if (name == "S&H")      v = steps[(int) (t * 8.0) % 8];
        else if (name == "Random")
        {
            const auto x = t * 8.0, f = x - std::floor (x);
            const auto a = steps[(int) x % 8], b = steps[((int) x + 1) % 8];
            v = a + (b - a) * 0.5 * (1.0 - std::cos (juce::MathConstants<double>::pi * f));
        }
        else if (name == "Noise")    v = noise[(int) (t * 24.0) % 24];
        else if (name == "Half sine") v = juce::jmax (0.0, s);
        else if (name == "Abs sine")  v = std::abs (s);
        else if (name == "Quarter sine") v = std::fmod (t, 0.5) < 0.25 ? std::abs (s) : 0.0;
        else if (name == "Alternating")  v = t < 0.5 ? std::sin (2.0 * twoPi * t) : 0.0;
        else if (name == "Camel")        v = t < 0.5 ? std::abs (std::sin (2.0 * twoPi * t)) : 0.0;
        else if (name == "Log saw")      v = t < 0.5 ? std::exp (-10.0 * t) : -std::exp (-10.0 * (1.0 - t));
        else return false;
        return true;
    }

    static bool has (const juce::String& name) { double v; return sample (name, 0.0, v); }

    // Trazo de un ciclo dentro de 'area' (amplitud normalizada al pico); vacío si no tiene icono
    static juce::Path path (const juce::String& name, juce::Rectangle<float> area)
    {
        constexpr int n = 96; // saltos inclinados 1/96 del ancho: < 0,3 px en un icono de 26 px
        double v[n + 1], peak = 1.0e-9;
        for (int i = 0; i <= n; ++i)
        {
            if (! sample (name, juce::jmin ((double) i / n, 0.99999), v[i]))
                return {};
            peak = juce::jmax (peak, std::abs (v[i]));
        }
        juce::Path p;
        for (int i = 0; i <= n; ++i)
        {
            const juce::Point<float> pt { area.getX() + area.getWidth() * (float) i / n, area.getCentreY() - area.getHeight() * 0.5f * (float) (v[i] / peak) };
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        return p;
    }

    static void draw (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour, float thickness = 1.5f)
    {
        g.setColour (colour);
        g.strokePath (path (name, area), juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Elección de forma de onda: nombre con Wave, Shape o Source y al menos 2 opciones con icono (las demás elecciones
    // con "Noise" u otra opción suelta no cuentan)
    static bool isWaveChoice (const juce::RangedAudioParameter* p)
    {
        const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (p);
        if (choice == nullptr || ! (choice->name.contains ("Wave") || choice->name.contains ("Shape") || choice->name.contains ("Source")))
            return false;
        int withGlyph = 0;
        for (const auto& c : choice->choices)
            withGlyph += has (c) ? 1 : 0;
        return withGlyph >= 2;
    }

    // Desplegable de forma: icono en cada opción de la lista (imagen del item) y, con la propiedad, en la caja cerrada
    // (KoniLookAndFeel::drawComboBox)
    static void decorate (juce::ComboBox& combo)
    {
        combo.getProperties().set ("waveGlyphs", true);
        for (juce::PopupMenu::MenuItemIterator it (*combo.getRootMenu()); it.next();)
        {
            auto& item = it.getItem();
            const auto shape = path (item.text, { 0.0f, 0.0f, 26.0f, 12.0f });
            if (shape.isEmpty())
                continue;
            auto icon = std::make_unique<juce::DrawablePath>();
            icon->setPath (shape);
            icon->setFill (juce::FillType (juce::Colours::transparentBlack));
            icon->setStrokeFill (Skin::accent());
            icon->setStrokeType (juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            item.image = std::move (icon);
        }
    }
}

// UI: control de un parámetro continuo con etiqueta: perilla o slider vertical (envolventes). Doble clic = valor por
// defecto. La etiqueta muestra el CC asignado ("Cutoff [74]") o "Learn..." mientras espera un CC.
// Parámetros de los mandos de un árbol de componentes (los marca ParamMenu), sin repetir; también los ocultos (pestañas no
// elegidas)
inline void collectParamIds (const juce::Component& c, juce::StringArray& ids)
{
    if (const auto* id = c.getProperties().getVarPointer (paramIdTag))
        ids.addIfNotAlreadyThere (id->toString());
    for (auto* child : c.getChildren())
        collectParamIds (*child, ids);
}

// "Reset ... to defaults" (marco de un bloque, pestañas de fuente y de bloque): solo parámetros (no las propiedades del preset:
// receta de Modal, sample, wavetable...), en un solo paso de deshacer. 'engine' nunca: cambiar de motor no es restablecer un
// bloque. Devuelve cuántos cambian
inline juce::StringArray resettableIds (juce::StringArray ids)
{
    ids.removeString ("engine");
    return ids;
}

inline int paramsOffDefault (APVTS& s, const juce::StringArray& ids)
{
    auto n = 0;
    for (const auto& id : resettableIds (ids))
        if (auto* p = s.getParameter (id); p != nullptr && std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-6f)
            ++n;
    return n;
}

inline int resetParamsToDefaults (APVTS& s, const juce::StringArray& ids)
{
    auto& undo = dynamic_cast<KoniSynthProcessor&> (s.processor).undoHistory;
    auto n = 0;
    undo.beginGroup();
    for (const auto& id : resettableIds (ids))
        if (auto* p = s.getParameter (id); p != nullptr && std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
            ++n;
        }
    undo.endGroup();
    return n;
}

// Menú de una sola opción "Reset <qué> to defaults" (atenuada si ya están todos en su valor por defecto)
inline void showResetMenu (APVTS& s, juce::Component& target, const juce::String& what, std::function<juce::StringArray()> ids)
{
    const auto list = ids();
    if (resettableIds (list).isEmpty())
        return;
    const auto off = paramsOffDefault (s, list);
    juce::PopupMenu m;
    m.addItem (1, "Reset " + what + " to defaults" + (off > 0 ? " (" + juce::String (off) + " changed)" : juce::String()), off > 0);
    // Junto al puntero (el marco de un bloque puede medir cientos de px); el componente solo fija la escala del menú
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMousePosition(),
                     [st = &s, list] (int id) { if (id == 1) resetParamsToDefaults (*st, list); });
}

struct Knob : juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
    enum class Style { rotary, vertical };

    Knob (APVTS& s, const juce::String& id, const juce::String& text, Style st = Style::rotary)
        : apvts (s),
          style (st),
          attachment (s, id, slider),
          menu (s, id, slider),
          name (text),
          learn (dynamic_cast<KoniSynthProcessor&> (s.processor).midiLearn),
          paramIndex (s.getParameter (id)->getParameterIndex()),
          paramId (id)
    {
        auto* p = s.getParameter (id);
        paramPtr = p;
        // Perilla: arrastre vertical (arriba = más) con el valor en una burbuja legible mientras se arrastra
        slider.setSliderStyle (style == Style::rotary ? juce::Slider::RotaryVerticalDrag : juce::Slider::LinearVertical);
        slider.setViewportIgnoreDragFlag (true); // arrastrar un mando no desplaza el panel
        slider.onDragStart = [this] { showBubble (true); };
        slider.onValueChange = [this] { if (bubble.isShowing()) showBubble (true); };
        slider.onDragEnd = [this] { showBubble (false); };
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, style == Style::rotary ? 70 : 56, 18);
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        const auto tip = paramTooltip (s, id);
        slider.setTooltip (tip);
        label.setTooltip (tip);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (label);

        // Elecciones sin valor numérico (AudioParameterChoice, o Bool): 2 opciones = tecla de dos posiciones con LED; de 3 a
        // 10 = conmutador rotatorio por pasos, con una marca por posición y 28 px de arrastre por paso (antes 250 px el
        // recorrido entero: con 3 opciones, 83 px por paso y sin sensación de posición). Más de 10, perilla continua
        const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p);
        options = choice != nullptr ? choice->choices.size() : dynamic_cast<juce::AudioParameterBool*> (p) != nullptr ? 2 : 0;
        if (WaveGlyph::isWaveChoice (p)) // la caja de valor muestra el icono de la forma en vez del nombre
        {
            slider.getProperties().set ("waveGlyphs", true);
            keyValue.getProperties().set ("waveGlyphs", true);
        }
        if (style == Style::rotary && options == 2)
        {
            key.setClickingTogglesState (true);
            key.setTooltip (tip);
            key.setViewportIgnoreDragFlag (true);
            keyAttachment = std::make_unique<APVTS::ButtonAttachment> (s, id, key);
            keyMenu = std::make_unique<ParamMenu> (s, id, key);
            key.onStateChange = [this] { updateKeyText(); };
            keyValue.getProperties().set ("valueBox", true); // KoniLookAndFeel::drawLabel: caja hundida
            keyValue.setJustificationType (juce::Justification::centred);
            updateKeyText();
            addAndMakeVisible (key);
            addAndMakeVisible (keyValue);
            addChildComponent (slider); // sigue conectado (attachment, anillo de la matriz) pero no se ve
            return;
        }
        if (style == Style::rotary && options >= 3 && options <= 10)
        {
            slider.getProperties().set ("detents", options); // KoniLookAndFeel::drawRotarySlider dibuja las marcas
            slider.setMouseDragSensitivity (28 * (options - 1));
        }
        addAndMakeVisible (slider);
        lampHit.setTooltip ("Modulation sources moving this knob (pilot lamps, glowing with their signal). Click: their "
                            "attenuators. Right-click the knob: remove a modulation.");
        lampHit.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        lampHit.onClick = [this] { openAttenuators(); };
        addChildComponent (lampHit);
        startTimerHz (5); // refleja asignaciones hechas desde el hilo de audio
    }

    void timerCallback() override
    {
        juce::String text = name;
        if (learn.learning.load() == paramIndex)
            text = "Learn...";
        else if (const auto cc = learn.ccFor (paramIndex); cc >= 0)
            text << " [" << cc << "]";

        if (label.getText() != text)
            label.setText (text, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        // Perilla de 64 px como mucho (más la etiqueta de 18 px y la caja de valor de ~22): en una ventana grande o una fila
        // alta no crece sin límite (antes ~110 px en Complex frente a ~25 en Osc 1 con dos filas); el bloque va centrado
        if (style == Style::rotary)
            r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 18 + maxDial + 22));
        label.setBounds (r.removeFromTop (18));
        slider.setBounds (r);
        layoutLamps();
        if (key.isVisible())
        {
            // Tecla de 56 × 34 px como mucho en el sitio del dial; debajo, la opción elegida en una caja de valor como la de
            // las perillas (misma altura de fila)
            auto valueArea = r.removeFromBottom (22);
            key.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 6, 56), juce::jmin (r.getHeight() - 4, 34)));
            keyValue.setBounds (valueArea.withSizeKeepingCentre (juce::jmin (valueArea.getWidth() - 4, 70), 18));
        }
    }
    static constexpr int maxDial = 64;

    // Tecla de dos posiciones con LED, que luce en la segunda opción; la opción elegida se lee debajo (keyValue)
    struct KeySwitch : juce::TextButton
    {
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto on = getToggleState();
            const auto travel = down ? 1.5f : 0.0f; // la tecla baja al pulsarla
            auto r = getLocalBounds().toFloat().reduced (2.0f, 1.0f).withTrimmedBottom (2.0f);
            // Sombra en dos pasadas (ancha y tenue, estrecha y densa): más corta con la tecla pulsada
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.5f - travel).expanded (1.0f), 5.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.2f - travel * 0.6f), 4.5f);
            r = r.translated (0.0f, travel);
            // Material del skin (Skin::material), como las perillas: aluminio claro o topacio; si no, la de siempre
            const auto mat = Skin::material();
            const auto topaz = mat == Skin::Material::topaz, bake = mat == Skin::Material::bakelite;
            const auto light = mat == Skin::Material::aluminium || topaz;
            if (bake)
                g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff47453f : 0xff3a3936), 0.0f, r.getY(),
                                                         juce::Colour (0xff070707), 0.0f, r.getBottom(), false));
            else if (topaz)
                g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xffe4a448 : 0xffd08a30), 0.0f, r.getY(),
                                                         juce::Colour (0xff3e1c04), 0.0f, r.getBottom(), false));
            else if (light)
                g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xffeef1f4 : 0xffdde1e5), 0.0f, r.getY(),
                                                         juce::Colour (0xff7f868e), 0.0f, r.getBottom(), false));
            else
                g.setGradientFill (juce::ColourGradient (Skin::c (over ? 0xff5a616b : 0xff515861), 0.0f, r.getY(),
                                                         Skin::c (0xff23272d), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 4.0f);
            if (topaz) // brillo interior de la gema en la mitad baja (la luz sale por el lado opuesto)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc45a).withAlpha (0.4f), r.getCentreX(), r.getBottom() - 3.0f,
                                                         juce::Colour (0xffffc45a).withAlpha (0.0f), r.getCentreX(), r.getCentreY() - 2.0f, false));
                g.fillRoundedRectangle (r, 4.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (light ? 0.5f : bake ? 0.28f : 0.16f)); // canto superior iluminado
            g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 4.0f, r.getRight() - 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            // LED rectangular de 16 × 4 px en el centro (rojo fijo, como los LED de los interruptores)
            const auto led = juce::Rectangle<float> (16.0f, 4.0f).withCentre (r.getCentre());
            if (on)
            {
                g.setColour (juce::Colour (0xffff3030).withAlpha (0.35f));
                g.fillRoundedRectangle (led.expanded (3.0f, 2.5f), 3.0f);
            }
            if (light) // ventana del LED hundida en el aluminio
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (led.expanded (1.2f), 2.0f);
            }
            g.setColour (on ? juce::Colour (0xffff5a4a) : light ? juce::Colour (0xff3a1414) : Skin::c (0xff3a1414));
            g.fillRoundedRectangle (led, 1.5f);
        }
    };
    void updateKeyText()
    {
        const auto text = paramPtr->getText (key.getToggleState() ? 1.0f : 0.0f, 40);
        keyValue.setText (text, juce::dontSendNotification);
        keyValue.setTooltip (text); // el texto entero si la caja lo recorta
    }

    // Burbuja con "Nombre: valor" en 15 px sobre el mando mientras se arrastra (en la ventana de nivel superior, por encima de
    // todo; sigue la escala del editor). Colores fijos de tooltip del skin
    struct Bubble : juce::Component
    {
        juce::String text;
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Skin::c (0xff101316).withAlpha (0.95f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (Skin::accent());
            g.drawRoundedRectangle (r, 6.0f, 1.2f);
            g.setColour (Skin::white());
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (text, r, juce::Justification::centred, false);
        }
    };
    void showBubble (bool on)
    {
        auto* top = getTopLevelComponent();
        if (! on || top == nullptr) { bubble.setVisible (false); if (auto* p = bubble.getParentComponent()) p->removeChildComponent (&bubble); return; }
        if (bubble.getParentComponent() != top) top->addChildComponent (bubble);
        bubble.setInterceptsMouseClicks (false, false);
        bubble.text = name + ":  " + slider.getTextFromValue (slider.getValue());
        const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)), bubble.text)) + 24;
        const auto area = top->getLocalArea (this, slider.getBounds());
        bubble.setBounds (juce::Rectangle<int> (w, 28).withCentre ({ area.getCentreX(), area.getY() - 18 }).constrainedWithin (top->getLocalBounds()));
        bubble.setVisible (true);
        bubble.toFront (false);
        bubble.repaint();
    }
    Bubble bubble;

    float layoutWeight() const { return style == Style::rotary ? 1.0f : 0.9f; } // slider: su caja de valor ("300 ms") necesita ~50 px

    // Anillo de modulación (matriz, UI/MatrixUi.h): rango [lo, hi] y valor en vivo (normalizados; live < 0 = sin punto)
    void setRing (bool on, float lo, float hi, float live)
    {
        if (on == ringOn && std::abs (lo - ringLo) < 1.0e-3f && std::abs (hi - ringHi) < 1.0e-3f && std::abs (live - ringLive) < 1.0e-3f)
            return;
        ringOn = on; ringLo = lo; ringHi = hi; ringLive = live;
        repaint();
    }

    // Lámpara piloto de una fuente que mueve este mando: su color y su brillo (0..1, el valor de la fuente ahora)
    struct Lamp { juce::Colour colour; float glow; };
    // Un punto por voz que suena (normalizados; sustituyen al punto único de setRing) y las lámparas de sus fuentes
    void setModView (std::vector<float> dots, std::vector<Lamp> sources)
    {
        auto same = dots.size() == voiceDots.size() && sources.size() == lamps.size();
        for (size_t i = 0; same && i < dots.size(); ++i) same = std::abs (dots[i] - voiceDots[i]) < 1.0e-3f;
        for (size_t i = 0; same && i < sources.size(); ++i) same = sources[i].colour == lamps[i].colour && std::abs (sources[i].glow - lamps[i].glow) < 0.02f;
        if (same) return;
        voiceDots = std::move (dots);
        lamps = std::move (sources);
        lampHit.setVisible (! lamps.empty());
        layoutLamps();
        repaint();
    }

    // Fuentes de la matriz arrastradas desde Mod Sources ("modsrc:N"): solo los mandos que son destino
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith ("modsrc:") && MatrixUi::destinationOf (paramId) > 0;
    }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHover = false;
        repaint();
        const auto src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
        const auto r = MatrixUi::addRoute (apvts, src, MatrixUi::destinationOf (paramId));
        if (r == MatrixUi::routeExists)
            showMessage (ModMatrix::sourceNames()[src] + " already moves " + name);
        else if (r == MatrixUi::routesFull)
            showMessage ("All 8 matrix slots in use");
    }

    // Aviso breve en la burbuja (1,6 s)
    void showMessage (const juce::String& text)
    {
        showBubble (true);
        bubble.text = text;
        const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)), text)) + 24;
        bubble.setSize (w, bubble.getHeight());
        bubble.repaint();
        juce::Timer::callAfterDelay (1600, [safe = juce::Component::SafePointer<Knob> (this)] { if (safe != nullptr) safe->showBubble (false); });
    }

    // Abre la tira de atenuadores de las fuentes de este mando (definida tras AttenuatorStrip)
    void openAttenuators();

    // Arco ámbar por fuera del arco de valor, con la geometría de LookAndFeel_V4::drawRotarySlider tal como lo llama
    // KoniLookAndFeel (rectángulo ampliado 8 px y reducido 10: neto, 2 px por lado). Encima: un punto por voz que suena y,
    // en el hueco inferior del dial (los 90° sin recorrido), las lámparas piloto de las fuentes, que brillan con su valor
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropHover) // una fuente arrastrada encima: se puede soltar aquí
        {
            g.setColour (Skin::accent().withAlpha (0.18f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f);
            g.setColour (Skin::accent());
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 1.2f);
        }
        if (! ringOn || style != Style::rotary)
            return;
        const auto area = slider.getBounds().withTrimmedBottom (18).toFloat().reduced (2.0f);
        const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto lineW = juce::jmin (8.0f, radius * 0.5f);
        const auto ringR = radius + 1.5f;
        const auto rp = slider.getRotaryParameters();
        const auto angle = [&rp] (float v) { return rp.startAngleRadians + v * (rp.endAngleRadians - rp.startAngleRadians); };
        const auto c = area.getCentre();
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, angle (ringLo), angle (juce::jmax (ringHi, ringLo + 0.004f)), true);
        g.setColour (Skin::c (0xffffb000).withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (juce::jmax (2.0f, lineW * 0.35f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto dot = [&] (float v)
        {
            const auto a = angle (v) - juce::MathConstants<float>::halfPi;
            g.setColour (Skin::white());
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ c.x + ringR * std::cos (a), c.y + ringR * std::sin (a) }));
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ c.x + ringR * std::cos (a), c.y + ringR * std::sin (a) }), 0.6f);
        };
        if (! voiceDots.empty())
            for (const auto v : voiceDots) dot (v);
        else if (ringLive >= 0.0f)
            dot (ringLive);
        for (size_t i = 0; i < lamps.size(); ++i) // lámparas: halo según el brillo, cuerpo del color y reflejo
        {
            const auto r = lampRect ((int) i);
            const auto glow = juce::jlimit (0.0f, 1.0f, lamps[i].glow);
            g.setColour (lamps[i].colour.withAlpha (0.15f + 0.45f * glow));
            g.fillEllipse (r.expanded (1.5f + 2.5f * glow));
            g.setColour (lamps[i].colour.withMultipliedBrightness (0.45f + 0.55f * glow));
            g.fillEllipse (r);
            g.setColour (juce::Colours::white.withAlpha (0.25f + 0.5f * glow));
            g.fillEllipse (r.reduced (r.getWidth() * 0.3f).translated (-0.8f, -0.8f));
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (r, 0.7f);
        }
    }

    // Lámpara i: 8 px, separadas 11, centradas en el hueco inferior del dial (0,8 del radio bajo el centro)
    juce::Rectangle<float> lampRect (int i) const
    {
        const auto area = slider.getBounds().withTrimmedBottom (18).toFloat().reduced (2.0f);
        const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto n = (float) lamps.size();
        const auto x = area.getCentreX() + ((float) i - (n - 1.0f) * 0.5f) * 11.0f;
        return juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, area.getCentreY() + radius * 0.8f });
    }
    void layoutLamps()
    {
        if (lamps.empty()) return;
        const auto a = lampRect (0), b = lampRect ((int) lamps.size() - 1);
        lampHit.setBounds (a.getUnion (b).expanded (4.0f, 5.0f).getSmallestIntegerContainer());
        lampHit.toFront (false);
    }

    // Zona de clic de las lámparas (encima del dial): abre la tira de atenuadores
    struct LampHit : juce::Component, juce::SettableTooltipClient
    {
        std::function<void()> onClick;
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu() && contains (e.getPosition()) && onClick) onClick(); }
    };

    APVTS& apvts;
    Style style;
    juce::Slider slider;
    juce::Label label;
    APVTS::SliderAttachment attachment; // declarado tras slider: orden de construcción
    ParamMenu menu;
    juce::String name;
    MidiLearn& learn;
    int paramIndex;
    juce::String paramId;
    bool ringOn = false, dropHover = false;
    float ringLo = 0.0f, ringHi = 0.0f, ringLive = -1.0f;
    std::vector<float> voiceDots;
    std::vector<Lamp> lamps;
    LampHit lampHit;
    juce::RangedAudioParameter* paramPtr = nullptr;
    int options = 0;      // opciones de una elección (0 = valor numérico)
    KeySwitch key;        // solo con 2 opciones
    juce::Label keyValue; // la opción elegida, bajo la tecla
    std::unique_ptr<APVTS::ButtonAttachment> keyAttachment; // tras key: muere antes
    std::unique_ptr<ParamMenu> keyMenu;
};

// Tira de atenuadores de un mando destino: un fader vertical bipolar por cada ranura de la matriz que lo mueve (su cantidad,
// mtxAmtN, ±100 %), rotulado y teñido con el color de la fuente, y "Remove" debajo. Se rehace si cambian las ranuras (5 Hz)
// y se cierra cuando ya no queda ninguna. Se abre en un CallOutBox desde las lámparas del mando (Knob::openAttenuators)
struct AttenuatorStrip : juce::Component, private juce::Timer
{
    AttenuatorStrip (APVTS& s, int dest, const juce::String& knobName) : apvts (s), destination (dest)
    {
        title.setText (knobName + ": attenuators", juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (title);
        rebuild();
        startTimerHz (5);
    }
    ~AttenuatorStrip() override { setLookAndFeel (nullptr); } // el que le da Knob::openAttenuators no es suyo

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        title.setBounds (r.removeFromTop (20));
        for (size_t i = 0; i < faders.size(); ++i)
        {
            auto col = r.removeFromLeft (84);
            removes[i]->setBounds (col.removeFromBottom (22).reduced (4, 0));
            faders[i]->setBounds (col);
        }
    }

private:
    juce::String signature() const
    {
        juce::String sig;
        for (const auto slot : MatrixUi::slotsFor (apvts, destination))
            sig << slot << ":" << (int) MatrixUi::slotValue (apvts, "mtxSrc", slot) << ";";
        return sig;
    }

    void rebuild()
    {
        faders.clear();
        removes.clear();
        shown = signature();
        for (const auto slot : MatrixUi::slotsFor (apvts, destination))
        {
            const auto src = (int) MatrixUi::slotValue (apvts, "mtxSrc", slot);
            auto& k = faders.emplace_back (std::make_unique<Knob> (apvts, "mtxAmt" + juce::String (slot + 1), ModMatrix::sourceNames()[src],
                                                                   Knob::Style::vertical));
            k->label.setColour (juce::Label::textColourId, MatrixUi::sourceColour (src));
            addAndMakeVisible (*k);
            auto& b = removes.emplace_back (std::make_unique<juce::TextButton> ("Remove"));
            b->setTooltip ("Remove this modulation (empties its matrix slot).");
            b->onClick = [this, slot] { MatrixUi::removeSlot (apvts, slot); timerCallback(); };
            addAndMakeVisible (*b);
        }
        setSize (16 + 84 * juce::jmax (1, (int) faders.size()), 300);
        resized();
    }

    void timerCallback() override
    {
        if (signature() == shown)
            return;
        if (MatrixUi::slotsFor (apvts, destination).empty())
        {
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
            return;
        }
        rebuild();
    }

    APVTS& apvts;
    const int destination;
    juce::Label title;
    juce::String shown;
    std::vector<std::unique_ptr<Knob>> faders;
    std::vector<std::unique_ptr<juce::TextButton>> removes;
};

inline void Knob::openAttenuators()
{
    const auto destination = MatrixUi::destinationOf (paramId);
    auto* top = getTopLevelComponent();
    if (destination <= 0 || top == nullptr || MatrixUi::slotsFor (apvts, destination).empty())
        return;
    // Con el LookAndFeel de este mando (el del editor: faders de consola): la ventana de nivel superior del standalone tiene
    // el de JUCE por defecto y la tira lo heredaría. setLookAndFeel no la posee: KoniLookAndFeel vive mientras el editor
    auto strip = std::make_unique<AttenuatorStrip> (apvts, destination, name);
    strip->setLookAndFeel (&getLookAndFeel());
    juce::CallOutBox::launchAsynchronously (std::move (strip), top->getLocalArea (this, lampHit.getBounds()), top);
}

// Lista de valores con nombre para un parámetro entero, en la fila de controles (Program del MT-32: "1 - Acou Piano 1"):
// etiqueta arriba como en las perillas y un desplegable de una línea. Un elemento por paso del parámetro; los textos se
// cambian con setItemTexts sin tocar la selección. Mismo menú contextual (Reset, valor, MIDI Learn) y ayuda que una perilla
struct ListControl : juce::Component, private juce::Timer
{
    ListControl (APVTS& s, const juce::String& id, const juce::String& text, float weight)
        : name (text),
          learn (dynamic_cast<KoniSynthProcessor&> (s.processor).midiLearn),
          paramIndex (s.getParameter (id)->getParameterIndex()),
          paramId (id),
          width (weight)
    {
        auto* p = s.getParameter (id);
        const auto steps = juce::jmax (2, p->getNumSteps());
        for (int i = 0; i < steps; ++i) // los items deben existir antes del attachment
            combo.addItem (p->getText ((float) i / (float) (steps - 1), 32), i + 1);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (s, id, combo);
        menu = std::make_unique<ParamMenu> (s, id, combo);
        const auto tip = paramTooltip (s, id);
        combo.setTooltip (tip);
        label.setTooltip (tip);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (combo);
        addAndMakeVisible (label);
        startTimerHz (5); // refleja asignaciones de MIDI Learn hechas desde el hilo de audio
    }

    void setItemTexts (const juce::StringArray& texts)
    {
        // El elegido se lee antes: getSelectedId/getSelectedItemIndex dan 0/-1 en cuanto el texto visible deja de coincidir
        // con el del elemento, y changeItemText cambia el elemento pero no el texto visible (juce_ComboBox.cpp)
        const auto selectedId = combo.getSelectedId();
        for (int i = 0; i < juce::jmin (texts.size(), combo.getNumItems()); ++i)
            if (combo.getItemText (i) != texts[i])
                combo.changeItemText (i + 1, texts[i]);
        if (selectedId != 0)
            combo.setSelectedId (selectedId, juce::dontSendNotification);
        resized(); // el ancho sigue al texto más largo
        if (auto* parent = getParentComponent()) parent->resized();
    }

    void timerCallback() override
    {
        juce::String text = name;
        if (learn.learning.load() == paramIndex)
            text = "Learn...";
        else if (const auto cc = learn.ccFor (paramIndex); cc >= 0)
            text << " [" << cc << "]";
        if (label.getText() != text)
            label.setText (text, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (18));
        combo.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 8, preferredWidth()), juce::jmin (26, r.getHeight())));
    }

    // Ancho del texto más largo de la lista más la flecha y los márgenes del ComboBox (~34 px)
    int preferredWidth() const
    {
        const auto font = getLookAndFeel().getComboBoxFont (const_cast<juce::ComboBox&> (combo));
        float widest = 0.0f;
        for (int i = 0; i < combo.getNumItems(); ++i)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, combo.getItemText (i)));
        return juce::roundToInt (widest) + 34;
    }

    // Peso en la fila: el de 'width' perillas o lo que pide el texto más largo (60 px por unidad, como layoutWeight de Section)
    float layoutWeight() const { return juce::jmax (width, (float) preferredWidth() / 60.0f); }

    juce::ComboBox combo;
    juce::Label label;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment; // tras combo: muere antes
    std::unique_ptr<ParamMenu> menu;
    juce::String name;
    MidiLearn& learn;
    int paramIndex;
    juce::String paramId;
    float width;
};

// UI: aspecto. Interruptores con LED rojo (encendido: rojo con halo; apagado: granate oscuro).
struct KoniLookAndFeel : juce::LookAndFeel_V4
{
    KoniLookAndFeel() { applySkin(); }

    // Esquema de colores de JUCE y los propios en el skin actual (UI/Skin.h); el editor lo llama al cambiar de skin
    void applySkin()
    {
        setColourScheme (Skin::scheme());
        setColour (juce::TooltipWindow::backgroundColourId, Skin::c (0xff1b1f24));
        setColour (juce::TooltipWindow::textColourId, Skin::white());
        setColour (juce::TooltipWindow::outlineColourId, Skin::c (0xff42a2c8));
    }

    // LookAndFeel_V4 reduce 10 px por lado el área de la perilla (juce_LookAndFeel_V4.cpp:1079): con ~40 px de alto solo quedaba
    // un punto. Aquí el margen es de 2 px (se amplía el rectángulo 8 px antes de llamar al original)
    // Perilla dibujada (escala con la ventana; tono original en todos los skins). De fuera a dentro, sobre el radio r:
    // - arco de valor fino (trazo 0,1·r, entre 2 y 3,5 px) en el borde; el anillo ámbar de la matriz queda fuera de él;
    // - hueco libre hasta el cuerpo (0,62·r): el cuerpo no toca el arco;
    // - faldón con estrías y tapa con indicador. Faldón, estrías e indicador giran con el valor; la luz es fija (arriba a la
    //   izquierda): cada estría se ilumina según su ángulo absoluto, así que al girar pasan por la luz como en una pieza real
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float startAngle,
                           float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x - 8, y - 8, width + 16, height + 16).toFloat().reduced (10.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        if (radius < 4.0f)
            return;
        const auto c = bounds.getCentre();
        const auto lineW = juce::jlimit (2.0f, 3.5f, radius * 0.1f);
        const auto arcR = radius - lineW * 0.5f;
        const auto angle = startAngle + sliderPos * (endAngle - startAngle);
        const auto enabled = slider.isEnabled();
        const auto bodyR = radius * 0.62f;
        const auto pi = juce::MathConstants<float>::pi;
        const auto halfPi = juce::MathConstants<float>::halfPi;
        const auto lightA = -0.75f * pi; // dirección de la luz (pantalla, y hacia abajo): arriba a la izquierda

        const int detents = slider.getProperties().getWithDefault ("detents", 0);
        if (detents > 0)
        {
            // Conmutador por pasos (Knob: elecciones de 3 a 10 opciones): una marca radial por posición en el hueco, en lugar
            // del arco; la elegida, en el acento con halo
            const auto chosen = juce::roundToInt (sliderPos * (float) (detents - 1));
            const auto r0 = bodyR + (radius - bodyR) * 0.4f;
            for (int k = 0; k < detents; ++k)
            {
                const auto a = startAngle + (endAngle - startAngle) * (float) k / (float) (detents - 1) - halfPi;
                const juce::Point<float> p0 { c.x + std::cos (a) * r0, c.y + std::sin (a) * r0 },
                                         p1 { c.x + std::cos (a) * radius, c.y + std::sin (a) * radius };
                if (k == chosen && enabled)
                {
                    g.setColour (Skin::accent().withAlpha (0.3f));
                    g.drawLine ({ p0, p1 }, 4.5f);
                }
                g.setColour (k == chosen && enabled ? Skin::accent() : Skin::white().withAlpha (0.35f));
                g.drawLine ({ p0, p1 }, k == chosen ? 2.2f : 1.4f);
            }
        }
        else
        {
            // Arco: pista hundida y valor con halo
            const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            juce::Path track;
            track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (juce::Colour (0xff0d0f12));
            g.strokePath (track, stroke);
            if (enabled)
            {
                juce::Path value;
                value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
                g.setColour (Skin::accent().withAlpha (0.22f));
                g.strokePath (value, juce::PathStrokeType (lineW * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (Skin::accent());
                g.strokePath (value, stroke);
            }
        }

        if (bodyR < 3.0f)
            return;
        const auto body = juce::Rectangle<float> (2.0f * bodyR, 2.0f * bodyR).withCentre (c);
        {
            // Sombra proyectada hacia abajo (luz alta): desenfoque gaussiano de radio 0,45·r desplazado 0,22·r, y sombra de
            // contacto pegada al borde
            juce::Path disc;
            disc.addEllipse (body);
            juce::DropShadow (juce::Colours::black.withAlpha (0.75f), juce::jmax (2, juce::roundToInt (bodyR * 0.45f)),
                              { 0, juce::jmax (1, juce::roundToInt (bodyR * 0.22f)) }).drawForPath (g, disc);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (body.translated (0.0f, 1.0f).expanded (0.6f));
        }
        // Faldón con luz fija, del material del skin (Skin::material): metal oscuro, aluminio claro, topacio o baquelita
        const auto mat = Skin::material();
        const auto light = mat == Skin::Material::aluminium, topaz = mat == Skin::Material::topaz, bake = mat == Skin::Material::bakelite;
        g.setGradientFill (juce::ColourGradient (juce::Colour (light ? 0xffe2e6ea : topaz ? 0xffb8741e : bake ? 0xff3a3936 : 0xff4a5059), body.getX(), body.getY(),
                                                 juce::Colour (light ? 0xff6f767e : topaz ? 0xff2e1503 : bake ? 0xff050505 : 0xff0e1013), body.getRight(), body.getBottom(), false));
        g.fillEllipse (body);
        if (topaz)
        {
            // Gema: la luz entra por arriba a la izquierda y sale refractada por el lado opuesto: brillo interior cálido
            // desplazado 0,3·r hacia abajo a la derecha, que se apaga antes del borde
            const juce::Point<float> glow { c.x + bodyR * 0.3f, c.y + bodyR * 0.3f };
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc45a).withAlpha (0.55f), glow.x, glow.y,
                                                     juce::Colour (0xffffc45a).withAlpha (0.0f), glow.x + bodyR * 0.9f, glow.y, true));
            g.fillEllipse (body);
        }
        // Estrías: n surcos que giran con el valor; cada uno con un canto de luz cuyo brillo es el coseno entre su ángulo
        // absoluto y la luz (0,03 de alfa a contraluz, 0,19 de frente)
        const auto capR = bodyR * 0.74f;
        const int flutes = bodyR < 14.0f ? 20 : 28;
        const auto step = juce::MathConstants<float>::twoPi / (float) flutes;
        const auto fluteW = juce::jmax (0.7f, bodyR * 0.045f);
        for (int k = 0; k < flutes; ++k)
        {
            const auto a = angle + step * (float) k;
            const auto lit = 0.5f + 0.5f * std::cos (a - lightA);
            const auto ca = std::cos (a), sa = std::sin (a);
            g.setColour ((topaz ? juce::Colour (0xff2a1002) : juce::Colours::black).withAlpha (light ? 0.22f : topaz ? 0.35f : bake ? 0.55f : 0.32f));
            g.drawLine (c.x + ca * (capR + 0.5f), c.y + sa * (capR + 0.5f), c.x + ca * (bodyR - 0.5f), c.y + sa * (bodyR - 0.5f), fluteW);
            const auto b = a + step * 0.28f;
            const auto cb = std::cos (b), sb = std::sin (b);
            g.setColour ((topaz ? juce::Colour (0xfffff0c8) : juce::Colours::white)
                             .withAlpha ((light ? 0.10f : topaz || bake ? 0.04f : 0.03f) + (light ? 0.35f : topaz ? 0.40f : bake ? 0.30f : 0.16f)
                                         * lit * (topaz || bake ? lit : 1.0f)));
            g.drawLine (c.x + cb * (capR + 0.5f), c.y + sb * (capR + 0.5f), c.x + cb * (bodyR - 0.5f), c.y + sb * (bodyR - 0.5f), fluteW * 0.7f);
        }
        g.setColour (bake ? juce::Colour (0xff6a655d) : juce::Colours::black.withAlpha (0.8f)); // baquelita: canto gris cálido
        g.drawEllipse (body, 1.0f);
        {
            // Luz de canto en el borde del faldón, del lado de la luz (±70° alrededor de ella)
            juce::Path rim;
            rim.addCentredArc (c.x, c.y, bodyR - 0.7f, bodyR - 0.7f, 0.0f, lightA + halfPi - 1.22f, lightA + halfPi + 1.22f, true);
            g.setColour (juce::Colours::white.withAlpha (bake ? 0.4f : 0.2f));
            g.strokePath (rim, juce::PathStrokeType (1.0f));
        }
        // Tapa: cóncava (más oscura del lado de la luz que el faldón), con bisel claro arriba a la izquierda y oscuro abajo
        const auto cap = juce::Rectangle<float> (2.0f * capR, 2.0f * capR).withCentre (c);
        if (topaz) // tabla de la gema: oscura del lado de la luz y encendida por dentro del opuesto (refracción)
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6c060), c.x + capR * 0.35f, c.y + capR * 0.4f,
                                                     juce::Colour (0xff4a2205), c.x - capR, c.y - capR, true));
        else if (bake) // inserto de aluminio torneado: claro arriba a la izquierda (convexo, no cóncavo)
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeceef0), cap.getX(), cap.getY(),
                                                     juce::Colour (0xff80868c), cap.getRight(), cap.getBottom(), false));
        else
            g.setGradientFill (juce::ColourGradient (juce::Colour (light ? 0xffa9b0b7 : 0xff2a2e34), cap.getX(), cap.getY(),
                                                     juce::Colour (light ? 0xffeef1f4 : 0xff474d56), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse (cap);
        if (bake) // surcos del torneado: anillos concéntricos alternos, finos
            for (int k = 1; k <= 4; ++k)
            {
                g.setColour (k % 2 ? juce::Colours::black.withAlpha (0.10f) : juce::Colours::white.withAlpha (0.18f));
                g.drawEllipse (cap.reduced (capR * 0.18f * (float) k), 0.6f);
            }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.3f), cap.getX(), cap.getY(),
                                                 juce::Colours::black.withAlpha (0.6f), cap.getRight(), cap.getBottom(), false));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), c.x - capR * 0.35f, c.y - capR * 0.45f,
                                                 juce::Colours::white.withAlpha (0.0f), c.x, c.y + capR * 0.2f, true));
        g.fillEllipse (cap.reduced (capR * 0.12f)); // reflejo fijo: no gira
        if (topaz) // destello especular de la gema: pequeño y nítido, arriba a la izquierda
        {
            const auto spot = juce::Rectangle<float> (capR * 0.42f, capR * 0.26f).withCentre ({ c.x - capR * 0.38f, c.y - capR * 0.44f });
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f), spot.getCentreX(), spot.getCentreY(),
                                                     juce::Colours::white.withAlpha (0.0f), spot.getRight(), spot.getCentreY(), true));
            g.fillEllipse (spot);
        }
        // Indicador: surco oscuro con línea clara, del centro de la tapa al borde del faldón; gira con el valor
        const auto ia = angle - halfPi;
        const juce::Point<float> p0 { c.x + std::cos (ia) * capR * 0.2f, c.y + std::sin (ia) * capR * 0.2f },
                                 p1 { c.x + std::cos (ia) * (bodyR - 1.2f), c.y + std::sin (ia) * (bodyR - 1.2f) };
        // (en aluminio claro, al revés: surco claro y línea negra, que es la que contrasta)
        g.setColour (light || bake ? juce::Colours::white.withAlpha (0.55f) : juce::Colours::black.withAlpha (topaz ? 0.5f : 0.65f));
        g.drawLine ({ p0, p1 }, juce::jmax (2.6f, bodyR * 0.17f));
        const auto mark = light || bake ? juce::Colour (0xff15181b) : topaz ? juce::Colour (0xfffff3dc) : juce::Colours::white;
        g.setColour (enabled ? mark : mark.withAlpha (0.5f));
        g.drawLine ({ p0, p1 }, juce::jmax (1.3f, bodyR * 0.08f));
    }


    // Fader de consola (vertical u horizontal; otros estilos, V4): escala de 11 marcas a cada lado (cada 10 %, más largas en
    // 0, 50 y 100 %), ranura hundida de 3 px con el relleno de valor, y tapa de 18 px con dos caras talladas hacia la línea
    // central (la que mira a la luz, clara; la otra, oscura) y sombra proyectada hacia abajo. La tapa cabe en el recorrido:
    // JUCE deja getSliderThumbRadius (12 px en V4) en cada extremo y media tapa son 9 px
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }
        const auto vertical = style == juce::Slider::LinearVertical;
        const auto enabled = slider.isEnabled();
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto p0 = (float) slider.getPositionOfValue (slider.getMinimum()), p1 = (float) slider.getPositionOfValue (slider.getMaximum());
        // Escala: marcas a ambos lados de la ranura, a 5 px de su eje
        const auto mid = vertical ? area.getCentreX() : area.getCentreY();
        for (int k = 0; k <= 10; ++k)
        {
            const auto pos = p0 + (p1 - p0) * (float) k / 10.0f;
            const auto len = (k % 5 == 0) ? 5.0f : 3.0f;
            g.setColour (juce::Colours::white.withAlpha (k % 5 == 0 ? 0.30f : 0.16f));
            for (int side = -1; side <= 1; side += 2)
            {
                const auto a = mid + (float) side * 5.0f, b = a + (float) side * len;
                if (vertical) g.drawLine (a, pos, b, pos, 1.0f);
                else          g.drawLine (pos, a, pos, b, 1.0f);
            }
        }
        // Ranura hundida: fondo casi negro, sombra interior arriba y luz abajo
        const auto grooveW = 3.0f;
        const auto groove = vertical ? juce::Rectangle<float> (mid - grooveW * 0.5f, area.getY(), grooveW, area.getHeight())
                                     : juce::Rectangle<float> (area.getX(), mid - grooveW * 0.5f, area.getWidth(), grooveW);
        g.setColour (juce::Colour (0xff07090b));
        g.fillRoundedRectangle (groove.expanded (1.0f), 2.5f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (groove.expanded (1.0f).translated (0.0f, -0.5f), 2.5f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (groove.expanded (1.5f).translated (0.0f, 0.8f), 2.5f, 0.8f);
        if (enabled) // relleno desde el mínimo, dentro de la ranura
        {
            const auto fill = vertical ? groove.withTop (sliderPos) : groove.withRight (sliderPos);
            g.setColour (Skin::accent().withAlpha (0.25f)); // halo
            g.fillRoundedRectangle (fill.expanded (2.0f), 2.5f);
            g.setColour (Skin::accent());
            g.fillRoundedRectangle (fill.reduced (vertical ? 0.5f : 0.0f, vertical ? 0.0f : 0.5f), 1.0f);
        }
        // Tapa
        const auto across = juce::jmin ((vertical ? area.getWidth() : area.getHeight()) * 0.85f, 26.0f);
        const auto along = 18.0f;
        const auto cap = juce::Rectangle<float> (vertical ? across : along, vertical ? along : across)
                             .withCentre (vertical ? juce::Point<float> { mid, sliderPos } : juce::Point<float> { sliderPos, mid });
        {
            juce::Path shape;
            shape.addRoundedRectangle (cap, 2.5f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 6, { 0, 4 }).drawForPath (g, shape); // proyectada
            g.setColour (juce::Colours::black.withAlpha (0.55f)); // de contacto
            g.fillRoundedRectangle (cap.translated (0.0f, 1.2f).expanded (0.6f), 2.5f);
        }
        const auto mat = Skin::material(); // material del skin (Skin::material)
        const auto light = mat == Skin::Material::aluminium, topaz = mat == Skin::Material::topaz, bake = mat == Skin::Material::bakelite;
        g.setGradientFill (juce::ColourGradient (juce::Colour (light ? 0xffd2d7dc : topaz ? 0xffc0802a : bake ? 0xff2c2b29 : 0xff4d535b), cap.getX(), cap.getY(),
                                                 juce::Colour (light ? 0xff7c838b : topaz ? 0xff3a1a04 : bake ? 0xff050505 : 0xff1a1d21), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.5f);
        // Caras talladas: mitad hacia la luz (arriba, o izquierda en horizontal) clara y la otra oscura, cada una con
        // degradado hacia la línea central (el fondo del tallado)
        const auto inner = cap.reduced (1.5f);
        auto firstHalf = inner, secondHalf = inner;
        if (vertical) { firstHalf = inner.withBottom (inner.getCentreY()); secondHalf = inner.withTop (inner.getCentreY()); }
        else          { firstHalf = inner.withRight (inner.getCentreX()); secondHalf = inner.withLeft (inner.getCentreX()); }
        const auto grad = [vertical] (juce::Rectangle<float> r, juce::Colour from, juce::Colour to, bool towardsEnd)
        {
            const auto a = towardsEnd ? r.getTopLeft() : (vertical ? r.getBottomLeft() : r.getTopRight());
            const auto b = towardsEnd ? (vertical ? r.getBottomLeft() : r.getTopRight()) : r.getTopLeft();
            return juce::ColourGradient (from, a, to, b, false);
        };
        g.setGradientFill (grad (firstHalf, juce::Colour (light ? 0xfff4f6f8 : topaz ? 0xfff8d68e : bake ? 0xff55524d : 0xff8a919a),
                                 juce::Colour (light ? 0xffb4bac1 : topaz ? 0xffb06a18 : bake ? 0xff1c1b19 : 0xff50565e), true));
        g.fillRoundedRectangle (firstHalf, 1.5f);
        // Topacio: la cara de sombra se enciende hacia el fondo del tallado (la luz atraviesa la gema)
        g.setGradientFill (grad (secondHalf, juce::Colour (light ? 0xff9ba1a8 : topaz ? 0xffe09a36 : bake ? 0xff141312 : 0xff3a3f46),
                                 juce::Colour (light ? 0xff767d85 : topaz ? 0xff5a2a06 : bake ? 0xff060606 : 0xff23272c), false));
        g.fillRoundedRectangle (secondHalf, 1.5f);
        // Luz en el canto superior y contorno
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawHorizontalLine (juce::roundToInt (cap.getY() + 0.5f), cap.getX() + 2.5f, cap.getRight() - 2.5f);
        g.setColour (bake ? juce::Colour (0xff6a655d) : juce::Colours::black.withAlpha (0.75f)); // baquelita: canto gris cálido
        g.drawRoundedRectangle (cap, 2.5f, 0.9f);
        // Línea central: surco oscuro con el trazo blanco
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        if (vertical) g.fillRect (juce::Rectangle<float> (inner.getWidth(), 2.4f).withCentre (cap.getCentre()));
        else          g.fillRect (juce::Rectangle<float> (2.4f, inner.getHeight()).withCentre (cap.getCentre()));
        g.setColour ((light ? juce::Colour (0xff15181b) : topaz ? juce::Colour (0xfffff3dc) : juce::Colours::white)
                     .withAlpha (enabled ? 0.92f : 0.45f)); // aluminio: trazo negro
        if (vertical) g.fillRect (juce::Rectangle<float> (inner.getWidth() - 2.0f, 1.0f).withCentre (cap.getCentre()));
        else          g.fillRect (juce::Rectangle<float> (1.0f, inner.getHeight() - 2.0f).withCentre (cap.getCentre()));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat();
        const auto d = juce::jmin (12.0f, r.getHeight() - 6.0f);
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + 4.0f + d * 0.5f, r.getCentreY() });
        const auto on = b.getToggleState();

        // Phosphor Green, Amber y Vintage: el LED va engastado en un bisel (aluminio claro, topacio o cromado), de luz arriba
        // a la izquierda
        if (const auto mat = Skin::material(); mat != Skin::Material::darkMetal)
        {
            const auto bezel = led.expanded (2.2f);
            const auto topaz = mat == Skin::Material::topaz; // aluminio y baquelita: bisel cromado
            g.setGradientFill (juce::ColourGradient (juce::Colour (topaz ? 0xfff2c470 : 0xffeef1f4), bezel.getX(), bezel.getY(),
                                                     juce::Colour (topaz ? 0xff4a2205 : 0xff6f767e), bezel.getRight(), bezel.getBottom(), false));
            g.fillEllipse (bezel);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (bezel, 0.8f);
        }
        // LED rojo fijo: no sigue el skin (el contraste del rojo sobre los skins monocromos)
        if (on)
        {
            g.setColour (juce::Colour (0xffff3030).withAlpha (0.35f));
            g.fillEllipse (led.expanded (3.0f)); // halo
        }
        g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xffff8a80) : juce::Colour (0xff5a1e1e),
                                                 led.getX() + d * 0.3f, led.getY() + d * 0.3f,
                                                 on ? juce::Colour (0xffd01010) : juce::Colour (0xff2a0c0c),
                                                 led.getRight(), led.getBottom(), true));
        g.fillEllipse (led);
        g.setColour (Skin::black().withAlpha (0.6f));
        g.drawEllipse (led, 1.0f);

        g.setColour (b.findColour (juce::ToggleButton::textColourId).withAlpha (b.isEnabled() ? (highlighted || down ? 1.0f : 0.85f) : 0.4f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (d + 10.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    }

    // Marco de sección como panel en relieve (antes, la línea de 1 px de V4 cortada por el título): sombra proyectada hacia
    // abajo, cara con degradado vertical del skin (Skin::panelFace; en Brushed Metal, con vetas y un brillo diagonal de
    // metal pulido), banda de título de 19 px algo más clara, chaflán claro 1 px por dentro del canto, luz en el canto
    // superior y contorno oscuro; dos tornillos en las esquinas de la banda y el título tras el izquierdo. Todo dentro de los límites del componente: 1,5 px de margen y 5 px de sombra abajo
    // (el borde inferior del panel coincide con el de los displays a ras, Section::resized). La sombra son 5 capas
    // apiladas y no un desenfoque: el marco se repinta bajo cada perilla que cambia y DropShadow difuminaría el panel entero
    void drawGroupComponentOutline (juce::Graphics& g, int width, int height, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent& group) override
    {
        const auto card = juce::Rectangle<float> ((float) width, (float) height).reduced (1.5f).withTrimmedBottom (3.5f);
        constexpr float radius = 7.0f;
        for (int k = 5; k >= 1; --k) // capa k: 0,9·k px más abajo, 0,3·k px más ancha, 9 % de negro (acumulado ≈ 38 %)
        {
            g.setColour (juce::Colours::black.withAlpha (0.09f));
            g.fillRoundedRectangle (card.translated (0.0f, 0.9f * (float) k).expanded (0.3f * (float) k, 0.0f), radius + 0.5f * (float) k);
        }
        g.setColour (juce::Colours::black.withAlpha (0.3f)); // de contacto
        g.fillRoundedRectangle (card.translated (0.0f, 0.8f), radius);
        const auto face = Skin::panelFace();
        g.setGradientFill (juce::ColourGradient (face.top.withAlpha (0.94f), 0.0f, card.getY(), face.bottom.withAlpha (0.94f), 0.0f, card.getBottom(), false));
        g.fillRoundedRectangle (card, radius);
        if (Skin::current() == Skin::metal)
        {
            // Vetas del cepillado (las del fondo de ventana) y brillo de metal pulido: banda diagonal blanca al 12 % en el
            // 40 % del recorrido de la esquina superior izquierda a la inferior derecha
            g.setTiledImageFill (Skin::brushedTile(), 0, 0, 1.0f);
            g.fillRoundedRectangle (card, radius);
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), card.getTopLeft(), juce::Colours::white.withAlpha (0.0f), card.getBottomRight(), false);
            sheen.addColour (0.4, juce::Colours::white.withAlpha (0.12f));
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (card, radius);
        }
        if (Skin::current() == Skin::vintage)
        {
            // Placa de aluminio anodizado en negro: vetas del cepillado (las de Brushed Metal, que sobre el grafito quedan
            // en ±5 % de luz) y un brillo diagonal tenue (blanco al 6 % en el 35 % del recorrido), más suave que el pulido de metal
            g.setTiledImageFill (Skin::brushedTile(), 0, 0, 1.0f);
            g.fillRoundedRectangle (card, radius);
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), card.getTopLeft(), juce::Colours::white.withAlpha (0.0f), card.getBottomRight(), false);
            sheen.addColour (0.35, juce::Colours::white.withAlpha (0.06f));
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (card, radius);
            // Filete de aluminio cepillado en el canto superior (1,5 px, entre los radios de las esquinas; sombra de 0,8 px
            // debajo), como el de los paneles de módulos de finales de los 60. En el canto y no bajo la banda de título: los
            // controles de cabecera empiezan a 20 px (Section::resized) y una línea bajo la banda de título los cruzaba
            const auto y = card.getY() + 0.8f;
            const auto x0 = card.getX() + radius, x1 = card.getRight() - radius;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd2d5d9), x0, y, juce::Colour (0xff8a8e93), x1, y, false));
            g.fillRect (juce::Rectangle<float> (x0, y, x1 - x0, 1.5f));
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (x0, y + 1.5f, x1 - x0, 0.8f));
        }
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (card.withHeight (19.0f).toNearestInt());
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.fillRoundedRectangle (card, radius);
        }
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine (juce::roundToInt (card.getY() + 1.0f), card.getX() + radius, card.getRight() - radius);
        // Chaflán: línea clara 1 px por dentro del canto, de 14 % arriba a nada abajo
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), 0.0f, card.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, card.getBottom(), false));
        g.drawRoundedRectangle (card.reduced (1.5f), radius - 1.5f, 0.8f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (card, radius, 1.0f);
        // Tornillos de cabeza ranurada (Ø 7 px) en las esquinas de la banda de título, con la ranura a 35° y -20°. Acero
        // blanco azulado en todos los skins (sin Skin::c), como el cuerpo de las perillas
        for (const auto [cx, slot] : { std::pair<float, float> { card.getX() + 9.5f, 0.61f }, { card.getRight() - 9.5f, -0.35f } })
        {
            const auto head = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ cx, card.getY() + 9.5f });
            g.setColour (juce::Colours::black.withAlpha (0.45f)); // avellanado: sombra arriba, luz abajo
            g.fillEllipse (head.expanded (1.0f).translated (0.0f, -0.4f));
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawEllipse (head.expanded (0.8f).translated (0.0f, 0.5f), 0.6f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8f0fa), head.getX() + 1.5f, head.getY() + 1.0f, juce::Colour (0xff66778c), head.getRight(), head.getBottom(), true));
            g.fillEllipse (head);
            const juce::Point<float> d { 2.6f * std::cos (slot), 2.6f * std::sin (slot) };
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawLine ({ head.getCentre() - d, head.getCentre() + d }, 1.2f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawLine ({ head.getCentre() - d + juce::Point<float> { 0.0f, 0.8f }, head.getCentre() + d + juce::Point<float> { 0.0f, 0.8f } }, 0.5f);
        }
        if (text.isNotEmpty())
        {
            g.setColour (group.findColour (juce::GroupComponent::textColourId).withAlpha (group.isEnabled() ? 0.95f : 0.45f));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (text, card.withHeight (19.0f).withTrimmedLeft (19.0f).toNearestInt(), juce::Justification::centredLeft, true);
        }
    }

    // Desplegable de forma de onda (propiedad waveGlyphs, WaveGlyph::decorate): icono de la opción elegida a la izquierda
    // del texto, en una franja de 30 px
    void drawComboBox (juce::Graphics& g, int width, int height, bool down, int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override
    {
        LookAndFeel_V4::drawComboBox (g, width, height, down, buttonX, buttonY, buttonW, buttonH, box);
        if (box.getProperties().contains ("waveGlyphs"))
            WaveGlyph::draw (g, box.getText(), juce::Rectangle<float> (7.0f, 0.0f, 22.0f, (float) height).reduced (0.0f, (float) height * 0.3f),
                             Skin::accent().withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        LookAndFeel_V4::positionComboBoxText (box, label);
        if (box.getProperties().contains ("waveGlyphs"))
            label.setBounds (label.getBounds().withTrimmedLeft (28));
    }

    // Opción de menú con icono (formas de onda): franja de 34 px para el icono en vez del cuadrado de una línea de V4, y la
    // opción elegida con el texto en el acento (el icono ocupa el sitio de la marca). Sin icono, como V4
    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override
    {
        if (icon == nullptr || isSeparator)
        {
            LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu, text, shortcutKeyText, icon, textColour);
            return;
        }
        auto r = area.reduced (1);
        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRect (r);
        }
        icon->drawWithin (g, r.removeFromLeft (34).reduced (5, 0).toFloat().reduced (0.0f, (float) r.getHeight() * 0.22f),
                          juce::RectanglePlacement::stretchToFit, isActive ? 1.0f : 0.5f);
        auto colour = isTicked ? Skin::accent() : findColour (isHighlighted ? juce::PopupMenu::highlightedTextColourId : juce::PopupMenu::textColourId);
        g.setColour (colour.withMultipliedAlpha (isActive ? 1.0f : 0.5f));
        g.setFont (getPopupMenuFont().withHeight (juce::jmin ((float) r.getHeight() / 1.3f, 17.0f)));
        g.drawFittedText (text, r.withTrimmedRight (3), juce::Justification::centredLeft, 1);
    }

    // Caja de valor de perillas y faders: hundida (fondo oscuro, sombra interior arriba, luz abajo) en vez del contorno de V4.
    // Las demás etiquetas y la caja mientras se edita, como V4
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if ((dynamic_cast<juce::Slider*> (label.getParentComponent()) == nullptr && ! label.getProperties().contains ("valueBox"))
            || label.isBeingEdited())
        {
            LookAndFeel_V4::drawLabel (g, label);
            return;
        }
        const auto r = label.getLocalBounds().toFloat().reduced (1.0f, 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 0.5f), r.getX() + 3.0f, r.getRight() - 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (juce::roundToInt (r.getBottom() - 1.0f), r.getX() + 3.0f, r.getRight() - 3.0f);
        const auto colour = label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f);
        const auto* parent = label.getParentComponent();
        if ((label.getProperties().contains ("waveGlyphs") || (parent != nullptr && parent->getProperties().contains ("waveGlyphs")))
            && WaveGlyph::has (label.getText()))
        {
            // Elección de forma de onda: el trazo en lugar del nombre (el nombre sigue en el tooltip y en el menú Set value)
            WaveGlyph::draw (g, label.getText(), r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 8.0f, 30.0f), r.getHeight() * 0.56f), colour);
            return;
        }
        g.setColour (colour);
        g.setFont (getLabelFont (label));
        g.drawFittedText (label.getText(), r.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
};

// UI: sección con marco, interruptor On opcional, selector opcional y N controles (perillas y sliders).
// Todos los controles llevan tooltip y menú contextual de parámetro.
// ComboBox que puede mostrar el desplegable en orden alfabético sin reordenar sus items: ComboBoxAttachment trabaja con
// índices, así que el valor del parámetro no cambia. "Off" (si es el primero) se queda arriba.
// Con itemTooltips, cada opción del desplegable lleva su propio tooltip y el del control añade el de la opción elegida.
struct SortableComboBox : juce::ComboBox
{
    bool alphabetical = false;
    juce::StringArray itemTooltips; // por índice de item; vacío = sin tooltips por opción
    std::vector<int> displayOrder;  // orden del desplegable (índices de item); vacío = el de los items

    juce::String getTooltip() override
    {
        const auto base = juce::ComboBox::getTooltip();
        const auto i = getSelectedItemIndex();
        return juce::isPositiveAndBelow (i, itemTooltips.size()) && itemTooltips[i].isNotEmpty()
                   ? base + "\n\n" + getItemText (i) + ": " + itemTooltips[i] : base;
    }

    // Opción del desplegable con tooltip: dibujada con el LookAndFeel como una normal; el clic la elige (PopupMenu)
    struct TooltipItem : juce::PopupMenu::CustomComponent, juce::SettableTooltipClient
    {
        TooltipItem (const juce::String& t, const juce::String& tip, bool tick) : text (t), ticked (tick) { setTooltip (tip); }

        void getIdealSize (int& w, int& h) override { getLookAndFeel().getIdealPopupMenuItemSize (text, false, -1, w, h); }

        void paint (juce::Graphics& g) override
        {
            getLookAndFeel().drawPopupMenuItem (g, getLocalBounds(), false, true, isItemHighlighted(), ticked, false, text, {}, nullptr, nullptr);
        }

        juce::String text;
        bool ticked;
    };

    void showPopup() override
    {
        if (! alphabetical && itemTooltips.isEmpty() && displayOrder.empty())
        {
            juce::ComboBox::showPopup();
            return;
        }
        std::vector<int> order;
        for (int i = 0; i < getNumItems(); ++i)
            order.push_back (i);
        if (! displayOrder.empty()) // orden dado (índices de item); los que falten, al final en su orden
        {
            order = displayOrder;
            for (int i = 0; i < getNumItems(); ++i)
                if (std::find (order.begin(), order.end(), i) == order.end())
                    order.push_back (i);
            order.erase (std::remove_if (order.begin(), order.end(), [this] (int i) { return ! juce::isPositiveAndBelow (i, getNumItems()); }),
                         order.end());
        }
        const auto first = getNumItems() > 0 && getItemText (0) == "Off" ? 1 : 0;
        // Las ranuras de usuario ("User N") van al final, en su orden, detrás de las de fábrica ya ordenadas
        const auto isUser = [this] (int i) { return getItemText (i).startsWith ("User "); };
        if (alphabetical)
            std::stable_sort (order.begin() + first, order.end(), [this, &isUser] (int a, int b)
                              {
                                  if (isUser (a) != isUser (b)) return isUser (b);
                                  return ! isUser (a) && getItemText (a).compareNatural (getItemText (b)) < 0;
                              });
        juce::PopupMenu m;
        for (const auto i : order)
        {
            const auto ticked = getItemId (i) == getSelectedId();
            if (juce::isPositiveAndBelow (i, itemTooltips.size()) && itemTooltips[i].isNotEmpty())
                m.addCustomItem (getItemId (i), std::make_unique<TooltipItem> (getItemText (i), itemTooltips[i], ticked), nullptr, getItemText (i));
            else
                m.addItem (getItemId (i), getItemText (i), true, ticked);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                                                   .withItemThatMustBeVisible (getSelectedId()),
                         [safe = SafePointer<SortableComboBox> (this)] (int id)
                         {
                             if (safe == nullptr)
                                 return;
                             // hidePopup() limpia ComboBox::menuActive (lo activa showPopupIfNotActive). Sin esto, tras el
                             // primer despliegue el combo ignoraba clics y rueda hasta reiniciar (juce_ComboBox.cpp:495, 604)
                             safe->hidePopup();
                             if (id != 0)
                                 safe->setSelectedId (id);
                         });
    }
};

// Desplegable unido a un parámetro de elección en el que la elección del usuario llama a onPick (p. ej. cargar el preset
// representativo de un motor) además de escribir el parámetro. Los cambios externos (preset, host, automatización,
// MIDI Learn, Reset) solo actualizan el desplegable, sin onPick: así cargar un preset no encadena otra carga.
struct PickComboAttachment
{
    PickComboAttachment (juce::RangedAudioParameter& p, juce::ComboBox& c, std::function<void (int)> onPick)
        : combo (c), pick (std::move (onPick)),
          attachment (p, [this] (float v) { combo.setSelectedId (juce::roundToInt (v) + 1, juce::dontSendNotification); })
    {
        // Por id (índice del parámetro + 1), no por posición: el desplegable puede tener otro orden y cabeceras
        combo.onChange = [this]
        {
            const auto id = combo.getSelectedId();
            if (id <= 0)
                return;
            attachment.setValueAsCompleteGesture ((float) (id - 1));
            pick (id - 1);
        };
        attachment.sendInitialUpdate();
    }

    ~PickComboAttachment() { combo.onChange = nullptr; }

    juce::ComboBox& combo;
    std::function<void (int)> pick;
    juce::ParameterAttachment attachment;
};

struct Section : juce::Component
{
    explicit Section (const juce::String& title)
    {
        group.setText (title);
        group.setInterceptsMouseClicks (false, false); // el clic derecho en el marco llega a la sección (mouseDown)
        addAndMakeVisible (group);
        modFrame.setInterceptsMouseClicks (false, false);
        addChildComponent (modFrame); // tras el marco y antes de los mandos: queda debajo de ellos
    }

    // Clic derecho en el marco o en un hueco de la sección: "Reset <título> to defaults" con los parámetros de sus mandos y de
    // sus diálogos (withResetSource)
    void mouseDown (const juce::MouseEvent& e) override
    {
        // Solo si el clic es en la propia sección (marco, título, hueco): sobre un mando o una vista con su propio menú
        // contextual, ese menú tiene prioridad y este no aparece
        if (e.mods.isPopupMenu() && paramState != nullptr && e.originalComponent == this)
            showResetMenu (*paramState, *this, group.getText().isNotEmpty() ? group.getText() : juce::String ("block"),
                           [this] { return resetIds(); });
    }

    juce::StringArray resetIds() const
    {
        juce::StringArray ids;
        collectParamIds (*this, ids);
        for (const auto& source : resetSources)
            for (const auto& id : source())
                ids.addIfNotAlreadyThere (id);
        return ids;
    }

    // Parámetros de un diálogo de la sección (Edit..., Operators...), que no están en el panel: entran en su Reset
    Section& withResetSource (std::function<juce::StringArray()> ids)
    {
        resetSources.push_back (std::move (ids));
        return *this;
    }

    // En seco (ParamPanel solo para saber qué parámetros tiene un diálogo): withBody y withSideBody no se apropian de
    // componentes ajenos
    bool dryRun = false;

    Section& withToggle (APVTS& s, const juce::String& id)
    {
        toggle.setButtonText ("On");
        addAndMakeVisible (toggle);
        toggleAttachment = std::make_unique<APVTS::ButtonAttachment> (s, id, toggle);
        decorate (s, id, toggle);
        return *this;
    }

    // Igual que withCombo, con el desplegable en orden alfabético (listas largas: patrones, ritmos)
    Section& withSortedCombo (APVTS& s, const juce::String& id)
    {
        combo.alphabetical = true;
        return withCombo (s, id);
    }

    Section& withCombo (APVTS& s, const juce::String& id)
    {
        jassert (comboAttachment == nullptr); // un solo desplegable por sección: un segundo mezclaría sus opciones en el mismo
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
            combo.addItemList (choice->choices, 1); // los items deben existir antes del attachment
        addAndMakeVisible (combo);
        comboAttachment = std::make_unique<APVTS::ComboBoxAttachment> (s, id, combo);
        if (WaveGlyph::isWaveChoice (s.getParameter (id)))
            WaveGlyph::decorate (combo);
        decorate (s, id, combo);
        return *this;
    }

    // Igual que withCombo, pero la elección del usuario llama además a onPick (ver PickComboAttachment)
    Section& withPickCombo (APVTS& s, const juce::String& id, std::function<void (int)> onPick)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
            combo.addItemList (choice->choices, 1);
        addAndMakeVisible (combo);
        pickAttachment = std::make_unique<PickComboAttachment> (*s.getParameter (id), combo, std::move (onPick));
        if (WaveGlyph::isWaveChoice (s.getParameter (id)))
            WaveGlyph::decorate (combo);
        decorate (s, id, combo);
        return *this;
    }

    // Texto visible de cada opción del desplegable (el del parámetro, para el host y el agente, no cambia)
    Section& withItemLabels (const juce::StringArray& labels)
    {
        for (int i = 0; i < labels.size(); ++i)
            combo.changeItemText (i + 1, labels[i]);
        combo.getProperties().set ("fitText", true); // etiquetas largas: el desplegable se ensancha hasta leer la más larga
        return *this;
    }

    // Orden en que el desplegable muestra las opciones (índices del parámetro): solo presentación, el valor no cambia
    Section& withItemOrder (std::vector<int> order)
    {
        combo.displayOrder = std::move (order);
        return *this;
    }

    // Ancho del desplegable de la cabecera: 110 px (140 con icono de onda); con withItemLabels, el texto más largo + 34 px de
    // flecha y márgenes, sin quitar el sitio de los botones y el conmutador de la cabecera. La fuente es la que el LookAndFeel
    // da a un desplegable de 24 px de alto (LookAndFeel_V4: min(16, 0,85·alto) = 16 px): con getComboBoxFont del propio
    // desplegable, en la primera maquetación su alto aún es 0 y el texto medía casi nada (se quedaba en 110 px)
    int comboHeaderWidth (int headerWidth) const
    {
        const auto base = combo.getProperties().contains ("waveGlyphs") ? 140 : 110; // +30: icono
        if (! combo.getProperties().contains ("fitText"))
            return base;
        const juce::Font font (juce::FontOptions (juce::jmin (16.0f, 24.0f * 0.85f)));
        float widest = 0.0f;
        for (int i = 0; i < combo.getNumItems(); ++i)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, combo.getItemText (i)));
        auto reserved = extraToggle.isVisible() ? 80 : 0;
        for (const auto& b : headerButtons)
            reserved += buttonWidth (*b);
        return juce::jlimit (base, juce::jmax (base, headerWidth - reserved), juce::roundToInt (widest) + 34);
    }

    // Tooltip por opción del desplegable de la cabecera (en el orden de sus items)
    Section& withItemTooltips (const juce::StringArray& tips)
    {
        combo.itemTooltips = tips;
        return *this;
    }

    Section& withKnob (APVTS& s, const juce::String& id, const juce::String& text)
    {
        paramState = &s;
        knobs.push_back (std::make_unique<Knob> (s, id, text));
        addAndMakeVisible (*knobs.back());
        row.push_back ({ knobs.back().get(), [k = knobs.back().get()] { return k->layoutWeight(); } });
        return *this;
    }

    // Slider vertical (envolventes ADSR, como en los paneles de hardware)
    Section& withSlider (APVTS& s, const juce::String& id, const juce::String& text)
    {
        paramState = &s;
        knobs.push_back (std::make_unique<Knob> (s, id, text, Knob::Style::vertical));
        addAndMakeVisible (*knobs.back());
        row.push_back ({ knobs.back().get(), [k = knobs.back().get()] { return k->layoutWeight(); } });
        return *this;
    }

    // Lista con nombres en la fila de controles (ListControl), con el ancho de 'weight' perillas
    Section& withList (APVTS& s, const juce::String& id, const juce::String& text, float weight)
    {
        paramState = &s;
        lists.push_back (std::make_unique<ListControl> (s, id, text, weight));
        addAndMakeVisible (*lists.back());
        row.push_back ({ lists.back().get(), [l = lists.back().get()] { return l->layoutWeight(); } });
        return *this;
    }

    // Componente a todo el ancho entre la cabecera y la fila de controles (LCD del MT-32); no es propiedad de la sección.
    // height 0: ocupa todo el alto disponible. minWidth: el ancho que necesita (cuenta para el ancho y el peso de la sección)
    Section& withBody (juce::Component& c, int height, int minWidth)
    {
        if (dryRun) return *this;
        body = &c;
        bodyHeight = height;
        bodyMinWidth = minWidth;
        addAndMakeVisible (c);
        return *this;
    }

    // Componente a la izquierda de los controles, con su ancho en px y todo el alto bajo la cabecera (mini-display del
    // filtro); no es propiedad de la sección
    // tall: el componente ocupa todo el alto de la caja y la cabecera pasa a su derecha (envolventes, LFO)
    Section& withSideBody (juce::Component& c, int width, bool tall = false)
    {
        if (dryRun) return *this;
        sideBody = &c;
        sideBodyWidth = width;
        sideBodyTall = tall;
        addAndMakeVisible (c);
        return *this;
    }

    // Segundo interruptor en la cabecera (Sync, Freeze...)
    Section& withExtraToggle (APVTS& s, const juce::String& id, const juce::String& text)
    {
        extraToggle.setButtonText (text);
        addAndMakeVisible (extraToggle);
        extraToggleAttachment = std::make_unique<APVTS::ButtonAttachment> (s, id, extraToggle);
        decorate (s, id, extraToggle);
        return *this;
    }

    // Botón de cabecera que abre un diálogo de parámetros: sus parámetros entran además en el Reset de la sección
    struct Dialog { std::function<void()> open; std::function<juce::StringArray()> ids; };
    Section& withHeaderButton (const juce::String& text, const juce::String& tooltip, Dialog dialog)
    {
        withResetSource (std::move (dialog.ids));
        return withHeaderButton (text, tooltip, std::move (dialog.open));
    }

    // Botones de acción en la cabecera (Load / Clear...) y texto informativo que ocupa el resto
    Section& withHeaderButton (const juce::String& text, const juce::String& tooltip, std::function<void()> onClick)
    {
        auto& b = headerButtons.emplace_back (std::make_unique<juce::TextButton> (text));
        b->onClick = std::move (onClick);
        b->setTooltip (tooltip);
        addAndMakeVisible (*b);
        return *this;
    }

    // Selector de cabecera con su etiqueta, tras los botones (un ajuste que no es un parámetro, p. ej. Ladder quality)
    Section& withHeaderControl (const juce::String& labelText, juce::Component& c, int width)
    {
        headerLabel.setText (labelText, juce::dontSendNotification);
        headerLabel.setFont (juce::FontOptions (13.0f));
        headerLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (headerLabel);
        addAndMakeVisible (c);
        headerControl = &c;
        headerControlWidth = width;
        return *this;
    }

    // Atenúa el selector de cabecera y su etiqueta sin desactivarlos (sigue respondiendo a los clics)
    void setHeaderControlAlpha (float alpha)
    {
        if (headerControl == nullptr || headerControl->getAlpha() == alpha) return;
        headerControl->setAlpha (alpha);
        headerLabel.setAlpha (alpha);
    }

    void setInfo (const juce::String& text)
    {
        if (! info.isVisible())
        {
            info.setFont (juce::FontOptions (13.0f));
            info.setMinimumHorizontalScale (0.6f);
            addAndMakeVisible (info);
            resized();
        }
        info.setText (text, juce::dontSendNotification);
        info.setTooltip (text);
    }

    void resized() override
    {
        group.setBounds (getLocalBounds());

        auto r = getLocalBounds().reduced (10).withTrimmedTop (10); // 2 px de respiro bajo la línea del título
        // Componente lateral alto (withSideBody, tall): todo el alto de la caja, a 5 px del marco a los lados y abajo y bajo
        // la línea del título; la cabecera y los controles quedan a su derecha
        if (sideBody != nullptr && sideBodyTall)
        {
            sideBody->setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (5).withTrimmedLeft (5).withWidth (sideBodyWidth));
            r.setLeft (sideBody->getRight() + 8);
        }
        // Cabecera solo si tiene controles: sin ella, perillas y sliders ganan 28 px de alto
        if (toggle.isVisible() || combo.isVisible() || extraToggle.isVisible() || ! headerButtons.empty() || headerControl != nullptr)
        {
            auto header = r.removeFromTop (24);
            if (toggle.isVisible()) toggle.setBounds (header.removeFromLeft (60));
            if (combo.isVisible())  combo.setBounds (header.removeFromLeft (comboHeaderWidth (header.getWidth())));
            if (extraToggle.isVisible()) extraToggle.setBounds (header.removeFromLeft (80).withTrimmedLeft (10));
            for (auto& b : headerButtons)
                b->setBounds (header.removeFromLeft (buttonWidth (*b)).withTrimmedLeft (6));
            if (headerControl != nullptr)
            {
                headerLabel.setBounds (header.removeFromLeft (headerLabelWidth()));
                headerControl->setBounds (header.removeFromLeft (headerControlWidth).withTrimmedLeft (4));
            }
            if (info.isVisible()) info.setBounds (header.withTrimmedLeft (6));
            r.removeFromTop (4);
        }
        if (controlsWidth > 0) // el resto, a la derecha bajo la cabecera, queda libre (panel PATCH del complejo)
            r.setWidth (juce::jmin (r.getWidth(), controlsWidth));
        if (body != nullptr && bodyFlush) // a ras del marco: todo el alto y el ancho de la caja, a 5 px, bajo la línea del título
            body->setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (5).reduced (5, 0));
        else if (body != nullptr)
        {
            body->setBounds (bodyHeight > 0 ? r.removeFromTop (bodyHeight) : r);
            r.removeFromTop (4);
        }
        if (sideBody != nullptr && ! sideBodyTall) // a la izquierda de los controles, todo su alto (bajo la cabecera)
        {
            sideBody->setBounds (r.removeFromLeft (sideBodyWidth));
            r.removeFromLeft (8);
        }

        modFrame.setVisible (false);
        if (row.empty())
            return;
        const juce::ScopeGuard placeModFrame { [this] { layoutModFrame(); } }; // tras colocar los mandos, en cualquier salida

        // Como mucho maxControlUnit px por unidad: con pocos controles en una sección ancha no se estiran hasta el borde; el
        // hueco queda al final (en las filas llenas la unidad real es ~55-70 px y no cambia nada). Con varias filas
        // (withControlRows), la misma unidad en todas: la de la fila más ancha
        const auto unit = juce::jmin (maxControlUnit, (float) r.getWidth() / juce::jmax (0.01f, controlsWeight()));
        const auto perRow = controlsPerRow();
        const auto rowH = r.getHeight() / juce::jmax (1, controlRows);
        if (slotsPerRow > 0)
        {
            // Huecos fijos: solo las visibles, en su orden, perRow por fila; el ancho de la sección no cambia
            auto line = r.removeFromTop (rowH);
            size_t placed = 0;
            for (auto& item : row)
            {
                if (! item.component->isVisible())
                    continue;
                if (placed > 0 && placed % perRow == 0)
                    line = r.removeFromTop (rowH);
                item.component->setBounds (line.removeFromLeft (juce::roundToInt (unit * item.weight())));
                ++placed;
            }
            return;
        }
        for (size_t i = 0; i < row.size(); i += perRow)
        {
            auto line = controlRows > 1 ? r.removeFromTop (rowH) : r;
            for (size_t k = i; k < juce::jmin (row.size(), i + perRow); ++k)
                row[k].component->setBounds (line.removeFromLeft (juce::roundToInt (unit * row[k].weight())));
        }
    }

    // Marco del grupo modulable, como la serigrafía de un panel: línea negra de perímetro (con un filo claro por fuera, como
    // grabada) alrededor de las perillas que son destino de la matriz (se les puede soltar una fuente de la barra MOD), y el
    // rótulo "MOD" cortando la línea de abajo a la izquierda; si debajo hay otra fila de mandos (Osc 1, Complex), en vertical
    // sobre la línea izquierda, leído de abajo arriba. Las perillas del grupo van juntas en el orden de la sección (Editor)
    struct ModFrame : juce::Component
    {
        bool sideLabel = false;
        void paint (juce::Graphics& g) override
        {
            if (sideLabel)
            {
                paintSide (g);
                return;
            }
            // 10,5 pt con un 18 % de espacio entre letras: a 9,5 pt sin espaciar se leía mal
            const auto font = juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.18f);
            const auto r = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (6.0f); // la línea de abajo, a media altura del rótulo
            const auto textW = juce::GlyphArrangement::getStringWidth (font, "MOD") + 8.0f;
            const auto gapX = r.getX() + 9.0f;
            // Perímetro sin el tramo del rótulo: desde el final del hueco, en sentido antihorario, hasta su comienzo
            juce::Path p;
            const auto rad = 5.0f;
            p.startNewSubPath (gapX + textW, r.getBottom());
            p.lineTo (r.getRight() - rad, r.getBottom());
            p.quadraticTo (r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - rad);
            p.lineTo (r.getRight(), r.getY() + rad);
            p.quadraticTo (r.getRight(), r.getY(), r.getRight() - rad, r.getY());
            p.lineTo (r.getX() + rad, r.getY());
            p.quadraticTo (r.getX(), r.getY(), r.getX(), r.getY() + rad);
            p.lineTo (r.getX(), r.getBottom() - rad);
            p.quadraticTo (r.getX(), r.getBottom(), r.getX() + rad, r.getBottom());
            p.lineTo (gapX, r.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.10f)); // filo claro, 1 px abajo a la derecha: línea grabada
            g.strokePath (p, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.8f, 0.8f));
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.strokePath (p, juce::PathStrokeType (1.3f));
            g.setColour (Skin::white().withAlpha (0.7f));
            g.setFont (font);
            g.drawText ("MOD", juce::Rectangle<float> (gapX, r.getBottom() - 7.0f, textW, 14.0f), juce::Justification::centred);
        }

        // Rótulo en la línea izquierda: el perímetro deja un hueco en ella, abajo, y el texto va girado -90°
        void paintSide (juce::Graphics& g)
        {
            // 10,5 pt con un 18 % de espacio entre letras: a 9,5 pt sin espaciar se leía mal
            const auto font = juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.18f);
            const auto r = getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft (6.0f);
            const auto textW = juce::GlyphArrangement::getStringWidth (font, "MOD") + 8.0f;
            const auto gapY = r.getBottom() - 9.0f; // el hueco acaba 9 px sobre la esquina de abajo
            juce::Path p;
            const auto rad = 5.0f;
            p.startNewSubPath (r.getX(), gapY - textW);
            p.lineTo (r.getX(), r.getY() + rad);
            p.quadraticTo (r.getX(), r.getY(), r.getX() + rad, r.getY());
            p.lineTo (r.getRight() - rad, r.getY());
            p.quadraticTo (r.getRight(), r.getY(), r.getRight(), r.getY() + rad);
            p.lineTo (r.getRight(), r.getBottom() - rad);
            p.quadraticTo (r.getRight(), r.getBottom(), r.getRight() - rad, r.getBottom());
            p.lineTo (r.getX() + rad, r.getBottom());
            p.quadraticTo (r.getX(), r.getBottom(), r.getX(), r.getBottom() - rad);
            p.lineTo (r.getX(), gapY);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.strokePath (p, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.8f, 0.8f));
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.strokePath (p, juce::PathStrokeType (1.3f));
            const juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getX(), gapY));
            g.setColour (Skin::white().withAlpha (0.7f));
            g.setFont (font);
            g.drawText ("MOD", juce::Rectangle<float> (r.getX(), gapY - 7.0f, textW, 14.0f), juce::Justification::centred);
        }
    };
    void layoutModFrame()
    {
        juce::Rectangle<int> area;
        for (auto& k : knobs)
            if (k->isVisible() && k->style == Knob::Style::rotary && MatrixUi::destinationOf (k->paramId) > 0)
                area = area.isEmpty() ? k->getBounds() : area.getUnion (k->getBounds());
        modFrame.setVisible (! area.isEmpty());
        if (area.isEmpty())
            return;
        // ¿Otro mando justo debajo (12 px)? Entonces el rótulo, en la línea izquierda
        const auto below = juce::Rectangle<int> (area.getX(), area.getBottom(), area.getWidth(), 12);
        modFrame.sideLabel = false;
        for (auto& item : row)
            if (item.component->isVisible() && item.component->getBounds().intersects (below))
                modFrame.sideLabel = true;
        // Rótulo abajo: 3 px por los lados, 2 arriba y 8 abajo. A la izquierda: 8 a la izquierda y 3 en el resto
        modFrame.setBounds (modFrame.sideLabel ? area.withTrimmedLeft (-8).expanded (0, 2).withTrimmedRight (-3)
                                               : area.expanded (3, 0).withTop (area.getY() - 2).withBottom (area.getBottom() + 8));
        modFrame.repaint();
    }
    ModFrame modFrame;

    // Controles en n filas (en el orden en que se añadieron, ⌈N/n⌉ por fila): la sección pide el ancho de la fila más
    // ancha. Para secciones con muchas perillas en una fila alta (Osc 1 junto al display de onda)
    Section& withControlRows (int n)
    {
        controlRows = juce::jlimit (1, 4, n);
        return *this;
    }

    // Huecos fijos por fila (withSlotsPerRow): para secciones que muestran u ocultan mandos según un selector (Math Models). Las
    // visibles se colocan en orden en filas de n; la sección pide siempre el ancho de n mandos
    Section& withSlotsPerRow (int n)
    {
        slotsPerRow = juce::jmax (1, n);
        return *this;
    }

    // Peso de los controles: el de su fila (o el de la fila más ancha si hay varias)
    float controlsWeight() const
    {
        if (slotsPerRow > 0)
        {
            float widest = 0.0f;
            for (const auto& item : row)
                widest = juce::jmax (widest, item.weight());
            return widest * (float) slotsPerRow;
        }
        const auto perRow = controlsPerRow();
        float widest = 0.0f;
        for (size_t i = 0; i < row.size(); i += perRow)
        {
            float w = 0.0f;
            for (size_t k = i; k < juce::jmin (row.size(), i + perRow); ++k)
                if (row[k].component->isVisible()) w += row[k].weight();
            widest = juce::jmax (widest, w);
        }
        return widest;
    }
    size_t controlsPerRow() const
    {
        if (slotsPerRow > 0) return (size_t) slotsPerRow;
        return juce::jmax ((size_t) 1, (row.size() + (size_t) controlRows - 1) / (size_t) controlRows);
    }
    int controlRows = 1, slotsPerRow = 0;
    // Ancho máximo de cuerpo y controles (px, 0 = todo): lo de su derecha lo ocupa otro componente (panel PATCH, PatchBay)
    int controlsWidth = 0;
    // Elástica: si su fila cabe a ancho natural, se queda con el sobrante (Editor::layoutRow)
    bool stretchToFill = false;
    // A ras del marco: el cuerpo ocupa la caja entera bajo la línea del título (display de onda; sin cabecera ni controles)
    bool bodyFlush = false;

    // Muestra u oculta los controles (interruptor, selector, segundo interruptor y mandos) dejando los botones de cabecera
    // y el texto: la sección conserva su marco y su ancho (layoutWeight) pasa a ser el de lo visible
    void showControls (bool on)
    {
        if (on == controlsShown) return;
        controlsShown = on;
        toggle.setVisible (on && toggleAttachment != nullptr);
        combo.setVisible (on && (comboAttachment != nullptr || pickAttachment != nullptr));
        extraToggle.setVisible (on && extraToggleAttachment != nullptr);
        for (auto& item : row) item.component->setVisible (on);
        resized();
    }
    bool controlsShown = true;

    juce::TextButton* headerButton (size_t i) { return i < headerButtons.size() ? headerButtons[i].get() : nullptr; }

    // Para atenuar controles sin efecto (el editor decide cuándo)
    juce::Component& comboComponent() { return combo; }
    juce::Component& extraToggleComponent() { return extraToggle; }
    Knob* knobFor (const juce::String& id)
    {
        for (auto& k : knobs)
            if (k->paramId == id) return k.get();
        return nullptr;
    }
    ListControl* listFor (const juce::String& id)
    {
        for (auto& l : lists)
            if (l->paramId == id) return l.get();
        return nullptr;
    }

    // Ancho relativo en su fila: el mayor entre sus controles (1 por perilla, 0,75 por slider) y lo que pide la cabecera en
    // píxeles (On 60, selector 110, segundo interruptor 80, botón 64, texto informativo 110, márgenes 20) a 60 px por unidad
    // (unidad real en las filas más llenas: ~55–70 px; con 76 se cortaban el botón Edit... de Wavetable y el selector de Chorus).
    // Mínimo 1,3: con 1 potenciómetro el título ("Master") salía cortado
    virtual float layoutWeight() const
    {
        // Con el componente lateral alto, la cabecera comparte fila con él: su ancho también se suma
        return juce::jmax (controlsWeight() + sidePixels() / 60.0f, (headerPixels() + (sideBodyTall ? sidePixels() : 0.0f)) / 60.0f,
                           ((float) bodyMinWidth + 34.0f) / 60.0f, 1.3f);
    }

    // Lo que pide la cabecera en píxeles, márgenes incluidos: 20 del marco + 14 de la celda en layoutRow (con 20 se cortaba
    // el Edit... del Vocoder)
    float headerPixels() const
    {
        auto headerPx = 34.0f;
        if (toggle.isVisible())      headerPx += 60.0f;
        if (combo.isVisible())       headerPx += 110.0f;
        if (extraToggle.isVisible()) headerPx += 80.0f;
        for (auto& b : headerButtons)
            headerPx += (float) buttonWidth (*b);
        if (info.isVisible())        headerPx += 110.0f;
        if (headerControl != nullptr) headerPx += (float) (headerLabelWidth() + headerControlWidth);
        return headerPx;
    }

    // Etiqueta del selector de cabecera: su texto entero más 18 px de separación
    int headerLabelWidth() const
    {
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (headerLabel.getFont(), headerLabel.getText())) + 18;
    }

    // Unidad máxima de los controles (px por perilla): con pocos controles no se estiran hasta el borde
    static constexpr float maxControlUnit = 110.0f;

    // Ancho natural en px (márgenes incluidos) con la unidad máxima: lo mayor entre cabecera, cuerpo y controles. layoutRow
    // lo usa cuando todas las secciones de la fila caben así; si no, reparte por layoutWeight
    virtual float naturalWidth() const
    {
        return juce::jmax (headerPixels() + (sideBodyTall ? sidePixels() : 0.0f), (float) bodyMinWidth + 34.0f,
                           controlsWeight() * maxControlUnit + sidePixels() + 34.0f, 1.3f * 60.0f);
    }
    // Ancho del componente lateral (withSideBody) con su separación
    float sidePixels() const { return sideBody != nullptr ? (float) sideBodyWidth + 8.0f : 0.0f; }

    // Ancho de un botón de cabecera: 64 px o lo que pida su texto ("Frame Macros...") más el margen izquierdo de 6
    static int buttonWidth (juce::TextButton& b) { return juce::jmax (64, b.getBestWidthForHeight (24) + 6); }

protected:
    // Sin marco ni título (contenedores como SourceTabs, cuyo contenido son otras secciones)
    void hideFrame() { group.setVisible (false); }

public:
    // Perilla i-ésima (nullptr si no existe): las macros renombran la etiqueta
    Knob* knob (size_t i) { return i < knobs.size() ? knobs[i].get() : nullptr; }

    // Título del marco (SourceTabs lo vacía: sus pestañas ocupan la línea del título)
    void setTitle (const juce::String& text) { group.setText (text); }

private:
    void decorate (APVTS& s, const juce::String& id, juce::Component& c)
    {
        if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (&c))
            tc->setTooltip (paramTooltip (s, id));
        menus.push_back (std::make_unique<ParamMenu> (s, id, c));
        paramState = &s;
    }

    // Destrucción en orden inverso: los menús primero; cada attachment muere antes que su control.
    juce::GroupComponent group;
    juce::ToggleButton toggle;
    std::unique_ptr<APVTS::ButtonAttachment> toggleAttachment;
    SortableComboBox combo;
    std::unique_ptr<APVTS::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<PickComboAttachment> pickAttachment;
    juce::ToggleButton extraToggle;
    std::unique_ptr<APVTS::ButtonAttachment> extraToggleAttachment;
    std::vector<std::unique_ptr<juce::TextButton>> headerButtons;
    juce::Label headerLabel;                // etiqueta del selector de cabecera (withHeaderControl)
    juce::Component* headerControl = nullptr; // propiedad del editor
    int headerControlWidth = 0;
    juce::Label info;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<ListControl>> lists;
    struct RowItem { juce::Component* component; std::function<float()> weight; };
    std::vector<RowItem> row; // perillas, sliders y listas en el orden en que se añadieron
    juce::Component* body = nullptr;
    int bodyHeight = 0, bodyMinWidth = 0;
    juce::Component* sideBody = nullptr;
    int sideBodyWidth = 0;
    bool sideBodyTall = false;
    std::vector<std::unique_ptr<ParamMenu>> menus;
    APVTS* paramState = nullptr; // el de sus parámetros (Reset ... to defaults); nulo en una sección sin mandos de parámetro
    std::vector<std::function<juce::StringArray()>> resetSources;
};

// Cuerpo de los diálogos "Edit..." de las secciones del panel: los parámetros de escenario (los que se ajustan antes de
// tocar, no mientras) salen del panel y viven aquí. Siguen siendo parámetros del plugin: automatización, MIDI learn y
// menú contextual igual que en el panel. Secciones en una fila, cada una con el ancho de su peso
struct ParamPanel : juce::Component
{
    ParamPanel() = default;
    // En seco: solo para saber qué parámetros tiene el diálogo (Reset ... to defaults de su sección); no se muestra
    explicit ParamPanel (bool dry) : dryRun (dry) {}
    const bool dryRun = false;

    Section& add (const juce::String& title)
    {
        sections.push_back (std::make_unique<Section> (title));
        sections.back()->dryRun = dryRun;
        addAndMakeVisible (*sections.back());
        return *sections.back();
    }

    float weight() const
    {
        float w = 0.0f;
        for (auto& s : sections) w += s->layoutWeight();
        return w;
    }

    // Perilla de un parámetro en cualquiera de sus secciones (el editor atenúa los mandos inertes también en los diálogos)
    Knob* knobFor (const juce::String& id)
    {
        for (auto& s : sections)
            if (auto* k = s->knobFor (id)) return k;
        return nullptr;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto unit = (float) r.getWidth() / juce::jmax (1.0f, weight());
        for (auto& s : sections)
            s->setBounds (r.removeFromLeft (juce::roundToInt (unit * s->layoutWeight())).reduced (4, 0));
    }

private:
    std::vector<std::unique_ptr<Section>> sections;
};

// UI: ventana flotante (bloque desacoplado, teclado, superficie, display) con barra de título de JUCE: la nativa de Windows
// no admite botones propios. No queda encima de las demás por defecto; la chincheta de la barra, a la izquierda de los
// botones de la ventana, la fija encima (setAlwaysOnTop) y la suelta. El estado no se guarda: cada ventana nueva, suelta
struct PinnableWindow : juce::DocumentWindow
{
    PinnableWindow (const juce::String& name, juce::Colour background, int buttons)
        : juce::DocumentWindow (name, background, buttons)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (26);
        pin.setClickingTogglesState (true);
        pin.setTooltip ("Keep this window on top of other windows.");
        pin.onClick = [this] { setAlwaysOnTop (pin.getToggleState()); };
        juce::Component::addAndMakeVisible (pin); // sobre la barra, fuera del contenido (ResizableWindow avisa si no)
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        const auto bar = getTitleBarArea();
        auto right = bar.getRight();
        for (auto* b : { getCloseButton(), getMaximiseButton(), getMinimiseButton() })
            if (b != nullptr && b->isVisible())
                right = juce::jmin (right, b->getX());
        pin.setBounds (right - bar.getHeight() - 4, bar.getY(), bar.getHeight(), bar.getHeight());
        pin.toFront (false);
    }

    // Chincheta: suelta, inclinada 45° y gris; fijada, vertical, en el acento del skin y con el fondo marcado
    struct Pin : juce::Button
    {
        Pin() : juce::Button ("Pin") {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto on = getToggleState();
            const auto r = getLocalBounds().toFloat().reduced (3.0f);
            if (on || over || down)
            {
                g.setColour (juce::Colours::white.withAlpha (on ? 0.16f : 0.08f));
                g.fillRoundedRectangle (r, 4.0f);
            }
            // Dibujada vertical en un cuadro de 12 × 16 px centrado, y girada si está suelta
            const auto c = r.getCentre();
            juce::Path head, needle;
            head.addRoundedRectangle (c.x - 4.0f, c.y - 8.0f, 8.0f, 3.0f, 1.0f);   // cabeza
            head.addRectangle (c.x - 2.5f, c.y - 5.0f, 5.0f, 5.0f);                // cuerpo
            head.addRoundedRectangle (c.x - 5.5f, c.y, 11.0f, 2.2f, 1.0f);         // collar
            needle.startNewSubPath (c.x, c.y + 2.2f);
            needle.lineTo (c.x, c.y + 8.0f);
            const auto t = on ? juce::AffineTransform() : juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f, c.x, c.y);
            const auto colour = on ? Skin::accent() : juce::Colours::white.withAlpha (over ? 0.9f : 0.6f);
            g.setColour (colour);
            g.fillPath (head, t);
            g.strokePath (needle, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
        }
    };
    Pin pin;
};

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

struct ParamMenu : juce::MouseListener
{
    ParamMenu (APVTS& s, const juce::String& id, juce::Component& target)
        : param (s.getParameter (id)),
          koni (dynamic_cast<KoniSynthProcessor&> (s.processor)),
          learn (koni.midiLearn),
          component (&target)
    {
        target.addMouseListener (this, true);
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
            [this, p = param, l = &learn, index, safe = component] (int result)
            {
                if (safe == nullptr)
                    return;
                if (result >= 200 && result < 200 + PerfMacros::numMacros)
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

// UI: control de un parámetro continuo con etiqueta: perilla o slider vertical (envolventes). Doble clic = valor por
// defecto. La etiqueta muestra el CC asignado ("Cutoff [74]") o "Learn..." mientras espera un CC.
struct Knob : juce::Component, private juce::Timer
{
    enum class Style { rotary, vertical };

    Knob (APVTS& s, const juce::String& id, const juce::String& text, Style st = Style::rotary)
        : style (st),
          attachment (s, id, slider),
          menu (s, id, slider),
          name (text),
          learn (dynamic_cast<KoniSynthProcessor&> (s.processor).midiLearn),
          paramIndex (s.getParameter (id)->getParameterIndex()),
          paramId (id)
    {
        auto* p = s.getParameter (id);
        slider.setSliderStyle (style == Style::rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, style == Style::rotary ? 70 : 56, 18);
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        const auto tip = paramTooltip (s, id);
        slider.setTooltip (tip);
        label.setTooltip (tip);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
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
        label.setBounds (r.removeFromTop (18));
        slider.setBounds (r);
    }

    float layoutWeight() const { return style == Style::rotary ? 1.0f : 0.9f; } // slider: su caja de valor ("300 ms") necesita ~50 px

    // Anillo de modulación (matriz, UI/MatrixPanel.h): rango [lo, hi] y valor en vivo (normalizados; live < 0 = sin punto)
    void setRing (bool on, float lo, float hi, float live)
    {
        if (on == ringOn && std::abs (lo - ringLo) < 1.0e-3f && std::abs (hi - ringHi) < 1.0e-3f && std::abs (live - ringLive) < 1.0e-3f)
            return;
        ringOn = on; ringLo = lo; ringHi = hi; ringLive = live;
        repaint();
    }

    // Arco ámbar por fuera del arco de valor, con la geometría de LookAndFeel_V4::drawRotarySlider tal como lo llama
    // KoniLookAndFeel (rectángulo ampliado 8 px y reducido 10: neto, 2 px por lado)
    void paintOverChildren (juce::Graphics& g) override
    {
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
        if (ringLive >= 0.0f)
        {
            const auto a = angle (ringLive) - juce::MathConstants<float>::halfPi;
            g.setColour (Skin::white());
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ c.x + ringR * std::cos (a), c.y + ringR * std::sin (a) }));
        }
    }

    Style style;
    juce::Slider slider;
    juce::Label label;
    APVTS::SliderAttachment attachment; // declarado tras slider: orden de construcción
    ParamMenu menu;
    juce::String name;
    MidiLearn& learn;
    int paramIndex;
    juce::String paramId;
    bool ringOn = false;
    float ringLo = 0.0f, ringHi = 0.0f, ringLive = -1.0f;
};

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
    // Perilla con aspecto de pieza física, dibujada (escala con la ventana; tono original en todos los skins): arco de valor por fuera, con la
    // geometría de LookAndFeel_V4 (el anillo ámbar de la matriz queda fuera de él), y dentro sombra, faldón moleteado de 32
    // estrías, tapa con degradado, reflejo y línea indicadora
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float startAngle,
                           float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x - 8, y - 8, width + 16, height + 16).toFloat().reduced (10.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        if (radius < 4.0f)
            return;
        const auto c = bounds.getCentre();
        const auto lineW = juce::jmin (8.0f, radius * 0.5f);
        const auto arcR = radius - lineW * 0.5f;
        const auto angle = startAngle + sliderPos * (endAngle - startAngle);
        const auto enabled = slider.isEnabled();

        // Arco: pista hundida y valor
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff101316));
        g.strokePath (track, juce::PathStrokeType (lineW * 0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (enabled)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (Skin::accent().withAlpha (0.28f)); // halo: pasada ancha y tenue, después la del valor
            g.strokePath (value, juce::PathStrokeType (lineW * 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Skin::accent());
            g.strokePath (value, juce::PathStrokeType (lineW * 0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Cuerpo
        const auto bodyR = radius - lineW - 1.0f;
        if (bodyR < 3.0f)
            return;
        const auto body = juce::Rectangle<float> (2.0f * bodyR, 2.0f * bodyR).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (body.translated (0.0f, bodyR * 0.08f + 0.8f).expanded (0.8f)); // sombra
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f46), c.x, body.getY(), juce::Colour (0xff111316), c.x, body.getBottom(), false));
        g.fillEllipse (body);
        // Moleteado: 32 estrías alternas en el faldón
        const auto capR = bodyR * 0.74f;
        for (int k = 0; k < 32; ++k)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) k / 32.0f;
            const auto ca = std::cos (a), sa = std::sin (a);
            g.setColour ((k & 1) != 0 ? juce::Colours::white.withAlpha (0.10f) : juce::Colours::black.withAlpha (0.35f));
            g.drawLine (c.x + ca * capR, c.y + sa * capR, c.x + ca * (bodyR - 0.6f), c.y + sa * (bodyR - 0.6f), juce::jmax (0.8f, bodyR * 0.05f));
        }
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (body, 1.0f);
        // Tapa
        const auto cap = juce::Rectangle<float> (2.0f * capR, 2.0f * capR).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a717b), cap.getX(), cap.getY(), juce::Colour (0xff23272c), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse (cap);
        for (int k = 1; k <= 4; ++k) // pulido concéntrico
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.drawEllipse (cap.reduced (capR * 0.18f * (float) k), 0.8f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), cap.getX() + capR * 0.5f, cap.getY() + capR * 0.35f,
                                                 juce::Colours::white.withAlpha (0.0f), c.x, c.y, true));
        g.fillEllipse (cap.reduced (capR * 0.08f)); // reflejo
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (cap, 0.8f);
        // Indicador
        const auto ia = angle - juce::MathConstants<float>::halfPi;
        const juce::Point<float> p0 { c.x + std::cos (ia) * capR * 0.25f, c.y + std::sin (ia) * capR * 0.25f },
                                 p1 { c.x + std::cos (ia) * (bodyR - 1.0f), c.y + std::sin (ia) * (bodyR - 1.0f) };
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine ({ p0, p1 }, juce::jmax (2.6f, bodyR * 0.16f));
        g.setColour (enabled ? juce::Colours::white : juce::Colours::white.withAlpha (0.5f));
        g.drawLine ({ p0, p1 }, juce::jmax (1.4f, bodyR * 0.08f));
    }

    // Fader: ranura hundida, relleno de valor fino y tapa con degradado y estrías (vertical u horizontal); otros estilos, V4
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }
        const auto vertical = style == juce::Slider::LinearVertical;
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto grooveW = 4.0f;
        const auto groove = vertical ? juce::Rectangle<float> (area.getCentreX() - grooveW * 0.5f, area.getY(), grooveW, area.getHeight())
                                     : juce::Rectangle<float> (area.getX(), area.getCentreY() - grooveW * 0.5f, area.getWidth(), grooveW);
        g.setColour (juce::Colour (0xff0b0d0f));
        g.fillRoundedRectangle (groove, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (groove.translated (0.0f, 0.5f), 2.0f, 0.8f);
        if (slider.isEnabled()) // relleno desde el mínimo
        {
            const auto fill = vertical ? groove.withTop (sliderPos).reduced (1.0f, 0.0f) : groove.withRight (sliderPos).reduced (0.0f, 1.0f);
            g.setColour (Skin::accent().withAlpha (0.28f)); // halo
            g.fillRoundedRectangle (fill.expanded (2.0f, 2.0f), 3.0f);
            g.setColour (Skin::accent());
            g.fillRoundedRectangle (fill, 1.5f);
        }
        // Tapa
        const auto capW = vertical ? juce::jmin (area.getWidth() * 0.8f, 24.0f) : 12.0f;
        const auto capH = vertical ? 12.0f : juce::jmin (area.getHeight() * 0.8f, 24.0f);
        const auto cap = juce::Rectangle<float> (capW, capH).withCentre (vertical ? juce::Point<float> { area.getCentreX(), sliderPos }
                                                                                    : juce::Point<float> { sliderPos, area.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.2f).expanded (0.6f), 2.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff70777f), cap.getX(), cap.getY(), juce::Colour (0xff23272c), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.0f);
        for (int k = -1; k <= 1; k += 2) // estrías a ±3 px de la línea central
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            if (vertical) g.drawHorizontalLine (juce::roundToInt (cap.getCentreY()) + 3 * k, cap.getX() + 3.0f, cap.getRight() - 3.0f);
            else          g.drawVerticalLine (juce::roundToInt (cap.getCentreX()) + 3 * k, cap.getY() + 3.0f, cap.getBottom() - 3.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (slider.isEnabled() ? 0.9f : 0.45f)); // línea central
        if (vertical) g.drawHorizontalLine (juce::roundToInt (cap.getCentreY()), cap.getX() + 1.5f, cap.getRight() - 1.5f);
        else          g.drawVerticalLine (juce::roundToInt (cap.getCentreX()), cap.getY() + 1.5f, cap.getBottom() - 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (cap, 2.0f, 0.8f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat();
        const auto d = juce::jmin (12.0f, r.getHeight() - 6.0f);
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + 4.0f + d * 0.5f, r.getCentreY() });
        const auto on = b.getToggleState();

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
        if (! alphabetical && itemTooltips.isEmpty())
        {
            juce::ComboBox::showPopup();
            return;
        }
        std::vector<int> order;
        for (int i = 0; i < getNumItems(); ++i)
            order.push_back (i);
        const auto first = getNumItems() > 0 && getItemText (0) == "Off" ? 1 : 0;
        if (alphabetical)
            std::stable_sort (order.begin() + first, order.end(), [this] (int a, int b)
                              { return getItemText (a).compareNatural (getItemText (b)) < 0; });
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
        addAndMakeVisible (group);
    }

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
        decorate (s, id, combo);
        return *this;
    }

    // Tooltip por opción del desplegable de la cabecera (en el orden de sus items)
    Section& withItemTooltips (const juce::StringArray& tips)
    {
        combo.itemTooltips = tips;
        return *this;
    }

    Section& withKnob (APVTS& s, const juce::String& id, const juce::String& text)
    {
        knobs.push_back (std::make_unique<Knob> (s, id, text));
        addAndMakeVisible (*knobs.back());
        row.push_back ({ knobs.back().get(), [k = knobs.back().get()] { return k->layoutWeight(); } });
        return *this;
    }

    // Slider vertical (envolventes ADSR, como en los paneles de hardware)
    Section& withSlider (APVTS& s, const juce::String& id, const juce::String& text)
    {
        knobs.push_back (std::make_unique<Knob> (s, id, text, Knob::Style::vertical));
        addAndMakeVisible (*knobs.back());
        row.push_back ({ knobs.back().get(), [k = knobs.back().get()] { return k->layoutWeight(); } });
        return *this;
    }

    // Lista con nombres en la fila de controles (ListControl), con el ancho de 'weight' perillas
    Section& withList (APVTS& s, const juce::String& id, const juce::String& text, float weight)
    {
        lists.push_back (std::make_unique<ListControl> (s, id, text, weight));
        addAndMakeVisible (*lists.back());
        row.push_back ({ lists.back().get(), [l = lists.back().get()] { return l->layoutWeight(); } });
        return *this;
    }

    // Componente a todo el ancho entre la cabecera y la fila de controles (LCD del MT-32); no es propiedad de la sección.
    // height 0: ocupa todo el alto disponible. minWidth: el ancho que necesita (cuenta para el ancho y el peso de la sección)
    Section& withBody (juce::Component& c, int height, int minWidth)
    {
        body = &c;
        bodyHeight = height;
        bodyMinWidth = minWidth;
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

        auto r = getLocalBounds().reduced (10).withTrimmedTop (8);
        // Cabecera solo si tiene controles: sin ella, perillas y sliders ganan 28 px de alto
        if (toggle.isVisible() || combo.isVisible() || extraToggle.isVisible() || ! headerButtons.empty() || headerControl != nullptr)
        {
            auto header = r.removeFromTop (24);
            if (toggle.isVisible()) toggle.setBounds (header.removeFromLeft (60));
            if (combo.isVisible())  combo.setBounds (header.removeFromLeft (110));
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
        if (body != nullptr)
        {
            body->setBounds (bodyHeight > 0 ? r.removeFromTop (bodyHeight) : r);
            r.removeFromTop (4);
        }

        if (row.empty())
            return;

        // Como mucho maxControlUnit px por unidad: con pocos controles en una sección ancha no se estiran hasta el borde; el
        // hueco queda al final (en las filas llenas la unidad real es ~55-70 px y no cambia nada)
        const auto unit = juce::jmin (maxControlUnit, (float) r.getWidth() / controlsWeight());
        for (auto& item : row)
            item.component->setBounds (r.removeFromLeft (juce::roundToInt (unit * item.weight())));
    }

    float controlsWeight() const
    {
        float w = 0.0f;
        for (auto& item : row)
            if (item.component->isVisible()) w += item.weight();
        return w;
    }

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
        return juce::jmax (controlsWeight(), headerPixels() / 60.0f, ((float) bodyMinWidth + 34.0f) / 60.0f, 1.3f);
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
        return juce::jmax (headerPixels(), (float) bodyMinWidth + 34.0f, controlsWeight() * maxControlUnit + 34.0f, 1.3f * 60.0f);
    }

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
    std::vector<std::unique_ptr<ParamMenu>> menus;
};

// Cuerpo de los diálogos "Edit..." de las secciones del panel: los parámetros de escenario (los que se ajustan antes de
// tocar, no mientras) salen del panel y viven aquí. Siguen siendo parámetros del plugin: automatización, MIDI learn y
// menú contextual igual que en el panel. Secciones en una fila, cada una con el ancho de su peso
struct ParamPanel : juce::Component
{
    Section& add (const juce::String& title)
    {
        sections.push_back (std::make_unique<Section> (title));
        addAndMakeVisible (*sections.back());
        return *sections.back();
    }

    float weight() const
    {
        float w = 0.0f;
        for (auto& s : sections) w += s->layoutWeight();
        return w;
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

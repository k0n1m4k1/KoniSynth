// Fuentes del Synth en pestañas, en dos grupos: Osc 1 / Wavetable / Sample / String y Osc 2 / Osc 3 / Noise. Solo se ve el
// contenido de una pestaña por grupo, pero todas suenan sumadas según su nivel (el mezclador no cambia). Cada pestaña lleva
// un LED rojo encendido si su fuente suena de verdad (nivel > 0 y audible según Mute y Solo); clic derecho: Mute / Solo.
// Mute es un parámetro de cada fuente; Solo es un único estado transitorio compartido por los dos grupos (Params::soloSource,
// sin guardar): no toca los Mute, y al quitarlo cada fuente vuelve a su Mute. Silenciada (por su Mute o por el Solo de otra):
// LED apagado y nombre en gris; etiqueta "M" si tiene Mute, "S" la del Solo.
// Se incluye desde KoniSynth.cpp (mismo TU), tras UI/Widgets.h.
#pragma once

class SourceTabs : public Section, private juce::Timer
{
public:
    struct Source { const char* name; Section* section; const char* levelId; const char* muteId; };

    // firstSource: índice global (Params::SourceIndex) de la primera pestaña del grupo
    SourceTabs (APVTS& s, std::vector<Source> list, int firstSource, std::atomic<int>& soloState)
        : Section ({}), apvts (s), sources (std::move (list)), first (firstSource), solo (soloState)
    {
        hideFrame();
        for (auto& src : sources)
        {
            addChildComponent (*src.section);
            src.section->setTitle ({}); // la pestaña hace de título
        }
        for (size_t k = 0; k < sources.size(); ++k) // las pestañas, después: siempre por encima de las secciones
        {
            auto& t = tabs.emplace_back (std::make_unique<Tab> (*this, (int) k));
            t->setTooltip (juce::String (sources[k].name) + ": click to show its controls. Right-click: Mute / Solo / Reset to defaults. "
                           "The red LED is lit while this source sounds (level above 0 and not muted). All sources play together.");
            addAndMakeVisible (*t);
        }
        select (0);
        startTimerHz (10);
    }

    // Pestaña elegida con el ratón (índice dentro del grupo): el display de onda cambia de vista. show() la elige desde fuera
    // sin llamar a onSelect
    std::function<void (int index)> onSelect;
    void show (int k) { if (k >= 0 && k < (int) sources.size() && k != current) select (k); }

    // Ancho en la fila: el de la pestaña más ancha (todas comparten el sitio)
    float layoutWeight() const override
    {
        auto w = 1.3f;
        for (const auto& src : sources) w = juce::jmax (w, src.section->layoutWeight());
        return w;
    }

    float naturalWidth() const override
    {
        auto w = 1.3f * 60.0f;
        for (const auto& src : sources) w = juce::jmax (w, src.section->naturalWidth());
        return w;
    }

    void resized() override
    {
        // Barra de pestañas de 24 px; el marco de la sección empieza bajo ella (la fila de fuentes tiene más alto)
        for (auto& src : sources)
            src.section->setBounds (getLocalBounds().withTrimmedTop (16));
        auto bar = getLocalBounds().removeFromTop (24).withTrimmedLeft (8).withTrimmedRight (8);
        const auto w = juce::jmin (130, bar.getWidth() / juce::jmax (1, (int) tabs.size()));
        for (auto& t : tabs)
            t->setBounds (bar.removeFromLeft (w).reduced (1, 0));
    }

private:
    struct Tab : juce::Button
    {
        Tab (SourceTabs& o, int i) : juce::Button (o.sources[(size_t) i].name), owner (o), index (i) {}

        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            // Pestaña de carpeta: la activa, del color de la cara del bloque (Skin::panelFace con la banda de título) y abierta
            // hacia él: baja hasta el borde inferior de la barra y tapa el canto superior del marco, sin línea debajo. Las
            // inactivas, más bajas y apagadas, terminan en el canto del marco: su línea queda debajo de ellas
            const auto selected = owner.current == index;
            const auto full = getLocalBounds().toFloat();
            const auto r = selected ? full.withTrimmedTop (1.0f) : full.withTrimmedTop (3.0f).withBottom (cardTop);
            const auto face = Skin::panelFace().top.overlaidWith (juce::Colours::white.withAlpha (0.035f));
            juce::Path shape;
            shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4.0f, 4.0f, true, true, false, false);
            g.setColour ((selected ? face : face.darker (0.45f)).brighter (highlighted ? 0.1f : 0.0f));
            g.fillPath (shape);
            {
                // Contorno como el del marco (negro 55 %) por arriba y los lados; nada por abajo
                juce::Path edge;
                edge.startNewSubPath (r.getX() + 0.5f, r.getBottom());
                edge.lineTo (r.getX() + 0.5f, r.getY() + 4.0f);
                edge.quadraticTo (r.getX() + 0.5f, r.getY() + 0.5f, r.getX() + 4.0f, r.getY() + 0.5f);
                edge.lineTo (r.getRight() - 4.0f, r.getY() + 0.5f);
                edge.quadraticTo (r.getRight() - 0.5f, r.getY() + 0.5f, r.getRight() - 0.5f, r.getY() + 4.0f);
                edge.lineTo (r.getRight() - 0.5f, r.getBottom());
                g.setColour (juce::Colours::black.withAlpha (selected ? 0.55f : 0.4f));
                g.strokePath (edge, juce::PathStrokeType (1.0f));
                if (selected) // luz en el canto superior, como la del marco
                {
                    g.setColour (juce::Colours::white.withAlpha (0.10f));
                    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.5f), r.getX() + 4.0f, r.getRight() - 4.0f);
                }
            }
            // LED rojo encendido/apagado, como los interruptores de la interfaz
            const auto d = juce::jmin (12.0f, r.getHeight() - 5.0f); // inactiva (14,5 px de alto): 9,5 px
            const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + 6.0f + d * 0.5f, r.getCentreY() });
            const auto on = owner.sounding (index);
            if (on)
            {
                g.setColour (juce::Colour (0xffff3030).withAlpha (0.35f)); // LED rojo fijo: no sigue el skin
                g.fillEllipse (led.expanded (3.0f));
            }
            g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xffff8a80) : juce::Colour (0xff5a1e1e), led.getX() + d * 0.3f, led.getY() + d * 0.3f,
                                                     on ? juce::Colour (0xffd01010) : juce::Colour (0xff2a0c0c), led.getRight(), led.getBottom(), true));
            g.fillEllipse (led);
            g.setColour (Skin::black().withAlpha (0.6f));
            g.drawEllipse (led, 1.0f);
            // Nombre y etiqueta M (silenciada) o S (solo)
            const auto soloed = owner.soloed (index);
            const auto muted = ! owner.audible (index);
            auto label = r.withTrimmedLeft (d + 12.0f);
            const auto tagText = soloed ? "S" : owner.muted (index) ? "M" : "";
            if (*tagText != 0)
            {
                const auto tag = label.removeFromRight (18.0f).reduced (1.0f, selected ? 4.0f : 2.0f);
                g.setColour (soloed ? Skin::c (0xffd8c040) : Skin::c (0xffc8a020).withAlpha (0.85f));
                g.fillRoundedRectangle (tag, 2.0f);
                g.setColour (Skin::black());
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (tagText, tag, juce::Justification::centred);
            }
            g.setColour (Skin::white().withAlpha (muted ? 0.4f : (selected ? 1.0f : 0.6f)));
            g.setFont (juce::FontOptions (selected ? 14.0f : 13.0f));
            g.drawFittedText (getButtonText(), label.toNearestInt(), juce::Justification::centredLeft, 1);
        }

        void clicked() override
        {
            owner.select (index);
            if (owner.onSelect) owner.onSelect (index);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                owner.showMenu (index);
            else
                juce::Button::mouseDown (e);
        }

        // Canto superior del marco de la sección en coordenadas de la pestaña: la sección empieza a 16 px (resized) y su
        // tarjeta, 1,5 px por dentro (drawGroupComponentOutline)
        static constexpr float cardTop = 16.0f + 1.5f;

        SourceTabs& owner;
        int index;
    };

    float value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool muted (int k) const { return value (sources[(size_t) k].muteId) > 0.5f; }
    bool soloed (int k) const { return solo.load() == first + k; }
    // Audible: con Solo (de cualquiera de los dos grupos), solo la fuente del Solo; sin Solo, según su Mute (como Synth.h)
    bool audible (int k) const { const auto s = solo.load(); return s >= 0 ? s == first + k : ! muted (k); }
    bool sounding (int k) const { return value (sources[(size_t) k].levelId) > 0.0f && audible (k); }

    void setMute (int k, bool on)
    {
        if (auto* p = apvts.getParameter (sources[(size_t) k].muteId))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    }

    void select (int k)
    {
        current = k;
        for (size_t i = 0; i < sources.size(); ++i)
            sources[i].section->setVisible ((int) i == k);
        for (auto& t : tabs) t->repaint();
    }

    void showMenu (int k)
    {
        juce::PopupMenu m;
        m.addItem (1, "Mute", true, muted (k));
        m.addItem (2, soloed (k) ? "Unsolo" : "Solo", true, soloed (k));
        // Solo los parámetros de esta pestaña (y de sus diálogos); Mute no: es de la pestaña, no de sus mandos
        const auto ids = sources[(size_t) k].section->resetIds();
        const auto off = paramsOffDefault (apvts, ids);
        m.addSeparator();
        m.addItem (3, "Reset " + juce::String (sources[(size_t) k].name) + " to defaults"
                          + (off > 0 ? " (" + juce::String (off) + " changed)" : juce::String()), off > 0);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tabs[(size_t) k].get()),
                         [safe = juce::Component::SafePointer<SourceTabs> (this), k, ids] (int id)
                         {
                             if (safe == nullptr || id == 0) return;
                             if (id == 1) safe->setMute (k, ! safe->muted (k));
                             else if (id == 3) resetParamsToDefaults (safe->apvts, ids);
                             else safe->solo.store (safe->soloed (k) ? -1 : safe->first + k); // el otro grupo lo ve en su temporizador
                         });
    }

    void timerCallback() override
    {
        // Repinta las pestañas si cambia qué suena, qué está silenciado o el Solo (preset, automatización, MIDI Learn, el otro grupo)
        unsigned state = (unsigned) (solo.load() + 1) << 24;
        for (size_t k = 0; k < sources.size(); ++k)
            state |= (sounding ((int) k) ? 1u : 0u) << (2 * k) | (muted ((int) k) ? 2u : 0u) << (2 * k);
        if (state != lastState)
        {
            lastState = state;
            for (auto& t : tabs) t->repaint();
        }
    }

    APVTS& apvts;
    std::vector<Source> sources;
    const int first;
    std::atomic<int>& solo;
    std::vector<std::unique_ptr<Tab>> tabs;
    int current = 0;
    unsigned lastState = ~0u;
};

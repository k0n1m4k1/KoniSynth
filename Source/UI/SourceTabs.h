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
            t->setTooltip (juce::String (sources[k].name) + ": click to show its controls. Right-click: Mute / Solo. "
                           "The red LED is lit while this source sounds (level above 0 and not muted). All sources play together.");
            addAndMakeVisible (*t);
        }
        select (0);
        startTimerHz (10);
    }

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
            const auto r = getLocalBounds().toFloat();
            const auto selected = owner.current == index;
            g.setColour (Skin::c (selected ? 0xff2c333b : 0xff1b1f24).brighter (highlighted ? 0.1f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);
            if (selected)
            {
                g.setColour (Skin::c (0xff42a2c8));
                g.fillRect (r.withTop (r.getBottom() - 2.0f));
            }
            // LED rojo encendido/apagado, como los interruptores de la interfaz
            const auto d = juce::jmin (12.0f, r.getHeight() - 8.0f);
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
                const auto tag = label.removeFromRight (18.0f).reduced (1.0f, 4.0f);
                g.setColour (soloed ? Skin::c (0xffd8c040) : Skin::c (0xffc8a020).withAlpha (0.85f));
                g.fillRoundedRectangle (tag, 2.0f);
                g.setColour (Skin::black());
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (tagText, tag, juce::Justification::centred);
            }
            g.setColour (Skin::white().withAlpha (muted ? 0.4f : (selected ? 1.0f : 0.8f)));
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText (getButtonText(), label.toNearestInt(), juce::Justification::centredLeft, 1);
        }

        void clicked() override { owner.select (index); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                owner.showMenu (index);
            else
                juce::Button::mouseDown (e);
        }

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
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tabs[(size_t) k].get()),
                         [safe = juce::Component::SafePointer<SourceTabs> (this), k] (int id)
                         {
                             if (safe == nullptr || id == 0) return;
                             if (id == 1) safe->setMute (k, ! safe->muted (k));
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

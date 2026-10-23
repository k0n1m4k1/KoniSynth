// Fuentes del Synth en pestañas: Osc 1 / Wavetable / Sample / String. Solo se ve el contenido de una, pero todas suenan
// sumadas según su nivel (el mezclador no cambia). Cada pestaña lleva un LED rojo encendido si su fuente suena (nivel > 0
// y sin Mute); clic derecho: Mute / Solo. Silenciada: LED apagado, nombre en gris y etiqueta "M"; en Solo: etiqueta "S".
// Se incluye desde KoniSynth.cpp (mismo TU), tras UI/Widgets.h.
#pragma once

class SourceTabs : public Section, private juce::Timer
{
public:
    struct Source { const char* name; Section* section; const char* levelId; const char* muteId; };

    SourceTabs (APVTS& s, std::vector<Source> list) : Section ({}), apvts (s), sources (std::move (list))
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
                g.setColour (Skin::c (0xffff3030).withAlpha (0.35f));
                g.fillEllipse (led.expanded (3.0f));
            }
            g.setGradientFill (juce::ColourGradient (on ? Skin::c (0xffff8a80) : Skin::c (0xff5a1e1e), led.getX() + d * 0.3f, led.getY() + d * 0.3f,
                                                     on ? Skin::c (0xffd01010) : Skin::c (0xff2a0c0c), led.getRight(), led.getBottom(), true));
            g.fillEllipse (led);
            g.setColour (Skin::black().withAlpha (0.6f));
            g.drawEllipse (led, 1.0f);
            // Nombre y etiqueta M (silenciada) o S (solo)
            const auto muted = owner.muted (index);
            auto label = r.withTrimmedLeft (d + 12.0f);
            const auto tagText = owner.solo == index ? "S" : muted ? "M" : "";
            if (*tagText != 0)
            {
                const auto tag = label.removeFromRight (18.0f).reduced (1.0f, 4.0f);
                g.setColour (owner.solo == index ? Skin::c (0xffd8c040) : Skin::c (0xffc8a020).withAlpha (0.85f));
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
    bool sounding (int k) const { return value (sources[(size_t) k].levelId) > 0.0f && ! muted (k); }

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
        m.addItem (2, solo == k ? "Unsolo" : "Solo", true, solo == k);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tabs[(size_t) k].get()),
                         [safe = juce::Component::SafePointer<SourceTabs> (this), k] (int id)
                         {
                             if (safe == nullptr || id == 0) return;
                             if (id == 1) { safe->solo = -1; safe->setMute (k, ! safe->muted (k)); }
                             else safe->toggleSolo (k);
                         });
    }

    // Solo: silencia las demás guardando su estado; al quitarlo, se restaura
    void toggleSolo (int k)
    {
        if (solo == k)
        {
            for (size_t i = 0; i < sources.size(); ++i)
                setMute ((int) i, savedMutes[i]);
            solo = -1;
            return;
        }
        if (solo < 0)
        {
            savedMutes.clear();
            for (size_t i = 0; i < sources.size(); ++i) savedMutes.push_back (muted ((int) i));
        }
        for (size_t i = 0; i < sources.size(); ++i)
            setMute ((int) i, (int) i != k);
        solo = k;
    }

    void timerCallback() override
    {
        // Repinta las pestañas si cambia qué suena o qué está silenciado (preset, automatización, MIDI Learn)
        unsigned state = 0;
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
    std::vector<std::unique_ptr<Tab>> tabs;
    std::vector<bool> savedMutes;
    int current = 0, solo = -1;
    unsigned lastState = ~0u;
};

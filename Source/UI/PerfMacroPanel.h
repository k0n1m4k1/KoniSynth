// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, KoniSynthProcessor, PerfMacros). No es una cabecera independiente.
#pragma once

// UI: diálogo de las macros de interpretación (Params/PerfMacros.h). Izquierda: las 8 macros. Derecha: nombre de la
// elegida y sus destinos, cada uno con From / To (en el formato del parámetro) y Remove. Los destinos se añaden desde
// el menú contextual de cualquier control (Assign to macro). Los cambios se escriben al momento: con la macro a medio
// recorrido se oye y se ve el destino moverse mientras se ajusta el rango.
class PerfMacroPanel : public juce::Component, private juce::Timer
{
public:
    explicit PerfMacroPanel (KoniSynthProcessor& p, int initial = 0) : koni (p), macros (p.perfMacros), selected (initial)
    {
        for (int m = 0; m < PerfMacros::numMacros; ++m)
        {
            auto& b = macroButtons[(size_t) m];
            b.setClickingTogglesState (false);
            b.onClick = [this, m] { select (m); };
            addAndMakeVisible (b);
        }
        nameLabel.setText ("Name", juce::dontSendNotification);
        addAndMakeVisible (nameLabel);
        nameEditor.setTooltip ("Name shown on the macro knob (up to 24 characters).");
        nameEditor.onTextChange = [this] { macros.setName (selected, nameEditor.getText()); refreshButtons(); };
        addAndMakeVisible (nameEditor);
        for (auto* l : { &paramHeader, &fromHeader, &toHeader })
        {
            l->setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
            addAndMakeVisible (*l);
        }
        hint.setText ("Add a parameter: right-click any control > Assign to macro. From = value with the macro at 0 %, "
                      "To = value at 100 %. Up to 8 parameters per macro; a parameter belongs to one macro.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);
        select (selected);
        startTimerHz (4); // asignaciones hechas desde el menú contextual con el diálogo abierto
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (190);
        for (auto& b : macroButtons)
            b.setBounds (left.removeFromTop (34).reduced (0, 2));
        r.removeFromLeft (16);
        hint.setBounds (r.removeFromBottom (40));
        auto top = r.removeFromTop (30);
        nameLabel.setBounds (top.removeFromLeft (60));
        nameEditor.setBounds (top.removeFromLeft (260));
        r.removeFromTop (10);
        auto head = r.removeFromTop (22);
        layoutColumns (head, paramHeader, fromHeader, toHeader, nullptr);
        for (auto& row : rows)
            layoutColumns (r.removeFromTop (36).reduced (0, 3), row->name, row->from, row->to, &row->remove);
    }

private:
    struct Row
    {
        juce::Label name;
        juce::Slider from, to;
        juce::TextButton remove { "Remove" };
        juce::String id;
    };

    static void layoutColumns (juce::Rectangle<int> r, juce::Component& a, juce::Component& b, juce::Component& c, juce::Component* d)
    {
        a.setBounds (r.removeFromLeft (220));
        r.removeFromLeft (8);
        const auto w = (r.getWidth() - 100) / 2;
        b.setBounds (r.removeFromLeft (w).reduced (4, 0));
        c.setBounds (r.removeFromLeft (w).reduced (4, 0));
        if (d != nullptr) d->setBounds (r.reduced (8, 0));
    }

    void select (int m)
    {
        selected = m;
        nameEditor.setText (macros.name (m) == "Macro " + juce::String (m + 1) ? juce::String() : macros.name (m), juce::dontSendNotification);
        nameEditor.setTextToShowWhenEmpty ("Macro " + juce::String (m + 1), Skin::white().withAlpha (0.35f));
        refreshButtons();
        rebuildRows();
    }

    void refreshButtons()
    {
        for (int m = 0; m < PerfMacros::numMacros; ++m)
        {
            const auto n = (int) macros.targets (m).size();
            auto& b = macroButtons[(size_t) m];
            b.setButtonText (juce::String (m + 1) + "  " + macros.name (m) + (n > 0 ? "  (" + juce::String (n) + ")" : juce::String()));
            b.setToggleState (m == selected, juce::dontSendNotification);
            b.setColour (juce::TextButton::buttonOnColourId, Skin::c (0xff42a2c8).withAlpha (0.55f));
        }
    }

    void rebuildRows()
    {
        rows.clear();
        shownText = koni.apvts.state.getProperty (PerfMacros::mapProp).toString();
        for (const auto& t : macros.targets (selected))
        {
            auto* prm = koni.apvts.getParameter (t.id);
            if (prm == nullptr)
                continue;
            auto row = std::make_unique<Row>();
            row->id = t.id;
            row->name.setText (prm->getName (40), juce::dontSendNotification);
            row->name.setTooltip (paramTooltip (koni.apvts, t.id));
            for (auto* s : { &row->from, &row->to })
            {
                s->setSliderStyle (juce::Slider::LinearHorizontal);
                s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 22);
                s->setRange (0.0, 1.0);
                s->textFromValueFunction = [prm] (double v) { return prm->getText ((float) v, 32); };
                s->valueFromTextFunction = [prm] (const juce::String& text) { return (double) prm->getValueForText (text); };
                s->onValueChange = [this] { writeRows(); };
                addAndMakeVisible (*s);
            }
            row->from.setValue (t.from, juce::dontSendNotification);
            row->to.setValue (t.to, juce::dontSendNotification);
            row->from.setTooltip ("Value of " + prm->getName (40) + " with the macro at 0 %.");
            row->to.setTooltip ("Value of " + prm->getName (40) + " with the macro at 100 %.");
            row->remove.setTooltip ("Remove " + prm->getName (40) + " from this macro (it keeps its current value).");
            row->remove.onClick = [this, id = t.id]
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PerfMacroPanel> (this), id]
                {
                    if (safe == nullptr) return;
                    safe->macros.remove (id);
                    safe->refreshButtons();
                    safe->rebuildRows();
                });
            };
            addAndMakeVisible (row->name);
            addAndMakeVisible (row->remove);
            rows.push_back (std::move (row));
        }
        resized();
    }

    void writeRows()
    {
        std::vector<PerfMacros::Target> list;
        for (auto& row : rows)
            list.push_back ({ row->id, (float) row->from.getValue(), (float) row->to.getValue() });
        macros.setTargets (selected, list);
        shownText = koni.apvts.state.getProperty (PerfMacros::mapProp).toString();
    }

    void timerCallback() override
    {
        if (koni.apvts.state.getProperty (PerfMacros::mapProp).toString() != shownText)
        {
            refreshButtons();
            rebuildRows();
        }
    }

    KoniSynthProcessor& koni;
    PerfMacros& macros;
    int selected = 0;
    std::array<juce::TextButton, PerfMacros::numMacros> macroButtons;
    juce::Label nameLabel, paramHeader { {}, "Parameter" }, fromHeader { {}, "From (macro 0 %)" }, toHeader { {}, "To (macro 100 %)" }, hint;
    juce::TextEditor nameEditor;
    std::vector<std::unique_ptr<Row>> rows;
    juce::String shownText;
};

// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// Editor de macros por frame (Dsp/FrameMacro.h) de un motor de consola: una fila por macro con su texto (sintaxis de Furnace)
// y su gráfica de barras con el bucle y el release. El texto válido se guarda en la propiedad de estado del motor; el
// procesador lo publica al motor en su siguiente tick (≤ 100 ms). El texto con errores no se aplica: se muestra el error.
class MacroPanel : public juce::Component
{
public:
    // labels: nombre de cada macro para este motor (vacío = no la usa); frameHz: frecuencia del driver, para la ayuda
    MacroPanel (KoniSynthProcessor& p, juce::String property, std::array<juce::String, MacroSet::numKinds> labels, juce::String frameNote)
        : koni (p), prop (std::move (property)), names (std::move (labels))
    {
        help.setText ("Values separated by spaces, one per frame (" + frameNote + "). '|' = loop start, '/' = release point "
                      "(the macro waits there while the key is held). Arpeggio: semitones, or @n = absolute note from C-0 (@48 = C4). "
                      "Pitch: change of the period register per frame, added up (+ = lower). Empty = not used.",
                      juce::dontSendNotification);
        help.setFont (juce::FontOptions (14.0f));
        help.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (help);
        const auto all = koni.apvts.state.getProperty (prop).toString();
        for (int k = 0; k < MacroSet::numKinds; ++k)
        {
            auto& row = rows[(size_t) k];
            row.label.setText (names[(size_t) k], juce::dontSendNotification);
            row.label.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            row.editor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));
            row.editor.setText (MacroSet::textOf (all, k), juce::dontSendNotification);
            row.editor.setTooltip (tooltipFor (k));
            row.editor.onTextChange = [this] { apply(); };
            row.graph.kind = k;
            const auto used = names[(size_t) k].isNotEmpty();
            for (auto* c : std::initializer_list<juce::Component*> { &row.label, &row.editor, &row.graph })
                if (used) addAndMakeVisible (*c);
        }
        status.setFont (juce::FontOptions (14.0f));
        addAndMakeVisible (status);
        clear.setTooltip ("Remove all macros of this engine.");
        clear.onClick = [this] { for (auto& r : rows) r.editor.setText ({}, juce::dontSendNotification); apply(); };
        addAndMakeVisible (clear);
        apply();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        help.setBounds (r.removeFromTop (58));
        auto bottom = r.removeFromBottom (32);
        clear.setBounds (bottom.removeFromRight (110));
        status.setBounds (bottom);
        r.removeFromBottom (8);
        int used = 0;
        for (int k = 0; k < MacroSet::numKinds; ++k) used += names[(size_t) k].isNotEmpty() ? 1 : 0;
        const auto h = used > 0 ? r.getHeight() / used : 0;
        for (int k = 0; k < MacroSet::numKinds; ++k)
        {
            if (names[(size_t) k].isEmpty()) continue;
            auto row = r.removeFromTop (h).reduced (0, 4);
            auto& w = rows[(size_t) k];
            w.label.setBounds (row.removeFromLeft (150));
            w.editor.setBounds (row.removeFromTop (30));
            w.graph.setBounds (row.withTrimmedTop (4));
        }
    }

private:
    // Gráfica de una macro: barras por frame, bucle sombreado y release en rojo
    struct Graph : public juce::Component
    {
        int kind = 0;
        FrameMacro macro;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (Skin::black().withAlpha (0.55f));
            g.fillRoundedRectangle (r, 5.0f);
            if (macro.empty()) return;
            int lo, hi;
            MacroSet::range (kind, lo, hi);
            if (kind == MacroSet::arpeggio) { lo = -24; hi = 24; }
            for (size_t i = 0; i < macro.values.size(); ++i)
                if (! macro.absolute[i]) { lo = std::min (lo, macro.values[i]); hi = std::max (hi, macro.values[i]); }
            const auto n = (float) std::max (16, macro.size());
            const auto w = r.getWidth() / n;
            const auto y0 = lo < 0 ? juce::jmap (0.0f, (float) lo, (float) hi, r.getBottom() - 2.0f, r.getY() + 2.0f) : r.getBottom() - 2.0f;
            if (macro.loop >= 0)
            {
                g.setColour (Skin::c (0xff42a2c8).withAlpha (0.18f));
                const auto loopEnd = macro.release > macro.loop ? macro.release + 1 : macro.size();
                g.fillRect (r.getX() + macro.loop * w, r.getY(), (loopEnd - macro.loop) * w, r.getHeight());
            }
            for (int i = 0; i < macro.size(); ++i)
            {
                const auto abs = macro.absolute[(size_t) i] != 0;
                const auto v = (float) macro.values[(size_t) i];
                const auto y = abs ? r.getY() + 2.0f : juce::jmap (v, (float) lo, (float) hi, r.getBottom() - 2.0f, r.getY() + 2.0f);
                g.setColour (abs ? Skin::c (0xffffc040) : Skin::c (0xff7fd0a0));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX() + i * w + 1.0f, std::min (y, y0), r.getX() + (i + 1) * w - 1.0f,
                                                                       std::max (y, y0) + (std::abs (y - y0) < 1.0f ? 2.0f : 0.0f)));
            }
            if (macro.release >= 0)
            {
                g.setColour (Skin::c (0xffff5050));
                g.fillRect (r.getX() + (macro.release + 1) * w - 1.0f, r.getY(), 2.0f, r.getHeight());
            }
        }
    };

    struct Row
    {
        juce::Label label;
        juce::TextEditor editor;
        Graph graph;
    };

    static juce::String tooltipFor (int k)
    {
        switch (k)
        {
            case MacroSet::volume:   return "Volume per frame, 0-15 (scaled by the channel volume and velocity). Example: 15 13 11 9 / 6 3 0";
            case MacroSet::arpeggio: return "Arpeggio per frame in semitones (or @n absolute note). Example of a looped major chord: | 0 4 7";
            case MacroSet::pitch:    return "Pitch per frame: added to the period register each frame (+ = lower). Example of a falling laser: 0 | 4";
            default:                 return "Duty per frame: pulse width 0-3 (12.5 / 25 / 50 / 75 %); on the noise channel, 0 = long and 1 = short noise. Example: 0 1 2 | 1";
        }
    }

    void apply()
    {
        std::array<juce::String, MacroSet::numKinds> texts;
        for (int k = 0; k < MacroSet::numKinds; ++k)
            texts[(size_t) k] = rows[(size_t) k].editor.getText();
        const auto joined = MacroSet::join (texts);
        MacroSet set;
        const auto error = MacroSet::parse (joined, set);
        for (int k = 0; k < MacroSet::numKinds; ++k)
        {
            rows[(size_t) k].graph.macro = set.macro[(size_t) k];
            rows[(size_t) k].graph.repaint();
        }
        if (error.isNotEmpty())
        {
            status.setColour (juce::Label::textColourId, Skin::c (0xffff7070));
            status.setText ("Not applied: " + error, juce::dontSendNotification);
            return;
        }
        status.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.7f));
        status.setText (set.empty() ? "No macros: the engine uses its panel settings." : "Applied.", juce::dontSendNotification);
        koni.apvts.state.setProperty (prop, joined, nullptr);
    }

    KoniSynthProcessor& koni;
    juce::String prop;
    std::array<juce::String, MacroSet::numKinds> names;
    std::array<Row, MacroSet::numKinds> rows;
    juce::Label help, status;
    juce::TextButton clear { "Clear" };
};

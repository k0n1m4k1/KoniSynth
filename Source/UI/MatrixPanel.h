// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, ModMatrix, MatrixUi, Knob, SortableComboBox, ParamMenu, paramTooltip). No es una cabecera independiente.
#pragma once

// UI de la matriz de modulación (Dsp/ModMatrix.h): diálogo con las 8 ranuras. Los ayudantes (destino de cada mando, anillos,
// colores, añadir y quitar rutas) están en UI/MatrixUi.h

// Cuerpo del diálogo "Modulation Matrix": 8 filas (fuente, destino, cantidad) en dos columnas
class MatrixPanel : public juce::Component
{
public:
    explicit MatrixPanel (KoniSynthProcessor& p) : apvts (p.apvts)
    {
        for (int i = 0; i < ModMatrix::numSlots; ++i)
        {
            auto row = std::make_unique<Row>();
            const auto n = juce::String (i + 1);
            row->number.setText (n, juce::dontSendNotification);
            row->number.setJustificationType (juce::Justification::centred);
            row->number.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            setupCombo (row->source, row->sourceAttachment, "mtxSrc" + n, row->menus);
            setupCombo (row->destination, row->destinationAttachment, "mtxDst" + n, row->menus);
            row->amount = std::make_unique<Knob> (apvts, "mtxAmt" + n, "Amount");
            for (auto* c : std::initializer_list<juce::Component*> { &row->number, &row->source, &row->destination, row->amount.get() })
                addAndMakeVisible (c);
            rows.push_back (std::move (row));
        }
        randomFade = std::make_unique<Knob> (apvts, "mtxRandomFade", "Random Fade");
        addAndMakeVisible (*randomFade);
        hint.setText ("Each slot moves one destination per voice. Amber rings on the knobs show the range; the dot, the live value. "
                      "Random Fade: the Random source fades with the filter envelope.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
        addAndMakeVisible (hint);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto bottom = r.removeFromBottom (72); // Random Fade (de toda la matriz) y la indicación
        randomFade->setBounds (bottom.removeFromLeft (80));
        hint.setBounds (bottom.reduced (8, 0));
        const auto colW = r.getWidth() / 2;
        const auto rowH = r.getHeight() / (ModMatrix::numSlots / 2);
        for (int i = 0; i < ModMatrix::numSlots; ++i)
        {
            auto cell = juce::Rectangle<int> (r.getX() + (i / 4) * colW, r.getY() + (i % 4) * rowH, colW, rowH).reduced (6, 4);
            auto& row = *rows[(size_t) i];
            row.number.setBounds (cell.removeFromLeft (28));
            row.amount->setBounds (cell.removeFromRight (80));
            const auto comboH = 28;
            auto combos = cell.withSizeKeepingCentre (cell.getWidth(), comboH);
            row.source.setBounds (combos.removeFromLeft (combos.getWidth() / 2).reduced (4, 0));
            row.destination.setBounds (combos.reduced (4, 0));
        }
    }

private:
    struct Row
    {
        juce::Label number;
        SortableComboBox source, destination;
        std::unique_ptr<APVTS::ComboBoxAttachment> sourceAttachment, destinationAttachment;
        std::unique_ptr<Knob> amount;
        std::vector<std::unique_ptr<ParamMenu>> menus; // tras los controles: se destruyen antes
    };

    void setupCombo (SortableComboBox& combo, std::unique_ptr<APVTS::ComboBoxAttachment>& attachment, const juce::String& id,
                     std::vector<std::unique_ptr<ParamMenu>>& menus)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
            combo.addItemList (choice->choices, 1); // los items deben existir antes del attachment
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, id, combo);
        combo.setTooltip (paramTooltip (apvts, id));
        menus.push_back (std::make_unique<ParamMenu> (apvts, id, combo));
    }

    APVTS& apvts;
    std::vector<std::unique_ptr<Row>> rows;
    std::unique_ptr<Knob> randomFade;
    juce::Label hint;
};

// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, ModMatrix, Knob, SortableComboBox, ParamMenu, paramTooltip). No es una cabecera independiente.
#pragma once

//==============================================================================
// UI de la matriz de modulación (Dsp/ModMatrix.h): diálogo con las 8 ranuras y cálculo de los anillos de modulación de los
// mandos que son destino.
namespace MatrixUi
{
// Mando que representa cada destino y cómo se aplica el desplazamiento a su valor real: sumando en su unidad, en octavas
// (corte) o escalado (resonancia: el destino está en k = 4,4·Reso). Tono, volumen y paneo no tienen un mando propio
struct Target { const char* paramId; int kind; float scale; }; // kind 0 suma, 1 octavas
inline Target targetFor (int destination)
{
    switch (destination)
    {
        case ModMatrix::dstOsc2Pitch:  return { "osc2Semi", 0, 1.0f };
        case ModMatrix::dstCutoff:     return { "cutoff", 1, 1.0f };
        case ModMatrix::dstResonance:  return { "resonance", 0, 1.0f / 4.4f };
        case ModMatrix::dstDrive:      return { "filterDrive", 0, 1.0f };
        case ModMatrix::dstPulseWidth: return { "pulseWidth", 0, 1.0f };
        case ModMatrix::dstOsc1Level:  return { "osc1Level", 0, 1.0f };
        case ModMatrix::dstOsc2Level:  return { "osc2Level", 0, 1.0f };
        case ModMatrix::dstNoiseLevel: return { "noiseLevel", 0, 1.0f };
        case ModMatrix::dstFmIndex:    return { "fmIndex", 0, 1.0f };
        case ModMatrix::dstTimbre:     return { "timbre", 0, 1.0f };
        case ModMatrix::dstWtPosition: return { "wtPosition", 0, 1.0f };
        case ModMatrix::dstOsc2WtPosition: return { "osc2WtPosition", 0, 1.0f };
        default:                       return { nullptr, 0, 1.0f };
    }
}

inline bool bipolar (int source)
{
    return source == ModMatrix::srcLfo1 || source == ModMatrix::srcLfo2 || source == ModMatrix::srcKeyTrack
        || source == ModMatrix::srcRandom || source == ModMatrix::srcPitchBend;
}

// Normalizado (0..1) del parámetro desplazado 'offset' unidades del destino desde su valor actual
inline float shifted (juce::RangedAudioParameter& p, const Target& t, float offset)
{
    const auto range = p.getNormalisableRange();
    const auto base = p.convertFrom0to1 (p.getValue());
    const auto v = t.kind == 1 ? base * std::exp2 (offset) : base + offset * t.scale;
    return p.convertTo0to1 (juce::jlimit (range.start, range.end, v));
}
} // namespace MatrixUi

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
        randomFade = std::make_unique<Knob> (apvts, "mtxRandomFade", "Rnd Fade");
        addAndMakeVisible (*randomFade);
        hint.setText ("Each slot moves one destination per voice. Amber rings on the knobs show the range; the dot, the live value. "
                      "Rnd Fade: the Random source fades with the filter envelope.",
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

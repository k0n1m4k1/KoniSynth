// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// UI: contenedor a tamaño lógico que dibuja flechas de flujo de señal entre secciones consecutivas de una cadena.
struct FlowContent : juce::Component
{
    void paint (juce::Graphics& g) override
    {
        g.setColour (Skin::c (0xff42a2c8).withAlpha (0.8f));
        for (auto p : arrows) // triángulo de 7 × 10 px apuntando a la derecha
        {
            juce::Path t;
            t.addTriangle (p.x - 3.5f, p.y - 5.0f, p.x - 3.5f, p.y + 5.0f, p.x + 3.5f, p.y);
            g.fillPath (t);
        }
        g.setFont (juce::FontOptions (10.0f));
        for (auto& l : arrowLabels) // rótulo bajo la flecha (enlaces que no son audio en serie: "mod" = moduladora)
            g.drawText (l.second, juce::Rectangle<float> (28.0f, 12.0f).withCentre (l.first.translated (0.0f, 12.0f)),
                        juce::Justification::centred);
        g.setColour (Skin::white().withAlpha (0.18f));
        for (auto& d : dividers) // separador vertical entre bloques no relacionados de una misma fila
            g.fillRect (d);
    }

    std::vector<juce::Point<float>> arrows;
    std::vector<std::pair<juce::Point<float>, juce::String>> arrowLabels;
    std::vector<juce::Rectangle<float>> dividers;
};

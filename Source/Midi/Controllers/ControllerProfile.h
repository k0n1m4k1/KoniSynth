// Perfiles de controladores MIDI físicos: disposición de controles, peculiaridades conocidas y mapa por defecto a parámetros
// de KoniSynth. Se incluye desde KoniSynth.cpp (mismo TU, tras MidiLearn). Los CC reales de cada control no van aquí: los
// aprende el asistente Controller Setup (MIDI Learn), porque dependen del preset cargado en el aparato.
#pragma once

struct ControllerControl
{
    enum class Kind { knob, fader, encoder, pad, wheel, pedal };
    const char* name;      // como aparece en el panel del aparato
    Kind kind;
    const char* paramId;   // parámetro de KoniSynth al que se asigna; nullptr = no se asigna (pads: notas)
};

struct ControllerProfile
{
    const char* name;
    std::vector<ControllerControl> controls;
    const char* notes; // peculiaridades (texto en inglés: se muestra en la UI)

    // Controles que el asistente asigna, en el orden en que los pide
    std::vector<const ControllerControl*> learnable() const
    {
        std::vector<const ControllerControl*> list;
        for (const auto& c : controls)
            if (c.paramId != nullptr)
                list.push_back (&c);
        return list;
    }
};

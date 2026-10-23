// Yamaha PSR-EW425 (76 teclas sensibles). Fuentes: PSR-E473/PSR-EW425 Owner's Manual (Functions 003, 005, 054, 062-067; mandos
// LIVE CONTROL pág. 30; usa.yamaha.com/files/download/other_assets/8/1543938/psre473_en_om_a1.pdf) y Data List (Implementation
// Chart, pág. 36). USB-MIDI con el driver estándar de Windows. Envía notas (canal 1: no verificado en la documentación), pitch
// bend (rango con la Function 003, de fábrica 2), CC 64 del pedal (Function 054 = Sustain) y Bank Select + Program Change al
// cambiar de voz; acompañamiento y canción solo si Style Out / Song Out están en ON (de fábrica OFF). Los mandos A/B transmiten
// CC según la Implementation Chart (71/73/74/91/93), pero qué CC corresponde a cada mando no está documentado: los aprende el
// asistente. Mapa por defecto: Knob A -> Cutoff, Knob B -> Resonance.
#pragma once

static const ControllerProfile& yamahaPsrEw425()
{
    using K = ControllerControl::Kind;
    static const ControllerProfile profile {
        "Yamaha PSR-EW425",
        {
            { "Live Control knob A", K::knob, "cutoff" }, { "Live Control knob B", K::knob, "resonance" },
            { "Keyboard (76 keys)", K::pad, nullptr },    { "Pitch bend wheel", K::wheel, nullptr },
            { "Sustain footswitch (CC 64)", K::pedal, nullptr },
        },
        "On the PSR-EW425 (FUNCTION): 062 Local Control OFF, 064 Keyboard Out ON, 065 Style Out OFF, 066 Song Out OFF, 063 External "
        "Clock OFF, 054 Pedal Function 1 (Sustain). Turn off Dual, Split, Harmony and Arpeggio. In KoniSynth set MIDI Ch to 1. "
        "Touch Response (005) sets the velocity curve (4 = Fixed). Set the same bend range on the keyboard (003) and in KoniSynth's "
        "Bend. Knobs A/B: press KNOB ASSIGN until function 1 is lit, then learn them; if they send nothing they only act on the "
        "internal sound. Voice changes send Bank Select + Program Change, which KoniSynth ignores. USB MIDI needs no driver."
    };
    return profile;
}

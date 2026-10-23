// Akai Professional Oxygen Pro Mini (32 teclas). Disposición según el manual de usuario v1.2: 4 knobs absolutos, 1 encoder,
// 4 faders, ruedas de pitch y modulación (CC 1), entrada de pedal, 8 pads × 2 bancos; sin aftertouch. Puertos USB: 1 =
// notas y preset, 3 = Mackie, 4 = editor. La tabla de CC por defecto no está publicada; hay indicios de que los knobs
// mandan CC 120-127 (mensajes de modo de canal), que KoniSynth no puede asignar: el asistente avisa y hay que cambiarlos
// en el Preset Editor del Oxygen. Mapa por defecto acordado con el usuario (2026-10-21): knobs -> Cutoff, Resonance,
// Attack, Release; faders -> Osc 1, Osc 2, Sub y Echo Mix; pads -> notas (Drum Machine, mapa GM).
#pragma once

static const ControllerProfile& oxygenProMini()
{
    using K = ControllerControl::Kind;
    static const ControllerProfile profile {
        "Akai Oxygen Pro Mini",
        {
            { "Knob 1", K::knob, "cutoff" },     { "Knob 2", K::knob, "resonance" },
            { "Knob 3", K::knob, "attack" },     { "Knob 4", K::knob, "release" },
            { "Fader 1", K::fader, "osc1Level" }, { "Fader 2", K::fader, "osc2Level" },
            { "Fader 3", K::fader, "subLevel" },  { "Fader 4", K::fader, "echoMix" },
            { "Encoder", K::encoder, nullptr },  { "Pads 1-8 (A/B)", K::pad, nullptr },
            { "Pitch wheel", K::wheel, nullptr }, { "Mod wheel (CC 1)", K::wheel, nullptr }, { "Pedal", K::pedal, nullptr },
        },
        "Use MIDI port 1 (notes). If a knob sends CC 120-127 (channel mode messages, e.g. All Notes Off), KoniSynth can't "
        "learn it: change that knob to a CC between 0 and 119 in the Oxygen Preset Editor. Pads play notes (Drum Machine: "
        "General MIDI drum map). Mod wheel already controls vibrato (Mod Wheel depth)."
    };
    return profile;
}

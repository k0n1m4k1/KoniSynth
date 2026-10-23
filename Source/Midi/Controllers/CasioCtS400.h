// Casio CT-S400 (Casiotone, 61 teclas). Fuente: "CT-S400/CT-S410/LK-S450 MIDI Implementation" (PDF oficial de Casio) y el
// User's Guide. USB-MIDI class-compliant ("CASIO USB-MIDI"); Bluetooth MIDI con el adaptador WU-BT10. Envía: teclado
// Upper1 en el canal 1 (Upper2 = 2, Lower = 3; acompañamiento, metrónomo y partes 6-7 en los canales 6-16), Note Off con
// velocidad fija 64, pitch bend 14 bits (+ RPN 0 al cambiar el rango), CC64 sustain (0/127), CC72 con el botón SUS,
// CC7, Bank Select + Program Change al cambiar de tono y CC120/121 al cambiar ajustes MIDI. Sin knobs, faders, rueda de
// modulación ni aftertouch: no hay controles que asignar; el perfil da la configuración recomendada.
#pragma once

static const ControllerProfile& casioCtS400()
{
    using K = ControllerControl::Kind;
    static const ControllerProfile profile {
        "Casio CT-S400",
        {
            { "Keyboard (61 keys, channel 1)", K::pad, nullptr }, { "Pitch bend wheel", K::wheel, nullptr },
            { "Sustain pedal (CC 64)", K::pedal, nullptr },        { "SUS button (CC 72)", K::pedal, nullptr },
        },
        "On the CT-S400 (Settings): Local Control Off (so it doesn't play its own sound), MIDI Out Ch Upper1 = 1, Upper2 and "
        "Lower Off (or don't use layer/split), and turn rhythm, accompaniment, arpeggiator and metronome off: they are sent "
        "on channels 6-16. In KoniSynth set MIDI Ch (Voice section) to 1 to ignore those channels. It has no knobs, faders "
        "or mod wheel: use KoniSynth's MIDI Learn with another controller for those. Its tone changes (Program Change) and "
        "the SUS button (CC 72) are ignored unless you learn them. Bend range: set it on the keyboard and in KoniSynth's Bend."
    };
    return profile;
}

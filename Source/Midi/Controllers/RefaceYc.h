// Yamaha reface YC (órgano combo, 37 minimteclas). Fuentes: reface Data List (Yamaha, 23-MAR-2015, págs. 15-19: Control Change
// Table e Implementation Chart; usa.yamaha.com/files/download/other_assets/7/794817/reface_en_dl_b0.pdf) y Owner's Manual
// CS/DX/CP/YC (págs. 32-34 y 41-43). Transmite siempre en el canal 1; notas con velocidad 1-127; sin pitch bend, rueda de
// modulación, sustain ni Program Change. Los sliders, interruptores y la palanca solo envían CC con MIDI Control = ON (ajuste al
// encender: E2 + Standby/On; de fábrica off): drawbars CC 102-110 en 7 pasos (0/21/42/64/85/106/127, arriba = 0), Wave CC 80,
// Vibrato/Chorus CC 79 (0/127) y profundidad CC 77 (5 pasos), Percussion CC 111/112/113, Rotary CC 19 (Off 0, Stop 42, Slow 85,
// Fast 127), Dist CC 18 y Reverb CC 91 (continuos). Pedal FC7: CC 11 siempre. VOLUME y OCTAVE no envían nada.
// Mapa por defecto: drawbars 16', 8' y 4' -> Sub, Osc 1 y Osc 2 (Osc 2 a +1 octava); los otros 6 -> Macros 1-6 (se asignan en el
// diálogo Macros); rotary -> velocidad del Leslie; Dist -> Tube; Reverb -> mezcla de la reverb; FC7 -> Gain.
#pragma once

static const ControllerProfile& yamahaRefaceYc()
{
    using K = ControllerControl::Kind;
    static const ControllerProfile profile {
        "Yamaha reface YC",
        {
            { "Footage 16' (CC 102)", K::fader, "subLevel" },     { "Footage 8' (CC 104)", K::fader, "osc1Level" },
            { "Footage 4' (CC 105)", K::fader, "osc2Level" },     { "Footage 5 1/3' (CC 103)", K::fader, "macro1" },
            { "Footage 2 2/3' (CC 106)", K::fader, "macro2" },    { "Footage 2' (CC 107)", K::fader, "macro3" },
            { "Footage 1 3/5' (CC 108)", K::fader, "macro4" },    { "Footage 1 1/3' (CC 109)", K::fader, "macro5" },
            { "Footage 1' (CC 110)", K::fader, "macro6" },        { "Vibrato/Chorus switch (CC 79)", K::pad, "chorusOn" },
            { "Vibrato/Chorus depth (CC 77)", K::fader, "chorusMix" },
            { "Rotary speed lever (CC 19)", K::pedal, "leslieSpeed" },
            { "Effect: Dist (CC 18)", K::fader, "tubeDrive" },    { "Effect: Reverb (CC 91)", K::fader, "ambientMix" },
            { "Foot controller FC7 (CC 11)", K::pedal, "gain" },
            { "Wave H/V/F/A/Y (CC 80)", K::knob, nullptr },       { "Percussion on/off, type, length (CC 111-113)", K::pad, nullptr },
            { "Keyboard (37 mini keys, channel 1)", K::pad, nullptr },
        },
        "On the YC: power it off, then hold E2 and press Standby/On to turn MIDI Control ON (its sliders, switches and lever send "
        "CC only then; ROTARY SPEED lamps lit = ON), and hold F2 at power-on to turn Local Control off. It always transmits on "
        "channel 1. Drawbars send 7 steps (up = 0), so levels move in steps. No pitch bend, mod wheel or sustain; VOLUME and "
        "OCTAVE send nothing. In KoniSynth turn Leslie, Tube and Reverb on (the YC moves their amounts, not their switches) and "
        "set Osc 2 Octave to +1 so the 4' drawbar is an octave up; give Macros 1-6 their targets in the Macros dialog. Rotary "
        "lever: Off = Stop, Stop and Slow = Chorale, Fast = Tremolo. Wave and Percussion are not mapped (learn them if you like)."
    };
    return profile;
}

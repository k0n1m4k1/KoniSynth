// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Nombre de nota -> número MIDI (C4 = 60), o número directo
//==============================================================================
// Patrones: una cadena por instrumento, un carácter por paso ('.' silencio, 'o' suave, 'x' normal, 'X' acento).
// Bajo (solo Omnichord): 'r' raíz, 'R' raíz una octava abajo, 'f' quinta, 't' tercera.
// Son patrones propios escritos con las convenciones de cada estilo, NO transcripciones de las ROM de Roland ni Suzuki
// (las de la CR-78 solo están publicadas como partitura en su service notes, y las del Omnichord no se han encontrado).
enum class DrumSlot { BD, SD, RS, CP, CH, OH, CY, CB, LT, HT, LC, MA, CL, TB, GU, Bass, count };

struct RhythmPattern
{
    const char* name;
    int numSteps, stepsPerBeat;
    std::vector<std::pair<DrumSlot, const char*>> lanes;
};

static const std::vector<RhythmPattern>& drumPatterns()
{
    using S = DrumSlot;
    static const std::vector<RhythmPattern> list {
        { "Rock 1",     16, 4, { { S::BD, "x.......x.x....." }, { S::SD, "....X.......X..." }, { S::CH, "x.x.x.x.x.x.x.x." } } },
        { "Rock 2",     16, 4, { { S::BD, "x..x..x.x.x....." }, { S::SD, "....X.......X..o" }, { S::CH, "XxxxXxxxXxxxXxxx" } } },
        { "Disco",      16, 4, { { S::BD, "X...X...X...X..." }, { S::SD, "....X.......X..." }, { S::CH, "x...x...x...x..." },
                                 { S::OH, "..x...x...x...x." } } },
        { "Funk",       16, 4, { { S::BD, "x.....x...x..x.." }, { S::SD, "....X..o.o..X..." }, { S::CH, "xxxxxxxxxxxxxxxx" } } },
        { "Electro",    16, 4, { { S::BD, "x.....x..x...x.." }, { S::CP, "....X.......X..." }, { S::CH, "xxxxxxxxxxxxxxxx" },
                                 { S::CB, "..x.....x.x....." } } },
        { "Hip Hop",    16, 4, { { S::BD, "x......x.xx....." }, { S::SD, "....X.......X..." }, { S::CH, "x.x.x.x.x.x.x.x." } } },
        { "Synth Pop",  16, 4, { { S::BD, "x...x...x...x..." }, { S::CP, "....X.......X..." }, { S::CH, "..x...x...x...x." },
                                 { S::LT, "..............xx" } } },
        { "Bossa Nova", 16, 4, { { S::BD, "x..xx..xx..xx..x" }, { S::RS, "x..x..x...x.x..." }, { S::CH, "oooooooooooooooo" } } },
        { "Samba",      16, 4, { { S::BD, "x..xx..xx..xx..x" }, { S::RS, "x.x..x.x.x..x.x." }, { S::CH, "xoxoxoxoxoxoxoxo" },
                                 { S::LC, "......x.......x." } } },
        { "Cha-Cha",    16, 4, { { S::BD, "x.......x......." }, { S::CB, "x...x...x...x..." }, { S::HT, "....x.x........." },
                                 { S::LC, "........x...x.x." }, { S::CH, "x.x.x.x.x.x.x.x." } } },
        { "Mambo",      16, 4, { { S::BD, "...x.......x...." }, { S::CB, "x..x..x...x.x..." }, { S::HT, "..x.......x....." },
                                 { S::LC, "......xx......x." }, { S::CH, "xxxxxxxxxxxxxxxx" } } },
        { "Beguine",    16, 4, { { S::BD, "x.......x......." }, { S::HT, "x..x..x.x......." }, { S::LC, "........x...x.x." },
                                 { S::CH, "x.x.x.x.x.x.x.x." } } },
        { "Tango",      16, 4, { { S::BD, "x...x...x...x.x." }, { S::SD, "............x.x." }, { S::CL, "x...x...x..x.x.." } } },
        { "Reggae",     16, 4, { { S::BD, "........X......." }, { S::RS, "........X......." }, { S::CH, "..x...x...x...x." } } },
        { "March",      16, 4, { { S::BD, "x.......x......." }, { S::SD, "....x.x.....x.xx" }, { S::CY, "x..............." } } },
        { "Foxtrot",    16, 4, { { S::BD, "x.......x......." }, { S::SD, "....x.......x..." }, { S::CY, "o...o...o...o..." } } },
        { "Waltz",      12, 4, { { S::BD, "x..........." }, { S::SD, "....x...x..." }, { S::CH, "x...x...x..." } } },
        { "Shuffle",    12, 3, { { S::BD, "x.....x....." }, { S::SD, "...X.....X.." }, { S::CH, "x.xx.xx.xx.x" } } },
        { "Slow Rock",  12, 3, { { S::BD, "x.....x.x..." }, { S::SD, "...X.....X.." }, { S::CH, "xxxxxxxxxxxx" } } },
        { "Swing",      12, 3, { { S::BD, "o.....o....." }, { S::CY, "x..x.xx..x.x" }, { S::CH, "...x.....x.." } } },
        { "Boogie",     12, 3, { { S::BD, "x..x..x..x.." }, { S::SD, "...X.....X.." }, { S::CH, "x.xx.xx.xx.x" } } },
        // Ritmos del Korg Mini Pops 7 que faltaban (lista de botones: djjondent.blogspot.com; patrones propios)
        { "Rumba",      16, 4, { { S::BD, "x..x....x..x...." }, { S::CL, "x..x...x..x.x..." }, { S::LC, "......x.......x." },
                                 { S::HT, "..x...x...x...x." }, { S::MA, "xoxoxoxoxoxoxoxo" } } },
        { "Merengue",   16, 4, { { S::BD, "x...x...x...x..." }, { S::GU, "x.xxx.xxx.xxx.xx" }, { S::LC, "......xx......xx" },
                                 { S::CH, "x.x.x.x.x.x.x.x." } } },
        { "Habanera",   16, 4, { { S::BD, "x..xx...x..xx..." }, { S::CL, "x..x....x..x...." }, { S::CH, "x.x.x.x.x.x.x.x." } } },
        { "Rock 4",     16, 4, { { S::BD, "x..x.x..x..x.x.." }, { S::SD, "....X.......X..." }, { S::CH, "xxxxxxxxxxxxxx.." },
                                 { S::OH, "..............x." } } },
        { "Latin Swing", 16, 4, { { S::BD, "x.......x.x....." }, { S::CY, "x..x.x..x..x.x.." }, { S::RS, "....x.......x..." },
                                  { S::LC, "......x.......x." } } },
        { "Jazz Waltz", 12, 4, { { S::BD, "x..........." }, { S::CY, "x...x..xx..." }, { S::CH, "....x...x..." } } },
        { "March 6/8",  12, 3, { { S::BD, "x.....x....." }, { S::SD, "..x..x..x..x" }, { S::CY, "x..........." } } },
        // Hardbass / hard house: bombo a negras, charles a contratiempo (pasos 3, 7, 11, 15, donde va el donk), palmada en 2 y 4
        { "Hardbass",   16, 4, { { S::BD, "X...X...X...X..." }, { S::CH, "..x...x...x...x." }, { S::CP, "....x.......x..." },
                                 { S::OH, "..............x." } } },
    };
    return list;
}

// Ritmos del Omnichord (nombres del OM-84 según Acadia Recording y Wikipedia) con su bajo automático
static const std::vector<RhythmPattern>& omnichordPatterns()
{
    using S = DrumSlot;
    static const std::vector<RhythmPattern> list {
        { "Rock 1",    16, 4, { { S::BD, "x.......x.x....." }, { S::SD, "....X.......X..." }, { S::CH, "x.x.x.x.x.x.x.x." },
                                { S::Bass, "r.......r.f....." } } },
        { "Rock 2",    16, 4, { { S::BD, "x..x..x.x.x....." }, { S::SD, "....X.......X..." }, { S::CH, "xxxxxxxxxxxxxxxx" },
                                { S::Bass, "r..r..f.r.f....." } } },
        { "Slow Rock", 12, 3, { { S::BD, "x.....x....." }, { S::SD, "...X.....X.." }, { S::CH, "xxxxxxxxxxxx" },
                                { S::Bass, "r.....f....." } } },
        { "Swing",     12, 3, { { S::BD, "o.....o....." }, { S::CY, "x..x.xx..x.x" }, { S::CH, "...x.....x.." },
                                { S::Bass, "r..t..f..t.." } } },
        { "Waltz",     12, 4, { { S::BD, "x..........." }, { S::CH, "....x...x..." }, { S::Bass, "R..........." } } },
        { "Latin",     16, 4, { { S::BD, "x..xx..xx..xx..x" }, { S::RS, "x..x..x...x.x..." }, { S::CH, "oooooooooooooooo" },
                                { S::Bass, "r..f....r..f...." } } },
        { "Foxtrot",   16, 4, { { S::BD, "x.......x......." }, { S::SD, "....x.......x..." }, { S::CY, "o...o...o...o..." },
                                { S::Bass, "r...f...r...f..." } } },
        { "Country",   16, 4, { { S::BD, "x.......x......." }, { S::SD, "....x.......x..." }, { S::CH, "x.x.x.x.x.x.x.x." },
                                { S::Bass, "R.......f......." } } },
        { "Disco",     16, 4, { { S::BD, "X...X...X...X..." }, { S::SD, "....X.......X..." }, { S::OH, "..x...x...x...x." },
                                { S::Bass, "R.r.R.r.R.r.R.r." } } },
        { "March",     16, 4, { { S::BD, "x.......x......." }, { S::SD, "....x.x.....x.xx" }, { S::Bass, "r.......f......." } } },
        { "Blues",     12, 3, { { S::BD, "x.....x....." }, { S::SD, "...X.....X.." }, { S::CH, "x.xx.xx.xx.x" },
                                { S::Bass, "r.rt.tf.ft.t" } } },
        { "Tango",     16, 4, { { S::BD, "x...x...x...x.x." }, { S::SD, "............x.x." }, { S::Bass, "r...r...f...f.f." } } },
    };
    return list;
}

static float stepVelocity (char c)
{
    switch (c)
    {
        case 'X': return 1.0f;
        case 'x': return 0.75f;
        case 'o': return 0.45f;
        default:  return 0.0f;
    }
}

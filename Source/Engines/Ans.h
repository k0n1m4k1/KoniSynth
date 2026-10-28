// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Sintetizador fotoeléctrico ANS (Evgeny Murzin, 1958; modelo de 1964). Datos con ≥2 fuentes (folleto de VDNKh de 1964 en
// theremin.ru, Wikipedia, 120years.net, entrevista a S. Kreichi en InRock, habr): 720 tonos senoidales, 72 por octava (16,67
// cents), 10 octavas, 5 discos de vidrio de 144 pistas (2 octavas por disco); partitura en una placa de vidrio cubierta de
// masilla negra que se raspa: vertical = altura (1 mm por tono), horizontal = tiempo, anchura de la raya = volumen; velocidad de
// la placa de 0 a 24 mm/s (folleto: una sola fuente); 20 fotocélulas con filtros de media octava = ecualizador de 20 registros.
// Tono más grave: el disco lento gira a 6/64 rps con 225 ciclos por vuelta en su pista interior -> 21,09375 Hz (cálculo a
// partir de las velocidades de disco de journalpmn.ru, una sola fuente, que cuadra con los "21-21600 Hz" de habr); el tono k
// suena a 21,09375·2^(k/72) Hz. Los tonos por encima de Nyquist se callan.
// Sin datos publicados: ley anchura -> amplitud (aquí lineal), irregularidad de la emulsión (parámetro Emulsion: fluctuación lenta
// y aleatoria de amplitud por pista, estimada), desafinación entre discos (no se añade).
// La tecla transpone la partitura en pasos de 1/72 de octava (6 por semitono; C4 = sin transposición).
struct AnsScore
{
    static constexpr int kRows = 720, kColumns = 512; // 512 mm de placa: 21,3 s a 24 mm/s
    std::vector<std::uint8_t> cells = std::vector<std::uint8_t> ((size_t) (kRows * kColumns), 0);
    std::uint8_t at (int row, int col) const { return cells[(size_t) (row * kColumns + col)]; }
    std::uint8_t& at (int row, int col) { return cells[(size_t) (row * kColumns + col)]; }

    static double toneHz (int k) { return 21.09375 * std::exp2 (k / 72.0); }
    static int rowFor (double hz) { return (int) std::lround (72.0 * std::log2 (hz / 21.09375)); }

    // Partituras de fábrica generadas por código (composición propia, inspirada en los usos conocidos del ANS)
    static std::unique_ptr<AnsScore> factory (int which)
    {
        auto s = std::make_unique<AnsScore>();
        auto put = [&s] (int row, int col, int v)
        {
            if (row >= 0 && row < kRows && col >= 0 && col < kColumns)
                s->at (row, col) = (std::uint8_t) std::max ((int) s->at (row, col), juce::jlimit (0, 255, v));
        };
        const auto pi = juce::MathConstants<double>::pi;
        switch (which)
        {
            case 0: // Cluster microtonal (estilo Solaris): 9 tonos a 1/72 de octava alrededor de La 220, entrando y saliendo
                for (int c = 0; c < kColumns; ++c)
                    for (int j = -4; j <= 4; ++j)
                        put (rowFor (220.0) + j, c, (int) (200.0 * std::pow (std::sin (pi * c / kColumns), 2.0)
                                                         * (0.6 + 0.4 * std::sin (0.05 * c + j))));
                break;
            case 1: // Glissandos cruzados
                for (int c = 0; c < kColumns; ++c)
                    for (int g = 0; g < 4; ++g)
                    {
                        const auto base = rowFor (110.0) + g * 72;
                        const auto r = (g & 1) ? base + 120 - c * 120 / kColumns : base + c * 120 / kColumns;
                        for (int w = -1; w <= 1; ++w) put (r + w, c, 180 - 60 * std::abs (w));
                    }
                break;
            case 2: // Bordón con armónicos que se encienden por turnos
                for (int c = 0; c < kColumns; ++c)
                    for (int n = 1; n <= 12; ++n)
                    {
                        const auto row = rowFor (55.0 * n);
                        const auto env = std::max (0.0, std::sin (pi * (c - n * 18.0) / 300.0));
                        put (row, c, (int) (230.0 / std::sqrt ((double) n) * (n == 1 ? 1.0 : env)));
                    }
                break;
            case 3: // Campanas: golpes cortos de parciales inarmónicos que decaen
                for (int hit = 0; hit < 8; ++hit)
                {
                    const auto c0 = 20 + hit * 60;
                    const auto f0 = 196.0 * std::exp2 ((hit % 4) / 4.0);
                    for (const auto ratio : { 1.0, 2.0, 2.76, 5.40, 8.93 })
                        for (int c = 0; c < 70; ++c)
                            put (rowFor (f0 * ratio), c0 + c, (int) (220.0 * std::exp (-c / (20.0 / std::sqrt (ratio)))));
                }
                break;
            case 4: // Bandas de ruido: rayas verticales anchas (todas las pistas a la vez = ruido blanco)
                for (int c = 0; c < kColumns; ++c)
                {
                    const auto band = (c / 64) % 2 == 0;
                    const auto centre = rowFor (300.0 * std::exp2 ((c / 64) * 0.5));
                    for (int r = 0; r < kRows; ++r)
                        if (band) put (r, c, (int) (60.0 * std::exp (-std::pow ((r - centre) / 40.0, 2.0))));
                }
                break;
            default: // Escala de 72 tonos por octava, de La 220 a La 440, paso a paso
                for (int k = 0; k <= 72; ++k)
                    for (int c = 0; c < 6; ++c)
                        put (rowFor (220.0) + k, k * 7 + c, 200);
                break;
        }
        return s;
    }
};

using AnsSlot = DataSlot<AnsScore>;

struct AnsEngine
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { rng.setSeed (seed); }

    void prepare (double sr)
    {
        sampleRate = sr;
        ampCoef = 1.0 - std::exp (-1.0 / (0.001 * sr)); // ataque más rápido del ANS: 1 ms (folleto)
        reset();
    }

    // PANIC (silencio total): con Hold, la placa queda parada hasta la siguiente nota
    void stopHold() { holdStopped = true; }

    void reset()
    {
        amp.fill (0.0f);
        target.fill (0.0f);
        for (int k = 0; k < AnsScore::kRows; ++k) { re[(size_t) k] = 1.0; im[(size_t) k] = 0.0; }
        numHeld = 0;
        column = 0.0;
        wander.fill (1.0f);
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, const AnsScore* score)
    {
        const auto speed = (double) p.ansSpeed->load();            // mm/s = columnas/s
        const auto hold = p.ansHold->load() > 0.5f;
        const auto emulsion = p.ansEmulsion->load();
        std::array<float, 20> reg {};
        for (int b = 0; b < 20; ++b) reg[(size_t) b] = p.ansReg[(size_t) b]->load();
        const auto bend = std::exp2 ((p.pitchBend.load() * 100.0 + p.fineTune->load()) / 1200.0);
        // Rotaciones por tono (frecuencia fija por tono; el pitch bend las desplaza todas)
        for (int k = 0; k < AnsScore::kRows; ++k)
        {
            const auto w = juce::MathConstants<double>::twoPi * AnsScore::toneHz (k) * bend / sampleRate;
            cw[(size_t) k] = std::cos (w);
            sw[(size_t) k] = std::sin (w);
            nyquistOk[(size_t) k] = AnsScore::toneHz (k) * bend < 0.45 * sampleRate;
        }

        auto midiIt = midi.cbegin();
        int lastCol = -1;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
            {
                const auto m = (*midiIt).getMessage();
                if (m.isNoteOn())
                {
                    holdStopped = false;
                    if (numHeld == 0 && ! hold) column = 0.0; // la placa empieza al pulsar
                    if (numHeld < (int) held.size()) held[(size_t) numHeld++] = m.getNoteNumber();
                    transpose = (m.getNoteNumber() - 60) * 6;
                }
                else if (m.isNoteOff())
                {
                    int j = 0;
                    for (int k = 0; k < numHeld; ++k) if (held[(size_t) k] != m.getNoteNumber()) held[(size_t) j++] = held[(size_t) k];
                    numHeld = j;
                    if (numHeld > 0) transpose = (held[(size_t) numHeld - 1] - 60) * 6;
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff()) numHeld = 0;
            }
            const auto playing = score != nullptr && ((hold && ! holdStopped) || numHeld > 0);
            if (playing)
            {
                column += speed / sampleRate;
                if (column >= AnsScore::kColumns) column -= AnsScore::kColumns;
            }
            const auto col = (int) column;
            if (col != lastCol || ! playing)
            {
                // Lectura de la columna: el tono k suena con la fila k - transposición; registros de media octava
                lastCol = col;
                for (int k = 0; k < AnsScore::kRows; ++k)
                {
                    const auto row = k - transpose;
                    const auto v = playing && row >= 0 && row < AnsScore::kRows ? score->at (row, col) : 0;
                    const auto band = juce::jlimit (0, 19, k / 36);
                    target[(size_t) k] = nyquistOk[(size_t) k] ? (float) v / 255.0f * reg[(size_t) band] : 0.0f;
                }
                // Lista de tonos activos (sonando o por sonar): solo esos se calculan hasta la próxima columna
                numActive = 0;
                for (int k = 0; k < AnsScore::kRows; ++k)
                {
                    if (target[(size_t) k] > 0.0f || amp[(size_t) k] > 1.0e-5f) active[(size_t) numActive++] = k;
                    else amp[(size_t) k] = 0.0f;
                }
            }
            double y = 0.0;
            for (int q = 0; q < numActive; ++q)
            {
                const auto k = active[(size_t) q];
                auto& a = amp[(size_t) k];
                const auto t = target[(size_t) k];
                a += (float) ampCoef * (t - a);
                // rotación de la fase (oscilador senoidal recursivo)
                const auto r = re[(size_t) k] * cw[(size_t) k] - im[(size_t) k] * sw[(size_t) k];
                im[(size_t) k] = re[(size_t) k] * sw[(size_t) k] + im[(size_t) k] * cw[(size_t) k];
                re[(size_t) k] = r;
                y += a * wander[(size_t) k] * im[(size_t) k];
            }
            // Renormalización periódica y emulsión (fluctuación lenta por pista, estimada)
            if ((++tick & 1023) == 0)
                for (int k = 0; k < AnsScore::kRows; ++k)
                {
                    const auto n = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
                    re[(size_t) k] /= n; im[(size_t) k] /= n;
                    wander[(size_t) k] += 0.05f * ((1.0f + emulsion * (rng.nextFloat() - 0.5f)) - wander[(size_t) k]);
                }
            const auto o = (float) (0.04 * y);
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, i, o);
        }
        playColumn.store ((int) column);
    }

    std::atomic<int> playColumn { 0 };

private:
    double sampleRate = 44100.0, ampCoef = 0.0, column = 0.0;
    std::array<float, AnsScore::kRows> amp {}, target {}, wander {};
    bool holdStopped = false; // PANIC: Hold sin efecto hasta la siguiente nota
    std::array<double, AnsScore::kRows> re {}, im {}, cw {}, sw {};
    std::array<bool, AnsScore::kRows> nyquistOk {};
    std::array<int, 16> held {};
    std::array<int, AnsScore::kRows> active {};
    int numHeld = 0, transpose = 0, numActive = 0;
    unsigned tick = 0;
    juce::Random rng;
};

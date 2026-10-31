// Se incluye desde KoniSynth.cpp (mismo TU), antes de UI/Visualizer.h; usa SessionTransport y PlayAlongSong
// (Transport/SessionTransport.h, Transport/PlayAlong.h) y JUCE. No es una cabecera independiente.
#pragma once

//==============================================================================
// Vista "Play Along" del display: tu parte del .mid como una partitura en piano roll que avanza de derecha a izquierda
// hacia la línea de golpeo (a la izquierda).
// - Eje vertical en posiciones de pentagrama (pasos diatónicos: do, re, mi...), no en semitonos: cada línea y cada espacio
//   es una nota natural; sostenidos en el carril de su natural con un "#" (sin armadura: siempre sostenidos). La longitud
//   de la barra es la duración.
// - Ventana de ~1,5 octavas que sigue a la melodía (paneo vertical): se centra en las notas de los 2 próximos compases,
//   amortiguada (constante de 0,25 s) y con histéresis (no se mueve mientras quepan, con un paso de margen). Pentagrama de
//   guía de clave de sol o de fa según la altura de la ventana; líneas adicionales cortas fuera de él, como en papel.
// - Colores: pendiente (tinta), acertada (verde), fallada (rojo); la nota o el acorde que se espera (Wait / Follow) parpadea.
//   Tus notas, en una banda fina sobre su tono: verde si acertaron una nota de tu parte, rojo si no.
// - A la izquierda, una tira del teclado completo (A0-C8) con la zona que muestra la ventana. Flechas en el borde si una nota
//   próxima queda fuera. Durante la cuenta previa, los tiempos que faltan.
class PlayAlongView
{
public:
    void paint (juce::Graphics& g, juce::Rectangle<float> r, const SessionTransport& transport, juce::Colour ink, juce::Colour hot)
    {
        const auto song = transport.getSong();
        const auto t = transport.getSelectedTrack();
        g.setFont (juce::FontOptions (12.0f));
        if (song == nullptr || t < 0 || t >= (int) song->tracks.size())
        {
            g.setColour (ink.withAlpha (0.8f));
            g.drawFittedText ("PLAY ALONG\nLoad a MIDI file and pick your part: the \"...\" button next to PLAY (right of the BPM display) > MIDI Music Player",
                              r.toNearestInt(), juce::Justification::centred, 3);
            return;
        }
        const auto& notes = song->notes[(size_t) t];
        const auto pos = transport.practicePosition();
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto dt = juce::jlimit (0.0, 0.1, now - lastPaint);
        lastPaint = now;

        // Zonas: tira de teclado, clave, línea de golpeo; ventana de tiempo de ~2 compases por delante
        auto area = r;
        const auto strip = area.removeFromLeft (10.0f);
        area.removeFromLeft (4.0f);
        const auto header = area.removeFromTop (14.0f);
        // A la derecha, en grande, la próxima nota (o acorde) que te toca
        const auto nextPanel = area.removeFromRight (juce::jlimit (64.0f, 130.0f, area.getWidth() * 0.18f));
        area.removeFromRight (6.0f);
        const auto clefW = 18.0f;
        const auto hitX = area.getX() + clefW + 26.0f;
        const auto bar = barLengthAt (*song, juce::jmax (0.0, pos));
        const auto ahead = juce::jlimit (3.0, 8.0, 2.0 * bar);
        const auto pxPerSec = (area.getRight() - hitX) / (float) ahead;
        const auto xOf = [&] (double sec) { return hitX + (float) (sec - pos) * pxPerSec; };
        const auto t0 = pos - (hitX - area.getX()) / pxPerSec, t1 = pos + ahead;

        // Paneo vertical: notas de los 2 próximos compases (o, si no hay, la siguiente que venga)
        // Pasos diatónicos a la vista: ~12 px cada uno (7 = una octava en un display de 80 px; hasta 13 en uno alto)
        const auto steps = juce::jlimit (7, 13, (int) std::round (area.getHeight() / 12.0f));
        const auto half = steps * 0.5;
        int lo = 1000, hi = -1000;
        for (auto i = firstFrom (notes, pos - 0.5); i < notes.size() && notes[i].start < pos + 2.0 * bar; ++i)
        {
            lo = std::min (lo, diatonic (notes[i].pitch));
            hi = std::max (hi, diatonic (notes[i].pitch));
        }
        if (hi < lo)
            if (const auto i = firstFrom (notes, pos); i < notes.size())
                lo = hi = diatonic (notes[i].pitch);
        if (hi >= lo)
        {
            if (std::isnan (camera)) camera = 0.5 * (lo + hi);
            if (lo < camera - half + 1.0 || hi > camera + half - 1.0) // histéresis: solo si alguna se sale (con un paso de margen)
                target = 0.5 * (lo + hi);
        }
        if (! std::isnan (target))
            camera += (target - camera) * (1.0 - std::exp (-dt / 0.25));
        const auto cam = std::isnan (camera) ? (double) diatonic (67) : camera;
        const auto stepPx = area.getHeight() / (float) steps;
        const auto yOf = [&] (double d) { return area.getCentreY() - (float) (d - cam) * stepPx; };

        // Pentagrama de guía (líneas tenues) y clave: sol (mi4..fa5) o fa (sol2..la3) según la altura de la ventana
        const auto treble = cam >= diatonic (60) - 1;
        const int lines[5] = { treble ? diatonic (64) : diatonic (43), treble ? diatonic (67) : diatonic (47), treble ? diatonic (71) : diatonic (50),
                               treble ? diatonic (74) : diatonic (53), treble ? diatonic (77) : diatonic (57) };
        g.setColour (ink.withAlpha (0.22f));
        for (const auto d : lines)
            g.drawHorizontalLine (juce::roundToInt (yOf (d)), area.getX(), area.getRight());
        g.setColour (ink.withAlpha (0.55f));
        g.setFont (juce::FontOptions (juce::Font::getDefaultSerifFontName(), stepPx * 4.2f, juce::Font::italic));
        g.drawText (treble ? "G" : "F", juce::Rectangle<float> (area.getX(), yOf (treble ? lines[1] : lines[3]) - stepPx * 2.4f, clefW, stepPx * 4.8f),
                    juce::Justification::centred);

        // Compases y tiempos
        for (const auto b : song->beats)
            if (b >= t0 && b <= t1) { g.setColour (ink.withAlpha (0.08f)); g.drawVerticalLine (juce::roundToInt (xOf (b)), area.getY(), area.getBottom()); }
        g.setFont (juce::FontOptions (10.0f));
        for (size_t k = 0; k < song->bars.size(); ++k)
            if (const auto b = song->bars[k]; b >= t0 && b <= t1)
            {
                g.setColour (ink.withAlpha (0.25f));
                g.drawVerticalLine (juce::roundToInt (xOf (b)), area.getY(), area.getBottom());
                g.setColour (ink.withAlpha (0.45f));
                g.drawText (juce::String ((int) k + 1), juce::Rectangle<float> (xOf (b) + 2.0f, area.getY(), 30.0f, 11.0f), juce::Justification::left);
            }

        // Bucle A-B
        if (transport.getLoopB() > transport.getLoopA() && transport.getLoopA() >= 0.0)
        {
            const auto xa = xOf (transport.getLoopA()), xb = xOf (transport.getLoopB());
            g.setColour (ink.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax (area.getX(), xa), area.getY(), juce::jmin (area.getRight(), xb), area.getBottom()));
        }

        // Tus notas tocadas (debajo de las de la pista)
        for (const auto& u : transport.getUserNotes())
        {
            const auto p = u.pitch.load();
            const auto on = u.on.load();
            if (p < 0 || on < t0 - 1.0) continue;
            const auto off = u.off.load() >= on ? u.off.load() : pos;
            const auto y = yOf (diatonic (p));
            g.setColour ((u.ok.load() ? juce::Colour (0xff4cd964) : juce::Colour (0xffff4a3a)).withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (on), y - stepPx * 0.18f, juce::jmax (xOf (on) + 2.0f, xOf (off)), y + stepPx * 0.18f));
        }

        // Notas de tu parte
        const auto waiting = transport.isWaiting();
        const auto blink = std::fmod (now, 0.5) < 0.25;
        int above = 0, below = 0, hits = 0, done = 0;
        g.setFont (juce::FontOptions (juce::jlimit (9.0f, 13.0f, stepPx * 0.95f), juce::Font::bold));
        for (size_t i = firstFrom (notes, t0 - 8.0); i < notes.size() && notes[i].start <= t1; ++i)
        {
            const auto& n = notes[i];
            if (n.end < t0) continue;
            const auto d = diatonic (n.pitch);
            const auto y = yOf (d);
            if (y < area.getY() - stepPx) { ++above; continue; }
            if (y > area.getBottom() + stepPx) { ++below; continue; }
            const auto st = song->statusOf (t, (int) i);
            const auto x0 = xOf (n.start), x1 = juce::jmax (x0 + 4.0f, xOf (n.end));
            // Líneas adicionales: fuera del pentagrama, una por línea entre él y la nota
            g.setColour (ink.withAlpha (0.3f));
            for (auto l = lines[4] + 2; l <= d; l += 2) g.drawHorizontalLine (juce::roundToInt (yOf (l)), x0 - 4.0f, x0 + 10.0f);
            for (auto l = lines[0] - 2; l >= d; l -= 2) g.drawHorizontalLine (juce::roundToInt (yOf (l)), x0 - 4.0f, x0 + 10.0f);
            auto colour = st == PlayAlongSong::hit ? juce::Colour (0xff4cd964) : st == PlayAlongSong::missed ? juce::Colour (0xffff4a3a) : ink;
            if (st == PlayAlongSong::pending && waiting && std::abs (n.start - pos) < 0.09)
                colour = blink ? hot : ink;
            const auto box = juce::Rectangle<float>::leftTopRightBottom (x0, y - stepPx * 0.48f, x1, y + stepPx * 0.48f);
            g.setColour (colour.withAlpha (st == PlayAlongSong::pending ? 0.85f : 0.65f));
            g.fillRoundedRectangle (box, stepPx * 0.4f);
            const auto name = noteName (n.pitch);
            if (sharp (n.pitch))
            {
                g.setColour (colour);
                g.drawText ("#", juce::Rectangle<float> (x0 - 11.0f, y - stepPx, 10.0f, stepPx * 2.0f), juce::Justification::centredRight);
            }
            if (box.getWidth() > 18.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.drawText (name, box.withTrimmedLeft (3.0f), juce::Justification::centredLeft, false);
            }
        }
        for (int i = 0; i < (int) notes.size(); ++i)
            if (const auto st = song->statusOf (t, i); st != PlayAlongSong::pending) { ++done; hits += st == PlayAlongSong::hit ? 1 : 0; }

        paintNext (g, nextPanel, notes, *song, t, pos, waiting && blink, ink, hot);

        // Línea de golpeo (late en Wait / Follow mientras espera)
        g.setColour ((waiting && blink ? hot : ink).withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (hitX - 1.0f, area.getY(), 2.0f, area.getHeight()));
        // Flechas: notas próximas fuera de la ventana
        g.setColour (hot);
        if (above > 0) g.fillPath (arrow (area.getRight() - 10.0f, area.getY() + 4.0f, true));
        if (below > 0) g.fillPath (arrow (area.getRight() - 10.0f, area.getBottom() - 4.0f, false));

        // Tira del teclado completo (A0-C8) con la zona visible
        g.setColour (ink.withAlpha (0.15f));
        g.fillRect (strip);
        const auto stripY = [&] (double d) { return strip.getBottom() - strip.getHeight() * (float) ((d - diatonic (21)) / (diatonic (108) - diatonic (21))); };
        g.setColour (ink.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (strip.getX(), stripY (cam + half), strip.getRight(), stripY (cam - half)));
        g.setColour (ink.withAlpha (0.35f));
        g.drawHorizontalLine (juce::roundToInt (stripY (diatonic (60))), strip.getX(), strip.getRight()); // do central

        // Cabecera: pista, modo, tempo, aciertos; cuenta previa
        static const char* modes[3] = { "PLAY", "WAIT", "FOLLOW" };
        g.setColour (ink.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("PLAY ALONG · " + song->tracks[(size_t) t].name + " · " + modes[juce::jlimit (0, 2, transport.getPracticeMode())] + " · "
                        + juce::String (juce::roundToInt (transport.getTempoScale() * 100.0f)) + " %"
                        + (done > 0 ? " · " + juce::String (hits) + "/" + juce::String (done) : juce::String()),
                    header, juce::Justification::centredLeft, true);
        if (pos < 0.0)
        {
            const auto beat = bar / juce::jmax (1, beatsInBar (*song));
            g.setColour (hot);
            g.setFont (juce::FontOptions (area.getHeight() * 0.5f, juce::Font::bold));
            g.drawText (juce::String ((int) std::ceil (-pos / juce::jmax (0.05, beat))), area, juce::Justification::centred);
        }
    }

    void reset() { camera = target = std::numeric_limits<double>::quiet_NaN(); }

    // Panel NEXT: el primer grupo pendiente (notas que empiezan a la vez, ±80 ms, como en el transporte), en grande; hasta 3
    // nombres de un acorde, uno por línea; debajo, cuánto falta (s). En el instante de tocarla (±150 ms) o esperando, "NOW"
    void paintNext (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<PlayAlongSong::Note>& notes, const PlayAlongSong& song,
                    int t, double pos, bool flash, juce::Colour ink, juce::Colour hot) const
    {
        g.setColour (ink.withAlpha (0.07f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (ink.withAlpha (0.3f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        auto inner = r.reduced (4.0f, 3.0f);
        g.setColour (ink.withAlpha (0.6f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("NEXT", inner.removeFromTop (12.0f), juce::Justification::centred);
        size_t i = firstFrom (notes, pos - 0.15);
        while (i < notes.size() && song.statusOf (t, (int) i) != PlayAlongSong::pending) ++i;
        if (i >= notes.size())
        {
            g.drawText ("END", inner, juce::Justification::centred);
            return;
        }
        juce::StringArray names;
        for (auto j = i; j < notes.size() && notes[j].start <= notes[i].start + 0.08 && names.size() < 3; ++j)
            if (song.statusOf (t, (int) j) == PlayAlongSong::pending)
                names.addIfNotAlreadyThere (noteName (notes[j].pitch));
        const auto wait = notes[i].start - pos;
        const auto now = wait < 0.15 || flash;
        auto foot = inner.removeFromBottom (12.0f);
        g.setColour (now ? hot : ink.withAlpha (0.6f));
        g.drawText (now ? juce::String ("NOW") : "in " + juce::String (wait, 1) + " s", foot, juce::Justification::centred);
        const auto lineH = inner.getHeight() / (float) names.size();
        const auto size = juce::jmin (lineH * 0.9f, inner.getWidth() / 2.4f); // "C#4": 3 caracteres de ≈ 0,62 em, más margen
        g.setColour (now ? hot : ink);
        g.setFont (juce::FontOptions (size, juce::Font::bold));
        for (int k = 0; k < names.size(); ++k)
            g.drawText (names[k], juce::Rectangle<float> (inner.getX(), inner.getY() + lineH * (float) k, inner.getWidth(), lineH),
                        juce::Justification::centred, false);
    }

private:
    // Paso diatónico de una nota MIDI: 7 por octava; los sostenidos, en el paso de su natural
    static int diatonic (int pitch)
    {
        static const int step[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        return (pitch / 12) * 7 + step[pitch % 12];
    }
    static bool sharp (int pitch) { const auto pc = pitch % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    static juce::String noteName (int pitch) { return juce::MidiMessage::getMidiNoteName (pitch, true, true, 4); }

    static size_t firstFrom (const std::vector<PlayAlongSong::Note>& notes, double sec)
    {
        return (size_t) (std::lower_bound (notes.begin(), notes.end(), sec, [] (const PlayAlongSong::Note& n, double s) { return n.start < s; })
                         - notes.begin());
    }

    static double barLengthAt (const PlayAlongSong& s, double sec)
    {
        for (size_t k = 0; k + 1 < s.bars.size(); ++k)
            if (s.bars[k + 1] > sec) return juce::jmax (0.5, s.bars[k + 1] - s.bars[k]);
        return s.firstBarSeconds;
    }
    static int beatsInBar (const PlayAlongSong& s)
    {
        if (s.bars.size() < 2) return 4;
        int n = 0;
        for (const auto b : s.beats) if (b >= s.bars[0] && b < s.bars[1]) ++n;
        return juce::jmax (1, n);
    }

    static juce::Path arrow (float x, float y, bool up)
    {
        juce::Path p;
        const auto s = up ? 1.0f : -1.0f;
        p.addTriangle (x - 5.0f, y + s * 7.0f, x + 5.0f, y + s * 7.0f, x, y);
        return p;
    }

    double camera = std::numeric_limits<double>::quiet_NaN(), target = std::numeric_limits<double>::quiet_NaN(), lastPaint = 0.0;
};

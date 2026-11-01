// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE, Skin y FmAlgorithms ya incluidos). No es una
// cabecera independiente.
#pragma once

// Diagrama de un algoritmo FM: cajas numeradas por operador, líneas de modulación, bus de salida bajo las portadoras y el
// lazo de realimentación. La disposición se calcula del grafo (no hay dibujos por algoritmo): fila = distancia a la salida
// (portadoras abajo), columna = árbol de moduladores sobre cada portadora, de izquierda a derecha en orden de operador
struct AlgorithmGraph
{
    int operators = 0;
    std::vector<std::pair<int, int>> edges; // (modulador, modulado), operadores desde 1
    std::vector<int> carriers;
    int fbFrom = 0, fbTo = 0;               // 0 = sin realimentación

    // DX7: conexiones de FmAlgorithms::flags (la misma lectura de los buses que FmAlgorithms::describe)
    static AlgorithmGraph dx7 (int alg)
    {
        AlgorithmGraph a;
        a.operators = 6;
        std::vector<int> buses[3];
        for (int i = 0; i < 6; ++i)
        {
            const auto f = FmAlgorithms::flags[juce::jlimit (0, 31, alg)][i];
            const auto op = 6 - i;
            const auto in = (f >> 4) & 3, out = f & 3;
            if (f & 0x40) a.fbTo = op;
            if (f & 0x80) a.fbFrom = op;
            if (in != 0)
                for (const auto m : buses[in])
                    a.edges.push_back ({ m, op });
            if (out == 0) a.carriers.push_back (op);
            else if (f & 4) buses[out].push_back (op);
            else buses[out] = { op };
        }
        return a;
    }

    // YM2612 de la Mega Drive (MegaDriveEngine::describe y carriers; OP1-OP4 como en el datasheet). Realimentación en OP1
    static AlgorithmGraph ym2612 (int alg)
    {
        AlgorithmGraph a;
        a.operators = 4;
        a.fbFrom = a.fbTo = 1;
        switch (juce::jlimit (0, 7, alg))
        {
            case 0:  a.edges = { { 1, 2 }, { 2, 3 }, { 3, 4 } }; break;           // 1 -> 2 -> 3 -> 4
            case 1:  a.edges = { { 1, 3 }, { 2, 3 }, { 3, 4 } }; break;           // (1 + 2) -> 3 -> 4
            case 2:  a.edges = { { 1, 4 }, { 2, 3 }, { 3, 4 } }; break;           // (1 + (2 -> 3)) -> 4
            case 3:  a.edges = { { 1, 2 }, { 2, 4 }, { 3, 4 } }; break;           // ((1 -> 2) + 3) -> 4
            case 4:  a.edges = { { 1, 2 }, { 3, 4 } }; break;                     // (1 -> 2) + (3 -> 4)
            case 5:  a.edges = { { 1, 2 }, { 1, 3 }, { 1, 4 } }; break;           // 1 -> 2, 3, 4
            case 6:  a.edges = { { 1, 2 } }; break;                               // (1 -> 2) + 3 + 4
            default: break;                                                       // 1 + 2 + 3 + 4
        }
        const auto mask = MegaDriveEngine::carriers (alg); // las mismas portadoras que resalta el panel
        for (int k = 0; k < 4; ++k)
            if ((mask >> k) & 1) a.carriers.push_back (k + 1);
        return a;
    }

    // OPL2/OPL3 (OplEngine::describe): 2 operadores, conexión FM (0) o AM (1); 4 operadores del OPL3, algoritmos 0-3.
    // Realimentación en OP1
    static AlgorithmGraph opl (bool fourOp, int alg)
    {
        AlgorithmGraph a;
        a.fbFrom = a.fbTo = 1;
        if (! fourOp)
        {
            a.operators = 2;
            if ((alg & 1) == 0) { a.edges = { { 1, 2 } }; a.carriers = { 2 }; }
            else a.carriers = { 1, 2 };
            return a;
        }
        a.operators = 4;
        switch (juce::jlimit (0, 3, alg))
        {
            case 0:  a.edges = { { 1, 2 }, { 2, 3 }, { 3, 4 } }; a.carriers = { 4 }; break;
            case 1:  a.edges = { { 1, 2 }, { 3, 4 } };           a.carriers = { 2, 4 }; break;
            case 2:  a.edges = { { 2, 3 }, { 3, 4 } };           a.carriers = { 1, 4 }; break;
            default: a.edges = { { 2, 3 } };                     a.carriers = { 1, 3, 4 }; break;
        }
        return a;
    }
};

class AlgorithmDiagram : public juce::Component
{
public:
    void setGraph (const AlgorithmGraph& g)
    {
        graph = g;
        layout();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (graph.operators == 0 || cols == 0)
            return;
        const auto area = getLocalBounds().toFloat().reduced (4.0f);
        // Celda: columnas + medio hueco a la derecha para el lazo; filas + el bus de salida
        const auto cell = juce::jmin (area.getWidth() / ((float) cols + 0.5f), area.getHeight() / ((float) rows + 0.7f));
        const auto box = cell * 0.62f;
        const auto origin = juce::Point<float> (area.getCentreX() - cell * ((float) cols + 0.5f) * 0.5f,
                                                area.getCentreY() - cell * ((float) rows + 0.7f) * 0.5f);
        const auto centre = [&] (int op)
        {
            const auto& n = nodes[(size_t) op];
            return juce::Point<float> (origin.x + cell * (n.x + 0.5f), origin.y + cell * ((float) (rows - 1 - n.row) + 0.5f));
        };
        const auto ink = Skin::c (0xffd8dde3), wire = ink.withAlpha (0.75f);
        const auto stroke = juce::jmax (1.0f, cell * 0.035f);
        g.setColour (wire);

        // Modulación: baja del modulador al hueco sobre el modulado, horizontal y entra por arriba
        for (const auto& [from, to] : graph.edges)
        {
            const auto a = centre (from), b = centre (to);
            const auto yMid = b.y - box * 0.5f - (cell - box) * 0.5f;
            juce::Path p;
            p.startNewSubPath (a.x, a.y + box * 0.5f);
            p.lineTo (a.x, yMid);
            p.lineTo (b.x, yMid);
            p.lineTo (b.x, b.y - box * 0.5f);
            g.strokePath (p, juce::PathStrokeType (stroke));
        }
        // Bus de salida bajo las portadoras
        const auto busY = origin.y + cell * ((float) rows + 0.25f);
        float busL = 1.0e9f, busR = -1.0e9f;
        for (const auto c : graph.carriers)
        {
            const auto p = centre (c);
            g.drawLine (p.x, p.y + box * 0.5f, p.x, busY, stroke);
            busL = juce::jmin (busL, p.x);
            busR = juce::jmax (busR, p.x);
        }
        if (! graph.carriers.empty())
        {
            g.drawLine (busL, busY, busR, busY, stroke);
            const auto outX = (busL + busR) * 0.5f;
            g.drawArrow ({ outX, busY, outX, busY + cell * 0.35f }, stroke, cell * 0.14f, cell * 0.12f);
        }
        // Realimentación: sale por la derecha del origen, sube por encima del destino y entra por arriba
        if (graph.fbFrom > 0 && graph.fbTo > 0)
        {
            const auto a = centre (graph.fbFrom), b = centre (graph.fbTo);
            const auto xR = juce::jmax (a.x, b.x) + box * 0.5f + (cell - box) * 0.45f;
            const auto yTop = b.y - box * 0.5f - (cell - box) * 0.3f;
            juce::Path p;
            p.startNewSubPath (a.x + box * 0.5f, a.y);
            p.lineTo (xR, a.y);
            p.lineTo (xR, yTop);
            p.lineTo (b.x, yTop);
            p.lineTo (b.x, b.y - box * 0.5f);
            g.setColour (Skin::accent());
            g.strokePath (p, juce::PathStrokeType (stroke));
        }
        // Cajas: portadoras con el tono de las columnas de portadora del panel
        g.setFont (juce::FontOptions (juce::jlimit (9.0f, 22.0f, box * 0.55f), juce::Font::bold));
        for (int op = 1; op <= graph.operators; ++op)
        {
            const auto c = centre (op);
            const auto r = juce::Rectangle<float> (box, box).withCentre (c);
            const auto isCarrier = std::find (graph.carriers.begin(), graph.carriers.end(), op) != graph.carriers.end();
            g.setColour (isCarrier ? Skin::c (0xff42a2c8).withAlpha (0.55f).overlaidWith (Skin::black().withAlpha (0.25f))
                                   : Skin::black().withAlpha (0.55f));
            g.fillRoundedRectangle (r, box * 0.12f);
            g.setColour (ink);
            g.drawRoundedRectangle (r, box * 0.12f, stroke);
            g.drawText (juce::String (op), r, juce::Justification::centred);
        }
    }

private:
    struct Node { int row = 0; float x = 0.0f; bool placed = false; };

    // Fila: 0 en las portadoras, 1 + la mayor de sus destinos en los moduladores. Columna: árbol de moduladores sobre cada
    // portadora (hojas en columnas consecutivas, cada operador centrado sobre los suyos); un modulador que alimenta a varios
    // se centra después sobre todos si ese hueco está libre
    void layout()
    {
        nodes.assign ((size_t) graph.operators + 1, {});
        cols = rows = 0;
        if (graph.operators == 0)
            return;
        std::function<int (int, int)> rowOf = [&] (int op, int guard) -> int
        {
            if (guard > graph.operators) return 0; // ciclo (no ocurre en estos grafos): corta
            int best = -1;
            for (const auto& [from, to] : graph.edges)
                if (from == op) best = juce::jmax (best, rowOf (to, guard + 1));
            return best + 1;
        };
        for (int op = 1; op <= graph.operators; ++op)
            nodes[(size_t) op].row = rowOf (op, 0);
        float next = 0.0f;
        std::function<float (int)> place = [&] (int op) -> float
        {
            auto& n = nodes[(size_t) op];
            if (n.placed) return n.x;
            n.placed = true;
            std::vector<int> mods;
            for (const auto& [from, to] : graph.edges)
                if (to == op && ! nodes[(size_t) from].placed) mods.push_back (from);
            std::sort (mods.begin(), mods.end());
            if (mods.empty())
            {
                n.x = next;
                next += 1.0f;
            }
            else
            {
                float sum = 0.0f;
                for (const auto m : mods) sum += place (m);
                n.x = sum / (float) mods.size();
            }
            return n.x;
        };
        auto carriers = graph.carriers;
        std::sort (carriers.begin(), carriers.end());
        for (const auto c : carriers)
            place (c);
        for (int op = 1; op <= graph.operators; ++op) // sueltos (no ocurre): a la derecha
            if (! nodes[(size_t) op].placed) { nodes[(size_t) op].placed = true; nodes[(size_t) op].x = next; next += 1.0f; }
        for (int op = 1; op <= graph.operators; ++op)
        {
            std::vector<float> targets;
            for (const auto& [from, to] : graph.edges)
                if (from == op) targets.push_back (nodes[(size_t) to].x);
            if (targets.size() < 2) continue;
            const auto mean = std::accumulate (targets.begin(), targets.end(), 0.0f) / (float) targets.size();
            bool free = true;
            for (int other = 1; other <= graph.operators; ++other)
                if (other != op && nodes[(size_t) other].row == nodes[(size_t) op].row && std::abs (nodes[(size_t) other].x - mean) < 0.9f)
                    free = false;
            if (free) nodes[(size_t) op].x = mean;
        }
        for (int op = 1; op <= graph.operators; ++op)
            rows = juce::jmax (rows, nodes[(size_t) op].row + 1);
        cols = (int) next;
    }

    AlgorithmGraph graph;
    std::vector<Node> nodes;
    int cols = 0, rows = 0;
};

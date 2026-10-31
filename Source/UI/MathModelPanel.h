// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE, KoniSynthProcessor, Skin, Widgets ya
// declarados). No es una cabecera independiente.
#pragma once

// UI: diálogo Formula... de Math Models. Para el modelo elegido muestra:
//  - el nombre del modelo y su referencia publicada,
//  - su fórmula compuesta, en negro sobre una tarjeta clara (SVG de Resources/formulas, generado con
//    tools/make_formula_svgs.py; con Noise Burst y Pick, también la de Buzz),
//  - la explicación: lo que KoniSynth adapta del modelo publicado (lo mismo que docs/engines/synth.md),
//  - la onda real en pantallas como las del display: la fuente Math Models sola, con los parámetros actuales, nota C4
//    (261,63 Hz) a velocidad 100, calculada por una voz propia del procesador (KoniSynthProcessor::renderMathPreview) a 48 kHz.
//    Vista completa (envolvente mín./máx. por columna) y detalle de 4 periodos.
// Columna izquierda: fórmula y explicación al 50 % del alto cada una. Con Modal, la receta (tabla de deslizadores: 8 modos con
// Ratio, Level y Decay) ocupa la parte de abajo y fórmula y explicación pasan a dos pestañas (Formula por defecto); cada cambio
// de la receta se aplica en el momento (setModalRecipe; se guarda con el preset) y una tercera pantalla dibuja sus modos
class MathModelPanel : public juce::Component, private juce::Timer
{
public:
    explicit MathModelPanel (KoniSynthProcessor& p) : koni (p)
    {
        text.setMultiLine (true);
        text.setReadOnly (true);
        text.setScrollbarsShown (true);
        text.setCaretVisible (false);
        text.setFont (juce::FontOptions (15.0f));
        text.setIndents (8, 8);
        addAndMakeVisible (text);

        for (auto* b : { &formulaTab, &explanationTab })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1);
            b->onClick = [this] { resized(); repaint(); };
            addChildComponent (*b);
        }
        formulaTab.setButtonText ("Formula");
        explanationTab.setButtonText ("Explanation");
        formulaTab.setConnectedEdges (juce::Button::ConnectedOnRight);
        explanationTab.setConnectedEdges (juce::Button::ConnectedOnLeft);
        formulaTab.setToggleState (true, juce::dontSendNotification);

        recipeBox.addItem ("Soft Key", softKeyId);
        recipeBox.addItem ("Metal", metalId);
        recipeBox.addItem ("Custom", customId);
        recipeBox.setTooltip ("Soft Key: 4 harmonic modes (1, 2, 3, 7), a soft keyboard attack. Metal: 4 inharmonic modes "
                              "(1, 2.73, 5.41, 8.17), a short metallic hit. Custom: your own modes (move any slider).");
        recipeBox.onChange = [this]
        {
            const auto id = recipeBox.getSelectedId();
            if (id == softKeyId || id == metalId)
            {
                juce::String error;
                if (auto r = ModalRecipe::parse (id == softKeyId ? ModalRecipe::softKey() : ModalRecipe::metal(), error))
                    loadRows (*r);
                applyRows();
            }
        };
        addChildComponent (recipeBox);
        recipeLabel.setText ("Recipe", juce::dontSendNotification);
        addChildComponent (recipeLabel);

        const char* headers[] = { "Mode", "Ratio", "Level", "Decay" };
        for (size_t i = 0; i < header.size(); ++i)
        {
            header[i].setText (headers[i], juce::dontSendNotification);
            header[i].setJustificationType (juce::Justification::centred);
            addChildComponent (header[i]);
        }
        for (size_t k = 0; k < rows.size(); ++k)
        {
            auto& row = rows[k];
            row.name.setText (juce::String ((int) k + 1), juce::dontSendNotification);
            row.name.setJustificationType (juce::Justification::centred);
            addChildComponent (row.name);
            setupSlider (row.ratio, logRange (0.25, 32.0), 3, {},
                         "Frequency of this mode relative to the played note (1 = the note, 2 = an octave up; a value that is "
                         "not a whole number gives an inharmonic, bell or metal-like mode).");
            setupSlider (row.level, { 0.0, 1.0 }, 3, {}, "Amplitude of this mode. 0 = off (the mode is left out).");
            setupSlider (row.decay, logRange (0.005, 10.0), 3, " s",
                         "Time for this mode to fall to 1/e (-8.7 dB), with the Decay knob at its default (3 s); Decay scales "
                         "all modes.");
        }
        status.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (status);

        juce::String error;
        if (auto r = ModalRecipe::parse (koni.getModalRecipe(), error))
            loadRows (*r);
        refresh();
        startTimerHz (5);
    }

    void paint (juce::Graphics& g) override
    {
        // Título: nombre del modelo y, debajo, su referencia
        g.setColour (Skin::white());
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (modelName, titleArea.withHeight (30), juce::Justification::bottomLeft);
        g.setColour (Skin::white().withAlpha (0.7f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::italic));
        g.drawText (reference, titleArea.withTrimmedTop (32), juce::Justification::topLeft);

        if (! cardArea.isEmpty())
            drawCard (g);
        drawOverview (g);
        drawDetail (g);
        if (usesRecipe())
            drawModes (g);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto left = r.removeFromLeft ((r.getWidth() - 16) / 2);
        r.removeFromLeft (16);
        titleArea = left.removeFromTop (52);
        left.removeFromTop (8);
        cardArea = {};
        if (usesRecipe())
        {
            // Receta abajo (fila del desplegable, cabecera, 8 filas de 26 px y estado); arriba, las dos pestañas
            status.setBounds (left.removeFromBottom (22));
            auto table = left.removeFromBottom (28 + 20 + 8 * 26);
            left.removeFromBottom (8);
            auto top = table.removeFromTop (28).withTrimmedBottom (2);
            recipeLabel.setBounds (top.removeFromLeft (60));
            recipeBox.setBounds (top.removeFromLeft (150));
            const auto w = table.getWidth();
            auto head = table.removeFromTop (20);
            const int cols[] = { 46, (w - 46) / 3, (w - 46) / 3, w - 46 - 2 * ((w - 46) / 3) };
            for (size_t i = 0; i < header.size(); ++i)
                header[i].setBounds (head.removeFromLeft (cols[i]));
            for (auto& row : rows)
            {
                auto line = table.removeFromTop (26);
                row.name.setBounds (line.removeFromLeft (cols[0]));
                row.ratio.setBounds (line.removeFromLeft (cols[1]).reduced (2, 1));
                row.level.setBounds (line.removeFromLeft (cols[2]).reduced (2, 1));
                row.decay.setBounds (line.removeFromLeft (cols[3]).reduced (2, 1));
            }
            auto tabs = left.removeFromTop (26);
            formulaTab.setBounds (tabs.removeFromLeft (110));
            explanationTab.setBounds (tabs.removeFromLeft (110));
            left.removeFromTop (6);
            const auto showFormula = formulaTab.getToggleState();
            text.setVisible (! showFormula);
            if (showFormula) cardArea = left; else text.setBounds (left);

            modesArea = r.removeFromBottom (180);
            r.removeFromBottom (10);
        }
        else
        {
            // Fórmula y explicación, la mitad del alto cada una
            cardArea = left.removeFromTop ((left.getHeight() - 10) / 2);
            left.removeFromTop (10);
            text.setVisible (true);
            text.setBounds (left);
            modesArea = {};
        }
        overviewArea = r.removeFromTop ((r.getHeight() - 10) * 3 / 5);
        r.removeFromTop (10);
        detailArea = r;
    }

private:
    static constexpr int excNoise = 0, excPick = 1, excEbow = 4, modal = 5, banded = 9, mallet = 11;
    bool usesRecipe() const { return mode == modal || mode == mallet; }
    static constexpr int softKeyId = 1, metalId = 2, customId = 3;
    static constexpr double previewNote = 261.625565; // C4
    static constexpr float previewVelocity = 100.0f / 127.0f;

    struct Row
    {
        juce::Label name;
        juce::Slider ratio, level, decay;
    };

    static juce::NormalisableRange<double> logRange (double lo, double hi)
    {
        return { lo, hi,
                 [] (double s, double e, double v) { return s * std::pow (e / s, v); },
                 [] (double s, double e, double x) { return std::log (x / s) / std::log (e / s); } };
    }

    void setupSlider (juce::Slider& s, juce::NormalisableRange<double> range, int decimals, const juce::String& suffix,
                      const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        s.setNormalisableRange (range);
        s.setNumDecimalPlacesToDisplay (decimals);
        s.setTextValueSuffix (suffix);
        s.setTooltip (tip);
        s.onValueChange = [this] { if (! loading) applyRows(); };
        addChildComponent (s);
    }

    // Filas desde una receta; las que sobran quedan apagadas (Level 0) con valores de partida
    void loadRows (const ModalRecipe& r)
    {
        const juce::ScopedValueSetter<bool> guard (loading, true);
        for (int k = 0; k < (int) rows.size(); ++k)
        {
            auto& row = rows[(size_t) k];
            const auto on = k < r.count;
            row.ratio.setValue (on ? r.ratio[(size_t) k] : (double) (k + 1), juce::dontSendNotification);
            row.level.setValue (on ? r.amp[(size_t) k] : 0.0, juce::dontSendNotification);
            row.decay.setValue (on ? r.tau[(size_t) k] : 0.1, juce::dontSendNotification);
        }
        updateRowLook();
    }

    static juce::String number (double v)
    {
        auto s = juce::String (v, 4);
        while (s.containsChar ('.') && (s.endsWithChar ('0') || s.endsWithChar ('.')))
            s = s.dropLastCharacters (1);
        return s;
    }

    // Receta en texto (el formato de la propiedad modalRecipe): una línea "ratio nivel caída" por fila con Level > 0
    juce::String rowsText() const
    {
        juce::StringArray lines;
        for (const auto& row : rows)
            if (row.level.getValue() > 0.0)
                lines.add (number (row.ratio.getValue()) + " " + number (row.level.getValue()) + " " + number (row.decay.getValue()));
        return lines.joinIntoString ("\n");
    }

    void selectRecipeItem (const juce::String& recipeText)
    {
        recipeBox.setSelectedId (recipeText == ModalRecipe::softKey() ? softKeyId : recipeText == ModalRecipe::metal() ? metalId : customId,
                                 juce::dontSendNotification);
    }

    void applyRows()
    {
        const auto recipeText = rowsText();
        koni.setModalRecipe (recipeText);
        selectRecipeItem (recipeText);
        updateRowLook();
        status.setText (statusText(), juce::dontSendNotification);
        repaint();
    }

    void updateRowLook()
    {
        for (auto& row : rows)
        {
            const auto alpha = row.level.getValue() > 0.0 ? 1.0f : 0.45f;
            row.ratio.setAlpha (alpha);
            row.decay.setAlpha (alpha);
            row.name.setAlpha (alpha);
        }
    }

    juce::String statusText() const
    {
        const auto error = koni.getModalError();
        return error.isNotEmpty() ? "Not applied: " + error
                                  : juce::String ("Applied (saved with the preset). Modes above 0.45 x the sample rate are muted.");
    }

    void timerCallback() override
    {
        if ((int) koni.apvts.getRawParameterValue ("stringExcite")->load() != mode)
            refresh();
        renderIfChanged();
    }

    void refresh()
    {
        mode = (int) koni.apvts.getRawParameterValue ("stringExcite")->load();
        const auto all = formulation (mode);
        const auto first = all.upToFirstOccurrenceOf ("\n", false, false);
        modelName = first.upToFirstOccurrenceOf (" - ", false, false);
        reference = first.fromFirstOccurrenceOf (" - ", false, false);
        text.setText (all.fromFirstOccurrenceOf ("\n", false, false).trim(), false);
        static const char* files[] = { "noise-burst.svg", "pick.svg", "bow.svg", "hammer.svg", "ebow.svg", "modal.svg", "reed.svg",
                                       "flute.svg", "brass.svg", "banded.svg", "shaker.svg", "mallet.svg" };
        formula = loadFormula (files[juce::jlimit (0, (int) std::size (files) - 1, mode)]);
        // Debajo, el modificador: Buzz en Noise Burst y Pick; Stiff y Tension en Bow, Hammer y E-Bow
        buzzFormula = mode == excNoise || mode == excPick ? loadFormula ("buzz.svg")
                    : mode >= 2 && mode <= excEbow ? loadFormula ("stiff.svg") : nullptr;
        const auto isModal = usesRecipe(); // Modal y Mallet: la receta se edita aquí
        juce::Component* modalParts[] = { &recipeBox, &recipeLabel, &status, &formulaTab, &explanationTab };
        for (auto* c : modalParts)
            c->setVisible (isModal);
        for (auto& h : header)
            h.setVisible (isModal);
        for (auto& row : rows)
            for (juce::Component* c : { (juce::Component*) &row.name, (juce::Component*) &row.ratio, (juce::Component*) &row.level,
                                        (juce::Component*) &row.decay })
                c->setVisible (isModal);
        if (isModal)
        {
            juce::String error;
            if (auto r = ModalRecipe::parse (koni.getModalRecipe(), error))
                loadRows (*r);
            selectRecipeItem (rowsText());
            status.setText (statusText(), juce::dontSendNotification);
        }
        previewKey.clear();
        resized();
        renderIfChanged();
        repaint();
    }

    // SVG incrustado por su nombre de fichero original (negro: se dibuja tal cual sobre la tarjeta clara)
    static std::unique_ptr<juce::Drawable> loadFormula (const char* fileName)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (juce::String (fileName) == BinaryData::originalFilenames[i])
            {
                int size = 0;
                const auto* bytes = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                return juce::Drawable::createFromImageData (bytes, (size_t) size);
            }
        return nullptr;
    }

    // Tarjeta de la fórmula: papel marfil, tinta negra. Fórmula principal centrada; con Buzz, debajo, separada por una línea
    // y con su rótulo. Escala 1,2 como máximo (16 pt del SVG -> ~26 px) y menos si no cabe
    void drawCard (juce::Graphics& g) const
    {
        const auto card = cardArea.toFloat();
        g.setColour (juce::Colour (0xfff7f4ec));
        g.fillRoundedRectangle (card, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
        auto inner = card.reduced (18.0f, 14.0f);
        if (buzzFormula != nullptr)
        {
            auto bottom = inner.removeFromBottom (inner.getHeight() * 0.38f);
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.drawHorizontalLine ((int) bottom.getY(), bottom.getX(), bottom.getRight());
            bottom.removeFromTop (6.0f);
            g.setColour (juce::Colour (0xff4a4a4a));
            g.setFont (juce::FontOptions (13.0f, juce::Font::italic));
            g.drawText (mode <= excPick ? "Buzz (when the Buzz knob is above 0)" : "Stiff and Tension (when their knobs are above 0)",
                        bottom.removeFromTop (18.0f), juce::Justification::centredLeft);
            drawFormula (g, *buzzFormula, bottom.reduced (0.0f, 4.0f));
            inner.removeFromBottom (8.0f);
        }
        if (formula != nullptr)
            drawFormula (g, *formula, inner);
    }

    static void drawFormula (juce::Graphics& g, const juce::Drawable& d, juce::Rectangle<float> area)
    {
        const auto b = d.getDrawableBounds();
        if (b.isEmpty() || area.isEmpty())
            return;
        const auto scale = juce::jmin (1.2f, area.getWidth() / b.getWidth(), area.getHeight() / b.getHeight());
        d.drawWithin (g, area.withSizeKeepingCentre (b.getWidth() * scale, b.getHeight() * scale), juce::RectanglePlacement::centred, 1.0f);
    }

    // Onda: se recalcula solo si cambia algún parámetro de Math Models o la receta (cada 200 ms se compara la clave)
    void renderIfChanged()
    {
        juce::String key (mode);
        for (const auto* id : { "stringDecay", "stringBright", "stringPick", "stringDamping", "stringPickup", "stringBowPressure",
                                "stringHammerFelt", "stringEbowDrive", "stringBuzz", "stringHarmonic", "stringStiff", "stringTension", "reedEmbouchure", "reedNoise",
                                "fluteJet", "fluteNoise", "brassLip", "brassSlide", "bandedShape", "bandedExcite",
                                "shakerType", "shakerObjects", "shakerShake", "shakerTone", "malletHardness", "malletMass",
                                "malletDamp" })
            key << ' ' << koni.apvts.getRawParameterValue (id)->load();
        key << ' ' << koni.getModalRecipe();
        if (key == previewKey)
            return;
        previewKey = key;
        // E-Bow crece despacio (la mitad de su nivel en 0,35-0,81 s, medido): 1,5 s con la tecla pulsada; Banded con arco, -6 dB
        // de su nivel en 0,95 s (prototipo): 1,5 s pulsada y 0,5 s de cola. Los demás, 0,7 s con la tecla soltada a 0,5 s (Bow:
        // el arco se retira)
        seconds = mode == excEbow ? 1.5 : bowedBanded() ? 2.0 : 0.7;
        gateSeconds = mode == excEbow ? seconds : bowedBanded() ? 1.5 : 0.5;
        wave.assign ((size_t) std::round (seconds * KoniSynthProcessor::mathPreviewRate), 0.0f);
        koni.renderMathPreview (wave, previewNote, previewVelocity, (int) std::round (gateSeconds * KoniSynthProcessor::mathPreviewRate));
        peak = 1.0e-9f;
        for (auto v : wave)
            peak = juce::jmax (peak, std::abs (v));
        repaint();
    }

    // Detalle: 4 periodos de C4 desde 0,1 s (E-Bow: desde 1,2 s, ya crecida)
    double detailStart() const { return mode == excEbow || bowedBanded() ? 1.2 : 0.1; }
    bool bowedBanded() const { return mode == banded && koni.apvts.getRawParameterValue ("bandedExcite")->load() > 0.5f; }
    static constexpr double detailLength = 4.0 / previewNote;

    // Pantalla como las del display (Skin::screen + glass): retícula de 8 × 4, rótulos en monoespaciada del color de la traza.
    // Devuelve el área de trazado; el contenido se dibuja entre screenBegin y screenEnd
    static juce::Rectangle<float> screenBegin (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& topLeft,
                                               const juce::String& topRight)
    {
        const auto screen = area.toFloat();
        Skin::screen (g, screen, 6.0f);
        g.saveState();
        g.reduceClipRegion (area.reduced (4));
        const auto plot = screen.reduced (12.0f, 10.0f).withTrimmedTop (14.0f);
        const auto trace = Skin::trace();
        g.setColour (trace.withAlpha (0.08f));
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * (float) i / 8.0f), plot.getY(), plot.getBottom());
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) i / 4.0f), plot.getX(), plot.getRight());
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        g.setColour (trace.withAlpha (0.85f));
        const auto label = screen.reduced (12.0f, 7.0f);
        g.drawText (topLeft, label, juce::Justification::topLeft, false);
        g.setColour (trace.withAlpha (0.55f));
        g.drawText (topRight, label, juce::Justification::topRight, false);
        return plot;
    }

    static void screenEnd (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.restoreState();
        Skin::glass (g, area.toFloat(), 6.0f);
    }

    // Traza con halo, como la del display: 5 px al 18 % y 1,8 px encima
    static void strokeTrace (juce::Graphics& g, const juce::Path& p)
    {
        const auto trace = Skin::trace();
        g.setColour (trace.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (trace);
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

    void drawOverview (juce::Graphics& g) const
    {
        if (overviewArea.isEmpty())
            return;
        const auto a = screenBegin (g, overviewArea,
                                    "C4  VEL 100  " + juce::String (seconds, 1) + " S"
                                        + (gateSeconds < seconds ? "  KEY UP " + juce::String (gateSeconds, 1) + " S" : juce::String()),
                                    "PEAK " + juce::String (peak, 3) + "  (BEFORE LEVEL)");
        if (! wave.empty())
        {
            // Envolvente mín./máx. por columna, rellena, y su contorno con halo
            const auto n = wave.size();
            const auto columns = juce::jmax (2, (int) a.getWidth());
            const auto scale = 0.48f * a.getHeight() / peak;
            juce::Path upper, lower, body;
            for (int x = 0; x < columns; ++x)
            {
                const auto i0 = n * (size_t) x / (size_t) columns, i1 = juce::jmax (i0 + 1, n * (size_t) (x + 1) / (size_t) columns);
                auto lo = wave[i0], hi = wave[i0];
                for (auto i = i0; i < i1 && i < n; ++i)
                    lo = juce::jmin (lo, wave[i]), hi = juce::jmax (hi, wave[i]);
                const auto px = a.getX() + (float) x;
                const auto yHi = a.getCentreY() - hi * scale, yLo = a.getCentreY() - lo * scale;
                if (x == 0) { upper.startNewSubPath (px, yHi); lower.startNewSubPath (px, yLo); }
                else        { upper.lineTo (px, yHi); lower.lineTo (px, yLo); }
                g.setColour (Skin::trace().withAlpha (0.45f));
                g.drawVerticalLine ((int) px, yHi, yLo + 1.0f);
            }
            strokeTrace (g, upper);
            strokeTrace (g, lower);
            // Ventana del detalle
            const auto x0 = a.getX() + a.getWidth() * (float) (detailStart() / seconds);
            const auto w = juce::jmax (2.0f, a.getWidth() * (float) (detailLength / seconds));
            g.setColour (Skin::trace().withAlpha (0.14f));
            g.fillRect (x0, a.getY(), w, a.getHeight());
        }
        screenEnd (g, overviewArea);
    }

    void drawDetail (juce::Graphics& g) const
    {
        if (detailArea.isEmpty())
            return;
        const auto a = screenBegin (g, detailArea, "DETAIL  4 PERIODS FROM " + juce::String (detailStart(), 1) + " S", "OWN SCALE");
        const auto rate = KoniSynthProcessor::mathPreviewRate;
        const auto i0 = (size_t) (detailStart() * rate);
        const auto count = (size_t) (detailLength * rate);
        if (i0 + count <= wave.size())
        {
            auto local = 1.0e-9f;
            for (size_t i = 0; i < count; ++i)
                local = juce::jmax (local, std::abs (wave[i0 + i]));
            juce::Path p;
            for (size_t i = 0; i < count; ++i)
            {
                const auto x = a.getX() + a.getWidth() * (float) i / (float) (count - 1);
                const auto y = a.getCentreY() - wave[i0 + i] / local * 0.48f * a.getHeight();
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            strokeTrace (g, p);
        }
        screenEnd (g, detailArea);
    }

    // Modal: una barra por modo encendido; posición = Ratio (escala logarítmica, 0,25 a 32), altura = Level, anchura = Decay
    void drawModes (juce::Graphics& g) const
    {
        auto a = screenBegin (g, modesArea, "MODES", "X = RATIO  H = LEVEL  W = DECAY");
        auto axis = a.removeFromBottom (14.0f);
        const auto xOf = [a] (double ratio) { return a.getX() + a.getWidth() * (float) (std::log (ratio / 0.25) / std::log (128.0)); };
        const auto trace = Skin::trace();
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        for (const auto r : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 })
        {
            const auto x = xOf (r);
            g.setColour (trace.withAlpha (0.12f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (trace.withAlpha (0.55f));
            g.drawText ("x" + number (r), juce::Rectangle<float> (x - 20.0f, axis.getY(), 40.0f, 14.0f), juce::Justification::centred);
        }
        for (const auto& row : rows)
        {
            const auto level = (float) row.level.getValue();
            if (level <= 0.0f)
                continue;
            const auto x = xOf (row.ratio.getValue());
            const auto w = 3.0f + 9.0f * (float) (std::log (row.decay.getValue() / 0.005) / std::log (2000.0));
            const auto h = level * a.getHeight();
            const juce::Rectangle<float> bar (x - 0.5f * w, a.getBottom() - h, w, h);
            g.setColour (trace.withAlpha (0.18f));
            g.fillRect (bar.expanded (2.0f, 0.0f).withTrimmedTop (-2.0f));
            g.setColour (trace.withAlpha (0.85f));
            g.fillRect (bar);
        }
        screenEnd (g, modesArea);
    }

    // Formulación de cada modelo, referencia y adaptaciones (en inglés, como la UI; el detalle está en docs/engines/synth.md).
    // Primera línea: "Nombre - referencia"
    static juce::String formulation (int m)
    {
        const juce::String common =
            "\n\nCommon to the string models: the string is a delay loop whose length sets the pitch (fractional, read with "
            "Hermite interpolation, so bend and vibrato glide). Decay sets the loss per period (T60 of the fundamental). "
            "Harmonic n plays the string as if a finger damped the node at 1/n: only modes k*n remain. Pickup filters the "
            "output as a magnetic pickup at that position. Stiff (Van Duyne & Smith 1994; Rauhala & Valimaki 2006) puts 16 "
            "first-order all-pass filters in the loop so that partial k sounds at k f0 sqrt(1 + B k^2); adapted: their "
            "closed-form coefficient is accurate only up to about C4 and B = 1e-4 (above C5 it bends the partials flat), so "
            "the coefficient is solved to put partial 12 exactly in place (partials 1-24 within 1 cent at B = 1e-5, 8 "
            "cents at 1e-4). Tension (Valimaki, Tolonen & Karjalainen 1999) shortens the loop with the power of the "
            "string slopes, P = sum (s_k + s_N-1-k)^2, integrated by one pole: a loud note starts sharp and settles, and "
            "missing partials appear; adapted: the power is taken relative to its peak in the note (at 100 % and velocity "
            "127 the attack is a semitone sharp with any excitation; the absolute power of the paper made a pick 3 "
            "semitones sharp at 30 %), in Bow, Hammer and E-Bow it is the energy of the two velocity loops (no cross "
            "term), and the pole sits at the note (the paper gives no value).";
        switch (m)
        {
            case 0: return "Noise Burst - Karplus & Strong (1983), with the extensions of Jaffe & Smith (1983)\n"
                           "Start: one period of white noise, low-pass filtered (Bright) and comb filtered at Position (beta). "
                           "Loop: each period the string is averaged with its neighbour sample (S) and scaled by rho; S and rho "
                           "give the Decay time. Above ~650 Hz S grows instead (decay stretching).\n\n"
                           "Adapted: fractional tuning by Hermite interpolation instead of an all-pass filter; Damping "
                           "(extra high-frequency loss with tuning kept) and Pickup are KoniSynth additions; DC blocker at "
                           "20 Hz; the burst is normalised to a 0.8 peak.\n\n"
                           "Buzz (Siddiq 2012, adapted): the 1 % of the string next to the bridge is computed by finite "
                           "differences against a curved bridge p(x) it cannot cross (the rest stays a delay loop, so the "
                           "tuning stays exact)." + common;
            case 1: return "Pick - Jaffe & Smith (1983): the initial shape of a plucked string\n"
                           "Start: a triangle with its tip at Position (beta): harmonic k falls as sin(pi*k*beta) / k^2. "
                           "Then the same loop as Noise Burst.\n\n"
                           "Adapted: Bright rounds the tip with a forward and backward low-pass (a wider plectrum or a "
                           "finger); in the ideal model the tip is sharp. Buzz as in Noise Burst (Siddiq 2012, adapted)." + common;
            case 2: return "Bow - McIntyre, Schumacher & Woodhouse (1983); waveguide form by Smith (1986), as in STK Bowed\n"
                           "Two loops meet at the bow (Position): bridge side beta*N, nut side (1 - beta)*N. The bow sees the "
                           "difference dv between its speed and the string's; the friction table T passes most of it when dv is "
                           "small (the bow grips) and little when it is large (it slips): the Helmholtz motion. "
                           "P = Press, v_bow = 0.03 + 0.2 * velocity.\n\n"
                           "Adapted: the bridge loss comes from Decay and Bright (STK: a fixed pole of 0.658 and gain 0.95); "
                           "no violin body filter (STK uses six); the tuning compensates the loss filter's phase delay "
                           "(STK subtracts 4 samples); the bow speeds up in 20 ms (estimated) and is lifted at once when "
                           "the key is released." + common;
            case 3: return "Hammer - Hall (1992): the felt as a nonlinear spring\n"
                           "x = felt compression, m = hammer mass, Felt sets the stiffness K. The string receives F / 2 on each "
                           "side of the hammer. A harder or faster strike gives a shorter contact and a brighter tone (measured: "
                           "5.2 ms to 1.5 ms of contact).\n\n"
                           "Adapted: own units (string impedance 1, mass 0.001); p = 2.5 is inside the measured range "
                           "(2.2-3.5 for used hammers, D. Russell); explicit Euler integration, K limited to 1e10 for "
                           "stability; the hammer stops being computed 20 ms after it leaves the string." + common;
            case 4: return "E-Bow - after Heet's device (patent US 4,075,921, 1978)\n"
                           "A coil senses the string speed v_s at Position and another drives it with that signal, amplified "
                           "(G from Drive) and saturated. It starts from a tiny noise and grows (measured: half its level in "
                           "0.35-0.81 s).\n\n"
                           "Adapted: there is no published synthesis model of the device; this one is KoniSynth's own "
                           "(saturating feedback at Position while the key is held)." + common;
            case 6: return "Reed - Guillemain, Kergomard & Voinier (2005): real-time clarinet with digital impedance models\n"
                           "Physical variables instead of waves: p_e (pressure at the mouthpiece), u_e (flow) and x (reed opening). The reed is a damped mass on a spring (x), the air flows through it by Bernoulli (u_e, solved exactly each sample: W is the opening), and the cylindrical bore answers through its impedance: two loops of D samples (half a period) with the losses of a 7 mm bore (a, b). gamma is the blowing pressure "
                           "(velocity: 0.34 soft to 0.45 loud; it speaks above 1/3), zeta the embouchure. The sound is the "
                           "pressure outside the bell, the change of p_e + u_e.\n\n"
                           "Adapted, measured against recordings of a Bb clarinet (University of Iowa, pp, mf and ff at C4: harmonics within 4 to 8 dB): reed damping 0.6 (paper: 0.2 for a clarinet, 0.3 in its example; with them the model squeaks on the third mode); a low-pass at 1.5 kHz on the outside pressure (the paper warns that it overestimates the highs); a fractional delay with the losses' and the reed's delay taken out (paper: a whole number of samples): in tune within 7 cents from C2 to C6; on high notes the pressure stays above 0.40 (lower, it jumps to the third mode); level evened out along the keyboard. Its own controls: Embouchure and Noise.";
            case 7: return "Flute - Karjalainen et al. (1991), Valimaki et al. (1992); jet polynomial by Cook; as in STK Flute\n"
                           "A tube and an air jet that crosses the mouth hole: the jet takes a while to reach the edge (Jet), where it splits in and out of the tube, approximated by x(x^2 - 1). The tube reflects half of the wave at each end (0.5), with a one-pole low-pass (LP) as its losses.\n\n"
                           "Adapted, measured against recordings of a flute (University of Iowa, C4): STK's tube has one inversion, like a closed pipe, so it had no even harmonics (H2 26 dB down, 0 dB in a flute); here it is open at both ends (two inversions, one period), and the jet loop is two periods: in tune within 22 cents. STK's jet does not brighten when blown harder, so a tilt with velocity does it (s): calibrated, not physical; the brightness of pp, mf and ff is within 14 % of the flute. Blowing pressure 1 + 0.3 x velocity (STK: 1.1 + 0.2): above it the model has two tones and the noise picks one. No built-in vibrato, seeded noise, rates scaled to the sample rate. Its own controls: Jet and Noise.";
            case 8: return "Brass - Cook (1991, TBone); as in STK Brass (Cook & Scavone)\n"
                           "Lips as a mass on a spring: a resonant filter (lip) tuned to f_lip, driven by the pressure difference between the mouth and the tube. Its output squared is the opening a of the lips, which mixes mouth and tube pressure into the tube. The tube is two periods long and the lips lock to its harmonic nearest to f_lip; Lip moves between them.\n\n"
                           "Adapted (derivation and measurements in the manual): STK's lips have a fixed gain and bandwidth, so below about 650 Hz the breath holds them wide open and they do not oscillate (silent from C2 to C4); here they keep the same static opening and Q (15) on every note and sit 3.5 % below it: in tune within 17 cents from C2 to C6. STK gives the pressure inside the tube; the sound of a trumpet is what leaves the bell, which lets the highs out (a 2nd-order high-pass at 1.5 kHz): against a trumpet (University of Iowa, C4) the brightness is within 8 % at mf and ff. Level evened out along the keyboard. Its own controls: Lip (2 octaves around the note) and Slide (tube length, 0.5 to 1.5 times).";
            case 9: return "Banded - Essl & Cook (1999): banded waveguides; as in STK BandedWG\n"
                           "Each mode k of a bar, glass or bowl is its own loop: a delay of one period of that mode (L_k) and "
                           "a narrow band-pass (BP_k, 32 Hz wide) that lets only that mode circulate, with a loss g_k per trip. "
                           "Strike fills every loop at once; Bow drives them all with the friction table T of a bow (P = "
                           "Press) and the difference between the bow speed and the sum of the loops. Shapes: Uniform Bar, "
                           "Tuned Bar (vibraphone), Glass Harmonica and Tibetan Bowl (12 measured modes, Essl et al. 2002).\n\n"
                           "Adapted: STK rounds each delay to whole samples, so the modes fall off the centre of their "
                           "band-pass and lose gain; here the delay is fractional with the band-pass phase taken out, and the "
                           "band-pass gain at its centre is 1: every mode in tune (prototype: within 1 cent; STK up to 12 "
                           "cents off). Decay scales every loss (x1 at 3 s); the bowl's lossless modes get 0.9995 per trip; "
                           "no 1568 Hz limit (a mode is muted above 0.45 x the sample rate). A bowed note starts with a light "
                           "strike, the bow touching the bar (from rest it took 1.65 s to speak; so 0.95 s). Struck bars "
                           "use a soft mallet: an 8th-order low-pass on the modes whose cutoff f_c opens with velocity "
                           "(2012 Hz at 80, 2476 Hz at 120), fitted to a vibraphone (University of Iowa, C4-B4: mode 4 within "
                           "2 dB at mf and ff; STK strikes every mode alike and was 13 dB brighter). Its own "
                           "controls: Shape, Excite and Press (with Bow).";
            case 10: return "Shaker - Cook (1997): PhISEM, physically informed stochastic event modeling; as in STK Shakers\n"
                            "A shake gives energy E that fades (d_s, Shake). Each sample a bean or jingle hits the shell with "
                            "probability N / 1024 (N = Objects) and adds E to the sound level S, which fades much faster (d_l); "
                            "S drives a few resonances (the shell, the jingles) and a final filter. More objects give a "
                            "smoother, quieter hiss (gain ln N / N). Tambourine, Sleigh Bells and Bamboo Chimes move their "
                            "resonances a little at each hit.\n\n"
                            "Adapted: STK's constants are per sample at 44.1 kHz; here they keep their timing at any sample "
                            "rate. The resonances follow the note (at C4, the instrument) and Tone (STK's frequency control); "
                            "Objects goes from a quarter to four times the instrument's count. STK goes silent the moment the "
                            "energy drops below 0.001, cutting the resonances (a click); here they ring out. Seeded random "
                            "hits. Its own controls: Type, Objects, Shake and Tone.";
            case 11: return "Mallet - Avanzini & Rocchesso (2001): collision sounds with a nonlinear contact force "
                            "(Hunt & Crossley 1975); as in the Sound Design Toolkit\n"
                            "A mallet of mass m hits the modes of the Modal recipe: each mode k is a damped oscillator p_k "
                            "pushed by the contact force f. While the felt is compressed (x > 0) it pushes back as k x^2.8, "
                            "plus a loss mu that grows with the compression speed. The harder or faster the strike, the "
                            "shorter the contact and the more high modes it excites (measured with a 1 ms head: 1.6 ms at strike speed 0.3, 1.1 ms "
                            "at 0.8; strike speed = 0.2 + 0.8 x velocity). A heavy mallet bounces several times and damps the modes.\n\n"
                            "Adapted: Hardness sets the contact time on a rigid surface at speed 1 (10 ms to 0.3 ms) and k "
                            "follows from it (Hertz impact integral), so Hardness does not depend on Mass; every mode has "
                            "unit mass and gain 1 at the struck point, and each one is heard with A_k omega_k, so a hard, "
                            "light strike sounds as the recipe; the force is explicit, from the previous sample (as in the "
                            "Sound Design Toolkit; the paper iterates Newton-Raphson). Decay scales every decay as in Modal. "
                            "Its own controls: Hardness, Mass and Damp; the modes are edited below.";
            default: return "Modal - Adrien (1991): modal synthesis\n"
                            "Each mode k is a sine that fades: r = frequency ratio to the played note f, A = level, "
                            "tau = time to fall to 1/e. w(t) is a 1.5 ms fade-in; D scales every tau (Decay knob, x1 at its "
                            "default of 3 s). Normalised to a peak of 1 at C4, like a loaded sample.\n\n"
                            "Recipes: Soft Key (harmonic 1, 2, 3, 7) and Metal (inharmonic 1, 2.73, 5.41, 8.17), the "
                            "additive attacks of the factory presets, computed for every note. Custom: "
                            "move any slider (up to 8 modes; Level 0 leaves a mode out).\n\n"
                            "Adapted: the decays do not change with the note (a sample transposed an octave up lasted half "
                            "as long); modes above 0.45 x the sample rate are muted. Position, Bright, Damping, Pickup, "
                            "Buzz and Harmonic have no effect.";
        }
    }

    KoniSynthProcessor& koni;
    int mode = -1;
    bool loading = false;
    juce::String modelName, reference;
    juce::Label recipeLabel, status;
    juce::TextEditor text;
    juce::TextButton formulaTab, explanationTab;
    juce::ComboBox recipeBox;
    std::array<juce::Label, 4> header;
    std::array<Row, ModalRecipe::maxModes> rows;
    std::unique_ptr<juce::Drawable> formula, buzzFormula;
    juce::Rectangle<int> titleArea, cardArea, overviewArea, detailArea, modesArea;
    std::vector<float> wave;
    juce::String previewKey;
    double seconds = 0.7, gateSeconds = 0.5;
    float peak = 1.0e-9f;
};

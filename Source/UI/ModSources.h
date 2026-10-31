// Fichas de las fuentes de la matriz de modulación (Dsp/ModMatrix.h), para arrastrarlas sobre un mando: al soltar una sobre un
// mando que es destino (Knob::itemDropped) se crea su ruta en la primera ranura libre al +25 %. Dos grupos: continuas (LFO,
// envolventes, rueda, aftertouch, timbre MPE, bend: un valor que se mueve) y por nota (Velocity, Key Track, Random: fijas
// desde el note-on de cada nota; ficha con la marca cuadrada). El color es el de la fuente (MatrixUi::sourceColour).
// Se incluye desde KoniSynth.cpp (mismo TU), tras UI/Widgets.h.
#pragma once

class ModSourceChips : public juce::Component
{
public:
    ModSourceChips()
    {
        for (int src = 1; src < ModMatrix::numSources; ++src)
            if (! MatrixUi::perNote (src)) add (src);
        for (int src = 1; src < ModMatrix::numSources; ++src)
            if (MatrixUi::perNote (src)) add (src);
    }

    // Ancho que piden todas en una fila: su texto + 34 px cada una y 6 de separación
    int preferredWidth() const
    {
        int w = 0;
        for (auto& c : chips) w += c->preferredWidth() + 6;
        return w + 10;
    }

    void resized() override
    {
        // Una fila centrada en alto (hasta 28 px: el alto de la barra menos 1 px por lado); si no cabe, dos de 24
        auto r = getLocalBounds().reduced (2, 0);
        const auto rows = preferredWidth() > r.getWidth() ? 2 : 1;
        const auto h = rows == 1 ? juce::jlimit (20, 28, r.getHeight() - 2) : 24, gap = 6;
        auto y = r.getCentreY() - (rows * h + (rows - 1) * gap) / 2;
        auto x = r.getX();
        for (auto& c : chips)
        {
            const auto w = c->preferredWidth();
            if (x + w > r.getRight() && x > r.getX()) { x = r.getX(); y += h + gap; }
            c->setBounds (x, y, w, h);
            x += w + 6;
        }
    }

private:
    struct Chip : juce::Component, juce::SettableTooltipClient
    {
        explicit Chip (int s) : source (s)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTooltip (juce::String ("Drag ") + ModMatrix::sourceNames()[s] + " onto a knob: it moves that knob (a new matrix route, "
                        "+25 %). The pilot lamps under the knob show it; click them to set the amount."
                        + (MatrixUi::perNote (s) ? " Per note: each note keeps its own value from its note-on." : ""));
        }
        int preferredWidth() const
        {
            return juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (fontSize, juce::Font::bold)),
                                                                             ModMatrix::sourceNames()[source])) + 34;
        }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const auto colour = MatrixUi::sourceColour (source);
            g.setColour (Skin::white().withAlpha (isMouseOver() ? 0.12f : 0.06f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (isMouseOver() ? Skin::accent() : Skin::white().withAlpha (0.16f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            const auto mark = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ r.getX() + 14.0f, r.getCentreY() });
            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (mark.expanded (2.5f));
            g.setColour (colour);
            if (MatrixUi::perNote (source)) g.fillRoundedRectangle (mark, 2.0f); // por nota: marca cuadrada
            else g.fillEllipse (mark);
            g.setColour (Skin::white());
            g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
            g.drawText (ModMatrix::sourceNames()[source], r.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.getDistanceFromDragStart() < 4) return;
            if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this); c != nullptr && ! c->isDragAndDropActive())
                c->startDragging ("modsrc:" + juce::String (source), this);
        }
        static constexpr float fontSize = 13.5f; // el de la tira SIGNAL es 14 (regular); en negrita, 13,5 ocupa lo mismo
        const int source;
    };

    void add (int src)
    {
        auto& c = chips.emplace_back (std::make_unique<Chip> (src));
        addAndMakeVisible (*c);
    }

    std::vector<std::unique_ptr<Chip>> chips;
};

// Barra MOD bajo la tira SIGNAL: rótulo MOD, las fichas y Rnd Fade (Random Fade de la matriz) como perilla pequeña con su
// valor. Mismo fondo que SignalStrip. Con ENGINE acoplado va fija sobre el scroll; en su ventana flotante, como una fila más
// (por eso es una Section sin marco, como SignalStrip)
class ModSourceBar : public Section
{
public:
    explicit ModSourceBar (APVTS& s) : Section ({}), fadeAttachment (s, "mtxRandomFade", fade), fadeMenu (s, "mtxRandomFade", fade)
    {
        hideFrame();
        stretchToFill = true;
        addAndMakeVisible (chips);
        fade.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        fade.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        fade.setTooltip (paramTooltip (s, "mtxRandomFade"));
        fade.onValueChange = [this] { fadeValue.setText (fade.getTextFromValue (fade.getValue()), juce::dontSendNotification); };
        fade.onValueChange();
        addAndMakeVisible (fade);
        for (auto* l : { &fadeLabel, &fadeValue })
        {
            l->setFont (juce::FontOptions (13.0f));
            l->setJustificationType (juce::Justification::centredLeft);
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*l);
        }
        fadeLabel.setText ("Rnd Fade", juce::dontSendNotification);
    }

    float layoutWeight() const override { return 1.3f; }
    float naturalWidth() const override { return 300.0f; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
        g.setColour (Skin::c (0xff14171b).withAlpha (0.85f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Skin::c (0xff3a434d));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (Skin::white().withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.5f)); // como SIGNAL
        g.drawText ("MOD", r.withX (r.getX() + 10.0f).withWidth (40.0f), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 1);
        r.removeFromLeft (52); // rótulo MOD (como SIGNAL en la tira de señal)
        auto right = r.removeFromRight (150);
        fadeLabel.setBounds (right.removeFromLeft (66));
        fade.setBounds (right.removeFromLeft (28).withSizeKeepingCentre (26, 26));
        fadeValue.setBounds (right.withTrimmedLeft (4));
        chips.setBounds (r);
    }

private:
    ModSourceChips chips;
    juce::Slider fade;
    juce::Label fadeLabel, fadeValue;
    APVTS::SliderAttachment fadeAttachment; // tras fade: orden de construcción
    ParamMenu fadeMenu;
};

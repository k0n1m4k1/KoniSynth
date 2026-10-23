// Asistente Controller Setup: recorre los controles asignables de un perfil (Midi/Controllers) y pide moverlos uno a uno;
// cada movimiento se asigna con el MIDI Learn de siempre (mismo almacenamiento, mismo aviso de CC 120-127 del editor).
// Se incluye desde KoniSynth.cpp (mismo TU), tras los demás paneles.
#pragma once

class ControllerSetupPanel : public juce::Component, private juce::Timer
{
public:
    ControllerSetupPanel (KoniSynthProcessor& p, const ControllerProfile& prof) : koni (p), profile (prof)
    {
        title.setText (profile.name, juce::dontSendNotification);
        title.setFont (juce::FontOptions (20.0f));
        addAndMakeVisible (title);
        notes.setText (profile.notes, juce::dontSendNotification);
        notes.setJustificationType (juce::Justification::topLeft);
        notes.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (notes);
        for (const auto* c : profile.learnable())
        {
            auto* param = koni.apvts.getParameter (c->paramId);
            jassert (param != nullptr);
            auto& r = rows.emplace_back (std::make_unique<Row>());
            r->paramIndex = param->getParameterIndex();
            r->control.setText (c->name, juce::dontSendNotification);
            r->target.setText (param->getName (40), juce::dontSendNotification);
            for (auto* l : { &r->control, &r->target, &r->cc })
                addAndMakeVisible (*l);
        }
        start.setTooltip ("Start: move each control when it is highlighted. Its MIDI CC is learned and assigned to the parameter shown.");
        skip.setTooltip ("Skip this control (keeps its current assignment).");
        clear.setTooltip ("Remove the MIDI assignments of the parameters in this list.");
        start.onClick = [this] { step = 0; arm(); };
        skip.onClick = [this] { if (step >= 0) { ++step; arm(); } };
        clear.onClick = [this]
        {
            stopLearning();
            step = -1;
            for (auto& r : rows) koni.midiLearn.clear (r->paramIndex);
        };
        for (auto* b : { &start, &skip, &clear })
            addAndMakeVisible (*b);
        if (rows.empty()) // perfil sin controles asignables (Casio CT-S400): solo la configuración recomendada
            for (auto* b : { &start, &skip, &clear })
                b->setVisible (false);
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);
        startTimerHz (10);
        timerCallback();
    }

    ~ControllerSetupPanel() override { stopLearning(); }

    void paint (juce::Graphics& g) override
    {
        if (juce::isPositiveAndBelow (step, (int) rows.size()))
        {
            g.setColour (Skin::c (0xff42a2c8).withAlpha (0.3f));
            g.fillRoundedRectangle (rows[(size_t) step]->control.getBounds().getUnion (rows[(size_t) step]->cc.getBounds()).toFloat(), 4.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (30));
        notes.setBounds (r.removeFromTop (70));
        r.removeFromTop (8);
        auto buttons = r.removeFromBottom (32);
        for (auto* b : { &start, &skip, &clear })
            b->setBounds (buttons.removeFromLeft (110).withTrimmedRight (10));
        status.setBounds (buttons);
        r.removeFromBottom (8);
        for (auto& row : rows)
        {
            auto line = r.removeFromTop (28);
            row->control.setBounds (line.removeFromLeft (180));
            row->target.setBounds (line.removeFromLeft (260));
            row->cc.setBounds (line);
        }
    }

private:
    struct Row
    {
        int paramIndex = -1;
        juce::Label control, target, cc;
    };

    void arm()
    {
        if (juce::isPositiveAndBelow (step, (int) rows.size()))
            koni.midiLearn.learning.store (rows[(size_t) step]->paramIndex);
        else
            stopLearning();
        repaint();
    }

    void stopLearning()
    {
        // Solo cancela si el aprendizaje pendiente es de esta lista (no pisa un MIDI Learn pedido desde otro control)
        for (auto& r : rows)
        {
            auto expected = r->paramIndex;
            koni.midiLearn.learning.compare_exchange_strong (expected, -1);
        }
    }

    void timerCallback() override
    {
        // El hilo de audio pone learning a -1 al asignar: entonces pasa al siguiente control
        if (juce::isPositiveAndBelow (step, (int) rows.size()) && koni.midiLearn.learning.load() != rows[(size_t) step]->paramIndex)
        {
            ++step;
            arm();
        }
        for (auto& r : rows)
        {
            const auto cc = koni.midiLearn.ccFor (r->paramIndex);
            r->cc.setText (cc >= 0 ? "CC " + juce::String (cc) : "-", juce::dontSendNotification);
        }
        const auto text = juce::isPositiveAndBelow (step, (int) rows.size()) ? "Move " + rows[(size_t) step]->control.getText() + " on the controller..."
                        : step >= (int) rows.size() ? juce::String ("Done. Assignments are saved with the plugin state.")
                                                    : juce::String ("Press Start to learn the controls in order.");
        status.setText (text, juce::dontSendNotification);
    }

    KoniSynthProcessor& koni;
    const ControllerProfile& profile;
    juce::Label title, notes, status;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton start { "Start" }, skip { "Skip" }, clear { "Clear" };
    int step = -1; // control pedido; -1 = parado; >= rows.size() = terminado
};

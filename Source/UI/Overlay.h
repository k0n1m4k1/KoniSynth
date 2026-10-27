// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// UI: diálogo superpuesto al editor (oscurece el fondo; panel con mensaje, campo de texto opcional y 1–2 botones).
// Es un componente hijo del editor: funciona igual en standalone y en cualquier host, con la UI escalada.
// Return = aceptar, Escape = cancelar. Lo posee quien lo abre (unique_ptr): si ese componente se destruye, el diálogo
// también. Al cerrar avisa de forma asíncrona (nunca dentro de su propio onClick), y el dueño puede destruirlo entonces.
class Overlay : public juce::Component
{
public:
    using Done = std::function<void (bool accepted, const juce::String& text, int choice)>;

    // anchor: cualquier componente del editor. text: valor inicial del campo (sin valor = sin campo de texto).
    // cancelText vacío = solo botón de aceptar.
    static std::unique_ptr<Overlay> create (juce::Component& anchor, const juce::String& title, const juce::String& message,
                                            std::optional<juce::String> text, const juce::String& okText,
                                            const juce::String& cancelText, Done done)
    {
        auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (&anchor);
        if (editor == nullptr)
            editor = anchor.findParentComponentOfClass<juce::AudioProcessorEditor>();
        auto* parent = editor != nullptr ? static_cast<juce::Component*> (editor) : anchor.getTopLevelComponent();
        std::unique_ptr<Overlay> o (new Overlay (title, message, std::move (text), okText, cancelText, std::move (done)));
        parent->addAndMakeVisible (*o);
        o->setBounds (parent->getLocalBounds());
        o->focusInitial();
        return o;
    }

    // Selector bajo el campo de texto (p. ej. la categoría del preset); el índice elegido llega a Done
    void setChoices (const juce::String& label, const juce::StringArray& items, int initialIndex)
    {
        choiceLabel.setText (label, juce::dontSendNotification);
        choice.addItemList (items, 1);
        choice.setSelectedItemIndex (juce::jlimit (0, items.size() - 1, initialIndex), juce::dontSendNotification);
        addAndMakeVisible (choiceLabel);
        addAndMakeVisible (choice);
        resized();
        repaint();
    }

    // Tercer botón, entre aceptar y cancelar (p. ej. Overwrite / Skip / Cancel): cierra con accepted = false y choice = -2
    void setExtraButton (const juce::String& text)
    {
        extra.setButtonText (text);
        extra.onClick = [this] { close (false, true); };
        addAndMakeVisible (extra);
        resized();
    }

    ~Overlay() override
    {
        if (auto* parent = getParentComponent())
            parent->removeChildComponent (this);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Skin::black().withAlpha (0.55f));
        const auto p = panel().toFloat();
        g.setColour (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.15f));
        g.fillRoundedRectangle (p, 8.0f);
        g.setColour (Skin::white().withAlpha (0.25f));
        g.drawRoundedRectangle (p, 8.0f, 1.0f);
        auto r = panel().reduced (16);
        g.setColour (Skin::white());
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (title, r.removeFromTop (24), juce::Justification::left);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (message, r.removeFromTop (60), juce::Justification::topLeft, 4); // hasta 4 líneas (aviso con ruta)
    }

    void resized() override
    {
        auto r = panel().reduced (16);
        r.removeFromTop (84);
        if (field.isVisible())
        {
            field.setBounds (r.removeFromTop (28));
            r.removeFromTop (12);
        }
        if (choice.isVisible())
        {
            auto row = r.removeFromTop (28);
            choiceLabel.setBounds (row.removeFromLeft (80));
            choice.setBounds (row);
            r.removeFromTop (12);
        }
        auto buttons = r.removeFromBottom (30);
        if (cancel.isVisible())
        {
            cancel.setBounds (buttons.removeFromRight (100));
            buttons.removeFromRight (8);
        }
        if (extra.isVisible())
        {
            extra.setBounds (buttons.removeFromRight (100));
            buttons.removeFromRight (8);
        }
        ok.setBounds (buttons.removeFromRight (100));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { close (false); return true; }
        if (k == juce::KeyPress::returnKey) { close (true);  return true; }
        return true; // modal: no deja pasar teclas al teclado MIDI de pantalla
    }

    void mouseDown (const juce::MouseEvent&) override {} // bloquea clics al fondo

private:
    Overlay (const juce::String& t, const juce::String& m, std::optional<juce::String> text,
             const juce::String& okText, const juce::String& cancelText, Done d)
        : title (t), message (m), done (std::move (d))
    {
        setWantsKeyboardFocus (true);
        ok.setButtonText (okText);
        ok.onClick = [this] { close (true); };
        addAndMakeVisible (ok);
        if (cancelText.isNotEmpty())
        {
            cancel.setButtonText (cancelText);
            cancel.onClick = [this] { close (false); };
            addAndMakeVisible (cancel);
        }
        if (text.has_value())
        {
            field.setText (*text, false);
            field.onReturnKey = [this] { close (true); };
            field.onEscapeKey = [this] { close (false); };
            addAndMakeVisible (field);
        }
    }

    juce::Rectangle<int> panel() const
    {
        return getLocalBounds().withSizeKeepingCentre (480, (field.isVisible() ? 210 : 170) + (choice.isVisible() ? 40 : 0));
    }

    void focusInitial()
    {
        if (field.isVisible())
        {
            field.grabKeyboardFocus();
            field.selectAll();
        }
        else
        {
            grabKeyboardFocus();
        }
    }

    void close (bool accepted, bool extraButton = false)
    {
        if (closing)
            return;
        closing = true;
        setVisible (false);
        const auto value = field.getText().trim();
        const auto chosen = extraButton ? -2 : choice.getSelectedItemIndex();
        // Diferido: este método se llama desde el onClick de un botón propio y el dueño destruirá el diálogo
        juce::MessageManager::callAsync ([safe = SafePointer<Overlay> (this), accepted, value, chosen]
        {
            if (auto* o = safe.getComponent())
            {
                auto d = o->done; // copia: d puede destruir 'o'
                if (d)
                    d (accepted, value, chosen);
            }
        });
    }

    juce::String title, message;
    Done done;
    juce::TextEditor field;
    juce::Label choiceLabel;
    juce::ComboBox choice;
    juce::TextButton ok, cancel, extra;
    bool closing = false;
};

// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// UI: selector de presets + Save / Save As / Rename / Delete. "*" tras el nombre = modificado sin guardar;
// volver a elegir el mismo preset en la lista descarta los cambios.
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (PresetManager& pm) : presets (pm)
    {
        combo.setTextWhenNothingSelected ("(no preset)");
        combo.onChange = [this]
        {
            if (const auto id = combo.getSelectedId(); id > 0)
            {
                if (auto r = presets.load (id - 1); r.failed())
                    showError (r.getErrorMessage());
                else if (presets.lastLoadNote.isNotEmpty())
                    showError (presets.lastLoadNote); // formato posterior o ajustes desconocidos
                refreshDisplay();
            }
        };

        combo.setTooltip ("Presets: factory sounds and your own, grouped by category. A \"*\" after the name means "
                          "unsaved changes; choosing the same preset again discards them.");
        filterCombo.addItem ("All", 1);
        for (int i = 0; i < presetCategories().size(); ++i)
            filterCombo.addItem (presetCategories()[i], i + 2);
        filterCombo.setSelectedId (1, juce::dontSendNotification);
        filterCombo.setTooltip ("Show only presets of one category (Bass, Lead, Pad, Drums...).");
        filterCombo.onChange = [this] { rebuild(); };
        saveButton.setTooltip ("Save: overwrite the current user preset with the current settings.");
        saveAsButton.setTooltip ("Save As: save the current settings as a new user preset.");
        renameButton.setTooltip ("Rename the current user preset.");
        deleteButton.setTooltip ("Delete the current user preset.");
        prevButton.setTooltip ("Previous preset (in the list order, within the category filter).");
        nextButton.setTooltip ("Next preset (in the list order, within the category filter).");
        browseButton.setTooltip ("Preset browser: search, filter by category, engine or favorites, and listen with the arrow keys.");
        prevButton.onClick = [this] { step (-1); };
        nextButton.onClick = [this] { step (1); };
        browseButton.onClick = [this] { if (onBrowse) onBrowse(); };
        saveButton.onClick   = [this] { finish (presets.save()); };
        saveAsButton.onClick = [this] { saveAs(); };
        renameButton.onClick = [this] { rename(); };
        deleteButton.onClick = [this] { remove(); };

        for (auto* c : std::initializer_list<juce::Component*> { &filterCombo, &combo, &saveButton, &saveAsButton, &renameButton, &deleteButton,
                                                                 &prevButton, &nextButton, &browseButton })
            addAndMakeVisible (*c);

        rebuild();
        startTimerHz (5); // detecta cambios de potenciómetros para el "*"
    }

    void resized() override
    {
        auto r = getLocalBounds();
        for (auto* b : { &deleteButton, &renameButton, &saveAsButton, &saveButton })
        {
            b->setBounds (r.removeFromRight (34));
            r.removeFromRight (6);
        }
        browseButton.setBounds (r.removeFromLeft (30)); // lupa a la izquierda de la categoría
        r.removeFromLeft (4);
        filterCombo.setBounds (r.removeFromLeft (110));
        r.removeFromLeft (6);
        nextButton.setBounds (r.removeFromRight (26));
        prevButton.setBounds (r.removeFromRight (26));
        r.removeFromRight (4);
        combo.setBounds (r);
    }

    std::function<void()> onBrowse; // lo instala el editor: abre el navegador (UI/PresetBrowser.h)

    // Ancho que necesita: 4 botones de icono (34 + 6) + lupa y categoría (30 + 4 + 110 + 6) + anterior / siguiente (4 + 26 + 26)
    // + selector de 280 px (el nombre de fábrica más largo, 25 caracteres con " *", mide ~205 px). El resto queda libre
    static constexpr int comboWidth = 280;
    static int preferredWidth() { return 4 * 40 + 150 + 56 + comboWidth; }

private:
    void timerCallback() override { refreshDisplay(); }

    void rebuild()
    {
        presets.refresh();
        combo.clear (juce::dontSendNotification);

        // Fábrica por categoría (en el orden de presetCategories) y después usuario por categoría; el filtro deja una
        const auto filter = filterCombo.getSelectedId() <= 1 ? juce::String() : filterCombo.getText();
        const auto& list = presets.getPresets();
        for (const auto factory : { true, false })
            for (const auto& category : presetCategories())
            {
                if (filter.isNotEmpty() && category != filter)
                    continue;
                // Orden alfabético dentro de la sección (natural: "Pad 2" antes que "Pad 10"); el id sigue siendo el índice
                std::vector<size_t> section;
                for (size_t i = 0; i < list.size(); ++i)
                    if (list[i].factory == factory && list[i].category == category)
                        section.push_back (i);
                std::stable_sort (section.begin(), section.end(), [&list] (size_t a, size_t b)
                                  { return list[a].name.compareNatural (list[b].name) < 0; });
                if (! section.empty())
                    combo.addSectionHeading (factory ? category : "User - " + category);
                for (const auto i : section)
                    combo.addItem (list[i].name, (int) i + 1); // id = índice + 1 (0 = sin selección)
            }

        lastIndex = -2; // fuerza el refresco
        refreshDisplay();
    }

    void refreshDisplay()
    {
        const auto index = presets.currentIndex();
        const auto dirty = presets.isDirty();
        if (index == lastIndex && dirty == lastDirty)
            return;
        lastIndex = index;
        lastDirty = dirty;

        if (index < 0)
            combo.setSelectedId (0, juce::dontSendNotification);
        else if (combo.indexOfItemId (index + 1) < 0) // oculto por el filtro de categoría
            combo.setText (presets.getPresets()[(size_t) index].name + (dirty ? " *" : ""), juce::dontSendNotification);
        else if (dirty)
            combo.setText (presets.getPresets()[(size_t) index].name + " *", juce::dontSendNotification);
        else
            combo.setSelectedId (index + 1, juce::dontSendNotification);

        const auto user = presets.currentIsUser();
        saveButton.setEnabled (user && dirty);
        renameButton.setEnabled (user);
        deleteButton.setEnabled (user);
    }

    // Anterior / siguiente en el orden de la lista (con el filtro de categoría), con vuelta al principio
    void step (int delta)
    {
        std::vector<int> ids;
        for (int i = 0; i < combo.getNumItems(); ++i)
            if (const auto id = combo.getItemId (i); id > 0) ids.push_back (id);
        if (ids.empty()) return;
        const auto cur = presets.currentIndex() + 1;
        auto it = std::find (ids.begin(), ids.end(), cur);
        const auto pos = it == ids.end() ? (delta > 0 ? -1 : 0) : (int) (it - ids.begin());
        const auto next = ids[(size_t) ((pos + delta + (int) ids.size()) % (int) ids.size())];
        if (auto r = presets.load (next - 1); r.failed()) showError (r.getErrorMessage());
        else if (presets.lastLoadNote.isNotEmpty()) showError (presets.lastLoadNote);
        refreshDisplay();
    }

    juce::String currentName() const
    {
        const auto index = presets.currentIndex();
        return index >= 0 ? presets.getPresets()[(size_t) index].name : juce::String();
    }

    void saveAs()
    {
        // Categoría propuesta: la del preset actual (Init -> la del filtro, o Lead)
        auto categories = presetCategories();
        categories.removeString ("Init");
        const auto index = presets.currentIndex();
        auto current = index >= 0 ? presets.getPresets()[(size_t) index].category : juce::String();
        if (! categories.contains (current))
            current = categories.contains (filterCombo.getText()) ? filterCombo.getText() : juce::String ("Lead");

        open ("Save preset as", "Preset name:", presets.currentIsUser() ? currentName() : juce::String ("My Preset"), "OK", "Cancel",
              [this, categories] (const juce::String& name, int choice)
              {
                  const auto category = categories[juce::jmax (0, choice)];
                  if (auto r = presets.validateName (name); r.failed())
                      return showError (r.getErrorMessage());
                  if (! presets.userPresetExists (name))
                      return finish (presets.saveAs (name, category));
                  confirm ("Overwrite \"" + name + "\"?", "Overwrite", [this, name, category] { finish (presets.saveAs (name, category)); });
              });
        if (overlay != nullptr)
            overlay->setChoices ("Category:", categories, categories.indexOf (current));
    }

    void rename()
    {
        askName ("Rename preset", currentName(), [this] (const juce::String& name) { finish (presets.rename (name)); });
    }

    void remove()
    {
        confirm ("Delete \"" + currentName() + "\"? This cannot be undone.", "Delete", [this] { finish (presets.remove()); });
    }

    void finish (const juce::Result& r)
    {
        if (r.failed())
            showError (r.getErrorMessage());
        rebuild();
    }

    // Diálogos dentro del editor (Overlay), no ventanas modales del sistema. Antes: AlertWindow con
    // deleteWhenDismissed = false que nunca se ocultaba -> tras OK/Cancel el diálogo seguía en pantalla.
    void askName (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> onOk)
    {
        open (title, "Preset name:", initial, "OK", "Cancel", [onOk] (const juce::String& v, int) { onOk (v); });
    }

    void confirm (const juce::String& message, const juce::String& okText, std::function<void()> onOk)
    {
        open ("Preset", message, std::nullopt, okText, "Cancel", [onOk] (const juce::String&, int) { onOk(); });
    }

    void showError (const juce::String& message)
    {
        open ("Preset", message, std::nullopt, "OK", {}, nullptr);
    }

    // Un diálogo a la vez. Al cerrarse se destruye y, si se aceptó, se ejecuta onOk (que puede abrir otro diálogo)
    void open (const juce::String& title, const juce::String& message, std::optional<juce::String> text,
               const juce::String& okText, const juce::String& cancelText, std::function<void (const juce::String&, int)> onOk)
    {
        overlay = Overlay::create (*this, title, message, std::move (text), okText, cancelText,
            [this, onOk] (bool accepted, const juce::String& value, int choice)
            {
                overlay.reset();
                if (accepted && onOk)
                    onOk (value, choice);
            });
    }

    PresetManager& presets;
    juce::ComboBox filterCombo, combo;
public:
    // Botones de icono (dibujados con Path): disquete, disquete con +, lápiz, papelera; flechas curvas de deshacer y
    // rehacer (también en la barra superior del editor)
    struct IconButton : juce::Button
    {
        enum class Icon { save, saveAs, rename, remove, undo, redo, prev, next, browse, midi };
        IconButton (Icon i, const juce::String& name) : juce::Button (name), icon (i) {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Skin::c (0xff2c333b).brighter (down ? 0.2f : highlighted ? 0.1f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);
            const auto ink = Skin::white().withAlpha (isEnabled() ? 0.85f : 0.3f);
            const auto s = juce::jmin (r.getWidth(), r.getHeight()) * 0.55f;
            const auto b = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
            g.setColour (ink);
            juce::Path p;
            switch (icon)
            {
                case Icon::save:
                case Icon::saveAs:
                {
                    // Disquete: cuerpo con esquina cortada, ventana de la chapa arriba y etiqueta abajo
                    p.startNewSubPath (b.getX(), b.getY());
                    p.lineTo (b.getRight() - s * 0.2f, b.getY());
                    p.lineTo (b.getRight(), b.getY() + s * 0.2f);
                    p.lineTo (b.getRight(), b.getBottom());
                    p.lineTo (b.getX(), b.getBottom());
                    p.closeSubPath();
                    g.strokePath (p, juce::PathStrokeType (1.4f));
                    g.fillRect (juce::Rectangle<float> (b.getX() + s * 0.25f, b.getY(), s * 0.42f, s * 0.3f));
                    g.drawRect (juce::Rectangle<float> (b.getX() + s * 0.18f, b.getY() + s * 0.55f, s * 0.64f, s * 0.38f), 1.2f);
                    if (icon == Icon::saveAs) // "+" en la esquina
                    {
                        const auto c = b.getBottomRight().translated (s * 0.1f, s * 0.05f);
                        g.setColour (Skin::c (0xff42a2c8));
                        g.fillRect (juce::Rectangle<float> (s * 0.5f, s * 0.14f).withCentre (c));
                        g.fillRect (juce::Rectangle<float> (s * 0.14f, s * 0.5f).withCentre (c));
                    }
                    break;
                }
                case Icon::rename:
                {
                    // Lápiz en diagonal: cuerpo y punta
                    p.addRectangle (-s * 0.12f, -s * 0.55f, s * 0.24f, s * 0.8f);
                    p.addTriangle (-s * 0.12f, s * 0.25f, s * 0.12f, s * 0.25f, 0.0f, s * 0.48f);
                    p.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f).translated (b.getCentreX(), b.getCentreY()));
                    g.fillPath (p);
                    break;
                }
                case Icon::remove:
                {
                    // Papelera: tapa con asa, cubeta y tres ranuras
                    g.fillRect (juce::Rectangle<float> (b.getX(), b.getY() + s * 0.12f, s, s * 0.1f));
                    g.fillRect (juce::Rectangle<float> (b.getCentreX() - s * 0.15f, b.getY(), s * 0.3f, s * 0.12f));
                    p.startNewSubPath (b.getX() + s * 0.1f, b.getY() + s * 0.28f);
                    p.lineTo (b.getX() + s * 0.2f, b.getBottom());
                    p.lineTo (b.getRight() - s * 0.2f, b.getBottom());
                    p.lineTo (b.getRight() - s * 0.1f, b.getY() + s * 0.28f);
                    g.strokePath (p, juce::PathStrokeType (1.4f));
                    for (int k = -1; k <= 1; ++k)
                        g.fillRect (juce::Rectangle<float> (b.getCentreX() + k * s * 0.2f - 0.6f, b.getY() + s * 0.42f, 1.2f, s * 0.45f));
                    break;
                }
                case Icon::undo:
                case Icon::redo:
                {
                    // Flecha curva: arco de 0,55·π a 1,75·π (abierto arriba a la izquierda) y punta en su extremo, que
                    // apunta a la izquierda; rehacer = espejo horizontal
                    const auto pi = juce::MathConstants<float>::pi;
                    const auto rad = s * 0.42f;
                    const auto c = b.getCentre().translated (0.0f, s * 0.06f);
                    p.addCentredArc (c.x, c.y, rad, rad, 0.0f, -0.55f * pi, 0.75f * pi, true);
                    const auto tip = c.translated (-rad * std::sin (0.55f * pi), -rad * std::cos (0.55f * pi));
                    juce::Path head;
                    head.addTriangle (tip.x - s * 0.24f, tip.y, tip.x + s * 0.08f, tip.y - s * 0.22f, tip.x + s * 0.08f, tip.y + s * 0.22f);
                    if (icon == Icon::redo)
                    {
                        const auto mirror = juce::AffineTransform::scale (-1.0f, 1.0f, b.getCentreX(), b.getCentreY());
                        p.applyTransform (mirror);
                        head.applyTransform (mirror);
                    }
                    g.strokePath (p, juce::PathStrokeType (1.6f));
                    g.fillPath (head);
                    break;
                }
                case Icon::prev:
                case Icon::next:
                {
                    // Triángulo: anterior a la izquierda, siguiente a la derecha
                    const auto l = icon == Icon::prev;
                    p.addTriangle (l ? b.getRight() - s * 0.2f : b.getX() + s * 0.2f, b.getY() + s * 0.1f,
                                   l ? b.getRight() - s * 0.2f : b.getX() + s * 0.2f, b.getBottom() - s * 0.1f,
                                   l ? b.getX() + s * 0.15f : b.getRight() - s * 0.15f, b.getCentreY());
                    g.fillPath (p);
                    break;
                }
                case Icon::midi:
                {
                    // Conector DIN de 5 pines (MIDI): aro, cinco pines en arco de 180° y la muesca abajo
                    g.drawEllipse (b.reduced (0.5f), 1.4f);
                    const auto c = b.getCentre();
                    const auto rad = s * 0.3f, dot = juce::jmax (2.0f, s * 0.16f);
                    for (int k = 0; k < 5; ++k)
                    {
                        const auto a = juce::MathConstants<float>::pi * (1.0f + (float) k / 4.0f); // 180°..360°: arco superior
                        g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre ({ c.x + rad * std::cos (a), c.y + rad * std::sin (a) + s * 0.06f }));
                    }
                    g.fillRect (juce::Rectangle<float> (s * 0.18f, s * 0.12f).withCentre ({ c.x, b.getBottom() - s * 0.08f }));
                    break;
                }
                case Icon::browse:
                {
                    // Lupa: aro y mango
                    const auto rad = s * 0.32f;
                    const auto c = b.getCentre().translated (-s * 0.1f, -s * 0.1f);
                    g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.6f);
                    const auto d = rad * 0.7071f;
                    g.drawLine (c.x + d, c.y + d, b.getRight(), b.getBottom(), 2.2f);
                    break;
                }
            }
        }
        Icon icon;
    };

private:
    IconButton saveButton { IconButton::Icon::save, "Save" }, saveAsButton { IconButton::Icon::saveAs, "Save As" },
               renameButton { IconButton::Icon::rename, "Rename" }, deleteButton { IconButton::Icon::remove, "Delete" },
               prevButton { IconButton::Icon::prev, "Previous preset" }, nextButton { IconButton::Icon::next, "Next preset" },
               browseButton { IconButton::Icon::browse, "Browse presets" };
    std::unique_ptr<Overlay> overlay;
    int lastIndex = -2;
    bool lastDirty = false;
};

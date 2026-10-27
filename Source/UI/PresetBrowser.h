// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, KoniSynthProcessor, PresetManager, presetCategories). No es una cabecera independiente.
#pragma once

//==============================================================================
// Navegador de presets (botón de lupa de la barra de presets): búsqueda en vivo (sin mayúsculas ni acentos; todas las
// palabras deben aparecer en nombre, categoría o motor), filtros con recuento (origen / favoritos, categoría y motor:
// cada recuento cuenta lo que quedaría al elegirlo con los demás filtros), tabla ordenable y favoritos con la estrella.
// Elegir una fila (clic o flechas) carga el preset para escucharlo; doble clic o Intro lo carga y cierra.
// Favoritos con orden propio (las teclas 1-9 cargan los nueve primeros, cuyo número se ve junto a la estrella): con la
// tabla ordenada por la columna ★ se reordenan arrastrando; en cualquier orden, con el menú del clic derecho o Alt+flechas.
// Import... / Export...: presets de usuario sueltos (.xml) o en banco (.zip); con un nombre ya existente, Replace / Skip / Cancel.
class PresetBrowser : public juce::Component, public juce::DragAndDropContainer, private juce::DragAndDropTarget,
                      private juce::TableListBoxModel
{
public:
    std::function<void()> onClose;

    explicit PresetBrowser (KoniSynthProcessor& p) : processor (p), presets (p.presetManager)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("engine")))
            engineNames = choice->choices;
        presets.refresh();

        search.setTextToShowWhenEmpty ("Search: start of words in name, category or engine (e.g. bass nes)", Skin::white().withAlpha (0.4f));
        search.setFont (juce::FontOptions (16.0f));
        search.onTextChange = [this] { refilter(); };
        search.onReturnKey = [this] { if (! visible.empty()) { if (table.getSelectedRow() < 0) table.selectRow (0); close(); } };
        search.onEscapeKey = [this] { if (search.isEmpty()) close(); else search.clear(), refilter(); };
        arrows.owner = this;
        search.addKeyListener (&arrows);
        addAndMakeVisible (search);

        filters.owner = this; // antes de setModel: setModel ya pide getNumRows ()
        filterList.setModel (&filters);
        filterList.setRowHeight (22);
        filterList.setColour (juce::ListBox::backgroundColourId, Skin::c (0xff1e2329));
        addAndMakeVisible (filterList);

        auto& h = table.getHeader();
        h.addColumn (juce::String::fromUTF8 ("\xe2\x98\x85"), colFav, 46, 46, 46, juce::TableHeaderComponent::visible | juce::TableHeaderComponent::sortable);
        h.addColumn ("Name", colName, 300, 120, 600, juce::TableHeaderComponent::defaultFlags);
        h.addColumn ("Category", colCategory, 120);
        h.addColumn ("Engine", colEngine, 200);
        h.addColumn ("Source", colSource, 80);
        h.setStretchToFitActive (true);
        h.setSortColumnId (colCategory, true);
        table.setModel (this);
        table.setRowHeight (24);
        table.setColour (juce::ListBox::backgroundColourId, Skin::c (0xff1e2329));
        table.setMultipleSelectionEnabled (false);
        addAndMakeVisible (table);

        count.setJustificationType (juce::Justification::centredRight);
        count.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
        addAndMakeVisible (count);
        importButton.setTooltip ("Import user presets: a KoniSynth preset (.xml) or a bank of presets (.zip). The samples, "
                                 "impulse responses and wavetables they use are not included: copy them too, or use Locate... when loading.");
        importButton.onClick = [this] { chooseImport(); };
        addAndMakeVisible (importButton);
        exportButton.setTooltip ("Export the selected user preset (.xml) or all your user presets as a bank (.zip). The files "
                                 "they use (samples, impulse responses, wavetables) are not included.");
        exportButton.onClick = [this] { showExportMenu(); };
        addAndMakeVisible (exportButton);
        hint.setText (defaultHint(), juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.5f));
        addAndMakeVisible (hint);

        refilter();
        selectCurrent();
    }

    void visibilityChanged() override { if (isShowing()) search.grabKeyboardFocus(); }
    void parentHierarchyChanged() override { if (isShowing()) search.grabKeyboardFocus(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (30);
        count.setBounds (top.removeFromRight (140));
        exportButton.setBounds (top.removeFromRight (90));
        top.removeFromRight (6);
        importButton.setBounds (top.removeFromRight (90));
        top.removeFromRight (8);
        search.setBounds (top);
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (20);
        hint.setBounds (bottom);
        r.removeFromBottom (4);
        filterList.setBounds (r.removeFromLeft (220));
        r.removeFromLeft (8);
        table.setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Skin::c (0xff262c33)); }

    // Línea de inserción mientras se arrastra un favorito
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropY < 0) return;
        g.setColour (Skin::c (0xffffc040));
        g.fillRect (table.getX(), table.getY() + dropY - 1, table.getWidth(), 3);
    }

private:
    enum { colFav = 1, colName, colCategory, colEngine, colSource };

    static juce::String defaultHint()
    {
        return "Click or arrow keys: listen. Double-click or Enter: load and close. Star: favorite (keys 1-9 load the first nine; "
               "drag them when sorted by the star column, or right-click / Alt+arrows to reorder).";
    }

    //==========================================================================
    // Filtros: tres grupos de selección única. Fila = cabecera o elemento con su valor
    struct FilterRow { int group; juce::String value, label; bool heading; };
    enum { groupSource = 0, groupCategory, groupEngine };

    struct FilterModel : juce::ListBoxModel
    {
        PresetBrowser* owner = nullptr;
        int getNumRows() override { return (int) owner->filterRows.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
        {
            if (row < 0 || row >= getNumRows()) return;
            const auto& f = owner->filterRows[(size_t) row];
            if (f.heading)
            {
                g.setColour (Skin::c (0xff42a2c8));
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (f.label.toUpperCase(), 8, 0, w - 16, h, juce::Justification::bottomLeft);
                return;
            }
            const auto selected = owner->selection[(size_t) f.group] == f.value;
            const auto n = owner->facetCount (f.group, f.value);
            if (selected)
            {
                g.setColour (Skin::c (0xff42a2c8).withAlpha (0.35f));
                g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 1.0f, (float) w - 8.0f, (float) h - 2.0f), 4.0f);
            }
            g.setColour (Skin::white().withAlpha (n > 0 || selected ? 0.9f : 0.3f));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (f.label, 16, 0, w - 60, h, juce::Justification::centredLeft, true);
            g.setColour (Skin::white().withAlpha (0.45f));
            g.drawText (juce::String (n), w - 50, 0, 42, h, juce::Justification::centredRight);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            if (row < 0 || row >= getNumRows() || owner->filterRows[(size_t) row].heading) return;
            const auto& f = owner->filterRows[(size_t) row];
            owner->selection[(size_t) f.group] = f.value;
            if (f.value == "fav") // los favoritos se ven en su orden (el de las teclas 1-9)
                owner->table.getHeader().setSortColumnId (colFav, true); // avisa a sortOrderChanged, que vuelve a filtrar
            owner->refilter();
        }
    };

    // Flechas desde el cuadro de búsqueda: mueven la selección de la tabla (y escuchan)
    struct ArrowForwarder : juce::KeyListener
    {
        PresetBrowser* owner = nullptr;
        bool keyPressed (const juce::KeyPress& k, juce::Component*) override
        {
            const auto n = (int) owner->visible.size();
            if (n == 0) return false;
            const auto cur = owner->table.getSelectedRow();
            if (k.getModifiers().isAltDown() && (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey))
            {
                if (cur >= 0) owner->moveFavoriteBy (owner->visible[(size_t) cur], k.getKeyCode() == juce::KeyPress::upKey ? -1 : 1);
                return true;
            }
            int next = -1;
            if (k == juce::KeyPress::downKey) next = juce::jmin (n - 1, cur + 1);
            else if (k == juce::KeyPress::upKey) next = juce::jmax (0, cur - 1);
            else if (k == juce::KeyPress::pageDownKey) next = juce::jmin (n - 1, cur + 10);
            else if (k == juce::KeyPress::pageUpKey) next = juce::jmax (0, cur - 10);
            if (next < 0) return false;
            owner->table.selectRow (next);
            return true;
        }
    };

    // Texto plegado: minúsculas y sin acentos
    static juce::String fold (const juce::String& s)
    {
        auto t = s.toLowerCase();
        static const char* from[] = { "\xc3\xa1", "\xc3\xa9", "\xc3\xad", "\xc3\xb3", "\xc3\xba", "\xc3\xbc", "\xc3\xb1", "\xc3\xa0", "\xc3\xa8", "\xc3\xb6" };
        static const char* to[]   = { "a", "e", "i", "o", "u", "u", "n", "a", "e", "o" };
        for (size_t i = 0; i < std::size (from); ++i)
            t = t.replace (juce::String::fromUTF8 (from[i]), to[i]);
        return t;
    }

    juce::String engineName (int e) const { return juce::isPositiveAndBelow (e, engineNames.size()) ? engineNames[e] : juce::String ("?"); }

    // ¿Pasa el preset i los filtros? skipGroup: grupo que no se mira (para los recuentos por faceta)
    bool passes (size_t i, int skipGroup = -1) const
    {
        const auto& p = presets.getPresets()[i];
        const auto& src = selection[groupSource];
        if (skipGroup != groupSource && src.isNotEmpty())
        {
            if (src == "fav" && ! presets.isFavorite (i)) return false;
            if (src == "factory" && ! p.factory) return false;
            if (src == "user" && p.factory) return false;
        }
        if (skipGroup != groupCategory && selection[groupCategory].isNotEmpty() && p.category != selection[groupCategory]) return false;
        if (skipGroup != groupEngine && selection[groupEngine].isNotEmpty() && juce::String (p.engine) != selection[groupEngine]) return false;
        if (! tokens.isEmpty())
        {
            // Cada palabra buscada debe ser el principio de alguna palabra ("nes" encuentra NES, no SNES ni Machines)
            juce::StringArray words;
            words.addTokens (fold (p.name + " " + p.category + " " + engineName (p.engine) + (p.factory ? " factory" : " user")), " ()/-_.,", "");
            for (const auto& t : tokens)
            {
                bool found = false;
                for (const auto& w : words)
                    if (w.startsWith (t)) { found = true; break; }
                if (! found) return false;
            }
        }
        return true;
    }

    int facetCount (int group, const juce::String& value) const
    {
        int n = 0;
        const auto& list = presets.getPresets();
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (! passes (i, group)) continue;
            const auto& p = list[i];
            switch (group)
            {
                case groupSource:
                    n += value.isEmpty() || (value == "fav" && presets.isFavorite (i)) || (value == "factory" && p.factory) || (value == "user" && ! p.factory);
                    break;
                case groupCategory: n += value.isEmpty() || p.category == value; break;
                default:            n += value.isEmpty() || juce::String (p.engine) == value; break;
            }
        }
        return n;
    }

    void buildFilterRows()
    {
        filterRows.clear();
        filterRows.push_back ({ groupSource, {}, "Source", true });
        filterRows.push_back ({ groupSource, {}, "All", false });
        filterRows.push_back ({ groupSource, "fav", juce::String::fromUTF8 ("\xe2\x98\x85 Favorites"), false });
        filterRows.push_back ({ groupSource, "factory", "Factory", false });
        filterRows.push_back ({ groupSource, "user", "User", false });
        filterRows.push_back ({ groupCategory, {}, "Category", true });
        filterRows.push_back ({ groupCategory, {}, "Any", false });
        for (const auto& c : presetCategories())
            filterRows.push_back ({ groupCategory, c, c, false });
        filterRows.push_back ({ groupEngine, {}, "Engine", true });
        filterRows.push_back ({ groupEngine, {}, "Any", false });
        for (int e = 0; e < engineNames.size(); ++e)
            filterRows.push_back ({ groupEngine, juce::String (e), engineNames[e], false });
    }

    void refilter()
    {
        if (filterRows.empty()) buildFilterRows();
        tokens.clear();
        tokens.addTokens (fold (search.getText()), " ", "");
        tokens.removeEmptyStrings();
        favPos.assign (presets.getPresets().size(), -1);
        const auto favs = presets.favoriteIndices();
        for (size_t p = 0; p < favs.size(); ++p)
            favPos[favs[p]] = (int) p;
        const auto keep = table.getSelectedRow() >= 0 && table.getSelectedRow() < (int) visible.size() ? visible[(size_t) table.getSelectedRow()] : (size_t) -1;
        visible.clear();
        for (size_t i = 0; i < presets.getPresets().size(); ++i)
            if (passes (i)) visible.push_back (i);
        sortVisible();
        count.setText (juce::String ((int) visible.size()) + " of " + juce::String ((int) presets.getPresets().size()), juce::dontSendNotification);
        quiet = true;
        table.updateContent();
        table.deselectAllRows();
        for (size_t r = 0; r < visible.size(); ++r)
            if (visible[r] == keep) { table.selectRow ((int) r); break; }
        quiet = false;
        table.repaint();
        filterList.updateContent();
        filterList.repaint();
    }

    void sortVisible()
    {
        const auto col = table.getHeader().getSortColumnId();
        const auto forward = table.getHeader().isSortedForwards();
        const auto& list = presets.getPresets();
        const auto catIndex = [] (const juce::String& c) { return presetCategories().indexOf (c); };
        std::stable_sort (visible.begin(), visible.end(), [&] (size_t a, size_t b)
        {
            const auto& pa = list[a]; const auto& pb = list[b];
            int c = 0;
            switch (col)
            {
                case colName:   c = pa.name.compareNatural (pb.name); break;
                case colEngine: c = pa.engine - pb.engine; break;
                case colSource: c = (int) pb.factory - (int) pa.factory; break;
                case colFav: // por posición; los que no son favoritos, detrás (en ambos sentidos, por categoría)
                {
                    const auto fa = favPos[a], fb = favPos[b];
                    if (fa < 0 && fb < 0) { c = catIndex (pa.category) - catIndex (pb.category); break; }
                    if (fa < 0 || fb < 0) return fb < 0;
                    c = fa - fb;
                    break;
                }
                default:        c = catIndex (pa.category) - catIndex (pb.category); break;
            }
            if (c == 0) c = pa.name.compareNatural (pb.name);
            return forward ? c < 0 : c > 0;
        });
    }

    void selectCurrent()
    {
        const auto cur = presets.currentIndex();
        quiet = true;
        for (size_t r = 0; r < visible.size(); ++r)
            if ((int) visible[r] == cur) { table.selectRow ((int) r); table.scrollToEnsureRowIsOnscreen ((int) r); break; }
        quiet = false;
    }

    void close()
    {
        if (onClose) juce::MessageManager::callAsync ([cb = onClose] { cb(); }); // no destruirse dentro de su propio evento
    }

    //==========================================================================
    // TableListBoxModel
    int getNumRows() override { return (int) visible.size(); }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        g.fillAll (selected ? Skin::c (0xff42a2c8).withAlpha (0.45f) : (row % 2 ? Skin::c (0xff222830) : Skin::c (0xff1e2329)));
    }

    void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) visible.size()) return;
        const auto i = visible[(size_t) row];
        const auto& p = presets.getPresets()[i];
        g.setFont (juce::FontOptions (14.0f));
        g.setColour (Skin::white().withAlpha (0.9f));
        switch (col)
        {
            case colFav:
            {
                const auto pos = i < favPos.size() ? favPos[i] : -1;
                g.setColour (pos >= 0 ? Skin::c (0xffffc040) : Skin::white().withAlpha (0.2f));
                g.setFont (juce::FontOptions (16.0f));
                g.drawText (juce::String::fromUTF8 (pos >= 0 ? "\xe2\x98\x85" : "\xe2\x98\x86"), 0, 0, 26, h, juce::Justification::centred);
                if (pos >= 0 && pos < 9) // número de la tecla 1-9 que lo carga
                {
                    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                    g.drawText (juce::String (pos + 1), 24, 0, w - 26, h, juce::Justification::centredLeft);
                }
                break;
            }
            case colName:     g.drawText (p.name, 4, 0, w - 8, h, juce::Justification::centredLeft, true); break;
            case colCategory: g.drawText (p.category, 4, 0, w - 8, h, juce::Justification::centredLeft, true); break;
            case colEngine:   g.drawText (engineName (p.engine), 4, 0, w - 8, h, juce::Justification::centredLeft, true); break;
            default:
                g.setColour (Skin::white().withAlpha (0.5f));
                g.drawText (p.factory ? "Factory" : "User", 4, 0, w - 8, h, juce::Justification::centredLeft, true);
                break;
        }
    }

    // Cómo tocar el preset (si lo indica), al pasar sobre su fila
    juce::String getCellTooltip (int row, int) override
    {
        if (row < 0 || row >= (int) visible.size()) return {};
        const auto& play = presets.getPresets()[visible[(size_t) row]].playHint;
        return play.isNotEmpty() ? "Play: " + play : juce::String();
    }

    void cellClicked (int row, int col, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) visible.size()) return;
        const auto i = visible[(size_t) row];
        if (e.mods.isPopupMenu())
        {
            const auto user = ! presets.getPresets()[i].factory;
            if (favPos[i] < 0 && ! user) return;
            juce::PopupMenu m;
            if (favPos[i] >= 0)
            {
                const auto last = (int) presets.favoriteIndices().size() - 1;
                m.addItem (1, "Move to top", favPos[i] > 0);
                m.addItem (2, "Move up", favPos[i] > 0);
                m.addItem (3, "Move down", favPos[i] < last);
                m.addItem (4, "Move to end", favPos[i] < last);
            }
            if (user)
            {
                m.addSeparator();
                m.addItem (5, "Export...");
            }
            m.showMenuAsync ({}, [this, i] (int r)
            {
                if (r == 1) moveFavoriteBy (i, -1000000);
                else if (r == 2) moveFavoriteBy (i, -1);
                else if (r == 3) moveFavoriteBy (i, 1);
                else if (r == 4) moveFavoriteBy (i, 1000000);
                else if (r == 5) exportTo ((int) i);
            });
            return;
        }
        if (col != colFav) return;
        presets.toggleFavorite (i);
        refilter(); // posiciones (números 1-9) y, con el filtro de favoritos, la lista
    }

    //==========================================================================
    // Exportar / importar presets de usuario (PresetManager::exportPreset, exportAll, readImport, writeImported)
    int selectedUserPreset() const
    {
        const auto row = table.getSelectedRow();
        if (row < 0 || row >= (int) visible.size()) return -1;
        const auto i = visible[(size_t) row];
        return presets.getPresets()[i].factory ? -1 : (int) i;
    }

    void showExportMenu()
    {
        const auto sel = selectedUserPreset();
        juce::PopupMenu m;
        m.addItem (1, sel >= 0 ? "Export \"" + presets.getPresets()[(size_t) sel].name + "\"..." : juce::String ("Export selected preset (select a user preset)"), sel >= 0);
        m.addItem (2, "Export all user presets (.zip)...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportButton), [this, sel] (int r)
        {
            if (r == 1 && sel >= 0) exportTo (sel);
            else if (r == 2) exportTo (-1);
        });
    }

    // index >= 0: ese preset como .xml; -1: todos en un .zip
    void exportTo (int index)
    {
        const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        const auto name = index >= 0 ? presets.getPresets()[(size_t) index].name + ".xml" : juce::String ("KoniSynth User Presets.zip");
        chooser = std::make_unique<juce::FileChooser> ("Export", docs.getChildFile (name), index >= 0 ? "*.xml" : "*.zip");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, index] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                f = f.withFileExtension (index >= 0 ? "xml" : "zip");
                const auto r = index >= 0 ? presets.exportPreset ((size_t) index, f) : presets.exportAll (f);
                report (r.wasOk() ? "Exported to " + f.getFullPathName() + "." : r.getErrorMessage());
            });
    }

    struct ImportJob
    {
        std::vector<PresetManager::Incoming> items;
        size_t next = 0;
        int imported = 0, skipped = 0;
        juce::StringArray rejected;
    };

    void chooseImport()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import presets", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.xml;*.zip");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            auto job = std::make_shared<ImportJob>();
            job->items = presets.readImport (f, job->rejected);
            importNext (job);
        });
    }

    // De uno en uno; con un nombre ya existente pregunta: Replace / Skip / Cancel (Cancel deja sin importar los que faltan)
    void importNext (std::shared_ptr<ImportJob> job)
    {
        for (; job->next < job->items.size(); ++job->next)
        {
            const auto& p = job->items[job->next];
            if (presets.userPresetExists (p.name))
            {
                overlay = Overlay::create (*this, "Import", "A user preset named \"" + p.name + "\" already exists.", std::nullopt,
                                           "Replace", "Cancel", [this, job] (bool accepted, const juce::String&, int choice)
                {
                    overlay.reset();
                    if (accepted) store (*job);
                    else if (choice == -2) ++job->skipped;
                    else { finishImport (*job, true); return; }
                    ++job->next;
                    importNext (job);
                });
                overlay->setExtraButton ("Skip");
                return;
            }
            store (*job);
        }
        finishImport (*job, false);
    }

    void store (ImportJob& job)
    {
        const auto& p = job.items[job.next];
        if (auto r = presets.writeImported (p); r.wasOk()) ++job.imported;
        else job.rejected.add (p.name + ": " + r.getErrorMessage());
    }

    void finishImport (const ImportJob& job, bool cancelled)
    {
        presets.refresh();
        refilter();
        juce::String msg;
        msg << "Imported " << job.imported << (job.imported == 1 ? " preset" : " presets");
        if (job.skipped > 0) msg << ", skipped " << job.skipped;
        if (cancelled) msg << ", cancelled (" << (int) (job.items.size() - job.next) << " not imported)";
        msg << ".";
        if (! job.rejected.isEmpty())
            msg << " Not imported: " << job.rejected.joinIntoString ("; ", 0, 3) << (job.rejected.size() > 3 ? "..." : ".");
        report (msg);
    }

    void report (const juce::String& message)
    {
        overlay = Overlay::create (*this, "Presets", message, std::nullopt, "OK", {}, [this] (bool, const juce::String&, int) { overlay.reset(); });
    }

    // Mueve el favorito del preset i delta posiciones en el orden global (acotado a los extremos)
    void moveFavoriteBy (size_t i, int delta)
    {
        const auto favs = presets.favoriteIndices();
        const auto from = favPos[i];
        if (from < 0) return;
        const auto to = juce::jlimit (0, (int) favs.size() - 1, from + delta);
        if (to == from) return;
        // Delante del que ocupa la posición destino al subir; al bajar, delante del siguiente a ella (o al final)
        const auto before = to < from ? favs[(size_t) to] : to + 1 < (int) favs.size() ? favs[(size_t) to + 1] : std::numeric_limits<size_t>::max();
        presets.moveFavorite (i, before);
        refilter();
        table.scrollToEnsureRowIsOnscreen (table.getSelectedRow());
    }

    //==========================================================================
    // Arrastrar para reordenar: solo favoritos y con la tabla ordenada por ★ hacia delante (lo visto es el orden real)
    bool favoriteOrderShown() { return table.getHeader().getSortColumnId() == colFav && table.getHeader().isSortedForwards(); }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.size() != 1 || ! favoriteOrderShown()) return {};
        const auto row = rows[0];
        if (row < 0 || row >= (int) visible.size() || favPos[visible[(size_t) row]] < 0) return {};
        return "fav:" + juce::String ((juce::int64) visible[(size_t) row]);
    }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("fav:"); }

    // Fila delante de la que cae (visible.size () = al final de lo visible) según la mitad de la fila bajo el puntero
    int dropRow (juce::Point<int> p) const
    {
        const auto local = table.getLocalPoint (this, p);
        auto row = table.getRowContainingPosition (local.x, local.y);
        if (row < 0) return local.y < table.getHeaderHeight() ? 0 : (int) visible.size();
        const auto r = table.getRowPosition (row, true);
        return local.y > r.getCentreY() ? row + 1 : row;
    }

    void itemDragMove (const SourceDetails& d) override
    {
        const auto row = dropRow (d.localPosition);
        const auto y = row < (int) visible.size() ? table.getRowPosition (row, true).getY()
                                                  : table.getRowPosition ((int) visible.size() - 1, true).getBottom();
        dropY = juce::jlimit (table.getHeaderHeight(), table.getHeight(), y);
        repaint();
    }
    void itemDragExit (const SourceDetails&) override { dropY = -1; repaint(); }

    void itemDropped (const SourceDetails& d) override
    {
        dropY = -1;
        const auto i = (size_t) d.description.toString().fromFirstOccurrenceOf ("fav:", false, false).getLargeIntValue();
        const auto row = dropRow (d.localPosition);
        // Delante del favorito de esa fila; si no lo es (o es el final de lo visible), al final
        const auto before = row < (int) visible.size() ? visible[(size_t) row] : std::numeric_limits<size_t>::max();
        presets.moveFavorite (i, before);
        refilter();
    }

    void cellDoubleClicked (int, int col, const juce::MouseEvent&) override { if (col != colFav) close(); }
    void returnKeyPressed (int) override { close(); }

    void selectedRowsChanged (int row) override
    {
        if (quiet || row < 0 || row >= (int) visible.size()) return;
        if (auto r = presets.load ((int) visible[(size_t) row]); r.failed())
            hint.setText (r.getErrorMessage(), juce::dontSendNotification);
        else if (presets.lastLoadNote.isNotEmpty())
            hint.setText (presets.lastLoadNote, juce::dontSendNotification);
        else if (const auto& play = presets.getPresets()[visible[(size_t) row]].playHint; play.isNotEmpty())
            hint.setText (juce::String::fromUTF8 ("\xe2\x96\xb6 Play: ") + play, juce::dontSendNotification); // ▶
        else
            hint.setText (defaultHint(), juce::dontSendNotification);
    }

    void sortOrderChanged (int, bool) override { refilter(); }

    KoniSynthProcessor& processor;
    PresetManager& presets;
    juce::StringArray engineNames, tokens;
    juce::TextEditor search;
    juce::ListBox filterList;
    FilterModel filters;
    ArrowForwarder arrows;
    juce::TableListBox table;
    juce::Label count, hint;
    juce::TextButton importButton { "Import..." }, exportButton { "Export..." };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Overlay> overlay; // Replace / Skip / Cancel y el resumen de la importación o exportación
    std::vector<FilterRow> filterRows;
    std::array<juce::String, 3> selection {};
    std::vector<size_t> visible;
    std::vector<int> favPos; // posición de favorito por índice de preset (-1 = no lo es); se rehace en refilter
    int dropY = -1;          // línea de inserción (coordenadas de la tabla) mientras se arrastra
    bool quiet = false;
};

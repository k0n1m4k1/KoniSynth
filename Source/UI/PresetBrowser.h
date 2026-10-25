// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, KoniSynthProcessor, PresetManager, presetCategories). No es una cabecera independiente.
#pragma once

//==============================================================================
// Navegador de presets (botón de lupa de la barra de presets): búsqueda en vivo (sin mayúsculas ni acentos; todas las
// palabras deben aparecer en nombre, categoría o motor), filtros con recuento (origen / favoritos, categoría y motor:
// cada recuento cuenta lo que quedaría al elegirlo con los demás filtros), tabla ordenable y favoritos con la estrella.
// Elegir una fila (clic o flechas) carga el preset para escucharlo; doble clic o Intro lo carga y cierra.
class PresetBrowser : public juce::Component, private juce::TableListBoxModel
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
        h.addColumn (juce::String::fromUTF8 ("\xe2\x98\x85"), colFav, 30, 30, 30, juce::TableHeaderComponent::notResizableOrSortable);
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
        hint.setText ("Click or arrow keys: listen. Double-click or Enter: load and close. Star: favorite.", juce::dontSendNotification);
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

private:
    enum { colFav = 1, colName, colCategory, colEngine, colSource };

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
                g.setColour (presets.isFavorite (i) ? Skin::c (0xffffc040) : Skin::white().withAlpha (0.2f));
                g.setFont (juce::FontOptions (16.0f));
                g.drawText (juce::String::fromUTF8 (presets.isFavorite (i) ? "\xe2\x98\x85" : "\xe2\x98\x86"), 0, 0, w, h, juce::Justification::centred);
                break;
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

    void cellClicked (int row, int col, const juce::MouseEvent&) override
    {
        if (col != colFav || row < 0 || row >= (int) visible.size()) return;
        presets.toggleFavorite (visible[(size_t) row]);
        if (selection[groupSource] == "fav") refilter(); else { table.repaintRow (row); filterList.repaint(); }
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
            hint.setText ("Click or arrow keys: listen. Double-click or Enter: load and close. Star: favorite.", juce::dontSendNotification);
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
    std::vector<FilterRow> filterRows;
    std::array<juce::String, 3> selection {};
    std::vector<size_t> visible;
    bool quiet = false;
};

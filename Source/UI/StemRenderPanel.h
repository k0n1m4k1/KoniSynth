// Se incluye desde KoniSynth.cpp (mismo TU), tras Widgets.h; usa KoniSynthProcessor (renderStems / renderStatus), PlayAlongSong
// y Settings/Config.h. No es una cabecera independiente.
#pragma once

//==============================================================================
// Render Stems: una fila por pista lógica del .mid cargado en el reproductor (casilla, nombre, canal, número de notas y el
// preset con que suena, con su motor), frecuencia de muestreo, carpeta y la opción de una sola instancia multitímbrica
// (todo el .mid con el sonido actual: General MIDI en MT-32, Gravis u OPL3 con Multitimbral). Render llama a la misma
// KoniSynthProcessor::renderStems que la orden render_stems del agente; el progreso se lee 10 veces por segundo. La carpeta
// se guarda en config.settings (clave stemFolder); por defecto Documentos/KoniSynth/Stems.
class StemRenderPanel : public juce::Component, private juce::Timer
{
public:
    StemRenderPanel (KoniSynthProcessor& p, const juce::File& midiFile) : koni (p), mid (midiFile)
    {
        juce::FileInputStream in (mid);
        juce::MidiFile mf;
        if (in.openedOk() && mf.readFrom (in, true))
            song = PlayAlongSong::analyse (mf);
        title.setText ("Render each part of " + mid.getFileName() + " to its own stereo WAV file, faster than real time.", juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f));
        addAndMakeVisible (title);
        for (const auto* h : { "Part", "Channel - instrument", "Notes", "Preset (engine)" })
        {
            auto& l = headers.emplace_back (std::make_unique<juce::Label> (juce::String(), h));
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, Skin::white().withAlpha (0.7f));
            addAndMakeVisible (*l);
        }
        const auto& presets = koni.presetManager.getPresets();
        juce::StringArray engines;
        if (auto* e = dynamic_cast<juce::AudioParameterChoice*> (koni.apvts.getParameter ("engine")))
            engines = e->choices;
        if (song != nullptr)
            for (size_t k = 0; k < song->tracks.size(); ++k)
            {
                auto row = std::make_unique<Row>();
                const auto& t = song->tracks[k];
                row->name.setText (juce::String (k + 1) + ". " + (t.name.isNotEmpty() ? t.name : "Part " + juce::String (k + 1)), juce::dontSendNotification);
                // Como la lista del reproductor: canal y programa General MIDI del fichero (o drums / no program)
                row->channel.setText ("ch " + juce::String (t.channel) + " - " + (t.drums ? juce::String ("drums") : t.program >= 0 ? juce::String (PlayAlongSong::gmProgramName (t.program)) : juce::String ("no program")), juce::dontSendNotification);
                row->channel.setTooltip ("MIDI channel and the instrument the file asks for (General MIDI program).");
                auto notes = juce::String (song->numNotes ((int) k));
                if (! t.drums && song->numNotes ((int) k) > 0)
                    notes << " - " << juce::MidiMessage::getMidiNoteName (t.low, true, true, 4) << "-" << juce::MidiMessage::getMidiNoteName (t.high, true, true, 4);
                row->notes.setText (notes, juce::dontSendNotification);
                fillPresets (row->preset, presets, engines);
                row->preset.setTooltip ("The sound of this part: Skip (not rendered), Current sound (the one playing now) or any "
                                        "preset; each preset brings its engine.");
                for (auto* c : std::initializer_list<juce::Component*> { &row->name, &row->channel, &row->notes, &row->preset })
                    rows.addAndMakeVisible (*c);
                rowList.push_back (std::move (row));
            }
        viewport.setViewedComponent (&rows, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        rate.addItemList ({ "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz" }, 1);
        const auto device = koni.getSampleRate();
        rate.setSelectedId (device == 44100.0 ? 1 : device == 88200.0 ? 3 : device == 96000.0 ? 4 : 2, juce::dontSendNotification);
        rate.setTooltip ("Sample rate of the WAV files.");
        format.setText ("Stereo, 32-bit float", juce::dontSendNotification);
        format.setTooltip ("Every WAV file is stereo, 32-bit floating point (no clipping above 0 dBFS); all start at 0 and have the same length, so they line up in a DAW.");
        format.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.7f));
        rateLabel.setText ("Rate", juce::dontSendNotification);
        folderLabel.setText ("Folder", juce::dontSendNotification);
        const auto config = openConfig();
        const auto saved = config != nullptr ? config->getValue ("stemFolder").trim() : juce::String();
        folder = saved.isNotEmpty() && juce::File::isAbsolutePath (saved) ? juce::File (saved) : SessionTransport::recordingsFolder().getSiblingFile ("Stems");
        folderPath.setText (folder.getFullPathName(), juce::dontSendNotification);
        folderPath.setMinimumHorizontalScale (0.6f);
        browse.setTooltip ("Choose the folder for the WAV files.");
        browse.onClick = [this] { chooseFolder(); };
        multi.setTooltip ("Render the whole file through one instance with the current sound, as General MIDI on an engine "
                          "with Multitimbral on (MT-32, Gravis UltraSound, OPL3): one WAV with every part.");
        multi.onClick = [this] { updateEnabled(); };
        mixAll.setTooltip ("When the parts are done, also add them together into one more WAV file (<file> - Mix.wav), at the same "
                           "level as the parts.");
        renderButton.onClick = [this] { startRender(); };
        cancelButton.onClick = [this] { koni.cancelStems(); };
        openButton.setTooltip ("Show the WAV files in the file explorer.");
        openButton.onClick = [this] { if (! lastFiles.isEmpty()) juce::File (lastFiles[0]).revealToUser(); else folder.revealToUser(); };
        status.setMinimumHorizontalScale (0.7f);
        for (auto* c : std::initializer_list<juce::Component*> { &rate, &format, &rateLabel, &folderLabel, &folderPath, &browse, &multi, &mixAll, &renderButton,
                                                                  &cancelButton, &openButton, &progressBar, &status })
            addAndMakeVisible (*c);
        if (song == nullptr)
            status.setText ("Cannot read " + mid.getFileName() + ".", juce::dontSendNotification);
        updateEnabled();
        startTimerHz (10);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        title.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (112);
        auto head = r.removeFromTop (20);
        const auto cols = columns (head.getWidth());
        for (size_t i = 0; i < headers.size(); ++i)
            headers[i]->setBounds (head.getX() + cols[i], head.getY(), cols[i + 1] - cols[i], head.getHeight());
        viewport.setBounds (r);
        const auto rowW = r.getWidth() - viewport.getScrollBarThickness();
        rows.setSize (rowW, juce::jmax (r.getHeight(), (int) rowList.size() * rowH));
        const auto rc = columns (rowW);
        for (size_t k = 0; k < rowList.size(); ++k)
        {
            auto& row = *rowList[k];
            const auto y = (int) k * rowH;
            row.name.setBounds (rc[0], y, rc[1] - rc[0], rowH);
            row.channel.setBounds (rc[1], y, rc[2] - rc[1], rowH);
            row.notes.setBounds (rc[2], y, rc[3] - rc[2], rowH);
            row.preset.setBounds (rc[3], y + 3, rc[4] - rc[3] - 4, rowH - 6);
        }
        bottom.removeFromTop (8);
        auto line = bottom.removeFromTop (28);
        rateLabel.setBounds (line.removeFromLeft (40));
        rate.setBounds (line.removeFromLeft (110).reduced (0, 2));
        line.removeFromLeft (6);
        format.setBounds (line.removeFromLeft (130));
        line.removeFromLeft (10);
        folderLabel.setBounds (line.removeFromLeft (50));
        browse.setBounds (line.removeFromRight (36).reduced (0, 2));
        folderPath.setBounds (line.reduced (0, 2));
        bottom.removeFromTop (4);
        line = bottom.removeFromTop (24);
        multi.setBounds (line.removeFromLeft (300));
        line.removeFromLeft (12);
        mixAll.setBounds (line.removeFromLeft (300));
        bottom.removeFromTop (6);
        line = bottom.removeFromTop (28);
        renderButton.setBounds (line.removeFromLeft (90));
        line.removeFromLeft (6);
        cancelButton.setBounds (line.removeFromLeft (80));
        line.removeFromLeft (6);
        openButton.setBounds (line.removeFromRight (110));
        line.removeFromRight (8);
        progressBar.setBounds (line.reduced (0, 3));
        status.setBounds (bottom.removeFromTop (22));
    }

private:
    struct Row
    {
        juce::Label name, channel, notes;
        juce::ComboBox preset;
    };
    static constexpr int rowH = 30;

    // Bordes de columna: parte, canal, notas, preset
    static std::array<int, 5> columns (int w)
    {
        const auto part = juce::jmax (130, w * 24 / 100);
        return { 0, part, part + 190, part + 280, w };
    }

    // "Skip" (id 1, por defecto: la pista no se renderiza), "Current sound" (id 2) y un submenú por categoría, en el orden del navegador (presetCategories; al final, las que no
    // están en esa lista), cada una una sola vez; dentro, "Nombre (Motor)" en el orden de la lista de presets (id = índice + 3)
    static void fillPresets (juce::ComboBox& box, const std::vector<PresetManager::Preset>& presets, const juce::StringArray& engines)
    {
        box.addItem ("Skip", 1);
        box.addItem ("Current sound", 2);
        auto categories = presetCategories();
        for (const auto& p : presets)
            categories.addIfNotAlreadyThere (p.category);
        auto* root = box.getRootMenu();
        for (const auto& category : categories)
        {
            juce::PopupMenu sub;
            for (size_t i = 0; i < presets.size(); ++i)
                if (const auto& p = presets[i]; p.category == category)
                    sub.addItem ((int) i + 3, p.name + (juce::isPositiveAndBelow (p.engine, engines.size()) ? " (" + engines[p.engine] + ")" : juce::String()));
            if (sub.getNumItems() > 0)
                root->addSubMenu (category, sub);
        }
        box.setSelectedId (1, juce::dontSendNotification);
    }
    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Folder for the stems", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = SafePointer<StemRenderPanel> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                safe->folder = fc.getResult();
                safe->folderPath.setText (safe->folder.getFullPathName(), juce::dontSendNotification);
                setConfigText ("stemFolder", safe->folder.getFullPathName());
            });
    }

    void startRender()
    {
        static const double rates[4] = { 44100.0, 48000.0, 88200.0, 96000.0 };
        auto req = std::make_unique<juce::DynamicObject>();
        req->setProperty ("mid", mid.getFullPathName());
        req->setProperty ("outDir", folder.getFullPathName());
        req->setProperty ("sampleRate", rates[juce::jlimit (1, 4, rate.getSelectedId()) - 1]);
        if (multi.getToggleState())
            req->setProperty ("multitimbral", true);
        else
        {
            juce::Array<juce::var> tracks;
            for (size_t k = 0; k < rowList.size(); ++k)
                if (const auto id = rowList[k]->preset.getSelectedId(); id >= 2) // 1 = Skip
                {
                    auto t = std::make_unique<juce::DynamicObject>();
                    t->setProperty ("track", (int) k);
                    if (id >= 3)
                        t->setProperty ("preset", id - 3);
                    tracks.add (juce::var (t.release()));
                }
            if (tracks.isEmpty())
            {
                status.setText ("Choose a sound for at least one part (all are on Skip).", juce::dontSendNotification);
                return;
            }
            req->setProperty ("tracks", tracks);
            req->setProperty ("mix", mixAll.getToggleState());
        }
        juce::String error;
        koni.renderStems (juce::var (req.release()), error);
        status.setText (error.isNotEmpty() ? error : juce::String ("Rendering..."), juce::dontSendNotification);
        lastFiles.clear();
        updateEnabled();
    }

    void timerCallback() override
    {
        const auto st = koni.renderStatus();
        const auto running = (bool) st.getProperty ("running", false);
        progress = running ? (double) st.getProperty ("progress", 0.0) : (wasRunning ? 1.0 : progress);
        if (wasRunning && ! running)
        {
            const auto result = st.getProperty ("result", juce::var());
            status.setText (result.getProperty ("message", "").toString(), juce::dontSendNotification);
            if (const auto* files = result.getProperty ("files", juce::var()).getArray())
                for (const auto& f : *files) lastFiles.add (f.toString());
            if (! (bool) result.getProperty ("ok", false)) progress = 0.0;
        }
        if (running != wasRunning)
        {
            wasRunning = running;
            updateEnabled();
        }
    }

    void updateEnabled()
    {
        const auto busy = koni.renderStatus().getProperty ("running", false);
        renderButton.setEnabled (! (bool) busy && song != nullptr);
        cancelButton.setEnabled ((bool) busy);
        const auto perPart = ! multi.getToggleState();
        for (auto& r : rowList)
            r->preset.setEnabled (perPart);
        mixAll.setEnabled (perPart); // multitímbrica: ya es un solo WAV
    }

    KoniSynthProcessor& koni;
    juce::File mid, folder;
    std::shared_ptr<PlayAlongSong> song;
    juce::Label title, format, rateLabel, folderLabel, folderPath, status;
    std::vector<std::unique_ptr<juce::Label>> headers;
    juce::Component rows;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<Row>> rowList;
    juce::ComboBox rate;
    juce::TextButton browse { "..." }, renderButton { "Render" }, cancelButton { "Cancel" }, openButton { "Show files" };
    juce::ToggleButton multi { "One multitimbral instance (GM)" }, mixAll { "Also mix all parts into one WAV" };
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    bool wasRunning = false;
    juce::StringArray lastFiles;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Editor escalable: todo se maqueta en 'content' a tamaño lógico fijo (kWidth × kHeight) y se escala con una
// transformación al tamaño de la ventana. Proporción fija; 50–125 %; arranca al 75 % (1152 × 735: en una pantalla
// de 1920 × 1200 con Windows al 125 % ocupa 1440 × 919 px).
// Orden de las filas = orden de la señal: generación -> forma (filtro y envolventes) -> entrada/vocoder y modulación ->
// efectos en el orden en que se aplican -> master. Flechas entre las secciones que forman cadena.
// Tooltips a los 2 s (regla del proyecto: todo control lleva ayuda y menú con "Reset to default").
#if JUCE_WINDOWS
// user32 (ya enlazada por JUCE), declarada aquí para no incluir <windows.h> en la unidad de compilación: traduce un código de
// escaneo (posición física de la tecla) a la tecla virtual de la distribución activa (NoteKeyboard::physicalKey)
extern "C" __declspec (dllimport) unsigned int __stdcall MapVirtualKeyW (unsigned int code, unsigned int mapType);
#endif
// Ventana del standalone maximizada con la barra de tareas a la izquierda o arriba (Source/WinMaximiseFix.cpp; vacías fuera de Windows)
void koniHookMaximiseFix (void* nativeHandle);
void koniUnhookMaximiseFix (void* nativeHandle);

class KoniSynthEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer, private juce::ComponentListener
{
public:
    static constexpr int kWidth = 1636, kHeight = 1050; // 1636: +100 px de ancho (2026-10-21) para las filas más llenas // 1050: perilla >= ~37 px en filas con cabecera (6 filas)

    explicit KoniSynthEditor (KoniSynthProcessor& p)
        : AudioProcessorEditor (p),
          koni (p),
          presetBar (p.presetManager),
          scope (p, [this] { openDisplayWindow(); }),
          keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
          pad (p.apvts),
          styloPad (p),
          engineAttachment (*p.apvts.getParameter ("engine"), [this] (float v) { engine = juce::roundToInt (v); updateMode(); })
    {
        setLookAndFeel (&lookAndFeel);
        auto& s = p.apvts;

        title.setText ("KoniSynth", juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centredLeft);
        title.setFont (juce::FontOptions (22.0f));
        title.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        title.setTooltip ("About KoniSynth (right-click: MIDI, tuning, randomness, skin and agent)");
        title.onRightClick = [this]
        {
            juce::PopupMenu m;
            addMainItems (m);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&title));
        };
        title.onClick = [this] { openPanel ("About", std::make_unique<AboutBody> (koni.easterEgg), 640, 340); };
        content.addAndMakeVisible (title);
        version.setText (KONI_VERSION_LABEL, juce::dontSendNotification);
        version.setFont (juce::FontOptions (12.0f));
        version.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.55f));
        version.setJustificationType (juce::Justification::bottomLeft);
        version.setInterceptsMouseClicks (false, false);
        content.addAndMakeVisible (version);
        // Motores agrupados (los ids son el índice del parámetro + 1: los presets y estados no cambian)
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter ("engine")))
        {
            const std::pair<const char*, std::vector<int>> groups[] = { { "Synthesizers", { 0, 6, 8, 9 } },
                                                                        { "Instruments", { 1, 2, 3, 4, 5 } },
                                                                        { "Consoles", { 7, 10, 11, 12 } },
                                                                        { "Computers", { 13, 14 } } };
            for (const auto& [heading, items] : groups)
            {
                engineCombo.addSectionHeading (heading);
                for (const auto k : items)
                    if (k < choice->choices.size())
                        engineCombo.addItem (choice->choices[k], k + 1);
            }
        }
        engineCombo.setTooltip (paramTooltip (s, "engine"));
        // Elegir un motor en el desplegable carga su preset más representativo (Synth: solo cambia de motor)
        engineComboAttachment = std::make_unique<PickComboAttachment> (*s.getParameter ("engine"), engineCombo,
                                                                         [this] (int e) { loadRepresentative (e, -1); });
        engineMenu = std::make_unique<ParamMenu> (s, "engine", engineCombo);
        content.addAndMakeVisible (engineCombo);
        panicButton.setTooltip ("Panic: silences everything at once (all notes off and effect tails cleared). Also MIDI CC 120. "
                                "Right-click: MIDI Learn (any button of your controller).");
        panicButton.learn = &p.midiLearn;
        panicButton.action = MidiLearn::actionPanic;
        panicButton.onClick = [&p] { p.requestPanic(); };
        panicButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        content.addAndMakeVisible (panicButton);
        // Deshacer / rehacer (Params/UndoHistory.h): botones y Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z)
        undoButton.onClick = [this] { koni.undoHistory.undo(); };
        redoButton.onClick = [this] { koni.undoHistory.redo(); };
        content.addAndMakeVisible (undoButton);
        content.addAndMakeVisible (redoButton);
        koni.undoHistory.onChange = [safe = SafePointer<KoniSynthEditor> (this)] { if (safe != nullptr) safe->refreshUndoButtons(); };
        refreshUndoButtons();
        // AVX2: LED encendido = ruta SIMD permitida (unísono y oscilador complejo); se guarda en la configuración global
        avxButton.setToggleState (simdStatus().userEnabled.load(), juce::dontSendNotification);
        avxButton.onClick = [this]
        {
            simdStatus().userEnabled = avxButton.getToggleState();
            if (auto config = openConfig())
            {
                config->setValue ("useAvx2", avxButton.getToggleState());
                config->saveIfNeeded();
            }
            updateAvxTooltip();
        };
        updateAvxTooltip();
        content.addAndMakeVisible (avxButton);
        // MCP: LED encendido = control por agente activo; el clic no conmuta, abre el diálogo Agent (MCP), donde se activa o
        // desactiva y se prepara la conexión (UI/AgentPanel.h). El LED sigue al estado con el temporizador del editor (2 Hz)
        mcpButton.setClickingTogglesState (false);
        mcpButton.onClick = [this] { openPanel ("Agent (MCP)", std::make_unique<AgentPanel> (koni), 920, 360); };
        updateMcpButton();
        content.addAndMakeVisible (mcpButton);
        midiButton.setTooltip ("MIDI input (all engines): MIDI channel, fine tune, bend range and controller profiles "
                               "(M-Audio Oxygen Pro Mini, Casio CT-S400, Yamaha PSR-EW425, Yamaha reface YC).");
        midiButton.onClick = [this] { midiMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton)); };
        content.addAndMakeVisible (midiButton);
        tuningLed.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        tuningLed.onClick = [this]
        {
            tuningMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tuningLed));
        };
        content.addChildComponent (tuningLed);
        refreshTuningLed();
        // Acciones del teclado en pantalla: barrita a su izquierda (acoplado) o franja inferior (oculto / en ventana)
        // Pestañas laterales de grupo: un clic pliega o despliega (el alto pasa a los grupos abiertos)
        const char* groupNames[5] = { "ENGINE", "PLAY/MOD/IN", "FX", "KEYS", "MACROS" };
        const char* groupTips[5] = { "Engine: sound sources, filter, envelopes and modulation of the selected engine. Click to fold or unfold; "
                                     "right-click: docked, folded or floating window.",
                                     "Performance (voice, arpeggiator, sequencer), modulation (LFOs) and audio input "
                                     "(input and vocoder). Click to fold or unfold.",
                                     "Effects chain and master. Click to fold or unfold.",
                                     "On-screen keyboard. Click to fold or unfold; right-click: docked, folded or floating window.",
                                     "Performance macros: 8 knobs, each moving several parameters at once. Click to fold or unfold." };
        displayTab = std::make_unique<GroupTab> ("DISPLAY", [] {});
        attachDockTab (*displayTab, displayFoldId);
        displayTab->setTooltip ("Display (scope, spectrum, spectrogram, goniometer, VU). Click to fold or unfold; right-click: "
                                "docked, folded or floating window.");
        content.addAndMakeVisible (*displayTab);
        for (int g = 0; g < 5; ++g)
        {
            if (g != 0 && g != 3)
                continue; // PLAY/MOD/IN, FX y MACROS: bloques con pestaña propia (setupBlocks)
            groupTabs[(size_t) g] = std::make_unique<GroupTab> (groupNames[g], [] {});
            if (g == 0)
                attachDockTab (*groupTabs[0], engineFoldId);
            else
                attachModeTab (*groupTabs[3], [this] { return keyboardMode(); }, [this] (int m) { setKeyboardMode (m); });
            groupTabs[(size_t) g]->setTooltip (groupTips[g]);
            (g == 0 ? (juce::Component&) rows : (juce::Component&) content).addAndMakeVisible (*groupTabs[(size_t) g]);
        }
        setupBlocks();
        // PAD: superficie propia del motor (pad del Theremin, placas del Stylophone), con los mismos modos que KEYS
        padTab = std::make_unique<GroupTab> ("PAD", [] {});
        attachModeTab (*padTab, [this] { return padMode(); }, [this] (int m) { setPadMode (m); });
        padTab->setTooltip ("Pad of the engine (Theremin pad, Stylophone plates). Click to show or hide; right-click: Docked, Hidden or "
                            "Floating window (resizable). MIDI and the keyboard keep playing the engine in any mode.");
        content.addChildComponent (*padTab);
        content.addAndMakeVisible (presetBar);
        presetBar.onBrowse = [this]
        {
            auto browser = std::make_unique<PresetBrowser> (koni);
            browser->onClose = [safe = SafePointer<KoniSynthEditor> (this)] { if (safe != nullptr) safe->panel.reset(); };
            openPanel ("Presets", std::move (browser), 1100, 600);
        };
        keyboard.attachPlayRange (koni.apvts.state, [this] { editPlayHint(); });
        keyboard.onFavorite = [this] (int n) { loadFavorite (n); };
        keyboard.scaleInfo = [this] { return ScaleBar::info (koni.apvts); };
        scaleBar.onChanged = [this]
        {
            keyboard.repaint();
            if (floatingKeyboard != nullptr) floatingKeyboard->keys.repaint();
        };
        keyboard.onPcKeysChanged = [this] (const juce::String& line) { scope.showCaption ("PC keyboard", line); };
        // Al cambiar de preset, el display muestra cómo tocarlo si el preset lo indica (y quita el rótulo anterior si no)
        presetBar.onPresetChanged = [this] (int index)
        {
            const auto& p = koni.presetManager.getPresets()[(size_t) index];
            // La A del teclado del PC va al comienzo del rango recomendado (a la blanca si es negra); sin rango, a C4
            koni.apvts.state.setProperty (NoteKeyboard::pcBaseProp, p.playLow >= 0 ? p.playLow : 60, nullptr);
            scope.showCaption (p.name, p.playHint);
            if (displayWindow != nullptr)
                displayWindow->view.showCaption (p.name, p.playHint);
        };

        // 1. Generación
        osc.withCombo (s, "wave")
           .withKnob (s, "osc1Level", "Level").withKnob (s, "unisonVoices", "Voices").withKnob (s, "unisonDetune", "Detune").withKnob (s, "unisonSpread", "Spread")
           .withKnob (s, "pulseWidth", "PW").withKnob (s, "pwmDepth", "PWM").withKnob (s, "drift", "Drift")
           .withKnob (s, "pitchEnvAmount", "P.Env").withKnob (s, "pitchEnvDecay", "P.Decay")
           .withKnob (s, "osc1TriSym", "Tri Sym").withKnob (s, "osc1TriRound", "Tri Rnd")
           .withSideBody (pitchEnvView, 120, true)
           .withControlRows (2); // 6 + 5 perillas: la fila del display es alta y así el display gana ancho
        complexSection.withToggle (s, "complexOn").withCombo (s, "modWave").withExtraToggle (s, "modSync", "Sync")
                      .withKnob (s, "complexModSource", "Mod Src").withKnob (s, "modRatio", "Ratio").withKnob (s, "fmIndex", "FM")
                      .withKnob (s, "amDepth", "AM/Ring").withKnob (s, "timbre", "Timbre")
                      .withKnob (s, "symmetry", "Symmetry").withKnob (s, "timbreMod", "T.Mod").withKnob (s, "modLp", "Mod LP");
        // Segundo grupo de pestañas (Osc 2 | Osc 3 | Noise), con Mute/Solo como las fuentes de Osc 1
        osc2Section.withCombo (s, "osc2Wave").withExtraToggle (s, "osc2Sync", "Sync")
                   .withHeaderButton ("Osc 2 Sample...", "Open the sample editor of Osc 2 (Sample waveform): load its own file, see its waveform, "
                                      "set Root, Start, Length and Loop.", [this] { openSamplePanel (KoniSynthProcessor::osc2Sample); })
                   .withHeaderButton ("Osc 2 Table...", "Open the wavetable editor of Osc 2 (Wavetable waveform): its own table, "
                                      "independent of the Wavetable tab; see the frames, load your own file.",
                                      [this] { openWavetablePanel (KoniSynthProcessor::wtOsc2); })
                   .withKnob (s, "osc2Level", "Level").withKnob (s, "osc2Octave", "Oct").withKnob (s, "osc2Semi", "Semi")
                   .withKnob (s, "osc2Fine", "Fine").withKnob (s, "osc2WtPosition", "WT Pos")
                   .withKnob (s, "osc2TriSym", "Tri Sym").withKnob (s, "osc2TriRound", "Tri Rnd");
        osc3Section.withCombo (s, "osc3Wave")
                   .withKnob (s, "subLevel", "Level").withKnob (s, "subOctave", "Oct").withKnob (s, "osc3Semi", "Semi")
                   .withKnob (s, "osc3Fine", "Fine").withKnob (s, "osc3TriSym", "Tri Sym").withKnob (s, "osc3TriRound", "Tri Rnd");
        noiseSection.withCombo (s, "noiseColor").withKnob (s, "noiseLevel", "Level");
        mixerTabs = std::make_unique<SourceTabs> (s, std::vector<SourceTabs::Source> {
            { "Osc 2", &osc2Section, "osc2Level", "osc2Mute" }, { "Osc 3", &osc3Section, "subLevel", "osc3Mute" },
            { "Noise", &noiseSection, "noiseLevel", "noiseMute" } }, Params::srcOsc2, koni.getParams().soloSource);
        // Sample, Wavetable y Sequencer: sección compacta + panel de edición propio (Edit Sample..., Edit Table..., Edit Steps...)
        sampleSection.withHeaderButton ("Edit Sample...", "Open the sample editor: load a file, see its waveform, set Start, Length and Loop.",
                                        [this] { openSamplePanel (KoniSynthProcessor::sourceSample); })
                     .withKnob (s, "sampleLevel", "Level").withKnob (s, "sampleRoot", "Root").withKnob (s, "sampleStart", "Start")
                     .withKnob (s, "sampleLength", "Length").withKnob (s, "sampleLoop", "Loop"); // también en Edit Sample...
        wavetableSection.withCombo (s, "wtTable")
                        .withHeaderButton ("Edit Table...", "Open the wavetable editor: see the frames, load your own table.", [this] { openWavetablePanel(); })
                        .withKnob (s, "wtLevel", "Level").withKnob (s, "wtPosition", "Position");
        seqSection.withToggle (s, "seqOn")
                  .withHeaderButton ("Edit Steps...", "Open the step sequencer: pitch, on/off and tie for each of the 16 steps.", [this] { openSequencerPanel(); })
                  .withKnob (s, "seqRate", "Rate"); // Steps, Gate y BPM: en el diálogo
        stringSection.withCombo (s, "stringExcite").withKnob (s, "stringLevel", "Level").withKnob (s, "stringDecay", "Decay")
                     .withKnob (s, "stringBright", "Bright").withKnob (s, "stringPick", "Pick")
                     .withKnob (s, "stringDamping", "Damping").withKnob (s, "stringPickup", "Pickup");
        // Fuentes en pestañas: suenan sumadas; cada una con su nivel, LED y Mute/Solo
        sourceTabs = std::make_unique<SourceTabs> (s, std::vector<SourceTabs::Source> {
            { "Osc 1", &osc, "osc1Level", "osc1Mute" }, { "Wavetable", &wavetableSection, "wtLevel", "wtMute" },
            { "Sample", &sampleSection, "sampleLevel", "sampleMute" }, { "String", &stringSection, "stringLevel", "stringMute" } },
            Params::srcOsc1, koni.getParams().soloSource);
        // Display de onda: muestra la fuente de la pestaña elegida (vistas en el orden de Params::SourceIndex) y sus etiquetas
        // eligen la pestaña. 420 px de ancho mínimo: dos ciclos legibles y la lectura de una línea
        waveSection.withBody (waveView, 0, 420);
        waveSection.stretchToFill = true; // el resto del ancho de la fila, para el display
        waveSection.bodyFlush = true;     // a ras del marco, como los displays de envolventes y LFO
        // Pestañas, vistas del display y fichas de la tira de señal van juntas: elegir en uno elige en los otros
        const auto pickSource = [this] (int v)
        {
            waveView.setView (v);
            if (v < Params::srcOsc2) sourceTabs->show (v);
            else if (v < WaveView::mix) mixerTabs->show (v - Params::srcOsc2);
            signalStrip.setSelectedSource (v < WaveView::mix ? v : -1);
        };
        sourceTabs->onSelect = [pickSource] (int k) { pickSource (Params::srcOsc1 + k); };
        mixerTabs->onSelect = [pickSource] (int k) { pickSource (Params::srcOsc2 + k); };
        waveView.onViewPicked = pickSource;
        signalStrip.onSourcePicked = pickSource;

        // 2. Forma: filtro + envolventes (sliders verticales, como en un panel de hardware)
        qualitySelector.addItemList ({ "Classic", "Balanced", "High Quality" }, 1);
        qualitySelector.setSelectedId (1, juce::dontSendNotification);
        qualitySelector.setTooltip ("Ladder quality (LP 24, LP 12, BP 12 and HP 12 only). Classic (default): the original filter. "
                                    "Balanced: 2x oversampling with antialiased drive: much less aliasing on high notes with Drive, moderate CPU. "
                                    "High Quality: 4x oversampling: the least aliasing, about twice the CPU of the Synth engine. "
                                    "Each mode has its own tone: the oversampled ones are brighter above the cutoff, Balanced a little less "
                                    "than High Quality. Saved with the project, not with presets. A change waits until all voices are silent "
                                    "(or use Apply now). Dimmed with the other filter modes: it can still be set, and applies to the ladder modes.");
        qualitySelector.onOpen = [this] { showQualityMenu(); };
        filter.withToggle (s, "filterOn").withCombo (s, "filterMode")
              .withHeaderButton ("Saturation...", "Saturation: clean filter input (Drive 0 % linear) and per-voice saturation after or "
                                           "before the filter (curve Soft, Diode, Hard, Fold, drive and position).", [this]
              {
                  openParamPanel ("Saturation", [this] (ParamPanel& p, APVTS& st)
                  {
                      p.add ("Filter Input").withToggle (st, "filterCleanDrive").withKnob (st, "filterDrive", "Drive");
                      p.add ("Voice Saturation").withCombo (st, "voiceSatMode").withKnob (st, "voiceSatDrive", "Drive")
                                                .withKnob (st, "voiceSatPos", "Position").withSideBody (voiceSatView, 150, true);
                  });
              })
              .withHeaderControl ("Ladder quality", qualitySelector, 190)
              .withKnob (s, "cutoff", "Cutoff").withKnob (s, "resonance", "Reso").withKnob (s, "filterDrive", "Drive")
              .withKnob (s, "filterEnv", "Env").withKnob (s, "filterKey", "Key").withKnob (s, "velToFilter", "Vel")
              .withKnob (s, "filterSpread", "Spread").withKnob (s, "voiceSatDrive", "Sat")
              .withSideBody (filterView, 240).withControlRows (2) // respuesta del filtro a la izquierda; 4 + 4 perillas
              .withItemTooltips ({ "Ladder, 4 poles (24 dB/oct): the classic Moog low-pass.", "Ladder, 2 poles (12 dB/oct).",
                                   "Ladder band-pass.", "Ladder high-pass.", "State-variable low-pass (12 dB/oct), smooth like the Oberheim SEM.",
                                   "State-variable band-pass.", "State-variable high-pass.", "State-variable notch: removes a band.",
                                   "Korg35 low-pass of the MS-20: biting resonance that screams.", "Korg35 high-pass of the MS-20 (6 dB/oct, like the original).",
                                   "Comb resonator: all harmonics of Cutoff (metallic, plucked). Key 100 % to play in tune.",
                                   "Comb with negative feedback: odd harmonics only (hollow, like a closed pipe).",
                                   "Two band-pass filters in parallel, Spread apart: vowel-like formants.",
                                   "Two low-pass filters in series, Spread apart." });
        // Las dos envolventes con la misma caja: Env Curve en la cabecera (es una sola curva para las dos: Synth.h,
        // updateEnvelopes), display de 160 px y 4 faders; el paneo por voz del VCA va al diálogo Voice Pan... de Amp Envelope
        filterEnvSection.withCombo (s, "envCurve")
                        .withSlider (s, "filterAttack", "A").withSlider (s, "filterDecay", "D")
                        .withSlider (s, "filterSustain", "S").withSlider (s, "filterRelease", "R")
                        .withSideBody (filterEnvView, 160, true); // curva con asas A, D/S, R (UI/ModViews.h)
        ampEnvSection.withCombo (s, "envCurve")
                     .withHeaderButton ("Voice Pan...", "Per-voice panning after the VCA: V.Pan (how far the voices spread) and Pan Mode.", [this]
                     {
                         openParamPanel ("Amp Envelope", [] (ParamPanel& p, APVTS& st)
                         {
                             p.add ("Voice Pan").withKnob (st, "voicePan", "V.Pan").withKnob (st, "voicePanMode", "Pan Mode");
                         });
                     })
                     .withSlider (s, "attack", "A").withSlider (s, "decay", "D")
                     .withSlider (s, "sustain", "S").withSlider (s, "release", "R")
                     .withSideBody (ampEnvView, 160, true);

        // Motores alternativos
        thereminSection.withCombo (s, "thereminModel")
                       .withKnob (s, "thereminRange", "Range").withKnob (s, "thereminGlide", "Glide")
                       .withKnob (s, "thereminTone", "Tone").withKnob (s, "thereminField", "Field")
                       .withKnob (s, "thereminLock", "Lock").withKnob (s, "thereminZero", "Zero")
                       .withKnob (s, "drift", "Drift"); // mismo parámetro que el Drift de Oscillator
        vl1Section.withCombo (s, "vl1Sound")
                  .withKnob (s, "vl1Octave", "Octave").withSlider (s, "vl1Attack", "A").withSlider (s, "vl1Decay", "D")
                  .withSlider (s, "vl1SustainLevel", "SL").withSlider (s, "vl1SustainTime", "ST").withSlider (s, "vl1Release", "R")
                  .withKnob (s, "vl1Vibrato", "V").withKnob (s, "vl1Tremolo", "T")
                  .withSideBody (vl1EnvView, 150, true);
        styloSection.withCombo (s, "styloModel").withExtraToggle (s, "styloVibrato", "Vibrato")
                   .withKnob (s, "styloRange", "Range").withKnob (s, "styloRegister", "Register")
                   .withKnob (s, "styloVibDepth", "Depth").withKnob (s, "styloTuning", "Tuning")
                   .withKnob (s, "styloOutput", "Output").withKnob (s, "styloClick", "Click");
        // Gen X-2 (experimental): oscilador y envolvente | LFO, filtro y delay, en el orden del panel frontal del manual
        genxOscSection.withExtraToggle (s, "genxPwm", "X (PWM)")
                      .withKnob (s, "genxSub1", "-1").withKnob (s, "genxSub2", "-2")
                      .withKnob (s, "genxOctave", "Octave").withKnob (s, "genxAttack", "Attack").withKnob (s, "genxPitch", "Pitch")
                      .withKnob (s, "genxDecay", "Decay").withKnob (s, "genxStrip", "Strip")
                      .withSideBody (genxEnvView, 130, true);
        genxFxSection.withExtraToggle (s, "genxDelay", "Delay")
                     .withKnob (s, "genxLfoWave", "Wave").withKnob (s, "genxLfoRate", "Rate").withKnob (s, "genxLfoDepth", "Depth")
                     .withKnob (s, "genxCutoff", "Cutoff").withKnob (s, "genxRes", "Res")
                     .withKnob (s, "genxTime", "Time").withKnob (s, "genxRepeats", "Repeats").withKnob (s, "genxLevel", "Level");
        omniSection.withKnob (s, "omniSplit", "Split").withKnob (s, "omniSustain", "Sustain").withKnob (s, "omniHarpOctave", "Oct")
                   .withKnob (s, "omniHarpLevel", "Harp").withKnob (s, "omniChordLevel", "Chord").withKnob (s, "omniBassLevel", "Bass");
        omniRhythmSection.withHeaderButton ("Edit Rhythms...", "Open the rhythm editor: draw your own rhythms (User 1-8) with drum lanes and the "
                                            "automatic bass (root, low root, 5th, 3rd of the chord you hold). Each one is a MIDI file "
                                            "you can also edit elsewhere (drums on channel 10, bass on channel 2).",
                                            [this] { openPanel ("Omnichord Rhythm Editor", std::make_unique<RhythmEditor> (koni, UserRhythms::Target::omnichord), 1300, 560); })
                         .withSortedCombo (s, "omniRhythm").withKnob (s, "omniTempo", "Tempo").withKnob (s, "omniRhythmLevel", "Level");
        drumSection.withCombo (s, "drumKit").withExtraToggle (s, "drumVelocity", "Velocity")
                   .withKnob (s, "drumTune", "Tune").withKnob (s, "drumDecay", "Decay").withKnob (s, "drumTone", "Tone")
                   .withKnob (s, "drumSnappy", "Snappy").withKnob (s, "drumAccent", "Accent");
        drumPatternSection.withHeaderButton ("Edit Rhythms...", "Open the rhythm editor: draw your own rhythms (User 1-8) on a grid of "
                                             "instrument lanes and steps. Each one is a MIDI file you can also edit elsewhere.",
                                             [this] { openPanel ("Rhythm Editor", std::make_unique<RhythmEditor> (koni, UserRhythms::Target::drums), 1300, 560); })
                          .withSortedCombo (s, "drumPattern").withKnob (s, "drumPattern2", "+ Pattern")
                          .withKnob (s, "drumTempo", "Tempo").withKnob (s, "drumSwing", "Swing")
                          .withBody (drumLeds, 12, 0); // fila de LED de los pasos sobre las perillas
        fmSection.withHeaderButton ("Operators...", "Open the FM editor: ratio, level, envelope and keyboard scaling of the 6 operators.",
                                    [this] { openFmPanel(); })
                 .withHeaderButton ("Load .syx...", "Load a Yamaha DX7 voice or 32-voice bank (SysEx .syx file): operators, "
                                    "algorithm, LFO, pitch envelope, transpose and key sync, as stored in the voice.",
                                    [this] { chooseDx7Syx(); })
                 .withKnob (s, "fmMode", "Mode").withKnob (s, "fmAlgorithm", "Algorithm").withKnob (s, "fmFeedback", "Feedback");
        for (int op = 1; op <= 6; ++op)
            fmSection.withKnob (s, "fm" + juce::String (op) + "Level", "OP" + juce::String (op));
        fmLfoSection.withCombo (s, "fmLfoWave").withExtraToggle (s, "fmLfoSync", "Sync")
                    .withKnob (s, "fmLfoSpeed", "Speed").withKnob (s, "fmLfoDelay", "Delay").withKnob (s, "fmLfoPmd", "PMD")
                    .withKnob (s, "fmLfoAmd", "AMD").withKnob (s, "fmLfoPms", "PMS");
        // Datos de cada chip: NESdev (CPU, 2A03, APU Mixer), Pan Docs (Specifications, Audio details), AtariAge (ports PAL)
        chipSection.withHeaderButton ("Sample...", "This engine's own user sample for DPCM = User Sample: load a sound (WAV, AIFF, FLAC, OGG, "
                                      ".sf2) to convert to 1-bit DPCM, or a .dmc file to play as it is.",
                                      [this] { openSamplePanel (KoniSynthProcessor::dpcmSample); })
                   .withPickCombo (s, "chipModel", [this] (int model) { loadRepresentative (7, model); })
                   .withItemTooltips ({
                       "Ricoh RP2A03, US NES. CPU 1.789773 MHz. 2 pulse channels (12.5/25/50/75 % duty), triangle, noise (long "
                       "32767-step or metallic 93-step mode) and 1-bit DPCM. Output filters: high-pass 90 Hz + 440 Hz, low-pass 14 kHz "
                       "(thinner bass than the Famicom). Sound driver at 60.10 Hz.",
                       "Ricoh RP2A07, European NES. CPU 1.662607 MHz, with its own noise, DPCM and frame-counter tables. Same "
                       "channels and filters as the NTSC NES. Sound driver at 50.01 Hz: vibrato, arpeggios and envelopes run 17 % "
                       "slower. Notes are retuned to the PAL clock (unadapted Japanese ports played ~17 % slower and lower).",
                       "Ricoh RP2A03, Japanese Famicom: same chip as the NTSC NES. Only a 37 Hz high-pass filter, so the bass is "
                       "fuller. Expansion sound: VRC6 here (Expansion = VRC6); VRC7 in the OPL engine; N163 and FDS are not emulated.",
                       "DMG-CPU (Sharp, SM83 core), original Game Boy. Clock 4.194304 MHz. Pulse 1 with hardware sweep, pulse 2 "
                       "(12.5/25/50/75 %), 32-sample 4-bit wave channel (the crunchy bass) and noise. Output capacitor: soft high-pass "
                       "(charge factor 0.999958 per cycle).",
                       "CPU CGB (Sharp, SM83 core), Game Boy Color. Same 4 sound channels and output capacitor (0.999958 per cycle) "
                       "as the DMG; the stronger CGB value from an early draft has no measurements and is not used.",
                       "Ricoh RP2A07 playing a Japanese/US game that was not adapted for Europe: the game writes pitch values made "
                       "for the 1.789773 MHz NTSC clock, so on the 1.662607 MHz PAL chip everything sounds 127.6 cents (~1.3 "
                       "semitones) lower, and its 50.01 Hz driver makes vibrato and arpeggios ~17 % slower. The classic sound of "
                       "many early PAL releases." }).withExtraToggle (s, "chipFilter", "Filter")
                   .withKnob (s, "chipVoice", "Voice").withKnob (s, "chipDuty", "Duty").withKnob (s, "chipVolume", "Volume")
                   .withKnob (s, "chipEnvelope", "Env").withKnob (s, "chipEnvRate", "Rate").withKnob (s, "chipRelease", "Release")
                   .withKnob (s, "chipSweep", "Sweep").withKnob (s, "chipSweepRate", "S.Rate").withKnob (s, "chipNoiseMode", "Noise")
                   .withKnob (s, "chipWave", "Wave").withKnob (s, "chipWaveLevel", "W.Level").withKnob (s, "chipDpcm", "DPCM");
        chipDriverSection.withHeaderButton ("Frame Macros...", "Per-frame macros (volume, arpeggio, pitch, duty), as in FamiTracker and Furnace: "
                                            "sound effects and instruments that change every video frame.", [this] { openChipMacros(); })
                         .withCombo (s, "chipArp").withExtraToggle (s, "chipVelocity", "Velocity")
                         .withKnob (s, "chipArpSpeed", "Arp Spd").withKnob (s, "chipVibDepth", "Vib").withKnob (s, "chipVibRate", "Vib Rate")
                         .withKnob (s, "chipVibDelay", "Vib Delay").withKnob (s, "chipDpcmLoop", "DPCM Loop");
        // Datos de cada modelo: Nuked-OPN2 (ladder), Genesis Plus GX y ajustes del Mega Sg (filtro), Wikipedia "Yamaha YM2612"
        mdSection.withCombo (s, "mdModel")
                 .withItemTooltips ({
                     "Discrete Yamaha YM2612 (first Mega Drive / Genesis boards). FM at 53,267 Hz (master clock / 1008). Its 9-bit DAC "
                     "outputs each channel 1 cycle in 4 and the sign the rest: a jump of ~1.6 % of full scale at every zero crossing "
                     "(the 'ladder effect') that adds grit to quiet notes. Analog output low-pass ~3.4 kHz.",
                     "Yamaha YM3438 (OPN2C) core inside the later ASIC: no ladder effect and a cleaner low end; some early games "
                     "that relied on the ladder sound quieter. Same ~3.4 kHz FM low-pass as the emulators' Model 2 setting.",
                     "YM3438 core without the analog low-pass filter: the raw digital output, brighter than any real console." })
                 .withHeaderButton ("Operators...", "Open the Mega Drive editor: the 4 operators with the YM2612's own registers.", [this] { openMdPanel(); })
                 .withKnob (s, "mdAlgorithm", "Algorithm").withKnob (s, "mdFeedback", "Feedback");
        for (int op = 1; op <= 4; ++op)
            mdSection.withKnob (s, "md" + juce::String (op) + "Tl", "OP" + juce::String (op) + " TL");
        snesSection.withHeaderButton ("Sample...", "This engine's own user sample for Source = User Sample: load a sound (WAV, AIFF, FLAC, "
                                      "OGG, .sf2) to encode to BRR, or a .brr / .spc file to play as it is.",
                                      [this] { openSamplePanel (KoniSynthProcessor::snesSample); })
                   .withHeaderButton ("Frame Macros...", "Experimental per-frame macros (60.10 Hz): volume, arpeggio, pitch and noise per voice. "
                                      "Nintendo's N-SPC driver stepped by tempo ticks, not video frames.", [this] { openSnesMacros(); })
                   .withCombo (s, "snesSource").withExtraToggle (s, "snesNoise", "Noise")
                   .withKnob (s, "snesNoiseRate", "Noise Rate").withKnob (s, "snesInterp", "Interp").withKnob (s, "snesUserRate", "User Rate")
                   .withKnob (s, "snesVolume", "Volume");
        snesEnvSection.withCombo (s, "snesEnvMode").withExtraToggle (s, "snesVelocity", "Velocity")
                      .withSlider (s, "snesAttack", "A").withSlider (s, "snesDecay", "D").withSlider (s, "snesSustainLevel", "SL")
                      .withSlider (s, "snesSustainRate", "SR").withKnob (s, "snesGainRate", "Gain Rate").withKnob (s, "snesDirectGain", "Direct");
        snesEchoSection.withToggle (s, "snesEcho").withCombo (s, "snesFir").withExtraToggle (s, "snesFilter", "Filter")
                       .withKnob (s, "snesEdl", "EDL").withKnob (s, "snesEfb", "EFB").withKnob (s, "snesEvol", "EVOL").withKnob (s, "snesMainVolume", "Main")
                       .withKnob (s, "snesPan", "Pan").withKnob (s, "snesSpread", "Spread").withKnob (s, "snesOutput", "Output")
                       .withKnob (s, "snesDac", "DAC");
        n64Section.withHeaderButton ("Sample...", "This engine's own user sample for Source = User Sample: load a sound (WAV, AIFF, FLAC, "
                                     "OGG, .sf2) to encode to VADPCM.", [this] { openSamplePanel (KoniSynthProcessor::n64Sample); })
                  .withCombo (s, "n64Source").withExtraToggle (s, "n64Adpcm", "VADPCM")
                  .withKnob (s, "n64SourceRate", "Src Rate").withKnob (s, "n64OutputRate", "Out Rate").withKnob (s, "n64Volume", "Volume")
                  .withKnob (s, "n64Pan", "Pan").withKnob (s, "n64Spread", "Spread");
        n64EnvSection.withExtraToggle (s, "n64Velocity", "Velocity")
                     .withKnob (s, "n64Attack", "Attack").withKnob (s, "n64Decay", "Decay").withKnob (s, "n64Release", "Release")
                     .withKnob (s, "n64AttackVolume", "A Vol").withKnob (s, "n64DecayVolume", "D Vol")
                     .withSideBody (n64EnvView, 150, true);
        n64FxSection.withCombo (s, "n64Fx").withKnob (s, "n64FxMix", "Mix").withKnob (s, "n64Dac", "DAC");
        oplSection.withCombo (s, "oplModel").withExtraToggle (s, "oplFourOp", "4-Op")
                  .withHeaderButton ("Operators...", "Open the OPL editor: the operators with the chip's own registers.", [this] { openOplPanel(); })
                  .withKnob (s, "oplAlgorithm", "Algorithm").withKnob (s, "oplFeedback", "Feedback");
        for (int op = 1; op <= 4; ++op)
            oplSection.withKnob (s, "opl" + juce::String (op) + "Tl", "OP" + juce::String (op) + " TL");
        opllSection.withCombo (s, "opllInstrument").withExtraToggle (s, "opllRhythm", "Rhythm")
                   .withKnob (s, "opllVolume", "Volume").withKnob (s, "opllSustain", "SUS");
        oplLfoSection.withExtraToggle (s, "oplVelocity", "Velocity")
                     .withKnob (s, "oplDeepTremolo", "Deep AM").withKnob (s, "oplDeepVibrato", "Deep VIB")
                     .withKnob (s, "oplPan", "Pan").withKnob (s, "oplOutput", "Output");
        mdLfoSection.withCombo (s, "mdLfo").withExtraToggle (s, "mdVelocity", "Velocity")
                    .withKnob (s, "mdPms", "PMS").withKnob (s, "mdAms", "AMS").withKnob (s, "mdPan", "Pan")
                    .withKnob (s, "mdOutput", "Output");
        mdVoiceSection.withHeaderButton ("Sample...", "This engine's own sample for Voice = DAC: load a sound (WAV, AIFF, FLAC, OGG, .sf2); "
                                         "it is converted to the 8-bit DAC at the DAC Rate.", [this] { openSamplePanel (KoniSynthProcessor::mdSample); })
                      .withHeaderButton ("Frame Macros...", "Per-frame macros of the sound driver (59.92 Hz): volume, arpeggio and pitch "
                                         "for FM and PSG, and the PSG noise type.", [this] { openMdMacros(); })
                      .withCombo (s, "mdVoice")
                      .withKnob (s, "mdDacRate", "DAC Rate").withKnob (s, "mdCh3Op1", "Ch3 OP1").withKnob (s, "mdCh3Op2", "Ch3 OP2")
                      .withKnob (s, "mdCh3Op3", "Ch3 OP3");
        mdPsgSection.withCombo (s, "mdPsgNoise")
                    .withKnob (s, "mdPsgNoiseRate", "N.Rate").withSlider (s, "mdPsgAttack", "A").withSlider (s, "mdPsgDecay", "D")
                    .withSlider (s, "mdPsgSustain", "S").withSlider (s, "mdPsgRelease", "R")
                    .withSideBody (mdPsgEnvView, 150, true);
        chipVrc6Section.withCombo (s, "chipExpansion").withExtraToggle (s, "chipVrc6Overdrive", "Overdrive")
                       .withKnob (s, "chipVrc6Duty", "Duty");
        chipStereoSection.withKnob (s, "chipPan1", "P1").withKnob (s, "chipPan2", "P2").withKnob (s, "chipPan3", "Wave")
                         .withKnob (s, "chipPan4", "Noise").withKnob (s, "chipOutput", "Output");
        pvOscSection.withCombo (s, "pvVoice")
                    .withKnob (s, "pvWave1", "Wave 1").withKnob (s, "pvRange1", "Range 1").withKnob (s, "pvWave2", "Wave 2")
                    .withKnob (s, "pvRange2", "Range 2").withKnob (s, "pvDetune", "Detune").withKnob (s, "pvFm", "FM")
                    .withKnob (s, "pvGlide", "Glide").withKnob (s, "pvDrift", "Drift");
        pvFilterSection.withCombo (s, "pvFilterMode")
                       .withKnob (s, "pvMix1", "VCO 1").withKnob (s, "pvMix2", "VCO 2").withKnob (s, "pvNoise", "Noise")
                       .withKnob (s, "pvCutoff", "Cutoff").withKnob (s, "pvReso", "Reso").withKnob (s, "pvEnvAmount", "Env");
        pvFEnvSection.withExtraToggle (s, "pvFLoop", "Repeat")
                     .withSlider (s, "pvFAttack", "A").withSlider (s, "pvFDecay", "D").withSlider (s, "pvFSustain", "S").withSlider (s, "pvFRelease", "R")
                     .withSideBody (pvFEnvView, 150, true);
        pvAEnvSection.withExtraToggle (s, "pvALoop", "Repeat")
                     .withSlider (s, "pvAAttack", "A").withSlider (s, "pvADecay", "D").withSlider (s, "pvASustain", "S").withSlider (s, "pvARelease", "R")
                     .withSideBody (pvAEnvView, 150, true);
        pvLfoSection.withCombo (s, "pvLfoWave")
                    .withKnob (s, "pvLfoRate", "Rate").withKnob (s, "pvLfoVco1", "VCO 1").withKnob (s, "pvLfoVco2", "VCO 2")
                    .withKnob (s, "pvLfoVcf", "VCF").withKnob (s, "pvLfoVca", "VCA");
        mt32Section.withCombo (s, "mt32Model").withExtraToggle (s, "mt32Multi", "Multi")
                   .withHeaderButton ("ROMs...", "The Roland ROMs (control + PCM) are not included: put yours in a folder and choose it "
                                      "here. They are recognised by size and SHA-1 (MT-32 v1.04-2.07, CM-32L, LAPC-I; halves are merged).",
                                      [this] { mt32RomMenu(); })
                   .withHeaderButton ("Options...", "Optional Munt beautifications, all off by default (the real unit as it is): "
                                      "nice amp ramp, nice panning, nice partial mixing; the unit's reverb and the DAC mode.", [this]
                   {
                       openParamPanel ("MT-32 Options", [] (ParamPanel& p, APVTS& st)
                       {
                           p.add ("Reverb").withToggle (st, "mt32Reverb");
                           p.add ("DAC").withCombo (st, "mt32Dac");
                           p.add ("Amp Ramp").withToggle (st, "mt32NiceAmp");
                           p.add ("Panning").withToggle (st, "mt32NicePan");
                           p.add ("Partial Mixing").withToggle (st, "mt32NiceMix");
                       });
                   })
                   .withKnob (s, "mt32Mode", "Keys").withList (s, "mt32Program", "Program", 1.0f).withKnob (s, "mt32Reverb", "Reverb");
        mt32DisplaySection.withBody (mt32Lcd, 0, 900); // todo el alto; 900 px: ~6,4 px por punto (121 × 6,4 + márgenes) y el LED
        fmDisplaySection.withBody (dx7Display, 0, 560); // 16 × 2 caracteres y el LED de 2 dígitos a ~4,6 px por punto
        ansSection.withCombo (s, "ansScore").withExtraToggle (s, "ansHold", "Hold")
                  .withHeaderButton ("Plate...", "Open the ANS plate: draw the score (pitch vertically, time horizontally) and set the 20 registers.",
                                     [this] { openAnsPanel(); })
                  .withKnob (s, "ansSpeed", "Speed").withKnob (s, "ansEmulsion", "Emulsion");
        flangerSection.withToggle (s, "flangerOn").withExtraToggle (s, "flangerMatrix", "Matrix")
                      .withKnob (s, "flangerRate", "Rate").withKnob (s, "flangerRange", "Range").withKnob (s, "flangerColor", "Color")
                      .withKnob (s, "flangerMix", "Mix");
        fshiftSection.withToggle (s, "fshiftOn")
                     .withKnob (s, "fshiftHz", "Shift").withKnob (s, "fshiftBalance", "Up/Down").withKnob (s, "fshiftMix", "Mix");
        leslieSection.withToggle (s, "leslieOn").withCombo (s, "leslieSpeed")
                     .withKnob (s, "leslieDrive", "Drive").withKnob (s, "leslieMix", "Mix");
        springSection.withToggle (s, "springOn").withKnob (s, "springDecay", "Decay").withKnob (s, "springMix", "Mix");
        vl1RhythmSection.withHeaderButton ("Edit Rhythms...", "Open the rhythm editor: draw your own rhythms (User 1-8) with the three VL-1 "
                                           "sounds (Pock, Peek, Noise; one per step). Each one is a MIDI file you can also edit elsewhere "
                                           "(channel 10: notes 36, 37, 42).",
                                           [this] { openPanel ("VL-1 Rhythm Editor", std::make_unique<RhythmEditor> (koni, UserRhythms::Target::vl1), 1300, 400); })
                        .withCombo (s, "vl1Rhythm")
                        .withKnob (s, "vl1Tempo", "Tempo").withKnob (s, "vl1Balance", "Balance");
        vl1DisplaySection.withBody (vl1Display, 0, 420); // 9 posiciones de 7 segmentos a ~52 px de alto

        // 3. Entrada, vocoder y modulación
        inputSection.withKnob (s, "inputGain", "Gain").withKnob (s, "inputMonitor", "Monitor");
        vocoderSection.withToggle (s, "vocoderOn")
                      .withHeaderButton ("Bands...", "Vocoder settings: filter bank, band envelopes (attack, release) and sibilance.", [this]
                      {
                          openParamPanel ("Vocoder", [] (ParamPanel& p, APVTS& st)
                          {
                              p.add ("Filter Bank").withCombo (st, "vocoderBank");
                              p.add ("Band Envelopes").withKnob (st, "vocoderAttack", "Attack").withKnob (st, "vocoderRelease", "Release");
                              p.add ("Consonants").withKnob (st, "vocoderSibilance", "Sibilance");
                          });
                      })
                      .withKnob (s, "vocoderMix", "Mix").withKnob (s, "vocoderFormant", "Formant");
        lfo.withToggle (s, "lfoOn").withCombo (s, "lfoTarget")
           .withKnob (s, "lfoRate", "Rate").withKnob (s, "lfoDepth", "Depth").withKnob (s, "lfoShape", "Shape").withKnob (s, "lfoSync", "Sync")
           .withSideBody (lfoView, 170, true); // forma, alcance en el destino, Depth y Rate arrastrables (UI/ModViews.h)
        lfo2Section.withToggle (s, "lfo2On").withCombo (s, "lfo2Target")
                   .withHeaderButton ("Matrix...", "Modulation matrix of the Synth engine: 8 slots, each a source (LFOs, envelopes, velocity, "
                                      "key, wheel, aftertouch, MPE timbre, random, bend) moving a destination per voice. The number in "
                                      "brackets is the count of active routes; amber rings on the knobs show their range.",
                                      [this] { openPanel ("Modulation Matrix", std::make_unique<MatrixPanel> (koni), 1100, 520); })
                   .withKnob (s, "lfo2Rate", "Rate").withKnob (s, "lfo2Depth", "Depth").withKnob (s, "lfo2Shape", "Shape").withKnob (s, "lfo2Sync", "Sync")
                   .withSideBody (lfo2View, 170, true);
        formantSection.withToggle (s, "formantOn").withCombo (s, "formantVoice")
                      .withKnob (s, "formantVowel", "Vowel").withKnob (s, "formantShift", "Shift").withKnob (s, "formantRate", "Morph")
                      .withKnob (s, "formantDepth", "Depth").withKnob (s, "formantMix", "Mix");
        controllerSection.withCombo (s, "voiceMode")
                         .withHeaderButton ("Voice...", "Voice settings of the Synth engine: aftertouch, MPE, voice assignment, oscillator "
                                            "phase, tolerances and analog VCA. MIDI channel, fine tune, bend range and controller "
                                            "profiles are global: the MIDI button left of the engine selector.", [this]
                         {
                             openParamPanel ("Synth Voice", [] (ParamPanel& p, APVTS& st)
                             {
                                 p.add ("Aftertouch").withKnob (st, "atFilter", "AT Filt").withKnob (st, "atVibrato", "AT Vib").withKnob (st, "atAmp", "AT Vol");
                                 p.add ("MPE").withToggle (st, "mpeOn").withKnob (st, "mpeBendRange", "Bend");
                                 p.add ("Voice Assign").withCombo (st, "voiceAssign");
                                 p.add ("Osc Phase").withCombo (st, "oscPhase");
                                 p.add ("Tolerances").withCombo (st, "voiceTolerance");
                                 p.add ("Analog VCA").withKnob (st, "vcaClick", "Click").withKnob (st, "vcaLeak", "Leak");
                             });
                         })
                         .withKnob (s, "glide", "Glide").withKnob (s, "modWheelDepth", "Mod Wheel");
        arpSection.withToggle (s, "arpOn").withCombo (s, "arpMode")
                  .withHeaderButton ("Gate/Tempo...", "Arpeggiator settings: latch, octaves, gate, one-shot and the internal tempo.", [this]
                  {
                      openParamPanel ("Arpeggiator", [] (ParamPanel& p, APVTS& st)
                      {
                          p.add ("Arpeggiator").withExtraToggle (st, "arpLatch", "Latch").withKnob (st, "arpOctaves", "Octaves").withKnob (st, "arpGate", "Gate")
                                               .withKnob (st, "arpOneShot", "1-Shot");
                          p.add ("Tempo").withKnob (st, "arpBpm", "BPM");
                      });
                  })
                  .withKnob (s, "arpRate", "Rate");

        // Macros de interpretación (Params/PerfMacros.h): 8 perillas con el nombre de cada macro
        macroSection.withHeaderButton ("Assign...", "Performance macros: names, assigned parameters and their From / To ranges. "
                                       "Assign a parameter with right-click on its control > Assign to macro.", [this] { openPerfMacroPanel(); });
        for (int m = 0; m < PerfMacros::numMacros; ++m)
            macroSection.withKnob (s, PerfMacros::paramId (m), koni.perfMacros.name (m));

        // 4–5. Efectos en el orden de la cadena y master
        crushSection.withToggle (s, "crushOn")
                    .withKnob (s, "crushBits", "Bits").withKnob (s, "crushRate", "Rate").withKnob (s, "crushDrive", "Drive")
                    .withKnob (s, "crushMix", "Mix").withSideBody (crushView, 120, true);
        phaserSection.withToggle (s, "phaserOn")
                     .withKnob (s, "phaserRate", "Rate").withKnob (s, "phaserDepth", "Depth")
                     .withKnob (s, "phaserFeedback", "Color").withKnob (s, "phaserMix", "Mix");
        tube.withToggle (s, "tubeOn")
            .withKnob (s, "tubeDrive", "Drive").withKnob (s, "tubeBias", "Bias").withSideBody (tubeView, 120, true);
        // EQ: en el panel, como el de un ampli (Bass / Mid / Treble); todo lo demás, curva y nodos en EQ Bands...
        eqSection.withToggle (s, "eqOn")
                 .withHeaderButton ("EQ Bands...", "Parametric EQ: low/high cuts, shelves, two mid bands with Q, and the response curve "
                                    "over the live spectrum (drag the nodes).", [this] { openPanel ("Equalizer", std::make_unique<EqPanel> (koni), 1180, 640); })
                 .withKnob (s, "eqLowGain", "Bass").withKnob (s, "eqMid1Gain", "Mid").withKnob (s, "eqHighGain", "Treble");
        // Ampli: modelo y mandos de un ampli en el panel; Presence, Sag y Output en Cabinet...
        // Amp con su pantalla (la pantalla solo actúa con el ampli encendido): en el panel lo que se toca tocando, incluida la
        // mezcla de la pantalla (Cab); tipo de pantalla e IR del usuario en Cabinet...
        ampSection.withToggle (s, "ampOn").withCombo (s, "ampModel")
                  .withHeaderButton ("Cabinet...", "Amp: presence, power supply sag and output level; speaker cabinet type and your own "
                                     "impulse response.", [this]
                  {
                      openParamPanel ("Amp and Cabinet", [this] (ParamPanel& p, APVTS& st)
                      {
                          p.add ("Amp").withKnob (st, "ampPresence", "Presence").withKnob (st, "ampSag", "Sag").withKnob (st, "ampOutput", "Output");
                          cabDialogSection = &p.add ("Cabinet").withCombo (st, "cabType")
                                                .withHeaderButton ("Load IR...", "Load your own cabinet impulse response (WAV, AIFF, FLAC; up to 3 s) "
                                                                   "and select User IR. The file is referenced by user presets, like samples.",
                                                                   [this] { chooseCabIr(); })
                                                .withKnob (st, "cabMix", "Mix");
                          refreshCabIrInfo();
                      });
                  })
                  .withKnob (s, "ampGain", "Gain").withKnob (s, "ampBass", "Bass").withKnob (s, "ampMid", "Mid")
                  .withKnob (s, "ampTreble", "Treble").withKnob (s, "ampMaster", "Master").withKnob (s, "cabMix", "Cab");
        // Dinámica: en el panel lo que se toca tocando; tiempos, rodilla, llave y curva de transferencia en Sidechain...
        transSection.withToggle (s, "transOn").withKnob (s, "transAttack", "Attack").withKnob (s, "transSustain", "Sustain");
        compSection.withToggle (s, "compOn").withCombo (s, "compMode")
                   .withHeaderButton ("Sidechain...", "Compressor: attack, release, knee, makeup, sidechain filter and key (ducking from "
                                      "the audio input), with the transfer curve and the gain reduction meter.",
                                      [this] { openPanel ("Dynamics", std::make_unique<DynamicsPanel> (koni), 980, 560); })
                   .withKnob (s, "compThreshold", "Thresh").withKnob (s, "compRatio", "Ratio").withKnob (s, "compMix", "Mix");
        chorusSection.withToggle (s, "chorusOn").withCombo (s, "chorusMode")
                     .withKnob (s, "chorusMix", "Mix").withKnob (s, "chorusDepth", "Depth");
        echo.withToggle (s, "echoOn")
            .withHeaderButton ("Tone/Wow...", "Tape echo settings: ping-pong, tone, wow and tempo sync.", [this]
            {
                openParamPanel ("Tape Echo", [] (ParamPanel& p, APVTS& st)
                {
                    p.add ("Tape Echo").withExtraToggle (st, "echoPingPong", "Ping-Pong")
                                       .withKnob (st, "echoTone", "Tone").withKnob (st, "echoWow", "Wow").withKnob (st, "echoSync", "Sync");
                });
            })
            .withKnob (s, "echoTime", "Time").withKnob (s, "echoFeedback", "Feedback").withKnob (s, "echoMix", "Mix");
        ambient.withToggle (s, "ambientOn").withExtraToggle (s, "reverbFreeze", "Freeze")
               .withHeaderButton ("Diffusion...", "Plate reverb settings: predelay, diffusion, damping, tone, modulation and width.", [this]
               {
                   openParamPanel ("Plate Reverb", [] (ParamPanel& p, APVTS& st)
                   {
                       p.add ("Plate Reverb").withKnob (st, "reverbPredelay", "Predelay").withKnob (st, "reverbDiffusion", "Diffusion")
                                             .withKnob (st, "reverbDamp", "Damp").withKnob (st, "reverbTone", "Tone")
                                             .withKnob (st, "reverbMod", "Mod").withKnob (st, "reverbWidth", "Width");
                   });
               })
               .withKnob (s, "reverbDecay", "Decay").withKnob (s, "ambientSize", "Size").withKnob (s, "ambientMix", "Mix");
        mediaSection.withToggle (s, "mediaOn").withCombo (s, "mediaType")
                    .withKnob (s, "mediaAge", "Age").withKnob (s, "mediaNoise", "Noise")
                    .withKnob (s, "mediaWow", "Wow").withKnob (s, "mediaDrive", "Drive");
        limiterSection.withToggle (s, "limOn").withCombo (s, "limLookahead")
                      .withKnob (s, "limCeiling", "Ceiling").withKnob (s, "limRelease", "Release");
        master.withExtraToggle (s, "monoOut", "Mono").withKnob (s, "gain", "Gain");

        refreshSampleInfo();
        startTimerHz (2); // el host puede restaurar un estado con otro sample con el editor abierto
        ringTimer.tick = [this] { updateRings(); };
        ringTimer.startTimerHz (30);

        for (auto* c : { &master, &lfo, &echo, &ambient, &tube, &eqSection, &crushSection, &phaserSection, &lfo2Section, &formantSection, &chorusSection, &mediaSection, &vocoderSection, &inputSection,
                         &controllerSection, &arpSection, &seqSection, &macroSection, &flangerSection, &fshiftSection, &leslieSection, &springSection,
                         &transSection, &compSection, &limiterSection, &ampSection })
            rows.addAndMakeVisible (*c);
        rows.addChildComponent (*sourceTabs);
        rows.addChildComponent (*mixerTabs);
        for (auto* c : std::initializer_list<juce::Component*> { &signalStrip, &waveSection, &filter, &complexSection, &thereminSection,
                                                                 &vl1Section, &vl1RhythmSection, &vl1DisplaySection, &styloSection, &genxOscSection, &genxFxSection, &omniSection, &omniRhythmSection,
                                                                 &drumSection, &drumPatternSection, &fmSection, &fmLfoSection, &fmDisplaySection, &chipSection, &chipDriverSection, &chipStereoSection, &chipVrc6Section, &mdSection, &mdLfoSection, &mdVoiceSection, &mdPsgSection, &snesSection, &snesEnvSection, &snesEchoSection, &n64Section, &n64EnvSection, &n64FxSection, &oplSection, &oplLfoSection, &opllSection, &mt32Section, &mt32DisplaySection, &pvOscSection, &pvFilterSection,
                                                                 &pvFEnvSection, &pvAEnvSection, &pvLfoSection, &ansSection, &filterEnvSection,
                                                                 &ampEnvSection })
            rows.addChildComponent (*c);
        for (auto* c : std::initializer_list<juce::Component*> { &keyboard, &pad, &styloPad })
            content.addChildComponent (*c);
        rowsView.setViewedComponent (&rows, false);
        rowsView.setScrollBarsShown (true, false); // solo vertical
        rowsView.setScrollBarThickness (32);       // 32 px (antes 12): más fácil de agarrar
        // Arrastrar el fondo (marcos, títulos, huecos) desplaza el panel en vertical; los controles no: botones, selectores y
        // listas se marcan aquí (perillas, sliders y asas de los displays ya lo hacen en su constructor)
        rowsView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        std::function<void (juce::Component&)> blockDrag = [&blockDrag] (juce::Component& c)
        {
            if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::ComboBox*> (&c) != nullptr
                || dynamic_cast<juce::ListBox*> (&c) != nullptr || dynamic_cast<juce::TextEditor*> (&c) != nullptr)
                c.setViewportIgnoreDragFlag (true);
            for (auto* child : c.getChildren())
                blockDrag (*child);
        };
        blockDrag (rows);
        content.addAndMakeVisible (rowsView);
        content.addAndMakeVisible (scaleBar);
        content.addAndMakeVisible (scope);
        // Modo y estilo del display: propiedades del estado del plugin
        scope.setMode ((int) koni.apvts.state.getProperty ("displayMode", 0));
        scope.setStyle ((int) koni.apvts.state.getProperty ("displayStyle", 0));
        scope.onModeChange = [this] (int m, int st)
        {
            koni.apvts.state.setProperty ("displayMode", m, nullptr);
            koni.apvts.state.setProperty ("displayStyle", st, nullptr);
        };
        // Marco del display: displayBezelMode (Auto / Brass / None). Estados anteriores solo tienen displayBezel (latón sí/no):
        // apagado -> None, encendido -> Auto
        {
            const auto& st = koni.apvts.state;
            scope.setBezel (st.hasProperty ("displayBezelMode") ? (int) st.getProperty ("displayBezelMode")
                                                                : ((bool) st.getProperty ("displayBezel", true) ? Visualizer::bezelAuto : Visualizer::bezelNone));
        }
        scope.onBezelChange = [this] (int b) { koni.apvts.state.setProperty ("displayBezelMode", b, nullptr); };
        content.addAndMakeVisible (bpmDisplay);
        transportPanel.onError = [this] (const juce::String& title, const juce::String& message)
        {
            overlay = Overlay::create (*this, title, message, std::nullopt, "OK", {}, [this] (bool, const juce::String&, int) { overlay.reset(); });
        };
        content.addAndMakeVisible (transportPanel);

        content.setSize (kWidth, kHeight);
        addAndMakeVisible (content);
        engineAttachment.sendInitialUpdate(); // decide qué se ve con cada motor (y maqueta)

        setResizable (true, true);
        // Ancho: escala (mínimo 70 %); alto: libre, las filas que no caben se desplazan (mínimo: una línea visible)
        setResizeLimits (kWidth * 7 / 10, 420, kWidth * 5 / 2, 4000); // máximo 2,5x: ventana maximizada en pantallas grandes
        setSize (kWidth * 3 / 4, kHeight * 3 / 4);
        if (keyboardMode() == 2) // el proyecto se guardó con el teclado en ventana propia
            setKeyboardMode (2);
    }

    ~KoniSynthEditor() override
    {
        koni.undoHistory.onChange = nullptr;
        blockWindows.clear();
        bpmWindow.reset();
        engineWindow.reset();
        displayWindow.reset();
        floatingKeyboard.reset();
        floatingPad.reset();
        if (auto* area = hookedNotification.getComponent())
            area->removeComponentListener (this);
        if (auto* window = hookedWindow.getComponent(); window != nullptr && window->getPeer() != nullptr)
            koniUnhookMaximiseFix (window->getPeer()->getNativeHandle());
        if (auto* original = hookedOptions.getComponent())
            original->setVisible (true); // el botón propio muere con el editor (p. ej. "Reset to default state")
        setLookAndFeel (nullptr);
    }

    // Ventana standalone de JUCE: su botón "Options" se sustituye por uno con las mismas entradas + "About KoniSynth...",
    // y el botón "Settings..." del aviso amarillo (entrada silenciada) pasa a "Audio/MIDI config". Sin modificar JUCE:
    // se localizan por texto entre los hijos de la ventana.
    void parentHierarchyChanged() override
    {
        AudioProcessorEditor::parentHierarchyChanged();
       #if JucePlugin_Build_Standalone
        if (koni.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
            return;
        auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
        if (window == nullptr || window == hookedWindow.getComponent())
            return;
        hookedWindow = window;
        // Windows: ventana maximizada con la barra de tareas a la izquierda o arriba (Source/WinMaximiseFix.cpp); el HWND
        // existe cuando la ventana está en el escritorio: tras este cambio de jerarquía
        juce::MessageManager::callAsync ([w = juce::Component::SafePointer<juce::Component> (window)]
        {
            if (w != nullptr && w->getPeer() != nullptr) koniHookMaximiseFix (w->getPeer()->getNativeHandle());
        });
        window->setName (juce::String ("KoniSynth ") + KONI_VERSION_LABEL);
        window->setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, false); // + maximizar (y doble clic en el título)

        for (auto* c : window->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b != &optionsButton && b->getButtonText() == "Options")
                hookedOptions = b;
        if (auto* original = hookedOptions.getComponent())
        {
            original->setVisible (false);
            optionsButton.setTriggeredOnMouseDown (true);
            optionsButton.setTooltip ("Audio/MIDI configuration, save or load the app state, and About KoniSynth.");
            optionsButton.onClick = [this] { showStandaloneOptions(); };
            window->Component::addAndMakeVisible (optionsButton);
            optionsButton.setBounds (original->getBounds());
        }

        if (auto* parent = getParentComponent())
            renameNotificationButton (*parent);
       #endif
    }

private:
    // Preset que carga el desplegable de motor (y el de modelo del Chip): el sonido más conocido de cada aparato.
    // Fuentes en el comentario de cada preset de fábrica. Synth (0): sin preset, solo cambia de motor.
    static juce::String representativePreset (int engineIndex, int chipModel)
    {
        if (engineIndex == 7)
        {
            static const char* chip[] = { "NES Square Lead", "NES PAL Square Lead", "Famicom Poly", "Game Boy Square", "Game Boy Color Lead",
                                          "NES PAL Unadapted Lead" };
            return chip[juce::jlimit (0, 5, chipModel)];
        }
        static const char* names[] = { "", "Etheric Theremin", "VL-1 Piano", "Stylophone", "Omnichord Strum", "808 Kit",
                                       "FM E.Piano", "", "Polivoks Scream", "ANS Cluster", "MD Green Hill Bass", "SNES Echo Strings", "N64 Big Room Choir",
                                       "OPL2 E.Piano", "MT-32 Piano 1" };
        return juce::isPositiveAndBelow (engineIndex, (int) std::size (names)) ? names[engineIndex] : "";
    }

    // chipModel < 0: elegido el motor Chip desde el selector de motor -> el modelo que ya tenía
    void loadRepresentative (int engineIndex, int chipModel)
    {
        if (engineIndex == 7 && chipModel < 0)
            chipModel = juce::roundToInt (koni.apvts.getParameter ("chipModel")->convertFrom0to1 (koni.apvts.getParameter ("chipModel")->getValue()));
        const auto index = koni.presetManager.factoryIndex (representativePreset (engineIndex, chipModel));
        if (index >= 0)
            koni.presetManager.load (index);
    }

    // Control por agente (Agent/AgentServer.h): opción global, desactivada por defecto. En el standalone, en Options; en
    // cualquier formato, clic derecho en el título
    // Aleatoriedad (Dsp/RandomClock.h): reproducible (por defecto) o libre, y nueva semilla
    void addRandomItems (juce::PopupMenu& m)
    {
        const auto locked = koni.isRandomLocked();
        m.addItem ("Reproducible randomness (bounce = what you heard)", true, locked, [safe = SafePointer<KoniSynthEditor> (this), locked]
        {
            if (auto* ed = safe.getComponent()) ed->koni.setRandomLocked (! locked);
        });
        m.addItem ("New random seed (detune, drift, noise...)", locked, false, [safe = SafePointer<KoniSynthEditor> (this)]
        {
            if (auto* ed = safe.getComponent()) ed->koni.newRandomSeed();
        });
    }

    // Indicador de microafinación en la barra superior: "μ" ámbar, solo visible con una escala Scala o un maestro MTS-ESP
    // activos (el proyecto la guarda: que no pase inadvertida). Clic: menú Tuning
    struct TuningLed : juce::Component, juce::SettableTooltipClient
    {
        std::function<void()> onClick;
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Skin::c (0xff2a1d08));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Skin::c (0xffffb000));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 ("\xce\xbc"), r, juce::Justification::centred); // μ
        }
    };
    TuningLed tuningLed;

    void refreshTuningLed()
    {
        auto& t = koni.tuning;
        const auto mts = t.mtsHasMaster(), on = mts || t.hasScala();
        const auto tip = on ? "Microtonal tuning: " + (mts ? "MTS-ESP master (" + t.mtsScaleName() + ")" : "Scala " + t.getScaleName())
                                  + ". Saved with the project, not with presets. Click: tuning menu."
                            : juce::String();
        if (tuningLed.isVisible() != on)
            tuningLed.setVisible (on);
        if (tip != tuningLed.getTooltip())
            tuningLed.setTooltip (tip);
    }

    // Microafinación (Dsp/Tuning.h): Scala y MTS-ESP
    void addTuningItems (juce::PopupMenu& m)
    {
        const auto& tuning = koni.tuning;
        m.addSubMenu ("Tuning" + juce::String (tuning.mtsHasMaster() || tuning.hasScala() ? " (microtonal)" : ""), tuningMenu());
    }

    juce::PopupMenu tuningMenu()
    {
        juce::PopupMenu t;
        auto& tuning = koni.tuning;
        const auto status = tuning.mtsHasMaster() ? "MTS-ESP: " + tuning.mtsScaleName()
                          : tuning.hasScala() ? "Scala: " + tuning.getScaleName() : juce::String ("12-TET (standard)");
        t.addItem (status, false, false, [] {});
        t.addSeparator();
        t.addItem ("Load Scala scale (.scl)...", [this] { chooseTuning (false); });
        t.addItem ("Load keyboard mapping (.kbm)...", [this] { chooseTuning (true); });
        t.addItem ("Reset to 12-TET", tuning.hasScala(), false, [safe = SafePointer<KoniSynthEditor> (this)] { if (auto* ed = safe.getComponent()) ed->koni.resetTuning(); });
        t.addSeparator();
        t.addItem ("Follow MTS-ESP master when present", true, tuning.isMtsEnabled(), [safe = SafePointer<KoniSynthEditor> (this)]
        {
            if (auto* ed = safe.getComponent()) ed->koni.setMtsEnabled (! ed->koni.tuning.isMtsEnabled());
        });
        return t;
    }

    // ROM del MT-32: carpeta (global para todas las instancias), abrirla, volver a buscar y el estado de la última búsqueda
    void mt32RomMenu()
    {
        juce::PopupMenu m;
        const auto status = koni.getMt32Status();
        m.addSectionHeader (status.isEmpty() ? juce::String ("Not scanned yet") : status);
        m.addItem ("Choose ROM folder...", [this]
        {
            fileChooser = std::make_unique<juce::FileChooser> ("MT-32 / CM-32L ROM folder", mt32RomFolder());
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [safe = SafePointer<KoniSynthEditor> (this)] (const juce::FileChooser& fc)
                {
                    if (safe != nullptr && fc.getResult() != juce::File())
                        safe->koni.setMt32Folder (fc.getResult());
                });
        });
        m.addItem ("Open ROM folder", [] { const auto f = mt32RomFolder(); f.createDirectory(); f.startAsProcess(); });
        m.addItem ("Rescan", [this] { koni.rebuildMt32(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mt32Section));
    }

    // Skin (UI/Skin.h): ajuste global; se aplica al momento en esta ventana y en las flotantes (las demás instancias, al repintar)
    void addSkinItems (juce::PopupMenu& m)
    {
        juce::PopupMenu skins;
        for (int id = 0; id < Skin::count; ++id)
            skins.addItem (Skin::name (id), true, Skin::current() == id, [this, id] { setSkin (id); });
        m.addSubMenu ("Skin", skins);
    }

    void setSkin (int id)
    {
        Skin::set (id);
        // El display toma el tono del skin (fósforo verde / ámbar; normal en Default y Brushed Metal); luego se puede cambiar
        // a mano en su menú como siempre (sus colores no pasan por el skin)
        scope.setStyle (id == Skin::green ? 1 : id == Skin::amber ? 2 : 0);
        lookAndFeel.applySkin();
        sendLookAndFeelChange();
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* c = desktop.getComponent (i))
            {
                if (auto* w = dynamic_cast<juce::DocumentWindow*> (c)) // flotantes: su fondo y barra de título se fijan al crearlas
                    if (w != getTopLevelComponent())
                        w->setBackgroundColour (Skin::c (0xff1b1f24));
                c->sendLookAndFeelChange();
                c->repaint();
            }
    }

    void chooseTuning (bool mapping)
    {
        fileChooser = std::make_unique<juce::FileChooser> (mapping ? "Load Scala keyboard mapping" : "Load Scala scale",
                                                           juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), mapping ? "*.kbm" : "*.scl");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                if (const auto error = safe->koni.loadTuningFile (fc.getResult()); error.isNotEmpty())
                    safe->overlay = Overlay::create (*safe, "Tuning", error, std::nullopt, "OK", {},
                                                     [safe] (bool, const juce::String&, int) { if (safe != nullptr) safe->overlay.reset(); });
            });
    }

    // Menú principal (clic derecho en el título y Options del standalone, bajo sus entradas de ventana): configuración de la
    // entrada (MIDI, afinación), comportamiento (aleatoriedad), aspecto (skin), integración (agente) y About
    void addMainItems (juce::PopupMenu& m)
    {
        addMidiItems (m);
        addTuningItems (m);
        juce::PopupMenu random;
        addRandomItems (random);
        m.addSubMenu ("Randomness", random);
        addSkinItems (m);
        m.addSeparator();
        addAgentItem (m);
        m.addSeparator();
        m.addItem ("About KoniSynth...", [safe = SafePointer<KoniSynthEditor> (this)]
        {
            if (auto* ed = safe.getComponent())
                ed->openPanel ("About", std::make_unique<AboutBody> (ed->koni.easterEgg), 640, 340);
        });
    }

    // Agente: diálogo con el interruptor, el estado y la preparación de la conexión (UI/AgentPanel.h)
    void addAgentItem (juce::PopupMenu& m)
    {
        const auto on = koni.isAgentEnabled();
        m.addItem (on ? "Agent (MCP): on, port " + juce::String (koni.agentPort()) + "..." : juce::String ("Agent (MCP)..."), true, on,
                   [safe = SafePointer<KoniSynthEditor> (this)]
        {
            if (auto* ed = safe.getComponent())
                ed->openPanel ("Agent (MCP)", std::make_unique<AgentPanel> (ed->koni), 920, 360);
        });
    }

   #if JucePlugin_Build_Standalone
    void showStandaloneOptions()
    {
        juce::PopupMenu m;
        juce::Component::SafePointer<juce::StandaloneFilterWindow> w (dynamic_cast<juce::StandaloneFilterWindow*> (hookedWindow.getComponent()));
        m.addItem ("Audio/MIDI config...", [w] { if (w != nullptr) w->pluginHolder->showAudioSettingsDialog(); });
        m.addSeparator();
        m.addItem ("Save current state...", [w] { if (w != nullptr) w->pluginHolder->askUserToSaveState(); });
        m.addItem ("Load a saved state...", [w] { if (w != nullptr) w->pluginHolder->askUserToLoadState(); });
        m.addSeparator();
        m.addItem ("Reset to default state", [w] { if (w != nullptr) w->resetToDefaultState(); });
        m.addSeparator();
        addMainItems (m);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton));
    }

    void renameNotificationButton (juce::Component& root)
    {
        for (auto* c : root.getChildren())
        {
            if (c == this)
                continue;
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Settings...")
            {
                b->setButtonText ("Audio/MIDI config");
                b->setTooltip ("Open the audio and MIDI device configuration (input is muted until you enable it there).");
                hookedNotification = b->getParentComponent();
                hookedNotification->addComponentListener (this);
                widenNotificationButton();
                return;
            }
            renameNotificationButton (*c);
        }
    }

    // El aviso reserva 70 px al botón (NotificationArea::resized): se ensancha a 130 tras cada maquetación
    void widenNotificationButton()
    {
        if (auto* area = hookedNotification.getComponent())
            for (auto* c : area->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c))
                    b->setBounds (b->getBounds().withLeft (b->getRight() - 130));
    }

    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        if (wasResized)
            widenNotificationButton();
    }
   #endif

public:

    void paint (juce::Graphics& g) override
    {
        Skin::paintBackground (g, getLocalBounds(), juce::LookAndFeel_V4::getDarkColourScheme().getUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground));
    }

    void resized() override
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        const auto scale = (float) getWidth() / (float) kWidth;
        content.setTransform (juce::AffineTransform::scale (scale));
        const auto logicalHeight = juce::roundToInt ((float) getHeight() / scale);
        if (content.getHeight() != logicalHeight)
        {
            content.setSize (kWidth, logicalHeight);
            if (panel != nullptr)
                panel->setBounds (content.getLocalBounds());
            layoutContent();
        }
    }

private:
    // Motor: Synth (generación + forma), Theremin (pad en lugar del teclado) o Casio VL-1
    void updateMode()
    {
        const auto thereminMode = engine == 1, vl1Mode = engine == 2, synthMode = engine == 0;
        pad.setVisible (thereminMode && padMode() == 0);
        thereminSection.setVisible (thereminMode);
        vl1Section.setVisible (vl1Mode);
        vl1RhythmSection.setVisible (vl1Mode);
        vl1DisplaySection.setVisible (vl1Mode);
        styloSection.setVisible (engine == 3);
        const auto genx = engine == 3 && (int) koni.apvts.getRawParameterValue ("styloModel")->load() == 1;
        genxOscSection.setVisible (genx);
        genxFxSection.setVisible (genx);
        shownStyloModel = (int) koni.apvts.getRawParameterValue ("styloModel")->load();
        omniSection.setVisible (engine == 4);
        omniRhythmSection.setVisible (engine == 4);
        drumSection.setVisible (engine == 5);
        drumPatternSection.setVisible (engine == 5);
        fmSection.setVisible (engine == 6);
        fmLfoSection.setVisible (engine == 6);
        fmDisplaySection.setVisible (engine == 6);
        chipSection.setVisible (engine == 7);
        chipDriverSection.setVisible (engine == 7);
        chipStereoSection.setVisible (engine == 7);
        chipVrc6Section.setVisible (engine == 7);
        for (auto* c : { &pvOscSection, &pvFilterSection, &pvFEnvSection, &pvAEnvSection, &pvLfoSection })
            c->setVisible (engine == 8);
        ansSection.setVisible (engine == 9);
        mdSection.setVisible (engine == 10);
        mdLfoSection.setVisible (engine == 10);
        mdVoiceSection.setVisible (engine == 10);
        mdPsgSection.setVisible (engine == 10);
        for (auto* c : { &snesSection, &snesEnvSection, &snesEchoSection })
            c->setVisible (engine == 11);
        for (auto* c : { &n64Section, &n64EnvSection, &n64FxSection })
            c->setVisible (engine == 12);
        const auto opllModel = (int) koni.apvts.getRawParameterValue ("oplModel")->load() >= 2;
        oplSection.setVisible (engine == 13);
        oplLfoSection.setVisible (engine == 13);
        opllSection.setVisible (engine == 13 && opllModel);
        shownOplModel = (int) koni.apvts.getRawParameterValue ("oplModel")->load();
        mt32Section.setVisible (engine == 14);
        mt32DisplaySection.setVisible (engine == 14);
        keyboard.setVisible (keyboardMode() == 0); // el piano en todos los motores (el Theremin y el Stylophone lo doblan a su rango)
        styloPad.setVisible (engine == 3 && padMode() == 0);
        padTab->setVisible (engine == 1 || engine == 3);
        syncFloatingPad();
        for (auto* c : std::initializer_list<juce::Component*> { &signalStrip, &waveSection, sourceTabs.get(), &filter, &complexSection, mixerTabs.get(), &filterEnvSection, &ampEnvSection,
                                                                 &lfo2Section, &controllerSection })
            c->setVisible (synthMode); // layoutContent las vuelve a ocultar si su grupo está plegado
        lfo.setVisible (synthMode || thereminMode); // LFO 1 solo actúa en el Synth y el Theremin (filas de ENGINE)
        layoutContent();
    }

    // Paneles de edición: componentes hijos de 'content' (misma escala que el resto); uno a la vez
    void openPanel (const juce::String& panelTitle, std::unique_ptr<juce::Component> body, int w, int h)
    {
        // Pedido desde una ventana flotante (botón Edit... de un bloque desacoplado): se abre en esa ventana, que crece
        // para alojarlo y recupera su tamaño al cerrarlo
        if (auto* win = floatWindowUnderMouse())
        {
            win->showPanel (std::make_unique<PanelDialog> (panelTitle, std::move (body), w, h, [win] { win->closePanel(); }), w, h);
            return;
        }
        panel = std::make_unique<PanelDialog> (panelTitle, std::move (body), w, h, [this] { panel.reset(); });
        content.addAndMakeVisible (*panel);
        panel->setBounds (content.getLocalBounds());
        panel->grabKeyboardFocus();
    }

    void openSamplePanel (int which)
    {
        const auto ids = SampleIds::of (which);
        openPanel (ids.title, std::make_unique<SamplePanel> (koni, ids, [this, which] { chooseSample (which); }), 1100, 560);
    }
    void openWavetablePanel (int which = KoniSynthProcessor::wtMain)
    {
        openPanel (which == KoniSynthProcessor::wtOsc2 ? "Osc 2 Wavetable" : "Wavetable",
                   std::make_unique<WavetablePanel> (koni, which, [this, which] { chooseWavetable (which); }), 1100, 620);
    }
    void openSequencerPanel()  { openPanel ("Step Sequencer", std::make_unique<SequencerPanel> (koni), 1300, 560); }
    void openPerfMacroPanel()  { openPanel ("Macros", std::make_unique<PerfMacroPanel> (koni), 1100, 470); }
    // Diálogo "Edit..." de una sección: una fila de secciones; ~78 px por unidad de peso, alto de una fila del panel
    void openParamPanel (const juce::String& panelTitle, std::function<void (ParamPanel&, APVTS&)> build)
    {
        auto body = std::make_unique<ParamPanel>();
        build (*body, koni.apvts);
        paramPanel = body.get(); // updateInert atenúa también sus mandos
        const auto w = juce::jlimit (420, kWidth - 40, juce::roundToInt (body->weight() * 78.0f) + 52);
        openPanel (panelTitle, std::move (body), w, 230);
    }
    // Teclas 1-9 del PC: el favorito n (0-8) en el orden del usuario (se reordena en el navegador de presets)
    void loadFavorite (int n)
    {
        auto& pm = koni.presetManager;
        const auto favs = pm.favoriteIndices(); // en el orden del usuario (se reordena en el navegador)
        if (n >= (int) favs.size())
        {
            scope.showCaption ("Favorites", "No favorite " + juce::String (n + 1) + ": mark presets with the star in the browser");
            return;
        }
        pm.load ((int) favs[(size_t) n]); // el display la anuncia (PresetBar::onPresetChanged)
    }

    // Indicación de cómo tocar el preset actual (clic derecho en el teclado): se guarda con el preset (Save / Save As)
    void editPlayHint()
    {
        overlay = Overlay::create (*this, "Play hint", "How to play this preset. The display shows it when the preset loads, and the "
                                   "preset browser on hover. Save the preset to keep it.",
                                   koni.apvts.state.getProperty (PresetManager::playHintProp).toString(), "OK", "Cancel",
                                   [this] (bool accepted, const juce::String& text, int)
                                   {
                                       if (accepted)
                                           koni.apvts.state.setProperty (PresetManager::playHintProp, text.trim(), nullptr);
                                       juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<KoniSynthEditor> (this)]
                                                                        { if (safe != nullptr) safe->overlay.reset(); });
                                   });
    }

    // Modo del teclado en pantalla (propiedad del estado del plugin): 0 Docked, 1 Hidden, 2 Floating
    int keyboardMode() const { return juce::jlimit (0, 2, (int) koni.apvts.state.getProperty ("keyboardMode", 0)); }

    void setKeyboardMode (int mode)
    {
        koni.apvts.state.setProperty ("keyboardMode", mode, nullptr);
        if (mode == 2 && floatingKeyboard == nullptr)
        {
            floatingKeyboard = std::make_unique<KeyboardWindow> (koni.keyboardState, koni.apvts, lookAndFeel,
                                                                 [safe = juce::Component::SafePointer<KoniSynthEditor> (this)]
            {
                if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setKeyboardMode (0); }); // al cerrar: vuelve abajo
            });
            floatingKeyboard->keys.scaleInfo = [this] { return ScaleBar::info (koni.apvts); };
            floatingKeyboard->bar.onChanged = [this]
            {
                keyboard.repaint();
                if (floatingKeyboard != nullptr) floatingKeyboard->keys.repaint();
            };
            floatingKeyboard->keys.attachPlayRange (koni.apvts.state, [this] { editPlayHint(); });
            floatingKeyboard->keys.onFavorite = [this] (int n) { loadFavorite (n); };
            floatingKeyboard->keys.onPcKeysChanged = [this] (const juce::String& line) { scope.showCaption ("PC keyboard", line); };
        }
        else if (mode != 2)
            floatingKeyboard.reset();
        updateMode();
    }

    // Modo de la superficie del motor (propiedad del estado del plugin): 0 Docked, 1 Hidden, 2 Floating
    int padMode() const { return juce::jlimit (0, 2, (int) koni.apvts.state.getProperty ("padMode", 0)); }

    void setPadMode (int mode)
    {
        koni.apvts.state.setProperty ("padMode", mode, nullptr);
        updateMode();
    }

    // Ventana flotante del pad: se crea con la superficie del motor actual y se cierra si el motor no tiene, o al cambiar
    void syncFloatingPad()
    {
        const auto wanted = padMode() == 2 && (engine == 1 || engine == 3) ? engine : -1;
        if (wanted == floatingPadEngine)
            return;
        floatingPad.reset();
        floatingPadEngine = wanted;
        if (wanted < 0)
            return;
        std::unique_ptr<juce::Component> body;
        if (wanted == 1) body = std::make_unique<ThereminPad> (koni.apvts);
        else             body = std::make_unique<StylophonePad> (koni);
        floatingPad = std::make_unique<SurfaceWindow> (wanted == 1 ? "KoniSynth Theremin" : "KoniSynth Stylophone", std::move (body),
                                                       wanted == 1 ? 900 : 1000, wanted == 1 ? 300 : 160,
                                                       [safe = juce::Component::SafePointer<KoniSynthEditor> (this)]
                                                       {
                                                           if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setPadMode (0); });
                                                       });
    }

    void openControllerPanel (const ControllerProfile& profile) { openPanel ("Controller Setup", std::make_unique<ControllerSetupPanel> (koni, profile), 720, 520); }

    // MIDI global (todos los motores): perfiles del controlador y canal, afinación fina y rango del bend. En el menú del
    // título y en Options, junto a Tuning (configuración de la entrada, no del sonido)
    void addMidiItems (juce::PopupMenu& m) { m.addSubMenu ("MIDI", midiMenu()); }

    juce::PopupMenu midiMenu()
    {
        juce::PopupMenu midi;
        midi.addItem ("MIDI channel, fine tune and bend range...", [this]
        {
            openParamPanel ("MIDI", [] (ParamPanel& p, APVTS& st)
            {
                p.add ("MIDI Input").withKnob (st, "midiInChannel", "MIDI Ch").withKnob (st, "fineTune", "Fine").withKnob (st, "bendRange", "Bend");
            });
        });
        midi.addSeparator();
        midi.addItem ("Controller: M-Audio Oxygen Pro Mini...", [this] { openControllerPanel (oxygenProMini()); });
        midi.addItem ("Controller: Casio CT-S400...", [this] { openControllerPanel (casioCtS400()); });
        midi.addItem ("Controller: Yamaha PSR-EW425...", [this] { openControllerPanel (yamahaPsrEw425()); });
        midi.addItem ("Controller: Yamaha reface YC...", [this] { openControllerPanel (yamahaRefaceYc()); });
        return midi;
    }
    void openFmPanel()         { openPanel ("FM 6-Op", std::make_unique<FmPanel> (koni), 1480, 880); }
    void openMdPanel()         { openPanel ("Mega Drive", std::make_unique<MdPanel> (koni), 1180, 820); }
    void openOplPanel()        { openPanel ("OPL", std::make_unique<OplPanel> (koni), 1180, 820); }
    void openAnsPanel()        { openPanel ("ANS", std::make_unique<AnsPanel> (koni), 1480, 900); }
    void openMdMacros()
    {
        openPanel ("Mega Drive Frame Macros", std::make_unique<MacroPanel> (koni, KoniSynthProcessor::mdMacrosProp,
                   std::array<juce::String, MacroSet::numKinds> { "Volume", "Arpeggio", "Pitch", "PSG Noise" },
                   "59.92 Hz, the sound driver's video-frame tick; FM pitch moves the F-number, PSG pitch the period N"), 1100, 640);
    }

    void openSnesMacros()
    {
        openPanel ("SNES Frame Macros (experimental)", std::make_unique<MacroPanel> (koni, KoniSynthProcessor::snesMacrosProp,
                   std::array<juce::String, MacroSet::numKinds> { "Volume", "Arpeggio", "Pitch", "Noise (NON)" },
                   "60.10 Hz; pitch moves the S-DSP pitch register 8 units per step, noise 1 = that voice plays the noise generator"), 1100, 640);
    }

    void openChipMacros()
    {
        openPanel ("8-Bit Frame Macros", std::make_unique<MacroPanel> (koni, KoniSynthProcessor::chipMacrosProp,
                   std::array<juce::String, MacroSet::numKinds> { "Volume", "Arpeggio", "Pitch", "Duty / Noise" },
                   "60.10 Hz on NES NTSC and Famicom, 50.01 Hz on PAL, 59.73 Hz on Game Boy"), 1100, 640);
    }

    void chooseWavetable (int which = KoniSynthProcessor::wtMain)
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load wavetable", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this), which] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                if (const auto error = safe->koni.loadWavetable (fc.getResult(), which); error.isNotEmpty())
                    safe->overlay = Overlay::create (*safe, "Wavetable", error, std::nullopt, "OK", {},
                                                     [safe] (bool, const juce::String&, int) { if (safe != nullptr) safe->overlay.reset(); });
            });
    }

    // which: KoniSynthProcessor::sourceSample (pestaña Sample) u osc2Sample (Osc 2 en modo Sample)
    void chooseSample (int which)
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load sample, SoundFont (.sf2), SNES BRR or SPC, NES DMC", juce::File(), SampleImport::fileFilter);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this), which] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                if (SampleImport::isBank (fc.getResult()))
                    safe->chooseSampleItem (fc.getResult(), which);
                else
                    safe->loadSampleItem (fc.getResult(), {}, which);
            });
    }

    void loadSampleItem (const juce::File& file, const juce::String& item, int which)
    {
        const auto ids = SampleIds::of (which);
        if (const auto error = koni.loadSample (file, item, which); error.isNotEmpty())
        {
            overlay = Overlay::create (*this, ids.title, error, std::nullopt, "OK", {}, [this] (bool, const juce::String&, int) { overlay.reset(); });
        }
        else
        {
            // Nota raíz y bucle del formato (SoundFont, BRR): pasan a Root y Loop de su panel, que siguen siendo editables
            const auto [root, loops] = koni.getSampleFormatInfo (which);
            const auto set = [this] (const char* id, float value)
            {
                if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (koni.apvts.getParameter (id)))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
                    p->endChangeGesture();
                }
            };
            if (root >= 0) set (ids.root, (float) root);
            if (loops) set (ids.loop, 1.0f);
        }
        refreshSampleInfo();
    }

    // Bancos (.sf2, .spc): menú con su contenido. SoundFont: bancos -> presets -> zona (por defecto, la del Do central)
    void chooseSampleItem (const juce::File& file, int which)
    {
        juce::String error;
        auto items = std::make_shared<juce::StringArray>();
        juce::PopupMenu menu;
        if (file.hasFileExtension ("spc"))
        {
            const auto spc = SpcFile::load (file, error);
            if (spc != nullptr)
            {
                menu.addSectionHeader (spc->game.isNotEmpty() ? spc->game + (spc->song.isNotEmpty() ? " - " + spc->song : juce::String()) : file.getFileName());
                for (const auto& s : spc->samples)
                {
                    items->add ("sample=" + juce::String (s.index));
                    menu.addItem (items->size(), SpcFile::describe (s));
                }
            }
        }
        else
        {
            const auto sf = Sf2File::load (file, error);
            if (sf != nullptr)
            {
                std::map<int, juce::PopupMenu> banks;
                for (const auto& p : sf->presets)
                {
                    const auto key = "preset=" + juce::String (p.bank) + ":" + juce::String (p.program);
                    juce::PopupMenu zones;
                    items->add (key);
                    zones.addItem (items->size(), "Middle C zone: " + sf->describe (p.zones[(size_t) Sf2File::defaultZone (p)]));
                    zones.addSeparator();
                    for (size_t z = 0; z < p.zones.size(); ++z)
                    {
                        items->add (key + ";zone=" + juce::String ((int) z));
                        zones.addItem (items->size(), sf->describe (p.zones[z]));
                    }
                    banks[p.bank].addSubMenu (juce::String (p.program).paddedLeft ('0', 3) + "  " + p.name, zones);
                }
                for (auto& [bank, sub] : banks)
                    menu.addSubMenu ("Bank " + juce::String (bank), sub);
            }
        }
        if (error.isNotEmpty())
        {
            overlay = Overlay::create (*this, "Sample", error, std::nullopt, "OK", {}, [this] (bool, const juce::String&, int) { overlay.reset(); });
            return;
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (which == KoniSynthProcessor::osc2Sample ? &osc2Section : &sampleSection),
            [safe = SafePointer<KoniSynthEditor> (this), file, items, which] (int result)
            {
                if (safe != nullptr && result > 0 && result <= items->size())
                    safe->loadSampleItem (file, (*items)[result - 1], which);
            });
    }

    // DX7: voz única o banco de 32 (SysEx) -> parámetros del FM 6-Op
    void chooseDx7Syx()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load DX7 voice or bank (SysEx)", juce::File(), "*.syx");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                auto voices = std::make_shared<std::vector<Dx7Syx::Voice>>();
                if (const auto error = Dx7Syx::loadVoices (fc.getResult(), *voices); error.isNotEmpty())
                {
                    safe->overlay = Overlay::create (*safe, "DX7 SysEx", error, std::nullopt, "OK", {},
                                                     [safe] (bool, const juce::String&, int) { if (safe != nullptr) safe->overlay.reset(); });
                    return;
                }
                if (voices->size() == 1)
                {
                    safe->applyDx7Voice ((*voices)[0], 0); // voz suelta: sin número en el LED
                    return;
                }
                juce::PopupMenu menu;
                menu.addSectionHeader (fc.getResult().getFileName());
                for (size_t v = 0; v < voices->size(); ++v)
                    menu.addItem ((int) v + 1, juce::String ((int) v + 1).paddedLeft ('0', 2) + "  " + (*voices)[v].name);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&safe->fmSection),
                    [safe, voices] (int result)
                    {
                        if (safe != nullptr && result > 0 && result <= (int) voices->size())
                            safe->applyDx7Voice ((*voices)[(size_t) result - 1], result);
                    });
            });
    }

    // number: posición en el banco (1-32) o 0 si era una voz suelta. La pantalla del DX7 la muestra como voz de cartucho
    void applyDx7Voice (const Dx7Syx::Voice& voice, int number)
    {
        Dx7Syx::apply (voice, koni.apvts);
        koni.apvts.state.setProperty ("dx7Voice", juce::String (number) + "|" + voice.name, nullptr); // tras los parámetros
    }

    // Anillos de modulación (UI/MatrixPanel.h), 30 Hz: rango de cada destino según las ranuras activas y punto en vivo de la
    // última voz (oculto si no ha sonado ninguna voz con matriz en 0,2 s). Los mandos se buscan en el árbol una vez por segundo
    struct RingTimer : juce::Timer { std::function<void()> tick; void timerCallback() override { tick(); } };
    RingTimer ringTimer;
    std::vector<juce::Component::SafePointer<Knob>> ringKnobs;
    unsigned lastLiveTick = 0;
    int liveQuietFrames = 100, ringScanCountdown = 0, inertCountdown = 0, lastLearning = -1, shownRoutes = 0;
    int shownQuality = 0, shownQualityPending = 0; // selector Ladder quality (inicial: Classic, sin cambio pendiente)

    // Selector Ladder quality: aspecto de desplegable, pero abre un menú propio (los modos, el aviso de cambio pendiente y
    // Apply now). No es un parámetro: el modo es de la instancia (KoniSynth.cpp, setFilterQuality)
    struct QualitySelector : juce::ComboBox
    {
        std::function<void()> onOpen;
        void showPopup() override { if (onOpen) onOpen(); }
    };
    QualitySelector qualitySelector;

    static juce::String qualityText (int q, bool pending)
    {
        static const char* names[] { "Classic", "Balanced", "High Quality" };
        return juce::String (names[juce::jlimit (0, 2, q)]) + (pending ? " (pending)" : "");
    }

    // Menú del selector: los tres modos y, si el cambio espera a que callen las voces, aplicarlo ya (corta el sonido)
    void showQualityMenu()
    {
        const auto q = koni.getFilterQuality();
        const auto pending = koni.isFilterQualityPending();
        juce::PopupMenu m;
        m.addSectionHeader ("Ladder Quality");
        m.addItem (1, "Classic (original, 1x)", true, q == 0);
        m.addItem (2, "Balanced (2x + antialiased drive)", true, q == 1);
        m.addItem (3, "High Quality (4x)", true, q == 2);
        if (pending)
        {
            m.addSeparator();
            m.addItem (4, "Pending: applies when all voices are silent", false, false);
            m.addItem (5, "Apply now (cuts the sound, like PANIC)");
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&qualitySelector).withMinimumWidth (qualitySelector.getWidth()),
                         [this] (int r)
        {
            qualitySelector.hidePopup(); // el ComboBox marcó su menú como abierto antes de llamar a showPopup
            if (r >= 1 && r <= 3) koni.setFilterQuality (r - 1);
            else if (r == 5) koni.applyFilterQualityNow();
        });
    }

    // Controles que no suenan con los ajustes actuales: atenuados al 35 % (se pueden seguir tocando). Condiciones tomadas
    // del DSP: Dsp/OscillatorCore.h (PW solo en Square), Engines/Synth.h (unísono, Osc 2, sub, ruido, P.Env), Params.h
    // (Rate con Sync), KoniSynth.cpp (Time del eco con Sync), PlateReverb.h (Decay con Freeze), Theremin.h (LFO seno fijo)
    void updateInert()
    {
        const auto v = [this] (const char* id) { return koni.apvts.getRawParameterValue (id)->load(); };
        const auto targeted = [this] (int dst) // alguna ranura de la matriz mueve ese destino
        {
            for (int i = 0; i < ModMatrix::numSlots; ++i)
                if (koni.getParams().mtxSrc[(size_t) i]->load() > 0.5f && (int) koni.getParams().mtxDst[(size_t) i]->load() == dst
                    && std::abs (koni.getParams().mtxAmt[(size_t) i]->load()) > 1.0e-5f)
                    return true;
            return false;
        };
        const auto dim = [] (juce::Component* c, bool inert)
        {
            if (c != nullptr && c->getAlpha() != (inert ? 0.35f : 1.0f)) c->setAlpha (inert ? 0.35f : 1.0f);
        };
        const auto osc1Sounds = v ("osc1Level") > 0.0f || targeted (ModMatrix::dstOsc1Level);
        // Complex con Osc 2 u Osc 3 de modulador: Mod Wave y Ratio no actúan, y ese oscilador cuenta aunque su nivel sea 0
        const auto externalMod = (int) v ("complexModSource") != 0;
        const auto complexOn = v ("complexOn") > 0.5f;
        const auto osc2Sounds = v ("osc2Level") > 0.0f || targeted (ModMatrix::dstOsc2Level) || (complexOn && (int) v ("complexModSource") == 1);
        const auto osc3Sounds = v ("subLevel") > 0.0f || (complexOn && (int) v ("complexModSource") == 2);
        dim (complexSection.knobFor ("modRatio"), externalMod);
        dim (&complexSection.comboComponent(), externalMod);
        const auto osc2Wave = osc2WaveFromChoice ((int) v ("osc2Wave")); // código del motor: 4 Wavetable, 5 Sample
        const auto pwUsed = (waveUsesPw (waveFromChoice ((int) v ("wave"))) && osc1Sounds)
                         || (complexOn && ! externalMod && waveUsesPw (waveFromChoice ((int) v ("modWave"))))
                         || (waveUsesPw (osc2Wave) && osc2Sounds);
        dim (osc.knobFor ("pulseWidth"), ! pwUsed);
        dim (osc.knobFor ("pwmDepth"), ! pwUsed);
        const auto unison = v ("unisonVoices") > 1.5f;
        dim (osc.knobFor ("unisonDetune"), ! unison);
        dim (osc.knobFor ("unisonSpread"), ! unison);
        dim (osc.knobFor ("pitchEnvDecay"), std::abs (v ("pitchEnvAmount")) < 1.0e-4f);
        for (auto* id : { "osc2Octave", "osc2Semi", "osc2Fine" })
            dim (osc2Section.knobFor (id), ! osc2Sounds);
        dim (&osc2Section.comboComponent(), ! osc2Sounds);
        dim (&osc2Section.extraToggleComponent(), ! osc2Sounds);
        for (const auto* id : { "subOctave", "osc3Semi", "osc3Fine" })
            dim (osc3Section.knobFor (id), ! osc3Sounds);
        dim (&osc3Section.comboComponent(), ! osc3Sounds);
        dim (&noiseSection.comboComponent(), v ("noiseLevel") <= 0.0f && ! targeted (ModMatrix::dstNoiseLevel));
        dim (osc2Section.knobFor ("osc2WtPosition"), osc2Wave != 4 || ! osc2Sounds); // solo con la forma Wavetable
        // Forma del triángulo: solo con la forma Triangle (índice 1 en las tres listas); en Osc 1, no con el oscilador complejo
        const auto tri1 = (int) v ("wave") == 1 && osc1Sounds && ! complexOn;
        const auto tri2 = osc2Wave == triangle && osc2Sounds;
        const auto tri3 = (int) v ("osc3Wave") == 1 && osc3Sounds;
        dim (osc.knobFor ("osc1TriSym"), ! tri1);         dim (osc.knobFor ("osc1TriRound"), ! tri1);
        dim (osc2Section.knobFor ("osc2TriSym"), ! tri2); dim (osc2Section.knobFor ("osc2TriRound"), ! tri2);
        dim (osc3Section.knobFor ("osc3TriSym"), ! tri3); dim (osc3Section.knobFor ("osc3TriRound"), ! tri3);
        dim (osc2Section.headerButton (0), osc2Wave != 5); // Osc 2 Sample... (fichero de Osc 2): solo con la forma Sample
        dim (osc2Section.headerButton (1), osc2Wave != 4); // Osc 2 Table... (tabla de Osc 2): solo con la forma Wavetable
        const auto theremin = engine == 1; // el Theremin usa LFO 1 con seno fijo y su propia velocidad (sin Shape ni Sync)
        dim (lfo.knobFor ("lfoRate"), ! theremin && v ("lfoSync") > 0.5f);
        dim (lfo.knobFor ("lfoShape"), theremin);
        dim (lfo.knobFor ("lfoSync"), theremin);
        dim (lfo2Section.knobFor ("lfo2Rate"), v ("lfo2Sync") > 0.5f);
        dim (echo.knobFor ("echoTime"), v ("echoSync") > 0.5f);
        dim (ambient.knobFor ("reverbDecay"), v ("reverbFreeze") > 0.5f);
        dim (filter.knobFor ("filterSpread"), (int) v ("filterMode") < VoiceFilters::dualBp);
        // Ladder quality solo actúa en los modos ladder; es de la instancia y se puede elegir siempre: atenuación más suave
        filter.setHeaderControlAlpha ((int) v ("filterMode") < VoiceFilters::svfLp ? 1.0f : 0.6f);
        const auto mt32Multi = v ("mt32Multi") > 0.5f; // MT-32: con Multi el teclado no se enruta; Program solo en Part 1
        dim (mt32Section.knobFor ("mt32Mode"), mt32Multi);
        dim (mt32Section.listFor ("mt32Program"), mt32Multi || (int) v ("mt32Mode") == 1);
        // Synth.h: sin V.Pan no hay paneo que repartir (Pan Mode está en el diálogo Voice Pan... de Amp Envelope)
        dim (paramPanel != nullptr ? paramPanel->knobFor ("voicePanMode") : nullptr, std::abs (v ("voicePan")) < 1.0e-4f);
    }

    void updateRings()
    {
        if (const auto l = koni.midiLearn.learning.load(); l != lastLearning) { lastLearning = l; panicButton.repaint(); } // marco de MIDI learn
        if (++inertCountdown >= 3) { inertCountdown = 0; updateInert(); } // 10 Hz
        // Selector Ladder quality de Filter: modo pedido y si espera a que callen las voces
        if (const auto q = koni.getFilterQuality(), pending = koni.isFilterQualityPending() ? 1 : 0; q != shownQuality || pending != shownQualityPending)
        {
            shownQuality = q;
            shownQualityPending = pending;
            qualitySelector.setSelectedId (q + 1, juce::dontSendNotification);
            if (pending != 0)
                qualitySelector.setText (qualityText (q, true), juce::dontSendNotification);
        }
        if (--ringScanCountdown <= 0)
        {
            ringScanCountdown = 30;
            ringKnobs.clear();
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* k = dynamic_cast<Knob*> (&c)) ringKnobs.emplace_back (k);
                for (auto* child : c.getChildren()) walk (*child);
            };
            walk (content);
        }
        const auto tick = koni.getParams().liveModTick.load (std::memory_order_relaxed);
        liveQuietFrames = tick != lastLiveTick ? 0 : liveQuietFrames + 1;
        lastLiveTick = tick;
        const auto live = liveQuietFrames < 6; // 0,2 s a 30 Hz
        std::array<float, ModMatrix::numDestinations> lo {}, hi {};
        std::array<bool, ModMatrix::numDestinations> used {};
        int routes = 0;
        for (int i = 0; i < ModMatrix::numSlots; ++i)
        {
            const auto src = (int) koni.getParams().mtxSrc[(size_t) i]->load(), dst = (int) koni.getParams().mtxDst[(size_t) i]->load();
            const auto amt = koni.getParams().mtxAmt[(size_t) i]->load();
            if (src <= 0 || dst <= 0 || std::abs (amt) < 1.0e-5f) continue;
            ++routes;
            const auto a = amt * ModMatrix::destinationRange (dst);
            used[(size_t) dst] = true;
            if (MatrixUi::bipolar (src)) { lo[(size_t) dst] -= std::abs (a); hi[(size_t) dst] += std::abs (a); }
            else { lo[(size_t) dst] += juce::jmin (0.0f, a); hi[(size_t) dst] += juce::jmax (0.0f, a); }
        }
        signalStrip.setRoutes (routes); // la tira de señal las lista entre las modulaciones
        if (routes != shownRoutes) // botón Matrix de LFO 2 con el número de rutas activas (su ancho cambia: se rehace la fila)
        {
            shownRoutes = routes;
            if (auto* b = lfo2Section.headerButton (0))
                b->setButtonText (routes == 0 ? juce::String ("Matrix...") : "Matrix (" + juce::String (routes) + ")...");
            layoutContent();
        }
        for (auto& sp : ringKnobs)
        {
            auto* k = sp.getComponent();
            if (k == nullptr) continue;
            int dst = -1;
            for (int d = 1; d < ModMatrix::numDestinations; ++d)
                if (const auto t = MatrixUi::targetFor (d); t.paramId != nullptr && k->paramId == t.paramId) { dst = d; break; }
            auto* p = dst > 0 ? koni.apvts.getParameter (k->paramId) : nullptr;
            if (p == nullptr || ! used[(size_t) dst]) { k->setRing (false, 0.0f, 0.0f, -1.0f); continue; }
            const auto t = MatrixUi::targetFor (dst);
            const auto a = MatrixUi::shifted (*p, t, lo[(size_t) dst]), b = MatrixUi::shifted (*p, t, hi[(size_t) dst]);
            const auto v = live ? MatrixUi::shifted (*p, t, koni.getParams().liveMod[(size_t) dst].load (std::memory_order_relaxed)) : -1.0f;
            k->setRing (true, juce::jmin (a, b), juce::jmax (a, b), v);
        }
    }

    void timerCallback() override
    {
        refreshSampleInfo();
        updateMcpButton();
        if (engine == 14) // MT-32: por qué no suena (el LCD lo dibuja mt32Lcd) y nombres de programa según la memoria del aparato
        {
            const auto hasMachine = koni.hasMt32Machine();
            mt32Section.setInfo (hasMachine ? juce::String() : juce::String ("no ROMs: see ROMs..."));
            const auto namesVersion = hasMachine ? koni.getMt32ProgramNamesVersion() : 0u;            if (namesVersion != shownMt32Names)
                if (auto* list = mt32Section.listFor ("mt32Program"))
                {
                    shownMt32Names = namesVersion;
                    const auto names = hasMachine ? koni.getMt32ProgramNames() : juce::StringArray();
                    juce::StringArray texts;
                    for (int i = 0; i < 128; ++i)
                        texts.add (names[i].isEmpty() ? juce::String (i + 1) : juce::String (i + 1) + " - " + names[i]);
                    list->setItemTexts (texts);
                    resized(); // el ancho natural de la sección sigue al nombre más largo
                }
        }
        refreshCabIrInfo();
        refreshTuningLed();
        for (int m = 0; m < PerfMacros::numMacros; ++m) // nombres de las macros (editados en su diálogo o al cargar un preset)
            if (auto* k = macroSection.knob ((size_t) m)) k->name = koni.perfMacros.name (m);
        if (engine == 3 && (int) koni.apvts.getRawParameterValue ("styloModel")->load() != shownStyloModel) // el Gen X-2 tiene sus propias secciones
            updateMode();
        if (engine == 13 && (int) koni.apvts.getRawParameterValue ("oplModel")->load() != shownOplModel) // OPLL: su sección
            updateMode();
        if (overlay == nullptr) // un aviso a la vez
        {
            if (const auto m = koni.popMissing())
                showMissing (*m);
            else if (const auto cc = koni.midiLearn.rejectedCc.exchange (-1); cc >= 120)
                showRejectedCc (cc);
        }
    }

    // MIDI Learn recibió un CC 120-127 (mensajes de modo de canal): no se puede asignar
    void showRejectedCc (int cc)
    {
        static const char* names[8] = { "All Sound Off", "Reset All Controllers", "Local Control", "All Notes Off",
                                        "Omni Off", "Omni On", "Mono Mode", "Poly Mode" };
        overlay = Overlay::create (*this, "CC " + juce::String (cc) + " can't be learned",
            "CC " + juce::String (cc) + " is the MIDI channel mode message \"" + names[cc - 120] + "\", not a normal controller. "
            "Assign that knob to a CC between 0 and 119 on your controller (Oxygen Pro: Preset Editor) and try again.\n"
            "Non-zero values of CC 120-127 are ignored, so turning that knob won't cut the sound.",
            std::nullopt, "OK", {}, [this] (bool, const juce::String&, int) { overlay.reset(); });
    }

    // Aviso de fichero no encontrado (o ilegible) al cargar un preset o un proyecto: muestra la ruta esperada y ofrece buscarlo
    void showMissing (const KoniSynthProcessor::MissingFile& m)
    {
        using Kind = KoniSynthProcessor::MissingFile::Kind;
        const auto sampleWhich = KoniSynthProcessor::sampleWhichFor (m.kind);
        const juce::String what = sampleWhich >= 0 ? juce::String (SampleIds::of (sampleWhich).title)
                                : m.kind == Kind::cabIr ? "Cabinet IR" : m.kind == Kind::osc2Wavetable ? "Osc 2 wavetable" : "Wavetable";
        const auto heading = what + (m.unreadable ? " file can't be read" : " file not found");
        const auto message = "\"" + m.file.getFileName() + "\" was expected at:\n" + m.file.getParentDirectory().getFullPathName()
                             + "\nLocate it to load it again, or ignore to continue without it.";
        overlay = Overlay::create (*this, heading, message, std::nullopt, "Locate...", "Ignore",
            [this, m] (bool accepted, const juce::String&, int)
            {
                overlay.reset();
                if (accepted)
                    locateMissing (m);
            });
    }

    void locateMissing (const KoniSynthProcessor::MissingFile& m)
    {
        if (m.kind == KoniSynthProcessor::MissingFile::Kind::cabIr)
        {
            chooseCabIr (m.file.getParentDirectory());
            return;
        }
        const auto isSample = KoniSynthProcessor::sampleWhichFor (m.kind) >= 0;
        const auto which = juce::jmax (0, KoniSynthProcessor::sampleWhichFor (m.kind));
        const auto table = m.kind == KoniSynthProcessor::MissingFile::Kind::osc2Wavetable ? KoniSynthProcessor::wtOsc2 : KoniSynthProcessor::wtMain;
        const auto start = m.file.getParentDirectory().isDirectory() ? m.file.getParentDirectory()
                                                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        fileChooser = std::make_unique<juce::FileChooser> (isSample ? "Locate sample: " + m.file.getFileName()
                                                                    : "Locate wavetable: " + m.file.getFileName(),
                                                           start, isSample ? SampleImport::fileFilter : "*.wav;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this), isSample, which, table] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                const auto error = isSample ? safe->koni.loadSample (fc.getResult(), {}, which) : safe->koni.loadWavetable (fc.getResult(), table);
                if (error.isNotEmpty())
                    safe->overlay = Overlay::create (*safe, isSample ? SampleIds::of (which).title : "Wavetable", error, std::nullopt, "OK", {},
                                                     [safe] (bool, const juce::String&, int) { if (safe != nullptr) safe->overlay.reset(); });
                safe->refreshSampleInfo();
            });
    }

    // IR de pantalla del usuario: la carga y elige "User IR"
    void chooseCabIr (juce::File start = {})
    {
        if (! start.isDirectory())
            start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        fileChooser = std::make_unique<juce::FileChooser> ("Load cabinet impulse response", start, "*.wav;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                if (const auto error = safe->koni.loadCabIr (fc.getResult()); error.isNotEmpty())
                    safe->overlay = Overlay::create (*safe, "Cabinet IR", error, std::nullopt, "OK", {},
                                                     [safe] (bool, const juce::String&, int) { if (safe != nullptr) safe->overlay.reset(); });
                else if (auto* p = safe->koni.apvts.getParameter ("cabType"))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) Amp::cabUserIr));
                    p->endChangeGesture();
                }
                safe->refreshCabIrInfo();
            });
    }

    void refreshCabIrInfo()
    {
        if (auto* cab = cabDialogSection.getComponent()) // solo con el diálogo Amp and Cabinet abierto
        {
            const auto text = koni.getCabIrInfo();
            cab->setInfo (text.isEmpty() ? juce::String ("(no IR)") : text);
        }
    }
    juce::Component::SafePointer<Section> cabDialogSection; // sección Cabinet del diálogo de Amp

    void refreshSampleInfo()
    {
        const auto text = koni.getSampleInfo();
        sampleSection.setInfo (text.isEmpty() ? juce::String ("(no sample)") : text);
    }

    // Maquetación en coordenadas lógicas (kWidth × kHeight). Synth: 5 filas; Theremin y VL-1: 4 (sin generación/forma).
    // Fila: secciones (nullptr = separador entre bloques no relacionados) y cadena de señal: flechas tras las secciones
    // chainStart .. chainStart + chain - 2 (índices de 'sections'), con rótulo opcional bajo la flecha
    struct RowSpec { float weight; std::vector<Section*> sections; int chain; int chainStart = 0; const char* arrowLabel = nullptr; };

    // Filas del grupo Engine según el motor (la de fuentes del Synth pesa 1,35: pestañas + controles)
    std::vector<RowSpec> engineRows()
    {
        switch (engine)
        {
            // Fuentes en dos filas: display de onda + pestañas A (Osc 1 / Wavetable / Sample / String) y, debajo, complejo -> Osc 2 /
            // Osc 3 / Noise. Encima, la tira del recorrido de la señal (0,24: ~40 px). La fila del display, la más alta (1,85):
            // con las perillas de Osc 1 en dos filas, el display queda en ~2,5:1 en vez de ~3,5:1 con 1,5
            case 0:  return { { 0.24f, { &signalStrip }, 0 },
                              { 1.85f, { &waveSection, sourceTabs.get() }, 0 },
                              { 1.35f, { &complexSection, mixerTabs.get() }, 2 },
                              { 1.3f, { &filter, &filterEnvSection, &ampEnvSection }, 0 }, // 1,3: el filtro, con display y 2 filas
                              { 1.0f, { &controllerSection, &lfo, &lfo2Section }, 0 } };      // voz y modulación: solo actúan en el Synth
            case 1:  return { { 1.0f, { &thereminSection, &lfo }, 1 } };                        // el Theremin usa LFO 1 (vibrato / trémolo)
            case 2:  return { { 1.0f, { &vl1Section, &vl1RhythmSection, &vl1DisplaySection }, 1 } };
            case 3:  if ((int) koni.apvts.getRawParameterValue ("styloModel")->load() == 1) // Gen X-2: cadena oscilador -> filtro/delay
                         return { { 1.0f, { &styloSection, &genxOscSection, &genxFxSection }, 3 } }; // una fila: cabe con PAD y KEYS
                     return { { 1.0f, { &styloSection }, 1 } };
            case 4:  return { { 1.0f, { &omniSection, &omniRhythmSection }, 1 } };
            case 5:  return { { 1.0f, { &drumSection, &drumPatternSection }, 1 } };
            case 6:  return { { 1.0f, { &fmSection, &fmLfoSection, &fmDisplaySection }, 1 } };
            case 7:  return { { 1.0f, { &chipSection, &chipDriverSection, &chipVrc6Section, &chipStereoSection }, 1 } };
            case 8:  return { { 1.0f, { &pvOscSection, &pvFilterSection }, 2 }, { 1.0f, { &pvFEnvSection, &pvAEnvSection, &pvLfoSection }, 0 } };
            case 10: return { { 1.0f, { &mdSection, &mdLfoSection }, 1 }, { 1.0f, { &mdVoiceSection, &mdPsgSection }, 0 } };
            case 11: return { { 1.0f, { &snesSection, &snesEnvSection, &snesEchoSection }, 3 } }; // fuente -> envolvente -> eco/salida
            case 12: return { { 1.0f, { &n64Section, &n64EnvSection, &n64FxSection }, 3 } };       // fuente -> envolvente -> efecto
            case 13: if ((int) koni.apvts.getRawParameterValue ("oplModel")->load() >= 2)
                         return { { 1.0f, { &oplSection, &opllSection, &oplLfoSection }, 1 } };
                     return { { 1.0f, { &oplSection, &oplLfoSection }, 1 } };
            case 14: return { { 1.0f, { &mt32Section, &mt32DisplaySection }, 1 } }; // controles y, a la derecha, el LCD
            default: return { { 1.0f, { &ansSection }, 1 } };
        }
    }

    // Bloques plegables bajo ENGINE, cada uno con su pestaña: se colocan de izquierda a derecha y bajan de línea solo si no
    // caben (60 px por unidad de peso de sección, 32 px por pestaña, 8 px entre bloques). Plegado, un bloque es una tira de
    // 26 px con su nombre; una línea con todos sus bloques plegados es una banda de 24 px con los nombres en horizontal.
    // Plegar bloques de líneas distintas puede juntarlos en menos líneas. Los de FX siguen el orden real de la cadena.
    struct Block
    {
        const char* name;
        const char* foldId;
        const char* tip;
        std::vector<Section*> sections;
        bool chain = false;              // flechas entre sus secciones
        const char* arrowLabel = nullptr;
        bool fx = false;                 // eslabón de la cadena de efectos: flecha desde el bloque FX anterior si es contiguo
    };
    std::vector<Block> blocks;

    void setupBlocks()
    {
        blocks = {
            { "COLOR", "foldColor", "Effects - color: crush, formant filter, tube and parametric EQ.", { &crushSection, &formantSection, &tube, &eqSection }, true, nullptr, true },
            { "AMP", "foldAmp", "Effects - tube amp and speaker cabinet (built-in or your impulse response, in Cabinet...).", { &ampSection }, true, nullptr, true },
            { "DYNAMICS", "foldDynamics", "Effects - dynamics: transient shaper and compressor.", { &transSection, &compSection }, true, nullptr, true },
            { "MOD FX", "foldModFx", "Effects - modulation: phaser, flanger and frequency shifter.", { &phaserSection, &flangerSection, &fshiftSection }, true, nullptr, true },
            { "ENSEMBLE", "foldEnsemble", "Effects - ensemble: chorus and Leslie.", { &chorusSection, &leslieSection }, true, nullptr, true },
            { "SPACE", "foldSpace", "Effects - space: tape echo, plate reverb and spring reverb.", { &echo, &ambient, &springSection }, true, nullptr, true },
            { "OUTPUT", "foldOutput", "Effects - output: media (tape, vinyl...), master and limiter.", { &mediaSection, &master, &limiterSection }, true, nullptr, true },
            // Interpretación, entrada y macros: bajo la cadena de efectos, en línea propia (blockLines)
            { "PLAY", "foldPlay", "Performance: arpeggiator and step sequencer (for every engine).", { &arpSection, &seqSection } },
            { "IN", "foldInput", "Audio input and vocoder. The input is the vocoder's modulator (\"mod\"); Monitor sends it dry "
                                 "into the effects chain.", { &inputSection, &vocoderSection }, true, "mod" },
            { "MACROS", "foldMacros", "Performance macros: 8 knobs, each moving several parameters at once.", { &macroSection } } };

        // Plegados de la versión anterior (un grupo PLAY/MOD/IN y otro FX): se reparten entre sus bloques
        auto& st = koni.apvts.state;
        const std::pair<const char*, std::vector<const char*>> legacy[] = {
            { "foldMod", { "foldPlay", "foldLfo", "foldInput" } },
            { "foldFx", { "foldColor", "foldAmp", "foldDynamics", "foldModFx", "foldEnsemble", "foldSpace", "foldOutput" } } };
        for (const auto& [old, ids] : legacy)
            if (st.hasProperty (old))
            {
                const auto v = (bool) st.getProperty (old);
                for (auto* id : ids)
                    if (! st.hasProperty (id)) st.setProperty (id, v, nullptr);
                st.removeProperty (old, nullptr);
            }

        for (size_t b = 0; b < blocks.size(); ++b)
        {
            auto tab = std::make_unique<GroupTab> (blocks[b].name, [] {});
            const auto foldId = blocks[b].foldId;
            if (b < (size_t) KoniSynthProcessor::numFxGroups) // grupo de efectos: su lugar en la cadena se elige
            {
                const auto group = (int) b;
                tab->fxGroup = group;
                tab->onFxDrop = [this] (int from, int to) { dropFx (from, to); };
                attachModeTab (*tab, [this, foldId] { return dockMode (foldId); }, [this, foldId] (int m) { setDockMode (foldId, m); },
                    [this, group] (juce::PopupMenu& m)
                    {
                        const auto order = koni.getFxOrder();
                        const auto at = (int) (std::find (order.begin(), order.end(), group) - order.begin());
                        m.addSeparator();
                        m.addItem (100, "Move earlier in the chain", at > 0);
                        m.addItem (101, "Move later in the chain", at < KoniSynthProcessor::numFxGroups - 1);
                        m.addItem (102, "Reset chain order", order != std::array<int, 6> { 0, 1, 2, 3, 4, 5 });
                    },
                    [this, group] (int id) { if (id == 102) setFxOrder ({ 0, 1, 2, 3, 4, 5 }); else moveFx (group, id == 100 ? -1 : 1); });
                tab->setTooltip (juce::String (blocks[b].tip) + " Click to fold or unfold; right-click: docked, folded or floating window, "
                                 "and its place in the chain. Drag it onto another effects block to change the order of the chain.");
            }
            else
            {
                attachDockTab (*tab, foldId);
                tab->setTooltip (juce::String (blocks[b].tip) + " Click to fold or unfold; right-click: docked, folded or floating window.");
            }
            rows.addAndMakeVisible (*tab);
            blockTabs.push_back (std::move (tab));
        }
        bpmTab = std::make_unique<GroupTab> ("BPM", [] {});
        attachDockTab (*bpmTab, bpmFoldId);
        bpmTab->setTooltip ("Tempo LED. Click to fold or unfold; right-click: docked, folded or floating window.");
        content.addAndMakeVisible (*bpmTab);
    }

    bool blockFolded (size_t b) const { return (bool) koni.apvts.state.getProperty (blocks[b].foldId, false); }
    // Flotante: sus secciones viven en una ventana propia; en el panel cuenta como plegado (tira de 26 px)
    static juce::Identifier floatId (const char* foldId) { return juce::String (foldId) + "Float"; }
    bool blockFloating (size_t b) const { return (bool) koni.apvts.state.getProperty (floatId (blocks[b].foldId), false); }
    bool blockCollapsed (size_t b) const { return blockFolded (b) || blockFloating (b); }

    // Modo de un elemento desacoplable (bloque o LED de BPM): 0 acoplado, 1 plegado, 2 ventana flotante
    void setDockMode (const char* foldId, int mode)
    {
        auto& st = koni.apvts.state;
        st.setProperty (foldId, mode == 1, nullptr);
        st.setProperty (floatId (foldId), mode == 2, nullptr);
        updateMode();
    }

    int dockMode (const char* foldId) const
    {
        const auto& st = koni.apvts.state;
        return (bool) st.getProperty (floatId (foldId), false) ? 2 : (bool) st.getProperty (foldId, false) ? 1 : 0;
    }

    // Comportamiento común de todas las pestañas (DISPLAY, BPM, ENGINE, bloques, PAD, KEYS): clic = plegar / desplegar
    // (flotante: vuelve al panel); clic derecho = Docked / Folded / Floating window. get/set: modo 0/1/2 del elemento
    template <typename Tab> // GroupTab se declara más abajo en la clase
    // extraItems / extraPicked: opciones propias tras las de acoplado (ids desde 100)
    void attachModeTab (Tab& tab, std::function<int()> get, std::function<void (int)> set,
                        std::function<void (juce::PopupMenu&)> extraItems = {}, std::function<void (int)> extraPicked = {})
    {
        tab.onClick = [get, set] { set (get() == 0 ? 1 : 0); };
        tab.onRightClick = [this, get, set, extraItems, extraPicked, t = &tab]
        {
            juce::PopupMenu m;
            const auto mode = get();
            m.addItem (1, "Docked", true, mode == 0);
            m.addItem (2, "Folded", true, mode == 1);
            m.addItem (3, "Floating window", true, mode == 2);
            if (extraItems) extraItems (m);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (t),
                             [safe = SafePointer<KoniSynthEditor> (this), set, extraPicked] (int id)
                             {
                                 if (safe == nullptr || id <= 0) return;
                                 if (id >= 100) { if (extraPicked) extraPicked (id); }
                                 else set (id - 1);
                             });
        };
    }

    // Orden de la cadena de efectos (KoniSynthProcessor::setFxOrder): los bloques COLOR..SPACE se recolocan en ese orden
    void setFxOrder (const std::array<int, 6>& order)
    {
        koni.setFxOrder (order);
        resized();
    }
    // Arrastre de un bloque sobre otro: 'from' pasa al lugar de 'to' y los de en medio se corren un puesto
    void dropFx (int from, int to)
    {
        auto order = koni.getFxOrder();
        std::vector<int> v (order.begin(), order.end());
        const auto src = std::find (v.begin(), v.end(), from) - v.begin(), dst = std::find (v.begin(), v.end(), to) - v.begin();
        if (src == dst || src >= (std::ptrdiff_t) v.size() || dst >= (std::ptrdiff_t) v.size())
            return;
        v.erase (v.begin() + src);
        v.insert (v.begin() + dst, from);
        std::copy (v.begin(), v.end(), order.begin());
        setFxOrder (order);
    }
    void moveFx (int group, int delta)
    {
        auto order = koni.getFxOrder();
        const auto at = (int) (std::find (order.begin(), order.end(), group) - order.begin());
        const auto to = at + delta;
        if (to < 0 || to >= KoniSynthProcessor::numFxGroups)
            return;
        std::swap (order[(size_t) at], order[(size_t) to]);
        setFxOrder (order);
    }
    // Orden en pantalla de los bloques: los grupos de efectos en el de la cadena (bloque g = grupo g), después OUTPUT,
    // PLAY, IN y MACROS
    std::vector<size_t> blockOrder() const
    {
        std::vector<size_t> out;
        for (const auto g : koni.getFxOrder()) out.push_back ((size_t) g);
        for (size_t b = (size_t) KoniSynthProcessor::numFxGroups; b < blocks.size(); ++b) out.push_back (b);
        return out;
    }

    template <typename Tab>
    void attachDockTab (Tab& tab, const char* foldId)
    {
        attachModeTab (tab, [this, foldId] { return dockMode (foldId); }, [this, foldId] (int m) { setDockMode (foldId, m); });
    }

    static constexpr const char* displayFoldId = "foldDisplay";
    static constexpr const char* engineFoldId = "foldEngine";
    bool engineCollapsed() const { return dockMode (engineFoldId) != 0; }

    // Ventana flotante de componentes del editor (secciones de un bloque, LED de BPM): los reparte en horizontal según su
    // peso; redimensionable y siempre encima. Al cerrar, el elemento vuelve al panel. Los componentes no son suyos: al
    // acoplar, el editor los devuelve a 'content' antes de destruir la ventana
    struct FloatHolder : juce::Component
    {
        std::vector<std::vector<std::pair<juce::Component*, float>>> rows; // en cada fila, ancho por peso
        std::vector<float> rowWeights; // alto de cada fila por peso (los de engineRows); vacío = filas del mismo alto
        juce::Component* overlay = nullptr; // diálogo abierto encima (ocupa todo)
        void paint (juce::Graphics& g) override { Skin::paintBackground (g, getLocalBounds(), juce::Colour (0xff22272e)); }
        void resized() override
        {
            if (overlay != nullptr)
                overlay->setBounds (getLocalBounds());
            auto area = getLocalBounds().reduced (6);
            const auto weighted = rowWeights.size() == rows.size();
            float totalWeight = 0.0f;
            for (size_t i = 0; i < rows.size(); ++i) totalWeight += weighted ? rowWeights[i] : 1.0f;
            const auto unitH = (float) area.getHeight() / juce::jmax (0.001f, totalWeight);
            for (size_t row = 0; row < rows.size(); ++row)
            {
                auto& items = rows[row];
                auto r = area.removeFromTop (juce::roundToInt (unitH * (weighted ? rowWeights[row] : 1.0f)));
                float total = 0.0f;
                for (auto& it : items) total += it.second;
                const auto unit = (float) r.getWidth() / juce::jmax (0.001f, total);
                for (auto& it : items)
                    it.first->setBounds (r.removeFromLeft (juce::roundToInt (unit * it.second)).reduced (items.size() > 1 ? 4 : 0, 2));
            }
        }
    };

    struct FloatWindow : PinnableWindow
    {
        FloatWindow (const juce::String& name, std::vector<std::pair<juce::Component*, float>> items, int w, int h,
                     juce::LookAndFeel& laf, std::function<void()> onClose)
            : FloatWindow (name, std::vector<std::vector<std::pair<juce::Component*, float>>> { std::move (items) }, w, h, laf, std::move (onClose)) {}

        FloatWindow (const juce::String& name, std::vector<std::vector<std::pair<juce::Component*, float>>> rows, int w, int h,
                     juce::LookAndFeel& laf, std::function<void()> onClose, std::vector<float> rowWeights = {})
            : PinnableWindow (name, Skin::c (0xff1b1f24), juce::DocumentWindow::closeButton), closed (std::move (onClose))
        {
            auto* holder = new FloatHolder();
            holder->setLookAndFeel (&laf); // fuera del editor no hereda su LookAndFeel
            holder->rows = std::move (rows);
            holder->rowWeights = std::move (rowWeights);
            for (auto& row : holder->rows)
                for (auto& it : row)
                    holder->addAndMakeVisible (it.first);
            holder->setSize (w, h);
            setContentOwned (holder, true);
            setResizable (true, true);
            setResizeLimits (160, 60, 3000, 1200);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        ~FloatWindow() override
        {
            if (auto* c = getContentComponent())
                c->setLookAndFeel (nullptr);
        }
        void closeButtonPressed() override { if (closed) closed(); }

        // Diálogo dentro de la ventana: el contenido crece a su tamaño (+24 px) y vuelve al anterior al cerrar
        void showPanel (std::unique_ptr<juce::Component> d, int w, int h)
        {
            auto* holder = dynamic_cast<FloatHolder*> (getContentComponent());
            if (holder == nullptr)
                return;
            if (dialog == nullptr)
                savedSize = { holder->getWidth(), holder->getHeight() };
            dialog = std::move (d);
            holder->overlay = dialog.get();
            holder->addAndMakeVisible (*dialog);
            setContentComponentSize (juce::jmax (w + 24, savedSize.x), juce::jmax (h + 24, savedSize.y));
            dialog->setBounds (holder->getLocalBounds());
            dialog->grabKeyboardFocus();
        }

        void closePanel()
        {
            if (auto* holder = dynamic_cast<FloatHolder*> (getContentComponent()))
                holder->overlay = nullptr;
            dialog.reset();
            if (savedSize.x > 0)
                setContentComponentSize (savedSize.x, savedSize.y);
        }

        std::function<void()> closed;
        std::unique_ptr<juce::Component> dialog;
        juce::Point<int> savedSize;
    };

    FloatWindow* floatWindowUnderMouse() const
    {
        auto* c = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
        if (c == nullptr)
            return nullptr;
        for (auto& w : blockWindows)
            if (w != nullptr && w->isParentOf (c)) return w.get();
        if (engineWindow != nullptr && engineWindow->isParentOf (c)) return engineWindow.get();
        return bpmWindow != nullptr && bpmWindow->isParentOf (c) ? bpmWindow.get() : nullptr;
    }

    // Crea o cierra las ventanas según el estado (constructor, cambios de modo, estado restaurado)
    void syncFloats()
    {
        blockWindows.resize (blocks.size());
        for (size_t b = 0; b < blocks.size(); ++b)
        {
            const auto want = blockFloating (b);
            if (want && blockWindows[b] == nullptr)
            {
                std::vector<std::pair<juce::Component*, float>> items;
                float units = 0.0f;
                for (auto* sec : blocks[b].sections) { items.push_back ({ sec, sec->layoutWeight() }); units += sec->layoutWeight(); sec->setVisible (true); }
                blockWindows[b] = std::make_unique<FloatWindow> ("KoniSynth " + juce::String (blocks[b].name), std::move (items),
                                                                 juce::roundToInt (units * 72.0f) + 12, 150, lookAndFeel,
                                                                 [this, id = blocks[b].foldId] { closeFloat (id); });
            }
            else if (! want && blockWindows[b] != nullptr)
            {
                for (auto* sec : blocks[b].sections) rows.addChildComponent (sec);
                blockWindows[b].reset();
            }
        }
        // DISPLAY flotante = la ventana del visualizador (también la abre el clic derecho en el display)
        const auto wantDisplay = dockMode (displayFoldId) == 2;
        if (wantDisplay && displayWindow == nullptr)
            displayWindow = std::make_unique<VisualizerWindow> (koni, scope.getMode(), scope.getStyle(), scope.getBezel(), [this] { closeFloat (displayFoldId); });
        else if (! wantDisplay && displayWindow != nullptr)
            displayWindow.reset();

        // ENGINE flotante: sus filas (las del motor actual) en una ventana; si cambian (otro motor, modelo Gen X-2, OPLL), se
        // rehace conservando la posición
        std::vector<Section*> engineSecs;
        const auto rowsNow = engineRows();
        for (const auto& row : rowsNow)
            for (auto* sec : row.sections) if (sec != nullptr) engineSecs.push_back (sec);
        const auto wantEngine = dockMode (engineFoldId) == 2;
        if (engineWindow != nullptr && (! wantEngine || engineSecs != engineWindowSecs))
        {
            engineWindowPos = engineWindow->getPosition();
            for (auto* sec : engineWindowSecs) rows.addChildComponent (sec);
            engineWindow.reset();
            engineWindowSecs.clear();
        }
        if (wantEngine && engineWindow == nullptr)
        {
            std::vector<std::vector<std::pair<juce::Component*, float>>> winRows;
            std::vector<float> rowWeights; // el mismo reparto de alto que en el panel (tira de señal 0,24, display 1,85...)
            float maxUnits = 0.0f, totalWeight = 0.0f;
            for (const auto& row : rowsNow)
            {
                winRows.emplace_back();
                rowWeights.push_back (row.weight);
                totalWeight += row.weight;
                float units = 0.0f;
                for (auto* sec : row.sections)
                    if (sec != nullptr) { winRows.back().push_back ({ sec, sec->layoutWeight() }); units += sec->layoutWeight(); sec->setVisible (true); }
                maxUnits = juce::jmax (maxUnits, units);
            }
            engineWindow = std::make_unique<FloatWindow> ("KoniSynth ENGINE", std::move (winRows), juce::jmin (1600, juce::roundToInt (maxUnits * 60.0f) + 12), // tope: nace como mucho del ancho del editor
                                                          juce::roundToInt (165.0f * totalWeight), lookAndFeel, [this] { closeFloat (engineFoldId); },
                                                          std::move (rowWeights)); // 165 px por unidad de peso: minLineHeight del panel
            if (engineWindowPos.x != 0 || engineWindowPos.y != 0)
                engineWindow->setTopLeftPosition (engineWindowPos);
            engineWindowSecs = engineSecs;
        }

        const auto wantBpm = dockMode (bpmFoldId) == 2;
        if (wantBpm && bpmWindow == nullptr)
        {
            bpmDisplay.setVisible (true);
            bpmWindow = std::make_unique<FloatWindow> ("KoniSynth BPM", std::vector<std::pair<juce::Component*, float>> { { &bpmDisplay, 1.0f } },
                                                       260, 100, lookAndFeel, [this] { closeFloat (bpmFoldId); });
        }
        else if (! wantBpm && bpmWindow != nullptr)
        {
            content.addChildComponent (bpmDisplay);
            bpmWindow.reset();
        }
    }

    // Cierre de una ventana flotante: vuelve acoplado (asíncrono: no destruir la ventana dentro de su propio callback)
    void closeFloat (const char* foldId)
    {
        juce::MessageManager::callAsync ([safe = SafePointer<KoniSynthEditor> (this), foldId] { if (safe != nullptr) safe->setDockMode (foldId, 0); });
    }

    static constexpr const char* bpmFoldId = "foldBpm";

    // Coloca los bloques abiertos en 'area' (incluye la columna de pestañas), una línea de alto unitHeight cada una. Los
    // plegados o flotantes no ocupan sitio aquí: su pestaña va a la barra inferior (layoutStatusBar)
    int chainPosition (size_t block) const
    {
        const auto order = blockOrder();
        return (int) (std::find (order.begin(), order.end(), block) - order.begin());
    }

    void layoutBlocks (juce::Rectangle<int> area, int unitHeight)
    {
        for (size_t b = 0; b < blocks.size(); ++b)
            if (blockCollapsed (b) && ! blockFloating (b))
                for (auto* sec : blocks[b].sections) sec->setVisible (false);

        for (const auto& line : blockLines (area.getWidth()))
        {
            const auto row = area.removeFromTop (unitHeight);
            float units = 0.0f;
            int fixedPx = 0;
            for (size_t i = 0; i < line.size(); ++i)
            {
                fixedPx += tabStrip + 6 + (i > 0 ? blockGap : 0);
                for (auto* sec : blocks[line[i]].sections) units += sec->layoutWeight();
            }
            const auto unit = units > 0.0f ? (float) (row.getWidth() - fixedPx) / units : 0.0f;
            const auto cellsRow = row.reduced (0, 3);
            int x = row.getX();
            for (size_t i = 0; i < line.size(); ++i)
            {
                const auto b = line[i];
                if (i > 0)
                {
                    // Flecha de la cadena de efectos entre bloques FX consecutivos de la cadena (con uno plegado en medio
                    // no se dibuja: el audio pasa por él y la flecha no debe saltárselo)
                    if (blocks[b].fx && blocks[line[i - 1]].fx && chainPosition (line[i - 1]) + 1 == chainPosition (b))
                        rows.arrows.push_back ({ (float) x + blockGap * 0.5f, (float) cellsRow.getY() + 24.0f });
                    x += blockGap;
                }
                reparent (rows, *blockTabs[b]);
                blockTabs[b]->setFolded (false);
                blockTabs[b]->setFloating (false);
                blockTabs[b]->setBounds (juce::Rectangle<int> (x, cellsRow.getY(), tabStrip, cellsRow.getHeight()));
                x += tabStrip + 6;
                const auto& secs = blocks[b].sections;
                for (size_t k = 0; k < secs.size(); ++k)
                {
                    const auto w = juce::roundToInt (unit * secs[k]->layoutWeight());
                    const juce::Rectangle<int> cell (x, cellsRow.getY(), w, cellsRow.getHeight());
                    secs[k]->setVisible (true);
                    secs[k]->setBounds (cell.reduced (7, 0));
                    x += w;
                    if (blocks[b].chain && k + 1 < secs.size())
                    {
                        const juce::Point<float> at ((float) cell.getRight(), (float) cell.getY() + 24.0f);
                        rows.arrows.push_back (at);
                        if (blocks[b].arrowLabel != nullptr)
                            rows.arrowLabels.push_back ({ at, blocks[b].arrowLabel });
                    }
                }
            }
        }
    }

    // Reparto en líneas de los bloques abiertos, en su orden: pestaña + 6 + 60 px por unidad de peso; baja de línea si no cabe
    // y también al pasar de la cadena de efectos a los demás bloques (PLAY, IN, MACROS: siempre en línea aparte, debajo)
    std::vector<std::vector<size_t>> blockLines (int width) const
    {
        std::vector<std::vector<size_t>> lines;
        int used = 0;
        bool prevFx = false;
        for (const auto b : blockOrder())
        {
            if (blockCollapsed (b))
                continue;
            float units = 0.0f;
            for (auto* sec : blocks[b].sections) units += sec->layoutWeight();
            const auto own = tabStrip + 6 + juce::roundToInt (units * 60.0f);
            const auto leavesChain = prevFx && ! blocks[b].fx;
            prevFx = blocks[b].fx;
            if (lines.empty() || leavesChain || used + blockGap + own > width)
            {
                lines.emplace_back();
                used = own;
            }
            else
                used += blockGap + own;
            lines.back().push_back (b);
        }
        return lines;
    }

    // Barra inferior: las pestañas de todo lo plegado o flotante (DISPLAY, BPM, ENGINE, bloques, PAD, KEYS), en horizontal,
    // en ese orden y una detrás de otra; si no caben, siguen en otra línea. Ancho de cada una: su texto + 40 px, mínimo
    // 110. Con el display plegado, PANIC y el LED de BPM (si está acoplado) van al extremo derecho de la primera línea.
    // Devuelve el alto usado (0 si no hay nada)
    int layoutStatusBar (juce::Rectangle<int> area, bool measureOnly)
    {
        struct Chip { GroupTab* tab; bool floating; };
        std::vector<Chip> chips;
        const auto displayFolded = dockMode (displayFoldId) != 0;
        const auto bpmMode = dockMode (bpmFoldId);
        if (displayFolded) chips.push_back ({ displayTab.get(), dockMode (displayFoldId) == 2 });
        if (bpmMode != 0) chips.push_back ({ bpmTab.get(), bpmMode == 2 });
        if (engineCollapsed()) chips.push_back ({ groupTabs[0].get(), dockMode (engineFoldId) == 2 });
        for (const auto b : blockOrder())
            if (blockCollapsed (b)) chips.push_back ({ blockTabs[b].get(), blockFloating (b) });
        if ((engine == 1 || engine == 3) && padMode() != 0) chips.push_back ({ padTab.get(), padMode() == 2 });
        if (keyboardMode() != 0) chips.push_back ({ groupTabs[3].get(), keyboardMode() == 2 });

        const auto rightPx = displayFolded ? transportCompactW + 8 + 70 + 8 + (bpmMode == 0 ? tabStrip + 4 + 110 + 8 : 0) : 0;
        if (chips.empty() && rightPx == 0)
            return 0;
        constexpr int lineH = 24, lineGap = 4, chipGap = 6;
        const juce::Font font (juce::FontOptions (12.0f, juce::Font::bold));
        int lines = 1, x = 0;
        std::vector<juce::Rectangle<int>> places;
        for (auto& c : chips)
        {
            const auto w = juce::jmax (110, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, c.tab->getButtonText())) + 40);
            const auto avail = area.getWidth() - (lines == 1 ? rightPx : 0);
            if (x > 0 && x + w > avail) { ++lines; x = 0; }
            places.push_back ({ area.getX() + x, area.getY() + (lines - 1) * (lineH + lineGap), w, lineH });
            x += w + chipGap;
        }
        const auto height = lines * lineH + (lines - 1) * lineGap;
        if (measureOnly)
            return height;
        for (size_t i = 0; i < chips.size(); ++i)
        {
            reparent (content, *chips[i].tab); // ENGINE y bloques viven en 'rows' mientras están acoplados
            chips[i].tab->setFolded (true);
            chips[i].tab->setFloating (chips[i].floating);
            chips[i].tab->setVisible (true);
            chips[i].tab->setBounds (places[i]);
        }
        if (displayFolded)
        {
            auto right = juce::Rectangle<int> (area.getRight() - rightPx, area.getY(), rightPx, lineH);
            right.removeFromLeft (8);
            if (bpmMode == 0)
            {
                bpmTab->setFolded (false);
                bpmTab->setFloating (false);
                bpmTab->setBounds (right.removeFromLeft (tabStrip));
                right.removeFromLeft (4);
                bpmDisplay.setVisible (true);
                bpmDisplay.setBounds (right.removeFromLeft (110));
                right.removeFromLeft (8);
            }
            transportPanel.setBounds (right.removeFromLeft (transportCompactW));
            right.removeFromLeft (8);
            panicButton.setBounds (right);
        }
        return height;
    }

    static constexpr int tabStrip = 26, blockGap = 8, transportCompactW = 210;

    // Mueve una pestaña al contenedor que corresponde a su estado (solo si cambia: re-emparentar repinta)
    static void reparent (juce::Component& parent, juce::Component& c)
    {
        if (c.getParentComponent() != &parent)
            parent.addAndMakeVisible (c);
    }

    // Grupos plegables (propiedades del estado del plugin): 0 Engine; 3 Keys = modo del teclado
    bool folded (int group) const
    {
        static const char* ids[5] = { "foldEngine", "foldMod", "foldFx", "", "foldMacros" };
        return (bool) koni.apvts.state.getProperty (ids[group], false);
    }

    void toggleGroup (int group)
    {
        if (group == 3) { setKeyboardMode (keyboardMode() == 0 ? 1 : 0); return; }
        static const char* ids[5] = { "foldEngine", "foldMod", "foldFx", "", "foldMacros" };
        koni.apvts.state.setProperty (ids[group], ! folded (group), nullptr);
        updateMode();
    }

    // Maquetación en coordenadas lógicas (kWidth × kHeight): barra superior, osciloscopio, columna de pestañas de grupo a
    // la izquierda y los grupos Engine / Mod / FX / Keys. Un grupo plegado queda en una banda de 24 px; el alto libre se
    // reparte entre las filas abiertas según su peso
    void layoutContent()
    {
        syncFloats();
        content.arrows.clear();
        content.arrowLabels.clear();
        content.dividers.clear();
        rows.arrows.clear();
        rows.arrowLabels.clear();
        rows.dividers.clear();
        auto r = content.getLocalBounds().reduced (10);
        auto top = r.removeFromTop (30);
        title.setBounds (top.removeFromLeft (112));
        version.setBounds (top.removeFromLeft (72).withTrimmedBottom (6));
        avxButton.setBounds (top.removeFromLeft (70));
        mcpButton.setBounds (top.removeFromLeft (64));
        tuningLed.setBounds (top.removeFromLeft (20).withTrimmedLeft (1).withTrimmedRight (3).withSizeKeepingCentre (16, 20)); // hueco de 20 px ante el motor
        // Orden del flujo: entrada MIDI -> motor | edición (deshacer) | presets; 15 px entre grupos
        midiButton.setBounds (top.removeFromLeft (34).withTrimmedRight (4));
        engineCombo.setBounds (top.removeFromLeft (190));
        top.removeFromLeft (15);
        undoButton.setBounds (top.removeFromLeft (34).withTrimmedRight (4));
        redoButton.setBounds (top.removeFromLeft (30));
        top.removeFromLeft (15);
        presetBar.setBounds (top.removeFromLeft (juce::jmin (top.getWidth(), PresetBar::preferredWidth()))); // a la derecha queda espacio libre
        r.removeFromTop (8);
        auto tabColumn = r.removeFromLeft (26);
        r.removeFromLeft (6);
        const auto full = [&tabColumn] (juce::Rectangle<int> band) { return band.withLeft (tabColumn.getX()); }; // ancho con la columna

        // Barra inferior con todo lo plegado o flotante (layoutStatusBar)
        if (const auto barH = layoutStatusBar (full (r), true); barH > 0)
        {
            layoutStatusBar (full (r.removeFromBottom (barH)), false);
            r.removeFromBottom (8);
        }

        const auto displayFolded = dockMode (displayFoldId) != 0; // plegado o en su ventana
        scope.setVisible (! displayFolded);
        if (! displayFolded)
        {
            // DISPLAY: visualizador, PANIC y LED de tempo con su pestaña (BPM: plegarlo o sacarlo a una ventana)
            auto scopeRow = r.removeFromTop (80);
            displayTab->setFolded (false);
            displayTab->setFloating (false);
            displayTab->setBounds (tabColumn.withY (scopeRow.getY()).withHeight (scopeRow.getHeight()));
            // Grupo a la derecha, de izquierda a derecha: BPM (con su pestaña), REC / STOP / PLAY y PANIC (mismo orden que
            // en la barra inferior con el display plegado)
            panicButton.setBounds (scopeRow.removeFromRight (96));
            scopeRow.removeFromRight (8);
            transportPanel.setBounds (scopeRow.removeFromRight (170));
            const auto bpmMode = dockMode (bpmFoldId);
            if (bpmMode == 0)
            {
                scopeRow.removeFromRight (8);
                bpmDisplay.setVisible (true);
                bpmDisplay.setBounds (scopeRow.removeFromRight (190));
                scopeRow.removeFromRight (4);
                bpmTab->setFolded (false);
                bpmTab->setFloating (false);
                bpmTab->setBounds (scopeRow.removeFromRight (tabStrip));
            }
            else if (bpmMode == 1)
                bpmDisplay.setVisible (false);
            scopeRow.removeFromRight (12); // separación algo mayor: el osciloscopio no es parte del grupo
            scope.setBounds (scopeRow);
            r.removeFromTop (8);
        }
        else if (dockMode (bpmFoldId) == 1)
            bpmDisplay.setVisible (false);

        // Keys: teclado acoplado (100 px) con su pie (Transpose y escala, 30 px) debajo; el pie va siempre con el teclado:
        // plegado, se pliega con él; flotante, va en su ventana (KeyboardWindow). Su pestaña, en la barra
        const auto mode = keyboardMode();
        groupTabs[3]->setVisible (true);
        scaleBar.setVisible (mode == 0);
        if (mode == 0)
        {
            scaleBar.setBounds (r.removeFromBottom (30));
            r.removeFromBottom (6);
            auto band = r.removeFromBottom (100);
            groupTabs[3]->setFolded (false);
            groupTabs[3]->setFloating (false);
            groupTabs[3]->setBounds (tabColumn.withY (band.getY()).withHeight (band.getHeight()));
            keyboard.setBounds (band);
            keyboard.setKeyWidth ((float) band.getWidth() / 75.0f); // 128 notas MIDI = 75 teclas blancas: llena el ancho
            r.removeFromBottom (8);
        }
        if ((engine == 1 || engine == 3) && padMode() == 0)
        {
            auto band = r.removeFromBottom (engine == 3 ? 110 : 100);
            padTab->setFolded (false);
            padTab->setFloating (false);
            padTab->setBounds (tabColumn.withY (band.getY()).withHeight (band.getHeight()));
            pad.setBounds (band);
            styloPad.setBounds (band);
            r.removeFromBottom (8);
        }

        // ENGINE (pestaña en la columna) y debajo los bloques abiertos en líneas, dentro del Viewport. Alto por unidad de peso:
        // el libre repartido entre las filas del motor (según su peso) y las líneas de bloques (peso 1), y nunca menos de
        // minLineHeight; si así no caben, scroll vertical (la barra quita su ancho: se recalculan las líneas con él)
        const juce::Rectangle<int> viewArea (tabColumn.getX(), r.getY(), r.getRight() - tabColumn.getX(), r.getHeight());
        rowsView.setBounds (viewArea);
        const auto engineRowsNow = engineRows();
        float weights = 0.0f, unit = 0.0f;
        int rowsWidth = viewArea.getWidth();
        for (int pass = 0; pass < 2; ++pass)
        {
            weights = (float) blockLines (rowsWidth).size();
            if (! engineCollapsed())
                for (const auto& row : engineRowsNow) weights += row.weight;
            unit = weights > 0.0f ? juce::jmax ((float) minLineHeight, (float) viewArea.getHeight() / weights) : 0.0f;
            const auto scrolls = juce::roundToInt (unit * weights) > viewArea.getHeight();
            const auto w = viewArea.getWidth() - (scrolls ? rowsView.getScrollBarThickness() + 4 : 0);
            if (w == rowsWidth) break;
            rowsWidth = w;
        }
        rows.setSize (rowsWidth, juce::jmax (viewArea.getHeight(), juce::roundToInt (unit * weights)));
        // De aquí en adelante, coordenadas de 'rows': la columna de pestañas en x = 0 y las secciones a su derecha
        tabColumn = juce::Rectangle<int> (0, 0, tabColumn.getWidth(), rows.getHeight());
        r = juce::Rectangle<int> (tabColumn.getRight() + 6, 0, rowsWidth - tabColumn.getRight() - 6, rows.getHeight());
        if (engineCollapsed())
        {
            if (dockMode (engineFoldId) == 1) // flotante: sus secciones están en la ventana
                for (const auto& row : engineRowsNow)
                    for (auto* sec : row.sections) if (sec != nullptr) sec->setVisible (false);
        }
        else
        {
            const auto y0 = r.getY();
            for (const auto& row : engineRowsNow)
            {
                for (auto* sec : row.sections) if (sec != nullptr) sec->setVisible (true);
                layoutRow (r.removeFromTop (juce::roundToInt (unit * row.weight)).reduced (0, 3), row);
            }
            reparent (rows, *groupTabs[0]);
            groupTabs[0]->setFolded (false);
            groupTabs[0]->setFloating (false);
            groupTabs[0]->setBounds (tabColumn.withY (y0).withBottom (r.getY()).reduced (0, 3));
        }
        layoutBlocks (juce::Rectangle<int> (tabColumn.getX(), r.getY(), r.getRight() - tabColumn.getX(), r.getHeight()),
                      juce::roundToInt (unit));
        content.repaint();
        rows.repaint();
    }

    // Ancho de cada sección proporcional a su peso (≈ número de controles). Las primeras chainLength secciones forman
    // cadena de señal: flecha en el hueco de 14 px entre ellas.
    // Separador entre bloques: 16 px con una línea de 1 px en el centro
    // Si todas caben con su ancho natural (controles a Section::maxControlUnit como mucho), cada una toma ese ancho y el
    // hueco queda al final de la fila: con pocos controles el marco no se estira hasta el borde
    void layoutRow (juce::Rectangle<int> row, const RowSpec& spec)
    {
        constexpr int gap = 16;
        float total = 0.0f, natural = 0.0f;
        for (auto* s : spec.sections)
            if (s != nullptr) { total += s->layoutWeight(); natural += s->naturalWidth(); }
            else row.removeFromRight (gap); // el ancho de los separadores sale del total antes de repartir
        const auto fitsNatural = natural <= (float) row.getWidth();
        // Si caben a su ancho natural, el sobrante va a las secciones elásticas (display de onda); sin ellas, al final
        int stretchers = 0;
        for (auto* s : spec.sections)
            if (s != nullptr && s->stretchToFill) ++stretchers;
        const auto extra = fitsNatural && stretchers > 0 ? ((float) row.getWidth() - natural) / (float) stretchers : 0.0f;

        const auto unit = (float) row.getWidth() / total;
        for (int index = 0; index < (int) spec.sections.size(); ++index)
        {
            auto* s = spec.sections[(size_t) index];
            if (s == nullptr)
            {
                const auto g = row.removeFromLeft (gap);
                rows.dividers.push_back (juce::Rectangle<float> ((float) g.getCentreX() - 0.5f, (float) g.getY() + 6.0f,
                                                                    1.0f, (float) g.getHeight() - 12.0f));
                continue;
            }
            const auto cell = row.removeFromLeft (juce::roundToInt (fitsNatural ? s->naturalWidth() + (s->stretchToFill ? extra : 0.0f)
                                                                                : unit * s->layoutWeight()));
            s->setBounds (cell.reduced (7, 0));            if (index >= spec.chainStart && index < spec.chainStart + spec.chain - 1)
            {
                const juce::Point<float> at ((float) cell.getRight(), (float) cell.getY() + 24.0f);
                rows.arrows.push_back (at);
                if (spec.arrowLabel != nullptr)
                    rows.arrowLabels.push_back ({ at, spec.arrowLabel });
            }
        }
    }

    KoniLookAndFeel lookAndFeel; // primero: se destruye el último, cuando ningún componente lo usa
    juce::TooltipWindow tooltipWindow { this, 2000 }; // tooltips a los 2 s de reposo del ratón
    KoniSynthProcessor& koni;
    FlowContent content; // contenedor a tamaño lógico; se escala en resized()
    // Filas de parámetros (ENGINE y bloques) con scroll vertical: cada línea tiene un alto mínimo (minLineHeight) y, si no
    // caben todas, se desplazan. Fuera del scroll: barra superior, DISPLAY/BPM, PAD/KEYS y la barra de plegados
    juce::Viewport rowsView;
    FlowContent rows;
    static constexpr int minLineHeight = 165; // px lógicos por línea de peso 1 (antes ~114 con 7 líneas en 1050 px)
    // Título clicable: abre el About (cursor de mano al pasar por encima)
    struct TitleLabel : juce::Label
    {
        std::function<void()> onClick, onRightClick;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! contains (e.getPosition()))
                return;
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
            else if (onClick) onClick();
        }
    };
    TitleLabel title;
    juce::Label version; // KONI_VERSION_LABEL junto al título
    juce::ComboBox engineCombo;
    std::unique_ptr<PickComboAttachment> engineComboAttachment; // declarado tras engineCombo: muere antes
    std::unique_ptr<ParamMenu> engineMenu;
    MidiActionButton<juce::TextButton> panicButton { "PANIC" };
    TransportPanel transportPanel { koni };
    PresetBar::IconButton undoButton { PresetBar::IconButton::Icon::undo, "Undo" }, redoButton { PresetBar::IconButton::Icon::redo, "Redo" },
                          midiButton { PresetBar::IconButton::Icon::midi, "MIDI" };

    void refreshUndoButtons()
    {
        auto& h = koni.undoHistory;
        undoButton.setEnabled (h.canUndo());
        redoButton.setEnabled (h.canRedo());
        undoButton.setTooltip (h.canUndo() ? "Undo: " + h.undoName() + " (Ctrl+Z)" : juce::String ("Nothing to undo (Ctrl+Z)"));
        redoButton.setTooltip (h.canRedo() ? "Redo: " + h.redoName() + " (Ctrl+Y / Ctrl+Shift+Z)" : juce::String ("Nothing to redo (Ctrl+Y)"));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto cmd = k.getModifiers().isCommandDown();
        const auto code = k.getKeyCode();
        if (cmd && (code == 'Z' || code == 'z') && ! k.getModifiers().isShiftDown()) { koni.undoHistory.undo(); return true; }
        if (cmd && ((code == 'Y' || code == 'y') || ((code == 'Z' || code == 'z') && k.getModifiers().isShiftDown())))
        {
            koni.undoHistory.redo();
            return true;
        }
        // 1-9 sin modificadores: favoritos, también sin el foco en el teclado en pantalla (los campos de texto se las quedan)
        if (! k.getModifiers().isAnyModifierKeyDown() && k.getTextCharacter() >= '1' && k.getTextCharacter() <= '9')
        {
            loadFavorite ((int) (k.getTextCharacter() - '1'));
            return true;
        }
        // Z / X / C / V sin el foco en el teclado: se lo da y las aplica
        if (auto* kb = pcKeyboardTarget(); kb != nullptr && NoteKeyboard::isControlKey (k))
        {
            kb->grabKeyboardFocus();
            return kb->keyPressed (k);
        }
        return false;
    }

    // Una tecla de nota del teclado del PC sin el foco en el teclado en pantalla: se lo da y la toca, sin tener que hacer clic
    bool keyStateChanged (bool isKeyDown) override
    {
        if (auto* kb = pcKeyboardTarget(); kb != nullptr && isKeyDown && kb->anyPcNoteKeyDown())
        {
            kb->grabKeyboardFocus();
            return kb->keyStateChanged (isKeyDown);
        }
        return false;
    }
    juce::ToggleButton avxButton { "AVX2" }, mcpButton { "MCP" };

    void updateMcpButton()
    {
        const auto on = koni.isAgentEnabled();
        mcpButton.setToggleState (on, juce::dontSendNotification);
        const auto clients = koni.agentClients();
        mcpButton.setTooltip (on ? "Agent control (MCP): on, localhost:" + juce::String (koni.agentPort()) + ", "
                                       + juce::String (clients) + (clients == 1 ? " agent connected" : " agents connected")
                                       + ". Click to open the Agent dialog (turn it off, set up your agent)."
                                 : juce::String ("Agent control (MCP): off, no port open. Click to open the Agent dialog (turn it on, "
                                                 "set up your agent)."));
    }

    void updateAvxTooltip()
    {
        auto& st = simdStatus();
        juce::String text ("AVX2 acceleration (unison and complex oscillator; the sound is the same). ");
        text << "CPU support: " << (st.cpuHasAvx2.load() ? "yes" : (st.tested.load() ? "no" : "unknown"));
        if (st.tested.load() && st.cpuHasAvx2.load())
            text << ", self-test " << (st.selfTestPassed.load() ? "passed" : "FAILED");
        text << ". Active path: " << (simdActive() ? "AVX2" : "scalar (SSE2)") << ".";
        avxButton.setTooltip (text);
        avxButton.setEnabled (st.cpuHasAvx2.load() || ! st.tested.load());
    }
    // Pestaña lateral vertical de un grupo plegable: nombre girado (o abreviado si está plegada) y triángulo de estado
    // Las de los grupos de efectos (fxGroup >= 0) se arrastran sobre otra para cambiar el orden de la cadena (onFxDrop)
    struct GroupTab : juce::Button, juce::DragAndDropTarget
    {
        GroupTab (const juce::String& name, std::function<void()> click) : juce::Button (name) { onClick = std::move (click); }
        std::function<void()> onRightClick;
        int fxGroup = -1;
        std::function<void (int from, int to)> onFxDrop;
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragged = false;
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
            else juce::Button::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            juce::Button::mouseDrag (e);
            if (fxGroup < 0 || dragged || e.getDistanceFromDragStart() < 8 || e.mods.isPopupMenu())
                return;
            if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            {
                dragged = true;
                c->startDragging ("fxgroup:" + juce::String (fxGroup), this);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! dragged) { juce::Button::mouseUp (e); return; }
            dragged = false; // un arrastre no pliega el bloque
            setState (juce::Button::buttonNormal);
        }
        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            return fxGroup >= 0 && d.description.toString().startsWith ("fxgroup:");
        }
        void itemDragEnter (const SourceDetails&) override { dropTarget = true; repaint(); }
        void itemDragExit (const SourceDetails&) override { dropTarget = false; repaint(); }
        void itemDropped (const SourceDetails& d) override
        {
            dropTarget = false;
            repaint();
            const auto from = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
            if (onFxDrop && from != fxGroup)
                juce::MessageManager::callAsync ([safe = SafePointer<GroupTab> (this), from]
                                                 { if (safe != nullptr && safe->onFxDrop) safe->onFxDrop (from, safe->fxGroup); });
        }
        bool dragged = false, dropTarget = false;
        void setFolded (bool f) { if (f != isFolded) { isFolded = f; repaint(); } }
        void setFloating (bool f) { if (f != isFloating) { isFloating = f; repaint(); } }
        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (Skin::c (isFolded ? 0xff1b1f24 : 0xff2c333b).brighter (highlighted ? 0.12f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Skin::c (0xff42a2c8).withAlpha (isFolded ? 0.5f : 1.0f));
            g.fillRect (r.withWidth (2.0f));
            // Triángulo: abajo = abierto, derecha = plegado (plegada: a la izquierda, con el nombre en horizontal)
            juce::Path tri;
            const auto horizontal = r.getHeight() <= 70.0f;
            const auto c = horizontal ? juce::Point<float> (13.0f, r.getCentreY()) : juce::Point<float> (r.getCentreX() + 1.0f, r.getY() + 10.0f);
            if (isFolded) tri.addTriangle (c.x - 3, c.y - 4, c.x - 3, c.y + 4, c.x + 4, c.y);
            else          tri.addTriangle (c.x - 4, c.y - 3, c.x + 4, c.y - 3, c.x, c.y + 4);
            g.setColour (Skin::white().withAlpha (0.8f));
            g.fillPath (tri);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            if (horizontal)
            {
                g.drawText (getButtonText(), r.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
                if (isFloating) // en ventana propia: dos ventanitas superpuestas a la derecha
                {
                    const auto w = juce::Rectangle<float> (9.0f, 7.0f).withCentre ({ r.getRight() - 12.0f, r.getCentreY() + 1.5f });
                    g.setColour (Skin::c (0xff42a2c8));
                    g.drawRect (w, 1.2f);
                    g.drawRect (w.translated (3.0f, -3.0f), 1.0f);
                }
            }
            else // nombre girado, de abajo arriba
            {
                juce::Graphics::ScopedSaveState save (g);
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
                const auto t = juce::Rectangle<float> (r.getHeight() - 24.0f, r.getWidth()).withCentre (r.getCentre().translated (0.0f, 0.0f));
                g.drawText (getButtonText(), t, juce::Justification::centred);
            }
        }
        void paintOverChildren (juce::Graphics& g) override
        {
            if (! dropTarget)
                return;
            g.setColour (Skin::accent());
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f); // destino del arrastre
        }
        bool isFolded = false, isFloating = false;
    };
    std::array<std::unique_ptr<GroupTab>, 5> groupTabs;
    std::vector<std::unique_ptr<GroupTab>> blockTabs; // una por bloque (setupBlocks)
    std::unique_ptr<GroupTab> bpmTab;
    std::unique_ptr<GroupTab> displayTab;

    // Display en ventana propia (clic derecho en el display): mismo modo y estilo; doble clic = pantalla completa
    void openDisplayWindow()
    {
        if (displayWindow != nullptr) { displayWindow->toFront (true); return; }
        setDockMode (displayFoldId, 2); // mismo estado que "Floating window" en la pestaña DISPLAY
    }
    // Teclado en pantalla que nombra la tecla bajo el ratón ("E6 - MIDI 88"), para encontrar la nota que pide un preset.
    // C4 = MIDI 60, como en los manuales y en las indicaciones de los presets (JUCE rotula por defecto C3 = 60)
    struct NoteKeyboard : juce::MidiKeyboardComponent, juce::TooltipClient, private juce::ValueTree::Listener
    {
        NoteKeyboard (juce::MidiKeyboardState& state, Orientation o) : juce::MidiKeyboardComponent (state, o) { setOctaveForMiddleC (4); }
        juce::String getTooltip() override
        {
            const auto n = getNoteAndVelocityAtPosition (getMouseXYRelative().toFloat()).note;
            return n < 0 ? juce::String() : juce::MidiMessage::getMidiNoteName (n, true, true, 4) + " - MIDI " + juce::String (n);
        }
        // Rango recomendado del preset (propiedades playLow / playHigh del estado): fuera de él, blancas en gris claro y negras
        // aclaradas. Clic derecho: fijar el rango o editar la indicación (onEditHint lo abre el editor)
        void attachPlayRange (juce::ValueTree s, std::function<void()> editHint)
        {
            state = std::move (s);
            onEditHint = std::move (editHint);
            state.addListener (this);
            remapPcKeys();
            repaint();
        }

        // Teclado del PC (solo el de escribir; el MIDI no cambia). Fila A = blancas desde pcKeyBase (propiedad del estado;
        // C4 por defecto), fila Q = las negras que hay entre ellas, como en un piano. Z / X: octava abajo / arriba; C / V:
        // velocidad -/+ 10 (pcKeyVelocity, 1-127); 1-9: favoritos (onFavorite). Clic derecho: "PC keyboard starts at"
        std::function<void (int index)> onFavorite;
        std::function<void (const juce::String& line)> onPcKeysChanged; // rótulo en el display tras Z / X / C / V
        static inline const juce::Identifier pcBaseProp { "pcKeyBase" }, pcVelocityProp { "pcKeyVelocity" };

        int pcBase() const { return juce::jlimit (0, 120, (int) state.getProperty (pcBaseProp, 60)); }
        // Alguna tecla con nota está pulsada (el editor lo usa para dar el foco al teclado sin hacer clic en él)
        bool anyPcNoteKeyDown() const
        {
            return std::any_of (pcKeys.begin(), pcKeys.end(), [] (const juce::KeyPress& k) { return k.isCurrentlyDown(); });
        }
        static bool isControlKey (const juce::KeyPress& k)
        {
            const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
            return ! k.getModifiers().isAnyModifierKeyDown() && (c == 'z' || c == 'x' || c == 'c' || c == 'v');
        }
        // 127 por defecto: JUCE usa la misma velocidad como tope de la del ratón, que así queda como antes
        int pcVelocity() const { return juce::jlimit (1, 127, (int) state.getProperty (pcVelocityProp, 127)); }
        juce::String pcSummary() const
        {
            return "A = " + juce::MidiMessage::getMidiNoteName (firstWhite (pcBase()), true, true, 4) + ", velocity " + juce::String (pcVelocity());
        }

        bool keyPressed (const juce::KeyPress& k) override
        {
            if (state.isValid() && ! k.getModifiers().isAnyModifierKeyDown())
            {
                const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
                if (c == 'z' || c == 'x')
                {
                    state.setProperty (pcBaseProp, juce::jlimit (0, 108, firstWhite (pcBase()) + (c == 'z' ? -12 : 12)), nullptr);
                    if (onPcKeysChanged) onPcKeysChanged (pcSummary());
                    return true;
                }
                if (c == 'c' || c == 'v')
                {
                    state.setProperty (pcVelocityProp, juce::jlimit (1, 127, pcVelocity() + (c == 'c' ? -10 : 10)), nullptr);
                    if (onPcKeysChanged) onPcKeysChanged (pcSummary());
                    return true;
                }
                if (c >= '1' && c <= '9' && onFavorite)
                {
                    onFavorite ((int) (c - '1'));
                    return true;
                }
            }
            return juce::MidiKeyboardComponent::keyPressed (k);
        }
        ~NoteKeyboard() override { state.removeListener (this); }

        bool outsideRange (int note) const
        {
            const int low = state.getProperty (PresetManager::playLowProp, -1), high = state.getProperty (PresetManager::playHighProp, -1);
            return low >= 0 && high >= low && (note < low || note > high);
        }

        bool mouseDownOnKey (int note, const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu() || ! state.isValid())
                return juce::MidiKeyboardComponent::mouseDownOnKey (note, e);
            const auto name = juce::MidiMessage::getMidiNoteName (note, true, true, 4);
            const int low = state.getProperty (PresetManager::playLowProp, -1), high = state.getProperty (PresetManager::playHighProp, -1);
            juce::PopupMenu m;
            m.addSectionHeader ("Recommended range" + (low >= 0 && high >= low ? ": " + juce::MidiMessage::getMidiNoteName (low, true, true, 4) + " - "
                                                                              + juce::MidiMessage::getMidiNoteName (high, true, true, 4) : juce::String()));
            m.addItem (1, "Range starts at " + name);
            m.addItem (2, "Range ends at " + name);
            m.addItem (3, "Only " + name);
            m.addItem (4, "Clear range", low >= 0);
            m.addSeparator();
            m.addItem (5, "Edit play hint...", (bool) onEditHint);
            m.addSeparator();
            m.addSectionHeader ("PC keyboard: " + pcSummary());
            m.addItem (6, "PC keyboard starts at " + juce::MidiMessage::getMidiNoteName (firstWhite (note), true, true, 4) + " (key A)");
            m.addItem (7, "Keys: A row = white, Q row = black, Z/X octave, C/V velocity, 1-9 favorites", false, false);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                             [safe = juce::Component::SafePointer<NoteKeyboard> (this), note, low, high] (int id)
                             {
                                 if (safe == nullptr || id == 0) return;
                                 auto set = [&] (int lo, int hi)
                                 {
                                     safe->state.setProperty (PresetManager::playLowProp, lo, nullptr);
                                     safe->state.setProperty (PresetManager::playHighProp, hi, nullptr);
                                 };
                                 // Un extremo nuevo conserva el otro; si queda al revés, el rango se reduce a esa tecla
                                 if (id == 1) set (note, high >= note ? high : note);
                                 else if (id == 2) set (low >= 0 && low <= note ? low : note, note);
                                 else if (id == 3) set (note, note);
                                 else if (id == 4) set (-1, -1);
                                 else if (id == 5 && safe->onEditHint) safe->onEditHint();
                                 else if (id == 6)
                                 {
                                     safe->state.setProperty (pcBaseProp, firstWhite (note), nullptr);
                                     if (safe->onPcKeysChanged) safe->onPcKeysChanged (safe->pcSummary());
                                 }
                             });
            return false; // el clic derecho no toca la nota
        }

        // Además, la tecla bajo el ratón muestra su nombre al momento, como los Do (que JUCE rotula siempre)
        void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override
        {
            if (outsideRange (note))
            {
                g.setColour (juce::Colour (0xffd8d8d8)); // gris claro sobre el blanco de la tecla
                g.fillRect (area);
            }
            juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
            scaleMark (note, g, area.withTrimmedBottom (16.0f));
            if (isOver && getWhiteNoteText (note).isEmpty())
            {
                g.setColour (textColour);
                g.setFont (labelFont());
                g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 4), area.withTrimmedLeft (1.0f).withTrimmedBottom (2.0f),
                            juce::Justification::centredBottom, false);
            }
        }
        // Negras: en blanco dentro de la propia tecla (fuera de ella quedaría sobre las blancas vecinas, que no se repintan al
        // salir el ratón), ajustado a su ancho y en dos líneas si no cabe ("C#" / "4")
        void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour fill) override
        {
            juce::MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, outsideRange (note) ? fill.brighter (0.5f) : fill);
            scaleMark (note, g, area.withTrimmedBottom (4.0f));
            if (isOver)
            {
                g.setColour (juce::Colours::white);
                g.setFont (labelFont());
                const auto name = juce::MidiMessage::getMidiNoteName (note, true, false, 4) + "\n"
                                + juce::String (note / 12 - 1); // octava con C4 = 60
                g.drawFittedText (name, area.reduced (1.0f, 3.0f).toNearestInt(), juce::Justification::centredBottom, 2, 0.6f);
            }
        }
        juce::Font labelFont() const
        {
            return juce::Font (juce::FontOptions (juce::jmin (12.0f, getKeyWidth() * 0.9f))).withHorizontalScale (0.8f);
        }

        // Teclas de la escala (pie del teclado, Mode Round o Filter): un punto al pie de cada tecla de la escala, mayor en la
        // raíz. Colores fijos, como el teclado (no se tiñe con el skin). scaleInfo: máscara de 12 bits por clase de nota
        // absoluta (bit 0 = C; 0 = sin escala) y clase de la raíz
        std::function<std::pair<int, int>()> scaleInfo;
        void scaleMark (int note, juce::Graphics& g, juce::Rectangle<float> area)
        {
            if (! scaleInfo) return;
            const auto [mask, rootPc] = scaleInfo();
            if (mask == 0 || ((mask >> (note % 12)) & 1) == 0) return;
            const auto isRoot = note % 12 == rootPc;
            const auto d = juce::jmin (area.getWidth() * (isRoot ? 0.55f : 0.4f), isRoot ? 8.0f : 6.0f);
            g.setColour (isRoot ? juce::Colour (0xffe08a20) : juce::Colour (0xff42a2c8));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ area.getCentreX(), area.getBottom() - d }));
        }

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& id) override
        {
            if (id == PresetManager::playLowProp || id == PresetManager::playHighProp)
                repaint();
            else if (id == pcBaseProp || id == pcVelocityProp)
                remapPcKeys();
        }

        // Tecla blanca de la nota, o la blanca anterior si es negra
        static int firstWhite (int note)
        {
            while (note > 0 && juce::MidiMessage::isMidiNoteBlack (note)) --note;
            return note;
        }

        // Tecla por su posición física (código de escaneo de un teclado PC: 0x27 y 0x28 tras la L, 0x1A tras la P). En Windows,
        // JUCE traduce un carácter con VkKeyScan según la distribución activa (juce_Windowing_windows.cpp, isKeyCurrentlyDown):
        // ';' en un teclado español es Mayús + coma y sonaría con la tecla de la coma. Con la tecla virtual de esa posición
        // (marca extendedKeyModifier = 0x10000, juce_Windowing_windows.cpp:681) suena la tecla que hay ahí: Ñ y ´ en español,
        // ; y ' en inglés. Fuera de Windows, el carácter de la distribución inglesa
        static juce::KeyPress physicalKey (int scanCode, char fallback)
        {
           #if JUCE_WINDOWS
            if (const auto vk = MapVirtualKeyW ((unsigned int) scanCode, 1 /* MAPVK_VSC_TO_VK */); vk != 0)
                return juce::KeyPress ((int) vk | 0x10000, 0, 0);
           #else
            juce::ignoreUnused (scanCode);
           #endif
            return juce::KeyPress (fallback, 0, 0);
        }

        void remapPcKeys()
        {
            // Suelta las notas del teclado del PC antes de cambiar el mapa (JUCE buscaría la nota en el mapa nuevo y la antigua
            // se quedaría sonando); focusLost es la vía pública a resetAnyKeysInUse
            juce::MidiKeyboardComponent::focusLost (juce::Component::focusChangedDirectly);
            clearKeyMappings();
            pcKeys.clear();
            setKeyPressBaseOctave (0); // la "posición" de cada tecla es directamente su nota MIDI
            // Blancas: A S D F G H J K L y las dos siguientes (Ñ y ´ en español); negras: la tecla de arriba entre cada par
            const std::array<juce::KeyPress, 11> whites { juce::KeyPress ('a', 0, 0), { 's', 0, 0 }, { 'd', 0, 0 }, { 'f', 0, 0 }, { 'g', 0, 0 },
                                                          { 'h', 0, 0 }, { 'j', 0, 0 }, { 'k', 0, 0 }, { 'l', 0, 0 }, physicalKey (0x27, ';'),
                                                          physicalKey (0x28, '\'') };
            const std::array<juce::KeyPress, 10> gaps { juce::KeyPress ('w', 0, 0), { 'e', 0, 0 }, { 'r', 0, 0 }, { 't', 0, 0 }, { 'y', 0, 0 },
                                                        { 'u', 0, 0 }, { 'i', 0, 0 }, { 'o', 0, 0 }, { 'p', 0, 0 }, physicalKey (0x1A, '[') };
            auto note = firstWhite (pcBase());
            int prev = -1;
            for (size_t i = 0; i < whites.size() && note < 128; ++i)
            {
                setKeyPressForNote (whites[i], note);
                pcKeys.push_back (whites[i]);
                if (prev >= 0 && note - prev == 2)
                {
                    setKeyPressForNote (gaps[i - 1], prev + 1);
                    pcKeys.push_back (gaps[i - 1]);
                }
                prev = note;
                note += juce::MidiMessage::isMidiNoteBlack (note + 1) ? 2 : 1;
            }
            setVelocity ((float) pcVelocity() / 127.0f, true); // el ratón sigue dando velocidad por la altura del clic
        }
        std::vector<juce::KeyPress> pcKeys; // teclas con nota en el mapa actual
        juce::ValueTree state;
        std::function<void()> onEditHint;
    };
    // Teclado en ventana propia (modo Floating): otro teclado sobre el mismo MidiKeyboardState del procesador, con su pie
    // (Transpose y escala: otra ScaleBar sobre los mismos parámetros) debajo, como acoplado
    struct KeyboardWindow : PinnableWindow
    {
        KeyboardWindow (juce::MidiKeyboardState& state, APVTS& s, juce::LookAndFeel& laf, std::function<void()> onClose)
            : PinnableWindow ("KoniSynth Keyboard", Skin::c (0xff1b1f24), juce::DocumentWindow::closeButton),
              keys (state, juce::MidiKeyboardComponent::horizontalKeyboard), bar (s), closed (std::move (onClose))
        {
            body.setLookAndFeel (&laf); // fuera del editor no hereda su LookAndFeel
            body.addAndMakeVisible (keys);
            body.addAndMakeVisible (bar);
            body.onResize = [this]
            {
                auto r = body.getLocalBounds();
                bar.setBounds (r.removeFromBottom (30));
                keys.setBounds (r);
                keys.setKeyWidth ((float) keys.getWidth() / 52.0f); // ~4 octavas y media visibles
            };
            body.setSize (1100, 140);
            setContentNonOwned (&body, true);
            setResizable (true, true);
            setResizeLimits (400, 110, 3000, 430);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        ~KeyboardWindow() override { body.setLookAndFeel (nullptr); }
        void closeButtonPressed() override { if (closed) closed(); }
        struct Body : juce::Component
        {
            std::function<void()> onResize;
            void resized() override { if (onResize) onResize(); }
        };
        Body body;
        NoteKeyboard keys;
        ScaleBar bar;
        std::function<void()> closed;
    };
    std::unique_ptr<KeyboardWindow> floatingKeyboard;

    // Teclado que recibe el teclado del PC (el flotante si está abierto) cuando no tiene el foco; nadie si se está escribiendo
    // en un campo de texto o si ya lo tiene
    NoteKeyboard* pcKeyboardTarget()
    {
        if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
            return nullptr;
        auto* kb = floatingKeyboard != nullptr ? &floatingKeyboard->keys : (keyboard.isShowing() ? &keyboard : nullptr);
        return kb != nullptr && ! kb->hasKeyboardFocus (false) ? kb : nullptr;
    }

    // Superficie del motor en ventana propia (modo Floating de PAD): redimensionable, encima solo con la chincheta; al
    // cerrar vuelve abajo
    struct SurfaceWindow : PinnableWindow
    {
        SurfaceWindow (const juce::String& name, std::unique_ptr<juce::Component> body, int w, int h, std::function<void()> onClose)
            : PinnableWindow (name, Skin::c (0xff1b1f24), juce::DocumentWindow::closeButton), closed (std::move (onClose))
        {
            body->setSize (w, h);
            setContentOwned (body.release(), true);
            setResizable (true, true);
            setResizeLimits (300, 80, 3000, 1200);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { if (closed) closed(); }
        std::function<void()> closed;
    };
    std::unique_ptr<SurfaceWindow> floatingPad;
    int floatingPadEngine = -1;
    std::unique_ptr<GroupTab> padTab;
    juce::TextButton optionsButton { "Options" }; // solo standalone: sustituye al de la ventana de JUCE
    juce::Component::SafePointer<juce::Component> hookedWindow, hookedOptions, hookedNotification;
    PresetBar presetBar;
    std::unique_ptr<SourceTabs> sourceTabs; // contiene osc, wavetableSection, sampleSection y stringSection (se crea en el constructor)
    std::unique_ptr<SourceTabs> mixerTabs;  // contiene osc2Section, osc3Section y noiseSection
    Visualizer scope;
    std::unique_ptr<VisualizerWindow> displayWindow;
    BpmDisplay bpmDisplay { koni };
    WaveView waveView { koni }; // antes que waveSection (cuerpo de la sección): se destruye después
    SignalStrip signalStrip { koni }; // tira del recorrido de la señal (fila superior del Synth)
    FilterView filterView { koni };   // antes que filter (cuerpo lateral de la sección): se destruye después
    EnvView filterEnvView { koni.apvts, "filterAttack", "filterDecay", "filterSustain", "filterRelease" }; // ídem, envolventes
    EnvView ampEnvView { koni.apvts, "attack", "decay", "sustain", "release" };
    StageEnvView pitchEnvView { koni.apvts, EngineEnv::synthPitch() };
    // Envolventes de los motores emulados (ModViews.h, EngineEnv)
    StageEnvView vl1EnvView { koni.apvts, EngineEnv::vl1() }, genxEnvView { koni.apvts, EngineEnv::genx() },
                 n64EnvView { koni.apvts, EngineEnv::n64() }, mdPsgEnvView { koni.apvts, EngineEnv::mdPsg() },
                 pvFEnvView { koni.apvts, EngineEnv::polivoks ("pvF") }, pvAEnvView { koni.apvts, EngineEnv::polivoks ("pvA") };
    // Curvas de transferencia: saturación por voz (diálogo Saturation...), Crush y Tube
    ShaperView voiceSatView { koni.apvts, Shapers::voiceSat() }, crushView { koni.apvts, Shapers::crush() }, tubeView { koni.apvts, Shapers::tube() };
    LfoView lfoView { koni.apvts, false }, lfo2View { koni.apvts, true }; // ídem, LFO 1 y LFO 2
    Mt32Lcd mt32Lcd { koni }; // antes que mt32DisplaySection (cuerpo de la sección): se destruye después
    Dx7Display dx7Display { koni }; // ídem con fmDisplaySection
    Vl1Display vl1Display { koni }; // ídem con vl1DisplaySection
    DrumStepLeds drumLeds { koni }; // ídem con drumPatternSection
    juce::uint32 shownMt32Names = ~0u; // versión de los nombres de programa mostrados en la lista
    std::vector<std::unique_ptr<FloatWindow>> blockWindows; // tras las secciones y el LED: se destruyen antes que ellos
    std::unique_ptr<FloatWindow> bpmWindow, engineWindow;
    std::vector<Section*> engineWindowSecs; // secciones que tiene la ventana del motor (para detectar cambio de motor)
    juce::Point<int> engineWindowPos;
    Section osc { "Oscillator" }, filter { "Filter" }, master { "Master" }, lfo { "LFO" },
            echo { "Tape Echo" }, crushSection { "Crush" }, phaserSection { "Phaser" }, lfo2Section { "LFO 2" },
            formantSection { "Formant" }, ambient { "Plate Reverb" }, tube { "Tube" }, thereminSection { "Theremin" },
            chorusSection { "Chorus" }, mediaSection { "Media" }, complexSection { "Complex Oscillator" },
            osc2Section { "Osc 2" }, osc3Section { "Osc 3" }, noiseSection { "Noise" }, vl1Section { "Casio VL-1" }, vl1RhythmSection { "VL-1 Rhythm" },
            sampleSection { "Sample" }, vocoderSection { "Vocoder" }, inputSection { "Input" },
            filterEnvSection { "Filter Envelope" }, ampEnvSection { "Amp Envelope" }, controllerSection { "Voice" },
            stringSection { "Plucked String" }, arpSection { "Arpeggiator" }, wavetableSection { "Wavetable" },
            seqSection { "Sequencer" }, macroSection { "Macros" }, styloSection { "Stylophone" }, genxOscSection { "Gen X-2 Osc / Envelope (experimental)" }, genxFxSection { "Gen X-2 LFO / Filter / Delay" }, omniSection { "Omnichord" },
            omniRhythmSection { "Omnichord Rhythm" }, drumSection { "Drum Kit" }, drumPatternSection { "Pattern" },
            fmSection { "FM 6-Op" }, fmLfoSection { "FM LFO" }, chipSection { "8-Bit/DPCM" }, chipDriverSection { "8-Bit/DPCM Driver" }, chipStereoSection { "GB Stereo" }, chipVrc6Section { "Famicom Expansion" },
            pvOscSection { "Polivoks VCO" }, pvFilterSection { "Polivoks Mixer / VCF" }, pvFEnvSection { "Polivoks Filter EG" },
            pvAEnvSection { "Polivoks Amp EG" }, pvLfoSection { "Polivoks LFO" }, ansSection { "ANS" },
            mdSection { "Mega Drive" }, mdLfoSection { "MD LFO" }, mdVoiceSection { "MD Voice" }, mdPsgSection { "MD PSG (SN76489)" }, snesSection { "SNES" }, snesEnvSection { "SNES Envelope" },
            snesEchoSection { "SNES Echo / Output" }, n64Section { "N64" }, n64EnvSection { "N64 Envelope" }, n64FxSection { "N64 Effect" }, oplSection { "OPL" }, oplLfoSection { "OPL LFO / Output" }, opllSection { "OPLL / VRC7" }, mt32Section { "Roland MT-32" }, mt32DisplaySection { "Display" }, fmDisplaySection { "Display" }, vl1DisplaySection { "Display" },
            flangerSection { "Flanger" }, fshiftSection { "Freq Shifter" }, leslieSection { "Leslie" }, springSection { "Spring Reverb" }, eqSection { "EQ" },
            transSection { "Transient" }, compSection { "Compressor" }, limiterSection { "Limiter" },
            ampSection { "Amp" }, waveSection { "Wave" };
    std::unique_ptr<PanelDialog> panel;
    juce::Component::SafePointer<ParamPanel> paramPanel; // el último diálogo de parámetros abierto (nulo al cerrarse)
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<Overlay> overlay;
    NoteKeyboard keyboard;
    ScaleBar scaleBar { koni.apvts }; // pie del teclado: Transpose y escala (UI/ScaleBar.h)
    ThereminPad pad;
    StylophonePad styloPad;
    int shownStyloModel = -1, shownOplModel = -1;
    int engine = 0; // 0 Synth, 1 Theremin, 2 Casio VL-1, 3 Stylophone, 4 Omnichord, 5 Drum Machine, 6 FM 6-Op, 7 8-Bit/DPCM, 8 Polivoks, 9 ANS
    juce::ParameterAttachment engineAttachment; // declarado al final: su callback usa todo lo anterior

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KoniSynthEditor)
};

// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Editor escalable: todo se maqueta en 'content' a tamaño lógico fijo (kWidth × kHeight) y se escala con una
// transformación al tamaño de la ventana. Proporción fija; 50–125 %; arranca al 75 % (1152 × 735: en una pantalla
// de 1920 × 1200 con Windows al 125 % ocupa 1440 × 919 px).
// Orden de las filas = orden de la señal: generación -> forma (filtro y envolventes) -> entrada/vocoder y modulación ->
// efectos en el orden en que se aplican -> master. Flechas entre las secciones que forman cadena.
// Tooltips a los 2 s (regla del proyecto: todo control lleva ayuda y menú con "Reset to default").
class KoniSynthEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ComponentListener
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
        midiButton.setTooltip ("MIDI input (all engines): MIDI channel, fine tune, bend range and controller profiles "
                               "(Akai Oxygen Pro Mini, Casio CT-S400, Yamaha PSR-EW425, Yamaha reface YC).");
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

        // 1. Generación
        osc.withCombo (s, "wave")
           .withKnob (s, "osc1Level", "Level").withKnob (s, "unisonVoices", "Voices").withKnob (s, "unisonDetune", "Detune").withKnob (s, "unisonSpread", "Spread")
           .withKnob (s, "pulseWidth", "PW").withKnob (s, "pwmDepth", "PWM").withKnob (s, "drift", "Drift")
           .withKnob (s, "pitchEnvAmount", "P.Env").withKnob (s, "pitchEnvDecay", "P.Decay");
        complexSection.withToggle (s, "complexOn").withCombo (s, "modWave").withExtraToggle (s, "modSync", "Sync")
                      .withKnob (s, "modRatio", "Ratio").withKnob (s, "fmIndex", "FM")
                      .withKnob (s, "amDepth", "AM/Ring").withKnob (s, "timbre", "Timbre")
                      .withKnob (s, "symmetry", "Symmetry").withKnob (s, "timbreMod", "T.Mod");
        // Segundo grupo de pestañas (Osc 2 | Osc 3 | Noise), con Mute/Solo como las fuentes de Osc 1
        osc2Section.withCombo (s, "osc2Wave").withExtraToggle (s, "osc2Sync", "Sync")
                   .withKnob (s, "osc2Level", "Level").withKnob (s, "osc2Octave", "Oct").withKnob (s, "osc2Semi", "Semi")
                   .withKnob (s, "osc2Fine", "Fine").withKnob (s, "osc2WtPosition", "WT Pos");
        osc3Section.withCombo (s, "osc3Wave")
                   .withKnob (s, "subLevel", "Level").withKnob (s, "subOctave", "Oct").withKnob (s, "osc3Semi", "Semi")
                   .withKnob (s, "osc3Fine", "Fine");
        noiseSection.withCombo (s, "noiseColor").withKnob (s, "noiseLevel", "Level");
        mixerTabs = std::make_unique<SourceTabs> (s, std::vector<SourceTabs::Source> {
            { "Osc 2", &osc2Section, "osc2Level", "osc2Mute" }, { "Osc 3", &osc3Section, "subLevel", "osc3Mute" },
            { "Noise", &noiseSection, "noiseLevel", "noiseMute" } }, Params::srcOsc2, koni.getParams().soloSource);
        // Sample, Wavetable y Sequencer: sección compacta + panel de edición propio (Edit...)
        sampleSection.withHeaderButton ("Edit...", "Open the sample editor: load a file, see its waveform, set Start, Length and Loop.",
                                        [this] { openSamplePanel(); })
                     .withKnob (s, "sampleLevel", "Level");
        wavetableSection.withCombo (s, "wtTable")
                        .withHeaderButton ("Edit...", "Open the wavetable editor: see the frames, load your own table.", [this] { openWavetablePanel(); })
                        .withKnob (s, "wtLevel", "Level").withKnob (s, "wtPosition", "Position");
        seqSection.withToggle (s, "seqOn")
                  .withHeaderButton ("Edit...", "Open the step sequencer: pitch, on/off and tie for each of the 16 steps.", [this] { openSequencerPanel(); })
                  .withKnob (s, "seqRate", "Rate"); // Steps, Gate y BPM: en el diálogo
        stringSection.withCombo (s, "stringExcite").withKnob (s, "stringLevel", "Level").withKnob (s, "stringDecay", "Decay")
                     .withKnob (s, "stringBright", "Bright").withKnob (s, "stringPick", "Pick")
                     .withKnob (s, "stringDamping", "Damping").withKnob (s, "stringPickup", "Pickup");
        // Fuentes en pestañas: suenan sumadas; cada una con su nivel, LED y Mute/Solo
        sourceTabs = std::make_unique<SourceTabs> (s, std::vector<SourceTabs::Source> {
            { "Osc 1", &osc, "osc1Level", "osc1Mute" }, { "Wavetable", &wavetableSection, "wtLevel", "wtMute" },
            { "Sample", &sampleSection, "sampleLevel", "sampleMute" }, { "String", &stringSection, "stringLevel", "stringMute" } },
            Params::srcOsc1, koni.getParams().soloSource);

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
              .withHeaderButton ("Saturation...", "Saturation: clean filter input (Drive 0 % linear) and per-voice saturation after the filter "
                                           "(curve Soft, Diode, Hard, Fold, and drive).", [this]
              {
                  openParamPanel ("Saturation", [] (ParamPanel& p, APVTS& st)
                  {
                      p.add ("Filter Input").withToggle (st, "filterCleanDrive").withKnob (st, "filterDrive", "Drive");
                      p.add ("Voice Saturation").withCombo (st, "voiceSatMode").withKnob (st, "voiceSatDrive", "Drive");
                  });
              })
              .withHeaderControl ("Ladder quality", qualitySelector, 190)
              .withKnob (s, "cutoff", "Cutoff").withKnob (s, "resonance", "Reso").withKnob (s, "filterDrive", "Drive")
              .withKnob (s, "filterEnv", "Env").withKnob (s, "filterKey", "Key").withKnob (s, "velToFilter", "Vel")
              .withKnob (s, "filterSpread", "Spread").withKnob (s, "voiceSatDrive", "Sat")
              .withItemTooltips ({ "Ladder, 4 poles (24 dB/oct): the classic Moog low-pass.", "Ladder, 2 poles (12 dB/oct).",
                                   "Ladder band-pass.", "Ladder high-pass.", "State-variable low-pass (12 dB/oct), smooth like the Oberheim SEM.",
                                   "State-variable band-pass.", "State-variable high-pass.", "State-variable notch: removes a band.",
                                   "Korg35 low-pass of the MS-20: biting resonance that screams.", "Korg35 high-pass of the MS-20 (6 dB/oct, like the original).",
                                   "Comb resonator: all harmonics of Cutoff (metallic, plucked). Key 100 % to play in tune.",
                                   "Comb with negative feedback: odd harmonics only (hollow, like a closed pipe).",
                                   "Two band-pass filters in parallel, Spread apart: vowel-like formants.",
                                   "Two low-pass filters in series, Spread apart." });
        filterEnvSection.withSlider (s, "filterAttack", "A").withSlider (s, "filterDecay", "D")
                        .withSlider (s, "filterSustain", "S").withSlider (s, "filterRelease", "R");
        ampEnvSection.withCombo (s, "envCurve").withSlider (s, "attack", "A").withSlider (s, "decay", "D")
                     .withSlider (s, "sustain", "S").withSlider (s, "release", "R")
                     .withKnob (s, "voicePan", "V.Pan").withKnob (s, "voicePanMode", "Pan Mode"); // paneo por voz, tras el VCA

        // Motores alternativos
        thereminSection.withCombo (s, "thereminModel")
                       .withKnob (s, "thereminRange", "Range").withKnob (s, "thereminGlide", "Glide")
                       .withKnob (s, "thereminTone", "Tone").withKnob (s, "thereminField", "Field")
                       .withKnob (s, "thereminLock", "Lock").withKnob (s, "thereminZero", "Zero")
                       .withKnob (s, "drift", "Drift"); // mismo parámetro que el Drift de Oscillator
        vl1Section.withCombo (s, "vl1Sound")
                  .withKnob (s, "vl1Octave", "Octave").withSlider (s, "vl1Attack", "A").withSlider (s, "vl1Decay", "D")
                  .withSlider (s, "vl1SustainLevel", "SL").withSlider (s, "vl1SustainTime", "ST").withSlider (s, "vl1Release", "R")
                  .withKnob (s, "vl1Vibrato", "V").withKnob (s, "vl1Tremolo", "T");
        styloSection.withCombo (s, "styloModel").withExtraToggle (s, "styloVibrato", "Vibrato")
                   .withKnob (s, "styloRange", "Range").withKnob (s, "styloRegister", "Register")
                   .withKnob (s, "styloVibDepth", "Depth").withKnob (s, "styloTuning", "Tuning")
                   .withKnob (s, "styloOutput", "Output").withKnob (s, "styloClick", "Click");
        // Gen X-2 (experimental): oscilador y envolvente | LFO, filtro y delay, en el orden del panel frontal del manual
        genxOscSection.withExtraToggle (s, "genxPwm", "X (PWM)")
                      .withKnob (s, "genxSub1", "-1").withKnob (s, "genxSub2", "-2")
                      .withKnob (s, "genxOctave", "Octave").withKnob (s, "genxAttack", "Attack").withKnob (s, "genxPitch", "Pitch")
                      .withKnob (s, "genxDecay", "Decay").withKnob (s, "genxStrip", "Strip");
        genxFxSection.withExtraToggle (s, "genxDelay", "Delay")
                     .withKnob (s, "genxLfoWave", "Wave").withKnob (s, "genxLfoRate", "Rate").withKnob (s, "genxLfoDepth", "Depth")
                     .withKnob (s, "genxCutoff", "Cutoff").withKnob (s, "genxRes", "Res")
                     .withKnob (s, "genxTime", "Time").withKnob (s, "genxRepeats", "Repeats").withKnob (s, "genxLevel", "Level");
        omniSection.withKnob (s, "omniSplit", "Split").withKnob (s, "omniSustain", "Sustain").withKnob (s, "omniHarpOctave", "Oct")
                   .withKnob (s, "omniHarpLevel", "Harp").withKnob (s, "omniChordLevel", "Chord").withKnob (s, "omniBassLevel", "Bass");
        omniRhythmSection.withSortedCombo (s, "omniRhythm").withKnob (s, "omniTempo", "Tempo").withKnob (s, "omniRhythmLevel", "Level");
        drumSection.withCombo (s, "drumKit").withExtraToggle (s, "drumVelocity", "Velocity")
                   .withKnob (s, "drumTune", "Tune").withKnob (s, "drumDecay", "Decay").withKnob (s, "drumTone", "Tone")
                   .withKnob (s, "drumSnappy", "Snappy").withKnob (s, "drumAccent", "Accent");
        drumPatternSection.withSortedCombo (s, "drumPattern").withKnob (s, "drumPattern2", "+ Pattern")
                          .withKnob (s, "drumTempo", "Tempo").withKnob (s, "drumSwing", "Swing")
                          .withBody (drumLeds, 12, 0); // fila de LED de los pasos sobre las perillas
        fmSection.withHeaderButton ("Edit...", "Open the FM editor: ratio, level, envelope and keyboard scaling of the 6 operators.",
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
        chipSection.withPickCombo (s, "chipModel", [this] (int model) { loadRepresentative (7, model); })
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
                 .withHeaderButton ("Edit...", "Open the Mega Drive editor: the 4 operators with the YM2612's own registers.", [this] { openMdPanel(); })
                 .withKnob (s, "mdAlgorithm", "Algorithm").withKnob (s, "mdFeedback", "Feedback");
        for (int op = 1; op <= 4; ++op)
            mdSection.withKnob (s, "md" + juce::String (op) + "Tl", "OP" + juce::String (op) + " TL");
        snesSection.withHeaderButton ("Frame Macros...", "Experimental per-frame macros (60.10 Hz): volume, arpeggio, pitch and noise per voice. "
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
        n64Section.withCombo (s, "n64Source").withExtraToggle (s, "n64Adpcm", "VADPCM")
                  .withKnob (s, "n64SourceRate", "Src Rate").withKnob (s, "n64OutputRate", "Out Rate").withKnob (s, "n64Volume", "Volume")
                  .withKnob (s, "n64Pan", "Pan").withKnob (s, "n64Spread", "Spread");
        n64EnvSection.withExtraToggle (s, "n64Velocity", "Velocity")
                     .withKnob (s, "n64Attack", "Attack").withKnob (s, "n64Decay", "Decay").withKnob (s, "n64Release", "Release")
                     .withKnob (s, "n64AttackVolume", "A Vol").withKnob (s, "n64DecayVolume", "D Vol");
        n64FxSection.withCombo (s, "n64Fx").withKnob (s, "n64FxMix", "Mix").withKnob (s, "n64Dac", "DAC");
        oplSection.withCombo (s, "oplModel").withExtraToggle (s, "oplFourOp", "4-Op")
                  .withHeaderButton ("Edit...", "Open the OPL editor: the operators with the chip's own registers.", [this] { openOplPanel(); })
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
        mdVoiceSection.withHeaderButton ("Frame Macros...", "Per-frame macros of the sound driver (59.92 Hz): volume, arpeggio and pitch "
                                         "for FM and PSG, and the PSG noise type.", [this] { openMdMacros(); })
                      .withCombo (s, "mdVoice")
                      .withKnob (s, "mdDacRate", "DAC Rate").withKnob (s, "mdCh3Op1", "Ch3 OP1").withKnob (s, "mdCh3Op2", "Ch3 OP2")
                      .withKnob (s, "mdCh3Op3", "Ch3 OP3");
        mdPsgSection.withCombo (s, "mdPsgNoise")
                    .withKnob (s, "mdPsgNoiseRate", "N.Rate").withSlider (s, "mdPsgAttack", "A").withSlider (s, "mdPsgDecay", "D")
                    .withSlider (s, "mdPsgSustain", "S").withSlider (s, "mdPsgRelease", "R");
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
                     .withSlider (s, "pvFAttack", "A").withSlider (s, "pvFDecay", "D").withSlider (s, "pvFSustain", "S").withSlider (s, "pvFRelease", "R");
        pvAEnvSection.withExtraToggle (s, "pvALoop", "Repeat")
                     .withSlider (s, "pvAAttack", "A").withSlider (s, "pvADecay", "D").withSlider (s, "pvASustain", "S").withSlider (s, "pvARelease", "R");
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
                  .withHeaderButton ("Edit...", "Open the ANS plate: draw the score (pitch vertically, time horizontally) and set the 20 registers.",
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
        vl1RhythmSection.withCombo (s, "vl1Rhythm")
                        .withKnob (s, "vl1Tempo", "Tempo").withKnob (s, "vl1Balance", "Balance");
        vl1DisplaySection.withBody (vl1Display, 0, 420); // 9 posiciones de 7 segmentos a ~52 px de alto

        // 3. Entrada, vocoder y modulación
        inputSection.withKnob (s, "inputGain", "Gain").withKnob (s, "inputMonitor", "Monitor");
        vocoderSection.withToggle (s, "vocoderOn")
                      .withHeaderButton ("Edit...", "Vocoder settings: filter bank, band envelopes (attack, release) and sibilance.", [this]
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
           .withKnob (s, "lfoRate", "Rate").withKnob (s, "lfoDepth", "Depth").withKnob (s, "lfoShape", "Shape").withKnob (s, "lfoSync", "Sync");
        lfo2Section.withToggle (s, "lfo2On").withCombo (s, "lfo2Target")
                   .withHeaderButton ("Matrix...", "Modulation matrix of the Synth engine: 8 slots, each a source (LFOs, envelopes, velocity, "
                                      "key, wheel, aftertouch, MPE timbre, random, bend) moving a destination per voice. The number in "
                                      "brackets is the count of active routes; amber rings on the knobs show their range.",
                                      [this] { openPanel ("Modulation Matrix", std::make_unique<MatrixPanel> (koni), 1100, 520); })
                   .withKnob (s, "lfo2Rate", "Rate").withKnob (s, "lfo2Depth", "Depth").withKnob (s, "lfo2Shape", "Shape").withKnob (s, "lfo2Sync", "Sync");
        formantSection.withToggle (s, "formantOn").withCombo (s, "formantVoice")
                      .withKnob (s, "formantVowel", "Vowel").withKnob (s, "formantShift", "Shift").withKnob (s, "formantRate", "Morph")
                      .withKnob (s, "formantDepth", "Depth").withKnob (s, "formantMix", "Mix");
        controllerSection.withCombo (s, "voiceMode")
                         .withHeaderButton ("Edit...", "Voice settings of the Synth engine: aftertouch, MPE, voice assignment, oscillator "
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
                  .withHeaderButton ("Edit...", "Arpeggiator settings: latch, octaves, gate, one-shot and the internal tempo.", [this]
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
        macroSection.withHeaderButton ("Edit...", "Performance macros: names, assigned parameters and their From / To ranges. "
                                       "Assign a parameter with right-click on its control > Assign to macro.", [this] { openPerfMacroPanel(); });
        for (int m = 0; m < PerfMacros::numMacros; ++m)
            macroSection.withKnob (s, PerfMacros::paramId (m), koni.perfMacros.name (m));

        // 4–5. Efectos en el orden de la cadena y master
        crushSection.withToggle (s, "crushOn")
                    .withKnob (s, "crushBits", "Bits").withKnob (s, "crushRate", "Rate").withKnob (s, "crushDrive", "Drive")
                    .withKnob (s, "crushMix", "Mix");
        phaserSection.withToggle (s, "phaserOn")
                     .withKnob (s, "phaserRate", "Rate").withKnob (s, "phaserDepth", "Depth")
                     .withKnob (s, "phaserFeedback", "Color").withKnob (s, "phaserMix", "Mix");
        tube.withToggle (s, "tubeOn")
            .withKnob (s, "tubeDrive", "Drive").withKnob (s, "tubeBias", "Bias");
        // EQ: en el panel, como el de un ampli (Bass / Mid / Treble); todo lo demás, curva y nodos en Edit...
        eqSection.withToggle (s, "eqOn")
                 .withHeaderButton ("Edit...", "Parametric EQ: low/high cuts, shelves, two mid bands with Q, and the response curve "
                                    "over the live spectrum (drag the nodes).", [this] { openPanel ("Equalizer", std::make_unique<EqPanel> (koni), 1180, 640); })
                 .withKnob (s, "eqLowGain", "Bass").withKnob (s, "eqMid1Gain", "Mid").withKnob (s, "eqHighGain", "Treble");
        // Ampli: modelo y mandos de un ampli en el panel; Presence, Sag y Output en Edit...
        // Amp con su pantalla (la pantalla solo actúa con el ampli encendido): en el panel lo que se toca tocando, incluida la
        // mezcla de la pantalla (Cab); tipo de pantalla e IR del usuario en Edit...
        ampSection.withToggle (s, "ampOn").withCombo (s, "ampModel")
                  .withHeaderButton ("Edit...", "Amp: presence, power supply sag and output level; speaker cabinet type and your own "
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
        // Dinámica: en el panel lo que se toca tocando; tiempos, rodilla, llave y curva de transferencia en Edit...
        transSection.withToggle (s, "transOn").withKnob (s, "transAttack", "Attack").withKnob (s, "transSustain", "Sustain");
        compSection.withToggle (s, "compOn").withCombo (s, "compMode")
                   .withHeaderButton ("Edit...", "Compressor: attack, release, knee, makeup, sidechain filter and key (ducking from "
                                      "the audio input), with the transfer curve and the gain reduction meter.",
                                      [this] { openPanel ("Dynamics", std::make_unique<DynamicsPanel> (koni), 980, 560); })
                   .withKnob (s, "compThreshold", "Thresh").withKnob (s, "compRatio", "Ratio").withKnob (s, "compMix", "Mix");
        chorusSection.withToggle (s, "chorusOn").withCombo (s, "chorusMode")
                     .withKnob (s, "chorusMix", "Mix").withKnob (s, "chorusDepth", "Depth");
        echo.withToggle (s, "echoOn")
            .withHeaderButton ("Edit...", "Tape echo settings: ping-pong, tone, wow and tempo sync.", [this]
            {
                openParamPanel ("Tape Echo", [] (ParamPanel& p, APVTS& st)
                {
                    p.add ("Tape Echo").withExtraToggle (st, "echoPingPong", "Ping-Pong")
                                       .withKnob (st, "echoTone", "Tone").withKnob (st, "echoWow", "Wow").withKnob (st, "echoSync", "Sync");
                });
            })
            .withKnob (s, "echoTime", "Time").withKnob (s, "echoFeedback", "Feedback").withKnob (s, "echoMix", "Mix");
        ambient.withToggle (s, "ambientOn").withExtraToggle (s, "reverbFreeze", "Freeze")
               .withHeaderButton ("Edit...", "Plate reverb settings: predelay, diffusion, damping, tone, modulation and width.", [this]
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
        for (auto* c : std::initializer_list<juce::Component*> { &filter, &complexSection, &thereminSection,
                                                                 &vl1Section, &vl1RhythmSection, &vl1DisplaySection, &styloSection, &genxOscSection, &genxFxSection, &omniSection, &omniRhythmSection,
                                                                 &drumSection, &drumPatternSection, &fmSection, &fmLfoSection, &fmDisplaySection, &chipSection, &chipDriverSection, &chipStereoSection, &chipVrc6Section, &mdSection, &mdLfoSection, &mdVoiceSection, &mdPsgSection, &snesSection, &snesEnvSection, &snesEchoSection, &n64Section, &n64EnvSection, &n64FxSection, &oplSection, &oplLfoSection, &opllSection, &mt32Section, &mt32DisplaySection, &pvOscSection, &pvFilterSection,
                                                                 &pvFEnvSection, &pvAEnvSection, &pvLfoSection, &ansSection, &filterEnvSection,
                                                                 &ampEnvSection })
            rows.addChildComponent (*c);
        for (auto* c : std::initializer_list<juce::Component*> { &keyboard, &pad, &styloPad })
            content.addChildComponent (*c);
        rowsView.setViewedComponent (&rows, false);
        rowsView.setScrollBarsShown (true, false); // solo vertical
        rowsView.setScrollBarThickness (12);
        content.addAndMakeVisible (rowsView);
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
            fileChooser = std::make_unique<juce::FileChooser> ("MT-32 / CM-32L ROM folder", Mt32Machine::romFolder());
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [safe = SafePointer<KoniSynthEditor> (this)] (const juce::FileChooser& fc)
                {
                    if (safe != nullptr && fc.getResult() != juce::File())
                        safe->koni.setMt32Folder (fc.getResult());
                });
        });
        m.addItem ("Open ROM folder", [] { const auto f = Mt32Machine::romFolder(); f.createDirectory(); f.startAsProcess(); });
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
        for (auto* c : std::initializer_list<juce::Component*> { sourceTabs.get(), &filter, &complexSection, mixerTabs.get(), &filterEnvSection, &ampEnvSection,
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

    void openSamplePanel()     { openPanel ("Sample", std::make_unique<SamplePanel> (koni, [this] { chooseSample(); }), 1100, 560); }
    void openWavetablePanel()  { openPanel ("Wavetable", std::make_unique<WavetablePanel> (koni, [this] { chooseWavetable(); }), 1100, 620); }
    void openSequencerPanel()  { openPanel ("Step Sequencer", std::make_unique<SequencerPanel> (koni), 1300, 560); }
    void openPerfMacroPanel()  { openPanel ("Macros", std::make_unique<PerfMacroPanel> (koni), 1100, 470); }
    // Diálogo "Edit..." de una sección: una fila de secciones; ~78 px por unidad de peso, alto de una fila del panel
    void openParamPanel (const juce::String& panelTitle, std::function<void (ParamPanel&, APVTS&)> build)
    {
        auto body = std::make_unique<ParamPanel>();
        build (*body, koni.apvts);
        const auto w = juce::jlimit (420, kWidth - 40, juce::roundToInt (body->weight() * 78.0f) + 52);
        openPanel (panelTitle, std::move (body), w, 230);
    }
    // Modo del teclado en pantalla (propiedad del estado del plugin): 0 Docked, 1 Hidden, 2 Floating
    int keyboardMode() const { return juce::jlimit (0, 2, (int) koni.apvts.state.getProperty ("keyboardMode", 0)); }

    void setKeyboardMode (int mode)
    {
        koni.apvts.state.setProperty ("keyboardMode", mode, nullptr);
        if (mode == 2 && floatingKeyboard == nullptr)
            floatingKeyboard = std::make_unique<KeyboardWindow> (koni.keyboardState, [safe = juce::Component::SafePointer<KoniSynthEditor> (this)]
            {
                if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setKeyboardMode (0); }); // al cerrar: vuelve abajo
            });
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
        midi.addItem ("Controller: Akai Oxygen Pro Mini...", [this] { openControllerPanel (oxygenProMini()); });
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

    void chooseWavetable()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load wavetable", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                if (const auto error = safe->koni.loadWavetable (fc.getResult()); error.isNotEmpty())
                    safe->overlay = Overlay::create (*safe, "Wavetable", error, std::nullopt, "OK", {},
                                                     [safe] (bool, const juce::String&, int) { if (safe != nullptr) safe->overlay.reset(); });
            });
    }

    void chooseSample()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load sample, SoundFont (.sf2), SNES BRR or SPC", juce::File(), SampleImport::fileFilter);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                if (SampleImport::isBank (fc.getResult()))
                    safe->chooseSampleItem (fc.getResult());
                else
                    safe->loadSampleItem (fc.getResult(), {});
            });
    }

    void loadSampleItem (const juce::File& file, const juce::String& item)
    {
        if (const auto error = koni.loadSample (file, item); error.isNotEmpty())
        {
            overlay = Overlay::create (*this, "Sample", error, std::nullopt, "OK", {}, [this] (bool, const juce::String&, int) { overlay.reset(); });
        }
        else
        {
            // Nota raíz y bucle del formato (SoundFont, BRR): pasan a Root y Loop del panel Sample, que siguen siendo editables
            const auto [root, loops] = koni.getSampleFormatInfo();
            const auto set = [this] (const char* id, float value)
            {
                if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (koni.apvts.getParameter (id)))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
                    p->endChangeGesture();
                }
            };
            if (root >= 0) set ("sampleRoot", (float) root);
            if (loops) set ("sampleLoop", 1.0f);
        }
        refreshSampleInfo();
    }

    // Bancos (.sf2, .spc): menú con su contenido. SoundFont: bancos -> presets -> zona (por defecto, la del Do central)
    void chooseSampleItem (const juce::File& file)
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
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sampleSection),
            [safe = SafePointer<KoniSynthEditor> (this), file, items] (int result)
            {
                if (safe != nullptr && result > 0 && result <= items->size())
                    safe->loadSampleItem (file, (*items)[result - 1]);
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
        constexpr int square = 3;
        const auto osc1Sounds = v ("osc1Level") > 0.0f || targeted (ModMatrix::dstOsc1Level);
        const auto osc2Sounds = v ("osc2Level") > 0.0f || targeted (ModMatrix::dstOsc2Level);
        const auto pwUsed = ((int) v ("wave") == square && osc1Sounds) || (v ("complexOn") > 0.5f && (int) v ("modWave") == square)
                         || ((int) v ("osc2Wave") == square && osc2Sounds);
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
            dim (osc3Section.knobFor (id), v ("subLevel") <= 0.0f);
        dim (&osc3Section.comboComponent(), v ("subLevel") <= 0.0f);
        dim (&noiseSection.comboComponent(), v ("noiseLevel") <= 0.0f && ! targeted (ModMatrix::dstNoiseLevel));
        dim (osc2Section.knobFor ("osc2WtPosition"), (int) v ("osc2Wave") != 4 || ! osc2Sounds); // solo con la forma Wavetable
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
        dim (ampEnvSection.knobFor ("voicePanMode"), std::abs (v ("voicePan")) < 1.0e-4f); // Synth.h: sin V.Pan no hay paneo que repartir
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
        const auto isSample = m.kind == KoniSynthProcessor::MissingFile::Kind::sample;
        const juce::String what = isSample ? "Sample" : m.kind == KoniSynthProcessor::MissingFile::Kind::cabIr ? "Cabinet IR" : "Wavetable";
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
        const auto isSample = m.kind == KoniSynthProcessor::MissingFile::Kind::sample;
        const auto start = m.file.getParentDirectory().isDirectory() ? m.file.getParentDirectory()
                                                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        fileChooser = std::make_unique<juce::FileChooser> (isSample ? "Locate sample: " + m.file.getFileName()
                                                                    : "Locate wavetable: " + m.file.getFileName(),
                                                           start, isSample ? SampleImport::fileFilter : "*.wav;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<KoniSynthEditor> (this), isSample] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                const auto error = isSample ? safe->koni.loadSample (fc.getResult()) : safe->koni.loadWavetable (fc.getResult());
                if (error.isNotEmpty())
                    safe->overlay = Overlay::create (*safe, isSample ? "Sample" : "Wavetable", error, std::nullopt, "OK", {},
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
            case 0:  return { { 1.35f, { sourceTabs.get(), &complexSection, mixerTabs.get() }, 3 },  // fuentes -> complejo -> Osc 2 / Osc 3 / Noise
                              { 1.0f, { &filter, &filterEnvSection, &ampEnvSection }, 0 },
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
            { "PLAY", "foldPlay", "Performance: arpeggiator and step sequencer (for every engine).", { &arpSection, &seqSection } },
            { "IN", "foldInput", "Audio input and vocoder. The input is the vocoder's modulator (\"mod\"); Monitor sends it dry "
                                 "into the effects chain.", { &inputSection, &vocoderSection }, true, "mod" },
            { "MACROS", "foldMacros", "Performance macros: 8 knobs, each moving several parameters at once.", { &macroSection } },
            { "COLOR", "foldColor", "Effects 1/7 - color: crush, formant filter, tube and parametric EQ.", { &crushSection, &formantSection, &tube, &eqSection }, true, nullptr, true },
            { "AMP", "foldAmp", "Effects 2/7 - tube amp and speaker cabinet (built-in or your impulse response, in Edit...).", { &ampSection }, true, nullptr, true },
            { "DYNAMICS", "foldDynamics", "Effects 3/7 - dynamics: transient shaper and compressor.", { &transSection, &compSection }, true, nullptr, true },
            { "MOD FX", "foldModFx", "Effects 4/7 - modulation: phaser, flanger and frequency shifter.", { &phaserSection, &flangerSection, &fshiftSection }, true, nullptr, true },
            { "ENSEMBLE", "foldEnsemble", "Effects 5/7 - ensemble: chorus and Leslie.", { &chorusSection, &leslieSection }, true, nullptr, true },
            { "SPACE", "foldSpace", "Effects 6/7 - space: tape echo, plate reverb and spring reverb.", { &echo, &ambient, &springSection }, true, nullptr, true },
            { "OUTPUT", "foldOutput", "Effects 7/7 - output: media (tape, vinyl...), master and limiter.", { &mediaSection, &master, &limiterSection }, true, nullptr, true } };

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
            attachDockTab (*tab, blocks[b].foldId);
            tab->setTooltip (juce::String (blocks[b].tip) + " Click to fold or unfold; right-click: docked, folded or floating window.");
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
    void attachModeTab (Tab& tab, std::function<int()> get, std::function<void (int)> set)
    {
        tab.onClick = [get, set] { set (get() == 0 ? 1 : 0); };
        tab.onRightClick = [this, get, set, t = &tab]
        {
            juce::PopupMenu m;
            const auto mode = get();
            m.addItem (1, "Docked", true, mode == 0);
            m.addItem (2, "Folded", true, mode == 1);
            m.addItem (3, "Floating window", true, mode == 2);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (t),
                             [safe = SafePointer<KoniSynthEditor> (this), set] (int id)
                             {
                                 if (safe != nullptr && id > 0) set (id - 1);
                             });
        };
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
        std::vector<std::vector<std::pair<juce::Component*, float>>> rows; // filas del mismo alto; en cada una, ancho por peso
        juce::Component* overlay = nullptr; // diálogo abierto encima (ocupa todo)
        void paint (juce::Graphics& g) override { Skin::paintBackground (g, getLocalBounds(), juce::Colour (0xff22272e)); }
        void resized() override
        {
            if (overlay != nullptr)
                overlay->setBounds (getLocalBounds());
            auto area = getLocalBounds().reduced (6);
            const auto rowH = area.getHeight() / juce::jmax (1, (int) rows.size());
            for (auto& items : rows)
            {
                auto r = area.removeFromTop (rowH);
                float total = 0.0f;
                for (auto& it : items) total += it.second;
                const auto unit = (float) r.getWidth() / juce::jmax (0.001f, total);
                for (auto& it : items)
                    it.first->setBounds (r.removeFromLeft (juce::roundToInt (unit * it.second)).reduced (items.size() > 1 ? 4 : 0, 2));
            }
        }
    };

    struct FloatWindow : juce::DocumentWindow
    {
        FloatWindow (const juce::String& name, std::vector<std::pair<juce::Component*, float>> items, int w, int h,
                     juce::LookAndFeel& laf, std::function<void()> onClose)
            : FloatWindow (name, std::vector<std::vector<std::pair<juce::Component*, float>>> { std::move (items) }, w, h, laf, std::move (onClose)) {}

        FloatWindow (const juce::String& name, std::vector<std::vector<std::pair<juce::Component*, float>>> rows, int w, int h,
                     juce::LookAndFeel& laf, std::function<void()> onClose)
            : juce::DocumentWindow (name, Skin::c (0xff1b1f24), juce::DocumentWindow::closeButton), closed (std::move (onClose))
        {
            auto* holder = new FloatHolder();
            holder->setLookAndFeel (&laf); // fuera del editor no hereda su LookAndFeel
            holder->rows = std::move (rows);
            for (auto& row : holder->rows)
                for (auto& it : row)
                    holder->addAndMakeVisible (it.first);
            holder->setSize (w, h);
            setContentOwned (holder, true);
            setResizable (true, true);
            setResizeLimits (160, 60, 3000, 1200);
            setAlwaysOnTop (true);
            setUsingNativeTitleBar (true);
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
            float maxUnits = 0.0f;
            for (const auto& row : rowsNow)
            {
                winRows.emplace_back();
                float units = 0.0f;
                for (auto* sec : row.sections)
                    if (sec != nullptr) { winRows.back().push_back ({ sec, sec->layoutWeight() }); units += sec->layoutWeight(); sec->setVisible (true); }
                maxUnits = juce::jmax (maxUnits, units);
            }
            engineWindow = std::make_unique<FloatWindow> ("KoniSynth ENGINE", std::move (winRows), juce::jmin (1600, juce::roundToInt (maxUnits * 60.0f) + 12), // tope: nace como mucho del ancho del editor
                                                          150 * (int) rowsNow.size(), lookAndFeel, [this] { closeFloat (engineFoldId); });
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
                    if (blocks[b].fx && blocks[line[i - 1]].fx && line[i - 1] + 1 == b)
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
    std::vector<std::vector<size_t>> blockLines (int width) const
    {
        std::vector<std::vector<size_t>> lines;
        int used = 0;
        for (size_t b = 0; b < blocks.size(); ++b)
        {
            if (blockCollapsed (b))
                continue;
            float units = 0.0f;
            for (auto* sec : blocks[b].sections) units += sec->layoutWeight();
            const auto own = tabStrip + 6 + juce::roundToInt (units * 60.0f);
            if (lines.empty() || used + blockGap + own > width)
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
        for (size_t b = 0; b < blocks.size(); ++b)
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

        // Keys: teclado acoplado (100 px) encima de la barra; oculto o flotante, su pestaña está en la barra
        const auto mode = keyboardMode();
        groupTabs[3]->setVisible (true);
        if (mode == 0)
        {
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
            const auto cell = row.removeFromLeft (juce::roundToInt (fitsNatural ? s->naturalWidth() : unit * s->layoutWeight()));
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
        return false;
    }
    juce::ToggleButton avxButton { "AVX2" };

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
    struct GroupTab : juce::Button
    {
        GroupTab (const juce::String& name, std::function<void()> click) : juce::Button (name) { onClick = std::move (click); }
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
            else juce::Button::mouseDown (e);
        }
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
    // Teclado en ventana propia (modo Floating): otro MidiKeyboardComponent sobre el mismo MidiKeyboardState del procesador
    struct KeyboardWindow : juce::DocumentWindow
    {
        KeyboardWindow (juce::MidiKeyboardState& state, std::function<void()> onClose)
            : juce::DocumentWindow ("KoniSynth Keyboard", Skin::c (0xff1b1f24), juce::DocumentWindow::closeButton),
              keys (state, juce::MidiKeyboardComponent::horizontalKeyboard), closed (std::move (onClose))
        {
            keys.setSize (1100, 110);
            setContentNonOwned (&keys, true);
            setResizable (true, true);
            setResizeLimits (400, 80, 3000, 400);
            setAlwaysOnTop (true);
            setUsingNativeTitleBar (true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { if (closed) closed(); }
        void resized() override
        {
            juce::DocumentWindow::resized();
            keys.setKeyWidth ((float) keys.getWidth() / 52.0f); // ~4 octavas y media visibles
        }
        juce::MidiKeyboardComponent keys;
        std::function<void()> closed;
    };
    std::unique_ptr<KeyboardWindow> floatingKeyboard;

    // Superficie del motor en ventana propia (modo Floating de PAD): redimensionable, siempre encima; al cerrar vuelve abajo
    struct SurfaceWindow : juce::DocumentWindow
    {
        SurfaceWindow (const juce::String& name, std::unique_ptr<juce::Component> body, int w, int h, std::function<void()> onClose)
            : juce::DocumentWindow (name, Skin::c (0xff1b1f24), juce::DocumentWindow::closeButton), closed (std::move (onClose))
        {
            body->setSize (w, h);
            setContentOwned (body.release(), true);
            setResizable (true, true);
            setResizeLimits (300, 80, 3000, 1200);
            setAlwaysOnTop (true);
            setUsingNativeTitleBar (true);
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
            fmSection { "FM 6-Op" }, fmLfoSection { "FM LFO" }, chipSection { "Chip" }, chipDriverSection { "Chip Driver" }, chipStereoSection { "GB Stereo" }, chipVrc6Section { "Famicom Expansion" },
            pvOscSection { "Polivoks VCO" }, pvFilterSection { "Polivoks Mixer / VCF" }, pvFEnvSection { "Polivoks Filter EG" },
            pvAEnvSection { "Polivoks Amp EG" }, pvLfoSection { "Polivoks LFO" }, ansSection { "ANS" },
            mdSection { "Mega Drive" }, mdLfoSection { "MD LFO" }, mdVoiceSection { "MD Voice" }, mdPsgSection { "MD PSG (SN76489)" }, snesSection { "SNES" }, snesEnvSection { "SNES Envelope" },
            snesEchoSection { "SNES Echo / Output" }, n64Section { "N64" }, n64EnvSection { "N64 Envelope" }, n64FxSection { "N64 Effect" }, oplSection { "OPL" }, oplLfoSection { "OPL LFO / Output" }, opllSection { "OPLL / VRC7" }, mt32Section { "Roland MT-32" }, mt32DisplaySection { "Display" }, fmDisplaySection { "Display" }, vl1DisplaySection { "Display" },
            flangerSection { "Flanger" }, fshiftSection { "Freq Shifter" }, leslieSection { "Leslie" }, springSection { "Spring Reverb" }, eqSection { "EQ" },
            transSection { "Transient" }, compSection { "Compressor" }, limiterSection { "Limiter" },
            ampSection { "Amp" };
    std::unique_ptr<PanelDialog> panel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<Overlay> overlay;
    juce::MidiKeyboardComponent keyboard;
    ThereminPad pad;
    StylophonePad styloPad;
    int shownStyloModel = -1, shownOplModel = -1;
    int engine = 0; // 0 Synth, 1 Theremin, 2 Casio VL-1, 3 Stylophone, 4 Omnichord, 5 Drum Machine, 6 FM 6-Op, 7 Chip, 8 Polivoks, 9 ANS
    juce::ParameterAttachment engineAttachment; // declarado al final: su callback usa todo lo anterior

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KoniSynthEditor)
};

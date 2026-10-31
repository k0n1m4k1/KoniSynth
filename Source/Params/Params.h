// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Punteros a los valores crudos: lectura lock-free desde el hilo de audio.
struct Params
{
    explicit Params (APVTS& s)
    {
        auto get = [&s] (const juce::String& id) { return s.getRawParameterValue (id); };
        gain = get ("gain"); monoOut = get ("monoOut");
        mpeOn = get ("mpeOn"); mpeBendRange = get ("mpeBendRange");
        voiceAssign = get ("voiceAssign"); oscPhase = get ("oscPhase"); voiceTolerance = get ("voiceTolerance");
        vcaClick = get ("vcaClick"); vcaLeak = get ("vcaLeak");
        voiceSatMode = get ("voiceSatMode"); filterCleanDrive = get ("filterCleanDrive"); voiceSatDrive = get ("voiceSatDrive"); voiceSatPos = get ("voiceSatPos"); filterSpread = get ("filterSpread");
        stringPickup = get ("stringPickup"); stringDamping = get ("stringDamping"); stringExcite = get ("stringExcite");
        stringBowPressure = get ("stringBowPressure"); stringHammerFelt = get ("stringHammerFelt"); stringEbowDrive = get ("stringEbowDrive");
        stringBuzz = get ("stringBuzz"); stringHarmonic = get ("stringHarmonic");
        stringStiff = get ("stringStiff"); stringTension = get ("stringTension");
        reedEmbouchure = get ("reedEmbouchure"); reedNoise = get ("reedNoise"); fluteJet = get ("fluteJet"); fluteNoise = get ("fluteNoise");
        brassLip = get ("brassLip"); brassSlide = get ("brassSlide");
        bandedShape = get ("bandedShape"); bandedExcite = get ("bandedExcite");
        shakerType = get ("shakerType"); shakerObjects = get ("shakerObjects"); shakerShake = get ("shakerShake"); shakerTone = get ("shakerTone");
        malletHardness = get ("malletHardness"); malletMass = get ("malletMass"); malletDamp = get ("malletDamp");
        for (int m = 0; m < ModMatrix::numSlots; ++m)
        {
            mtxSrc[(size_t) m] = get ("mtxSrc" + juce::String (m + 1));
            mtxDst[(size_t) m] = get ("mtxDst" + juce::String (m + 1));
            mtxAmt[(size_t) m] = get ("mtxAmt" + juce::String (m + 1));
        }
        mtxRandomFade = get ("mtxRandomFade");
        osc1Mute = get ("osc1Mute"); wtMute = get ("wtMute"); sampleMute = get ("sampleMute"); stringMute = get ("stringMute");
        osc2Mute = get ("osc2Mute"); osc3Mute = get ("osc3Mute"); noiseMute = get ("noiseMute");
        mdOutput = get ("mdOutput"); snesOutput = get ("snesOutput"); chipOutput = get ("chipOutput");
        n64Source = get ("n64Source"); n64SourceRate = get ("n64SourceRate"); n64OutputRate = get ("n64OutputRate"); n64Adpcm = get ("n64Adpcm"); n64Dac = get ("n64Dac");
        n64Attack = get ("n64Attack"); n64Decay = get ("n64Decay"); n64Release = get ("n64Release"); n64AttackVolume = get ("n64AttackVolume");
        n64DecayVolume = get ("n64DecayVolume"); n64Volume = get ("n64Volume"); n64Pan = get ("n64Pan"); n64Spread = get ("n64Spread");
        n64Fx = get ("n64Fx"); n64FxMix = get ("n64FxMix"); n64Velocity = get ("n64Velocity");
        bendRange = get ("bendRange"); modWheelDepth = get ("modWheelDepth");
        atFilter = get ("atFilter"); atVibrato = get ("atVibrato"); atAmp = get ("atAmp");
        formantOn = get ("formantOn"); formantVoice = get ("formantVoice"); formantVowel = get ("formantVowel");
        formantShift = get ("formantShift"); formantRate = get ("formantRate"); formantDepth = get ("formantDepth"); formantMix = get ("formantMix");
        voiceMode = get ("voiceMode"); glide = get ("glide"); pitchEnvAmount = get ("pitchEnvAmount"); pitchEnvDecay = get ("pitchEnvDecay");
        seqOn = get ("seqOn"); seqSteps = get ("seqSteps"); seqRate = get ("seqRate"); seqGate = get ("seqGate"); seqLatch = get ("seqLatch");
        for (int i = 0; i < 16; ++i)
        {
            stepNote[(size_t) i] = get (("stepNote" + juce::String (i + 1)).toRawUTF8());
            stepOn[(size_t) i]   = get (("stepOn" + juce::String (i + 1)).toRawUTF8());
            stepTie[(size_t) i]  = get (("stepTie" + juce::String (i + 1)).toRawUTF8());
        }
        keyTranspose = get ("keyTranspose"); scaleMode = get ("scaleMode"); scaleRoot = get ("scaleRoot"); scaleType = get ("scaleType");
        scaleRound = get ("scaleRound");
        arpOn = get ("arpOn"); arpMode = get ("arpMode"); arpRate = get ("arpRate"); arpOctaves = get ("arpOctaves");
        arpGate = get ("arpGate"); arpLatch = get ("arpLatch"); arpBpm = get ("arpBpm"); arpOneShot = get ("arpOneShot");
        wave = get ("wave"); attack = get ("attack"); decay = get ("decay"); sustain = get ("sustain"); release = get ("release"); envCurve = get ("envCurve");
        drift = get ("drift");
        unisonVoices = get ("unisonVoices"); unisonDetune = get ("unisonDetune"); unisonSpread = get ("unisonSpread");
        voicePan = get ("voicePan"); voicePanMode = get ("voicePanMode");
        osc1Level = get ("osc1Level"); pulseWidth = get ("pulseWidth"); pwmDepth = get ("pwmDepth");
        osc2Wave = get ("osc2Wave"); osc2Octave = get ("osc2Octave"); osc2Semi = get ("osc2Semi"); osc2Fine = get ("osc2Fine");
        osc2Level = get ("osc2Level"); osc2Sync = get ("osc2Sync"); subLevel = get ("subLevel"); osc3Octave = get ("osc3Octave");
        osc2WtPosition = get ("osc2WtPosition"); osc2WtTable = get ("osc2WtTable"); osc3Wave = get ("osc3Wave"); osc3Semi = get ("osc3Semi"); osc3Fine = get ("osc3Fine");
        osc1TriSym = get ("osc1TriSym"); osc1TriRound = get ("osc1TriRound"); osc2TriSym = get ("osc2TriSym"); osc2TriRound = get ("osc2TriRound");
        osc3TriSym = get ("osc3TriSym"); osc3TriRound = get ("osc3TriRound");
        noiseLevel = get ("noiseLevel"); noiseColor = get ("noiseColor");
        wtLevel = get ("wtLevel"); wtTable = get ("wtTable"); wtPosition = get ("wtPosition");
        stringLevel = get ("stringLevel"); stringDecay = get ("stringDecay"); stringBright = get ("stringBright"); stringPick = get ("stringPick");
        sampleLevel = get ("sampleLevel"); sampleLength = get ("sampleLength"); sampleRoot = get ("sampleRoot"); sampleStart = get ("sampleStart"); sampleLoop = get ("sampleLoop");
        inputGain = get ("inputGain"); inputMonitor = get ("inputMonitor");
        vocoderOn = get ("vocoderOn"); vocoderBank = get ("vocoderBank"); vocoderMix = get ("vocoderMix");
        vocoderAttack = get ("vocoderAttack"); vocoderRelease = get ("vocoderRelease"); vocoderFormant = get ("vocoderFormant");
        vocoderSibilance = get ("vocoderSibilance");
        filterSustain = get ("filterSustain"); filterRelease = get ("filterRelease"); velToFilter = get ("velToFilter");
        chorusOn = get ("chorusOn"); chorusMode = get ("chorusMode"); chorusMix = get ("chorusMix"); chorusDepth = get ("chorusDepth");
        mediaOn = get ("mediaOn"); mediaType = get ("mediaType"); mediaAge = get ("mediaAge");
        mediaNoise = get ("mediaNoise"); mediaWow = get ("mediaWow"); mediaDrive = get ("mediaDrive");
        filterOn = get ("filterOn"); filterMode = get ("filterMode"); cutoff = get ("cutoff"); resonance = get ("resonance");
        filterDrive = get ("filterDrive"); filterEnv = get ("filterEnv"); filterAttack = get ("filterAttack");
        filterDecay = get ("filterDecay"); filterKey = get ("filterKey");
        tubeOn = get ("tubeOn"); tubeDrive = get ("tubeDrive"); tubeBias = get ("tubeBias");
        eqOn = get ("eqOn"); eqLowCut = get ("eqLowCut"); eqLowCutSlope = get ("eqLowCutSlope"); eqLowFreq = get ("eqLowFreq"); eqLowGain = get ("eqLowGain"); eqMid1Freq = get ("eqMid1Freq"); eqMid1Gain = get ("eqMid1Gain"); eqMid1Q = get ("eqMid1Q"); eqMid2Freq = get ("eqMid2Freq"); eqMid2Gain = get ("eqMid2Gain"); eqMid2Q = get ("eqMid2Q"); eqHighFreq = get ("eqHighFreq"); eqHighGain = get ("eqHighGain"); eqHighCut = get ("eqHighCut"); eqHighCutSlope = get ("eqHighCutSlope"); eqOutput = get ("eqOutput");
        { const char* ids[6] = { "eqLowCutOn", "eqLowOn", "eqMid1On", "eqMid2On", "eqHighOn", "eqHighCutOn" }; for (size_t b = 0; b < 6; ++b) eqBandOn[b] = get (ids[b]); }
        transOn = get ("transOn"); transAttack = get ("transAttack"); transSustain = get ("transSustain"); compOn = get ("compOn"); compMode = get ("compMode"); compThreshold = get ("compThreshold"); compRatio = get ("compRatio"); compAttack = get ("compAttack"); compRelease = get ("compRelease"); compKnee = get ("compKnee"); compMakeup = get ("compMakeup"); compMix = get ("compMix"); compScHpf = get ("compScHpf"); compSource = get ("compSource"); limOn = get ("limOn"); limCeiling = get ("limCeiling"); limRelease = get ("limRelease"); limLookahead = get ("limLookahead");
        ampOn = get ("ampOn"); ampModel = get ("ampModel"); ampGain = get ("ampGain"); ampBass = get ("ampBass"); ampMid = get ("ampMid"); ampTreble = get ("ampTreble"); ampPresence = get ("ampPresence"); ampMaster = get ("ampMaster"); ampSag = get ("ampSag"); ampOutput = get ("ampOutput"); cabType = get ("cabType"); cabMix = get ("cabMix");
        phaserOn = get ("phaserOn"); phaserRate = get ("phaserRate"); phaserDepth = get ("phaserDepth");
        phaserFeedback = get ("phaserFeedback"); phaserMix = get ("phaserMix");
        crushOn = get ("crushOn"); crushBits = get ("crushBits"); crushRate = get ("crushRate"); crushMix = get ("crushMix"); crushDrive = get ("crushDrive");
        engine = get ("engine"); thereminRange = get ("thereminRange"); thereminGlide = get ("thereminGlide");
        thereminTone = get ("thereminTone"); thereminPitch = get ("thereminPitch"); thereminVolume = get ("thereminVolume");
        thereminModel = get ("thereminModel"); thereminField = get ("thereminField"); thereminLock = get ("thereminLock");
        thereminZero = get ("thereminZero");
        vl1Sound = get ("vl1Sound"); vl1Octave = get ("vl1Octave"); vl1Attack = get ("vl1Attack");
        vl1Decay = get ("vl1Decay"); vl1SustainLevel = get ("vl1SustainLevel"); vl1SustainTime = get ("vl1SustainTime");
        vl1Release = get ("vl1Release"); vl1Vibrato = get ("vl1Vibrato"); vl1Tremolo = get ("vl1Tremolo");
        vl1Rhythm = get ("vl1Rhythm"); vl1Tempo = get ("vl1Tempo"); vl1Balance = get ("vl1Balance");
        styloModel = get ("styloModel"); styloVibrato = get ("styloVibrato"); styloVibDepth = get ("styloVibDepth");
        styloTuning = get ("styloTuning"); styloOutput = get ("styloOutput"); styloClick = get ("styloClick");
        omniSustain = get ("omniSustain"); omniHarpLevel = get ("omniHarpLevel"); omniChordLevel = get ("omniChordLevel");
        omniBassLevel = get ("omniBassLevel"); omniHarpOctave = get ("omniHarpOctave"); omniSplit = get ("omniSplit");
        omniRhythm = get ("omniRhythm"); omniTempo = get ("omniTempo"); omniRhythmLevel = get ("omniRhythmLevel");
        drumKit = get ("drumKit"); drumPattern = get ("drumPattern"); drumPattern2 = get ("drumPattern2"); drumTempo = get ("drumTempo"); drumSwing = get ("drumSwing");
        drumTune = get ("drumTune"); drumDecay = get ("drumDecay"); drumTone = get ("drumTone"); drumSnappy = get ("drumSnappy");
        drumAccent = get ("drumAccent"); drumVelocity = get ("drumVelocity");
        chipModel = get ("chipModel"); chipVoice = get ("chipVoice"); chipDuty = get ("chipDuty"); chipVolume = get ("chipVolume");
        chipEnvelope = get ("chipEnvelope"); chipEnvRate = get ("chipEnvRate"); chipRelease = get ("chipRelease");
        chipSweep = get ("chipSweep"); chipSweepRate = get ("chipSweepRate"); chipArp = get ("chipArp"); chipArpSpeed = get ("chipArpSpeed");
        chipVibDepth = get ("chipVibDepth"); chipVibRate = get ("chipVibRate"); chipVibDelay = get ("chipVibDelay");
        chipNoiseMode = get ("chipNoiseMode"); chipWave = get ("chipWave"); chipWaveLevel = get ("chipWaveLevel");
        chipVelocity = get ("chipVelocity"); chipFilter = get ("chipFilter"); chipDpcm = get ("chipDpcm"); chipDpcmLoop = get ("chipDpcmLoop");
        fmMode = get ("fmMode"); fmAlgorithm = get ("fmAlgorithm");
        pvVoice = get ("pvVoice"); pvWave1 = get ("pvWave1"); pvRange1 = get ("pvRange1"); pvWave2 = get ("pvWave2"); pvRange2 = get ("pvRange2");
        pvDetune = get ("pvDetune"); pvFm = get ("pvFm"); pvGlide = get ("pvGlide"); pvDrift = get ("pvDrift");
        pvMix1 = get ("pvMix1"); pvMix2 = get ("pvMix2"); pvNoise = get ("pvNoise"); pvFilterMode = get ("pvFilterMode");
        pvCutoff = get ("pvCutoff"); pvReso = get ("pvReso"); pvEnvAmount = get ("pvEnvAmount");
        pvFAttack = get ("pvFAttack"); pvFDecay = get ("pvFDecay"); pvFSustain = get ("pvFSustain"); pvFRelease = get ("pvFRelease"); pvFLoop = get ("pvFLoop");
        pvAAttack = get ("pvAAttack"); pvADecay = get ("pvADecay"); pvASustain = get ("pvASustain"); pvARelease = get ("pvARelease"); pvALoop = get ("pvALoop");
        pvLfoWave = get ("pvLfoWave"); pvLfoRate = get ("pvLfoRate"); pvLfoVco1 = get ("pvLfoVco1"); pvLfoVco2 = get ("pvLfoVco2");
        pvLfoVcf = get ("pvLfoVcf"); pvLfoVca = get ("pvLfoVca");
        ansScore = get ("ansScore"); ansSpeed = get ("ansSpeed"); ansHold = get ("ansHold"); ansEmulsion = get ("ansEmulsion");
        for (int b = 0; b < 20; ++b) ansReg[(size_t) b] = get ("ansReg" + juce::String (b + 1));
        flangerOn = get ("flangerOn"); flangerRate = get ("flangerRate"); flangerRange = get ("flangerRange"); flangerColor = get ("flangerColor");
        flangerMatrix = get ("flangerMatrix"); flangerMix = get ("flangerMix");
        fshiftOn = get ("fshiftOn"); fshiftHz = get ("fshiftHz"); fshiftBalance = get ("fshiftBalance"); fshiftMix = get ("fshiftMix");
        leslieOn = get ("leslieOn"); leslieSpeed = get ("leslieSpeed"); leslieDrive = get ("leslieDrive"); leslieMix = get ("leslieMix");
        springOn = get ("springOn"); springDecay = get ("springDecay"); springMix = get ("springMix"); fmFeedback = get ("fmFeedback"); fmLfoWave = get ("fmLfoWave"); fmLfoSpeed = get ("fmLfoSpeed");
        fmLfoDelay = get ("fmLfoDelay"); fmLfoPmd = get ("fmLfoPmd"); fmLfoAmd = get ("fmLfoAmd"); fmLfoPms = get ("fmLfoPms");
        fmTranspose = get ("fmTranspose"); fmOscSync = get ("fmOscSync"); fmLfoSync = get ("fmLfoSync");
        for (int k = 0; k < 4; ++k)
        {
            fmPitchRate[(size_t) k] = get ("fmPR" + juce::String (k + 1));
            fmPitchLevel[(size_t) k] = get ("fmPL" + juce::String (k + 1));
        }
        mdModel = get ("mdModel"); mdAlgorithm = get ("mdAlgorithm"); mdFeedback = get ("mdFeedback"); mdLfo = get ("mdLfo");
        mdPms = get ("mdPms"); mdAms = get ("mdAms"); mdVelocity = get ("mdVelocity"); mdPan = get ("mdPan");
        styloRange = get ("styloRange"); styloRegister = get ("styloRegister");
        genxPwm = get ("genxPwm"); genxSub1 = get ("genxSub1"); genxSub2 = get ("genxSub2"); genxAttack = get ("genxAttack"); genxDecay = get ("genxDecay"); genxPitch = get ("genxPitch"); genxLfoWave = get ("genxLfoWave"); genxLfoRate = get ("genxLfoRate"); genxLfoDepth = get ("genxLfoDepth"); genxCutoff = get ("genxCutoff"); genxRes = get ("genxRes"); genxDelay = get ("genxDelay"); genxTime = get ("genxTime"); genxRepeats = get ("genxRepeats"); genxLevel = get ("genxLevel"); genxStrip = get ("genxStrip"); genxOctave = get ("genxOctave");
        midiInChannel = get ("midiInChannel");
        chipExpansion = get ("chipExpansion"); chipVrc6Duty = get ("chipVrc6Duty"); chipVrc6Overdrive = get ("chipVrc6Overdrive");
        opllInstrument = get ("opllInstrument"); opllVolume = get ("opllVolume"); opllSustain = get ("opllSustain"); opllRhythm = get ("opllRhythm");
        oplModel = get ("oplModel"); oplFourOp = get ("oplFourOp"); oplAlgorithm = get ("oplAlgorithm"); oplFeedback = get ("oplFeedback");
        oplDeepTremolo = get ("oplDeepTremolo"); oplDeepVibrato = get ("oplDeepVibrato"); oplVelocity = get ("oplVelocity");
        oplPan = get ("oplPan"); oplOutput = get ("oplOutput");
        mt32Model = get ("mt32Model"); mt32Mode = get ("mt32Mode"); mt32Multi = get ("mt32Multi"); mt32Program = get ("mt32Program"); mt32Reverb = get ("mt32Reverb");
        mt32Dac = get ("mt32Dac"); mt32NiceAmp = get ("mt32NiceAmp"); mt32NicePan = get ("mt32NicePan"); mt32NiceMix = get ("mt32NiceMix");
        for (int k = 0; k < 4; ++k)
        {
            const auto pre = "opl" + juce::String (k + 1);
            auto& o = oplOp[(size_t) k];
            o.am = get (pre + "Am"); o.vib = get (pre + "Vib"); o.egt = get (pre + "Egt"); o.ksr = get (pre + "Ksr"); o.mult = get (pre + "Mult");
            o.ksl = get (pre + "Ksl"); o.tl = get (pre + "Tl"); o.ar = get (pre + "Ar"); o.dr = get (pre + "Dr"); o.sl = get (pre + "Sl");
            o.rr = get (pre + "Rr"); o.wave = get (pre + "Wave");
        }
        mdVoice = get ("mdVoice"); mdPsgNoise = get ("mdPsgNoise"); mdPsgNoiseRate = get ("mdPsgNoiseRate"); mdPsgAttack = get ("mdPsgAttack");
        mdPsgDecay = get ("mdPsgDecay"); mdPsgSustain = get ("mdPsgSustain"); mdPsgRelease = get ("mdPsgRelease"); mdDacRate = get ("mdDacRate");
        for (int k = 0; k < 3; ++k)
            mdCh3Op[(size_t) k] = get ("mdCh3Op" + juce::String (k + 1));
        snesPan = get ("snesPan"); snesSpread = get ("snesSpread");
        for (int k = 0; k < 4; ++k)
            chipPan[(size_t) k] = get ("chipPan" + juce::String (k + 1));
        snesSource = get ("snesSource"); snesUserRate = get ("snesUserRate"); snesInterp = get ("snesInterp"); snesEnvMode = get ("snesEnvMode");
        snesAttack = get ("snesAttack"); snesDecay = get ("snesDecay"); snesSustainLevel = get ("snesSustainLevel"); snesSustainRate = get ("snesSustainRate");
        snesGainRate = get ("snesGainRate"); snesDirectGain = get ("snesDirectGain"); snesNoise = get ("snesNoise"); snesNoiseRate = get ("snesNoiseRate");
        snesVolume = get ("snesVolume"); snesMainVolume = get ("snesMainVolume"); snesVelocity = get ("snesVelocity"); snesEcho = get ("snesEcho");
        snesEdl = get ("snesEdl"); snesEfb = get ("snesEfb"); snesEvol = get ("snesEvol"); snesFir = get ("snesFir"); snesFilter = get ("snesFilter"); snesDac = get ("snesDac");
        for (int op = 0; op < 4; ++op)
        {
            const auto pre = "md" + juce::String (op + 1);
            auto& o = mdOp[(size_t) op];
            o.tl = get (pre + "Tl"); o.ar = get (pre + "Ar"); o.dr = get (pre + "Dr"); o.sr = get (pre + "Sr"); o.sl = get (pre + "Sl");
            o.rr = get (pre + "Rr"); o.mul = get (pre + "Mul"); o.dt = get (pre + "Dt"); o.ks = get (pre + "Ks"); o.am = get (pre + "Am");
            o.ssg = get (pre + "Ssg");
        }
        for (int op = 0; op < 6; ++op)
        {
            const auto pre = "fm" + juce::String (op + 1);
            auto& o = fmOp[(size_t) op];
            for (int k = 0; k < 4; ++k)
            {
                o.rate[(size_t) k] = get (pre + "R" + juce::String (k + 1));
                o.level[(size_t) k] = get (pre + "L" + juce::String (k + 1));
            }
            o.coarse = get (pre + "Coarse"); o.fine = get (pre + "Fine"); o.detune = get (pre + "Detune"); o.fixed = get (pre + "Fixed");
            o.output = get (pre + "Level"); o.velSens = get (pre + "Vel"); o.rateScale = get (pre + "RateScale"); o.ams = get (pre + "Ams");
            o.breakPoint = get (pre + "Break"); o.leftDepth = get (pre + "LDepth"); o.rightDepth = get (pre + "RDepth");
            o.leftCurve = get (pre + "LCurve"); o.rightCurve = get (pre + "RCurve");
        }
        lfoOn = get ("lfoOn"); lfoTarget = get ("lfoTarget"); lfoRate = get ("lfoRate"); lfoDepth = get ("lfoDepth");
        lfoShape = get ("lfoShape"); lfoSync = get ("lfoSync"); lfo2On = get ("lfo2On"); lfo2Target = get ("lfo2Target");
        lfo2Shape = get ("lfo2Shape"); lfo2Rate = get ("lfo2Rate"); lfo2Sync = get ("lfo2Sync"); lfo2Depth = get ("lfo2Depth");
        fineTune = get ("fineTune");
        echoOn = get ("echoOn"); echoTime = get ("echoTime"); echoFeedback = get ("echoFeedback"); echoMix = get ("echoMix");
        echoTone = get ("echoTone"); echoWow = get ("echoWow"); echoSync = get ("echoSync"); echoPingPong = get ("echoPingPong");
        ambientOn = get ("ambientOn"); ambientSize = get ("ambientSize"); ambientMix = get ("ambientMix");
        reverbDecay = get ("reverbDecay"); reverbPredelay = get ("reverbPredelay"); reverbDiffusion = get ("reverbDiffusion");
        reverbDamp = get ("reverbDamp"); reverbTone = get ("reverbTone"); reverbMod = get ("reverbMod");
        reverbWidth = get ("reverbWidth"); reverbFreeze = get ("reverbFreeze");
        complexOn = get ("complexOn"); modWave = get ("modWave"); modRatio = get ("modRatio"); fmIndex = get ("fmIndex");
        amDepth = get ("amDepth"); timbre = get ("timbre"); symmetry = get ("symmetry"); timbreMod = get ("timbreMod"); modLp = get ("modLp");
        modSync = get ("modSync"); complexModSource = get ("complexModSource");
        osc2SampleRoot = get ("osc2SampleRoot"); osc2SampleStart = get ("osc2SampleStart"); osc2SampleLoop = get ("osc2SampleLoop"); osc2SampleLength = get ("osc2SampleLength");
        dpcmSampleStart = get ("dpcmSampleStart"); dpcmSampleLength = get ("dpcmSampleLength");
        mdSampleRoot = get ("mdSampleRoot"); mdSampleStart = get ("mdSampleStart"); mdSampleLoop = get ("mdSampleLoop"); mdSampleLength = get ("mdSampleLength");
        snesSampleRoot = get ("snesSampleRoot"); snesSampleStart = get ("snesSampleStart"); snesSampleLoop = get ("snesSampleLoop"); snesSampleLength = get ("snesSampleLength");
        n64SampleRoot = get ("n64SampleRoot"); n64SampleStart = get ("n64SampleStart"); n64SampleLoop = get ("n64SampleLoop"); n64SampleLength = get ("n64SampleLength");
        osc3Sync = get ("osc3Sync"); osc3WtPosition = get ("osc3WtPosition"); osc3WtTable = get ("osc3WtTable");
        osc3SampleRoot = get ("osc3SampleRoot"); osc3SampleStart = get ("osc3SampleStart"); osc3SampleLoop = get ("osc3SampleLoop"); osc3SampleLength = get ("osc3SampleLength");
    }

    // Interpretación (no son parámetros): los escribe processBlock desde el MIDI entrante (pitch bend en semitonos ya
    // escalado por Bend Range; rueda de modulación 0..1). Mismo hilo que las voces.
    std::atomic<float> pitchBend { 0.0f }, modWheel { 0.0f };
    std::atomic<float> stripTouch { 0.0f }; // tira de expresión del Gen X-2 en el pad del Stylophone (-1..1, 0 = sin tocar)
    // Calidad del ladder del Synth aplicada (0 Classic 1x, 1 Balanced 2x + ADAA, 2 High Quality 4x). No es un parámetro: la
    // escribe el hilo de audio solo cuando no suena ninguna voz del Synth (todas las voces usan siempre el mismo modo)
    std::atomic<int> ladderQuality { 0 };
    // Frecuencia de la última nota pulsada (origen del glide); la escriben las voces en startNote (hilo de audio)
    mutable std::atomic<double> lastNoteHz { 0.0 };
    mutable RandomClock randomClock; // Dsp/RandomClock.h: hilo de audio
    // Voice Pan Alternate: contador de notas de esta instancia (antes, un static compartido por todas). Hilo de audio; vuelve a
    // 0 con la reanclada de la aleatoriedad (render reproducible)
    mutable unsigned panAlternate = 0;
    std::array<std::atomic<float>*, ModMatrix::numSlots> mtxSrc {}, mtxDst {}, mtxAmt {}; // matriz de modulación (Dsp/ModMatrix.h)
    std::atomic<float>* mtxRandomFade {}; // Random de la matriz × (1 - f + f·envolvente de filtro)
    std::array<std::atomic<float>*, 6> eqBandOn {}; // bypass por etapa del EQ (Effects/Equalizer.h, Eq::bypass)
    std::atomic<float>* mpeOn {}, *mpeBendRange {}, *voiceAssign {}, *oscPhase {}, *voiceTolerance {}, *vcaClick {}, *vcaLeak {}, *voiceSatMode {}, *voiceSatDrive {}, *voiceSatPos {}, *filterCleanDrive {}, *filterSpread {}, *stringPickup {}, *stringDamping {}, *stringExcite {};
    std::atomic<float>* stringBowPressure {}, *stringHammerFelt {}, *stringEbowDrive {}, *stringBuzz {}, *stringHarmonic {}, *stringStiff {}, *stringTension {};
    std::atomic<float>* reedEmbouchure {}, *reedNoise {}, *fluteJet {}, *fluteNoise {}, *brassLip {}, *brassSlide {};
    std::atomic<float>* bandedShape {}, *bandedExcite {}, *shakerType {}, *shakerObjects {}, *shakerShake {}, *shakerTone {};
    std::atomic<float>* malletHardness {}, *malletMass {}, *malletDamp {};
    // Matriz: desplazamiento por destino de la última voz renderizada (para los anillos de la UI) y contador de bloques con voces
    mutable std::array<std::atomic<float>, ModMatrix::numDestinations> liveMod {};
    mutable std::atomic<unsigned> liveModTick { 0 };
    // Por voz física (cardIndex 0..7): desplazamientos de la matriz al final de su último bloque y un contador que avanza en
    // cada bloque que suena (la UI dibuja un punto por voz viva en los mandos destino). Fuentes de la última voz que sonó
    // (brillo de las lámparas piloto de los mandos)
    static constexpr int liveVoices = 8;
    mutable std::array<std::array<std::atomic<float>, ModMatrix::numDestinations>, liveVoices> liveModVoice {};
    mutable std::array<std::atomic<unsigned>, liveVoices> liveVoiceTick {};
    mutable std::array<std::atomic<float>, ModMatrix::numSources> liveSrc {};
    std::array<std::atomic<float>, 16> channelTimbre {}; // último CC74 de cada canal (0..1): lo hereda una nota nueva (MPE)
    std::atomic<float> *voiceMode {}, *glide {}, *pitchEnvAmount {}, *pitchEnvDecay {};
    std::atomic<float> *lfoShape {}, *lfoSync {}, *lfo2On {}, *lfo2Target {}, *lfo2Shape {}, *lfo2Rate {}, *lfo2Sync {}, *lfo2Depth {}, *fineTune {};
    // Tempo para los LFO y el eco sincronizados: el del host si lo da, si no Arp BPM. Lo escribe processBlock
    std::atomic<double> tempoBpm { 120.0 };
    // Tempo que muestra el LED del editor: host si lo da; si no, el reloj del motor activo (Drum/Omnichord/VL-1) o Arp BPM
    std::atomic<float> displayBpm { 120.0f };
    std::atomic<bool> displayBpmFromHost { false };

    // Hz de un LFO: Free -> rate; sync -> una figura (1/4 = 1 negra) al tempo actual
    double lfoHz (int sync, float rate) const
    {
        static constexpr double beats[] = { 0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return sync <= 0 || sync > 10 ? (double) rate : tempoBpm.load() / 60.0 / beats[sync];
    }

    std::atomic<float> *bendRange {}, *modWheelDepth {}, *atFilter {}, *atVibrato {}, *atAmp {};
    std::atomic<float> *formantOn {}, *formantVoice {}, *formantVowel {}, *formantShift {}, *formantRate {}, *formantDepth {}, *formantMix {};
    std::atomic<float> *seqOn {}, *seqSteps {}, *seqRate {}, *seqGate {}, *seqLatch {};
    std::array<std::atomic<float>*, 16> stepNote {}, stepOn {}, stepTie {};
    struct FmOp
    {
        std::array<std::atomic<float>*, 4> rate {}, level {};
        std::atomic<float> *coarse {}, *fine {}, *detune {}, *fixed {}, *output {}, *velSens {}, *rateScale {}, *ams {},
                           *breakPoint {}, *leftDepth {}, *rightDepth {}, *leftCurve {}, *rightCurve {};
    };
    std::array<FmOp, 6> fmOp {}; // índice 0 = OP1
    std::atomic<float> *fmTranspose {}, *fmOscSync {}, *fmLfoSync {};
    std::array<std::atomic<float>*, 4> fmPitchRate {}, fmPitchLevel {}; // pitch EG del DX7
    struct MdOp { std::atomic<float> *tl {}, *ar {}, *dr {}, *sr {}, *sl {}, *rr {}, *mul {}, *dt {}, *ks {}, *am {}, *ssg {}; };
    std::array<MdOp, 4> mdOp {}; // índice 0 = OP1
    std::atomic<float> *mdModel {}, *mdAlgorithm {}, *mdFeedback {}, *mdLfo {}, *mdPms {}, *mdAms {}, *mdVelocity {}, *mdPan {};
    std::atomic<float> *styloRange {}, *styloRegister {};
    std::atomic<float> *genxPwm {}, *genxSub1 {}, *genxSub2 {}, *genxAttack {}, *genxDecay {}, *genxPitch {}, *genxLfoWave {}, *genxLfoRate {}, *genxLfoDepth {}, *genxCutoff {}, *genxRes {}, *genxDelay {}, *genxTime {}, *genxRepeats {}, *genxLevel {}, *genxStrip {}, *genxOctave {};
    struct OplOp { std::atomic<float> *am {}, *vib {}, *egt {}, *ksr {}, *mult {}, *ksl {}, *tl {}, *ar {}, *dr {}, *sl {}, *rr {}, *wave {}; };
    std::array<OplOp, 4> oplOp {}; // índice 0 = OP1
    std::atomic<float> *midiInChannel {};
    std::atomic<float> *chipExpansion {}, *chipVrc6Duty {}, *chipVrc6Overdrive {};
    std::atomic<float> *opllInstrument {}, *opllVolume {}, *opllSustain {}, *opllRhythm {};
    std::atomic<float> *oplModel {}, *oplFourOp {}, *oplAlgorithm {}, *oplFeedback {}, *oplDeepTremolo {}, *oplDeepVibrato {},
                       *oplVelocity {}, *oplPan {}, *oplOutput {},
                       *mt32Model {}, *mt32Mode {}, *mt32Multi {}, *mt32Program {}, *mt32Reverb {}, *mt32Dac {}, *mt32NiceAmp {}, *mt32NicePan {}, *mt32NiceMix {};
    std::atomic<float> *mdVoice {}, *mdPsgNoise {}, *mdPsgNoiseRate {}, *mdPsgAttack {}, *mdPsgDecay {}, *mdPsgSustain {}, *mdPsgRelease {}, *mdDacRate {};
    std::array<std::atomic<float>*, 3> mdCh3Op {};
    std::atomic<float> *snesPan {}, *snesSpread {};
    std::array<std::atomic<float>*, 4> chipPan {};
    std::atomic<float> *snesSource {}, *snesUserRate {}, *snesInterp {}, *snesEnvMode {}, *snesAttack {}, *snesDecay {}, *snesSustainLevel {},
                       *snesSustainRate {}, *snesGainRate {}, *snesDirectGain {}, *snesNoise {}, *snesNoiseRate {}, *snesVolume {},
                       *snesMainVolume {}, *snesVelocity {}, *snesEcho {}, *snesEdl {}, *snesEfb {}, *snesEvol {}, *snesFir {}, *snesFilter {}, *snesDac {};
    std::array<std::atomic<float>*, 20> ansReg {};
    std::atomic<float> *keyTranspose {}, *scaleMode {}, *scaleRoot {}, *scaleType {}, *scaleRound {};
    std::atomic<float> *arpOn {}, *arpMode {}, *arpRate {}, *arpOctaves {}, *arpGate {}, *arpLatch {}, *arpBpm {}, *arpOneShot {};
    std::atomic<float> *monoOut {}, *mdOutput {}, *snesOutput {}, *chipOutput {};
    std::atomic<float> *osc1Mute {}, *wtMute {}, *sampleMute {}, *stringMute {}, *osc2Mute {}, *osc3Mute {}, *noiseMute {};
    // Solo de las pestañas de fuentes, compartido por los dos grupos: -1 ninguno; 0 Osc 1, 1 Wavetable, 2 Sample, 3 String,
    // 4 Osc 2, 5 Osc 3, 6 Noise. Transitorio: no es un parámetro, no se guarda y no toca los Mute (el motor lo combina con ellos)
    mutable std::atomic<int> soloSource { -1 };
    // Medidores del Mixer (UI/MixerPanel.h): pico de cada fuente (índice SourceIndex) en la voz, tras su nivel y antes del
    // filtro, el máximo entre voces. Las voces lo suben (máximo) y la UI lo lee y lo pone a 0
    mutable std::array<std::atomic<float>, 7> sourcePeak {};
    enum SourceIndex { srcOsc1 = 0, srcWavetable, srcSample, srcString, srcOsc2, srcOsc3, srcNoise };
    std::atomic<float> *n64Source {}, *n64SourceRate {}, *n64OutputRate {}, *n64Adpcm {}, *n64Dac {}, *n64Attack {}, *n64Decay {}, *n64Release {},
                       *n64AttackVolume {}, *n64DecayVolume {}, *n64Volume {}, *n64Pan {}, *n64Spread {}, *n64Fx {}, *n64FxMix {}, *n64Velocity {};
    std::atomic<float> *gain {}, *wave {}, *attack {}, *decay {}, *sustain {}, *release {}, *envCurve {}, *drift {},
                       *unisonVoices {}, *unisonDetune {}, *unisonSpread {}, *voicePan {}, *voicePanMode {},
                       *osc1Level {}, *pulseWidth {}, *pwmDepth {}, *osc2Wave {}, *osc2Octave {}, *osc2Semi {}, *osc2Fine {},
                       *osc2Level {}, *osc2Sync {}, *subLevel {}, *osc3Octave {}, *noiseLevel {}, *noiseColor {}, *osc2WtPosition {}, *osc2WtTable {}, *osc3Wave {}, *osc3Semi {}, *osc3Fine {},
                       *osc1TriSym {}, *osc1TriRound {}, *osc2TriSym {}, *osc2TriRound {}, *osc3TriSym {}, *osc3TriRound {},
                       *wtLevel {}, *wtTable {}, *wtPosition {},
                       *stringLevel {}, *stringDecay {}, *stringBright {}, *stringPick {},
                       *sampleLevel {}, *sampleLength {}, *sampleRoot {}, *sampleStart {}, *sampleLoop {}, *inputGain {}, *inputMonitor {},
                       *vocoderOn {}, *vocoderBank {}, *vocoderMix {}, *vocoderAttack {}, *vocoderRelease {},
                       *vocoderFormant {}, *vocoderSibilance {},
                       *filterSustain {}, *filterRelease {}, *velToFilter {},
                       *chorusOn {}, *chorusMode {}, *chorusMix {}, *chorusDepth {},
                       *mediaOn {}, *mediaType {}, *mediaAge {}, *mediaNoise {}, *mediaWow {}, *mediaDrive {},
                       *filterOn {}, *filterMode {}, *cutoff {}, *resonance {}, *filterDrive {},
                       *filterEnv {}, *filterAttack {}, *filterDecay {}, *filterKey {},
                       *tubeOn {}, *tubeDrive {}, *tubeBias {}, *eqOn {}, *eqLowCut {}, *eqLowCutSlope {}, *eqLowFreq {}, *eqLowGain {}, *eqMid1Freq {}, *eqMid1Gain {}, *eqMid1Q {}, *eqMid2Freq {}, *eqMid2Gain {}, *eqMid2Q {}, *eqHighFreq {}, *eqHighGain {}, *eqHighCut {}, *eqHighCutSlope {}, *eqOutput {}, *transOn {}, *transAttack {}, *transSustain {}, *compOn {}, *compMode {}, *compThreshold {}, *compRatio {}, *compAttack {}, *compRelease {}, *compKnee {}, *compMakeup {}, *compMix {}, *compScHpf {}, *compSource {}, *limOn {}, *limCeiling {}, *limRelease {}, *limLookahead {}, *ampOn {}, *ampModel {}, *ampGain {}, *ampBass {}, *ampMid {}, *ampTreble {}, *ampPresence {}, *ampMaster {}, *ampSag {}, *ampOutput {}, *cabType {}, *cabMix {}, *crushOn {}, *crushBits {}, *crushRate {}, *crushMix {}, *crushDrive {},
                       *phaserOn {}, *phaserRate {}, *phaserDepth {}, *phaserFeedback {}, *phaserMix {},
                       *engine {}, *thereminRange {}, *thereminGlide {}, *thereminTone {}, *thereminPitch {}, *thereminVolume {},
                       *thereminModel {}, *thereminField {}, *thereminLock {}, *thereminZero {},
                       *vl1Sound {}, *vl1Octave {}, *vl1Attack {}, *vl1Decay {}, *vl1SustainLevel {},
                       *vl1SustainTime {}, *vl1Release {}, *vl1Vibrato {}, *vl1Tremolo {}, *vl1Rhythm {}, *vl1Tempo {},
                       *vl1Balance {},
                       *styloModel {}, *styloVibrato {}, *styloVibDepth {}, *styloTuning {}, *styloOutput {}, *styloClick {},
                       *omniSustain {}, *omniHarpLevel {}, *omniChordLevel {}, *omniBassLevel {}, *omniHarpOctave {}, *omniSplit {},
                       *omniRhythm {}, *omniTempo {}, *omniRhythmLevel {},
                       *drumKit {}, *drumPattern {}, *drumPattern2 {}, *drumTempo {}, *drumSwing {}, *drumTune {}, *drumDecay {}, *drumTone {},
                       *drumSnappy {}, *drumAccent {}, *drumVelocity {},
                       *chipModel {}, *chipVoice {}, *chipDuty {}, *chipVolume {}, *chipEnvelope {}, *chipEnvRate {}, *chipRelease {},
                       *chipSweep {}, *chipSweepRate {}, *chipArp {}, *chipArpSpeed {}, *chipVibDepth {}, *chipVibRate {}, *chipVibDelay {},
                       *chipNoiseMode {}, *chipWave {}, *chipWaveLevel {}, *chipVelocity {}, *chipFilter {}, *chipDpcm {}, *chipDpcmLoop {},
                       *fmMode {}, *fmAlgorithm {}, *fmFeedback {},
                       *pvVoice {}, *pvWave1 {}, *pvRange1 {}, *pvWave2 {}, *pvRange2 {}, *pvDetune {}, *pvFm {}, *pvGlide {}, *pvDrift {},
                       *pvMix1 {}, *pvMix2 {}, *pvNoise {}, *pvFilterMode {}, *pvCutoff {}, *pvReso {}, *pvEnvAmount {},
                       *pvFAttack {}, *pvFDecay {}, *pvFSustain {}, *pvFRelease {}, *pvFLoop {},
                       *pvAAttack {}, *pvADecay {}, *pvASustain {}, *pvARelease {}, *pvALoop {},
                       *pvLfoWave {}, *pvLfoRate {}, *pvLfoVco1 {}, *pvLfoVco2 {}, *pvLfoVcf {}, *pvLfoVca {},
                       *ansScore {}, *ansSpeed {}, *ansHold {}, *ansEmulsion {},
                       *flangerOn {}, *flangerRate {}, *flangerRange {}, *flangerColor {}, *flangerMatrix {}, *flangerMix {},
                       *fshiftOn {}, *fshiftHz {}, *fshiftBalance {}, *fshiftMix {},
                       *leslieOn {}, *leslieSpeed {}, *leslieDrive {}, *leslieMix {}, *springOn {}, *springDecay {}, *springMix {}, *fmLfoWave {}, *fmLfoSpeed {}, *fmLfoDelay {}, *fmLfoPmd {}, *fmLfoAmd {}, *fmLfoPms {},
                       *lfoOn {}, *lfoTarget {}, *lfoRate {}, *lfoDepth {},
                       *echoOn {}, *echoTime {}, *echoFeedback {}, *echoMix {}, *echoTone {}, *echoWow {}, *echoSync {}, *echoPingPong {},
                       *ambientOn {}, *ambientSize {}, *ambientMix {},
                       *reverbDecay {}, *reverbPredelay {}, *reverbDiffusion {}, *reverbDamp {}, *reverbTone {},
                       *reverbMod {}, *reverbWidth {}, *reverbFreeze {},
                       *complexOn {}, *modWave {}, *modRatio {}, *fmIndex {}, *amDepth {}, *timbre {}, *symmetry {},
                       *timbreMod {}, *modLp {}, *modSync {}, *complexModSource {}, *osc2SampleRoot {}, *osc2SampleStart {}, *osc2SampleLoop {}, *osc2SampleLength {},
                       *dpcmSampleStart {}, *dpcmSampleLength {}, *mdSampleRoot {}, *mdSampleStart {}, *mdSampleLoop {}, *mdSampleLength {},
                       *snesSampleRoot {}, *snesSampleStart {}, *snesSampleLoop {}, *snesSampleLength {},
                       *n64SampleRoot {}, *n64SampleStart {}, *n64SampleLoop {}, *n64SampleLength {},
                       *osc3Sync {}, *osc3WtPosition {}, *osc3WtTable {}, *osc3SampleRoot {}, *osc3SampleStart {}, *osc3SampleLoop {}, *osc3SampleLength {};
};

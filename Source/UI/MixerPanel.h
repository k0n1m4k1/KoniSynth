// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, Params, Skin, KoniSynthProcessor). No es una cabecera independiente.
#pragma once

//==============================================================================
// Mixer del Synth (diálogo "Mixer..." de Master): una tira por fuente y Master aparte.
//   Fader en dB sobre el mismo parámetro lineal del nivel (g = 10^(dB/20): 100 % = 0 dB, 50 % = -6,02 dB, 0 = silencio):
//   no cambia el sonido ni los presets, solo la escala y el gesto. Ley del fader: lineal en dB de -60 a 0 dB y -inf abajo.
//   Mute (el parámetro de la fuente) y Solo (Params::soloSource, sin guardar: el mismo de las pestañas de fuentes).
//   Medidor: pico en dBFS de la señal de la fuente en la voz (tras su nivel, antes del filtro y la saturación; la voz más
//   fuerte), no la ganancia del fader. Bajar una fuente cambia cuánto excita el filtro y la saturación (el timbre);
//   Master (Gain, al final de la cadena) solo cambia el volumen de salida, y su medidor es el pico de la salida.
//   Grupos por familia: Osc 1 (Osc 1, Wavetable, Sample, Math Model), Osc 2 (Osc 2, su WT y su Sample), Osc 3 (igual) y Noise.
//   Bajo el nombre, lo que toca cada tira: forma, tabla o fichero (los de Osc 2 y Osc 3, propios: en el acento).
//   LINK (por familia de oscilador): mover un fader deja los demás de su grupo en la misma posición (el mismo nivel), también
//   los que estaban en -inf. Se guarda con el proyecto (propiedad mixerLinks del estado, bits 0..2), no en los presets ni
//   como parámetro
class MixerPanel : public juce::Component, private juce::Timer
{
public:
    explicit MixerPanel (KoniSynthProcessor& p) : processor (p)
    {
        struct Def { const char* name; const char* levelId; const char* muteId; int source; int group; };
        static const Def defs[] = {
            { "Osc 1", "osc1Level", "osc1Mute", Params::srcOsc1, 0 }, { "Wavetable", "wtLevel", "wtMute", Params::srcWavetable, 0 },
            { "Sample", "sampleLevel", "sampleMute", Params::srcSample, 0 }, { "Math Model", "stringLevel", "stringMute", Params::srcString, 0 },
            { "Osc 2", "osc2Level", "osc2Mute", Params::srcOsc2, 1 }, { "Osc 2 WT", "osc2WtLevel", "osc2WtMute", Params::srcOsc2Wt, 1 },
            { "Osc 2 Sample", "osc2SampleLevel", "osc2SampleMute", Params::srcOsc2Sample, 1 },
            { "Osc 3", "subLevel", "osc3Mute", Params::srcOsc3, 2 }, { "Osc 3 WT", "osc3WtLevel", "osc3WtMute", Params::srcOsc3Wt, 2 },
            { "Osc 3 Sample", "osc3SampleLevel", "osc3SampleMute", Params::srcOsc3Sample, 2 },
            { "Noise", "noiseLevel", "noiseMute", Params::srcNoise, 3 } };
        for (const auto& d : defs)
            strips.push_back (std::make_unique<Strip> (*this, d.name, d.levelId, d.muteId, d.source, d.group));
        strips.push_back (std::make_unique<Strip> (*this, "Master", "gain", nullptr, -1, numGroups));
        for (auto& s : strips) addAndMakeVisible (*s);

        const auto links = (int) processor.apvts.state.getProperty (linksProp, 0);
        for (int g = 0; g < numLinkable; ++g)
        {
            auto& b = linkButtons[(size_t) g];
            b.setButtonText ("LINK");
            b.setClickingTogglesState (true);
            b.setToggleState ((links >> g) & 1, juce::dontSendNotification);
            b.setColour (juce::TextButton::buttonOnColourId, Skin::accent().withAlpha (0.8f));
            b.setTooltip ("Link: moving any fader of this group sets the others to the same level. Saved with the project.");
            b.onClick = [this]
            {
                auto mask = 0;
                for (int k = 0; k < numLinkable; ++k)
                    mask |= linkButtons[(size_t) k].getToggleState() ? 1 << k : 0;
                processor.apvts.state.setProperty (linksProp, mask, nullptr);
            };
            addAndMakeVisible (b);
        }

        hint.setText ("Faders in dB (gain of each source before the filter; Master = output volume). Meters: peak in dBFS. "
                      "M = mute, S = solo (not saved). LINK: the group moves as one fader. Double-click a fader: default.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
        hint.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (hint);
        startTimerHz (60);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        hint.setBounds (r.removeFromBottom (22));
        r.removeFromTop (groupH + 4); // rótulos de grupo y LINK
        // Master con el ancho de una tira más media de separación; groupGap entre grupos
        const auto w = (r.getWidth() - numGroups * groupGap) * 2 / (2 * (int) strips.size() + 1);
        for (size_t k = 0; k + 1 < strips.size(); ++k)
        {
            if (k > 0 && strips[k]->group != strips[k - 1]->group)
                r.removeFromLeft (groupGap);
            strips[k]->setBounds (r.removeFromLeft (w).reduced (3, 0));
        }
        r.removeFromLeft (groupGap + w / 2);
        strips.back()->setBounds (r.removeFromLeft (w).reduced (3, 0));
        for (int g = 0; g < numLinkable; ++g) // LINK a la derecha del rótulo de su grupo
        {
            const auto [from, to] = groupRange (g);
            linkButtons[(size_t) g].setBounds (strips[to]->getRight() - 44, 8, 44, groupH);
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (strips.size() < 2)
            return;
        // Rótulos de grupo con su línea (sin el hueco del LINK), y separadores entre grupos y antes de Master
        const auto caption = [&] (size_t from, size_t to, const juce::String& text, bool link)
        {
            const auto x0 = (float) strips[from]->getX(), x1 = (float) strips[to]->getRight() - (link ? 50.0f : 0.0f);
            const auto y = 8.0f + (float) groupH * 0.5f;
            g.setColour (Skin::white().withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
            const auto tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12.0f;
            const auto cx = (x0 + x1) * 0.5f;
            g.drawText (text, juce::Rectangle<float> (cx - tw * 0.5f, 8.0f, tw, (float) groupH), juce::Justification::centred);
            g.setColour (Skin::white().withAlpha (0.18f));
            g.drawHorizontalLine (juce::roundToInt (y), x0, cx - tw * 0.5f);
            g.drawHorizontalLine (juce::roundToInt (y), cx + tw * 0.5f, x1);
        };
        static const char* names[] = { "OSC 1", "OSC 2", "OSC 3", "NOISE", "OUT" };
        for (int grp = 0; grp <= numGroups; ++grp)
        {
            const auto [from, to] = groupRange (grp);
            caption (from, to, names[grp], grp < numLinkable);
        }
        g.setColour (Skin::white().withAlpha (0.15f));
        for (size_t k = 1; k < strips.size(); ++k)
            if (strips[k]->group != strips[k - 1]->group)
            {
                const auto x = (float) (strips[k - 1]->getRight() + strips[k]->getX()) * 0.5f;
                g.drawVerticalLine (juce::roundToInt (x), 8.0f + (float) groupH, (float) getHeight() - 30.0f);
            }
    }

    // Posición del fader (0..1) <-> ganancia lineal. dB = 60·(p - 1) para p > 0; p = 0 = silencio
    static float gainFromPos (double p) { return p <= 0.0005 ? 0.0f : (float) std::pow (10.0, 3.0 * (p - 1.0)); }
    static double posFromGain (float g) { return g <= 1.0e-3f ? 0.0 : juce::jlimit (0.0, 1.0, 1.0 + std::log10 ((double) g) / 3.0); }
    static juce::String dbText (float g) { return g <= 0.0f ? juce::String ("-inf") : juce::String (20.0f * std::log10 (g), 1) + " dB"; }

private:
    static constexpr int numGroups = 4, numLinkable = 3; // Osc 1, Osc 2, Osc 3 (con LINK) y Noise; Master aparte (grupo 4)
    static inline const juce::Identifier linksProp { "mixerLinks" };

    // Medidor vertical: -60..0 dBFS, verde hasta -12, ámbar hasta -3, rojo arriba. Refresco de 60 Hz; sube al instante y cae
    // a 60 dB/s (de 0 a -60 dB en 1 s)
    struct Meter : juce::Component
    {
        float db = -100.0f;
        void set (float peak)
        {
            const auto now = peak > 0.0f ? 20.0f * std::log10 (peak) : -100.0f;
            const auto next = std::max (now, db - 60.0f / 60.0f); // 60 dB/s a 60 Hz
            if (std::abs (next - db) > 0.05f) { db = next; repaint(); }
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Skin::black().withAlpha (0.5f));
            g.fillRoundedRectangle (r, 2.0f);
            const auto frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            auto bar = r.reduced (2.0f);
            bar = bar.removeFromBottom (bar.getHeight() * frac);
            const auto colour = db > -3.0f ? juce::Colour (0xffe04030) : db > -12.0f ? juce::Colour (0xffe0b040) : juce::Colour (0xff50c070);
            g.setColour (colour);
            g.fillRect (bar);
        }
    };

    struct Strip : juce::Component, private juce::Slider::Listener
    {
        Strip (MixerPanel& o, const char* label, const char* levelId, const char* muteId, int src, int grp)
            : owner (o), source (src), group (grp), name (label)
        {
            auto& apvts = owner.processor.apvts;
            param = apvts.getParameter (levelId);
            fader.setSliderStyle (juce::Slider::LinearVertical);
            fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            fader.setRange (0.0, 1.0);
            fader.setDoubleClickReturnValue (true, posFromGain (param->convertFrom0to1 (param->getDefaultValue())));
            fader.setTooltip (juce::String (label) + (src >= 0 ? ": level of this source in dB (before the filter and saturation)."
                                                               : ": output volume in dB (Gain, last in the chain)."));
            fader.addListener (this);
            fader.onDragStart = [this] { dragging = true; attachment->beginGesture(); owner.beginLinked (*this); };
            fader.onDragEnd = [this] { attachment->endGesture(); dragging = false; owner.endLinked (*this); };
            addAndMakeVisible (fader);
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { fader.setValue (posFromGain (v), juce::dontSendNotification); update(); });
            attachment->sendInitialUpdate();
            title.setText (label, juce::dontSendNotification);
            title.setJustificationType (juce::Justification::centred);
            title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            title.setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (title);
            playing.setJustificationType (juce::Justification::centred);
            playing.setFont (juce::FontOptions (11.0f));
            playing.setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (playing);
            value.setJustificationType (juce::Justification::centred);
            value.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (value);
            addAndMakeVisible (meter);
            if (muteId != nullptr)
            {
                mute.setButtonText ("M");
                mute.setClickingTogglesState (true);
                mute.setTooltip ("Mute this source (saved with the preset).");
                muteAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, muteId, mute);
                addAndMakeVisible (mute);
                solo.setButtonText ("S");
                solo.setClickingTogglesState (true);
                solo.setTooltip ("Solo: only this source sounds (not saved; the same Solo as the source tabs).");
                solo.onClick = [this] { owner.processor.getParams().soloSource.store (solo.getToggleState() ? source : -1); };
                addAndMakeVisible (solo);
            }
        }

        void sliderValueChanged (juce::Slider*) override
        {
            // Arrastre: dentro del gesto; doble clic o rueda: un paso de deshacer completo. Con LINK, el grupo le sigue
            if (dragging) attachment->setValueAsPartOfGesture (gainFromPos (fader.getValue()));
            else attachment->setValueAsCompleteGesture (gainFromPos (fader.getValue()));
            owner.followLinked (*this, fader.getValue());
            update();
        }

        void update() { value.setText (dbText (param->convertFrom0to1 (param->getValue())), juce::dontSendNotification); }

        void resized() override
        {
            auto r = getLocalBounds();
            title.setBounds (r.removeFromTop (20));
            playing.setBounds (r.removeFromTop (16));
            value.setBounds (r.removeFromBottom (18));
            if (mute.isVisible() || solo.isVisible())
            {
                auto b = r.removeFromBottom (26).reduced (2, 2);
                mute.setBounds (b.removeFromLeft (b.getWidth() / 2).reduced (1, 0));
                solo.setBounds (b.reduced (1, 0));
            }
            meter.setBounds (r.removeFromRight (10).reduced (0, 8));
            fader.setBounds (r);
        }

        MixerPanel& owner;
        const int source, group;
        juce::String name;
        juce::RangedAudioParameter* param = nullptr;
        bool dragging = false;
        juce::Slider fader;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::Label title, value, playing; // playing: lo que toca la tira (MixerPanel::describe)
        Meter meter;
        juce::TextButton mute, solo;
        std::unique_ptr<APVTS::ButtonAttachment> muteAttachment;
    };

    // Tiras [from, to] de un grupo (el orden de defs los deja contiguos)
    std::pair<size_t, size_t> groupRange (int grp) const
    {
        size_t from = strips.size(), to = 0;
        for (size_t k = 0; k < strips.size(); ++k)
            if (strips[k]->group == grp) { from = std::min (from, k); to = k; }
        return { from, to };
    }

    bool linked (int grp) const { return grp < numLinkable && linkButtons[(size_t) grp].getToggleState(); }

    // LINK: al empezar un arrastre, las demás tiras del grupo abren su gesto (un paso de deshacer por parámetro) y, mientras dura,
    // toman la posición de la arrastrada; doble clic o rueda, igual, como un cambio completo
    void beginLinked (Strip& dragged)
    {
        if (! linked (dragged.group)) return;
        for (auto& s : strips)
            if (s.get() != &dragged && s->group == dragged.group)
                s->attachment->beginGesture();
    }
    void endLinked (Strip& dragged)
    {
        if (! linked (dragged.group)) return;
        for (auto& s : strips)
            if (s.get() != &dragged && s->group == dragged.group)
                s->attachment->endGesture();
    }
    void followLinked (Strip& moved, double position)
    {
        if (! linked (moved.group) || following) return;
        const juce::ScopedValueSetter<bool> guard (following, true); // los cambios de las demás no rebotan
        const auto gain = gainFromPos (position);
        for (auto& s : strips)
        {
            if (s.get() == &moved || s->group != moved.group) continue;
            if (moved.dragging) s->attachment->setValueAsPartOfGesture (gain);
            else                s->attachment->setValueAsCompleteGesture (gain);
        }
    }

    void timerCallback() override
    {
        auto& prm = processor.getParams();
        const auto soloed = prm.soloSource.load();
        if (++describeTick >= 12) // 5 Hz: nombres de tabla y de fichero
        {
            describeTick = 0;
            for (auto& s : strips)
                describe (*s);
        }
        for (auto& s : strips)
        {
            if (s->source >= 0)
            {
                s->meter.set (prm.sourcePeak[(size_t) s->source].exchange (0.0f, std::memory_order_relaxed));
                s->solo.setToggleState (soloed == s->source, juce::dontSendNotification);
                s->setAlpha (soloed >= 0 && soloed != s->source ? 0.5f : 1.0f); // silenciada por el Solo de otra
            }
            else
            {
                // Salida: pico de los últimos 960 valores del búfer del display (20 ms a 48 kHz: algo más que un refresco)
                const auto end = processor.scopeWritePos.load (std::memory_order_acquire);
                constexpr auto mask = KoniSynthProcessor::scopeSize - 1;
                float peak = 0.0f;
                for (int i = 0; i < 960; ++i)
                {
                    const auto idx = (size_t) ((end - 960 + i) & mask);
                    peak = std::max ({ peak, std::abs (processor.scopeLeft[idx].load (std::memory_order_relaxed)),
                                       std::abs (processor.scopeRight[idx].load (std::memory_order_relaxed)) });
                }
                s->meter.set (peak);
            }
        }
    }

    // Lo que toca cada tira: su forma, su tabla o su fichero. Los de Osc 2 y Osc 3 son propios (no las fuentes Wavetable /
    // Sample de Osc 1): en el acento
    void describe (Strip& s)
    {
        auto& apvts = processor.apvts;
        const auto text = [&apvts] (const char* id) { auto* p = apvts.getParameter (id); return p != nullptr ? p->getCurrentValueAsText() : juce::String(); };
        const auto fileName = [] (const juce::String& info) { return info.isNotEmpty() ? info : juce::String ("no file"); };
        juce::String what;
        auto own = false;
        switch (s.source)
        {
            case Params::srcWavetable:  what = processor.getWavetableName (KoniSynthProcessor::wtMain); break;
            case Params::srcSample:     what = fileName (processor.getSampleInfo (KoniSynthProcessor::sourceSample)); break;
            case Params::srcOsc1:       what = text ("wave"); break;
            case Params::srcOsc2:       what = text ("osc2Wave"); break;
            case Params::srcOsc3:       what = text ("osc3Wave"); break;
            case Params::srcNoise:      what = text ("noiseColor"); break;
            case Params::srcOsc2Wt:     own = true; what = processor.getWavetableName (KoniSynthProcessor::wtOsc2); break;
            case Params::srcOsc3Wt:     own = true; what = processor.getWavetableName (KoniSynthProcessor::wtOsc3); break;
            case Params::srcOsc2Sample: own = true; what = fileName (processor.getSampleInfo (KoniSynthProcessor::osc2Sample)); break;
            case Params::srcOsc3Sample: own = true; what = fileName (processor.getSampleInfo (KoniSynthProcessor::osc3Sample)); break;
            default: break;
        }
        if (s.playing.getText() != what)
        {
            s.playing.setText (what, juce::dontSendNotification);
            s.playing.setTooltip (what);
        }
        s.playing.setColour (juce::Label::textColourId, own ? Skin::accent() : Skin::white().withAlpha (0.6f));
    }

    static constexpr int groupH = 18, groupGap = 14;
    int describeTick = 12;
    bool following = false;
    KoniSynthProcessor& processor;
    std::vector<std::unique_ptr<Strip>> strips;
    std::array<juce::TextButton, numLinkable> linkButtons;
    juce::Label hint;
};

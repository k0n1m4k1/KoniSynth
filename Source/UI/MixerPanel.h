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
class MixerPanel : public juce::Component, private juce::Timer
{
public:
    explicit MixerPanel (KoniSynthProcessor& p) : processor (p)
    {
        struct Def { const char* name; const char* levelId; const char* muteId; int source; };
        static const Def defs[] = { { "Osc 1", "osc1Level", "osc1Mute", Params::srcOsc1 }, { "Osc 2", "osc2Level", "osc2Mute", Params::srcOsc2 },
                                    { "Osc 3", "subLevel", "osc3Mute", Params::srcOsc3 }, { "Noise", "noiseLevel", "noiseMute", Params::srcNoise },
                                    { "Wavetable", "wtLevel", "wtMute", Params::srcWavetable }, { "Sample", "sampleLevel", "sampleMute", Params::srcSample },
                                    { "Math", "stringLevel", "stringMute", Params::srcString } };
        for (const auto& d : defs)
            strips.push_back (std::make_unique<Strip> (*this, d.name, d.levelId, d.muteId, d.source));
        strips.push_back (std::make_unique<Strip> (*this, "Master", "gain", nullptr, -1));
        for (auto& s : strips) addAndMakeVisible (*s);
        hint.setText ("Faders in dB (gain of each source before the filter; Master = output volume). Meters: peak in dBFS. "
                      "M = mute, S = solo (not saved). Double-click a fader: default.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.6f));
        addAndMakeVisible (hint);
        startTimerHz (30);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        hint.setBounds (r.removeFromBottom (22));
        const auto w = r.getWidth() / ((int) strips.size() + 1); // Master con el ancho de dos tiras de separación
        for (size_t k = 0; k + 1 < strips.size(); ++k)
            strips[k]->setBounds (r.removeFromLeft (w).reduced (3, 0));
        r.removeFromLeft (w / 2);
        strips.back()->setBounds (r.removeFromLeft (w).reduced (3, 0));
    }

    void paint (juce::Graphics& g) override
    {
        // Separador antes de Master
        if (strips.size() > 1)
        {
            const auto x = (float) (strips[strips.size() - 2]->getRight() + strips.back()->getX()) * 0.5f;
            g.setColour (Skin::white().withAlpha (0.15f));
            g.drawVerticalLine (juce::roundToInt (x), 8.0f, (float) getHeight() - 30.0f);
        }
    }

    // Posición del fader (0..1) <-> ganancia lineal. dB = 60·(p - 1) para p > 0; p = 0 = silencio
    static float gainFromPos (double p) { return p <= 0.0005 ? 0.0f : (float) std::pow (10.0, 3.0 * (p - 1.0)); }
    static double posFromGain (float g) { return g <= 1.0e-3f ? 0.0 : juce::jlimit (0.0, 1.0, 1.0 + std::log10 ((double) g) / 3.0); }
    static juce::String dbText (float g) { return g <= 0.0f ? juce::String ("-inf") : juce::String (20.0f * std::log10 (g), 1) + " dB"; }

private:
    // Medidor vertical: -60..0 dBFS, verde hasta -12, ámbar hasta -3, rojo arriba; pico con caída de 20 dB/s
    struct Meter : juce::Component
    {
        float db = -100.0f;
        void set (float peak)
        {
            const auto now = peak > 0.0f ? 20.0f * std::log10 (peak) : -100.0f;
            const auto next = std::max (now, db - 20.0f / 30.0f); // 30 Hz
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
        Strip (MixerPanel& o, const char* label, const char* levelId, const char* muteId, int src)
            : owner (o), source (src), name (label)
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
            fader.onDragStart = [this] { dragging = true; attachment->beginGesture(); };
            fader.onDragEnd = [this] { attachment->endGesture(); dragging = false; };
            addAndMakeVisible (fader);
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { fader.setValue (posFromGain (v), juce::dontSendNotification); update(); });
            attachment->sendInitialUpdate();
            title.setText (label, juce::dontSendNotification);
            title.setJustificationType (juce::Justification::centred);
            title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            addAndMakeVisible (title);
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
            // Arrastre: dentro del gesto; doble clic o rueda: un paso de deshacer completo
            if (dragging) attachment->setValueAsPartOfGesture (gainFromPos (fader.getValue()));
            else attachment->setValueAsCompleteGesture (gainFromPos (fader.getValue()));
            update();
        }

        void update() { value.setText (dbText (param->convertFrom0to1 (param->getValue())), juce::dontSendNotification); }

        void resized() override
        {
            auto r = getLocalBounds();
            title.setBounds (r.removeFromTop (20));
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
        const int source;
        juce::String name;
        juce::RangedAudioParameter* param = nullptr;
        bool dragging = false;
        juce::Slider fader;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::Label title, value;
        Meter meter;
        juce::TextButton mute, solo;
        std::unique_ptr<APVTS::ButtonAttachment> muteAttachment;
    };

    void timerCallback() override
    {
        auto& prm = processor.getParams();
        const auto soloed = prm.soloSource.load();
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
                // Salida: pico de los últimos 1600 valores del búfer del display (33 ms a 48 kHz)
                const auto end = processor.scopeWritePos.load (std::memory_order_acquire);
                constexpr auto mask = KoniSynthProcessor::scopeSize - 1;
                float peak = 0.0f;
                for (int i = 0; i < 1600; ++i)
                {
                    const auto idx = (size_t) ((end - 1600 + i) & mask);
                    peak = std::max ({ peak, std::abs (processor.scopeLeft[idx].load (std::memory_order_relaxed)),
                                       std::abs (processor.scopeRight[idx].load (std::memory_order_relaxed)) });
                }
                s->meter.set (peak);
            }
        }
    }

    KoniSynthProcessor& processor;
    std::vector<std::unique_ptr<Strip>> strips;
    juce::Label hint;
};

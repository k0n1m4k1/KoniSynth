// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Panel del ANS: la placa (720 tonos × 512 mm) se pinta con el ratón como se raspaba la masilla: botón izquierdo añade, derecho
// borra; la raya vertical es el cursor de lectura. Debajo, los 20 registros de media octava
class AnsPanel : public juce::Component, private juce::Timer
{
public:
    explicit AnsPanel (KoniSynthProcessor& p) : koni (p)
    {
        auto& s = p.apvts;
        header.withCombo (s, "ansScore").withExtraToggle (s, "ansHold", "Hold")
              .withKnob (s, "ansSpeed", "Speed").withKnob (s, "ansEmulsion", "Emulsion");
        addAndMakeVisible (header);
        for (int b = 0; b < 20; ++b)
        {
            regs.push_back (std::make_unique<Knob> (s, "ansReg" + juce::String (b + 1), juce::String (b + 1), Knob::Style::vertical));
            addAndMakeVisible (*regs.back());
        }
        hint.setText ("Left drag: scratch (sound). Right drag: fill (silence). Vertical: pitch, 72 steps per octave. Horizontal: time.",
                      juce::dontSendNotification);
        addAndMakeVisible (hint);
        rebuildImage();
        startTimerHz (25);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Skin::black());
        g.fillRect (plate);
        g.drawImage (image, plate.toFloat());
        const auto col = koni.getAnsColumn();
        const auto x = plate.getX() + (float) col / AnsScore::kColumns * plate.getWidth();
        g.setColour (Skin::c (0xffe04040));
        g.drawVerticalLine ((int) x, (float) plate.getY(), (float) plate.getBottom());
        g.setColour (Skin::white().withAlpha (0.25f));
        for (int oct = 1; oct < 10; ++oct) // líneas de octava (cada 72 tonos)
        {
            const auto y = plate.getBottom() - (float) oct * 72 / AnsScore::kRows * plate.getHeight();
            g.drawHorizontalLine ((int) y, (float) plate.getX(), (float) plate.getRight());
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header.setBounds (r.removeFromTop (120));
        hint.setBounds (r.removeFromTop (22));
        auto regRow = r.removeFromBottom (130);
        const auto w = regRow.getWidth() / 20;
        for (auto& k : regs) k->setBounds (regRow.removeFromLeft (w));
        plate = r.reduced (4);
    }

    void mouseDown (const juce::MouseEvent& e) override { edit = std::make_unique<AnsScore> (koni.copyAnsScore()); scratch (e); }
    void mouseDrag (const juce::MouseEvent& e) override { scratch (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (edit != nullptr)
            koni.setAnsUserScore (std::move (edit));
        rebuildImage();
    }

private:
    void scratch (const juce::MouseEvent& e)
    {
        if (edit == nullptr || ! plate.contains (e.getPosition()))
            return;
        const auto col = (int) ((e.position.x - plate.getX()) / plate.getWidth() * AnsScore::kColumns);
        const auto row = (int) ((plate.getBottom() - e.position.y) / plate.getHeight() * AnsScore::kRows);
        const auto erase = e.mods.isRightButtonDown();
        for (int dc = -2; dc <= 2; ++dc)
            for (int dr = -2; dr <= 2; ++dr)
            {
                const auto c = col + dc, r = row + dr;
                if (c < 0 || c >= AnsScore::kColumns || r < 0 || r >= AnsScore::kRows) continue;
                const auto v = (int) (255.0 * std::max (0.0, 1.0 - std::hypot (dc, dr) / 3.0));
                auto& cell = edit->at (r, c);
                cell = (std::uint8_t) (erase ? std::max (0, (int) cell - v) : std::min (255, (int) cell + v));
                image.setPixelAt (c, AnsScore::kRows - 1 - r, juce::Colour::greyLevel (cell / 255.0f));
            }
        repaint (plate);
    }

    void rebuildImage()
    {
        const auto s = koni.copyAnsScore();
        image = juce::Image (juce::Image::RGB, AnsScore::kColumns, AnsScore::kRows, true);
        for (int r = 0; r < AnsScore::kRows; ++r)
            for (int c = 0; c < AnsScore::kColumns; ++c)
                image.setPixelAt (c, AnsScore::kRows - 1 - r, juce::Colour::greyLevel (s.at (r, c) / 255.0f));
        shownVersion = koni.getAnsVersion();
    }

    void timerCallback() override
    {
        if (edit == nullptr && koni.getAnsVersion() != shownVersion)
            rebuildImage();
        repaint (plate);
    }

    KoniSynthProcessor& koni;
    Section header { "ANS" };
    juce::Label hint;
    std::vector<std::unique_ptr<Knob>> regs;
    juce::Rectangle<int> plate;
    juce::Image image;
    std::unique_ptr<AnsScore> edit;
    int shownVersion = -1;
};

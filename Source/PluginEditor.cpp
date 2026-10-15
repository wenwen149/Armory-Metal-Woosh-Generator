#include "PluginEditor.h"

namespace
{
class ArmorySliderValueLabel final : public juce::Label,
                                     private juce::Timer
{
public:
    explicit ArmorySliderValueLabel (juce::Slider& sliderToControl)
        : slider (sliderToControl)
    {
        setJustificationType (juce::Justification::centred);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setInterceptsMouseClicks (true, false);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            return;

        stopTimer();
        dragStartY = event.getScreenPosition().y;
        dragStartValue = slider.getValue();
        isDragging = false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        const auto verticalDistance = std::abs (dragStartY - event.getScreenPosition().y);
        if (! isDragging && verticalDistance >= 2)
        {
            slider.startedDragging();
            isDragging = true;
        }

        if (! isDragging)
            return;

        const auto pixels = static_cast<double> (dragStartY - event.getScreenPosition().y);
        const auto fineScale = event.mods.isShiftDown() ? 0.1 : 1.0;
        const auto valuePerPixel = slider.getRange().getLength() / 180.0;
        slider.setValue (dragStartValue + pixels * valuePerPixel * fineScale, juce::sendNotificationSync);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (isDragging)
        {
            slider.stoppedDragging();
        }
        else if (suppressClickAfterDoubleClick)
        {
            suppressClickAfterDoubleClick = false;
        }
        else
        {
            // Delay single-click editing just long enough to distinguish it
            // from the double-click reset gesture.
            startTimer (250);
        }
        isDragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        stopTimer();
        suppressClickAfterDoubleClick = true;
        if (slider.isDoubleClickReturnEnabled())
            slider.setValue (slider.getDoubleClickReturnValue(), juce::sendNotificationSync);
    }

private:
    void timerCallback() override
    {
        stopTimer();
        slider.showTextBox();
    }

    juce::Slider& slider;
    int dragStartY = 0;
    double dragStartValue = 0.0;
    bool isDragging = false;
    bool suppressClickAfterDoubleClick = false;
};

class ArmoryLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ArmoryLookAndFeel()
    {
        setColour (juce::ComboBox::textColourId, juce::Colour (0xffd4af37));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff333333));
        setColour (juce::Label::textColourId, juce::Colour (0xffd4af37));
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff090909));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff303030));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        const auto centreY = static_cast<float> (y + height / 2);
        const auto startX = static_cast<float> (x + 3);
        const auto endX = static_cast<float> (x + width - 3);
        g.setColour (juce::Colour (0xff111111));
        g.fillRoundedRectangle (startX, centreY - 1.5f, endX - startX, 3.0f, 1.5f);
        g.setColour (juce::Colour (0xff365766));
        g.fillRoundedRectangle (startX, centreY - 1.5f, juce::jmax (0.0f, sliderPos - startX), 3.0f, 1.5f);
        g.setColour (juce::Colour (0xff6fa7bd));
        g.fillEllipse (sliderPos - 4.5f, centreY - 4.5f, 9.0f, 9.0f);
        g.setColour (juce::Colours::black);
        g.drawEllipse (sliderPos - 4.5f, centreY - 4.5f, 9.0f, 9.0f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float proportion, float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                     static_cast<float> (width), static_cast<float> (height)).reduced (3.0f);
        const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto knob = juce::Rectangle<float> (bounds.getCentreX() - diameter * 0.5f,
                                                  bounds.getCentreY() - diameter * 0.5f, diameter, diameter);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4a), knob.getCentreX(), knob.getCentreY(),
                                                 juce::Colour (0xff111111), knob.getRight(), knob.getBottom(), true));
        g.fillEllipse (knob);
        g.setColour (juce::Colours::black);
        g.drawEllipse (knob, 1.0f);
        // A conventional three-quarter turn: 225 degrees through 495 degrees.
        // Use one explicitly shared angle for the arc and pointer so their
        // endpoints cannot drift apart when JUCE changes a slider default.
        constexpr auto knobStartAngle = juce::MathConstants<float>::pi * 1.25f;
        constexpr auto knobEndAngle = juce::MathConstants<float>::pi * 2.75f;
        juce::ignoreUnused (startAngle, endAngle);
        const auto radius = diameter * 0.43f;
        const auto angle = knobStartAngle + proportion * (knobEndAngle - knobStartAngle);
        juce::Path arc;
        arc.addCentredArc (knob.getCentreX(), knob.getCentreY(), radius, radius, 0.0f, knobStartAngle, angle, true);
        g.setColour (juce::Colour (0xff6fa7bd));
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto pointerLength = diameter * 0.32f;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        // Path::addCentredArc measures zero from 12 o'clock, while the usual
        // cos/sin pair measures it from 3 o'clock.  Use the arc convention for
        // the pointer as well, so the pointer and yellow endpoint coincide.
        g.drawLine (knob.getCentreX(), knob.getCentreY(), knob.getCentreX() + std::sin (angle) * pointerLength,
                    knob.getCentreY() - std::cos (angle) * pointerLength, 1.8f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto shot = button.getButtonText().containsIgnoreCase ("SHOT");
        const auto accent = shot ? juce::Colour (0xff00d2ff) : juce::Colour (0xffd4af37);
        g.setColour (down ? accent : juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (highlighted || down ? accent : juce::Colour (0xff444444));
        g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down) override
    {
        if (button.getButtonText() == "RAND")
        {
            const auto die = button.getLocalBounds().toFloat().withSizeKeepingCentre (20.0f, 20.0f);
            g.setColour (highlighted || down ? juce::Colour (0xffd4af37) : juce::Colour (0xffaaaaaa));
            g.drawRoundedRectangle (die, 3.0f, 1.5f);
            constexpr std::array<juce::Point<float>, 5> pipPositions
            {{ { 0.28f, 0.28f }, { 0.72f, 0.28f }, { 0.50f, 0.50f }, { 0.28f, 0.72f }, { 0.72f, 0.72f } }};
            for (const auto& pip : pipPositions)
                g.fillEllipse (die.getX() + die.getWidth() * pip.x - 1.5f,
                               die.getY() + die.getHeight() * pip.y - 1.5f, 3.0f, 3.0f);
            return;
        }

        if (button.getButtonText() == "SYNC")
        {
            const auto centre = button.getLocalBounds().toFloat().getCentre();
            constexpr auto radius = 8.0f;
            const auto colour = button.getToggleState() ? juce::Colour (0xffd4af37)
                                                        : juce::Colour (0xffaaaaaa);
            g.setColour (colour);
            juce::Path arrows;
            arrows.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.2f, 2.8f, true);
            arrows.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 3.35f, 5.95f, true);
            g.strokePath (arrows, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            g.fillEllipse (centre.x + 5.0f, centre.y + 4.5f, 4.0f, 4.0f);
            g.fillEllipse (centre.x - 9.0f, centre.y - 8.5f, 4.0f, 4.0f);
            return;
        }

        LookAndFeel_V4::drawButtonText (g, button, highlighted, down);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool) override
    {
        const auto active = button.getToggleState();
        if (button.getButtonText() == "PWR")
        {
            const auto circle = juce::Rectangle<float> (3.0f, 2.0f, 15.0f, 15.0f);
            g.setColour (active ? juce::Colour (0xffd4af37) : juce::Colour (0xff444444));
            g.fillEllipse (circle);
            g.setColour (active ? juce::Colours::white : juce::Colour (0xff888888));
            g.drawEllipse (circle, 1.5f);
            g.setColour (juce::Colour (0xff777777));
            g.setFont (11.5f);
            g.drawText ("PWR", 21, 0, button.getWidth() - 21, button.getHeight(), juce::Justification::centredLeft);
            return;
        }

        if (button.getButtonText() == "SYNC")
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
            const auto accent = active ? juce::Colour (0xffd4af37) : juce::Colour (0xff888888);
            g.setColour (active ? juce::Colour (0xff2b2515) : juce::Colour (0xff151515));
            g.fillRoundedRectangle (bounds, 2.0f);
            g.setColour (active ? accent : juce::Colour (0xff444444));
            g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

            const auto centre = bounds.getCentre();
            constexpr auto radius = 7.0f;
            juce::Path arrows;
            arrows.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.2f, 2.8f, true);
            arrows.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 3.35f, 5.95f, true);
            g.setColour (accent);
            g.strokePath (arrows, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            g.fillEllipse (centre.x + 4.3f, centre.y + 4.0f, 3.8f, 3.8f);
            g.fillEllipse (centre.x - 8.1f, centre.y - 7.8f, 3.8f, 3.8f);
            return;
        }

        if (button.getButtonText() != "SHOT")
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
            const auto accent = active ? juce::Colour (0xffd4af37) : juce::Colour (0xff666666);
            g.setColour (active ? juce::Colour (0xff2b2515) : juce::Colour (0xff151515));
            g.fillRoundedRectangle (bounds, 2.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
            g.setColour (accent);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f, 4.0f);
        const auto accent = active ? juce::Colour (0xff00d2ff) : juce::Colour (0xffd4af37);
        g.setColour (juce::Colour (0xff111111));
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
        g.setColour (juce::Colour (0xff444444));
        g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.0f);
        const auto dotSize = bounds.getHeight() - 5.0f;
        const auto dotX = active ? bounds.getRight() - dotSize - 2.5f : bounds.getX() + 2.5f;
        g.setColour (accent);
        g.fillEllipse (dotX, bounds.getY() + 2.5f, dotSize, dotSize);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float> (0.5f, 0.5f, static_cast<float> (width - 1), static_cast<float> (height - 1));
        g.setColour (juce::Colour (0xff111111));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (juce::Colour (0xff333333));
        g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
        g.setColour (juce::Colour (0xffd4af37));
        juce::Path arrow;
        arrow.addTriangle (static_cast<float> (width - 16), height * 0.42f, static_cast<float> (width - 7), height * 0.42f,
                           static_cast<float> (width - 11.5f), height * 0.63f);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f, juce::Font::bold); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (13.0f, juce::Font::bold); }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = new ArmorySliderValueLabel (slider);
        label->setFont (juce::FontOptions (14.0f, juce::Font::bold));
        label->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
        label->setColour (juce::Label::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
        label->setColour (juce::Label::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
        label->setColour (juce::Label::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
        label->setColour (juce::TextEditor::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
        label->setColour (juce::TextEditor::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
        label->setColour (juce::TextEditor::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
        return label;
    }
};

ArmoryLookAndFeel armoryLookAndFeel;

bool isBlackKey (int midiNote)
{
    const auto noteInOctave = midiNote % 12;
    return noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 || noteInOctave == 8 || noteInOctave == 10;
}

int whiteKeyIndexFromC (int midiNote)
{
    constexpr std::array<int, 12> whiteKeyOffsets { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
    return (midiNote / 12) * 7 + whiteKeyOffsets[static_cast<size_t> (midiNote % 12)];
}
}

ArmoryPianoKeyboard::ArmoryPianoKeyboard()
{
    setSize (getRequiredWidth(), 90);
}

void ArmoryPianoKeyboard::setWhiteKeyWidth (float width)
{
    whiteKeyWidth = juce::jlimit (14.0f, 52.0f, width);
    setSize (getRequiredWidth(), getHeight());
    rebuildKeyBounds();
    repaint();
}

int ArmoryPianoKeyboard::getRequiredWidth() const noexcept
{
    return juce::roundToInt (75.0f * whiteKeyWidth);
}

void ArmoryPianoKeyboard::setLockedNotes (const ArmoryVSTAudioProcessor::LockedNoteArray& notes)
{
    lockedNotes = notes;
    repaint();
}

int ArmoryPianoKeyboard::xForMidiNote (int midiNote) const
{
    return juce::roundToInt (static_cast<float> (whiteKeyIndexFromC (juce::jlimit (0, 127, midiNote))) * whiteKeyWidth);
}

void ArmoryPianoKeyboard::rebuildKeyBounds()
{
    keyBounds.clear();
    constexpr auto firstNote = 0;
    constexpr auto lastNote = 127;
    const auto blackKeyWidth = whiteKeyWidth * 0.62f;

    for (int note = firstNote; note <= lastNote; ++note)
        if (! isBlackKey (note))
        {
            const auto x = static_cast<float> (whiteKeyIndexFromC (note)) * whiteKeyWidth;
            keyBounds.push_back ({ note, { x, 0.0f, whiteKeyWidth, static_cast<float> (getHeight()) }, false });
        }

    for (int note = firstNote; note <= lastNote; ++note)
        if (isBlackKey (note))
        {
            const auto x = static_cast<float> (whiteKeyIndexFromC (note)) * whiteKeyWidth - blackKeyWidth * 0.5f;
            keyBounds.push_back ({ note, { x, 0.0f, blackKeyWidth, static_cast<float> (getHeight()) * 0.62f }, true });
        }
}

void ArmoryPianoKeyboard::paint (juce::Graphics& graphics)
{
    rebuildKeyBounds();
    graphics.fillAll (juce::Colour (0xff0d0d0d));

    for (const auto& key : keyBounds)
        if (! key.isBlack)
        {
            graphics.setColour (juce::Colour (0xffe8e5dc));
            graphics.fillRect (key.bounds.reduced (0.5f));
            if (lockedNotes[static_cast<size_t> (key.midiNote)])
            {
                graphics.setColour (juce::Colour (0xffd4af37));
                graphics.fillRect (key.bounds.reduced (4.0f, 8.0f));
            }
            if (pressedNote == key.midiNote)
            {
                graphics.setColour (juce::Colour (0xff00d2ff));
                graphics.fillRect (key.bounds.reduced (7.0f, 13.0f));
            }
        }

    for (const auto& key : keyBounds)
        if (key.isBlack)
        {
            graphics.setColour (juce::Colour (0xff161616));
            graphics.fillRoundedRectangle (key.bounds.reduced (1.0f), 3.0f);
            if (lockedNotes[static_cast<size_t> (key.midiNote)])
            {
                graphics.setColour (juce::Colour (0xffb9952d));
                graphics.fillRoundedRectangle (key.bounds.reduced (4.0f, 7.0f), 2.0f);
            }
            if (pressedNote == key.midiNote)
            {
                graphics.setColour (juce::Colour (0xff00d2ff));
                graphics.fillRoundedRectangle (key.bounds.reduced (5.0f, 10.0f), 2.0f);
            }
        }
}

int ArmoryPianoKeyboard::noteAt (juce::Point<float> position) const
{
    for (auto iterator = keyBounds.rbegin(); iterator != keyBounds.rend(); ++iterator)
        if (iterator->bounds.contains (position))
            return iterator->midiNote;

    return -1;
}

void ArmoryPianoKeyboard::trigger (int midiNote, bool isNoteOn)
{
    if (onNote != nullptr && midiNote >= 0)
        onNote (midiNote, isNoteOn, false);
}

void ArmoryPianoKeyboard::mouseDown (const juce::MouseEvent& event)
{
    const auto midiNote = noteAt (event.position);
    if (midiNote < 0)
        return;

    if (event.mods.isRightButtonDown())
    {
        if (onToggleLock != nullptr)
            lockedNotes[static_cast<size_t> (midiNote)] = onToggleLock (midiNote);
        repaint();
        return;
    }

    pressedNote = midiNote;
    pressStartedMilliseconds = juce::Time::getMillisecondCounterHiRes();
    trigger (pressedNote, true);
    repaint();
}

void ArmoryPianoKeyboard::mouseDrag (const juce::MouseEvent& event)
{
    if (pressedNote < 0 || ! event.mods.isLeftButtonDown())
        return;

    const auto nextNote = noteAt (event.position);
    if (nextNote >= 0 && nextNote != pressedNote)
    {
        trigger (pressedNote, false);
        pressedNote = nextNote;
        pressStartedMilliseconds = juce::Time::getMillisecondCounterHiRes();
        trigger (pressedNote, true);
        repaint();
    }
}

void ArmoryPianoKeyboard::mouseUp (const juce::MouseEvent&)
{
    if (pressedNote >= 0)
    {
        const auto wasQuickTap = juce::Time::getMillisecondCounterHiRes() - pressStartedMilliseconds < 180.0;
        if (onNote != nullptr)
            onNote (pressedNote, false, wasQuickTap);
        pressedNote = -1;
        repaint();
    }
}

void ArmoryLayerSelector::setSelectedLayer (int layer)
{
    selectedLayer = juce::jlimit (0, 2, layer);
    repaint();
}

void ArmoryLayerSelector::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xff101010));
    const auto bounds = getLocalBounds().toFloat().reduced (5.0f);
    const auto railX = bounds.getX() + 7.0f;
    graphics.setColour (juce::Colour (0xff303030));
    graphics.fillRoundedRectangle (railX, bounds.getY() + 12.0f, 14.0f, bounds.getHeight() - 24.0f, 7.0f);

    const auto step = (bounds.getHeight() - 24.0f) / 2.0f;
    for (int layer = 0; layer < 3; ++layer)
    {
        const auto y = bounds.getY() + 12.0f + step * static_cast<float> (layer);
        graphics.setColour (layer == selectedLayer ? juce::Colour (0xffd4af37) : juce::Colour (0xff777777));
        graphics.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        graphics.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer)),
                           juce::Rectangle<float> (bounds.getX() + 25.0f, y - 9.0f, 14.0f, 18.0f), juce::Justification::centred);
    }

    const auto thumbY = bounds.getY() + 12.0f + step * static_cast<float> (selectedLayer);
    graphics.setColour (juce::Colour (0xffd4af37));
    graphics.fillEllipse (railX - 5.0f, thumbY - 12.0f, 24.0f, 24.0f);
    graphics.setColour (juce::Colour (0xff171717));
    graphics.fillEllipse (railX, thumbY - 7.0f, 14.0f, 14.0f);
}

void ArmoryLayerSelector::mouseDown (const juce::MouseEvent& event)
{
    const auto proportion = juce::jlimit (0.0f, 0.999f, event.position.y / static_cast<float> (juce::jmax (1, getHeight())));
    const auto nextLayer = juce::jlimit (0, 2, static_cast<int> (proportion * 3.0f));
    if (nextLayer != selectedLayer)
    {
        setSelectedLayer (nextLayer);
        if (onChange != nullptr)
            onChange (selectedLayer);
    }
}

ArmoryLayerStrip::ArmoryLayerStrip (ArmoryVSTAudioProcessor& processorToUse, int layerIndex)
    : processor (processorToUse), layer (layerIndex)
{
    setLookAndFeel (&armoryLookAndFeel);
    const auto layerName = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer));
    title.setText ("LAYER " + layerName, juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffd4af37));
    addAndMakeVisible (title);

    activeButton.setButtonText ("PWR");
    activeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, parameterId ("Active"), activeButton);
    activeButton.onStateChange = [this]
    {
        // The web rack dims inactive modules as a whole, rather than merely
        // changing the PWR lamp. Keep that unmistakable participation cue.
        setAlpha (activeButton.getToggleState() ? 1.0f : 0.60f);
        repaint();
    };
    setAlpha (activeButton.getToggleState() ? 1.0f : 0.60f);
    addAndMakeVisible (activeButton);

    randomButton.setButtonText ("RAND");
    randomButton.setTooltip ("Randomize Design");
    randomButton.onClick = [this] { if (onRandomise != nullptr) onRandomise (layer); };
    addAndMakeVisible (randomButton);

    syncButton.setButtonText ("SYNC");
    syncButton.setTooltip ("Sync Layer B / C Speed and Time to Layer A");
    syncButton.onClick = [this] { if (onSyncChanged != nullptr) onSyncChanged (syncButton.getToggleState()); };
    if (layer == 0)
        addAndMakeVisible (syncButton);

    for (int preset = 0; preset < 5; ++preset)
        presetSelector.addItem (std::array<juce::String, 5> { "Sword", "Dagger", "Greatsword", "Katana", "Chain" }[static_cast<size_t> (preset)], preset + 1);
    presetSelector.setSelectedId (1, juce::dontSendNotification);
    presetSelector.onChange = [this] { if (onPresetChanged != nullptr) onPresetChanged (layer); };
    addAndMakeVisible (presetSelector);

    configureSlider (0, "Transient", "SHAPE", 0.0, 100.0, 1.0, false);
    configureSlider (1, "Time", "TIME", 0.1, 5.0, 0.1, false);
    configureSlider (2, "Metal", "METAL", 0.1, 10.0, 0.1, false);
    configureSlider (3, "Wobble", "WOBBLE", 0.0, 100.0, 1.0, false);
    configureSlider (4, "Speed", "SPEED", 0.5, 25.0, 0.5, false);
    configureSlider (5, "Air", "AIR", 0.0, 100.0, 1.0, false);
    configureSlider (6, "EqLow", "LO", -20.0, 20.0, 1.0, true);
    configureSlider (7, "EqMid", "MID", -20.0, 20.0, 1.0, true);
    configureSlider (8, "EqHigh", "HI", -20.0, 20.0, 1.0, true);
    configureSlider (9, "HP", "HPF", 20.0, 1000.0, 10.0, true);
    configureSlider (10, "LP", "LPF", 1000.0, 20000.0, 100.0, true);
    configureSlider (11, "Width", "WIDTH", 0.0, 100.0, 1.0, true);
    configureSlider (12, "Verb", "VERB", 0.0, 100.0, 1.0, true);
    configureSlider (13, "Gain", "GAIN", -20.0, 10.0, 0.5, true);

    triggerButton.onStateChange = [this]
    {
        if (onTrigger != nullptr)
            onTrigger (layer, triggerButton.isDown(), shotMode);
    };
    addAndMakeVisible (triggerButton);

    modeButton.onClick = [this]
    {
        shotMode = modeButton.getToggleState();
        triggerButton.setButtonText (shotMode ? "SHOT" : "HOLD");
    };
    addAndMakeVisible (modeButton);
}

juce::String ArmoryLayerStrip::parameterId (const juce::String& suffix) const
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer)) + suffix;
}

void ArmoryLayerStrip::configureSlider (int index, const juce::String& suffix, const juce::String& label,
                                        double minimum, double maximum, double interval, bool rotary)
{
    auto& slider = controls[static_cast<size_t> (index)];
    auto& sliderLabel = controlLabels[static_cast<size_t> (index)];
    controlIds[static_cast<size_t> (index)] = suffix;
    // Configure the value box only after the slider has the Armory look-and-
    // feel; otherwise JUCE creates its stock Label before this Slider becomes
    // a child of the strip, bypassing ArmorySliderValueLabel entirely.
    slider.setLookAndFeel (&armoryLookAndFeel);
    slider.setRange (minimum, maximum, interval);
    slider.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    // Every parameter follows the same reading order: name at the cell's
    // upper-left, control beneath it, then the value beside/below the control.
    // Value labels support delayed single-click text editing, direct vertical
    // dragging, and double-click reset through ArmorySliderValueLabel.
    const auto rotaryValueWidth = (suffix == "HP" || suffix == "LP") ? 72 : 58;
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false,
                            rotary ? rotaryValueWidth : 78, 26);
    // The web control readouts always include their unit.
    const auto unit = suffix == "Time" ? "s"
                    : (suffix == "Speed" || suffix == "HP" || suffix == "LP") ? "Hz"
                    : (suffix == "EqLow" || suffix == "EqMid" || suffix == "EqHigh" || suffix == "Gain") ? "dB"
                    : (suffix == "Transient" || suffix == "Wobble" || suffix == "Air" || suffix == "Width" || suffix == "Verb") ? "%"
                    : "";
    slider.textFromValueFunction = [unit, interval] (double value)
    {
        auto text = interval < 1.0 ? juce::String (value, 1) : juce::String (juce::roundToInt (value));
        if (text.containsChar ('.'))
            text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return text + unit;
    };
    slider.valueFromTextFunction = [] (const juce::String& text)
    {
        return text.retainCharacters ("0123456789.-").getDoubleValue();
    };
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff6fa7bd));
    slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff6fa7bd));
    const auto defaults = std::array<double, 14> { 50.0, 1.5, 3.0, 20.0, 8.0, 50.0, 0.0, 0.0, 0.0, 20.0, 20000.0, 100.0, 30.0, 0.0 };
    slider.setDoubleClickReturnValue (true, defaults[static_cast<size_t> (index)]);
    sliderAttachments[static_cast<size_t> (index)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, parameterId (suffix), slider);
    slider.onValueChange = [this, index]
    {
        if (index == 0 || index == 1)
            repaint();
        if (onControlChanged != nullptr)
            onControlChanged (layer, controlIds[static_cast<size_t> (index)], static_cast<float> (controls[static_cast<size_t> (index)].getValue()));
    };
    addAndMakeVisible (slider);

    sliderLabel.setText (label, juce::dontSendNotification);
    sliderLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd4af37));
    sliderLabel.setJustificationType (juce::Justification::centredLeft);
    sliderLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (sliderLabel);
}

void ArmoryLayerStrip::setSyncEnabled (bool enabled)
{
    syncButton.setToggleState (enabled, juce::dontSendNotification);
}

void ArmoryLayerStrip::setSyncLocked (bool locked)
{
    controls[1].setEnabled (! locked);
    controls[4].setEnabled (! locked);
}

void ArmoryLayerStrip::setPresetDisplayCustom()
{
    // The web preset button changes to this label whenever a parameter no
    // longer represents one of the supplied recipes.
    presetSelector.setText ("Custom *", juce::dontSendNotification);
}

void ArmoryLayerStrip::setPresetDisplay (int presetId)
{
    presetSelector.setSelectedId (presetId, juce::dontSendNotification);
}

void ArmoryLayerStrip::paint (juce::Graphics& graphics)
{
    if (activeButton.getToggleState())
    {
        graphics.setGradientFill (juce::ColourGradient (juce::Colour (0xff252528), 0.0f, 0.0f,
                                                         juce::Colour (0xff1a1a1d), 0.0f, static_cast<float> (getHeight()), false));
        graphics.fillAll();
    }
    else
    {
        graphics.fillAll (juce::Colour (0xff1a1a1d));
    }
    graphics.setColour (juce::Colour (0xff333333));
    graphics.drawRect (getLocalBounds(), 1);
    graphics.setColour (activeButton.getToggleState() ? juce::Colour (0xffd4af37) : juce::Colour (0xff444444));
    graphics.fillRect (0, 0, getWidth(), 4);
    constexpr auto mixerTop = 365;
    constexpr auto footerHeight = 42;
    const auto mixerBottom = juce::jmax (mixerTop + 4, getHeight() - footerHeight - 8);
    const auto mixerRowHeight = juce::jmax (54, (mixerBottom - mixerTop) / 4);
    graphics.setColour (juce::Colour (0xff333333));
    graphics.drawLine (8.0f, 74.0f, static_cast<float> (getWidth() - 8), 74.0f);
    graphics.drawLine (8.0f, 342.0f, static_cast<float> (getWidth() - 8), 342.0f);
    graphics.setColour (juce::Colour (0xff666666));
    graphics.setFont (12.0f);
    graphics.drawText ("DESIGN", 8, 76, getWidth() - 16, 18, juce::Justification::centred);
    graphics.drawText ("MIXER", 8, 344, getWidth() - 16, 18, juce::Justification::centred);

    graphics.setFont (10.0f);
    const std::array<std::pair<juce::String, juce::Rectangle<int>>, 4> mixerGroups
    {{
        { "EQ",     { 8, mixerTop,                       getWidth() - 16, mixerRowHeight } },
        { "FILTER", { 8, mixerTop + mixerRowHeight,      getWidth() - 16, mixerRowHeight } },
        { "SPACE",  { 8, mixerTop + mixerRowHeight * 2, getWidth() - 16, mixerRowHeight } },
        { "GAIN",   { 8, mixerTop + mixerRowHeight * 3, getWidth() - 16, mixerRowHeight } }
    }};
    for (const auto& group : mixerGroups)
    {
        graphics.setColour (juce::Colour (0xff333333));
        graphics.drawRect (group.second, 1);
        graphics.setColour (juce::Colour (0xff666666));
        graphics.drawText (group.first, group.second.getX() + 5, group.second.getY() + 3, 44, 14, juce::Justification::centredLeft);
    }

    const auto shape = static_cast<float> (controls[0].getValue() / 100.0);
    const auto duration = juce::jmax (0.1f, static_cast<float> (controls[1].getValue()));
    auto graph = juce::Rectangle<float> (12.0f, 94.0f, static_cast<float> (getWidth() - 24), 22.0f);
    const auto attackWidth = ((0.01f + shape * 0.5f * duration) / duration) * graph.getWidth();
    const auto decayWidth = ((0.01f + (1.0f - shape) * 0.5f * duration) / duration) * graph.getWidth();
    const auto releaseWidth = graph.getWidth() * 0.25f;
    const auto sustainWidth = juce::jmax (0.0f, graph.getWidth() - attackWidth - decayWidth - releaseWidth);
    const auto sustainY = graph.getBottom() - shape * (graph.getHeight() - 2.0f);
    juce::Path envelope;
    envelope.startNewSubPath (graph.getX(), graph.getBottom());
    envelope.lineTo (graph.getX() + attackWidth, graph.getY() + 2.0f);
    envelope.lineTo (graph.getX() + attackWidth + decayWidth, sustainY);
    envelope.lineTo (graph.getX() + attackWidth + decayWidth + sustainWidth, sustainY);
    envelope.lineTo (graph.getRight(), graph.getBottom());
    graphics.setColour (juce::Colour (0x22d4af37));
    graphics.fillPath (envelope);
    graphics.setColour (juce::Colour (0xffd4af37));
    graphics.strokePath (envelope, juce::PathStrokeType (1.4f));
}

void ArmoryLayerStrip::resized()
{
    auto area = getLocalBounds().reduced (9);
    area.removeFromTop (5);
    auto header = area.removeFromTop (28);
    activeButton.setBounds (header.removeFromLeft (54));
    title.setBounds (header.removeFromLeft (88));
    if (layer == 0)
        syncButton.setBounds (header.removeFromRight (48));
    else
        syncButton.setBounds ({});
    randomButton.setBounds (header.removeFromRight (48));
    presetSelector.setBounds (area.removeFromTop (28).reduced (0, 2));
    area.removeFromTop (58);

    auto design = area.removeFromTop (220);
    const auto designColumnWidth = design.getWidth() / 2;
    const auto designRowHeight = design.getHeight() / 3;
    const std::array<std::pair<int, int>, 6> designPositions
    {{
        { 0, 0 }, { 1, 1 },
        { 2, 2 }, { 4, 3 },
        { 3, 4 }, { 5, 5 }
    }};
    for (const auto& item : designPositions)
    {
        const auto index = item.first;
        const auto slot = item.second;
        const auto column = slot % 2;
        const auto row = slot / 2;
        auto cell = juce::Rectangle<int> (design.getX() + column * designColumnWidth,
                                          design.getY() + row * designRowHeight,
                                          designColumnWidth, designRowHeight).reduced (7, 5);
        controlLabels[static_cast<size_t> (index)].setBounds (cell.removeFromTop (19));
        controls[static_cast<size_t> (index)].setBounds (cell);
    }

    area.removeFromTop (15);
    auto footer = area.removeFromBottom (38);
    auto mixer = area;
    const auto mixerRowHeight = juce::jmax (54, mixer.getHeight() / 4);
    const auto layoutRotaryGroup = [this] (juce::Rectangle<int> row, std::initializer_list<int> indices)
    {
        auto group = row.reduced (7, 3);
        group.removeFromTop (15);
        const auto cellWidth = group.getWidth() / static_cast<int> (indices.size());
        auto column = 0;
        for (const auto index : indices)
        {
            auto cell = juce::Rectangle<int> (group.getX() + column * cellWidth, group.getY(),
                                               cellWidth, group.getHeight()).reduced (5, 1);
            controlLabels[static_cast<size_t> (index)].setBounds (cell.removeFromTop (17));
            controls[static_cast<size_t> (index)].setBounds (cell);
            ++column;
        }
    };
    layoutRotaryGroup (mixer.removeFromTop (mixerRowHeight), { 6, 7, 8 });
    layoutRotaryGroup (mixer.removeFromTop (mixerRowHeight), { 9, 10 });
    layoutRotaryGroup (mixer.removeFromTop (mixerRowHeight), { 11, 12 });
    layoutRotaryGroup (mixer.removeFromTop (mixerRowHeight), { 13 });

    modeButton.setBounds (footer.removeFromRight (64));
    footer.removeFromRight (6);
    triggerButton.setBounds (footer);
}

#if 0
ArmoryVSTAudioProcessorEditor::ArmoryVSTAudioProcessorEditor (ArmoryVSTAudioProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse),
      processor (processorToUse)
{
    title.setText ("THE ARMORY  ·  VST PROTOTYPE", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffd4af37));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    layerSelector.addItem ("A  ·  LAYER A", 1);
    layerSelector.addItem ("B  ·  LAYER B", 2);
    layerSelector.addItem ("C  ·  LAYER C", 3);
    layerSelector.setSelectedId (1);
    layerSelector.setTooltip ("Select the layer to audition and lock.");
    layerSelector.onChange = [this]
    {
        selectedLayer = layerSelector.getSelectedId() - 1;
        refreshLockedNotes();
        refreshLayerControls();
    };
    addAndMakeVisible (layerSelector);

    presetSelector.addItem ("Sword", 1);
    presetSelector.addItem ("Dagger", 2);
    presetSelector.addItem ("Greatsword", 3);
    presetSelector.addItem ("Katana", 4);
    presetSelector.addItem ("Chain", 5);
    presetSelector.setTooltip ("Apply a metal synthesis preset to the selected layer.");
    presetSelector.onChange = [this] { applySelectedPreset (presetSelector.getSelectedId()); };
    addAndMakeVisible (presetSelector);

    octaveSelector.addItem ("C1", 1);
    octaveSelector.addItem ("C2", 2);
    octaveSelector.addItem ("C3", 3);
    octaveSelector.addItem ("C4", 4);
    octaveSelector.addItem ("C5", 5);
    octaveSelector.setSelectedId (3);
    octaveSelector.setTooltip ("Choose the visible piano octave range.");
    octaveSelector.onChange = [this]
    {
        const auto midiNote = 12 * (octaveSelector.getSelectedId() + 1);
        keyboardViewport.setViewPosition (juce::jmax (0, keyboard.xForMidiNote (midiNote) - 20), 0);
    };
    addAndMakeVisible (octaveSelector);

    controlPageSelector.addItem ("SOURCE", 1);
    controlPageSelector.addItem ("MOTION", 2);
    controlPageSelector.addItem ("TONE", 3);
    controlPageSelector.addItem ("SPACE", 4);
    controlPageSelector.setSelectedId (1);
    controlPageSelector.setTooltip ("Switch between synthesis and spatial/filter controls.");
    controlPageSelector.onChange = [this] { refreshControlPage(); };
    addAndMakeVisible (controlPageSelector);

    status.setText ("Left click: audition  ·  Right click: lock/unlock notes for the selected layer", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, juce::Colour (0xffa7a7a7));
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    keyboard.onNote = [this] (int midiNote, bool isNoteOn)
    {
        processor.enqueueKeyboardNote (selectedLayer, midiNote, isNoteOn);
    };
    keyboard.onToggleLock = [this] (int midiNote)
    {
        return processor.toggleLayerLockedNote (selectedLayer, midiNote);
    };
    keyboardViewport.setViewedComponent (&keyboard, false);
    keyboardViewport.setScrollBarsShown (false, true);
    keyboardViewport.setScrollBarThickness (10);
    addAndMakeVisible (keyboardViewport);

    layerActive.setButtonText ("Layer enabled");
    layerActive.onClick = [this] { writeSelectedParameter ("Active", layerActive.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible (layerActive);

    const auto configureControl = [this] (juce::Slider& control, juce::Label& label, int index, double minimum, double maximum, double interval)
    {
        control.setSliderStyle (juce::Slider::LinearBar);
        control.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        control.setRange (minimum, maximum, interval);
        control.setColour (juce::Slider::trackColourId, juce::Colour (0xff6fa7bd));
        control.onValueChange = [this, &control, index] { writeSelectedParameter (displayedControlIds[static_cast<size_t> (index)], static_cast<float> (control.getValue())); };
        addAndMakeVisible (control);
        label.setColour (juce::Label::textColourId, juce::Colour (0xffd4af37));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    };

    configureControl (metalControl, metalLabel, 0, 0.1, 10.0, 0.1);
    configureControl (wobbleControl, wobbleLabel, 1, 0.0, 100.0, 1.0);
    configureControl (timeControl, timeLabel, 2, 0.0, 100.0, 1.0);
    configureControl (gainControl, gainLabel, 3, 0.1, 5.0, 0.01);

    refreshLockedNotes();
    refreshControlPage();
    keyboardViewport.setViewPosition (keyboard.xForMidiNote (48), 0);

    setResizable (false, false);
    setSize (880, 410);
}

void ArmoryVSTAudioProcessorEditor::refreshLockedNotes()
{
    keyboard.setLockedNotes (processor.getLayerLockedNotes (selectedLayer));
}

juce::String ArmoryVSTAudioProcessorEditor::selectedParameterId (const juce::String& suffix) const
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + selectedLayer)) + suffix;
}

void ArmoryVSTAudioProcessorEditor::writeSelectedParameter (const juce::String& suffix, float value)
{
    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.parameters.getParameter (selectedParameterId (suffix))))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void ArmoryVSTAudioProcessorEditor::refreshLayerControls()
{
    const auto read = [this] (const juce::String& suffix)
    {
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.parameters.getParameter (selectedParameterId (suffix))))
            return parameter->convertFrom0to1 (parameter->getValue());

        return 0.0f;
    };

    layerActive.setToggleState (read ("Active") >= 0.5f, juce::dontSendNotification);
    metalControl.setValue (read (displayedControlIds[0]), juce::dontSendNotification);
    wobbleControl.setValue (read (displayedControlIds[1]), juce::dontSendNotification);
    timeControl.setValue (read (displayedControlIds[2]), juce::dontSendNotification);
    gainControl.setValue (read (displayedControlIds[3]), juce::dontSendNotification);
}

void ArmoryVSTAudioProcessorEditor::refreshControlPage()
{
    struct Page
    {
        std::array<juce::String, 4> ids;
        std::array<juce::String, 4> labels;
        std::array<double, 4> minimums;
        std::array<double, 4> maximums;
        std::array<double, 4> intervals;
    };

    const std::array<Page, 4> pages
    {{
        {{ "Metal", "Wobble", "Transient", "Time" }, { "Metal", "Wobble", "Shape", "Time" }, { 0.1, 0.0, 0.0, 0.1 }, { 10.0, 100.0, 100.0, 5.0 }, { 0.1, 1.0, 1.0, 0.01 }},
        {{ "Speed", "Air", "Gain", "Width" }, { "Speed", "Air", "Gain", "Width" }, { 0.5, 0.0, -24.0, 0.0 }, { 25.0, 100.0, 6.0, 100.0 }, { 0.1, 1.0, 0.1, 1.0 }},
        {{ "EqLow", "EqMid", "EqHigh", "HP" }, { "Low EQ", "Mid EQ", "High EQ", "High-pass" }, { -20.0, -20.0, -20.0, 20.0 }, { 20.0, 20.0, 20.0, 1000.0 }, { 0.1, 0.1, 0.1, 1.0 }},
        {{ "LP", "Verb", "Width", "Gain" }, { "Low-pass", "Reverb", "Width", "Gain" }, { 1000.0, 0.0, 0.0, -24.0 }, { 20000.0, 100.0, 100.0, 6.0 }, { 1.0, 1.0, 1.0, 0.1 }}
    }};

    const auto pageIndex = juce::jlimit (0, static_cast<int> (pages.size()) - 1, controlPageSelector.getSelectedId() - 1);
    const auto& page = pages[static_cast<size_t> (pageIndex)];
    displayedControlIds = page.ids;
    metalLabel.setText (page.labels[0], juce::dontSendNotification);
    wobbleLabel.setText (page.labels[1], juce::dontSendNotification);
    timeLabel.setText (page.labels[2], juce::dontSendNotification);
    gainLabel.setText (page.labels[3], juce::dontSendNotification);

    metalControl.setRange (page.minimums[0], page.maximums[0], page.intervals[0]);
    wobbleControl.setRange (page.minimums[1], page.maximums[1], page.intervals[1]);
    timeControl.setRange (page.minimums[2], page.maximums[2], page.intervals[2]);
    gainControl.setRange (page.minimums[3], page.maximums[3], page.intervals[3]);
    refreshLayerControls();
}

void ArmoryVSTAudioProcessorEditor::applySelectedPreset (int presetId)
{
    struct Preset
    {
        float metal, wobble, transient, time, speed, air, gain, highPass, lowPass, reverb, width, lowEq, midEq, highEq;
    };

    constexpr std::array<Preset, 5> presets
    {{
        { 3.5f, 25.0f, 50.0f, 1.5f, 8.0f, 50.0f, 0.0f, 20.0f, 20000.0f, 30.0f, 80.0f, 0.0f, 0.0f, 0.0f },
        { 2.0f, 10.0f, 10.0f, 0.5f, 18.0f, 30.0f, 2.0f, 200.0f, 20000.0f, 10.0f, 40.0f, -10.0f, 0.0f, 5.0f },
        { 0.5f, 60.0f, 80.0f, 3.0f, 3.5f, 80.0f, 3.0f, 20.0f, 8000.0f, 60.0f, 100.0f, 8.0f, 0.0f, 0.0f },
        { 5.0f, 5.0f, 20.0f, 1.0f, 12.0f, 40.0f, 1.0f, 100.0f, 20000.0f, 20.0f, 60.0f, 0.0f, 0.0f, 8.0f },
        { 9.0f, 80.0f, 5.0f, 0.8f, 22.0f, 20.0f, -2.0f, 500.0f, 20000.0f, 15.0f, 90.0f, -20.0f, 0.0f, 10.0f }
    }};

    if (! juce::isPositiveAndBelow (presetId - 1, static_cast<int> (presets.size())))
        return;

    const auto& preset = presets[static_cast<size_t> (presetId - 1)];
    writeSelectedParameter ("Active", 1.0f);
    writeSelectedParameter ("Metal", preset.metal);
    writeSelectedParameter ("Wobble", preset.wobble);
    writeSelectedParameter ("Transient", preset.transient);
    writeSelectedParameter ("Time", preset.time);
    writeSelectedParameter ("Speed", preset.speed);
    writeSelectedParameter ("Air", preset.air);
    writeSelectedParameter ("Gain", preset.gain);
    writeSelectedParameter ("HP", preset.highPass);
    writeSelectedParameter ("LP", preset.lowPass);
    writeSelectedParameter ("Verb", preset.reverb);
    writeSelectedParameter ("Width", preset.width);
    writeSelectedParameter ("EqLow", preset.lowEq);
    writeSelectedParameter ("EqMid", preset.midEq);
    writeSelectedParameter ("EqHigh", preset.highEq);
    refreshLayerControls();
}

void ArmoryVSTAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xff090909));
    graphics.setColour (juce::Colour (0xff2b2b2b));
    graphics.drawRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 8.0f, 1.0f);
    graphics.setColour (juce::Colour (0xff39301d));
    graphics.drawLine (20.0f, 92.0f, static_cast<float> (getWidth() - 20), 92.0f, 1.0f);
    graphics.drawLine (20.0f, 247.0f, static_cast<float> (getWidth() - 20), 247.0f, 1.0f);
}

void ArmoryVSTAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (48);
    title.setBounds (header.removeFromLeft (190));
    presetSelector.setBounds (header.removeFromRight (140).reduced (0, 8));
    layerSelector.setBounds (header.removeFromRight (170).reduced (0, 8));
    octaveSelector.setBounds (header.removeFromRight (80).reduced (0, 8));
    controlPageSelector.setBounds (header.removeFromRight (100).reduced (0, 8));
    area.removeFromTop (24);
    status.setBounds (area.removeFromTop (28));
    area.removeFromTop (16);
    keyboardViewport.setBounds (area.removeFromTop (132));
    area.removeFromTop (18);
    layerActive.setBounds (area.removeFromTop (26).removeFromLeft (170));
    area.removeFromTop (4);
    auto controls = area.removeFromTop (92);
    const auto controlWidth = controls.getWidth() / 2;
    auto left = controls.removeFromLeft (controlWidth).reduced (0, 2);
    auto right = controls.reduced (0, 2);
    metalLabel.setBounds (left.removeFromTop (20));
    metalControl.setBounds (left.removeFromTop (24));
    wobbleLabel.setBounds (left.removeFromTop (20));
    wobbleControl.setBounds (left.removeFromTop (24));
    timeLabel.setBounds (right.removeFromTop (20));
    timeControl.setBounds (right.removeFromTop (24));
    gainLabel.setBounds (right.removeFromTop (20));
    gainControl.setBounds (right.removeFromTop (24));
}
#endif

void ArmorySpectrum::refresh()
{
    std::array<float, 2048> samples {};
    processor.copyAnalyserSamples (samples);
    std::copy (samples.begin(), samples.end(), fftData.begin());
    std::fill (fftData.begin() + 2048, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), 2048);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (size_t bin = 0; bin < displayBins.size(); ++bin)
    {
        const auto proportion = static_cast<float> (bin) / static_cast<float> (displayBins.size() - 1);
        const auto fftIndex = juce::jlimit (1, 1023, juce::roundToInt (std::pow (1023.0f, proportion)));
        const auto decibels = juce::Decibels::gainToDecibels (fftData[static_cast<size_t> (fftIndex)] / 2048.0f, -90.0f);
        displayBins[bin] = juce::jmap (juce::jlimit (-90.0f, 0.0f, decibels), -90.0f, 0.0f, 0.0f, 1.0f);
    }
    repaint();
}

void ArmorySpectrum::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colours::black);
    auto area = getLocalBounds().toFloat().reduced (8.0f);
    graphics.setColour (juce::Colour (0xff202020));
    for (int line = 1; line < 5; ++line)
        graphics.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * line / 5.0f), area.getX(), area.getRight());

    juce::Path path;
    for (size_t index = 0; index < displayBins.size(); ++index)
    {
        const auto x = juce::jmap (static_cast<float> (index), 0.0f, static_cast<float> (displayBins.size() - 1), area.getX(), area.getRight());
        const auto y = area.getBottom() - displayBins[index] * area.getHeight();
        if (index == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    graphics.setColour (juce::Colour (0xffd4af37));
    graphics.strokePath (path, juce::PathStrokeType (1.6f));
    graphics.setFont (13.0f);
    graphics.setColour (juce::Colour (0xff777777));
    graphics.drawText ("PAZ | REALTIME SPECTRUM", area.toNearestInt(), juce::Justification::centredTop, false);
}

ArmoryVSTAudioProcessorEditor::ArmoryVSTAudioProcessorEditor (ArmoryVSTAudioProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse), processor (processorToUse)
{
    setLookAndFeel (&armoryLookAndFeel);
    // Match the original rack before any Layer A sync gesture has captured a
    // layer-specific value.
    preSyncSpeeds.fill (8.0f);
    preSyncTimes.fill (1.5f);
    title.setText ("THE ARMORY  v1.2  ·  VST", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffd4af37));
    // The web console has no separate application title bar; the branding is
    // part of the master strip. Keep this legacy label out of the layout.
    title.setVisible (false);

    for (int layer = 0; layer < ArmoryVSTAudioProcessor::numberOfLayers; ++layer)
    {
        auto strip = std::make_unique<ArmoryLayerStrip> (processor, layer);
        strip->onPresetChanged = [this] (int changedLayer)
        {
            applyPreset (changedLayer, layerStrips[static_cast<size_t> (changedLayer)]->getSelectedPresetId());
        };
        strip->onRandomise = [this] (int changedLayer) { randomiseLayer (changedLayer); };
        strip->onSyncChanged = [this] (bool enabled) { setSpeedTimeSync (enabled); };
        strip->onControlChanged = [this] (int changedLayer, const juce::String& parameter, float value)
        {
            handleLayerControlChange (changedLayer, parameter, value);
        };
        strip->onTrigger = [this] (int changedLayer, bool isDown, bool isShot)
        {
            triggerLayer (changedLayer, isDown, isShot);
        };
        addAndMakeVisible (*strip);
        layerStrips[static_cast<size_t> (layer)] = std::move (strip);
    }

    addAndMakeVisible (masterPanel);
    masterBrand.setText ("THE ARMORY  v1.2", juce::dontSendNotification);
    masterBrand.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    masterBrand.setColour (juce::Label::textColourId, juce::Colour (0xffd4af37));
    masterBrand.setJustificationType (juce::Justification::centred);
    masterPanel.addAndMakeVisible (masterBrand);

    infoButton.setTooltip ("About The Armory");
    infoButton.onClick = [this]
    {
        const auto shouldShow = ! infoPanel.isVisible();
        infoPanel.setVisible (shouldShow);
        if (shouldShow)
            infoPanel.toFront (false);
    };
    masterPanel.addAndMakeVisible (infoButton);

    infoPanel.setText ("ARMORY VST\nDeveloper: wenwen149\nLicense: MIT",
                       juce::dontSendNotification);
    infoPanel.setFont (juce::FontOptions (12.0f));
    infoPanel.setColour (juce::Label::textColourId, juce::Colour (0xffbbbbbb));
    infoPanel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0e0e0e));
    infoPanel.setColour (juce::Label::outlineColourId, juce::Colour (0xffd4af37));
    infoPanel.setJustificationType (juce::Justification::centredLeft);
    infoPanel.setBorderSize ({ 9, 10, 9, 10 });
    masterPanel.addAndMakeVisible (infoPanel);
    infoPanel.setVisible (false);

    masterPanel.addAndMakeVisible (spectrum);

    masterTrigger.onStateChange = [this] { triggerMaster (masterTrigger.isDown()); };
    masterPanel.addAndMakeVisible (masterTrigger);
    masterMode.onClick = [this]
    {
        masterTrigger.setButtonText (masterMode.getToggleState() ? "MASTER SHOT" : "MASTER HOLD");
    };
    masterPanel.addAndMakeVisible (masterMode);

    layerSelector.setSelectedLayer (0);
    layerSelector.onChange = [this] (int layer)
    {
        selectedLayer = layer;
        refreshLockedNotes();
    };
    addAndMakeVisible (layerSelector);

    clearLocksButton.onClick = [this]
    {
        processor.clearLayerLockedNotes (selectedLayer);
        refreshLockedNotes();
    };
    addAndMakeVisible (clearLocksButton);

    status.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    status.setColour (juce::Label::textColourId, juce::Colour (0xffa7a7a7));
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    keyboard.onNote = [this] (int midiNote, bool isNoteOn, bool isQuickTap)
    {
        if (isNoteOn)
        {
            processor.enqueueKeyboardNote (selectedLayer, midiNote, true);
            return;
        }

        if (! isQuickTap)
        {
            processor.enqueueKeyboardNote (selectedLayer, midiNote, false);
            return;
        }

        auto gateSeconds = 0.75f;
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.parameters.getParameter (selectedParameterId ("Time"))))
            gateSeconds = parameter->convertFrom0to1 (parameter->getValue()) * 0.75f;
        processor.scheduleKeyboardRelease (selectedLayer, midiNote, gateSeconds);
    };
    keyboard.onToggleLock = [this] (int midiNote)
    {
        const auto isLocked = processor.toggleLayerLockedNote (selectedLayer, midiNote);
        // A right-click changes the processor state synchronously. Refreshing
        // here keeps the lock counter and the piano highlight in lockstep,
        // rather than waiting for a later layer selection or preset action.
        refreshLockedNotes();
        return isLocked;
    };
    keyboardViewport.setViewedComponent (&keyboard, false);
    keyboardViewport.setScrollBarsShown (false, true);
    keyboardViewport.setScrollBarThickness (11);
    addAndMakeVisible (keyboardViewport);

    refreshLockedNotes();
    keyboardViewport.setViewPosition (keyboard.xForMidiNote (48), 0);
    startTimerHz (30);
    setResizable (true, true);
    // Default to a working-sized rack: it gives every retained control enough
    // physical space without requiring a host-specific zoom setting.
    setResizeLimits (1280, 900, 1920, 1200);
    setSize (1500, 1000);
}

void ArmoryVSTAudioProcessorEditor::timerCallback()
{
    spectrum.refresh();
}

void ArmoryVSTAudioProcessorEditor::refreshLockedNotes()
{
    const auto locks = processor.getLayerLockedNotes (selectedLayer);
    keyboard.setLockedNotes (locks);
    const auto count = static_cast<int> (std::count (locks.begin(), locks.end(), true));
    static const std::array<juce::String, ArmoryVSTAudioProcessor::numberOfLayers> layerNames
    {
        "A LAYER A", "B LAYER B", "C LAYER C"
    };
    status.setText (layerNames[static_cast<size_t> (selectedLayer)]
                    + " | " + juce::String (count) + " locked | Click: ADSR shot | Hold: sustain | Right: lock",
                    juce::dontSendNotification);
}

juce::String ArmoryVSTAudioProcessorEditor::selectedParameterId (const juce::String& suffix) const
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + selectedLayer)) + suffix;
}

void ArmoryVSTAudioProcessorEditor::writeSelectedParameter (const juce::String& suffix, float value)
{
    writeLayerParameter (selectedLayer, suffix, value);
}

void ArmoryVSTAudioProcessorEditor::writeLayerParameter (int layer, const juce::String& suffix, float value)
{
    const auto id = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer)) + suffix;
    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.parameters.getParameter (id)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void ArmoryVSTAudioProcessorEditor::applyPreset (int layer, int presetId)
{
    struct Preset
    {
        float pitch, metal, wobble, transient, time, speed, air, width, verb, lowEq, midEq, highEq, highPass, lowPass, gain;
    };
    constexpr std::array<Preset, 5> presets
    {{
        { 600.0f, 3.5f, 25.0f, 50.0f, 1.5f, 8.0f, 50.0f, 80.0f, 30.0f, 0.0f, 0.0f, 0.0f, 20.0f, 20000.0f, 0.0f },
        { 1500.0f, 2.0f, 10.0f, 10.0f, 0.5f, 18.0f, 30.0f, 40.0f, 10.0f, -10.0f, 0.0f, 5.0f, 200.0f, 20000.0f, 2.0f },
        { 120.0f, 0.5f, 60.0f, 80.0f, 3.0f, 3.5f, 80.0f, 100.0f, 60.0f, 8.0f, 0.0f, 0.0f, 20.0f, 8000.0f, 3.0f },
        { 850.0f, 5.0f, 5.0f, 20.0f, 1.0f, 12.0f, 40.0f, 60.0f, 20.0f, 0.0f, 0.0f, 8.0f, 100.0f, 20000.0f, 1.0f },
        { 1800.0f, 9.0f, 80.0f, 5.0f, 0.8f, 22.0f, 20.0f, 90.0f, 15.0f, -20.0f, 0.0f, 10.0f, 500.0f, 20000.0f, -2.0f }
    }};
    if (! juce::isPositiveAndBelow (presetId - 1, static_cast<int> (presets.size())))
        return;
    const auto& preset = presets[static_cast<size_t> (presetId - 1)];
    isApplyingPreset = true;
    const std::array<std::pair<juce::String, float>, 14> values
    {{
        { "Metal", preset.metal }, { "Wobble", preset.wobble }, { "Transient", preset.transient }, { "Time", preset.time },
        { "Speed", preset.speed }, { "Air", preset.air }, { "Width", preset.width }, { "Verb", preset.verb },
        { "EqLow", preset.lowEq }, { "EqMid", preset.midEq }, { "EqHigh", preset.highEq },
        { "HP", preset.highPass }, { "LP", preset.lowPass }, { "Gain", preset.gain }
    }};
    for (const auto& value : values)
        writeLayerParameter (layer, value.first, value.second);
    isApplyingPreset = false;
    layerStrips[static_cast<size_t> (layer)]->setPresetDisplay (presetId);
    // Pitch is owned exclusively by the piano in the VST edition. Applying a
    // recipe must not create a visible lock; only a right-click on the piano
    // changes the layer's lock memory.
    juce::ignoreUnused (preset.pitch);
}

void ArmoryVSTAudioProcessorEditor::randomiseLayer (int layer)
{
    juce::Random random;
    const auto range = [&random] (float minimum, float maximum) { return minimum + random.nextFloat() * (maximum - minimum); };
    writeLayerParameter (layer, "Metal", std::round (range (0.5f, 8.0f) * 10.0f) / 10.0f);
    writeLayerParameter (layer, "Wobble", static_cast<float> (random.nextInt ({ 0, 51 })));
    writeLayerParameter (layer, "Transient", static_cast<float> (random.nextInt ({ 10, 91 })));
    writeLayerParameter (layer, "Time", std::round (range (0.5f, 3.0f) * 10.0f) / 10.0f);
    if (! speedTimeSynced || layer == 0)
        writeLayerParameter (layer, "Speed", std::round (range (1.0f, 20.0f) * 10.0f) / 10.0f);
    writeLayerParameter (layer, "Air", static_cast<float> (random.nextInt ({ 20, 81 })));
    layerStrips[static_cast<size_t> (layer)]->setPresetDisplayCustom();
}

void ArmoryVSTAudioProcessorEditor::setSpeedTimeSync (bool enabled)
{
    const auto read = [this] (int layer, const juce::String& suffix)
    {
        const auto id = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer)) + suffix;
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.parameters.getParameter (id)))
            return parameter->convertFrom0to1 (parameter->getValue());
        return 0.0f;
    };
    speedTimeSynced = enabled;
    if (enabled)
    {
        for (int layer = 1; layer < 3; ++layer)
        {
            preSyncSpeeds[static_cast<size_t> (layer - 1)] = read (layer, "Speed");
            preSyncTimes[static_cast<size_t> (layer - 1)] = read (layer, "Time");
            writeLayerParameter (layer, "Speed", read (0, "Speed"));
            writeLayerParameter (layer, "Time", read (0, "Time"));
            layerStrips[static_cast<size_t> (layer)]->setSyncLocked (true);
            layerStrips[static_cast<size_t> (layer)]->setPresetDisplayCustom();
        }
    }
    else
    {
        for (int layer = 1; layer < 3; ++layer)
        {
            writeLayerParameter (layer, "Speed", preSyncSpeeds[static_cast<size_t> (layer - 1)]);
            writeLayerParameter (layer, "Time", preSyncTimes[static_cast<size_t> (layer - 1)]);
            layerStrips[static_cast<size_t> (layer)]->setSyncLocked (false);
            layerStrips[static_cast<size_t> (layer)]->setPresetDisplayCustom();
        }
    }
    layerStrips[0]->setSyncEnabled (enabled);
}

void ArmoryVSTAudioProcessorEditor::handleLayerControlChange (int layer, const juce::String& parameter, float value)
{
    if (! isApplyingPreset)
        layerStrips[static_cast<size_t> (layer)]->setPresetDisplayCustom();

    if (speedTimeSynced && layer == 0 && (parameter == "Speed" || parameter == "Time"))
    {
        writeLayerParameter (1, parameter, value);
        writeLayerParameter (2, parameter, value);
        layerStrips[1]->setPresetDisplayCustom();
        layerStrips[2]->setPresetDisplayCustom();
    }
}

void ArmoryVSTAudioProcessorEditor::triggerLayer (int layer, bool isDown, bool isShot)
{
    // The original web rack ignores both individual and master trigger gestures
    // for a powered-off module.  Do this before queuing any MIDI so toggling a
    // layer back on can never reveal an event that happened while it was off.
    const auto activeId = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer)) + "Active";
    if (auto* activeParameter = dynamic_cast<juce::RangedAudioParameter*> (processor.parameters.getParameter (activeId)))
        if (activeParameter->convertFrom0to1 (activeParameter->getValue()) < 0.5f)
            return;

    const auto lockedNotes = processor.getLayerLockedNotes (layer);
    std::vector<int> notes;
    for (int note = 0; note < static_cast<int> (lockedNotes.size()); ++note)
        if (lockedNotes[static_cast<size_t> (note)])
            notes.push_back (note);

    // A cleared layer must still be immediately testable. This is the VST
    // counterpart to the original single Pitch setting.
    if (notes.empty())
        notes.push_back (60);

    if (isDown)
    {
        if (isShot)
        {
            const auto id = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer)) + "Time";
            auto duration = 1.0f;
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.parameters.getParameter (id)))
                duration = parameter->convertFrom0to1 (parameter->getValue());
            for (const auto note : notes)
                processor.enqueueKeyboardShot (layer, note, duration * 0.75f);
        }
        else
        {
            heldTriggerNotes[static_cast<size_t> (layer)] = notes;
            for (const auto note : notes)
                processor.enqueueKeyboardNote (layer, note, true);
        }
    }
    else if (! isShot)
    {
        const auto& notesToRelease = heldTriggerNotes[static_cast<size_t> (layer)].empty()
                                   ? notes : heldTriggerNotes[static_cast<size_t> (layer)];
        for (const auto note : notesToRelease)
            processor.enqueueKeyboardNote (layer, note, false);
        heldTriggerNotes[static_cast<size_t> (layer)].clear();
    }
}

void ArmoryVSTAudioProcessorEditor::triggerMaster (bool isDown)
{
    const auto isShot = masterMode.getToggleState();
    for (int layer = 0; layer < ArmoryVSTAudioProcessor::numberOfLayers; ++layer)
        triggerLayer (layer, isDown, isShot);
}

void ArmoryVSTAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xff050505));
    graphics.setColour (juce::Colour (0xff333333));
    graphics.drawRoundedRectangle (getLocalBounds().toFloat().reduced (6.0f), 7.0f, 2.0f);
    graphics.setColour (juce::Colour (0xff151515));
    graphics.fillRect (masterPanel.getBounds());
    graphics.setColour (juce::Colour (0xff333333));
    graphics.drawRect (masterPanel.getBounds(), 1);

    const auto drawerOpen = keyboardViewport.isVisible();
    auto footer = getLocalBounds().reduced (12).removeFromBottom (drawerOpen ? 126 : 34);
    auto header = footer.removeFromTop (34);
    const auto drawerBounds = header.removeFromLeft (116);
    header.removeFromRight (120);
    const auto zoomBounds = drawerOpen ? header.removeFromRight (230).reduced (8, 3) : juce::Rectangle<int> {};

    const auto drawer = drawerBounds.toFloat().reduced (0.5f, 2.5f);
    graphics.setColour (juce::Colour (0xff181818));
    graphics.fillRoundedRectangle (drawer, 2.0f);
    graphics.setColour (juce::Colour (0xffd4af37));
    graphics.drawRoundedRectangle (drawer, 2.0f, 1.0f);
    graphics.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    graphics.drawText (drawerOpen ? "KEYBOARD  -" : "KEYBOARD  +",
                       drawerBounds, juce::Justification::centred);

    if (drawerOpen)
    {
        auto zoomLayout = zoomBounds;
        const auto zoomLabel = zoomLayout.removeFromLeft (50);
        const auto zoomMinus = zoomLayout.removeFromLeft (24);
        const auto zoomPlus = zoomLayout.removeFromRight (24);
        const auto track = zoomLayout.toFloat().reduced (5.0f, 0.0f).withHeight (4.0f).withCentre (zoomLayout.toFloat().getCentre());
        const auto proportion = (keyboard.getWhiteKeyWidth() - 14.0f) / 38.0f;
        const auto thumbX = track.getX() + track.getWidth() * proportion;
        graphics.setColour (juce::Colours::white.withAlpha (0.82f));
        graphics.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        graphics.drawText ("ZOOM", zoomLabel, juce::Justification::centredLeft);
        for (const auto& button : { zoomMinus, zoomPlus })
        {
            graphics.setColour (juce::Colour (0xff1a1a1a));
            graphics.fillRoundedRectangle (button.toFloat().reduced (1.0f, 2.0f), 2.0f);
            graphics.setColour (juce::Colour (0xff555555));
            graphics.drawRoundedRectangle (button.toFloat().reduced (1.0f, 2.0f), 2.0f, 1.0f);
        }
        graphics.setColour (juce::Colour (0xffd4af37));
        graphics.drawText ("-", zoomMinus, juce::Justification::centred);
        graphics.drawText ("+", zoomPlus, juce::Justification::centred);
        graphics.setColour (juce::Colour (0xff303030));
        graphics.fillRoundedRectangle (track, 2.0f);
        graphics.setColour (juce::Colour (0xff6fa7bd));
        graphics.fillRoundedRectangle (track.withRight (thumbX), 2.0f);
        graphics.fillEllipse (thumbX - 6.0f, track.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
}

void ArmoryVSTAudioProcessorEditor::updateKeyboardZoomFromX (int x)
{
    auto footer = getLocalBounds().reduced (12).removeFromBottom (126);
    auto header = footer.removeFromTop (34);
    header.removeFromLeft (116);
    header.removeFromRight (120);
    const auto zoomBounds = header.removeFromRight (230).reduced (8, 3);
    auto zoomLayout = zoomBounds;
    zoomLayout.removeFromLeft (50);
    zoomLayout.removeFromLeft (24);
    zoomLayout.removeFromRight (24);
    const auto track = zoomLayout.reduced (5, 0);
    if (track.getWidth() <= 0)
        return;

    const auto oldWidth = keyboard.getWhiteKeyWidth();
    const auto centreInKeys = (static_cast<float> (keyboardViewport.getViewPositionX())
                               + static_cast<float> (keyboardViewport.getWidth()) * 0.5f) / oldWidth;
    const auto proportion = juce::jlimit (0.0f, 1.0f,
                                          static_cast<float> (x - track.getX()) / static_cast<float> (track.getWidth()));
    keyboard.setWhiteKeyWidth (14.0f + proportion * 38.0f);
    const auto targetX = juce::roundToInt (centreInKeys * keyboard.getWhiteKeyWidth()
                                           - static_cast<float> (keyboardViewport.getWidth()) * 0.5f);
    keyboardViewport.setViewPosition (juce::jlimit (0, juce::jmax (0, keyboard.getWidth() - keyboardViewport.getWidth()), targetX), 0);
    repaint (zoomBounds);
}

void ArmoryVSTAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto drawerOpen = keyboardViewport.isVisible();
    auto footer = getLocalBounds().reduced (12).removeFromBottom (drawerOpen ? 126 : 34);
    auto header = footer.removeFromTop (34);
    const auto drawerBounds = header.removeFromLeft (116);
    header.removeFromRight (120);
    const auto zoomBounds = drawerOpen ? header.removeFromRight (230).reduced (8, 3) : juce::Rectangle<int> {};

    if (drawerBounds.contains (event.getPosition()))
    {
        keyboardViewport.setVisible (! drawerOpen);
        layerSelector.setVisible (! drawerOpen);
        resized();
        repaint();
        return;
    }

    auto zoomLayout = zoomBounds;
    zoomLayout.removeFromLeft (50);
    const auto zoomMinus = zoomLayout.removeFromLeft (24);
    const auto zoomPlus = zoomLayout.removeFromRight (24);
    const auto zoomTrack = zoomLayout.reduced (5, 0);
    const auto zoomByStep = [this, zoomTrack] (float step)
    {
        const auto width = juce::jlimit (14.0f, 52.0f, keyboard.getWhiteKeyWidth() + step);
        const auto proportion = (width - 14.0f) / 38.0f;
        updateKeyboardZoomFromX (zoomTrack.getX() + juce::roundToInt (proportion * static_cast<float> (zoomTrack.getWidth())));
    };

    if (drawerOpen && zoomMinus.contains (event.getPosition()))
    {
        zoomByStep (-2.0f);
        return;
    }

    if (drawerOpen && zoomPlus.contains (event.getPosition()))
    {
        zoomByStep (2.0f);
        return;
    }

    if (drawerOpen && zoomTrack.expanded (5, 5).contains (event.getPosition()))
        updateKeyboardZoomFromX (event.x);
}

void ArmoryVSTAudioProcessorEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (keyboardViewport.isVisible())
    {
        auto footer = getLocalBounds().reduced (12).removeFromBottom (126);
        auto header = footer.removeFromTop (34);
        header.removeFromLeft (116);
        header.removeFromRight (120);
        const auto zoomBounds = header.removeFromRight (230).reduced (8, 3);
        auto zoomLayout = zoomBounds;
        zoomLayout.removeFromLeft (50);
        zoomLayout.removeFromLeft (24);
        zoomLayout.removeFromRight (24);
        const auto zoomTrack = zoomLayout.reduced (5, 0);
        if (zoomTrack.expanded (8, 5).contains (event.getPosition()))
            updateKeyboardZoomFromX (event.x);
    }
}

void ArmoryVSTAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
}

void ArmoryVSTAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& event,
                                                               const juce::MouseWheelDetails& wheel)
{
    if (keyboardViewport.isVisible() && keyboardViewport.getLocalBounds().contains (event.getEventRelativeTo (&keyboardViewport).getPosition()))
    {
        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        const auto maximum = juce::jmax (0, keyboard.getWidth() - keyboardViewport.getWidth());
        const auto target = keyboardViewport.getViewPositionX() - juce::roundToInt (delta * 420.0f);
        keyboardViewport.setViewPosition (juce::jlimit (0, maximum, target), 0);
        return;
    }

    juce::AudioProcessorEditor::mouseWheelMove (event, wheel);
}

void ArmoryVSTAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    constexpr auto drawerHeaderHeight = 34;
    constexpr auto openDrawerHeight = 126;
    const auto drawerOpen = keyboardViewport.isVisible();
    auto keyboardArea = area.removeFromBottom (drawerOpen ? openDrawerHeight : drawerHeaderHeight);
    auto keyboardHeader = keyboardArea.removeFromTop (drawerHeaderHeight);
    keyboardHeader.removeFromLeft (116);
    clearLocksButton.setBounds (keyboardHeader.removeFromRight (120).reduced (0, 2));
    if (drawerOpen)
        keyboardHeader.removeFromRight (230);
    status.setBounds (keyboardHeader);

    if (drawerOpen)
    {
        auto layerSwitchArea = keyboardArea.removeFromLeft (54);
        layerSelector.setBounds (layerSwitchArea.reduced (3, 3));
        keyboardViewport.setBounds (keyboardArea);
    }
    else
    {
        layerSelector.setBounds ({});
        keyboardViewport.setBounds ({});
    }

    area.removeFromBottom (8);
    auto rack = area;
    auto master = rack.removeFromRight (juce::jlimit (320, 420, getWidth() / 4));
    masterPanel.setBounds (master);
    const auto gap = 8;
    rack.removeFromRight (gap);
    const auto stripWidth = (rack.getWidth() - gap * 2) / 3;
    for (int layer = 0; layer < 3; ++layer)
    {
        layerStrips[static_cast<size_t> (layer)]->setBounds (rack.removeFromLeft (stripWidth));
        if (layer < 2)
            rack.removeFromLeft (gap);
    }

    auto masterArea = masterPanel.getLocalBounds().reduced (14);
    auto brand = masterArea.removeFromTop (42);
    infoButton.setBounds (brand.removeFromRight (26).reduced (2, 7));
    masterBrand.setBounds (brand);
    spectrum.setBounds (masterArea.removeFromTop (masterArea.getHeight() - 105).reduced (0, 8));
    auto triggerRow = masterArea.removeFromTop (66);
    masterMode.setBounds (triggerRow.removeFromRight (72).reduced (4, 14));
    masterTrigger.setBounds (triggerRow.reduced (0, 5));
    infoPanel.setBounds (masterPanel.getLocalBounds().reduced (20).withHeight (128).withY (48));
}


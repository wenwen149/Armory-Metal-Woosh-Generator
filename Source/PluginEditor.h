#pragma once

#include "PluginProcessor.h"

class ArmoryPianoKeyboard final : public juce::Component
{
public:
    // `isQuickTap` is meaningful on note-off only. A quick click becomes a
    // complete one-shot ADSR; holding a key retains its sustain stage until
    // release.
    using NoteCallback = std::function<void (int midiNote, bool isNoteOn, bool isQuickTap)>;
    using LockCallback = std::function<bool (int midiNote)>;

    ArmoryPianoKeyboard();
    void setLockedNotes (const ArmoryVSTTestReleaseAudioProcessor::LockedNoteArray& notes);
    void setWhiteKeyWidth (float width);
    float getWhiteKeyWidth() const noexcept { return whiteKeyWidth; }
    int getRequiredWidth() const noexcept;
    int xForMidiNote (int midiNote) const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    NoteCallback onNote;
    LockCallback onToggleLock;

private:
    struct KeyBounds
    {
        int midiNote = 0;
        juce::Rectangle<float> bounds;
        bool isBlack = false;
    };

    int noteAt (juce::Point<float>) const;
    void trigger (int midiNote, bool isNoteOn);
    void rebuildKeyBounds();

    ArmoryVSTTestReleaseAudioProcessor::LockedNoteArray lockedNotes {};
    std::vector<KeyBounds> keyBounds;
    int pressedNote = -1;
    double pressStartedMilliseconds = 0.0;
    float whiteKeyWidth = 33.0f;
};

class ArmoryLayerSelector final : public juce::Component
{
public:
    std::function<void (int)> onChange;
    void setSelectedLayer (int layer);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int selectedLayer = 0;
};

class ArmoryLayerStrip final : public juce::Component
{
public:
    using TriggerCallback = std::function<void (int layer, bool isDown, bool isShot)>;
    using LayerCallback = std::function<void (int layer)>;
    using SyncCallback = std::function<void (bool enabled)>;
    using ControlCallback = std::function<void (int layer, const juce::String& parameter, float value)>;

    ArmoryLayerStrip (ArmoryVSTTestReleaseAudioProcessor&, int layerIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setSyncEnabled (bool enabled);
    void setSyncLocked (bool locked);
    void setPresetDisplayCustom();
    void setPresetDisplay (int presetId);
    int getSelectedPresetId() const { return presetSelector.getSelectedId(); }

    TriggerCallback onTrigger;
    LayerCallback onRandomise;
    LayerCallback onPresetChanged;
    SyncCallback onSyncChanged;
    ControlCallback onControlChanged;

private:
    juce::String parameterId (const juce::String& suffix) const;
    void configureSlider (int index, const juce::String& suffix, const juce::String& label,
                          double minimum, double maximum, double interval, bool rotary);

    ArmoryVSTTestReleaseAudioProcessor& processor;
    int layer = 0;
    bool shotMode = false;
    juce::Label title;
    juce::ToggleButton activeButton;
    juce::TextButton randomButton { "RAND" };
    juce::ToggleButton syncButton { "SYNC" };
    juce::ComboBox presetSelector;
    std::array<juce::Slider, 14> controls;
    std::array<juce::Label, 14> controlLabels;
    std::array<juce::String, 14> controlIds;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 14> sliderAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAttachment;
    juce::TextButton triggerButton { "HOLD" };
    juce::ToggleButton modeButton { "SHOT" };
};

class ArmorySpectrum final : public juce::Component
{
public:
    explicit ArmorySpectrum (ArmoryVSTTestReleaseAudioProcessor& owner) : processor (owner), fft (11), window (2048, juce::dsp::WindowingFunction<float>::hann) {}
    void refresh();
    void paint (juce::Graphics&) override;

private:
    ArmoryVSTTestReleaseAudioProcessor& processor;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, 4096> fftData {};
    std::array<float, 160> displayBins {};
};

class ArmoryVSTTestReleaseAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                       private juce::Timer
{
public:
    explicit ArmoryVSTTestReleaseAudioProcessorEditor (ArmoryVSTTestReleaseAudioProcessor&);
    ~ArmoryVSTTestReleaseAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void refreshLockedNotes();
    juce::String selectedParameterId (const juce::String& suffix) const;
    void writeSelectedParameter (const juce::String& suffix, float value);
    void writeLayerParameter (int layer, const juce::String& suffix, float value);
    void applyPreset (int layer, int presetId);
    void randomiseLayer (int layer);
    void setSpeedTimeSync (bool enabled);
    void handleLayerControlChange (int layer, const juce::String& parameter, float value);
    void triggerLayer (int layer, bool isDown, bool isShot);
    void triggerMaster (bool isDown);
    void updateKeyboardZoomFromX (int x);

    ArmoryVSTTestReleaseAudioProcessor& processor;
    juce::Label title;
    juce::Label status;
    ArmoryLayerSelector layerSelector;
    juce::TextButton clearLocksButton { "CLEAR LOCKS" };
    juce::Viewport keyboardViewport;
    ArmoryPianoKeyboard keyboard;
    std::array<std::unique_ptr<ArmoryLayerStrip>, ArmoryVSTTestReleaseAudioProcessor::numberOfLayers> layerStrips;
    juce::Component masterPanel;
    juce::Label masterBrand;
    juce::TextButton infoButton { "i" };
    juce::Label infoPanel;
    ArmorySpectrum spectrum { processor };
    juce::TextButton masterTrigger { "MASTER HOLD" };
    juce::ToggleButton masterMode { "SHOT" };
    // Matches the page's initial Sword design values. These are only a
    // fallback until enabling Layer A sync captures Layer B/C's live settings.
    std::array<float, 2> preSyncSpeeds { 8.0f, 8.0f };
    std::array<float, 2> preSyncTimes { 1.5f, 1.5f };
    std::array<std::vector<int>, ArmoryVSTTestReleaseAudioProcessor::numberOfLayers> heldTriggerNotes;
    bool speedTimeSynced = false;
    bool isApplyingPreset = false;
    int selectedLayer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArmoryVSTTestReleaseAudioProcessorEditor)
};

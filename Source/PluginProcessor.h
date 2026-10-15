#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <bitset>

class ArmorySound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class ArmoryVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    void setTone (float metalRatio, float wobblePercent, float transientPercent, float durationSeconds,
                  float speedHz, float airPercent, float outputGainDb);
    void setWobblePhases (double slowPhase, double fastPhase);

private:
    double carrierPhase = 0.0;
    double modulatorPhase = 0.0;
    double carrierDelta = 0.0;
    double modulatorDelta = 0.0;
    double baseCarrierDelta = 0.0;
    double baseModulatorDelta = 0.0;
    double wobblePhaseSlow = 0.0;
    double wobblePhaseFast = 0.0;
    float level = 0.0f;
    float envelope = 0.0f;
    float metal = 3.0f;
    float wobble = 0.2f;
    float sustainLevel = 0.5f;
    float attackSeconds = 0.08f;
    float decaySeconds = 0.25f;
    float releaseSeconds = 0.45f;
    float speed = 8.0f;
    float air = 0.5f;
    float gain = 0.5f;
    enum class EnvelopeStage { attack, decay, sustain } envelopeStage = EnvelopeStage::attack;
    double envelopeStageTime = 0.0;
    float releaseStartEnvelope = 0.0f;
    bool releasing = false;
};

class ArmoryVSTAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int numberOfLayers = 3;
    // The piano exposes all 128 MIDI notes and a lock is a promise that the
    // note will join a layer trigger. Allocate one voice per note so a wide
    // locked chord never silently steals an earlier key.
    static constexpr int maxPianoPolyphony = 128;
    using LockedNoteArray = std::array<bool, 128>;

    ArmoryVSTAudioProcessor();
    ~ArmoryVSTAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Armory VST"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Maximum Time is 5 s, which gives a 1.25 s release; Tone.Reverb then
    // contributes a two-second IR tail (plus its 10 ms predelay).
    double getTailLengthSeconds() const override { return 3.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;

    void enqueueKeyboardNote (int layer, int midiNote, bool isNoteOn);
    void enqueueKeyboardShot (int layer, int midiNote, float gateSeconds);
    void scheduleKeyboardRelease (int layer, int midiNote, float gateSeconds);
    bool toggleLayerLockedNote (int layer, int midiNote);
    void setLayerLockedNote (int layer, int midiNote);
    void clearLayerLockedNotes (int layer);
    LockedNoteArray getLayerLockedNotes (int layer) const;
    void copyAnalyserSamples (std::array<float, 2048>& destination) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void transformMidiForLayer (int layer, const juce::MidiBuffer& input, juce::MidiBuffer& output);
    juce::String serialiseLockedNotes (int layer) const;
    void restoreLockedNotes (int layer, const juce::String& value);

    std::array<juce::MidiKeyboardState, numberOfLayers> keyboardStates;
    // Shot mode may launch every locked piano key at once, so release state is
    // tracked per note instead of keeping one timer per layer.
    std::array<std::array<std::atomic<juce::int64>, 128>, numberOfLayers> shotSamplesRemaining {};
    std::array<juce::Synthesiser, numberOfLayers> synthesisers;
    std::array<juce::AudioBuffer<float>, numberOfLayers> layerBuffers;
    std::array<std::array<juce::IIRFilter, 2>, numberOfLayers> highPassFilters;
    std::array<std::array<juce::IIRFilter, 2>, numberOfLayers> lowPassFilters;
    // Tone.MultibandSplit drives four independent, second-order paths:
    // LP(400) for low, HP(400)->LP(2500) for mid, and HP(2500) for high.
    std::array<std::array<juce::IIRFilter, 2>, numberOfLayers> lowEqLowPassFilters;
    std::array<std::array<juce::IIRFilter, 2>, numberOfLayers> midEqHighPassFilters;
    std::array<std::array<juce::IIRFilter, 2>, numberOfLayers> midEqLowPassFilters;
    std::array<std::array<juce::IIRFilter, 2>, numberOfLayers> highEqHighPassFilters;
    // Tone.AutoFilter is backed by a WebAudio BiquadFilter. Keep one
    // second-order band-pass state per output channel rather than using the
    // otherwise convenient state-variable filter, whose high-Q sweep has a
    // noticeably different character.
    std::array<std::array<juce::IIRFilter, 2>, numberOfLayers> airFilters;
    std::array<juce::dsp::Convolution, numberOfLayers> reverbs;
    std::array<juce::AudioBuffer<float>, numberOfLayers> reverbDryBuffers;
    std::array<std::array<std::atomic_bool, 128>, numberOfLayers> lockedNotes {};
    std::array<std::array<std::bitset<128>, 128>, numberOfLayers> activeTriggeredNotes {};
    std::array<bool, numberOfLayers> layerWasActive {};
    std::array<std::atomic_bool, numberOfLayers> wobblePhaseResetRequests {};
    std::array<double, numberOfLayers> wobbleSlowPhases {};
    std::array<double, numberOfLayers> wobbleFastPhases {};
    std::array<double, numberOfLayers> motionPhases {};
    // Tone.Param.rampTo() uses an exponential 100 ms transition for
    // frequency-valued parameters. The AutoFilter and Tremolo share it.
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, numberOfLayers> motionSpeeds;
    juce::Random wobblePhaseRandom { 0x41c64e6du };
    std::array<std::atomic<float>*, numberOfLayers> activeParameters {};
    std::array<std::atomic<float>*, numberOfLayers> metalParameters {};
    std::array<std::atomic<float>*, numberOfLayers> wobbleParameters {};
    std::array<std::atomic<float>*, numberOfLayers> transientParameters {};
    std::array<std::atomic<float>*, numberOfLayers> durationParameters {};
    std::array<std::atomic<float>*, numberOfLayers> speedParameters {};
    std::array<std::atomic<float>*, numberOfLayers> airParameters {};
    std::array<std::atomic<float>*, numberOfLayers> gainParameters {};
    std::array<std::atomic<float>*, numberOfLayers> highPassParameters {};
    std::array<std::atomic<float>*, numberOfLayers> lowPassParameters {};
    std::array<std::atomic<float>*, numberOfLayers> reverbParameters {};
    std::array<std::atomic<float>*, numberOfLayers> widthParameters {};
    std::array<std::atomic<float>*, numberOfLayers> lowEqParameters {};
    std::array<std::atomic<float>*, numberOfLayers> midEqParameters {};
    std::array<std::atomic<float>*, numberOfLayers> highEqParameters {};
    juce::dsp::Compressor<float> masterLimiter;
    std::array<std::atomic<float>, 2048> analyserSamples {};
    std::atomic<juce::uint32> analyserWritePosition { 0 };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArmoryVSTAudioProcessor)
};


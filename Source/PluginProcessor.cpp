#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto twoPi = juce::MathConstants<double>::twoPi;

float toneTargetRamp (float startValue, float targetValue, double elapsedSeconds, double durationSeconds)
{
    if (durationSeconds <= 0.0 || elapsedSeconds >= durationSeconds)
        return targetValue;

    const auto linearStart = durationSeconds * 0.9;
    const auto timeConstant = std::log (durationSeconds + 1.0) / std::log (200.0);
    const auto exponentialValue = [=] (double time)
    {
        return targetValue + (startValue - targetValue) * static_cast<float> (std::exp (-time / timeConstant));
    };

    if (elapsedSeconds < linearStart)
        return exponentialValue (elapsedSeconds);

    const auto valueAtLinearStart = exponentialValue (linearStart);
    const auto linearAmount = static_cast<float> ((elapsedSeconds - linearStart) / (durationSeconds - linearStart));
    return valueAtLinearStart + (targetValue - valueAtLinearStart) * linearAmount;
}
}

bool ArmoryVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<ArmorySound*> (sound) != nullptr;
}

void ArmoryVoice::setTone (float metalRatio, float wobblePercent, float transientPercent, float durationSeconds,
                           float speedHz, float airPercent, float outputGainDb)
{
    metal = juce::jlimit (0.1f, 10.0f, metalRatio);
    wobble = juce::jlimit (0.0f, 1.0f, wobblePercent / 100.0f);
    const auto transient = juce::jlimit (0.0f, 1.0f, transientPercent / 100.0f);
    attackSeconds = 0.01f + transient * 0.5f * durationSeconds;
    decaySeconds = 0.01f + (1.0f - transient) * 0.5f * durationSeconds;
    sustainLevel = transient;
    releaseSeconds = juce::jlimit (0.001f, 2.0f, durationSeconds * 0.25f);
    speed = juce::jlimit (0.5f, 25.0f, speedHz);
    air = juce::jlimit (0.0f, 1.0f, airPercent / 100.0f);
    // The web version places Gain after EQ, filters and reverb, not inside the synth voice.
    gain = 1.0f;
    juce::ignoreUnused (outputGainDb);
}

void ArmoryVoice::setWobblePhases (double slowPhase, double fastPhase)
{
    wobblePhaseSlow = slowPhase;
    wobblePhaseFast = fastPhase;
}

void ArmoryVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    const auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    const auto sampleRate = getSampleRate();

    carrierPhase = 0.0;
    modulatorPhase = 0.0;
    baseCarrierDelta = twoPi * frequency / sampleRate;
    baseModulatorDelta = twoPi * frequency * metal / sampleRate;
    carrierDelta = baseCarrierDelta;
    modulatorDelta = baseModulatorDelta;
    level = velocity;
    envelope = 0.0f;
    envelopeStage = EnvelopeStage::attack;
    envelopeStageTime = 0.0;
    releaseStartEnvelope = 0.0f;
    releasing = false;
}

void ArmoryVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff && ! releasing)
    {
        releaseStartEnvelope = envelope;
        envelopeStageTime = 0.0;
        releasing = true;
    }
    else
    {
        if (! allowTailOff)
            clearCurrentNote();
    }
}

void ArmoryVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    constexpr auto toneInternalVoiceGain = 0.316227766f; // Tone.ModulationSynth internal voices are -10 dB.
    const auto sampleDuration = 1.0 / getSampleRate();
    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (releasing)
        {
            envelope = toneTargetRamp (releaseStartEnvelope, 0.0f, envelopeStageTime, releaseSeconds);
            if (envelopeStageTime >= releaseSeconds)
            {
                clearCurrentNote();
                break;
            }
        }
        else if (envelopeStage == EnvelopeStage::attack)
        {
            envelope = juce::jlimit (0.0f, 1.0f, static_cast<float> (envelopeStageTime / attackSeconds));
            if (envelopeStageTime >= attackSeconds)
            {
                envelope = 1.0f;
                envelopeStage = EnvelopeStage::decay;
                envelopeStageTime = 0.0;
            }
        }
        else if (envelopeStage == EnvelopeStage::decay)
        {
            envelope = toneTargetRamp (1.0f, sustainLevel, envelopeStageTime, decaySeconds);
            if (envelopeStageTime >= decaySeconds)
            {
                envelope = sustainLevel;
                envelopeStage = EnvelopeStage::sustain;
                envelopeStageTime = 0.0;
            }
        }

        // Tone.ModulationSynth defaults to a sine carrier and square modulator.
        // Tone.FMSynth routes `frequency -> modulationIndex -> modulation gain`,
        // then feeds the modulator into carrier.frequency. Consequently the FM
        // depth is the base pitch multiplied by Metal * 3, not a fixed-Hz offset.
        const auto modulator = std::sin (modulatorPhase) >= 0.0 ? 1.0f : -1.0f;
        const auto modulationIndex = metal * 3.0f;
        const auto baseFrequencyHz = baseCarrierDelta * getSampleRate() / twoPi;
        const auto modulationFrequencyOffset = modulator * envelope * level * toneInternalVoiceGain
                                             * static_cast<float> (baseFrequencyHz) * modulationIndex;
        const auto instantaneousCarrierDelta = carrierDelta + twoPi * modulationFrequencyOffset / getSampleRate();
        const auto value = static_cast<float> (std::sin (carrierPhase))
                         * envelope * level * gain * toneInternalVoiceGain;

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addSample (channel, startSample + sample, value);

        carrierPhase += instantaneousCarrierDelta;
        modulatorPhase += modulatorDelta;

        const auto cents = std::sin (wobblePhaseSlow) * (30.0 * wobble)
                         + std::sin (wobblePhaseFast) * (15.0 * wobble);
        const auto pitchRatio = std::exp2 (cents / 1200.0);
        carrierDelta = baseCarrierDelta * pitchRatio;
        modulatorDelta = baseModulatorDelta * pitchRatio;
        wobblePhaseSlow += twoPi * 0.2 / getSampleRate();
        wobblePhaseFast += twoPi * 3.1 / getSampleRate();
        if (wobblePhaseSlow >= twoPi) wobblePhaseSlow -= twoPi;
        if (wobblePhaseFast >= twoPi) wobblePhaseFast -= twoPi;

        carrierPhase = std::fmod (carrierPhase, twoPi);
        modulatorPhase = std::fmod (modulatorPhase, twoPi);
        envelopeStageTime += sampleDuration;
    }
}

ArmoryVSTAudioProcessor::ArmoryVSTAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ArmoryState", createParameterLayout())
{
    // Tail duration is declared in the processor interface so hosts retain the
    // final envelope and convolution tail after note-off.
    for (auto& sample : analyserSamples)
        sample.store (0.0f, std::memory_order_relaxed);

    for (auto& layer : lockedNotes)
        for (auto& note : layer)
            note.store (false, std::memory_order_relaxed);

    for (auto& synthesiser : synthesisers)
    {
        for (int index = 0; index < maxPianoPolyphony; ++index)
            synthesiser.addVoice (new ArmoryVoice());

        synthesiser.addSound (new ArmorySound());
    }

    for (int layer = 0; layer < numberOfLayers; ++layer)
    {
        const auto prefix = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer));
        activeParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Active");
        metalParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Metal");
        wobbleParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Wobble");
        transientParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Transient");
        durationParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Time");
        speedParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Speed");
        airParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Air");
        gainParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Gain");
        highPassParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "HP");
        lowPassParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "LP");
        reverbParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Verb");
        widthParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "Width");
        lowEqParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "EqLow");
        midEqParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "EqMid");
        highEqParameters[static_cast<size_t> (layer)] = parameters.getRawParameterValue (prefix + "EqHigh");
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout ArmoryVSTAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int layer = 0; layer < numberOfLayers; ++layer)
    {
        const auto prefix = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer));
        // The piano selector is an audition surface for all three independent
        // layers, so a fresh VST instance starts with every layer audible.
        // Individual PWR controls still preserve the original enable logic.
        layout.add (std::make_unique<juce::AudioParameterBool> (prefix + "Active", prefix + " Active", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Metal", prefix + " Metal", juce::NormalisableRange<float> (0.1f, 10.0f, 0.1f), 3.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Wobble", prefix + " Wobble", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Transient", prefix + " Shape", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Time", prefix + " Time", juce::NormalisableRange<float> (0.1f, 5.0f, 0.1f), 1.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Speed", prefix + " Speed", juce::NormalisableRange<float> (0.5f, 25.0f, 0.5f), 8.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Air", prefix + " Air", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Gain", prefix + " Gain", juce::NormalisableRange<float> (-20.0f, 10.0f, 0.5f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "HP", prefix + " High Pass", juce::NormalisableRange<float> (20.0f, 1000.0f, 10.0f), 20.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "LP", prefix + " Low Pass", juce::NormalisableRange<float> (1000.0f, 20000.0f, 100.0f), 20000.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Verb", prefix + " Reverb", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Width", prefix + " Width", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "EqLow", prefix + " Low EQ", juce::NormalisableRange<float> (-20.0f, 20.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "EqMid", prefix + " Mid EQ", juce::NormalisableRange<float> (-20.0f, 20.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "EqHigh", prefix + " High EQ", juce::NormalisableRange<float> (-20.0f, 20.0f, 1.0f), 0.0f));
    }
    return layout;
}

void ArmoryVSTAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    reverbTailSamplesRemaining.fill (0);
    for (auto& synthesiser : synthesisers)
        synthesiser.setCurrentPlaybackSampleRate (sampleRate);

    for (int layer = 0; layer < numberOfLayers; ++layer)
    {
        auto& motionSpeed = motionSpeeds[static_cast<size_t> (layer)];
        motionSpeed.reset (sampleRate, 0.1);
        motionSpeed.setCurrentAndTargetValue (speedParameters[static_cast<size_t> (layer)]->load());
    }

    // Keep the sound-design chain stereo internally. A mono host bus is
    // downmixed only at the final output stage, so Width = 0 and a negotiated
    // mono output layout are both safe without asking a stereo convolution to
    // process an undersized buffer.
    for (auto& layerBuffer : layerBuffers)
        layerBuffer.setSize (2, samplesPerBlock, false, false, true);

    // Tone.Reverb is a normalised stereo convolution of two independent white
    // noise bursts, with a 10 ms predelay and a two-second exponential tail.
    // Create equivalent, stable IRs here instead of using JUCE's room reverb.
    constexpr auto reverbPreDelay = 0.01;
    constexpr auto reverbDecay = 2.0;
    const auto preDelaySamples = static_cast<int> (std::round (sampleRate * reverbPreDelay));
    const auto irSamples = preDelaySamples + static_cast<int> (std::ceil (sampleRate * reverbDecay));
    const auto decayTimeConstant = std::log (reverbDecay + 1.0) / std::log (200.0);

    for (int layer = 0; layer < numberOfLayers; ++layer)
    {
        juce::AudioBuffer<float> impulse (2, irSamples);
        auto randomState = static_cast<juce::uint32> (0x5f3759dfu + 0x9e3779b9u * static_cast<juce::uint32> (layer + 1));
        const auto nextNoise = [&randomState]
        {
            randomState = randomState * 1664525u + 1013904223u;
            return static_cast<float> ((randomState >> 8) * (2.0 / 16777215.0) - 1.0);
        };

        for (int sample = preDelaySamples; sample < irSamples; ++sample)
        {
            const auto elapsed = static_cast<double> (sample - preDelaySamples) / sampleRate;
            const auto envelope = static_cast<float> (std::exp (-elapsed / decayTimeConstant));
            impulse.setSample (0, sample, nextNoise() * envelope);
            impulse.setSample (1, sample, nextNoise() * envelope);
        }

        auto& reverb = reverbs[static_cast<size_t> (layer)];
        reverb.loadImpulseResponse (std::move (impulse), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
        reverb.prepare ({ sampleRate, static_cast<juce::uint32> (samplesPerBlock), 2 });
        reverb.reset();
        reverbDryBuffers[static_cast<size_t> (layer)].setSize (2, samplesPerBlock, false, false, true);
    }

    for (auto& layerFilters : airFilters)
        for (auto& filter : layerFilters)
            filter.reset();

    for (auto& filters : highPassFilters)
        for (auto& filter : filters)
            filter.reset();

    for (auto& filters : lowPassFilters)
        for (auto& filter : filters)
            filter.reset();

    for (auto* filterBank : { &lowEqLowPassFilters, &midEqHighPassFilters,
                              &midEqLowPassFilters, &highEqHighPassFilters })
        for (auto& filters : *filterBank)
            for (auto& filter : filters)
                filter.reset();

    const juce::dsp::ProcessSpec masterSpec { sampleRate, static_cast<juce::uint32> (samplesPerBlock),
                                              static_cast<juce::uint32> (getTotalNumOutputChannels()) };
    masterLimiter.prepare (masterSpec);
    masterLimiter.reset();
    // Tone.Limiter(-1) wraps a DynamicsCompressorNode with these settings.
    masterLimiter.setThreshold (-1.0f);
    masterLimiter.setRatio (20.0f);
    masterLimiter.setAttack (3.0f);
    masterLimiter.setRelease (10.0f);
}

bool ArmoryVSTAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void ArmoryVSTAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Hosts are allowed to exercise a processor during format probing and
    // parameter fuzzing. Never let an invalid render state escape into the
    // host's output buffer: apart from being unsafe for downstream plug-ins,
    // one NaN would otherwise poison the analyser and all later bus samples.
    if (! std::isfinite (currentSampleRate) || currentSampleRate <= 0.0)
        return;

    // The DSP itself always runs in stereo. Reject only truly invalid layouts;
    // a negotiated mono bus is mixed down at the final output stage below.
    if (buffer.getNumChannels() < 1 || buffer.getNumChannels() > 2)
        return;

    for (int layer = 0; layer < numberOfLayers; ++layer)
    {
        const auto layerIndex = static_cast<size_t> (layer);
        juce::MidiBuffer keyboardMessages;
        keyboardStates[layerIndex].processNextMidiBuffer (keyboardMessages, 0, buffer.getNumSamples(), true);

        for (int note = 0; note < 128; ++note)
        {
            auto& remainingSamples = shotSamplesRemaining[layerIndex][static_cast<size_t> (note)];
            const auto remaining = remainingSamples.load (std::memory_order_acquire);
            if (remaining <= 0)
                continue;

            if (remaining <= buffer.getNumSamples())
            {
                keyboardMessages.addEvent (juce::MidiMessage::noteOff (1, note), static_cast<int> (remaining - 1));
                keyboardStates[layerIndex].noteOff (1, note, 0.0f);
                remainingSamples.store (0, std::memory_order_release);
            }
            else
            {
                remainingSamples.store (remaining - buffer.getNumSamples(), std::memory_order_release);
            }
        }

        const auto isActive = activeParameters[layerIndex]->load() >= 0.5f;
        if (! isActive)
        {
            if (layerWasActive[layerIndex])
                synthesisers[layerIndex].allNotesOff (0, true);

            layerWasActive[layerIndex] = false;
        }
        else
        {
            layerWasActive[layerIndex] = true;
        }

        auto& layerBuffer = layerBuffers[layerIndex];
        if (layerBuffer.getNumChannels() != buffer.getNumChannels() || layerBuffer.getNumSamples() < buffer.getNumSamples())
            layerBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        layerBuffer.clear();

        juce::MidiBuffer layerMessages;

        if (isActive)
        {
            // Hardware/host MIDI keeps the chord-routing behaviour, but events
            // originating from the on-screen piano are direct auditions. A
            // left-click must always play exactly the clicked key even when
            // this layer has one or more right-clicked locks.
            transformMidiForLayer (layer, midiMessages, layerMessages);
            for (const auto keyboardEvent : keyboardMessages)
                layerMessages.addEvent (keyboardEvent.getMessage(), keyboardEvent.samplePosition);
        }

        const auto hasNewNote = std::any_of (layerMessages.begin(), layerMessages.end(), [] (const auto metadata)
        {
            return metadata.getMessage().isNoteOn();
        });
        const auto hasActiveVoice = layerHasActiveVoices (layer);
        auto& reverbTailSamples = reverbTailSamplesRemaining[layerIndex];

        // Inactive layers previously still ran every filter and convolution
        // on every audio block. Skip them only after their audible tail is
        // exhausted, so the change has no effect on note or reverb timing.
        if (! hasNewNote && ! hasActiveVoice && reverbTailSamples <= 0)
            continue;

        auto& synthesiser = synthesisers[layerIndex];
        if (wobblePhaseResetRequests[layerIndex].exchange (false, std::memory_order_acq_rel))
            wobbleSlowPhases[layerIndex] = twoPi * wobblePhaseRandom.nextDouble();

        for (int index = 0; index < synthesiser.getNumVoices(); ++index)
            if (auto* voice = dynamic_cast<ArmoryVoice*> (synthesiser.getVoice (index));
                hasNewNote || voice->isVoiceActive())
            {
                voice->setTone (metalParameters[layerIndex]->load(), wobbleParameters[layerIndex]->load(), transientParameters[layerIndex]->load(), durationParameters[layerIndex]->load(), speedParameters[layerIndex]->load(), airParameters[layerIndex]->load(), gainParameters[layerIndex]->load());
                voice->setWobblePhases (wobbleSlowPhases[layerIndex], wobbleFastPhases[layerIndex]);
            }

        synthesiser.renderNextBlock (layerBuffer, layerMessages, 0, buffer.getNumSamples());
        wobbleSlowPhases[layerIndex] += twoPi * 0.2 * buffer.getNumSamples() / currentSampleRate;
        wobbleFastPhases[layerIndex] += twoPi * 3.1 * buffer.getNumSamples() / currentSampleRate;
        wobbleSlowPhases[layerIndex] = std::fmod (wobbleSlowPhases[layerIndex], twoPi);
        wobbleFastPhases[layerIndex] = std::fmod (wobbleFastPhases[layerIndex], twoPi);

        // Faithful counterpart of the web signal chain: AutoFilter -> Tremolo.
        // Air is the AutoFilter wet amount; Speed drives both the filter sweep and motion.
        const auto air = airParameters[layerIndex]->load() / 100.0f;
        const auto autoFilterDryGain = std::cos (juce::MathConstants<float>::halfPi * air);
        const auto autoFilterWetGain = std::sin (juce::MathConstants<float>::halfPi * air);
        auto& motionSpeed = motionSpeeds[layerIndex];
        motionSpeed.setTargetValue (speedParameters[layerIndex]->load());
        const auto width = widthParameters[layerIndex]->load() / 100.0f;
        auto& motionPhase = motionPhases[layerIndex];
        const auto spread = juce::MathConstants<double>::pi * width;
        const auto leftPhaseOffset = juce::MathConstants<double>::halfPi - spread * 0.5;
        const auto rightPhaseOffset = juce::MathConstants<double>::halfPi + spread * 0.5;
        constexpr auto autoFilterMinimum = 200.0f;
        const auto autoFilterMaximum = autoFilterMinimum * std::exp2 (2.6f);

        constexpr auto autoFilterControlInterval = 16;
        for (int sample = 0; sample < layerBuffer.getNumSamples(); ++sample)
        {
            const auto sweep = static_cast<float> (0.5 + 0.5 * std::sin (motionPhase));
            // Tone.AutoFilter maps its sine LFO linearly between baseFrequency and
            // baseFrequency * 2^octaves (200 Hz and roughly 1213 Hz by default).
            const auto autoFilterFrequency = autoFilterMinimum + sweep * (autoFilterMaximum - autoFilterMinimum);
            const auto autoFilterQ = 1.0f + air * 5.0f;

            // Coefficients are expensive to rebuild. Updating them at a
            // 16-sample control rate remains far above the modulation band,
            // while avoiding thousands of allocations/calculations per block.
            if (sample % autoFilterControlInterval == 0)
                for (int channel = 0; channel < layerBuffer.getNumChannels(); ++channel)
                    airFilters[layerIndex][static_cast<size_t> (channel)].setCoefficients (
                        juce::IIRCoefficients::makeBandPass (currentSampleRate, autoFilterFrequency, autoFilterQ));

            for (int channel = 0; channel < layerBuffer.getNumChannels(); ++channel)
            {
                auto* samples = layerBuffer.getWritePointer (channel);
                const auto dry = samples[sample];
                // Tone.Filter is a WebAudio BiquadFilter with a -12 dB/oct
                // slope. JUCE's biquad coefficients use the same RBJ form;
                // updating them per sample preserves Tone's continuously
                // moving AutoFilter frequency.
                auto& airFilter = airFilters[layerIndex][static_cast<size_t> (channel)];
                const auto filtered = airFilter.processSingleSampleRaw (dry);
                const auto tremoloPhase = motionPhase + (channel == 0 ? leftPhaseOffset : rightPhaseOffset);
                // Tone.Tremolo defaults to depth 0.5: its inverted LFO ranges from 0.25 to 0.75.
                const auto tremolo = 0.5f - 0.25f * static_cast<float> (std::sin (tremoloPhase));
                // Tone.AutoFilter inherits Effect's equal-power CrossFade: wet 0
                // is dry, wet 1 is fully filtered, without a level dip at 50%.
                samples[sample] = (dry * autoFilterDryGain + filtered * autoFilterWetGain) * tremolo;
            }

            motionPhase += twoPi * motionSpeed.getNextValue() / currentSampleRate;
            if (motionPhase >= twoPi)
                motionPhase -= twoPi;
        }

        const auto highPass = highPassParameters[layerIndex]->load();
        const auto lowPass = lowPassParameters[layerIndex]->load();
        // Tone.EQ3 uses Tone.MultibandSplit: independent second-order LP/HP
        // paths at 400 Hz and 2.5 kHz, all at Q=1. Apply their gains before
        // HP/LP, just as the browser chain does.
        const auto lowEqGain = juce::Decibels::decibelsToGain (lowEqParameters[layerIndex]->load());
        const auto midEqGain = juce::Decibels::decibelsToGain (midEqParameters[layerIndex]->load());
        const auto highEqGain = juce::Decibels::decibelsToGain (highEqParameters[layerIndex]->load());

        for (int channel = 0; channel < layerBuffer.getNumChannels(); ++channel)
        {
            lowEqLowPassFilters[layerIndex][static_cast<size_t> (channel)].setCoefficients (
                juce::IIRCoefficients::makeLowPass (currentSampleRate, 400.0, 1.0));
            midEqHighPassFilters[layerIndex][static_cast<size_t> (channel)].setCoefficients (
                juce::IIRCoefficients::makeHighPass (currentSampleRate, 400.0, 1.0));
            midEqLowPassFilters[layerIndex][static_cast<size_t> (channel)].setCoefficients (
                juce::IIRCoefficients::makeLowPass (currentSampleRate, 2500.0, 1.0));
            highEqHighPassFilters[layerIndex][static_cast<size_t> (channel)].setCoefficients (
                juce::IIRCoefficients::makeHighPass (currentSampleRate, 2500.0, 1.0));
        }

        for (int sample = 0; sample < layerBuffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < layerBuffer.getNumChannels(); ++channel)
            {
                auto* samples = layerBuffer.getWritePointer (channel);
                const auto source = samples[sample];
                const auto channelIndex = static_cast<size_t> (channel);
                const auto low = lowEqLowPassFilters[layerIndex][channelIndex].processSingleSampleRaw (source);
                const auto midHighPassed = midEqHighPassFilters[layerIndex][channelIndex].processSingleSampleRaw (source);
                const auto mid = midEqLowPassFilters[layerIndex][channelIndex].processSingleSampleRaw (midHighPassed);
                const auto high = highEqHighPassFilters[layerIndex][channelIndex].processSingleSampleRaw (source);
                samples[sample] = low * lowEqGain + mid * midEqGain + high * highEqGain;
            }
        }

        for (int channel = 0; channel < layerBuffer.getNumChannels(); ++channel)
        {
            auto& highPassFilter = highPassFilters[layerIndex][static_cast<size_t> (channel)];
            auto& lowPassFilter = lowPassFilters[layerIndex][static_cast<size_t> (channel)];
            // Tone.Filter defaults to a single -12 dB/oct BiquadFilter with Q=1.
            highPassFilter.setCoefficients (juce::IIRCoefficients::makeHighPass (currentSampleRate, highPass, 1.0));
            lowPassFilter.setCoefficients (juce::IIRCoefficients::makeLowPass (currentSampleRate, lowPass, 1.0));
            highPassFilter.processSamples (layerBuffer.getWritePointer (channel), layerBuffer.getNumSamples());
            lowPassFilter.processSamples (layerBuffer.getWritePointer (channel), layerBuffer.getNumSamples());
        }

        // Tone.Effect uses equal-power CrossFade rather than a linear dry/wet mix.
        const auto reverbWet = juce::jlimit (0.0f, 1.0f, reverbParameters[layerIndex]->load() / 100.0f);
        if (reverbWet > 0.0f)
        {
            auto& reverbDryBuffer = reverbDryBuffers[layerIndex];
            if (reverbDryBuffer.getNumSamples() < layerBuffer.getNumSamples())
                reverbDryBuffer.setSize (2, layerBuffer.getNumSamples(), false, false, true);
            reverbDryBuffer.makeCopyOf (layerBuffer, true);

            juce::dsp::AudioBlock<float> reverbBlock (layerBuffer);
            juce::dsp::ProcessContextReplacing<float> reverbContext (reverbBlock);
            reverbs[layerIndex].process (reverbContext);

            const auto dryGain = std::cos (juce::MathConstants<float>::halfPi * reverbWet);
            const auto wetGain = std::sin (juce::MathConstants<float>::halfPi * reverbWet);
            for (int channel = 0; channel < layerBuffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < layerBuffer.getNumSamples(); ++sample)
                    layerBuffer.setSample (channel, sample, reverbDryBuffer.getSample (channel, sample) * dryGain
                                                            + layerBuffer.getSample (channel, sample) * wetGain);
        }
        else
        {
            reverbs[layerIndex].reset();
            reverbTailSamples = 0;
        }

        if (layerHasActiveVoices (layer))
            reverbTailSamples = static_cast<juce::int64> (std::ceil (currentSampleRate * 2.01));
        else
            reverbTailSamples = juce::jmax<juce::int64> (0, reverbTailSamples - layerBuffer.getNumSamples());

        layerBuffer.applyGain (juce::Decibels::decibelsToGain (gainParameters[layerIndex]->load()));

        if (buffer.getNumChannels() == 1)
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.addSample (0, sample, 0.5f * (layerBuffer.getSample (0, sample)
                                                    + layerBuffer.getSample (1, sample)));
        }
        else
        {
            buffer.addFrom (0, 0, layerBuffer, 0, 0, buffer.getNumSamples());
            buffer.addFrom (1, 0, layerBuffer, 1, 0, buffer.getNumSamples());
        }
    }

    juce::dsp::AudioBlock<float> masterBlock (buffer);
    juce::dsp::ProcessContextReplacing<float> masterContext (masterBlock);
    masterLimiter.process (masterContext);

    // The web Audio graph silently contains non-finite intermediate values;
    // native hosts do not. Keep the boundary deterministic if a third-party
    // host restores an unusual parameter/block state while DSP nodes are
    // settling. The normal signal path is unchanged.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (samples[sample]))
                samples[sample] = 0.0f;
    }

    if (buffer.getNumChannels() > 0)
    {
        auto writePosition = analyserWritePosition.load (std::memory_order_relaxed);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            auto mono = buffer.getSample (0, sample);
            if (buffer.getNumChannels() > 1)
                mono = 0.5f * (mono + buffer.getSample (1, sample));
            analyserSamples[writePosition % analyserSamples.size()].store (mono, std::memory_order_relaxed);
            ++writePosition;
        }
        analyserWritePosition.store (writePosition, std::memory_order_release);
    }
}

bool ArmoryVSTAudioProcessor::layerHasActiveVoices (int layer) const
{
    const auto& synthesiser = synthesisers[static_cast<size_t> (layer)];
    for (int index = 0; index < synthesiser.getNumVoices(); ++index)
        if (const auto* voice = synthesiser.getVoice (index); voice != nullptr && voice->isVoiceActive())
            return true;

    return false;
}

void ArmoryVSTAudioProcessor::copyAnalyserSamples (std::array<float, 2048>& destination) const
{
    const auto writePosition = analyserWritePosition.load (std::memory_order_acquire);
    for (size_t index = 0; index < destination.size(); ++index)
    {
        const auto source = (writePosition + static_cast<juce::uint32> (index))
                          % static_cast<juce::uint32> (analyserSamples.size());
        destination[index] = analyserSamples[source].load (std::memory_order_relaxed);
    }
}

void ArmoryVSTAudioProcessor::enqueueKeyboardNote (int layer, int midiNote, bool isNoteOn)
{
    if (! juce::isPositiveAndBelow (layer, numberOfLayers) || ! juce::isPositiveAndBelow (midiNote, 128))
        return;

    auto& keyboard = keyboardStates[static_cast<size_t> (layer)];
    if (isNoteOn)
    {
        wobblePhaseResetRequests[static_cast<size_t> (layer)].store (true, std::memory_order_release);
        keyboard.noteOn (1, midiNote, 1.0f);
    }
    else
        keyboard.noteOff (1, midiNote, 0.0f);
}

void ArmoryVSTAudioProcessor::enqueueKeyboardShot (int layer, int midiNote, float gateSeconds)
{
    if (! juce::isPositiveAndBelow (layer, numberOfLayers) || ! juce::isPositiveAndBelow (midiNote, 128))
        return;

    const auto layerIndex = static_cast<size_t> (layer);
    wobblePhaseResetRequests[layerIndex].store (true, std::memory_order_release);
    shotSamplesRemaining[layerIndex][static_cast<size_t> (midiNote)].store (
        juce::jmax<juce::int64> (1, static_cast<juce::int64> (std::llround (gateSeconds * currentSampleRate))),
        std::memory_order_release);
    keyboardStates[layerIndex].noteOn (1, midiNote, 1.0f);
}

void ArmoryVSTAudioProcessor::scheduleKeyboardRelease (int layer, int midiNote, float gateSeconds)
{
    if (! juce::isPositiveAndBelow (layer, numberOfLayers) || ! juce::isPositiveAndBelow (midiNote, 128))
        return;

    shotSamplesRemaining[static_cast<size_t> (layer)][static_cast<size_t> (midiNote)].store (
        juce::jmax<juce::int64> (1, static_cast<juce::int64> (std::llround (gateSeconds * currentSampleRate))),
        std::memory_order_release);
}

bool ArmoryVSTAudioProcessor::toggleLayerLockedNote (int layer, int midiNote)
{
    if (! juce::isPositiveAndBelow (layer, numberOfLayers) || ! juce::isPositiveAndBelow (midiNote, 128))
        return false;

    auto& value = lockedNotes[static_cast<size_t> (layer)][static_cast<size_t> (midiNote)];
    const auto newValue = ! value.load (std::memory_order_relaxed);
    value.store (newValue, std::memory_order_relaxed);
    return newValue;
}

void ArmoryVSTAudioProcessor::setLayerLockedNote (int layer, int midiNote)
{
    if (! juce::isPositiveAndBelow (layer, numberOfLayers) || ! juce::isPositiveAndBelow (midiNote, 128))
        return;

    clearLayerLockedNotes (layer);
    lockedNotes[static_cast<size_t> (layer)][static_cast<size_t> (midiNote)].store (true, std::memory_order_relaxed);
}

void ArmoryVSTAudioProcessor::clearLayerLockedNotes (int layer)
{
    if (! juce::isPositiveAndBelow (layer, numberOfLayers))
        return;

    for (auto& note : lockedNotes[static_cast<size_t> (layer)])
        note.store (false, std::memory_order_relaxed);
}

ArmoryVSTAudioProcessor::LockedNoteArray ArmoryVSTAudioProcessor::getLayerLockedNotes (int layer) const
{
    LockedNoteArray result {};
    if (! juce::isPositiveAndBelow (layer, numberOfLayers))
        return result;

    for (int note = 0; note < 128; ++note)
        result[static_cast<size_t> (note)] = lockedNotes[static_cast<size_t> (layer)][static_cast<size_t> (note)].load (std::memory_order_relaxed);

    return result;
}

void ArmoryVSTAudioProcessor::transformMidiForLayer (int layer, const juce::MidiBuffer& input, juce::MidiBuffer& output)
{
    auto& triggeredForLayer = activeTriggeredNotes[static_cast<size_t> (layer)];

    for (const auto metadata : input)
    {
        const auto message = metadata.getMessage();
        const auto samplePosition = metadata.samplePosition;

        if (message.isNoteOn())
        {
            const auto inputNote = message.getNoteNumber();
            auto& triggeredNotes = triggeredForLayer[static_cast<size_t> (inputNote)];
            triggeredNotes.reset();

            auto hasLocks = false;
            for (int note = 0; note < 128; ++note)
                hasLocks = hasLocks || lockedNotes[static_cast<size_t> (layer)][static_cast<size_t> (note)].load (std::memory_order_relaxed);

            for (int note = 0; note < 128; ++note)
            {
                const auto shouldTrigger = hasLocks
                    ? lockedNotes[static_cast<size_t> (layer)][static_cast<size_t> (note)].load (std::memory_order_relaxed)
                    : note == inputNote;

                if (shouldTrigger)
                {
                    output.addEvent (juce::MidiMessage::noteOn (message.getChannel(), note, message.getFloatVelocity()), samplePosition);
                    triggeredNotes.set (static_cast<size_t> (note));
                }
            }
        }
        else if (message.isNoteOff())
        {
            const auto inputNote = message.getNoteNumber();
            auto& triggeredNotes = triggeredForLayer[static_cast<size_t> (inputNote)];

            for (int note = 0; note < 128; ++note)
                if (triggeredNotes.test (static_cast<size_t> (note)))
                    output.addEvent (juce::MidiMessage::noteOff (message.getChannel(), note), samplePosition);

            triggeredNotes.reset();
        }
        else
        {
            output.addEvent (message, samplePosition);
        }
    }
}

juce::String ArmoryVSTAudioProcessor::serialiseLockedNotes (int layer) const
{
    juce::StringArray notes;
    for (int note = 0; note < 128; ++note)
        if (lockedNotes[static_cast<size_t> (layer)][static_cast<size_t> (note)].load (std::memory_order_relaxed))
            notes.add (juce::String (note));

    return notes.joinIntoString (",");
}

void ArmoryVSTAudioProcessor::restoreLockedNotes (int layer, const juce::String& value)
{
    const auto tokens = juce::StringArray::fromTokens (value, ",", "");
    for (int note = 0; note < 128; ++note)
    {
        const auto isLocked = tokens.contains (juce::String (note));
        lockedNotes[static_cast<size_t> (layer)][static_cast<size_t> (note)].store (isLocked, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* ArmoryVSTAudioProcessor::createEditor()
{
    return new ArmoryVSTAudioProcessorEditor (*this);
}

void ArmoryVSTAudioProcessor::getStateInformation (juce::MemoryBlock& destinationData)
{
    if (auto state = parameters.copyState(); state.isValid())
    {
        // Keep a single fresh copy of the non-parameter piano lock state.
        for (int child = state.getNumChildren(); --child >= 0;)
            if (state.getChild (child).hasType ("LockedNotes"))
                state.removeChild (child, nullptr);
        juce::ValueTree lockedNoteState ("LockedNotes");
        for (int layer = 0; layer < numberOfLayers; ++layer)
            lockedNoteState.setProperty ("layer" + juce::String (layer), serialiseLockedNotes (layer), nullptr);

        state.addChild (lockedNoteState, -1, nullptr);
        copyXmlToBinary (*state.createXml(), destinationData);
    }
}

void ArmoryVSTAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);

            const auto lockedNoteState = state.getChildWithName ("LockedNotes");
            for (int layer = 0; layer < numberOfLayers; ++layer)
                restoreLockedNotes (layer, lockedNoteState.getProperty ("layer" + juce::String (layer), {}).toString());

            // The child has served its purpose; do not keep it in APVTS or a
            // later save could accumulate stale copies of it.
            for (int child = state.getNumChildren(); --child >= 0;)
                if (state.getChild (child).hasType ("LockedNotes"))
                    state.removeChild (child, nullptr);
            parameters.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ArmoryVSTAudioProcessor();
}


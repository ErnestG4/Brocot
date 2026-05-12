#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include "audio/BrocotVoice.h"
#include "audio/FilterBank.h"
#include "audio/LFO.h"
#include "params/ParameterTree.h"
#include "params/sources/MIDISource.h"
#include "params/sources/EnvSource.h"
#include "params/ModTarget.h"
#include "presets/PresetManager.h"

namespace brocot
{

class BrocotAudioProcessor : public juce::AudioProcessor
{
public:
    BrocotAudioProcessor();
    ~BrocotAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return true;  }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState&       getAPVTS()       noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }

    /** Snapshot the most-recent `dest.size()` samples of the mono mix into
        `dest`. Returns false if no audio has been written yet. Safe to call
        from any thread; uses relaxed atomic ordering. */
    bool snapshotRecentAudio (std::vector<float>& dest) const noexcept;

    PresetManager& getPresetManager() noexcept { return presetManager; }

    /** Exposed for the editor's on-screen MidiKeyboardComponent. Notes
        played on the on-screen keyboard inject events into this state;
        processBlock copies them into the MIDI buffer before the voice
        allocator runs. */
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    /** UI-thread accessors for per-operator paths. Audio thread reads only
        the cached ratio (atomic), never the string itself. */
    juce::String getOpPath (int idx) const;
    void setOpPath (int idx, const juce::String& pathString);

    /** Shared-path accessors. Mutating the shared path triggers a refresh of
        every operator's cached ratio (so position-along-shared resolves
        correctly without the audio thread touching the string). */
    juce::String getSharedPath() const;
    void setSharedPath (const juce::String& pathString);
    bool isSharedMode() const;

private:
    juce::AudioProcessorValueTreeState apvts;

    static constexpr int kMaxVoices = 2;
    std::array<BrocotVoice, kMaxVoices> voices {};
    std::array<juce::int64, kMaxVoices> voiceNoteOnOrder {};
    juce::int64 noteOnCounter { 0 };
    FilterBank  filter;
    LFO         lfo;

    bool isDuoMode() const noexcept;
    int  allocateVoice (int midiNote) noexcept;
    void noteOffOnAnyVoice (int midiNote) noexcept;
    void applyParametersToAllVoices();

    // MIDI-derived modulation sources, shared across the (currently single)
    // voice. Velocity moves to per-voice ownership when Slice 15 lights up
    // duo polyphony.
    MIDIModWheelSource         modWheelSource;
    MIDIPitchBendSource        pitchBendSource;
    MIDIChannelPressureSource  pressureSource;
    MIDIVelocitySource         velocitySource;
    AmpEnvSource               ampEnvSource;

    // Path data lives outside APVTS (paths aren't host-automatable). The
    // string representation is what the UI / preset code touches; the audio
    // thread only reads `opRatios` (atomic cache, recomputed on every path
    // mutation). pathsLock guards string reads/writes; ratios use relaxed
    // atomics so the audio thread never blocks on a UI update.
    juce::String sharedPath { "RLLLR" };
    std::array<juce::String, params::kNumOperators> opPaths {};
    std::array<std::atomic<float>, params::kNumOperators> opRatios {};
    juce::CriticalSection pathsLock;

    // Lock-free scope buffer. The audio thread writes the rendered mono
    // mix into `scopeRing` and bumps `scopeWritePos`; the UI thread
    // snapshots the last N samples relative to that index. Capacity is
    // a power of two so wrapping is a mask.
    static constexpr int kScopeBufferSize = 4096;
    std::array<float, kScopeBufferSize> scopeRing {};
    std::atomic<int> scopeWritePos { 0 };
    std::atomic<bool> scopeHasData { false };

    void writeScopeSamples (const float* samples, int n) noexcept;

    PresetManager presetManager { *this };

    juce::MidiKeyboardState keyboardState;

    void refreshSharedRatios();
    float midiNoteWithBend (int noteNumber) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrocotAudioProcessor)
};

} // namespace brocot

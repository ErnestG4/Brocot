#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include "audio/BrocotVoice.h"
#include "audio/FilterBank.h"
#include "audio/LFO.h"
#include "audio/UniversalModulator.h"
#include "params/ParameterTree.h"
#include "params/sources/MIDISource.h"
#include "params/sources/EnvSource.h"
#include "params/ModTarget.h"
#include "presets/PresetManager.h"

namespace brocot
{

class BrocotAudioProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
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

    /** Per-modulator path (used when the mod's lock target is non-Free).
        UI / preset thread accessors only — audio thread reads modRatios. */
    juce::String getModPath (int idx) const;
    void setModPath (int idx, const juce::String& pathString);

    /** UI-facing accessor for the modulator's live resolved rate (Hz)
        after the lock-graph walk. The bank UI calls this on a timer to
        show "x.xx Hz" readouts. */
    float getResolvedModRate (int idx) const;

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
    // Default shared path is 32 chars long so every position from 0..32 maps
    // to a distinct ratio. First 5 chars are "RLLLR" so Avatar·1 (positions
    // 1/3/5 → 2/1, 4/3, 9/7) is preserved at startup; the remainder is an
    // alternating LR extension that keeps mediant growth Fibonacci-bounded
    // (no int overflow) while giving variety at every deeper position.
    juce::String sharedPath { "RLLLRLRLRLRLRLRLRLRLRLRLRLRLRLRL" };
    std::array<juce::String, params::kNumOperators> opPaths {};
    std::array<std::atomic<float>, params::kNumOperators> opRatios {};
    juce::CriticalSection pathsLock;

    // V2 modulator bank. Each mod owns a path string (for lock-target SB
    // ratio) just like ops do; the audio thread reads only the atomic ratio.
    std::array<UniversalModulator, params::kNumModulators> modulators {};
    std::array<juce::String, params::kNumModulators> modPaths {};
    std::array<std::atomic<float>, params::kNumModulators> modRatios {};
    // Scratch buffer for the last computed mod outputs each block — V2.4
    // routing reads these. Bipolar; depth is already applied by the mod.
    std::array<float, params::kNumModulators> modOutputs {};

    void applyModulatorParameters();
    /** Recursively resolves the rate (Hz) for modulator `idx`, following
        lock-target indirection. `visited[i]=true` marks mod `i` as
        currently-being-resolved to break cycles (fallback: free rate).
        Audio-thread safe — reads atomic params only. */
    float resolveModRate (int idx, std::array<bool, params::kNumModulators>& visited) const noexcept;

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

    // APVTS listener — called whenever a path-mode or position parameter
    // changes. Defers the actual recompute to handleAsyncUpdate() so the
    // string + ratio update always runs on the message thread, even if the
    // host changed the parameter from the audio thread.
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrocotAudioProcessor)
};

} // namespace brocot

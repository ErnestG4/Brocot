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
#include "pull/PullIndex.h"
#include "pull/Landscape.h"

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

    /** Carrier frequency of the most recent note-on (bend included), for
        display math that needs an absolute Hz reference — the map field's
        separations scale with the carrier, so the readout shows them at the
        note actually played. Defaults to 220 Hz before any note. */
    double getLastNoteHz() const noexcept { return lastNoteHz.load (std::memory_order_relaxed); }

    PresetManager& getPresetManager() noexcept { return presetManager; }

    /** Runtime pull-to-nearest table. Loaded once at construction;
        returns an empty index if `resources/pull_index.json` wasn't
        found (the steer-tab UI shows a "rebuild the index" hint in
        that case). */
    const PullIndex& getPullIndex() const noexcept { return pullIndex; }

    // Per-depth landscape maps (4/8/16/32/64 + the 6-op default). getLandscape()
    // returns the ACTIVE map, so SteerTab + CloudView steer/navigate whichever
    // map is selected.
    const Landscape& getLandscape() const noexcept;
    int  getMapCount() const noexcept { return (int) maps.size(); }
    int  getMapDepth (int i) const noexcept;
    juce::String getMapName (int i) const;
    int  getActiveMap() const noexcept { return activeMap; }
    void setActiveMap (int i);

    /** Snapshot the current operator bank as `OpState`s the PullIndex
        morph helper can consume. Length is always `kNumOperators`. */
    std::vector<PullIndex::OpState> getCurrentOps() const;

    /** Inverse of `getCurrentOps` — push a vector of OpStates back into
        the APVTS + opPaths. Length is clipped to `kNumOperators` if
        the caller hands in fewer (or more); shorter inputs leave the
        tail untouched. Use to apply a morph result. */
    void applyOps (const std::vector<PullIndex::OpState>& states);

    /** Op-1 (index 0) self-feedback, in the 0..1 param convention — read for
        a steer session's start, written as the steer walk crosses landscape
        waypoints that carry feedback. */
    float getOp0Feedback() const;
    void  applyOp0Feedback (float value01);

    /** Continuous per-op "steered ratio" override (groove steering). While
        active for an op, the voice plays this exact ratio instead of the
        path-derived rational + detune — so steering can glide continuously
        through relative frequency and lock into Farey grooves. `ratios` is
        indexed by operator; a value <= 0 means "no override for this op".
        Set per timer tick during a steer drag; clear on release. */
    void applySteeredRatios (const std::vector<float>& ratios);
    void clearSteeredRatios();

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

    // Cached APVTS atomic pointers for the per-block hot path. Resolving the
    // "opN.suffix" ids by string on every block (× kMaxVoices × kNumOperators)
    // built and hashed millions of juce::Strings/sec ON THE AUDIO THREAD — a
    // CPU cost and a real-time-safety hazard (heap alloc in processBlock).
    // These pointers are stable for the APVTS's lifetime, so we resolve them
    // once (cacheParameterPointers) and read through them in the audio loop.
    struct OpParamPtrs
    {
        std::atomic<float>* enabled  { nullptr };
        std::atomic<float>* depth    { nullptr };
        std::atomic<float>* detune   { nullptr };
        std::atomic<float>* keyScale { nullptr };
        std::atomic<float>* waveform { nullptr };
        std::atomic<float>* duty     { nullptr };
        std::atomic<float>* evensup  { nullptr };
        std::atomic<float>* fold     { nullptr };
    };
    std::array<OpParamPtrs, params::kNumOperators> opParams {};
    std::atomic<float>* op0FeedbackParam { nullptr };
    float lastFeedbackAdd { 0.0f };   // TargetKind::Feedback hysteresis: lets
                                      // the base value be restored on the
                                      // first sample after modulation stops
    void cacheParameterPointers();

    // Compile-time max voice pool. "poly" mode runs up to the maxVoices param
    // (clamped to this); mono uses 1, duo uses 2. Must match the maxVoices
    // parameter's upper bound in ParameterTree.
    static constexpr int kMaxVoices = 16;
    std::array<BrocotVoice, kMaxVoices> voices {};
    std::array<juce::int64, kMaxVoices> voiceNoteOnOrder {};
    juce::int64 noteOnCounter { 0 };
    std::atomic<double> lastNoteHz { 220.0 };   // see getLastNoteHz()
    FilterBank  filter;
    LFO         lfo;

    /** Number of voices to render/allocate this block: mono=1, duo=2,
        poly=maxVoices (clamped to [1, kMaxVoices]). */
    int  activeVoices() const noexcept;
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

    // Per-op LUT mapping position 0..32 → ratio. Built on the message
    // thread whenever sharedPath, an op path, or path mode changes; read
    // atomically from the audio thread by the OpPosition routing handler.
    // Trunk source: sharedPath in shared mode, op's own path in free mode.
    static constexpr int kMaxPosition = 33;
    std::array<std::array<std::atomic<float>, kMaxPosition>, params::kNumOperators> opPositionRatios {};
    void rebuildOpPositionLUTs();

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

    // Runtime pull-to-nearest index. Loaded once at construction from
    // resources/pull_index.json; stays empty (and the steer-tab UI
    // hides itself) if the file isn't on disk.
    PullIndex pullIndex;

    // Per-depth landscape maps, sorted by depth. Headers (name/depth) are
    // peeked cheaply at startup so the selector is fully populated up front,
    // but each map's heavy node array is parsed lazily the first time the map
    // is activated — loading all 13 eagerly was ~9 s on the message thread.
    // All map state is touched on the message thread ONLY; the audio thread
    // never reads maps, so no locking is needed.
    struct MapSlot
    {
        juce::File                 file;
        int                        nOps { 6 };
        juce::String               name;
        std::unique_ptr<Landscape> map;     // null until first activated
    };
    std::vector<MapSlot> maps;
    int activeMap { 0 };
    void ensureMapLoaded (int i);   // parse slot i's nodes if not already loaded

    // Continuous per-op ratio override for groove steering (<=0 = inactive).
    // Written from the message thread (SteerTab timer), read on the audio
    // thread per block — atomics, like opRatios.
    std::array<std::atomic<float>, params::kNumOperators> steeredRatios {};

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

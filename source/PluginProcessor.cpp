#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "audio/Path.h"

namespace brocot
{

BrocotAudioProcessor::BrocotAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        // Declared-not-implemented per BROCOT-SPEC.md §2 — v1.x will route
        // this through a SidechainEnvelopeSource. Disabled at construction
        // so hosts don't surface an input on a synth plugin in v1.
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false))
    , apvts (*this, nullptr, "Brocot", params::createLayout())
{
    // Seed the path data with the Avatar·1 configuration (BROCOT-SPEC.md §10
    // preset #9). Slice 13 swaps this out for preset-driven init.
    setOpPath (0, Path (sharedPath).prefix (1).toString());  // R    → 2/1
    setOpPath (1, Path (sharedPath).prefix (3).toString());  // RLL  → 4/3
    setOpPath (2, Path (sharedPath).prefix (5).toString());  // RLLLR → 9/7
    for (int i = 3; i < params::kNumOperators; ++i)
        setOpPath (i, "");

    // Match the APVTS defaults for the Avatar·1 op depths so the host sees
    // the same starting state as the engine. The kNumOperators × 2 defaults
    // are defined in ParameterTree.cpp; only the first three get non-zero
    // depth for this preset.
    // Defensive: explicitly set defaults that should already come from the
    // ParameterTree constructor, in case the host / standalone wrapper
    // restores empty state and tramples on them. (Observed on Windows
    // where the BoolParam defaults didn't survive.)
    auto setBool = [this] (juce::StringRef id, bool value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (value ? 1.0f : 0.0f);
    };
    setBool (params::opId (0, "enabled"), true);
    setBool (params::opId (1, "enabled"), true);
    setBool (params::opId (2, "enabled"), true);
    for (int i = 3; i < params::kNumOperators; ++i)
        setBool (params::opId (i, "enabled"), false);

    if (auto* p = apvts.getParameter (params::opId (0, "depth"))) p->setValueNotifyingHost (p->convertTo0to1 (5.13f));
    if (auto* p = apvts.getParameter (params::opId (1, "depth"))) p->setValueNotifyingHost (p->convertTo0to1 (0.86f));
    if (auto* p = apvts.getParameter (params::opId (2, "depth"))) p->setValueNotifyingHost (p->convertTo0to1 (0.90f));

    // Shared-mode positions along RLLLR (1/3/5) so toggling to shared mode
    // out of the box reproduces the same ratios.
    auto setPos = [this] (int op, int pos)
    {
        if (auto* p = apvts.getParameter (params::opId (op, "position")))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) pos));
    };
    setPos (0, 1);
    setPos (1, 3);
    setPos (2, 5);

    // Register parameter listeners so that path-mode / position changes
    // trigger an off-audio-thread ratio recompute via AsyncUpdater. This
    // keeps the audio thread free of the lock-+-string-allocation cost
    // that refreshSharedRatios used to incur on every block.
    apvts.addParameterListener (params::ids::pathMode, this);
    for (int i = 0; i < params::kNumOperators; ++i)
        apvts.addParameterListener (params::opId (i, "position"), this);
}

BrocotAudioProcessor::~BrocotAudioProcessor()
{
    apvts.removeParameterListener (params::ids::pathMode, this);
    for (int i = 0; i < params::kNumOperators; ++i)
        apvts.removeParameterListener (params::opId (i, "position"), this);
}

void BrocotAudioProcessor::parameterChanged (const juce::String& /*parameterID*/,
                                             float /*newValue*/)
{
    // Called from whatever thread changed the parameter (host automation
    // may hit us from the audio thread). Defer to the message thread.
    triggerAsyncUpdate();
}

void BrocotAudioProcessor::handleAsyncUpdate()
{
    if (isSharedMode())
        refreshSharedRatios();
    rebuildOpPositionLUTs();
}

void BrocotAudioProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare (sampleRate);
    filter.prepare (sampleRate);
    lfo.prepare (sampleRate);
    for (auto& m : modulators)
        m.prepare (sampleRate);
    applyParametersToAllVoices();
    applyModulatorParameters();

    // Snap each operator's smoothed ratio to its current target so the
    // first audio block doesn't ramp from the default 1/1 to the seeded
    // value (5ms chirp at startup is audible during the first note).
    for (auto& v : voices)
        for (int i = 0; i < params::kNumOperators; ++i)
            v.op (i).snapRatio (opRatios[(size_t) i].load (std::memory_order_acquire));
}

void BrocotAudioProcessor::releaseResources() {}

bool BrocotAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 0)
    {
        const auto& sc = layouts.getChannelSet (true, 0);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void BrocotAudioProcessor::applyParametersToAllVoices()
{
    float masterLevel = 0.5f;
    if (auto* p = apvts.getRawParameterValue (params::ids::masterLevel))
        masterLevel = p->load();

    // Halve the per-voice level in duo mode so two simultaneous notes don't
    // clip; a more sophisticated voice mixer can come later.
    const float perVoiceLevel = isDuoMode() ? masterLevel * 0.5f : masterLevel;

    AmpEnvelope::Parameters env;
    if (auto* p = apvts.getRawParameterValue (params::ids::envAttack))  env.attack  = p->load();
    if (auto* p = apvts.getRawParameterValue (params::ids::envDecay))   env.decay   = p->load();
    if (auto* p = apvts.getRawParameterValue (params::ids::envSustain)) env.sustain = p->load();
    if (auto* p = apvts.getRawParameterValue (params::ids::envRelease)) env.release = p->load();

    for (auto& v : voices)
    {
        v.setLevel (perVoiceLevel);
        v.envelope().setParameters (env);

        for (int i = 0; i < params::kNumOperators; ++i)
        {
            auto& op = v.op (i);

            bool enabled = false;
            if (auto* p = apvts.getRawParameterValue (params::opId (i, "enabled")))
                enabled = (p->load() > 0.5f);
            op.setEnabled (enabled);

            if (enabled)
            {
                op.setRatio (opRatios[(size_t) i].load (std::memory_order_acquire));

                float depth = 0.0f;
                if (auto* p = apvts.getRawParameterValue (params::opId (i, "depth")))
                    depth = p->load();
                op.setDepth (depth);
            }
        }
    }

    // refreshSharedRatios is now invoked off the audio thread via
    // AsyncUpdater whenever a position or pathMode parameter changes, so
    // we don't pay for it on every block (and don't take the pathsLock here).

    // Filter mode + resonance refresh per block. Cutoff is modulated per-sample
    // in processBlock by the LFO so it isn't set here.
    if (auto* p = apvts.getRawParameterValue (params::ids::filterType))
    {
        const int idx = (int) std::round (p->load());
        filter.setMode (static_cast<FilterBank::Mode> (juce::jlimit (0, 3, idx)));
    }
    if (auto* p = apvts.getRawParameterValue (params::ids::filterResonance))
        filter.setResonance (p->load());

    // LFO config refresh per block.
    if (auto* p = apvts.getRawParameterValue (params::ids::lfoShape))
    {
        const int idx = (int) std::round (p->load());
        lfo.setShape (static_cast<LFO::Shape> (juce::jlimit (0, 3, idx)));
    }
    if (auto* p = apvts.getRawParameterValue (params::ids::lfoRate))  lfo.setRate  (p->load());
    if (auto* p = apvts.getRawParameterValue (params::ids::lfoDepth)) lfo.setDepth (p->load());
    if (auto* p = apvts.getRawParameterValue (params::ids::lfoKeySync))
        lfo.setKeySync (p->load() > 0.5f);
}

float BrocotAudioProcessor::midiNoteWithBend (int noteNumber) const noexcept
{
    // BROCOT-SPEC.md §8: pitch bend mapped to ±2 semitones in v1.
    int octave = 0;
    if (auto* p = apvts.getRawParameterValue (params::ids::octaveShift))
        octave = (int) std::round (p->load());

    const float bend = pitchBendSource.getModulationValue();
    const float shiftedNote = (float) noteNumber + 12.0f * (float) octave + 2.0f * bend;
    return 440.0f * std::pow (2.0f, (shiftedNote - 69.0f) / 12.0f);
}

void BrocotAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numOutChannels = getTotalNumOutputChannels();

    buffer.clear();

    if (numOutChannels == 0)
    {
        midi.clear();
        return;
    }

    // Merge any notes played on the editor's on-screen keyboard into the
    // incoming MIDI buffer. This is a no-op when no UI keyboard is active.
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    // Pull current parameter snapshot once per block.
    applyParametersToAllVoices();
    applyModulatorParameters();

    // Resolve modulator rates for this block (lock graph walk).
    std::array<float, params::kNumModulators> modRatesThisBlock {};
    for (int i = 0; i < params::kNumModulators; ++i)
    {
        std::array<bool, params::kNumModulators> visited {};
        modRatesThisBlock[(size_t) i] = resolveModRate (i, visited);
    }

    // Cache routing target choices and indices for this block — reading
    // these per-sample-per-mod would be ~8k atomic reads per block.
    std::array<int, params::kNumModulators> modTargetKinds {};
    std::array<int, params::kNumModulators> modTargetIndices {};
    for (int i = 0; i < params::kNumModulators; ++i)
    {
        if (auto* p = apvts.getRawParameterValue (params::modId (i, "targetKind")))
            modTargetKinds[(size_t) i] = juce::jlimit (0,
                (int) params::TargetKind::Count - 1, (int) std::round (p->load()));
        if (auto* p = apvts.getRawParameterValue (params::modId (i, "targetIndex")))
            modTargetIndices[(size_t) i] = juce::jlimit (0, 15,
                (int) std::round (p->load()));
    }

    float baseCutoff01 = 0.85f;
    if (auto* p = apvts.getRawParameterValue (params::ids::filterCutoff))
        baseCutoff01 = p->load();

    float* const monoOut = buffer.getWritePointer (0);

    auto refreshAllCarriers = [this]
    {
        for (auto& v : voices)
            if (v.getActiveNote() != BrocotVoice::kNoActiveNote)
                v.setCarrierHz (midiNoteWithBend (v.getActiveNote()));
    };

    // Per-sample loop: apply MIDI events at their exact sample positions,
    // tick the modulator bank, route to targets, render voices, apply
    // filter, write scope.
    const int activeVoiceCount = isDuoMode() ? kMaxVoices : 1;
    auto midiIter = midi.cbegin();
    const auto midiEnd = midi.cend();

    for (int n = 0; n < numSamples; ++n)
    {
        // 1. Apply any MIDI events scheduled for this sample.
        while (midiIter != midiEnd
               && juce::jlimit (0, numSamples, (*midiIter).samplePosition) <= n)
        {
            const auto msg = (*midiIter).getMessage();

            if (msg.isNoteOn())
            {
                const float vel = msg.getFloatVelocity();
                velocitySource.setVelocity (vel);
                const int idx = allocateVoice (msg.getNoteNumber());
                voices[(size_t) idx].noteOn (msg.getNoteNumber(), vel);
                voices[(size_t) idx].setCarrierHz (midiNoteWithBend (msg.getNoteNumber()));
                voiceNoteOnOrder[(size_t) idx] = ++noteOnCounter;
                for (auto& m : modulators)
                    m.onNoteOn();   // honours each mod's keySync flag
            }
            else if (msg.isNoteOff())
            {
                noteOffOnAnyVoice (msg.getNoteNumber());
            }
            else if (msg.isPitchWheel())
            {
                pitchBendSource.setBendRaw14 (msg.getPitchWheelValue());
                refreshAllCarriers();
            }
            else if (msg.isController())
            {
                if (msg.getControllerNumber() == 1)
                    modWheelSource.setRaw14 (msg.getControllerValue());
            }
            else if (msg.isChannelPressure())
            {
                pressureSource.setRaw7 (msg.getChannelPressureValue());
            }

            ++midiIter;
        }

        // 2. Tick the modulator bank at the rates resolved for this block.
        std::array<float, params::kNumModulators> modOut {};
        for (int i = 0; i < params::kNumModulators; ++i)
            modOut[(size_t) i] = modulators[(size_t) i].tickBipolar (modRatesThisBlock[(size_t) i]);

        // 3. Dispatch each mod's output to its routing target.
        BrocotVoice::PerSampleMod psm;
        float cutoffMod01 = 0.0f;   // additive offset on 0..1 cutoff space

        for (int i = 0; i < params::kNumModulators; ++i)
        {
            const float v = modOut[(size_t) i];
            const auto kind = (params::TargetKind) modTargetKinds[(size_t) i];
            const int idx = modTargetIndices[(size_t) i];

            switch (kind)
            {
                case params::TargetKind::FilterCutoff:
                    cutoffMod01 += v * 0.5f;        // ±0.5 of normalised range
                    break;
                case params::TargetKind::CarrierAm:
                    psm.carrierAmGain *= (1.0f + v); // ±1.0 range; clamp below
                    break;
                case params::TargetKind::CarrierFm:
                    if (! voices.empty())
                        psm.carrierFmHz += v * voices[0].getCarrierHz();
                    break;
                case params::TargetKind::OpDepth:
                {
                    const int safe = juce::jlimit (0, BrocotVoice::N_OPS - 1, idx);
                    psm.opDepthAdd[(size_t) safe] += v;
                    break;
                }
                case params::TargetKind::OpPosition:
                {
                    const int safe = juce::jlimit (0, params::kNumOperators - 1, idx);
                    int basePos = 0;
                    if (auto* p = apvts.getRawParameterValue (params::opId (safe, "position")))
                        basePos = (int) std::round (p->load());
                    // Full LFO swing = ±8 SB steps. Smoothing inside the
                    // op's ratio (V2.6 SmoothedValue) hides the per-step jumps.
                    const int offset = (int) std::round (v * 8.0f);
                    const int effPos = juce::jlimit (0, kMaxPosition - 1, basePos + offset);
                    const float newRatio = opPositionRatios[(size_t) safe][(size_t) effPos]
                                              .load (std::memory_order_acquire);
                    for (auto& vox : voices)
                        vox.op (safe).setRatio (newRatio);
                    break;
                }
                case params::TargetKind::ModRate:
                case params::TargetKind::ModDepth:
                    // Cascade — rate cascade already in the resolver; depth
                    // cascade lands when the per-mod APVTS depth gets
                    // continuously-driven by another mod (V2.7 hooks the UI).
                    break;
                case params::TargetKind::Count:
                    break;
            }
        }

        // Clamp carrier AM so heavy modulation doesn't go negative.
        psm.carrierAmGain = juce::jlimit (0.0f, 4.0f, psm.carrierAmGain);

        // 4. Render the voices into a mono sum.
        float mono = 0.0f;
        for (int v = 0; v < activeVoiceCount; ++v)
            mono += voices[(size_t) v].renderSample (psm);

        // 5. Apply filter with modulated cutoff.
        const float cutoff01 = juce::jlimit (0.0f, 1.0f, baseCutoff01 + cutoffMod01);
        const float cutoffHz = 20.0f * std::pow (1000.0f, cutoff01);
        filter.setCutoff (cutoffHz);
        monoOut[n] = filter.processSample (mono);
    }

    // Feed the scope ring before mirroring to stereo so the scope sees the
    // mono mix exactly as filtered.
    writeScopeSamples (monoOut, numSamples);

    for (int ch = 1; ch < numOutChannels; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    midi.clear();
}

void BrocotAudioProcessor::writeScopeSamples (const float* samples, int n) noexcept
{
    int w = scopeWritePos.load (std::memory_order_relaxed);
    constexpr int mask = kScopeBufferSize - 1;
    for (int i = 0; i < n; ++i)
    {
        scopeRing[(size_t) (w & mask)] = samples[i];
        ++w;
    }
    scopeWritePos.store (w & mask, std::memory_order_release);
    scopeHasData.store (true, std::memory_order_release);
}

bool BrocotAudioProcessor::snapshotRecentAudio (std::vector<float>& dest) const noexcept
{
    if (! scopeHasData.load (std::memory_order_acquire))
        return false;

    const int n = (int) dest.size();
    if (n <= 0 || n > kScopeBufferSize) return false;

    constexpr int mask = kScopeBufferSize - 1;
    const int w = scopeWritePos.load (std::memory_order_acquire);
    // Start `n` samples back from the current write head.
    for (int i = 0; i < n; ++i)
        dest[(size_t) i] = scopeRing[(size_t) ((w - n + i) & mask)];
    return true;
}

juce::AudioProcessorEditor* BrocotAudioProcessor::createEditor()
{
    return new BrocotAudioProcessorEditor (*this);
}

void BrocotAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // APVTS state covers all host-automatable parameters. Path strings live
    // in a sibling ValueTree branch so the host's parameter list stays clean
    // but state persistence still round-trips them.
    auto state = apvts.copyState();

    juce::ValueTree paths ("Paths");
    paths.setProperty ("shared", sharedPath, nullptr);
    for (int i = 0; i < params::kNumOperators; ++i)
        paths.setProperty ("op" + juce::String (i),
                           opPaths[(size_t) i], nullptr);
    state.appendChild (paths, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BrocotAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid()) return;

    if (auto paths = state.getChildWithName ("Paths"); paths.isValid())
    {
        sharedPath = paths.getProperty ("shared", sharedPath).toString();
        for (int i = 0; i < params::kNumOperators; ++i)
            opPaths[(size_t) i] = paths.getProperty ("op" + juce::String (i),
                                                     opPaths[(size_t) i]).toString();
        state.removeChild (paths, nullptr);
    }

    apvts.replaceState (state);
    applyParametersToAllVoices();
}

juce::String BrocotAudioProcessor::getOpPath (int idx) const
{
    if (idx < 0 || idx >= params::kNumOperators) return {};
    const juce::ScopedLock lock (pathsLock);
    return opPaths[(size_t) idx];
}

void BrocotAudioProcessor::setOpPath (int idx, const juce::String& pathString)
{
    if (idx < 0 || idx >= params::kNumOperators) return;
    const Path p (pathString);
    {
        const juce::ScopedLock lock (pathsLock);
        opPaths[(size_t) idx] = pathString;
    }
    opRatios[(size_t) idx].store ((float) p.ratio(), std::memory_order_release);
    rebuildOpPositionLUTs();
}

bool BrocotAudioProcessor::isDuoMode() const noexcept
{
    if (auto* p = apvts.getRawParameterValue (params::ids::polyphony))
        return p->load() > 0.5f;   // 0 = mono, 1 = duo
    return false;
}

int BrocotAudioProcessor::allocateVoice (int /*midiNote*/) noexcept
{
    // BROCOT-SPEC.md §8 mono: voice 0 always gets retriggered.
    if (! isDuoMode()) return 0;

    // Duo: prefer a voice that's done playing (envelope inactive).
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices[(size_t) i].isActive()) return i;

    // Otherwise steal the oldest voice.
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (voiceNoteOnOrder[(size_t) i] < voiceNoteOnOrder[(size_t) oldest])
            oldest = i;
    return oldest;
}

void BrocotAudioProcessor::noteOffOnAnyVoice (int midiNote) noexcept
{
    const int count = isDuoMode() ? kMaxVoices : 1;
    for (int i = 0; i < count; ++i)
        voices[(size_t) i].noteOff (midiNote);
}

juce::String BrocotAudioProcessor::getSharedPath() const
{
    const juce::ScopedLock lock (pathsLock);
    return sharedPath;
}

void BrocotAudioProcessor::setSharedPath (const juce::String& pathString)
{
    {
        const juce::ScopedLock lock (pathsLock);
        sharedPath = pathString;
    }
    refreshSharedRatios();
}

bool BrocotAudioProcessor::isSharedMode() const
{
    if (auto* p = apvts.getRawParameterValue (params::ids::pathMode))
        return p->load() < 0.5f;   // 0 = shared, 1 = free
    return true;
}

juce::String BrocotAudioProcessor::getModPath (int idx) const
{
    if (idx < 0 || idx >= params::kNumModulators) return {};
    const juce::ScopedLock lock (pathsLock);
    return modPaths[(size_t) idx];
}

void BrocotAudioProcessor::setModPath (int idx, const juce::String& pathString)
{
    if (idx < 0 || idx >= params::kNumModulators) return;
    const Path p (pathString);
    {
        const juce::ScopedLock lock (pathsLock);
        modPaths[(size_t) idx] = pathString;
    }
    // Empty path → 1/1 ratio, useful as the "no SB multiplier" identity.
    const float ratio = (float) p.ratio();
    modRatios[(size_t) idx].store (ratio, std::memory_order_release);
}

float BrocotAudioProcessor::getResolvedModRate (int idx) const
{
    std::array<bool, params::kNumModulators> visited {};
    return resolveModRate (idx, visited);
}

void BrocotAudioProcessor::applyModulatorParameters()
{
    for (int i = 0; i < params::kNumModulators; ++i)
    {
        auto& m = modulators[(size_t) i];

        if (auto* p = apvts.getRawParameterValue (params::modId (i, "shape")))
            m.setShape ((envelope::Shape) juce::jlimit (0,
                            (int) envelope::Shape::Count - 1,
                            (int) std::round (p->load())));

        if (auto* p = apvts.getRawParameterValue (params::modId (i, "kappa")))
            m.setKappa (p->load());

        if (auto* p = apvts.getRawParameterValue (params::modId (i, "depth")))
            m.setDepth (p->load());

        if (auto* p = apvts.getRawParameterValue (params::modId (i, "keySync")))
            m.setKeySync (p->load() > 0.5f);

        if (auto* p = apvts.getRawParameterValue (params::modId (i, "psk")))
        {
            // Choice index → state count. 0=off, 1=2, 2=4, 3=8, 4=16.
            const int idx = (int) std::round (p->load());
            const int counts[] = { 0, 2, 4, 8, 16 };
            m.setPSKStates (counts[juce::jlimit (0, 4, idx)]);
        }
        if (auto* p = apvts.getRawParameterValue (params::modId (i, "pskSmooth")))
            m.setPSKSmooth (p->load());
    }
}

float BrocotAudioProcessor::resolveModRate (int idx,
                                            std::array<bool, params::kNumModulators>& visited) const noexcept
{
    if (idx < 0 || idx >= params::kNumModulators) return 0.0f;
    if (visited[(size_t) idx])
    {
        // Cycle break — fall back to the mod's free rate.
        if (auto* p = apvts.getRawParameterValue (params::modId (idx, "rate")))
            return p->load();
        return 0.0f;
    }
    visited[(size_t) idx] = true;

    int kind = 0;
    if (auto* p = apvts.getRawParameterValue (params::modId (idx, "lockKind")))
        kind = juce::jlimit (0, (int) params::LockTargetKind::Count - 1,
                             (int) std::round (p->load()));

    if (kind == (int) params::LockTargetKind::Free)
    {
        if (auto* p = apvts.getRawParameterValue (params::modId (idx, "rate")))
            return p->load();
        return 0.0f;
    }

    const float ratio = modRatios[(size_t) idx].load (std::memory_order_acquire);
    const float carrierHz = voices[0].getCarrierHz();

    if (kind == (int) params::LockTargetKind::Carrier)
        return carrierHz * ratio;

    int lockIdx = 0;
    if (auto* p = apvts.getRawParameterValue (params::modId (idx, "lockIndex")))
        lockIdx = juce::jlimit (0, 15, (int) std::round (p->load()));

    if (kind == (int) params::LockTargetKind::Operator)
    {
        const int safeOpIdx = juce::jlimit (0, params::kNumOperators - 1, lockIdx);
        const float opRatio = opRatios[(size_t) safeOpIdx].load (std::memory_order_acquire);
        return carrierHz * opRatio * ratio;
    }

    if (kind == (int) params::LockTargetKind::Modulator)
        return resolveModRate (lockIdx, visited) * ratio;

    return 0.0f;
}

void BrocotAudioProcessor::refreshSharedRatios()
{
    juce::String snapshot;
    {
        const juce::ScopedLock lock (pathsLock);
        snapshot = sharedPath;
    }
    const Path shared (snapshot);
    const int len = shared.depth();

    for (int i = 0; i < params::kNumOperators; ++i)
    {
        int pos = 0;
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "position")))
            pos = (int) std::round (p->load());
        pos = juce::jlimit (0, len, pos);

        const Path derived = shared.prefix (pos);
        const juce::String derivedStr = derived.toString();
        {
            const juce::ScopedLock lock (pathsLock);
            opPaths[(size_t) i] = derivedStr;
        }
        opRatios[(size_t) i].store ((float) derived.ratio(),
                                    std::memory_order_release);
    }

    rebuildOpPositionLUTs();
}

void BrocotAudioProcessor::rebuildOpPositionLUTs()
{
    const bool sharedModeNow = isSharedMode();
    juce::String sharedSnap;
    {
        const juce::ScopedLock lock (pathsLock);
        sharedSnap = sharedPath;
    }

    for (int i = 0; i < params::kNumOperators; ++i)
    {
        // Trunk = sharedPath (shared mode) or this op's own path (free mode).
        juce::String trunkStr;
        if (sharedModeNow)
        {
            trunkStr = sharedSnap;
        }
        else
        {
            const juce::ScopedLock lock (pathsLock);
            trunkStr = opPaths[(size_t) i];
        }

        const Path trunk (trunkStr);
        const int len = trunk.depth();
        for (int pos = 0; pos < kMaxPosition; ++pos)
        {
            const int clamped = juce::jlimit (0, len, pos);
            const Path derived = trunk.prefix (clamped);
            opPositionRatios[(size_t) i][(size_t) pos].store (
                (float) derived.ratio(), std::memory_order_release);
        }
    }
}

} // namespace brocot

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new brocot::BrocotAudioProcessor();
}

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
}

BrocotAudioProcessor::~BrocotAudioProcessor() = default;

void BrocotAudioProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare (sampleRate);
    filter.prepare (sampleRate);
    lfo.prepare (sampleRate);
    applyParametersToAllVoices();
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

    if (isSharedMode())
        refreshSharedRatios();

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

    // Pull current parameter snapshot once per block. Block-level smoothing
    // is good enough for v1; per-sample smoothing arrives if/when a slider
    // is observed to click.
    applyParametersToAllVoices();

    float* const monoOut = buffer.getWritePointer (0);
    int processedSamples = 0;

    auto refreshAllCarriers = [this]
    {
        for (auto& v : voices)
            if (v.getActiveNote() != BrocotVoice::kNoActiveNote)
                v.setCarrierHz (midiNoteWithBend (v.getActiveNote()));
    };

    auto renderVoices = [this, monoOut] (int from, int count)
    {
        if (count <= 0) return;
        const int activeCount = isDuoMode() ? kMaxVoices : 1;
        for (int i = 0; i < activeCount; ++i)
            voices[(size_t) i].render (monoOut + from, count);
    };

    for (const auto meta : midi)
    {
        const int eventSample = juce::jlimit (0, numSamples, meta.samplePosition);
        renderVoices (processedSamples, eventSample - processedSamples);
        processedSamples = eventSample;

        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
        {
            const float vel = msg.getFloatVelocity();
            velocitySource.setVelocity (vel);

            const int idx = allocateVoice (msg.getNoteNumber());
            voices[(size_t) idx].noteOn (msg.getNoteNumber(), vel);
            voices[(size_t) idx].setCarrierHz (midiNoteWithBend (msg.getNoteNumber()));
            voiceNoteOnOrder[(size_t) idx] = ++noteOnCounter;
            lfo.onNoteOn();   // optional phase reset if key-sync is enabled
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
    }

    renderVoices (processedSamples, numSamples - processedSamples);

    // Apply the LFO → filter cutoff route per sample. Base cutoff is a 0..1
    // APVTS value mapped logarithmically to 20 Hz..20 kHz; the LFO modulates
    // ±2 octaves at full depth. Sample-level smoothing of cutoff comes from
    // recomputing each sample — coefficients are cheap.
    float baseCutoff01 = 0.85f;
    if (auto* p = apvts.getRawParameterValue (params::ids::filterCutoff))
        baseCutoff01 = p->load();

    for (int n = 0; n < numSamples; ++n)
    {
        const float lfoValue = lfo.tickScaled();   // [-depth, +depth]
        const float cutoff01 = juce::jlimit (0.0f, 1.0f,
                                             baseCutoff01 + lfoValue * 0.5f);
        const float cutoffHz = 20.0f * std::pow (1000.0f, cutoff01);  // 20..20k
        filter.setCutoff (cutoffHz);
        monoOut[n] = filter.processSample (monoOut[n]);
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
}

} // namespace brocot

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new brocot::BrocotAudioProcessor();
}

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
{
    // Hard-coded "Avatar·1" preset for Slice 4 — the first preset Will
    // discovered manually in the web prototype. BROCOT-SPEC.md §4 lists this
    // as the multi-modulator audio-engine verification case.
    //
    //   shared path     RLLLR
    //   carrier         C3 (130.81 Hz)
    //   op 0   pos 1 → R       (2/1)   depth 5.13
    //   op 1   pos 3 → RLL     (4/3)   depth 0.86
    //   op 2   pos 5 → RLLLR   (9/7)   depth 0.90
    //   ops 3..7 disabled
    voice.setCarrierHz (130.81f);
    voice.setLevel (0.18f);

    const Path sharedPath ("RLLLR");
    struct OpSetup { int position; float depth; };
    const OpSetup setups[] = {
        { 1, 5.13f },
        { 3, 0.86f },
        { 5, 0.90f },
    };

    for (size_t i = 0; i < std::size (setups); ++i)
    {
        auto& op = voice.op ((int) i);
        op.setEnabled (true);
        op.setPath (sharedPath.prefix (setups[i].position));
        op.setDepth (setups[i].depth);
    }
}

BrocotAudioProcessor::~BrocotAudioProcessor() = default;

void BrocotAudioProcessor::prepareToPlay (double sampleRate, int)
{
    voice.prepare (sampleRate);
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

    // Render mono FM voice into channel 0, then mirror to remaining outputs.
    voice.render (buffer.getWritePointer (0), numSamples);
    for (int ch = 1; ch < numOutChannels; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    midi.clear();
}

juce::AudioProcessorEditor* BrocotAudioProcessor::createEditor()
{
    return new BrocotAudioProcessorEditor (*this);
}

void BrocotAudioProcessor::getStateInformation (juce::MemoryBlock&) {}
void BrocotAudioProcessor::setStateInformation (const void*, int) {}

} // namespace brocot

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new brocot::BrocotAudioProcessor();
}

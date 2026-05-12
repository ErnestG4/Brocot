#include "PluginProcessor.h"
#include "PluginEditor.h"

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
}

BrocotAudioProcessor::~BrocotAudioProcessor() = default;

void BrocotAudioProcessor::prepareToPlay (double, int) {}
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

    const auto totalInputChannels  = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();

    for (int ch = totalInputChannels; ch < totalOutputChannels; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    buffer.clear();
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

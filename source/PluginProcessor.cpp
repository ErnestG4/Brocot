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
    // Hard-coded "octave·J₁" preset for Slice 3 — carrier at 220 Hz, one
    // modulator at path R (= 2/1), modulation index I = 1.841 (the J₁ peak).
    // BROCOT-SPEC.md §4 lists this as the first audio-engine verification.
    voice.setCarrierHz (220.0f);
    voice.setLevel (0.2f);

    auto& m0 = voice.op (0);
    m0.setEnabled (true);
    m0.setPath (Path ("R"));
    m0.setDepth (1.841f);
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

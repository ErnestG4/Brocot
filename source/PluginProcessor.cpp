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
    // Hard-coded "Avatar·1" operator setup per BROCOT-SPEC.md §10 (preset #9).
    // Carrier frequency now follows MIDI (set in noteOn); only the modulator
    // configuration is fixed at construction. Slice 13 replaces this with
    // the preset system.
    //
    //   shared path     RLLLR
    //   op 0   pos 1 → R       (2/1)   depth 5.13
    //   op 1   pos 3 → RLL     (4/3)   depth 0.86
    //   op 2   pos 5 → RLLLR   (9/7)   depth 0.90
    //   ops 3..7 disabled
    voice.setLevel (0.25f);

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

    // Sample-accurate MIDI: render the FM voice in chunks between MIDI events,
    // applying each event at its exact sample offset. Mono voice per spec §8;
    // duophony lands in Slice 15 once polyphony management is layered on top.
    float* const monoOut = buffer.getWritePointer (0);
    int processedSamples = 0;

    for (const auto meta : midi)
    {
        const int eventSample = juce::jlimit (0, numSamples, meta.samplePosition);
        const int samplesToRender = eventSample - processedSamples;
        if (samplesToRender > 0)
            voice.render (monoOut + processedSamples, samplesToRender);
        processedSamples = eventSample;

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            voice.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            voice.noteOff (msg.getNoteNumber());
        // Other MIDI (CCs, pitch bend, aftertouch) is plumbed in Slice 6.
    }

    if (processedSamples < numSamples)
        voice.render (monoOut + processedSamples, numSamples - processedSamples);

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

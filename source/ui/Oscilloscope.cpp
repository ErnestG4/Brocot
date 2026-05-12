#include "Oscilloscope.h"
#include "../PluginProcessor.h"
#include "../util/Colors.h"

namespace brocot
{

Oscilloscope::Oscilloscope (BrocotAudioProcessor& processor)
    : proc (processor)
{
    sampleBuffer.resize (1024);
    setOpaque (true);
    startTimerHz (30);
}

Oscilloscope::~Oscilloscope() = default;

void Oscilloscope::timerCallback()
{
    hasSamples = proc.snapshotRecentAudio (sampleBuffer);
    repaint();
}

void Oscilloscope::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (colors::background);

    const float W = bounds.getWidth();
    const float H = bounds.getHeight();
    const float midY = H * 0.5f;

    // Faint vertical/horizontal grid.
    g.setColour (colors::border.withAlpha (0.10f));
    for (int i = 1; i < 8; ++i)
    {
        const float x = (float) i / 8.0f * W;
        g.drawVerticalLine ((int) x, 0.0f, H);
    }
    g.drawHorizontalLine ((int) midY, 0.0f, W);

    if (! hasSamples || sampleBuffer.empty())
    {
        g.setColour (colors::secondaryAtAlpha (0.3f));
        g.drawText ("(silent)", bounds, juce::Justification::centred);
        return;
    }

    const auto n = (int) sampleBuffer.size();
    juce::Path waveform;
    for (int i = 0; i < n; ++i)
    {
        // sample clamped to ±1 then scaled to half-height.
        float s = sampleBuffer[(size_t) i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        const float x = (float) i / (float) (n - 1) * W;
        const float y = midY - s * midY * 0.9f;
        if (i == 0) waveform.startNewSubPath (x, y);
        else        waveform.lineTo (x, y);
    }

    // Glow underlay, then crisp stroke on top.
    g.setColour (colors::accent.withAlpha (0.25f));
    g.strokePath (waveform, juce::PathStrokeType (3.0f));
    g.setColour (colors::accentBright);
    g.strokePath (waveform, juce::PathStrokeType (1.0f));
}

void Oscilloscope::resized() {}

} // namespace brocot

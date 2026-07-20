#include "RatioReadout.h"
#include "../PluginProcessor.h"
#include "../audio/Coherence.h"
#include "../audio/DepthRatio.h"
#include "../audio/Path.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

RatioReadout::RatioReadout (BrocotAudioProcessor& processor)
    : proc (processor)
{
    auto setupHeader = [this] (juce::Label& l, juce::StringRef text)
    {
        addAndMakeVisible (l);
        l.setText (text, juce::dontSendNotification);
        l.setFont (fonts::mono (10.0f));
        l.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.5f));
    };
    setupHeader (freqHeader,  "frequency ratios");
    setupHeader (depthHeader, "depth ratios");
    setupHeader (coherenceHeader, "coherence");

    auto setupValues = [this] (juce::Label& l)
    {
        addAndMakeVisible (l);
        l.setFont (fonts::serif (15.0f));
        l.setColour (juce::Label::textColourId, colors::primaryText);
    };
    setupValues (freqValues);
    setupValues (depthValues);
    setupValues (coherenceValues);

    refreshFromProcessor();
    startTimerHz (10);
}

RatioReadout::~RatioReadout() = default;

void RatioReadout::timerCallback() { refreshFromProcessor(); }

void RatioReadout::refreshFromProcessor()
{
    juce::String freqs;
    std::vector<float> depths;
    std::vector<coherence::OpSpec> coherenceOps;

    auto& apvts = proc.getAPVTS();
    bool anyEnabled = false;

    for (int i = 0; i < params::kNumOperators; ++i)
    {
        bool enabled = false;
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "enabled")))
            enabled = (p->load() > 0.5f);

        float depth = 0.0f;
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "depth")))
            depth = p->load();

        depths.push_back (enabled ? depth : 0.0f);

        if (enabled)
        {
            anyEnabled = true;
            const Path path (proc.getOpPath (i));
            int n, d;
            path.walk (n, d);
            if (! freqs.isEmpty()) freqs += "  .  ";   // visual separator
            freqs += juce::String (n) + "/" + juce::String (d);

            // Coherence uses the base path ratio (the axis the readout shows)
            // and the operator depth as the FM modulation index.
            coherenceOps.push_back ({ path.ratio(), (double) depth });
        }
    }

    if (! anyEnabled) freqs = "(no operators enabled)";
    freqValues.setText (freqs, juce::dontSendNotification);

    juce::String depthDisplay = findDepthRatio (depths);
    if (depthDisplay.isEmpty()) depthDisplay = "-";
    depthValues.setText (depthDisplay, juce::dontSendNotification);

    // Path-prefix coherence (§P0-a). Needs ≥2 audible modulators to mean
    // anything; below that the readout dims to "-".
    const auto result = coherence::score (coherenceOps);
    coherence01 = result.coherence;
    if (result.activeOps < 2)
    {
        coherenceValues.setText ("-", juce::dontSendNotification);
        coherenceValues.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.4f));
    }
    else
    {
        coherenceValues.setText (juce::String (juce::roundToInt (coherence01 * 100.0f)) + "%",
                                 juce::dontSendNotification);
        // Dim → bright amber as coherence rises, so a prefix-coherent config
        // reads at a glance.
        coherenceValues.setColour (juce::Label::textColourId,
                                   colors::disabled.interpolatedWith (colors::accentBright, coherence01));
    }
    repaint();
}

void RatioReadout::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);
    g.setColour (colors::borderAtAlpha (0.3f));
    g.drawRect (getLocalBounds(), 1);

    // Vertical dividers between the three columns.
    g.setColour (colors::borderAtAlpha (0.2f));
    const float w = (float) getWidth();
    g.drawVerticalLine ((int) (w / 3.0f),       6.0f, (float) getHeight() - 6.0f);
    g.drawVerticalLine ((int) (w * 2.0f / 3.0f), 6.0f, (float) getHeight() - 6.0f);

    // Coherence bar: a slim gauge under the percentage so the config can be
    // steered toward coherence by eye.
    if (! coherenceBar.isEmpty())
    {
        g.setColour (colors::borderAtAlpha (0.25f));
        g.fillRect (coherenceBar);
        auto fill = coherenceBar.toFloat().withWidth (coherenceBar.getWidth() * coherence01);
        g.setColour (colors::accent.withAlpha (0.35f + 0.65f * coherence01));
        g.fillRect (fill);
    }
}

void RatioReadout::resized()
{
    auto bounds = getLocalBounds().reduced (8);
    const int colW = bounds.getWidth() / 3;
    auto left   = bounds.removeFromLeft (colW).reduced (4, 0);
    auto mid    = bounds.removeFromLeft (colW).reduced (4, 0);
    auto right  = bounds.reduced (4, 0);

    freqHeader.setBounds  (left.removeFromTop (14));
    freqValues.setBounds  (left);
    depthHeader.setBounds (mid.removeFromTop (14));
    depthValues.setBounds (mid);
    coherenceHeader.setBounds (right.removeFromTop (14));
    coherenceBar = right.removeFromBottom (4);
    coherenceValues.setBounds (right);
}

} // namespace brocot

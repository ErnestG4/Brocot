#include "RatioReadout.h"
#include "../PluginProcessor.h"
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

    auto setupValues = [this] (juce::Label& l)
    {
        addAndMakeVisible (l);
        l.setFont (fonts::serif (15.0f));
        l.setColour (juce::Label::textColourId, colors::primaryText);
    };
    setupValues (freqValues);
    setupValues (depthValues);

    refreshFromProcessor();
    startTimerHz (10);
}

RatioReadout::~RatioReadout() = default;

void RatioReadout::timerCallback() { refreshFromProcessor(); }

void RatioReadout::refreshFromProcessor()
{
    juce::String freqs;
    std::vector<float> depths;

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
        }
    }

    if (! anyEnabled) freqs = "(no operators enabled)";
    freqValues.setText (freqs, juce::dontSendNotification);

    juce::String depthDisplay = findDepthRatio (depths);
    if (depthDisplay.isEmpty()) depthDisplay = "-";
    depthValues.setText (depthDisplay, juce::dontSendNotification);
}

void RatioReadout::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);
    g.setColour (colors::borderAtAlpha (0.3f));
    g.drawRect (getLocalBounds(), 1);

    // Vertical divider between the two halves.
    g.setColour (colors::borderAtAlpha (0.2f));
    const float midX = (float) getWidth() * 0.5f;
    g.drawVerticalLine ((int) midX, 6.0f, (float) getHeight() - 6.0f);
}

void RatioReadout::resized()
{
    auto bounds = getLocalBounds().reduced (8);
    auto left  = bounds.removeFromLeft (bounds.getWidth() / 2).reduced (4, 0);
    auto right = bounds.reduced (4, 0);

    freqHeader.setBounds  (left.removeFromTop (14));
    freqValues.setBounds  (left);
    depthHeader.setBounds (right.removeFromTop (14));
    depthValues.setBounds (right);
}

} // namespace brocot

#include "PathNavigator.h"
#include "../PluginProcessor.h"
#include "../audio/Path.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

PathNavigator::PathNavigator (BrocotAudioProcessor& processor)
    : proc (processor)
{
    addAndMakeVisible (ratioLabel);
    ratioLabel.setFont (fonts::serifItalic (22.0f));
    ratioLabel.setJustificationType (juce::Justification::centredLeft);
    ratioLabel.setColour (juce::Label::textColourId, colors::primaryText);

    addAndMakeVisible (pathLabel);
    pathLabel.setFont (fonts::mono (12.0f));
    pathLabel.setJustificationType (juce::Justification::centredLeft);
    pathLabel.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.7f));

    for (auto* btn : { &btnAddL, &btnAddR, &btnDrop })
        addAndMakeVisible (*btn);

    btnAddL.onClick = [this] { proc.setSharedPath (proc.getSharedPath() + "L"); refreshFromProcessor(); };
    btnAddR.onClick = [this] { proc.setSharedPath (proc.getSharedPath() + "R"); refreshFromProcessor(); };
    btnDrop.onClick = [this]
    {
        auto s = proc.getSharedPath();
        if (! s.isEmpty()) proc.setSharedPath (s.dropLastCharacters (1));
        refreshFromProcessor();
    };

    refreshFromProcessor();
    startTimerHz (15);
}

PathNavigator::~PathNavigator() = default;

void PathNavigator::refreshFromProcessor()
{
    const auto pathStr = proc.getSharedPath();
    if (pathStr == cachedPath) return;
    cachedPath = pathStr;

    const Path p (pathStr);
    int n, d;
    p.walk (n, d);
    ratioLabel.setText (juce::String (n) + " / " + juce::String (d),
                        juce::dontSendNotification);

    pathLabel.setText ("shared:  " +
                       (pathStr.isEmpty() ? juce::String (".") : pathStr),
                       juce::dontSendNotification);

    btnDrop.setEnabled (! pathStr.isEmpty());
}

void PathNavigator::timerCallback() { refreshFromProcessor(); }

void PathNavigator::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);
    g.setColour (colors::borderAtAlpha (0.3f));
    g.drawRect (getLocalBounds(), 1);
}

void PathNavigator::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto row = r.removeFromTop (r.getHeight());

    ratioLabel.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (12);

    pathLabel.setBounds (row.removeFromLeft (juce::jmax (140, row.getWidth() - 180)));

    auto buttons = row.removeFromRight (160);
    const int btnW = buttons.getWidth() / 3;
    btnAddL.setBounds (buttons.removeFromLeft (btnW).reduced (2));
    btnAddR.setBounds (buttons.removeFromLeft (btnW).reduced (2));
    btnDrop.setBounds (buttons.reduced (2));
}

} // namespace brocot

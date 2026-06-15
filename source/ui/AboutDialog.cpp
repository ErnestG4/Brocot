#include "AboutDialog.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

AboutDialog::AboutDialog()
{
    addAndMakeVisible (closeBtn);
    closeBtn.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };
    setSize (520, 380);
}

AboutDialog::~AboutDialog() = default;

void AboutDialog::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    auto bounds = getLocalBounds().reduced (24);

    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (28.0f));
    g.drawText ("brocot", bounds.removeFromTop (38),
                juce::Justification::topLeft);

    g.setColour (colors::secondaryAtAlpha (0.7f));
    g.setFont (fonts::mono (10.0f));
    g.drawText ("v1.0", bounds.removeFromTop (16),
                juce::Justification::topLeft);

    bounds.removeFromTop (12);

    g.setColour (colors::primaryText);
    g.setFont (fonts::serif (12.0f));
    juce::String body =
        "A multi-modulator FM synthesizer whose parameter space is structured\n"
        "by the Stern-Brocot tree and Bessel function landmarks.\n"
        "\n"
        "Lineage:\n"
        "  - Barry Truax (1978)\n"
        "    Organizational Techniques for C:M Ratios in FM\n"
        "  - Dave Burraston / noyzelab (2016)\n"
        "    Farey programming tool for PreenFM2\n"
        "  - Moritz Stern (1858) and Achille Brocot (1861)\n"
        "    the tree itself\n"
        "\n"
        "Designed and built by Will Combust.\n"
        "Free software under AGPL-3.0-or-later.";
    g.drawMultiLineText (body, bounds.getX(), bounds.getY() + 16,
                         bounds.getWidth(), juce::Justification::topLeft);
}

void AboutDialog::resized()
{
    auto r = getLocalBounds().reduced (24);
    closeBtn.setBounds (r.removeFromBottom (28).removeFromRight (80));
}

void AboutDialog::launch (juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new AboutDialog());
    o.dialogTitle = "About Brocot";
    o.dialogBackgroundColour = colors::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = false;
    o.resizable = false;
    o.componentToCentreAround = parent;
    o.launchAsync();
}

} // namespace brocot

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

/**
    About / credits component per BROCOT-SPEC.md §16. Names Truax 1978,
    Burraston/noyzelab 2016, Stern, Brocot, and Will Combust as the lineage.
    Shown via DialogWindow::showDialog from the editor.
*/
class AboutDialog : public juce::Component
{
public:
    AboutDialog();
    ~AboutDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Show as a modal dialog. */
    static void launch (juce::Component* parent);

private:
    juce::TextButton closeBtn { "close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutDialog)
};

} // namespace brocot

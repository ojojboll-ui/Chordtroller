#include "PluginEditor.h"

using namespace juce;

ChordtrollerAudioProcessorEditor::ChordtrollerAudioProcessorEditor (ChordtrollerAudioProcessor& p)
    : AudioProcessorEditor (&p), panel (p), monitor (p)
{
    addAndMakeVisible (panel);
    addChildComponent (monitor);

    toggle.setColour (TextButton::buttonColourId, Colour (0xff1e1815));
    toggle.setColour (TextButton::textColourOffId, Colour (0xffa49c96));
    toggle.onClick = [this] { showDiagnostics (! diagnostics); };
    addAndMakeVisible (toggle);

    // Resizable because the panel is laid out from its actual bounds rather than
    // from assumed pixel heights, and because a window that cannot be made to
    // fit the screen it is on is a window that clips.
    setResizable (true, true);
    setResizeLimits (820, 560, 1800, 1200);
    setSize (1000, 680);
}

void ChordtrollerAudioProcessorEditor::showDiagnostics (bool shouldShow)
{
    diagnostics = shouldShow;

    panel.setVisible (! diagnostics);
    monitor.setVisible (diagnostics);

    toggle.setButtonText (diagnostics ? "INSTRUMENT" : "DIAGNOSTIK");

    // The monitor's rows are fixed-height and want more room than the panel does.
    if (diagnostics && getHeight() < 760)
        setSize (jmax (getWidth(), 1020), 760);
}

void ChordtrollerAudioProcessorEditor::resized()
{
    panel.setBounds (getLocalBounds());
    monitor.setBounds (getLocalBounds());

    toggle.setBounds (getWidth() - 132, 22, 100, 22);
}

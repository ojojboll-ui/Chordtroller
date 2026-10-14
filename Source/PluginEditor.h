#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/InputMonitor.h"
#include "ui/InstrumentPanel.h"

/**
    Two faces on the same instrument.

    The player's panel is what shows. The phase 1 monitor is still here behind a
    small button, because the day a pad misbehaves on someone else's machine the
    raw axis values and the poll rate are the only things that can say why — and
    that is worth a keystroke rather than a rebuild.

    The design handoff's window, with the drawn controller and its six callouts,
    is not being built: the panel below is the face the instrument is keeping.
*/
class ChordtrollerAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChordtrollerAudioProcessorEditor (ChordtrollerAudioProcessor&);
    ~ChordtrollerAudioProcessorEditor() override = default;

    void resized() override;

private:
    void showDiagnostics (bool shouldShow);

    chordtroller::InstrumentPanel panel;
    chordtroller::InputMonitor    monitor;

    juce::TextButton toggle { "DIAGNOSTIK" };
    bool diagnostics = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordtrollerAudioProcessorEditor)
};

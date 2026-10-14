#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

#include "../input/GamepadReader.h"
#include "../input/PadMath.h"
#include "../dsp/VoiceSpec.h"
#include "../music/ChordRing.h"

class ChordtrollerAudioProcessor;

namespace chordtroller
{

/**
    Everything the pad says, everything we make of it, and what the instrument
    did with it.

    Raw sits next to processed on purpose. When a direction feels wrong the first
    question is always whether the stick or the maths is at fault, and a screen
    that only shows the answer cannot tell you. So each stick draws its round
    gate, the square that gate is mapped onto, the deadzone it has to clear, and
    two dots — where the hardware says the thumb is, and where the maths thinks
    it is.

    Nothing stateful is computed here. The chord sector, the key and the string
    index are read from what the audio thread published, because a screen that
    works out its own copy of playing state is a screen that can disagree with
    what you are hearing.
*/
class InputMonitor final : public juce::Component,
                           private juce::Timer
{
public:
    explicit InputMonitor (ChordtrollerAudioProcessor&);
    ~InputMonitor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void drawHeader (juce::Graphics&, juce::Rectangle<int>);
    void drawLeftStick (juce::Graphics&, juce::Rectangle<int>);
    void drawRightStick (juce::Graphics&, juce::Rectangle<int>);
    void drawTriggers (juce::Graphics&, juce::Rectangle<int>);
    void drawInstrument (juce::Graphics&, juce::Rectangle<int>);
    void drawButtons (juce::Graphics&, juce::Rectangle<int>);

    ChordtrollerAudioProcessor& processor;

    PadState pad;

    /** A local copy, retuned each frame to the key the audio thread is actually
        in, purely so names can be composed without sending text across a thread. */
    music::ChordRing ring;

    juce::TextButton resetButton { "NOLLSTALL" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputMonitor)
};

} // namespace chordtroller

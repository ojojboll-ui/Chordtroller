#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "../dsp/VoiceSpec.h"
#include "../input/GamepadReader.h"
#include "../input/PadMath.h"
#include "../music/ChordRing.h"

class ChordtrollerAudioProcessor;

namespace chordtroller
{

/**
    The player's face, drawn in the diagnostic screen's language.

    The monitor that came out of phase 1 held two kinds of information. One was
    the instrument — chord, key, bank, voice, zone, note, ring. The other was
    evidence: raw axis floats to three decimals, poll rate, worst interval,
    button counters. That second kind existed to prove the input layer was not
    lying, and it has stopped needing to.

    So this keeps the dark panels, the monospace and the hairlines, and drops
    everything that was only ever there to be checked. Two things replace the
    stick plots, and both are things a player actually reads: the chord ring with
    the thumb's aim on it, and the plate with the string that just sounded lit.

    Nothing stateful is computed here. Everything that decides what is playing is
    read from what the audio thread published.
*/
class InstrumentPanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit InstrumentPanel (ChordtrollerAudioProcessor&);
    ~InstrumentPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setCompFromMouse (int x, juce::Rectangle<int> track);

    void drawHeader (juce::Graphics&, juce::Rectangle<int>);
    void drawChordRing (juce::Graphics&, juce::Rectangle<int>);
    void drawPlate (juce::Graphics&, juce::Rectangle<int>);
    void drawTriggers (juce::Graphics&, juce::Rectangle<int>);
    void drawReadout (juce::Graphics&, juce::Rectangle<int>);

    void drawScaleRuler (juce::Graphics&, juce::Rectangle<int>);
    void drawTone (juce::Graphics&, juce::Rectangle<int>);
    void drawThereminLevels (juce::Graphics&, juce::Rectangle<int>);
    void drawThereminReadout (juce::Graphics&, juce::Rectangle<int>);

    bool isTheremin() const;
    bool isJuno() const;
    bool isAcid() const;

    /** A button's name as printed on the controller in the player's hands. The
        code names buttons by where they sit on an Xbox pad; a Nintendo pad has
        the same positions under different letters. */
    juce::String buttonName (const char* xboxName) const;

    void drawAcidStep (juce::Graphics&, juce::Rectangle<int>);
    void drawAcidSequence (juce::Graphics&, juce::Rectangle<int>);
    void drawAcidLevels (juce::Graphics&, juce::Rectangle<int>);
    void drawAcidReadout (juce::Graphics&, juce::Rectangle<int>);

    bool isStrings() const;
    bool isArp() const;

    void drawArpPlate (juce::Graphics&, juce::Rectangle<int>);
    void drawArpLevels (juce::Graphics&, juce::Rectangle<int>);
    void drawArpReadout (juce::Graphics&, juce::Rectangle<int>);

    void drawStringRing (juce::Graphics&, juce::Rectangle<int>);
    void drawStringBow (juce::Graphics&, juce::Rectangle<int>);
    void drawStringLevels (juce::Graphics&, juce::Rectangle<int>);
    void drawStringReadout (juce::Graphics&, juce::Rectangle<int>);

    void drawJunoPhrase (juce::Graphics&, juce::Rectangle<int>);
    void drawKeyboard (juce::Graphics&, juce::Rectangle<int>);
    void drawJunoLevels (juce::Graphics&, juce::Rectangle<int>);
    void drawJunoReadout (juce::Graphics&, juce::Rectangle<int>);

    /** The ring's label for a place: Chordtroller's own name, or the Juno's
        genre-coloured one. */
    juce::String ringName (int sector) const;

    ChordtrollerAudioProcessor& processor;

    PadState pad;
    music::ChordRing ring;

    juce::ToggleButton rumble { "VIBRATION" };

    /** The tempo, as something you can click and type into. Dragging still works
        on the rest of the cell, but a number you can only change by discovering
        that it drags is a number nobody changes. */
    juce::Label tempoField;

    /** The instrument switch. Everything around it stays put — that is the
        point of having one shell. */
    juce::ComboBox instrumentBox;

    /** Where the notes also go — a system MIDI port, for Reason, which drops a
        plugin's own MIDI out. Refreshed every couple of seconds, because virtual
        cables come and go while the plugin is open. */
    juce::ComboBox    midiPortBox;
    juce::StringArray midiPortIds;
    int               midiRefreshCountdown = 0;
    void refreshMidiPorts();

    /** Dragged on the tempo cell. Only ever consulted when no host is supplying
        a tempo, so a DAW's transport always wins. */
    bool   draggingTempo = false;
    bool   draggingComp  = false;
    double tempoAtDragStart = 96.0;

    /** Drawn by hand so the tick box matches everything around it. Declared as a
        pointer because the class itself lives in the .cpp, next to the palette. */
    std::unique_ptr<juce::LookAndFeel> look;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

} // namespace chordtroller

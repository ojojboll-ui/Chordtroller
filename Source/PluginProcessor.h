#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MidiEmitter.h"
#include "MidiPortSender.h"
#include "PadParameter.h"
#include "dsp/AcidEngine.h"
#include "dsp/ArpEngine.h"
#include "dsp/CompEngine.h"
#include "dsp/Ensemble.h"
#include "dsp/JunoEngine.h"
#include "dsp/StringEngine.h"
#include "dsp/StrumEngine.h"
#include "dsp/TempoDelay.h"
#include "dsp/ThereminEngine.h"
#include "input/GamepadReader.h"
#include "input/PadMath.h"
#include "music/ChordRing.h"

/**
    Phase 2: the instrument plays.

    The audio thread is the only place that owns playing state. It drains the
    button queue, reads the stick snapshot, decides the chord, and runs the strum
    clock per sample. The editor reads published copies and computes nothing of
    its own that could drift — Scriptor learned that the expensive way, with two
    interfaces onto the same state and no way to tell which one was lying.

    What is deliberately not here yet: the Omnichord's chorus and reverb, which
    are phase 3 and belong after the voices rather than inside them; the seventh
    bank on LB; MIDI out; and the design's real face.
*/
class ChordtrollerAudioProcessor : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    ChordtrollerAudioProcessor();
    ~ChordtrollerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chordtroller"; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    /** What a project or the standalone app keeps between sessions: the 303's
        pattern, and what its steps need to be the same notes again — scale,
        octave and key. The sound settings are not in it. */
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    chordtroller::GamepadReader& gamepad() noexcept { return reader; }

    /** One instance, shared by whoever turns a stick into a number. Becomes host
        parameters when there is a panel to put them on. */
    chordtroller::pad::PadTuning& tuning() noexcept { return padTuning; }

    /** Where the notes also go: a system MIDI port by identifier, or none when
        empty. Reason drops a plugin's own MIDI out, so this is how its
        instruments hear Chordtroller. Message thread. */
    bool setMidiPort (const juce::String& identifier) { return midiPort.open (identifier); }
    juce::String midiPortIdentifier() const { return midiPort.identifier(); }

    /** Two instruments behind one shell. The pad means different things in each,
        but the d-pad, VIEW and the stick clicks keep their jobs so only the
        sticks and triggers have to be relearned. */
    enum class Instrument { chordtroller, theremin, juno, acid, strings, arp };

    Instrument instrument() const noexcept
    {
        return (Instrument) currentInstrument.load (std::memory_order_relaxed);
    }

    void setInstrument (Instrument i) noexcept
    {
        currentInstrument.store ((int) i, std::memory_order_relaxed);
    }

    /** Haptics on or off. Read by the audio thread, written by the editor, so it
        is atomic rather than a plain bool. */
    bool rumbleEnabled() const noexcept { return rumbleOn.load (std::memory_order_relaxed); }
    void setRumbleEnabled (bool on) noexcept { rumbleOn.store (on, std::memory_order_relaxed); }

    /** Used when no host is telling us a tempo, which is every standalone run
        and any DAW with the transport stopped. */
    /** The accompaniment's own level, which is separate from the strings on
        purpose — the right trigger is a swell pedal for the plate and has no
        business over the comp. */
    /** The Juno's output level. Its triggers are the filter, so the level has
        to live on the panel — the same slot the accompaniment level uses. */
    float junoLevel() const noexcept { return junoLevelParam->get(); }

    /** The 303's output level, in the same panel slot the others use. */
    float acidLevel() const noexcept { return acidLevelParam->get(); }

    float compLevel() const noexcept { return compLevelParam->get(); }

    /** The strings' output level, the same panel slot again. */
    float stringLevel() const noexcept { return stringLevelParam->get(); }

    /** The arpeggiator's output level. */
    float arpLevel() const noexcept { return arpLevelParam->get(); }

    /** The panel's level bar, for whichever instrument is up. Message thread. A
        drag is one gesture, so a host records it as one move. */
    void beginLevelDrag();
    void setLevelFromPanel (float level);
    void endLevelDrag();

    double internalTempo() const noexcept { return ownTempo.load (std::memory_order_relaxed); }
    void setInternalTempo (double bpm) noexcept
    {
        ownTempo.store (juce::jlimit (40.0, 220.0, bpm), std::memory_order_relaxed);
    }

    //==========================================================================
    /** What the audio thread publishes for the screen. Plain numbers, never
        text and never objects, so nothing has to be owned across a thread. The
        editor rebuilds names from keyRoot and sector itself. */
    struct Published
    {
        std::atomic<int>   sector       { 0 };
        std::atomic<int>   keyRoot      { 0 };
        std::atomic<int>   stringIndex  { -1 };
        std::atomic<int>   stringCount  { 0 };
        std::atomic<int>   stringMidi   { -1 };
        std::atomic<int>   zoneLow      { 0 };
        std::atomic<int>   zoneHigh     { 0 };
        std::atomic<int>   voiceIndex   { 0 };
        std::atomic<int>   octave       { 0 };
        std::atomic<int>   bank         { 0 };
        std::atomic<bool>  chordLatched { false };
        std::atomic<bool>  minorMode    { false };
        std::atomic<bool>  compRunning  { false };
        std::atomic<int>   compPattern  { 0 };
        std::atomic<float> tempo        { 96.0f };
        std::atomic<bool>  tempoFromHost { false };
        std::atomic<bool>  hostPlaying   { false };
        std::atomic<int>   compStep     { -1 };
        std::atomic<int>   compVoices   { 0 };
        std::atomic<bool>  drumsOn      { false };

        // Theremin
        std::atomic<float> rawMidi      { 60.0f };
        std::atomic<float> soundingMidi { 60.0f };
        std::atomic<float> centsOff     { 0.0f };
        std::atomic<float> quantise     { 0.5f };
        std::atomic<int>   scaleIndex   { 0 };
        std::atomic<int>   timbreIndex  { 1 };
        std::atomic<int>   rangeOctaves { 2 };
        std::atomic<int>   glideIndex   { 1 };
        std::atomic<float> vibrato      { 0.0f };
        std::atomic<float> tremolo      { 0.0f };
        std::atomic<float> lfoRate      { 5.0f };
        std::atomic<bool>  pitchFrozen  { false };
        std::atomic<bool>  motionAvailable { false };   // this controller has motion sensors
        std::atomic<bool>  thereminMotion  { false };
        std::atomic<float> motionVolume    { -1.0f };   // the left hand's swell, or -1 when not in use

        // Juno
        std::atomic<int>   junoPatch     { 0 };
        std::atomic<int>   junoGenre     { 0 };
        std::atomic<int>   junoChorus    { 1 };
        std::atomic<bool>  junoBass      { false };
        std::atomic<bool>  junoExtended  { false };
        std::atomic<int>   junoStyle     { -1 };    // -1 when nothing is playing
        std::atomic<int>   junoLastStyle { 0 };
        std::atomic<int>   junoVariation { 0 };     // of the last style
        std::atomic<bool>  junoHold      { false };
        std::atomic<float> junoCutoff    { 0.5f };
        std::atomic<float> junoResonance { 0.1f };
        std::atomic<int>   junoOctave    { 0 };
        std::atomic<int>   junoSounding[6] {};
        std::atomic<int>   junoSoundingCount { 0 };

        // 303. Steps are AcidStep::pack()ed, so each is one atomic.
        std::atomic<int>   acidSteps[16] {};
        std::atomic<int>   acidLength    { 16 };
        std::atomic<int>   acidPlayhead  { 0 };
        std::atomic<int>   acidCursor    { 0 };
        std::atomic<bool>  acidRunning   { false };
        std::atomic<bool>  acidShift     { false };  // LB held: the settings layer
        std::atomic<float> acidDelayMix  { 0.0f };
        std::atomic<float> acidDelayFeedback { 0.53f };
        std::atomic<int>   acidScale     { 1 };
        std::atomic<int>   acidGlideMode { 1 };
        std::atomic<int>   acidDelayMode { 1 };      // division: 1/8, 3/16, 1/4
        std::atomic<int>   acidOctave    { 0 };
        std::atomic<bool>  acidSquare    { false };
        std::atomic<float> acidCutoff    { 0.0f };
        std::atomic<float> acidResonance { 0.0f };
        std::atomic<float> acidEnvMod    { 0.5f };
        std::atomic<float> acidDecay     { 0.4f };

        // Strings
        std::atomic<float> stringNote       { 62.0f };  // sounding, before vibrato
        std::atomic<int>   stringOpen       { 55 };     // the open string it is stopped on
        std::atomic<int>   stringSector     { 0 };
        std::atomic<int>   stringOctave     { 0 };      // LB/RB held: -1, 0 or +1
        std::atomic<int>   stringRegister   { 0 };
        std::atomic<int>   stringScale      { 0 };
        std::atomic<int>   stringInstrument { 0 };
        std::atomic<float> stringBow        { 0.0f };   // signed share of full bow speed
        std::atomic<float> stringVibrato    { 0.0f };
        std::atomic<float> stringTremolo    { 0.0f };
        std::atomic<float> stringPressure   { 0.0f };   // RT after the host's floor
        std::atomic<float> stringGlide      { 0.0f };   // LT
        std::atomic<float> stringGlideSeconds { 0.003f };
        std::atomic<float> stringLevel      { 0.0f };   // output peak
        std::atomic<float> stringBowPoint   { 0.1f };
        std::atomic<int>   stringDrone      { -1 };     // MIDI note, or -1
        std::atomic<bool>  stringPizz       { false };
        std::atomic<bool>  stringSection    { false };
        std::atomic<bool>  stringSordino    { false };

        // Arpeggiator
        std::atomic<int>   arpMode       { 0 };
        std::atomic<int>   arpRate       { 1 };
        std::atomic<bool>  arpPlaying    { false };
        std::atomic<bool>  arpHold       { false };
        std::atomic<int>   arpOctaves    { 1 };
        std::atomic<int>   arpSound      { 0 };
        std::atomic<int>   arpRhythm     { 0 };
        std::atomic<bool>  arpTriplet    { false };
        std::atomic<int>   arpDelay      { 0 };      // 0 off, 1 eighth, 2 dotted eighth
        std::atomic<bool>  arpDrums      { false };
        std::atomic<float> arpCutoff     { 0.0f };
        std::atomic<float> arpGate       { 0.5f };   // right stick Y, 0..1
        std::atomic<float> arpSpeed      { 0.0f };   // RT
        std::atomic<bool>  arpShift      { false };  // VIEW held
        std::atomic<int>   arpWindowLo   { 0 };
        std::atomic<int>   arpWindowSize { 0 };
        std::atomic<float> arpGateShare  { 0.25f };  // of a step
        std::atomic<int>   arpRegister   { 0 };
        std::atomic<int>   arpPoolSize   { 0 };
        std::atomic<int>   arpPool[16]   {};
        std::atomic<int>   arpLastIndex  { -1 };
        std::atomic<int>   arpRhythmPos  { 0 };
        std::atomic<int>   activeVoices { 0 };
        std::atomic<float> ringSeconds  { 4.0f };
        std::atomic<float> masterDb     { -60.0f };

        static constexpr int kButtonSlots = 16;
        std::atomic<int> pressCounts[kButtonSlots] {};
        std::atomic<int>          lastButton { -1 };
        std::atomic<bool>         lastDown   { false };
        std::atomic<std::uint32_t> lastTimeMs { 0 };
    };

    const Published& published() const noexcept { return pub; }
    void clearCounters() noexcept;

private:
    void handleButton (const chordtroller::PadEvent&) noexcept;

    chordtroller::GamepadReader   reader;
    chordtroller::pad::PadTuning  padTuning;
    chordtroller::music::ChordRing ring;
    chordtroller::dsp::StrumEngine engine;

    chordtroller::music::Chord currentChord;
    int chordSector = 0;

    chordtroller::dsp::Ensemble       ensemble;
    chordtroller::dsp::CompEngine     comp;
    chordtroller::dsp::ThereminEngine theremin;

    void cycleScaleFamily (int family) noexcept;
    void renderTheremin (juce::AudioBuffer<float>&, const chordtroller::PadState&);
    void renderJuno (juce::AudioBuffer<float>&, juce::MidiBuffer&, const chordtroller::PadState&);
    void handleJunoButton (const chordtroller::PadEvent&) noexcept;

    chordtroller::dsp::JunoEngine junoEngine;
    chordtroller::dsp::TempoDelay junoDelay;

    void renderAcid (juce::AudioBuffer<float>&, juce::MidiBuffer&, const chordtroller::PadState&);
    void handleAcidButton (const chordtroller::PadEvent&) noexcept;

    void renderStrings (juce::AudioBuffer<float>&, juce::MidiBuffer&, const chordtroller::PadState&);
    void handleStringButton (const chordtroller::PadEvent&) noexcept;

    /** Every note the strings have told the host about, released. */
    void stringNotesOff (juce::MidiBuffer&, int sampleOffset) noexcept;

    chordtroller::dsp::StringEngine strings;
    chordtroller::MidiEmitter       stringPizzMidi;

    int  stringScale    = 0;
    int  stringRegister = 0;
    int  stringSector   = 0;
    int  stringLastNote = 62;          // what LS locks as the drone
    int  stringDroneNote = -1;         // held by LS, or -1
    int  stringMelodySent = -1;        // the bowed notes the host has been sent
    int  stringDroneSent  = -1;

    /** A pluck is a flick: the stick leaves the middle and reaches the edge.
        It has to come home before it can pluck again, and how long the trip
        took is how hard. */
    bool         pizzArmed = false;
    juce::int64  pizzRestAt = 0, stringClock = 0;

    void renderArp (juce::AudioBuffer<float>&, juce::MidiBuffer&, const chordtroller::PadState&);
    void handleArpButton (const chordtroller::PadEvent&) noexcept;

    chordtroller::dsp::ArpEngine  arp;
    chordtroller::dsp::TempoDelay arpDelay;

    int arpDelayMode = 0;       // 0 off, 1 eighth, 2 dotted eighth
    int arpRegister  = 0;
    int  arpRateStep = 0;
    bool arpPressed  = false;   // RT past its threshold, with hysteresis
    bool arpShift    = false;   // VIEW held: the settings layer

    chordtroller::dsp::AcidEngine acidEngine;
    chordtroller::dsp::TempoDelay acidDelay;

    int   acidScaleSlot   = 0;
    int   acidGlideIndex  = 1;
    int   acidDelayDivision = 1;       // 1/8, 3/16, 1/4 — the dotted eighth first
    int   acidOctaveShift = 0;
    bool  acidRightHome   = true;      // RS only makes up a pattern with this
    bool  acidSquare      = false;


    /** LB held: the d-pad and the face buttons change settings instead of
        writing steps. */
    bool acidShift = false;


    /** Saved state arrives on whatever thread the host likes, and the pattern
        belongs to the audio thread. So a restore is parked here under a spin
        lock and picked up at the top of the next block — never written into the
        engine from outside. */
    struct AcidSnapshot
    {
        std::array<chordtroller::dsp::AcidStep, chordtroller::dsp::kAcidMaxSteps> steps {};
        int length    = chordtroller::dsp::kAcidMaxSteps;
        int scaleSlot = 0;
        int octave    = 0;
        int keyRoot   = 0;
    };

    AcidSnapshot      acidRestore;
    juce::SpinLock    acidRestoreLock;
    std::atomic<bool> acidRestorePending { false };

    void publishAcidPattern() noexcept;
    void applyAcidRestore() noexcept;

    int  junoChorusMode  = 1;       // 0 off, 1 I, 2 II, 3 I+II
    int  junoGenreIndex  = 0;
    int  junoOctaveShift = 0;
    int  junoPhraseSector = -1;
    int  previousInstrument = 0;
    Instrument blockInstrument = Instrument::chordtroller;

    std::atomic<int> currentInstrument { 0 };
    bool thereminLatched = false;
    int  thereminOctave  = 0;

    /** Motion mode. RB switches it on, and where the hands are at that moment is
        home: the middle of the range for the right hand, silence for the left. */
    bool thereminMotion = false;
    chordtroller::Gravity thereminHomeLeft, thereminHomeRight;
    void toggleThereminMotion() noexcept;
    juce::Reverb reverb;
    chordtroller::MidiEmitter midiOut;

    bool chordLatched = false;
    bool flushMidi    = false;

    std::atomic<bool>   rumbleOn   { true };
    std::atomic<double> ownTempo   { 96.0 };
    /** Host parameters, for automation, Remote Overrides and Reason's CV
        Programmer. The AudioProcessor owns them once added; these are views.
        Levels and the floors under the triggers belong to the host alone; the
        303's four knobs are shared with the sticks — see PadParameter. */
    juce::AudioParameterFloat* compLevelParam     = nullptr;
    juce::AudioParameterFloat* junoLevelParam     = nullptr;
    juce::AudioParameterFloat* junoCutoffParam    = nullptr;
    juce::AudioParameterFloat* junoResonanceParam = nullptr;
    juce::AudioParameterFloat* thereminLockParam  = nullptr;
    juce::AudioParameterFloat* acidLevelParam     = nullptr;
    juce::AudioParameterFloat* acidCutoffParam    = nullptr;
    juce::AudioParameterFloat* acidResonanceParam = nullptr;
    juce::AudioParameterFloat* stringLevelParam    = nullptr;
    juce::AudioParameterFloat* stringPressureParam = nullptr;
    juce::AudioParameterFloat* arpLevelParam       = nullptr;
    juce::AudioParameterFloat* arpCutoffParam      = nullptr;

    std::unique_ptr<chordtroller::PadParameter> acidEnv, acidDecay, acidMix, acidFeedback;

    juce::AudioParameterFloat* levelParameterForInstrument() const noexcept;

    /** Hands the pad's knob turns to the host. Message thread. */
    void timerCallback() override;

    struct HostTransport
    {
        double bpm = 0.0, ppq = 0.0;
        bool   playing = false;
    };

    HostTransport readHostTransport();

    chordtroller::MidiPortSender midiPort;

    /** The master level closes slower than it opens. Releasing the trigger used
        to cut everything in five milliseconds, which is not a swell pedal
        letting go — it is a switch. The reverb sits after this and carries the
        rest of the tail. */
    float masterLevel  = 0.0f;
    float openCoef     = 1.0f;
    float closeCoef    = 1.0f;

    Published pub;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordtrollerAudioProcessor)
};

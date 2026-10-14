#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "AcidVoice.h"
#include "../music/Scales.h"

namespace chordtroller::dsp
{

/** One step of the pattern. The note is a scale degree rather than a pitch, so
    changing the key or the scale moves the whole line with it — which is the
    303's transpose, for free. A rest keeps its degree, so turning it back into a
    note gives back the note it had. */
struct AcidStep
{
    int  degree = 0;        // up the scale from the root, 0..kAcidMaxDegree
    bool rest   = true;
    bool accent = false;
    bool slide  = false;    // this note carries on into the next step's

    /** Packed so the screen can have a step as one atomic int. */
    int pack() const noexcept
    {
        return (degree & 0xff) | (rest ? 1 << 8 : 0) | (accent ? 1 << 9 : 0) | (slide ? 1 << 10 : 0);
    }

    static AcidStep unpack (int bits) noexcept
    {
        AcidStep s;
        s.degree = bits & 0xff;
        s.rest   = ((bits >> 8) & 1) != 0;
        s.accent = ((bits >> 9) & 1) != 0;
        s.slide  = ((bits >> 10) & 1) != 0;
        return s;
    }
};

inline constexpr int kAcidMaxSteps   = 16;
inline constexpr int kAcidMaxDegree  = 24;     // two octaves of the chromatic scale
inline constexpr int kAcidBaseMidi   = 36;
inline constexpr int kAcidScaleSlots = 6;
inline constexpr int kAcidGlides     = 4;
inline constexpr int kAcidDelays     = 3;

/** The scales Y steps through, as indices into music::scaleAt. Acid lives in
    minor, so minor comes first; chromatic is last, for the lines a scale will
    not hold. */
inline int acidScaleAt (int slot) noexcept
{
    static constexpr std::array<int, kAcidScaleSlots> scales { 1, 7, 4, 5, 0, 8 };
    return scales[(size_t) (((slot % kAcidScaleSlots) + kAcidScaleSlots) % kAcidScaleSlots)];
}

inline int acidNotesInScale (int scaleIndex) noexcept
{
    const auto mask = music::scaleAt (scaleIndex).mask;
    int count = 0;

    for (int pc = 0; pc < 12; ++pc)
        if (((mask >> pc) & 1u) != 0u)
            ++count;

    return std::max (1, count);
}

/** The highest degree up and down reaches: two octaves above the root, whatever
    the scale — fifteen notes of minor, eleven of a pentatonic, twenty-five
    chromatic. */
inline int acidTopDegree (int scaleIndex) noexcept
{
    return std::min (kAcidMaxDegree, 2 * acidNotesInScale (scaleIndex));
}

/** The nth note up the scale from the key's root, wrapping into the next octave
    when the scale runs out. */
inline int acidMidiFor (int degree, int keyRoot, int scaleIndex, int octave) noexcept
{
    const auto mask = music::scaleAt (scaleIndex).mask;

    std::array<int, 12> tones {};
    int count = 0;

    for (int pc = 0; pc < 12; ++pc)
        if (((mask >> pc) & 1u) != 0u)
            tones[(size_t) count++] = pc;

    if (count == 0)
        count = 1;

    const int d = std::max (0, degree);

    return kAcidBaseMidi + ((keyRoot % 12) + 12) % 12 + 12 * octave
             + 12 * (d / count) + tones[(size_t) (d % count)];
}

inline float acidGlideSeconds (int index) noexcept
{
    static constexpr float seconds[kAcidGlides] { 0.03f, 0.06f, 0.14f, 0.30f };
    return seconds[std::clamp (index, 0, kAcidGlides - 1)];
}

inline const char* acidGlideName (int index) noexcept
{
    static const char* names[kAcidGlides] { "KORT", "303", "LANG", "SEG" };
    return names[std::clamp (index, 0, kAcidGlides - 1)];
}

/** The TempoDelay's three divisions, in its order. There is no off: the right
    stick's mix at zero is off. */
inline const char* acidDelayName (int index) noexcept
{
    static const char* names[kAcidDelays] { "1/8", "3/16", "1/4" };
    return names[std::clamp (index, 0, kAcidDelays - 1)];
}

/**
    A 16-step sequencer, written one step at a time under a cursor.

    Every edit happens to the step under the cursor, running or stopped. Stopped,
    each edit and each cursor move plays that step, so a line can be written by
    ear; running, the loop is what you hear, and a change is heard the next time
    the playhead gets there.

    **Up and down on a rest** first make it a note again at the pitch it had —
    an untouched step starts on the root — and only then move it. So the first
    press never skips the note you were probably after.

    There is no undo. A pattern is replaced by clearing it or by making up a new
    one, and both are meant to be final.
*/
class AcidEngine
{
public:
    struct NoteEvent
    {
        int   note     = 0;
        float velocity = 0.0f;
        int   offset   = 0;
        bool  on       = false;
    };

    static constexpr int kMaxEvents = 128;

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;
        voice.prepare (sampleRate);

        minGateSamples = std::max (1, (int) (sampleRate * 0.025));
        setTempo (bpm);
    }

    void setTempo (double beatsPerMinute) noexcept
    {
        bpm            = std::clamp (beatsPerMinute, 40.0, 220.0);
        samplesPerStep = sampleRate * 60.0 / bpm / 4.0;
        stepIncrement  = 1.0 / samplesPerStep;
    }

    /** Where randomize() starts from. A fixed default, so a test run is the same
        every time; the processor seeds it from the system clock. */
    void seed (std::uint32_t value) noexcept
    {
        rng = value != 0u ? value : 0x9e3779b9u;
    }

    //== sound ================================================================

    void setFilter (float cutoff, float resonance) noexcept { voice.setFilter (cutoff, resonance); }
    void setEnvMod (float amount) noexcept                  { voice.setEnvMod (amount); }
    void setDecay (float knob) noexcept                     { voice.setDecay (knob); }
    void setSquare (bool on) noexcept                       { voice.setSquare (on); }
    void setGlide (int index) noexcept                      { voice.setGlideSeconds (acidGlideSeconds (index)); }

    //== pitch ================================================================

    void setScale (int scaleIndexIn) noexcept { scaleIndex = scaleIndexIn; }
    void setKeyRoot (int pitchClass) noexcept { keyRoot = ((pitchClass % 12) + 12) % 12; }
    void setOctave (int octaveIn) noexcept    { octave = std::clamp (octaveIn, -2, 2); }

    int midiOf (int degree) const noexcept { return acidMidiFor (degree, keyRoot, scaleIndex, octave); }

    //== transport ============================================================

    void play() noexcept
    {
        if (running)
            return;

        closeGate();

        running  = true;
        phase    = 0.0;
        playhead = 0;

        strike (playhead);
    }

    void stop() noexcept
    {
        if (! running)
            return;

        running = false;
        closeGate();
    }

    bool isRunning() const noexcept { return running; }

    /** Started on a step if the host's position is within this much of its
        start; later than that, the note would sound late, so the next step is
        waited for instead. */
    static constexpr double kCatchSteps = 0.1;

    /** The sequencer and the host run at the same tempo and only part when the
        host jumps — a loop, a locate. Past this they are put back together. */
    static constexpr double kResyncSteps = 0.05;

    /**
        Once per block, from a host that has a transport, before any audio.

        While the host plays, the sequencer runs locked to it: sixteenths of the
        host's beat, with step 1 on the song's first beat — so a loop shorter than
        a bar cycles against the bars, the way a 303's does. The host starting
        starts it and the host stopping stops it. While the host is stopped, MENY
        runs it freely, as it always has.
    */
    void followHost (bool hostPlaying, double ppqAtBlockStart) noexcept
    {
        if (! hostPlaying)
        {
            if (hostDriven)
            {
                hostDriven = false;
                stop();
            }

            return;
        }

        const double position = std::max (0.0, ppqAtBlockStart) * 4.0;
        const double whole    = std::floor (position);
        const int    wanted   = (int) std::fmod (whole, (double) loopLength);
        const double fraction = position - whole;

        if (! running || ! hostDriven)
        {
            // Running freely when the host started: the host takes over.
            closeGate();

            running    = true;
            hostDriven = true;
            playhead   = wanted;
            phase      = fraction;

            if (phase < kCatchSteps)
                strike (playhead);

            return;
        }

        const double length = (double) loopLength;
        double apart = ((double) wanted + fraction) - ((double) playhead + phase);

        while (apart >  length * 0.5) apart -= length;
        while (apart < -length * 0.5) apart += length;

        if (std::abs (apart) > kResyncSteps)
        {
            playhead = wanted;
            phase    = fraction;

            if (phase < kCatchSteps)
                strike (playhead);
            else
                closeGate();
        }
    }

    bool isFollowingHost() const noexcept { return hostDriven; }

    /** Stop, and let go of whatever is sounding — leaving the instrument. */
    void allOff() noexcept
    {
        stop();
        closeGate();
    }

    //== editing, all of it at the cursor =====================================

    /** All sixteen steps, whatever the loop length: the cursor has to be able to
        reach past the end, or a loop could never be made longer again. */
    void moveCursor (int delta) noexcept
    {
        cursor = (((cursor + delta) % kAcidMaxSteps) + kAcidMaxSteps) % kAcidMaxSteps;
        auditionCursor();
    }

    void changeNote (int delta) noexcept
    {
        auto& s = pattern[(size_t) cursor];

        if (s.rest)
            s.rest = false;
        else
            s.degree = std::clamp (s.degree + delta, 0, acidTopDegree (scaleIndex));

        ++writes;
        auditionCursor();
    }

    void toggleAccent() noexcept
    {
        pattern[(size_t) cursor].accent = ! pattern[(size_t) cursor].accent;
        ++writes;
        auditionCursor();
    }

    void toggleSlide() noexcept
    {
        pattern[(size_t) cursor].slide = ! pattern[(size_t) cursor].slide;
        ++writes;
        auditionCursor();
    }

    void toggleRest() noexcept
    {
        pattern[(size_t) cursor].rest = ! pattern[(size_t) cursor].rest;
        ++writes;
        auditionCursor();
    }

    /** The note of the nearest step before the cursor that plays one, round the
        back of the pattern if need be. Repeated notes are most of a bass line,
        and this makes each one a single flick instead of counting presses. */
    void copyPrevious() noexcept
    {
        auto& s = pattern[(size_t) cursor];

        for (int back = 1; back < kAcidMaxSteps; ++back)
        {
            const auto& earlier = pattern[(size_t) ((cursor - back + kAcidMaxSteps) % kAcidMaxSteps)];

            if (! earlier.rest)
            {
                s.degree = earlier.degree;
                break;
            }
        }

        s.rest = false;
        ++writes;
        auditionCursor();
    }

    void setLoopEndAtCursor() noexcept
    {
        setLength (cursor + 1);
    }

    /** Steps past the end are kept, not cleared: shortening the loop and
        lengthening it again gives the notes back. */
    void setLength (int steps) noexcept
    {
        loopLength = std::clamp (steps, 1, kAcidMaxSteps);
        playhead  %= loopLength;
        ++writes;
    }

    void clearPattern() noexcept
    {
        for (auto& s : pattern)
            s = AcidStep {};

        ++writes;
        auditionCursor();
    }

    /**
        A new line made up on the spot, across all sixteen steps.

        Not noise. A bass line leans on its root and its octave, rests often
        enough to breathe, and slides only into notes — a slide into a rest is a
        slide nobody hears. The first step is always the root, so the loop has a
        downbeat to come home to.

        How busy it is, how often it accents and slides, and how far it climbs
        are drawn fresh each time, so pressing again gives a different kind of
        line rather than the same one reshuffled.
    */
    void randomize() noexcept
    {
        const int notes = acidNotesInScale (scaleIndex);
        const int top   = acidTopDegree (scaleIndex);

        const float restChance   = 0.15f + 0.25f * unit();
        const float accentChance = 0.12f + 0.18f * unit();
        const float slideChance  = 0.08f + 0.20f * unit();

        // How far above the root the in-between notes may go: somewhere from
        // just past the octave to the top of the range.
        const int reach = std::clamp (notes + 1 + (int) (unit() * (float) notes), 1, top);

        for (int i = 0; i < kAcidMaxSteps; ++i)
        {
            auto& s = pattern[(size_t) i];

            s.rest   = i > 0 && unit() < restChance;
            s.accent = unit() < accentChance;
            s.slide  = unit() < slideChance;

            const float pick = unit();

            if (pick < 0.35f)
                s.degree = 0;
            else if (pick < 0.50f)
                s.degree = std::min (top, notes);
            else
                s.degree = 1 + (int) (unit() * (float) reach);
        }

        pattern[0].degree = 0;

        for (int i = 0; i < kAcidMaxSteps; ++i)
        {
            const int next = (i == loopLength - 1) ? 0 : (i + 1) % kAcidMaxSteps;

            if (pattern[(size_t) next].rest)
                pattern[(size_t) i].slide = false;
        }

        ++writes;
        auditionCursor();
    }

    /** A pattern from saved state. */
    void loadPattern (const std::array<AcidStep, kAcidMaxSteps>& steps, int stepCount) noexcept
    {
        pattern    = steps;
        loopLength = std::clamp (stepCount, 1, kAcidMaxSteps);
        playhead  %= loopLength;
        ++writes;
    }

    //== reading back =========================================================

    const AcidStep& step (int i) const noexcept { return pattern[(size_t) std::clamp (i, 0, kAcidMaxSteps - 1)]; }

    void setStep (int i, const AcidStep& s) noexcept
    {
        pattern[(size_t) std::clamp (i, 0, kAcidMaxSteps - 1)] = s;
        ++writes;
    }

    int  length()       const noexcept { return loopLength; }
    int  playheadStep() const noexcept { return playhead; }
    int  cursorStep()   const noexcept { return cursor; }
    int  soundingNote() const noexcept { return soundingMidi; }

    /** Goes up on every change to the pattern. */
    std::uint32_t writeCount() const noexcept { return writes; }

    //== audio ================================================================

    /** Before anything that can strike or release a note this block — an edit
        auditions, and see JunoEngine::beginBlock for what happens otherwise. */
    void beginBlock() noexcept
    {
        eventCount  = 0;
        blockOffset = 0;
    }

    float processSample() noexcept
    {
        if (running)
        {
            phase += stepIncrement;

            if (phase >= 1.0)
            {
                phase -= 1.0;
                playhead = (playhead + 1) % loopLength;
                strike (playhead);
            }
        }

        if (gateCountdown > 0 && --gateCountdown == 0)
            closeGate();

        const float out = voice.process();
        ++blockOffset;
        return out;
    }

    int eventsThisBlock() const noexcept { return eventCount; }
    const NoteEvent& event (int i) const noexcept { return events[(size_t) i]; }

private:
    int wrap (int i) const noexcept { return ((i % loopLength) + loopLength) % loopLength; }

    /** xorshift32, 24 bits of it as a float in [0, 1). */
    float unit() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float) (rng >> 8) / 16777216.0f;
    }

    void emit (int note, float velocity, bool on) noexcept
    {
        if (eventCount < kMaxEvents)
            events[(size_t) eventCount++] = { note, velocity, blockOffset, on };
    }

    void closeGate() noexcept
    {
        if (voice.isGateOpen())
            voice.release();

        if (soundingMidi >= 0)
        {
            emit (soundingMidi, 0.0f, false);
            soundingMidi = -1;
        }

        gateCountdown = 0;
    }

    void strike (int stepIndex) noexcept
    {
        const auto& s = pattern[(size_t) stepIndex];

        if (s.rest)
        {
            closeGate();
            return;
        }

        const int   note     = midiOf (s.degree);
        const auto& prev     = pattern[(size_t) wrap (stepIndex - 1)];
        const bool  legato   = prev.slide && ! prev.rest && voice.isGateOpen();
        const float velocity = s.accent ? 1.0f : 0.7f;

        if (legato)
        {
            if (note != soundingMidi)
            {
                // Overlapped on purpose: a note-on before the last note's
                // note-off is how a host is told legato, which is what a slide is.
                emit (note, velocity, true);

                if (soundingMidi >= 0)
                    emit (soundingMidi, 0.0f, false);

                soundingMidi = note;
            }

            voice.glideTo (note, s.accent);
        }
        else
        {
            if (soundingMidi >= 0)
                emit (soundingMidi, 0.0f, false);

            voice.trigger (note, s.accent);
            emit (note, velocity, true);
            soundingMidi = note;
        }

        // Half a step, the 303's own gate — unless this note slides on, in which
        // case the next step decides when it ends.
        const auto& next = pattern[(size_t) wrap (stepIndex + 1)];

        if (s.slide && ! next.rest && running)
            gateCountdown = 0;
        else
            gateCountdown = std::max (minGateSamples, (int) (0.5 * samplesPerStep));
    }

    /** Stopped, the step under the cursor is played, so a line can be written by
        ear. Running, the loop is already playing it. */
    void auditionCursor() noexcept
    {
        if (running)
            return;

        const auto& s = pattern[(size_t) cursor];

        if (s.rest)
        {
            closeGate();
            return;
        }

        const int note = midiOf (s.degree);

        if (soundingMidi >= 0)
            emit (soundingMidi, 0.0f, false);

        voice.trigger (note, s.accent);
        emit (note, s.accent ? 1.0f : 0.7f, true);
        soundingMidi = note;

        gateCountdown = std::max (minGateSamples, (int) (0.5 * samplesPerStep));
    }

    AcidVoice voice;

    std::array<AcidStep, kAcidMaxSteps> pattern {};
    std::array<NoteEvent, kMaxEvents>   events {};

    double sampleRate = 44100.0, bpm = 120.0;
    double samplesPerStep = 5512.5, stepIncrement = 1.0 / 5512.5, phase = 0.0;

    int minGateSamples = 1102, gateCountdown = 0;

    int loopLength = kAcidMaxSteps;
    int playhead = 0, cursor = 0;
    int scaleIndex = 1, keyRoot = 0, octave = 0;

    int soundingMidi = -1, eventCount = 0, blockOffset = 0;

    bool running = false, hostDriven = false;

    std::uint32_t rng = 0x2545f491u;
    std::uint32_t writes = 0;
};

} // namespace chordtroller::dsp

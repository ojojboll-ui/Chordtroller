#pragma once

#include "PluckVoice.h"
#include "../input/PadMath.h"
#include "../music/ChordRing.h"

#include <array>
#include <cmath>

namespace chordtroller::dsp
{

/**
    The thumb's rate, turned into notes.

    The stick hands over a number — how fast, and which way — and the clock that
    turns that into plucks lives here, running per sample inside the audio
    callback. That is the whole reason the mapping is rate-based rather than
    positional: poll jitter cannot smear the notes inside a sweep, because the
    poll never places a note. It only says how fast they should come.

    Three rules make it playable, and all three are in advanceAndPluck() and the
    branch above it:

    - Every direction change fires a note immediately and resets the accumulator.
      Without this a small movement can only ever mean "very slowly", never "one
      note", and the instrument cannot be picked.
    - The string index continues where the last sweep left it, so a stroke back
      does not restart from the edge.
    - Running off the end wraps rather than stopping, which is what gives the
      endless-harp feel a four-octave plate has.
*/
class StrumEngine
{
public:
    static constexpr int kVoices = 48;

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate    = sampleRateIn;
        invSampleRate = 1.0f / (float) sampleRateIn;

        for (auto& v : voices)
            v.prepare (sampleRateIn);

        phase = 0.0f;
        currentDirection = 0;
        currentString = -1;
    }

    /** Rebuilds the strings without moving the index: the register is fixed, so
        a chord change swaps the pitches under the thumb and leaves the sweep
        where it was. */
    void setChord (const music::Chord& chord) noexcept
    {
        strings.build (chord);

        if (strings.count > 0 && currentString >= strings.count)
            currentString = strings.count - 1;
    }

    void setStrumAxis (float squaredY, const pad::PadTuning& tuning) noexcept
    {
        rate     = pad::strumRate (squaredY, tuning);
        velocity = pad::strumVelocity (squaredY, tuning);
        depth    = pad::strumDepth (squaredY, tuning);

        targetDirection = rate > 0.0f ? (squaredY > 0.0f ? 1 : -1) : 0;
    }

    void setVoice (int index) noexcept
    {
        voiceIndex = ((index % kNumVoices) + kNumVoices) % kNumVoices;
    }

    int voice() const noexcept { return voiceIndex; }

    void setOctaveShift (int octaves) noexcept
    {
        octaveShift = std::clamp (octaves, -2, 2);
    }

    int octave() const noexcept { return octaveShift; }

    /** Called once at the top of every block. The sample cursor is what lets a
        pluck be reported at the offset it actually happened, which is what MIDI
        out needs to place a note inside the block rather than at its edge. */
    void beginBlock() noexcept
    {
        pluckCount   = 0;
        sampleCursor = 0;
    }

    struct PluckEvent
    {
        int   midiNote    = 0;
        float velocity    = 0.0f;
        int   sampleOffset = 0;
    };

    static constexpr int kMaxPlucksPerBlock = 64;

    int plucks() const noexcept { return pluckCount; }
    const PluckEvent& pluck (int i) const noexcept { return pluckEvents[(size_t) i]; }

    /**
        How much of the plate the sweep is allowed, and which end of it.

        Making the strum rate-based threw away the one thing a hand on a real
        plate actually decides — whereabouts it is stroking — and this puts it
        back. The first version snapped between four octave groups, which is only
        five states across the whole axis and feels like a switch under the
        thumb.

        This narrows instead. Centred, the sweep has all thirteen positions.
        Leaning shrinks the window down towards three while pinning it to the end
        you leaned towards, so the sweep tightens into a repeating figure low or
        high. Nothing jumps at the centre — at rest the window is simply the
        whole plate, and the first millimetre takes one string off the far end.

        Eleven widths is the ceiling, and it is the plate's, not the axis's:
        strings are whole things and there are thirteen of them.

        The thumb can reach this at any strum speed only because the axes were
        normalised out of the round gate. On the raw hardware the stick has
        almost no sideways travel left at full deflection.
    */
    void setZone (float squaredX, const pad::PadTuning& tuning) noexcept
    {
        if (strings.count <= 0)
            return;

        const float a = std::abs (squaredX);
        const float p = a <= tuning.zoneDeadzone
                          ? 0.0f
                          : std::clamp ((a - tuning.zoneDeadzone)
                                          / (1.0f - tuning.zoneDeadzone), 0.0f, 1.0f);

        const int widest    = strings.count;
        const int narrowest = std::min (kZoneSize, strings.count);

        const int width = std::clamp ((int) std::lround (
                              (float) widest + (float) (narrowest - widest) * p),
                              narrowest, widest);

        // Pinned to whichever end the thumb leaned towards. At full width both
        // give the same window, which is what keeps the centre continuous.
        const int lo = squaredX < 0.0f ? 0 : strings.count - width;
        const int hi = lo + width - 1;

        if (lo == windowLo && hi == windowHi)
            return;

        windowLo = lo;
        windowHi = hi;

        // Snap into the new window so the next pluck lands where the thumb is
        // pointing rather than wherever the last sweep happened to stop.
        if (currentString >= 0)
            currentString = std::clamp (currentString, windowLo, windowHi);
    }

    /** One coefficient for the whole voice pool, recomputed per block. Notes
        already ringing are retargeted too, so easing the trigger shortens what is
        sounding now rather than only the next pluck. */
    void setRing (float leftTrigger, const pad::PadTuning& tuning) noexcept
    {
        // The voice scales the trigger's setting rather than replacing it: vibes
        // ring longer than a guitar at every trigger position, and the trigger
        // still governs.
        ringLength = pad::ringSeconds (leftTrigger, tuning) * voiceSpec (voiceIndex).decayScale;

        decayCoefficient = std::pow (0.001f,
            1.0f / std::max (1.0f, (float) (sampleRate * (double) ringLength)));

        for (auto& v : voices)
            if (v.isActive())
                v.setDecayCoefficient (decayCoefficient);
    }

    void allNotesOff() noexcept
    {
        for (auto& v : voices)
            v.stopQuickly();
    }

    float processSample() noexcept
    {
        if (targetDirection != 0)
        {
            if (targetDirection != currentDirection)
            {
                // A flick, or a reversal. Either way it is a pluck, now.
                currentDirection = targetDirection;
                phase = 0.0f;
                advanceAndPluck();
            }
            else
            {
                phase += rate * invSampleRate;

                while (phase >= 1.0f)
                {
                    phase -= 1.0f;
                    advanceAndPluck();
                }
            }
        }
        else
        {
            // Back in the middle: the next push counts as a new stroke.
            currentDirection = 0;
        }

        float sum = 0.0f;

        for (auto& v : voices)
            sum += v.process();

        ++sampleCursor;

        return sum * kVoiceGain;
    }

    int   stringIndex()  const noexcept { return currentString; }
    int   stringCount()  const noexcept { return strings.count; }
    float ringSeconds()  const noexcept { return ringLength; }
    int   zoneLow()      const noexcept { return windowLo; }
    int   zoneHigh()     const noexcept { return windowHi; }

    /** How hard the last string was struck. For the haptics, which run a thread
        away from here — the pad's rumble call is a driver round trip and has no
        business on the audio thread. */
    float lastPluckStrength() const noexcept { return pluckStrength; }

    /** The note the last pluck landed on. Worth having on screen: the plate's
        order is not the pitch order, so the only way to see the pattern rolling
        is to watch the note rather than the index. */
    int currentMidi() const noexcept
    {
        return currentString >= 0 && currentString < strings.count
                 ? strings.midi[(size_t) currentString] : -1;
    }

    int activeVoices() const noexcept
    {
        int n = 0;

        for (const auto& v : voices)
            if (v.isActive())
                ++n;

        return n;
    }

private:
    // Twelve strings never ring at full level together — by the time the last
    // one is struck the first has decayed — so this is set for a hard sweep
    // rather than for the arithmetic worst case. Provisional until phase 3 puts
    // a real bus under it.
    static constexpr float kVoiceGain = 0.10f;

    // The plate is four octave groups of three, plus a closing root. The
    // narrowest the sweep window gets is one of those groups, so the tightest
    // figure is still root, third and colour rather than an arbitrary handful.
    static constexpr int kZoneSize = 3;

    void advanceAndPluck() noexcept
    {
        if (strings.count <= 0)
            return;

        const int lo   = std::clamp (windowLo, 0, strings.count - 1);
        const int hi   = std::clamp (windowHi, lo, strings.count - 1);
        const int span = hi - lo + 1;

        currentString = lo + ((currentString - lo + currentDirection) % span + span) % span;

        const int note = strings.midi[(size_t) currentString] + 12 * octaveShift;
        const float freq = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);

        allocate()->start (freq, velocity, decayCoefficient, voiceSpec (voiceIndex),
                           currentString, ++stamp);

        pluckStrength = velocity * (1.0f - 0.55f * depth);

        // Recorded rather than acted on: MIDI is written by the caller after the
        // block, and the haptics happen a thread away.
        if (pluckCount < kMaxPlucksPerBlock)
            pluckEvents[(size_t) pluckCount++] = { note, pluckStrength, sampleCursor };
    }

    /**
        Which voice takes the note.

        A string already ringing takes its own note back rather than layering a
        second copy on top: one string vibrates one way at a time, and stacking
        them is how a fast sweep turns into mud and eats the pool. Only after
        that does a free voice get a look in, and only after that the oldest one.
    */
    PluckVoice* allocate() noexcept
    {
        for (auto& v : voices)
            if (v.isActive() && v.string() == currentString)
                return &v;

        for (auto& v : voices)
            if (! v.isActive())
                return &v;

        auto* oldest = &voices[0];

        for (auto& v : voices)
            if (v.age() < oldest->age())
                oldest = &v;

        return oldest;
    }

    std::array<PluckVoice, kVoices> voices;
    music::StringSet strings;

    double sampleRate    = 44100.0;
    float  invSampleRate = 1.0f / 44100.0f;

    float rate       = 0.0f;
    float velocity   = 1.0f;
    float depth      = 0.0f;
    float phase      = 0.0f;
    float ringLength = 4.0f;
    float decayCoefficient = 0.999f;

    int   voiceIndex    = 0;
    int   octaveShift   = 0;
    int   pluckCount    = 0;
    int   sampleCursor  = 0;
    float pluckStrength = 0.0f;

    std::array<PluckEvent, kMaxPlucksPerBlock> pluckEvents {};

    int targetDirection  = 0;
    int currentDirection = 0;
    int currentString    = -1;

    int windowLo = 0;
    int windowHi = 0;

    std::uint64_t stamp = 0;
};

} // namespace chordtroller::dsp

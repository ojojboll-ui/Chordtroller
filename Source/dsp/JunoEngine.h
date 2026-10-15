#pragma once

#include "JunoPhrases.h"
#include "JunoVoice.h"
#include "../music/JunoChords.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace chordtroller::dsp
{

/**
    The chord generator: a chord from the left thumb, a way of playing it from
    the right, six Juno voices to play it on.

    Three things decide how it sounds, in this order of importance.

    **Voice leading.** Without it every chord arrives in root position and the
    whole pad jumps up and down the keyboard, which is the sound of a chord list
    rather than a keyboard player. Each new chord is tried in every inversion of
    its genre shape and the one that moves the fewest semitones from the last is
    kept. The upper structure of the shape survives — the ninth stays a ninth —
    because only the lowest notes are lifted.

    **The beat grid.** Phrases share one clock that starts when the right thumb
    first leaves the centre. Changing style or variation mid-bar keeps the beat
    and changes only what lands on it.

    **Six voices.** The Juno-6 had six, and five-note genre chords plus a bass
    fill them exactly. Long releases get stolen on busy phrases, released voices
    first, and that is the instrument's own behaviour rather than a shortfall.
*/
class JunoEngine
{
public:
    static constexpr int kVoices = 6;

    struct NoteEvent
    {
        int   note     = 0;
        float velocity = 0.0f;
        int   offset   = 0;
        bool  on       = false;
    };

    static constexpr int kMaxEvents = 256;

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;

        for (auto& v : voices)
            v.prepare (sampleRateIn);

        setPatch (patchIndex);
        setTempo (bpm);

        midiHeld.fill (false);
        playing = false;
        strumCount = 0;
        eventCount = 0;
    }

    //== sound ================================================================

    void setPatch (int index) noexcept
    {
        patchIndex = ((index % kNumJunoPatches) + kNumJunoPatches) % kNumJunoPatches;

        for (auto& v : voices)
            v.setPatch (junoPatch (patchIndex));
    }

    int patch() const noexcept { return patchIndex; }
    const JunoPatch& currentPatch() const noexcept { return junoPatch (patchIndex); }

    /** Slider positions for this block, triggers already added. */
    void setFilter (float cutoffPosition, float resonancePosition) noexcept
    {
        cutoff    = std::clamp (cutoffPosition, 0.0f, 1.0f);
        resonance = std::clamp (resonancePosition, 0.0f, 1.0f);
    }

    //== harmony ==============================================================

    void setChord (int rootPitchClass, music::FunctionClass function, bool extended, int genre) noexcept
    {
        if (rootPitchClass == chordRoot && function == chordFunction
             && extended == chordExtended && genre == chordGenre && voicedCount > 0)
            return;

        chordRoot     = ((rootPitchClass % 12) + 12) % 12;
        chordFunction = function;
        chordExtended = extended;
        chordGenre    = genre;

        leadVoicing();

        if (playing)
            followChord();
    }

    void setBass (bool on) noexcept
    {
        if (on == bassOn)
            return;

        bassOn = on;

        if (playing)
            followChord();
    }

    bool bassEnabled() const noexcept { return bassOn; }

    void setOctave (int octaves) noexcept
    {
        octave = std::clamp (octaves, -2, 2);

        if (playing)
            followChord();
    }

    int octaveShift() const noexcept { return octave; }

    //== phrase ===============================================================

    void setTempo (double beatsPerMinute) noexcept
    {
        bpm = std::clamp (beatsPerMinute, 40.0, 220.0);
        beatsPerSample = bpm / 60.0 / sampleRate;
    }

    /**
        Once per block from a host with a transport. While the host plays, the
        phrase clock is the song's beat: phrases land on the host's grid, and a
        push mid-bar comes in where the bar is rather than starting a bar of its
        own. While the host is stopped, each push starts the clock, as before.
    */
    void followHost (bool hostPlaying, double ppqAtBlockStart) noexcept
    {
        hostDriven = hostPlaying;

        if (! hostPlaying)
            return;

        const double beat = std::max (0.0, ppqAtBlockStart);

        if (std::abs (beat - beatPosition) > 0.02)
        {
            beatPosition = beat;

            // Jumped into the middle of a step: do not play it late.
            if (playing)
                lastStep = absoluteStep();
        }
    }

    /**
        Once per block, from the right trigger: how hard the phrase is played.

        Unlike the style this keeps applying while HOLD is on. A finger resting
        on a trigger has not let go of anything, so there is nothing to freeze it
        against — which was the whole reason the stick's distance could not be
        left in charge of it.
    */
    void setIntensity (float value) noexcept
    {
        intensity = std::clamp (value, 0.0f, 1.0f);
    }

    /**
        Once per block, from the right stick. A style of -1 means the thumb is
        back in the middle: the phrase stops, unless HOLD is keeping it — in
        which case it carries on with the last style.
    */
    void setPhrase (int style) noexcept
    {
        if (style < 0)
        {
            if (playing && ! hold)
                stop();

            return;
        }

        lastStyle = style;

        if (! playing)
            start (style);
        else if (style != currentStyle)
            switchTo (style);
    }

    void toggleHold() noexcept { hold = ! hold; }
    bool isHolding() const noexcept { return hold; }
    bool isPlaying() const noexcept { return playing; }

    int currentStyleIndex() const noexcept { return playing ? currentStyle : -1; }
    int lastStyleIndex()    const noexcept { return lastStyle; }
    int variationOf (int style) const noexcept
    {
        return variations[(size_t) (((style % kNumStyles) + kNumStyles) % kNumStyles)];
    }

    float currentIntensity() const noexcept { return intensity; }

    /** MENY: the next variation of whichever style is playing, or was last. */
    void cycleVariation() noexcept
    {
        const int style = playing ? currentStyle : lastStyle;
        const auto before = modeOf (style);

        auto& v = variations[(size_t) style];
        v = (v + 1) % kNumVariations;

        if (playing && style == currentStyle)
            changeMode (before, modeOf (style));
    }

    void allOff() noexcept
    {
        hold = false;
        stop();
    }

    //== audio ================================================================

    /**
        First thing every block, *before* any setChord, setPhrase or button.

        Those calls emit notes too — a pad struck, a chord change letting go of
        what no longer belongs — and they emit into this block's list. Calling
        this after them wiped those events, which silenced the pad and left every
        note it released hanging in the host. The offline test caught it; this
        comment is so the order is not "tidied" back.
    */
    void beginBlock() noexcept
    {
        eventCount = 0;
        cursor = 0;
    }

    float processSample() noexcept
    {
        const auto& p = currentPatch();

        lfoPhase += p.lfoRate / (float) sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;

        const float lfo = std::sin (6.28318530717959f * lfoPhase);

        // The beat runs while a phrase plays, and always while a host plays, so
        // a push lands where the host's bar is.
        if (playing || hostDriven)
            beatPosition += beatsPerSample;

        if (playing)
            tickClock();

        tickStrum();

        const float envScale = 0.4f + 0.6f * intensity;
        float sum = 0.0f;

        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            sum += v.process (lfo, cutoff, resonance, envScale);

            // The sound let go — an automatic gate ran out, or a pluck decayed
            // to nothing — so MIDI lets go at the same sample.
            const bool ended = v.takeGateEnded();

            if (midiHeld[(size_t) i] && (ended || ! v.isActive()))
                releaseMidi (i);
        }

        ++cursor;
        return sum * kOutputTrim;
    }

    int eventsThisBlock() const noexcept { return eventCount; }
    const NoteEvent& event (int i) const noexcept { return events[(size_t) i]; }

    //== for the screen =======================================================

    int voicedNotes (std::array<int, kVoices>& out) const noexcept
    {
        int n = 0;

        for (int i = 0; i < voicedCount && n < kVoices; ++i)
            out[(size_t) n++] = voiced[(size_t) i] + 12 * octave;

        if (bassOn && n < kVoices)
            out[(size_t) n++] = bassNote();

        return n;
    }

    int soundingNotes (std::array<int, kVoices>& out) const noexcept
    {
        int n = 0;

        for (const auto& v : voices)
            if (v.isActive() && v.isGateOn())
                out[(size_t) n++] = v.midiNote();

        return n;
    }

private:
    static constexpr float kOutputTrim = 0.24f;

    //== voicing ==============================================================

    /**
        Every inversion of the genre shape, lifted only from the bottom, placed
        so its lowest note sits between D3 and D4. The one that moves least from
        the previous chord wins; with nothing before it, the one nearest the
        middle of the keyboard.
    */
    void leadVoicing() noexcept
    {
        const auto& shape = music::junoShape (chordGenre, chordFunction, chordExtended);

        std::array<int, 5> best {};
        float bestCost = 1.0e9f;

        for (int inversion = 0; inversion < shape.count; ++inversion)
        {
            std::array<int, 5> candidate {};

            for (int i = 0; i < shape.count; ++i)
                candidate[(size_t) i] = shape.intervals[(size_t) i] + (i < inversion ? 12 : 0);

            std::sort (candidate.begin(), candidate.begin() + shape.count);

            int root = 36 + chordRoot;

            while (root + candidate[0] < 50) root += 12;
            while (root + candidate[0] >= 62) root -= 12;

            for (int i = 0; i < shape.count; ++i)
                candidate[(size_t) i] += root;

            const float cost = voicingCost (candidate, shape.count);

            if (cost < bestCost)
            {
                bestCost = cost;
                best = candidate;
            }
        }

        voiced = best;
        voicedCount = shape.count;
    }

    float voicingCost (const std::array<int, 5>& candidate, int count) const noexcept
    {
        float centre = 0.0f;

        for (int i = 0; i < count; ++i)
            centre += (float) candidate[(size_t) i];

        centre /= (float) count;

        if (voicedCount == 0)
            return std::abs (centre - 64.0f);

        float previousCentre = 0.0f;

        for (int i = 0; i < voicedCount; ++i)
            previousCentre += (float) voiced[(size_t) i];

        previousCentre /= (float) voicedCount;

        float movement = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            int nearest = 128;

            for (int j = 0; j < voicedCount; ++j)
                nearest = std::min (nearest, std::abs (candidate[(size_t) i] - voiced[(size_t) j]));

            movement += (float) nearest;
        }

        // Drift of the whole shape counts double: small moves that walk the pad
        // steadily upwards are still a walk.
        return movement + 2.0f * std::abs (centre - previousCentre);
    }

    int bassNote() const noexcept
    {
        int note = 36 + chordRoot + 12 * octave;

        while (note > 52) note -= 12;
        while (note < 24) note += 12;

        return note;
    }

    bool isTarget (int note) const noexcept
    {
        std::array<int, kVoices> target {};
        const int n = voicedNotes (target);

        for (int i = 0; i < n; ++i)
            if (target[(size_t) i] == note)
                return true;

        return false;
    }

    /** A chord change while playing. Notes that belong to the new chord keep
        sounding; the rest let go. A held pad then fills in what is new, so a
        progression on the pad is legato the way a player's hands are. */
    void followChord() noexcept
    {
        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            if (v.isGateOn() && ! isTarget (v.midiNote()))
            {
                v.noteOff();
                releaseMidi (i);
            }
        }

        if (playing && modeOf (currentStyle) == PhraseMode::hold)
            strikeHeld();
    }

    //== phrase playback ======================================================

    const PhraseVariation& variation() const noexcept
    {
        return phraseStyle (currentStyle).variations[(size_t) variationOf (currentStyle)];
    }

    PhraseMode modeOf (int style) const noexcept
    {
        return phraseStyle (style).variations[(size_t) variationOf (style)].mode;
    }

    void start (int style) noexcept
    {
        playing      = true;
        currentStyle = style;

        // A host's beat is kept: the phrase comes in where the song is.
        if (! hostDriven)
            beatPosition = 0.0;
        lastStep     = -1;          // fires the first step on the next sample
        arpIndex     = -1;
        arpDirection = 1;
        contourIndex = 0;
        strumUpwards = true;

        if (modeOf (style) == PhraseMode::hold)
            strikeHeld();
    }

    void switchTo (int style) noexcept
    {
        const auto before = modeOf (currentStyle);
        currentStyle = style;
        changeMode (before, modeOf (style));
    }

    /** Leaving a held pad lets it go; arriving at one strikes it. Everything
        else only changes what lands on the next step — the beat is kept. */
    void changeMode (PhraseMode before, PhraseMode after) noexcept
    {
        lastStep = absoluteStep();

        if (before == PhraseMode::hold && after != PhraseMode::hold)
            releaseAll();

        if (after == PhraseMode::hold && before != PhraseMode::hold)
            strikeHeld();
    }

    void stop() noexcept
    {
        playing = false;
        strumCount = 0;
        releaseAll();
    }

    /** Steps counted from the start of the beat, not within the figure. A
        figure one step long — FJARDEDEL, STRUM SNABB — sits on step 0 for ever,
        so comparing the step within the figure saw nothing new after the first
        hit and the phrase played once. */
    int absoluteStep() const noexcept
    {
        return (int) (beatPosition * variation().stepsPerBeat);
    }

    int samplesPerStep (const PhraseVariation& v) const noexcept
    {
        return std::max (1, (int) (sampleRate * 60.0 / (bpm * v.stepsPerBeat)));
    }

    void tickClock() noexcept
    {
        const auto& v = variation();

        if (v.mode == PhraseMode::hold)
            return;

        const int absolute = absoluteStep();

        if (absolute == lastStep)
            return;

        lastStep = absolute;

        const int step = absolute % (v.stepsPerBeat * v.beats);

        if (((v.hits >> step) & 1u) != 0u)
            fire (v, step);
    }

    void fire (const PhraseVariation& v, int step) noexcept
    {
        // The J-6 accents phrases; the downbeat of every beat is struck a little
        // harder than what falls between, which is most of what makes a
        // sequence groove rather than tick.
        const bool onBeat = (step % v.stepsPerBeat) == 0;
        const float velocity = (0.35f + 0.65f * intensity) * (onBeat ? 1.0f : 0.82f);
        const int gate = std::max (1, (int) (v.gate * (float) samplesPerStep (v)));

        if (bassOn && onBeat)
            play (bassNote(), velocity, std::max (gate, samplesPerStep (v) * v.stepsPerBeat));

        switch (v.mode)
        {
            case PhraseMode::pulse:
                for (int i = 0; i < voicedCount; ++i)
                    play (voiced[(size_t) i] + 12 * octave, velocity, gate);
                break;

            case PhraseMode::arp:
                play (nextArpNote (v.direction), velocity, gate);
                break;

            case PhraseMode::melody:
                play (toneAt (v.contour[(size_t) (contourIndex++ % 8)]), velocity, gate);
                break;

            case PhraseMode::strum:
                scheduleStrum (v, velocity, gate);
                break;

            case PhraseMode::hold:
                break;
        }
    }

    /** The voicing and the same voicing an octave up, walked in the arp's
        direction. Two octaves because one is over in four notes. */
    int nextArpNote (int direction) noexcept
    {
        const int n = voicedCount * 2;

        if (n <= 0)
            return 60;

        if (direction > 0)
        {
            arpIndex = (arpIndex + 1) % n;
        }
        else if (direction < 0)
        {
            arpIndex = arpIndex <= 0 ? n - 1 : arpIndex - 1;
        }
        else
        {
            arpIndex += arpDirection;

            if (arpIndex >= n) { arpIndex = n - 2; arpDirection = -1; }
            if (arpIndex < 0)  { arpIndex = std::min (1, n - 1); arpDirection = 1; }
        }

        return toneAt (arpIndex);
    }

    int toneAt (int index) const noexcept
    {
        if (voicedCount <= 0)
            return 60;

        const int i = std::max (0, index);
        return voiced[(size_t) (i % voicedCount)] + 12 * (i / voicedCount) + 12 * octave;
    }

    void scheduleStrum (const PhraseVariation& v, float velocity, int gate) noexcept
    {
        const int spacing = (int) (v.strumMs * 0.001 * sampleRate);

        for (int i = 0; i < voicedCount; ++i)
        {
            const int which = strumUpwards ? i : voicedCount - 1 - i;

            if (strumCount >= (int) strum.size())
                break;

            // Later notes in a strum are a little softer and a little shorter,
            // as they are under a hand.
            strum[(size_t) strumCount++] = { voiced[(size_t) which] + 12 * octave,
                                             velocity * (1.0f - 0.05f * (float) i),
                                             std::max (1, gate - i * spacing),
                                             i * spacing };
        }

        // Only a strum marked to alternate does; the rest always roll upwards.
        if (v.alternate)
            strumUpwards = ! strumUpwards;
    }

    void tickStrum() noexcept
    {
        for (int i = 0; i < strumCount;)
        {
            auto& s = strum[(size_t) i];

            if (s.delay-- > 0)
            {
                ++i;
                continue;
            }

            play (s.note, s.velocity, s.gate);
            strum[(size_t) i] = strum[(size_t) --strumCount];
        }
    }

    void strikeHeld() noexcept
    {
        const float velocity = 0.35f + 0.65f * intensity;

        std::array<int, kVoices> target {};
        const int n = voicedNotes (target);

        for (int i = 0; i < n; ++i)
        {
            bool alreadySounding = false;

            for (const auto& v : voices)
                if (v.isGateOn() && v.midiNote() == target[(size_t) i])
                    alreadySounding = true;

            if (! alreadySounding)
                play (target[(size_t) i], velocity, 0);
        }
    }

    void releaseAll() noexcept
    {
        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            if (v.isGateOn())
            {
                v.noteOff();
                releaseMidi (i);
            }
        }
    }

    //== voices and MIDI ======================================================

    void play (int note, float velocity, int gate) noexcept
    {
        note = std::clamp (note, 0, 127);

        int chosen = -1;

        // The same note already sounding takes it back: one key, one voice.
        for (int i = 0; i < kVoices && chosen < 0; ++i)
            if (voices[(size_t) i].isActive() && voices[(size_t) i].midiNote() == note)
                chosen = i;

        for (int i = 0; i < kVoices && chosen < 0; ++i)
            if (! voices[(size_t) i].isActive())
                chosen = i;

        // Stealing: a voice already in its release first, the oldest of those;
        // only then something still held.
        for (int pass = 0; pass < 2 && chosen < 0; ++pass)
        {
            std::uint64_t oldest = UINT64_MAX;

            for (int i = 0; i < kVoices; ++i)
            {
                const auto& v = voices[(size_t) i];

                if ((pass == 0 && v.isGateOn()) || v.age() >= oldest)
                    continue;

                oldest = v.age();
                chosen = i;
            }
        }

        if (chosen < 0)
            return;

        releaseMidi (chosen);

        voices[(size_t) chosen].noteOn (note, velocity, gate, ++order);
        emit (note, velocity, true);

        midiHeld[(size_t) chosen] = true;
        midiNotes[(size_t) chosen] = note;
    }

    void releaseMidi (int voice) noexcept
    {
        if (! midiHeld[(size_t) voice])
            return;

        midiHeld[(size_t) voice] = false;
        emit (midiNotes[(size_t) voice], 0.0f, false);
    }

    void emit (int note, float velocity, bool on) noexcept
    {
        if (eventCount < kMaxEvents)
            events[(size_t) eventCount++] = { note, velocity, cursor, on };
    }

    struct PendingStrum
    {
        int   note;
        float velocity;
        int   gate;
        int   delay;
    };

    std::array<JunoVoice, kVoices> voices;
    std::array<bool, kVoices> midiHeld {};
    std::array<int,  kVoices> midiNotes {};

    std::array<NoteEvent, kMaxEvents> events {};
    int eventCount = 0;
    int cursor = 0;

    std::array<PendingStrum, 32> strum {};
    int strumCount = 0;

    double sampleRate = 44100.0;
    double bpm = 96.0, beatsPerSample = 0.0, beatPosition = 0.0;
    bool   hostDriven = false;

    int patchIndex = 0;
    float cutoff = 0.0f, resonance = 0.0f, lfoPhase = 0.0f;

    int chordRoot = 0, chordGenre = 0;
    music::FunctionClass chordFunction = music::FunctionClass::major;
    bool chordExtended = false;

    std::array<int, 5> voiced {};
    int voicedCount = 0;

    bool bassOn = false;
    int  octave = 0;

    bool  playing = false, hold = false, strumUpwards = true;
    int   currentStyle = 0, lastStyle = 0, lastStep = -1;
    int   arpIndex = -1, arpDirection = 1, contourIndex = 0;
    float intensity = 0.7f;
    std::array<int, kNumStyles> variations {};

    std::uint64_t order = 0;
};

} // namespace chordtroller::dsp

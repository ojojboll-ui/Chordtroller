#pragma once

#include "CompVoice.h"
#include "DrumKit.h"
#include "../music/ChordRing.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace chordtroller::dsp
{

/**
    The accompaniment, without the drum machine.

    Nine figures on three buttons, in families. Pressing a button that already
    owns the current figure steps to the next one in its family, so the pad
    carries nine patterns without nine buttons — and, more usefully, each button
    means one thing. A is what holds, B is what marches, X is what swings.

    The three held figures are the reason to build this at all. The Omnichord's
    manual mode — a chord and a root and no pulse — is the sound most people who
    love the instrument are actually thinking of, and it needs no clock, no tempo
    and no drums. PAD drops the root for when something else is playing the bass;
    BAS drops the chord for when the strumming hand is already busy with it.

    One decision that matters more than any of the figures: **the accompaniment
    does not pass through the right trigger.** The trigger is a swell pedal for
    the strings, and an accompaniment that dies whenever you rest the strumming
    hand is not an accompaniment. The original has separate level controls for
    exactly this reason.
*/
enum class CompPattern
{
    drone, pad, bassOnly,       // held: no clock
    vamp, march, rock,          // straight
    bossa, swing, waltz         // swung and turning
};

inline constexpr int kNumCompPatterns = 9;
inline constexpr int kNumCompFamilies = 3;

inline const char* compPatternName (CompPattern p) noexcept
{
    switch (p)
    {
        case CompPattern::drone:    return "DRONE";
        case CompPattern::pad:      return "PAD";
        case CompPattern::bassOnly: return "BAS";
        case CompPattern::vamp:     return "VAMP";
        case CompPattern::march:    return "MARSCH";
        case CompPattern::rock:     return "ROCK";
        case CompPattern::bossa:    return "BOSSA";
        case CompPattern::swing:    return "SWING";
        case CompPattern::waltz:    return "VALS";
    }

    return "DRONE";
}

inline bool compPatternIsHeld (CompPattern p) noexcept
{
    return p == CompPattern::drone || p == CompPattern::pad || p == CompPattern::bassOnly;
}

class CompEngine
{
public:
    static constexpr int kMaxSteps    = 16;
    static constexpr int kChordVoices = 3;

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;

        for (auto& v : chordVoices)
        {
            v.prepare (sampleRateIn);
            v.setRegistration (0.42f, 0.18f);   // octave and twelfth: an organ
        }

        bass.prepare (sampleRateIn);
        bass.setRegistration (0.30f, 0.04f);    // almost pure, so it stays under

        drums.prepare (sampleRateIn);

        updateClock();
        allOff();
    }

    /** Once per block from a host with a transport. While the host plays, the
        figure's steps sit on the song's beat instead of counting from the press
        of RB, so the accompaniment and its drums land on the host's bars. */
    void followHost (bool hostPlaying, double ppqAtBlockStart) noexcept
    {
        hostDriven = hostPlaying;

        if (! hostPlaying)
            return;

        const double at = std::max (0.0, ppqAtBlockStart);

        if (std::abs (at - beat) > 0.02)
            beat = at;
    }

    void setTempo (double beatsPerMinute) noexcept
    {
        const auto wanted = std::clamp (beatsPerMinute, 40.0, 220.0);

        if (std::abs (wanted - bpm) < 1.0e-6)
            return;

        bpm = wanted;
        updateClock();
    }

    double tempo() const noexcept { return bpm; }

    void setLevel (float level) noexcept { outputLevel = std::clamp (level, 0.0f, 1.0f); }

    /** Drums ride on the same clock and the same level. Switching them on under
        a held figure starts the bar afresh, so the first kick lands on one
        rather than wherever the clock happened to be. */
    void setDrums (bool on) noexcept
    {
        if (on == drumsOn)
            return;

        drumsOn = on;

        if (! drumsOn)
        {
            drums.reset();
            return;
        }

        if (running && compPatternIsHeld (pattern))
        {
            step = -1;
            counter = 0;
        }
    }

    void toggleDrums() noexcept { setDrums (! drumsOn); }
    bool drumsEnabled() const noexcept { return drumsOn; }

    /**
        Press the family's button. Already inside that family and it steps on;
        coming from elsewhere and it takes the family's first figure, so a button
        always lands somewhere predictable rather than wherever it was left.
    */
    void cycleFamily (int family) noexcept
    {
        const auto& members = kFamilies[(size_t) std::clamp (family, 0, kNumCompFamilies - 1)];

        for (int i = 0; i < kFamilySize; ++i)
        {
            if (members[(size_t) i] != pattern)
                continue;

            apply (members[(size_t) (i + 1) % kFamilySize]);
            return;
        }

        apply (members[0]);
    }

    void setPattern (CompPattern p) noexcept
    {
        if (p != pattern)
            apply (p);
    }

    CompPattern currentPattern() const noexcept { return pattern; }

    void setChord (const music::Chord& c) noexcept
    {
        chord = c;
        bassAlternate = false;

        if (! running)
            return;

        if (compPatternIsHeld (pattern))
        {
            strikeHeld();
            return;
        }

        // A rhythm still waits for its next hit before striking again —
        // restriking off the beat is a stumble — but whatever is *already*
        // sounding follows the chord immediately. Leaving a held bass note on
        // the old root for the third of a second its gate has left is what makes
        // the accompaniment sound stuck, and it is most obvious with LB, where
        // the change is a colour rather than a new root.
        std::array<int, kChordVoices> notes {};
        voicing (notes);

        for (int i = 0; i < kChordVoices; ++i)
            if (chordVoices[(size_t) i].isActive())
                chordVoices[(size_t) i].setFrequency (frequencyOf (notes[(size_t) i]));

        if (bass.isActive())
            bass.setFrequency (frequencyOf (bassNote (false)));
    }

    void start() noexcept
    {
        if (running)
            return;

        running = true;
        step    = -1;
        counter = 0;

        if (compPatternIsHeld (pattern))
            strikeHeld();
    }

    void stop() noexcept
    {
        running = false;
        releaseAll();
    }

    void toggle() noexcept { running ? stop() : start(); }

    bool isRunning() const noexcept { return running; }

    /** Which step of the bar the clock is on, for the screen. */
    int currentStep() const noexcept { return running ? std::max (0, step) : -1; }

    int activeVoices() const noexcept
    {
        int n = bass.isActive() ? 1 : 0;

        for (const auto& v : chordVoices)
            if (v.isActive())
                ++n;

        return n;
    }

    float processSample() noexcept
    {
        if (hostDriven)
            beat += beatsPerSample;

        if (running)
        {
            // A held figure needs no clock of its own, but the drums do — so
            // switching them on under a drone starts one.
            if (hostDriven)
            {
                // Locked to the host: the step is wherever the song's beat is.
                if (! compPatternIsHeld (pattern) || drumsOn)
                {
                    const int now = stepAtBeat (beat);

                    if (now != step)
                    {
                        step = now;
                        fire (step);
                    }
                }
            }
            else if ((! compPatternIsHeld (pattern) || drumsOn) && --counter <= 0)
            {
                counter = samplesPerStep;
                step = (step + 1) % figureFor (pattern).length();
                fire (step);
            }
        }
        else
        {
            // A stopped accompaniment must be silent, and this makes that true
            // by construction rather than by every path remembering to release.
            releaseAll();
        }

        float sum = bass.process();

        for (auto& v : chordVoices)
            sum += v.process();

        if (drumsOn)
            sum += drums.processSample() * kDrumLevel;

        return sum * outputLevel;
    }

private:
    static constexpr int kFamilySize = 3;

    static constexpr std::array<std::array<CompPattern, kFamilySize>, kNumCompFamilies> kFamilies
    {{
        { CompPattern::drone, CompPattern::pad,   CompPattern::bassOnly },
        { CompPattern::vamp,  CompPattern::march, CompPattern::rock },
        { CompPattern::bossa, CompPattern::swing, CompPattern::waltz }
    }};

    /** Which steps a part lands on, as a bit per step. Written with at({...})
        below, which reads as the list of steps it is. */
    static std::uint16_t at (std::initializer_list<int> steps) noexcept
    {
        std::uint16_t mask = 0;

        for (const int s : steps)
            mask |= (std::uint16_t) (1u << s);

        return mask;
    }

    struct Figure
    {
        std::uint16_t bass, chord;
        std::uint16_t kick, snare, hat;
        int   stepsPerBeat;     // 4 for sixteenths, 3 for a triplet grid
        int   beats;            // 4, or 3 for the waltz
        float bassGate;
        float chordGate;

        int  length() const noexcept { return stepsPerBeat * beats; }
        static bool hits (std::uint16_t mask, int step) noexcept
        {
            return ((mask >> step) & 1u) != 0u;
        }
    };

    /**
        The grid is per figure, which is what lets swing swing and the waltz turn.

        Sixteenths cannot express a shuffle — the whole point of one is that the
        beat divides in three — so SWING runs on a triplet grid instead and its
        chord lands on two and four without any of the notes being nudged. VALS
        keeps sixteenths but only three beats to the bar.
    */
    static const Figure& figureFor (CompPattern p) noexcept
    {
        //                        bass           chord            kick           snare        hat
        static const Figure vamp
        {
            at ({ 0, 8 }), at ({ 4, 12 }),
            at ({ 0, 8 }), at ({ 4, 12 }), at ({ 0, 2, 4, 6, 8, 10, 12, 14 }),
            4, 4, 0.34f, 0.24f
        };

        // Four on the floor, chord answering on the backbeat.
        static const Figure march
        {
            at ({ 0, 4, 8, 12 }), at ({ 4, 12 }),
            at ({ 0, 4, 8, 12 }), at ({ 4, 12 }), at ({ 0, 2, 4, 6, 8, 10, 12, 14 }),
            4, 4, 0.20f, 0.22f
        };

        // The push before three is what makes it lean forward.
        static const Figure rock
        {
            at ({ 0, 3, 8, 11 }), at ({ 4, 12 }),
            at ({ 0, 3, 8, 11 }), at ({ 4, 12 }), at ({ 0, 2, 4, 6, 8, 10, 12, 14 }),
            4, 4, 0.22f, 0.26f
        };

        static const Figure bossa
        {
            at ({ 0, 6, 8, 14 }), at ({ 2, 6, 10, 14 }),
            at ({ 0, 6, 8, 14 }), at ({ 3, 10 }), at ({ 0, 2, 4, 6, 8, 10, 12, 14 }),
            4, 4, 0.26f, 0.18f
        };

        // Triplet grid, twelve steps: the ride's ding-ding-a-ding falls out of
        // the grid rather than being nudged into place.
        static const Figure swing
        {
            at ({ 0, 6 }), at ({ 3, 9 }),
            at ({ 0 }), at ({ 3, 9 }), at ({ 0, 2, 3, 6, 8, 9 }),
            3, 4, 0.28f, 0.20f
        };

        // Oom-pah-pah.
        static const Figure waltz
        {
            at ({ 0 }), at ({ 4, 8 }),
            at ({ 0 }), at ({ 4, 8 }), at ({ 0, 2, 4, 6, 8, 10 }),
            4, 3, 0.30f, 0.22f
        };

        // The held figures play nothing themselves, but they still need a beat
        // for the drums to sit on — a drone over a backbeat is exactly what the
        // original's manual mode plus rhythm is.
        static const Figure held
        {
            0, 0,
            at ({ 0, 8 }), at ({ 4, 12 }), at ({ 0, 2, 4, 6, 8, 10, 12, 14 }),
            4, 4, 0.0f, 0.0f
        };

        switch (p)
        {
            case CompPattern::vamp:  return vamp;
            case CompPattern::march: return march;
            case CompPattern::rock:  return rock;
            case CompPattern::bossa: return bossa;
            case CompPattern::swing: return swing;
            case CompPattern::waltz: return waltz;
            default:                 return held;
        }
    }

    void apply (CompPattern p) noexcept
    {
        pattern = p;
        updateClock();

        if (! running)
            return;

        // Changing figure mid-bar restarts the bar rather than landing in the
        // middle of something that was never played.
        step = -1;
        counter = 0;

        if (compPatternIsHeld (pattern))
            strikeHeld();
        else
            releaseAll();
    }

    int stepAtBeat (double b) const noexcept
    {
        const auto& figure = figureFor (pattern);
        const double beats = (double) std::max (1, figure.beats);

        return ((int) (std::fmod (b, beats) * figure.stepsPerBeat)) % std::max (1, figure.length());
    }

    void updateClock() noexcept
    {
        const auto perBeat = std::max (1, figureFor (pattern).stepsPerBeat);
        samplesPerStep = std::max (1, (int) std::lround (sampleRate * 60.0 / bpm / perBeat));
        beatsPerSample = bpm / 60.0 / sampleRate;
    }

    static float frequencyOf (int midiNote) noexcept
    {
        return 440.0f * std::pow (2.0f, (float) (midiNote - 69) / 12.0f);
    }

    /** Root in the second octave, where a bass belongs and where nothing the
        plate does can reach. */
    int bassNote (bool fifth) const noexcept
    {
        const int pc = fifth ? (chord.rootPitchClass + 7) % 12 : chord.rootPitchClass;
        return 36 + pc;
    }

    /** A close root-position voicing just under the middle of the plate's range.
        Sevenths drop the fifth exactly as the plate does, so the two sections
        agree about what the chord is. */
    void voicing (std::array<int, kChordVoices>& out) const noexcept
    {
        const int pc = chord.rootPitchClass;
        const int base = 55 + ((((pc - 55) % 12) + 12) % 12);

        out[0] = base;
        out[1] = base + music::chordThird (chord.quality);
        out[2] = base + music::chordColourTone (chord.quality);
    }

    /**
        The held figures, and which parts of them sound.

        Sustained sits lower than struck, and has to. A stab is present for a
        fifth of a second and gone; a held chord is present the whole time, and
        at equal peak level it is the louder thing by a wide margin — loudness is
        not peaks, it is how long the ear is given to add them up.
    */
    static constexpr float kHeldChordLevel = 0.15f;
    static constexpr float kHeldBassLevel  = 0.28f;

    static constexpr float kStabChordLevel = 0.26f;
    static constexpr float kStabBassLevel  = 0.55f;

    void strikeHeld() noexcept
    {
        const bool wantsChord = (pattern != CompPattern::bassOnly);
        const bool wantsBass  = (pattern != CompPattern::pad);

        if (wantsChord)
        {
            std::array<int, kChordVoices> notes {};
            voicing (notes);

            for (int i = 0; i < kChordVoices; ++i)
                chordVoices[(size_t) i].noteOn (frequencyOf (notes[(size_t) i]),
                                                kHeldChordLevel, 0.0f);
        }
        else
        {
            for (auto& v : chordVoices)
                v.noteOff();
        }

        if (wantsBass)
            bass.noteOn (frequencyOf (bassNote (false)), kHeldBassLevel, 0.0f);
        else
            bass.noteOff();
    }

    void releaseAll() noexcept
    {
        for (auto& v : chordVoices)
            v.noteOff();

        bass.noteOff();
    }

    void allOff() noexcept
    {
        for (auto& v : chordVoices)
            v.reset();

        bass.reset();
    }

    void fire (int atStep) noexcept
    {
        const auto& figure = figureFor (pattern);

        if (drumsOn)
        {
            if (Figure::hits (figure.kick, atStep))
                drums.trigger (DrumKit::Piece::kick, 0.95f);

            if (Figure::hits (figure.snare, atStep))
                drums.trigger (DrumKit::Piece::snare, 0.75f);

            if (Figure::hits (figure.hat, atStep))
                drums.trigger (DrumKit::Piece::hat, atStep % 4 == 0 ? 0.55f : 0.34f);
        }

        // Held figures borrow the clock for their drums and play nothing else.
        if (compPatternIsHeld (pattern))
            return;

        if (Figure::hits (figure.bass, atStep))
        {
            bass.noteOn (frequencyOf (bassNote (bassAlternate)), kStabBassLevel,
                         figure.bassGate);
            bassAlternate = ! bassAlternate;
        }

        if (! Figure::hits (figure.chord, atStep))
            return;

        std::array<int, kChordVoices> notes {};
        voicing (notes);

        for (int i = 0; i < kChordVoices; ++i)
            chordVoices[(size_t) i].noteOn (frequencyOf (notes[(size_t) i]),
                                            kStabChordLevel, figure.chordGate);
    }

    // Loud enough to drive, quiet enough to stay behind the plate. Scaled by the
    // same level control as the rest of the accompaniment, because it is the
    // rest of the accompaniment.
    static constexpr float kDrumLevel = 0.55f;

    std::array<CompVoice, kChordVoices> chordVoices;
    CompVoice bass;
    DrumKit   drums;

    music::Chord chord;
    CompPattern  pattern = CompPattern::drone;

    double sampleRate     = 44100.0;
    double bpm            = 96.0;
    int    samplesPerStep = 7500;
    int    counter        = 0;
    int    step           = -1;

    double beat           = 0.0;
    double beatsPerSample = 96.0 / 60.0 / 44100.0;
    bool   hostDriven     = false;

    float outputLevel   = 0.30f;
    bool  running       = false;
    bool  drumsOn       = false;
    bool  bassAlternate = false;
};

} // namespace chordtroller::dsp

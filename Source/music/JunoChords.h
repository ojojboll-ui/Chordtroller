#pragma once

#include "ChordRing.h"

#include <array>
#include <string>

namespace chordtroller::music
{

/**
    The Juno's chord vocabulary: genres, after the Roland J-6.

    The J-6's central idea is that a genre is mostly a voicing. Its hundred chord
    sets are the same few functions — home, dominant, the minor chords around
    them — dressed differently: synthwave stacks octaves and sus2s, neo soul
    lives on ninths and elevenths, gospel reaches for 9sus4 and the sharp nine.
    The same progression played in each sounds like a different record.

    That fits the ring exactly. Its eight places do not move — tonic straight up,
    the same hand movements as Chordtroller — and the genre decides what colour
    each function is given. The ring hands over its seventh bank, which says
    what *kind* of chord each place is; this table says what that kind sounds
    like in the genre, and what LB reaches for past it.

    Five notes at most, because the Juno has six voices and the bass needs one.
*/
enum class FunctionClass { major, minor, dominant, halfDiminished };

inline FunctionClass functionOf (Quality q) noexcept
{
    switch (q)
    {
        case Quality::minor:
        case Quality::minor7:         return FunctionClass::minor;
        case Quality::dominant7:      return FunctionClass::dominant;
        case Quality::diminished:
        case Quality::halfDiminished: return FunctionClass::halfDiminished;
        default:                      return FunctionClass::major;
    }
}

struct ChordShape
{
    const char*        suffix;
    std::array<int, 5> intervals;
    int                count;
};

inline constexpr int kNumGenres = 6;

inline const char* genreName (int genre) noexcept
{
    static const char* names[kNumGenres] =
        { "POP", "SYNTHWAVE", "HOUSE", "NEO SOUL", "GOSPEL", "LOFI" };

    return names[((genre % kNumGenres) + kNumGenres) % kNumGenres];
}

/** [genre][function][plain, extended]. Read across a row to hear a genre. */
inline const ChordShape& junoShape (int genre, FunctionClass function, bool extended) noexcept
{
    static const ChordShape table[kNumGenres][4][2] =
    {
        // POP — add9 on the major chords, the Coldplay shimmer; the dominant
        // stays a plain chord with a ninth because pop rarely wants a real V7.
        {
            { { "add9",  { 0, 4, 7, 14 },      4 }, { "maj9",    { 0, 4, 7, 11, 14 }, 5 } },
            { { "m7",    { 0, 3, 7, 10 },      4 }, { "m9",      { 0, 3, 7, 10, 14 }, 5 } },
            { { "add9",  { 0, 4, 7, 14 },      4 }, { "9sus4",   { 0, 5, 7, 10, 14 }, 5 } },
            { { "m7b5",  { 0, 3, 6, 10 },      4 }, { "m7b5",    { 0, 3, 6, 10, 12 }, 5 } }
        },

        // SYNTHWAVE — sus2 and octave stacks: open, hollow, eighties.
        {
            { { "sus2",  { 0, 2, 7, 12 },      4 }, { "add9",    { 0, 4, 7, 12, 14 }, 5 } },
            { { "madd9", { 0, 3, 7, 14 },      4 }, { "m9",      { 0, 3, 7, 10, 14 }, 5 } },
            { { "sus2",  { 0, 2, 7, 12 },      4 }, { "7sus4",   { 0, 5, 7, 10, 12 }, 5 } },
            { { "m7b5",  { 0, 3, 6, 10 },      4 }, { "m7b5",    { 0, 3, 6, 10, 12 }, 5 } }
        },

        // HOUSE — the minor seventh and its eleventh are the deep-house chord;
        // the dominant hangs on a sus4 and never quite resolves.
        {
            { { "maj7",  { 0, 4, 7, 11 },      4 }, { "maj9",    { 0, 4, 7, 11, 14 }, 5 } },
            { { "m7",    { 0, 3, 7, 10 },      4 }, { "m11",     { 0, 3, 7, 10, 17 }, 5 } },
            { { "7sus4", { 0, 5, 7, 10 },      4 }, { "9sus4",   { 0, 5, 7, 10, 14 }, 5 } },
            { { "m7b5",  { 0, 3, 6, 10 },      4 }, { "m7b5",    { 0, 3, 6, 10, 12 }, 5 } }
        },

        // NEO SOUL — nothing below a ninth; the dominant gets its thirteenth or
        // its sharp nine, which is most of what the genre sounds like.
        {
            { { "maj9",  { 0, 4, 7, 11, 14 },  5 }, { "maj9#11", { 0, 4, 11, 14, 18 }, 5 } },
            { { "m9",    { 0, 3, 7, 10, 14 },  5 }, { "m11",     { 0, 3, 10, 14, 17 }, 5 } },
            { { "13",    { 0, 4, 10, 14, 21 }, 5 }, { "7#9",     { 0, 4, 7, 10, 15 }, 5 } },
            { { "m9b5",  { 0, 3, 6, 10, 14 },  5 }, { "m11b5",   { 0, 3, 6, 10, 17 }, 5 } }
        },

        // GOSPEL — 9sus4 as the dominant is the church, 6/9 is the resolution.
        {
            { { "maj7",  { 0, 4, 7, 11 },      4 }, { "6/9",     { 0, 4, 7, 9, 14 },  5 } },
            { { "m7",    { 0, 3, 7, 10 },      4 }, { "m9",      { 0, 3, 7, 10, 14 }, 5 } },
            { { "9sus4", { 0, 5, 7, 10, 14 },  5 }, { "7#9",     { 0, 4, 7, 10, 15 }, 5 } },
            { { "m7b5",  { 0, 3, 6, 10 },      4 }, { "dim7",    { 0, 3, 6, 9 },      4 } }
        },

        // LOFI — soft sevenths and ninths, a dominant that is a ninth rather
        // than a seventh because nothing here is in a hurry to resolve.
        {
            { { "maj7",  { 0, 4, 7, 11 },      4 }, { "maj9",    { 0, 4, 7, 11, 14 }, 5 } },
            { { "m9",    { 0, 3, 7, 10, 14 },  5 }, { "m11",     { 0, 3, 10, 14, 17 }, 5 } },
            { { "9",     { 0, 4, 7, 10, 14 },  5 }, { "13",      { 0, 4, 10, 14, 21 }, 5 } },
            { { "m7b5",  { 0, 3, 6, 10 },      4 }, { "m9b5",    { 0, 3, 6, 10, 14 }, 5 } }
        }
    };

    const int g = ((genre % kNumGenres) + kNumGenres) % kNumGenres;
    return table[g][(int) function][extended ? 1 : 0];
}

inline std::string junoChordName (const Chord& chord, int genre, bool extended)
{
    return std::string (pitchClassName (chord.rootPitchClass))
             + junoShape (genre, functionOf (chord.quality), extended).suffix;
}

} // namespace chordtroller::music

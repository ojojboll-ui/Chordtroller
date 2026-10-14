#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace chordtroller::music
{

/**
    Scales, and the pull towards them.

    The pull is the important part, and it exists because of the hardware. A
    thumbstick gives eight or nine bits of *repeatable* position; spread four
    octaves across that and one step is about ten cents, which is audible as
    stepping on a slow glide and makes playing in tune by ear more or less
    impossible. Free pitch on a thumbstick is not a theremin, it is a slide
    whistle with a stutter.

    So quantising is not a convenience bolted on top — it is what makes the
    instrument playable. And that is exactly why it must not be a switch.

    At zero the pitch is untouched. In the middle it leans towards the nearest
    degree without ever reaching it unless you let it, so a deliberate bend still
    bends and an accidental wobble comes home. At one it lands on the note. The
    middle is where a theremin lives: the slide survives, the intonation does not
    have to.
*/
inline constexpr std::uint16_t scaleMask (std::initializer_list<int> degrees) noexcept
{
    std::uint16_t mask = 0;

    for (const int d : degrees)
        mask |= (std::uint16_t) (1u << (((d % 12) + 12) % 12));

    return mask;
}

struct Scale
{
    const char*   name;
    std::uint16_t mask;
};

inline constexpr int kNumScales   = 9;
inline constexpr int kScaleFamilies = 3;
inline constexpr int kScaleFamilySize = 3;

inline const Scale& scaleAt (int index) noexcept
{
    static const std::array<Scale, kNumScales> scales
    {{
        { "DUR",        scaleMask ({ 0, 2, 4, 5, 7, 9, 11 }) },
        { "MOLL",       scaleMask ({ 0, 2, 3, 5, 7, 8, 10 }) },
        { "HARM MOLL",  scaleMask ({ 0, 2, 3, 5, 7, 8, 11 }) },

        { "DURPENT",    scaleMask ({ 0, 2, 4, 7, 9 }) },
        { "MOLLPENT",   scaleMask ({ 0, 3, 5, 7, 10 }) },
        { "BLUES",      scaleMask ({ 0, 3, 5, 6, 7, 10 }) },

        { "HELTON",     scaleMask ({ 0, 2, 4, 6, 8, 10 }) },
        { "FREYGISK",   scaleMask ({ 0, 1, 4, 5, 7, 8, 10 }) },
        { "KROMATISK",  scaleMask ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }) }
    }};

    return scales[(size_t) (((index % kNumScales) + kNumScales) % kNumScales)];
}

/** Which family a scale belongs to, so a button can step within its own three. */
inline int scaleFamilyOf (int index) noexcept { return index / kScaleFamilySize; }

/** What a family's button does: inside the family already, step on; coming
    from elsewhere, take its first, so a button always lands somewhere
    predictable. The accompaniment's figure buttons follow the same rule. */
inline int stepScaleFamily (int current, int family) noexcept
{
    const int first = family * kScaleFamilySize;

    return scaleFamilyOf (current) == family ? first + (current - first + 1) % kScaleFamilySize
                                             : first;
}

/**
    Sharps or flats for a scale, for an instrument that names single notes.

    The chord ring can spell everything with flats, because it is read as
    chords round a circle of fifths. A melody cannot: D major with Gb and Db in
    it is unreadable. So each spelling is tried and the one that uses each
    letter once wins — F# beside G rather than Gb beside G. A tie, such as a
    whole-tone scale, goes to flats unless the root is spelt with a sharp.
*/
inline bool spellWithSharps (int keyRoot, std::uint16_t mask) noexcept
{
    static constexpr int sharpLetter[12] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };   // C C# D D# E F F# G G# A A# B
    static constexpr int flatLetter [12] { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };   // C Db D Eb E F Gb G Ab A Bb B

    auto repeats = [keyRoot, mask] (const int* letter)
    {
        int seen = 0, twice = 0;

        for (int d = 0; d < 12; ++d)
        {
            if (((mask >> d) & 1u) == 0u)
                continue;

            const int bit = 1 << letter[(keyRoot + d) % 12];
            twice += (seen & bit) != 0 ? 1 : 0;
            seen |= bit;
        }

        return twice;
    };

    const int sharps = repeats (sharpLetter), flats = repeats (flatLetter);

    if (sharps != flats)
        return sharps < flats;

    return keyRoot == 6;    // F# rather than Gb
}

inline const char* pitchClassNameSpelt (int pc, bool sharps) noexcept
{
    static const char* withSharps[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* withFlats [12] { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

    return (sharps ? withSharps : withFlats)[((pc % 12) + 12) % 12];
}

/**
    Leans a continuous pitch towards the nearest degree of the scale.

    `semitones` is measured from the key's root. Strength runs 0 (free) to 1
    (landed). Only the four semitones straddling the position are searched,
    which is enough: a scale never has a gap wider than three.
*/
inline float pullToScale (float semitones, std::uint16_t mask, float strength) noexcept
{
    const float amount = std::clamp (strength, 0.0f, 1.0f);

    if (amount <= 0.0f || mask == 0u)
        return semitones;

    const int first = (int) std::floor (semitones) - 1;

    float nearest = semitones;
    float best    = 1.0e9f;

    for (int n = first; n <= first + 3; ++n)
    {
        const int pitchClass = ((n % 12) + 12) % 12;

        if (((mask >> pitchClass) & 1u) == 0u)
            continue;

        const float distance = std::abs ((float) n - semitones);

        if (distance < best)
        {
            best = distance;
            nearest = (float) n;
        }
    }

    return semitones + (nearest - semitones) * amount;
}

} // namespace chordtroller::music

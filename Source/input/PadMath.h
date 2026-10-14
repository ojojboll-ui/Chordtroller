#pragma once

#include <algorithm>
#include <cmath>

namespace chordtroller::pad
{

/**
    Pure functions from stick and trigger readings to musical quantities.

    Nothing in here touches JUCE, the pad, or any state, which is deliberate:
    these are the decisions that will be argued about with a thumb on a stick,
    and they need to be readable and changeable in one place. The numbers are
    defaults in PadTuning, not constants — the plan says the strum curve is a
    guess until fingers say otherwise, so it must never be buried in a callsite.
*/

inline constexpr float kPi = 3.14159265358979f;

struct Vec2 { float x = 0.0f, y = 0.0f; };

/** How the raw readings are shaped. One struct so the tuning travels together
    and becomes host parameters in phase 2 without hunting for callsites. */
struct PadTuning
{
    float stickDeadzone   = 0.10f;  // both sticks, applied radially
    float triggerDeadzone = 0.04f;

    // Strum — right stick Y
    float strumDeadzone = 0.12f;
    float strumRateMin  = 2.0f;     // notes per second at the edge of the deadzone
    float strumRateMax  = 24.0f;    // ...and at full deflection

    float velocityDropAtFullRate = 0.35f;   // a fast sweep plucks lighter

    // Sweep window — right stick X. Small, because the window now narrows
    // smoothly from the centre rather than snapping between four buckets, so
    // there is nothing to protect the middle from.
    float zoneDeadzone = 0.10f;

    // How long a string rings, from the left trigger alone.
    float ringOpen     = 4.00f;     // seconds with LT released
    float ringSqueezed = 0.25f;     // seconds with LT fully pressed
    float sustainCurve = 0.65f;     // below 1 the trigger bites earlier in its travel

    // Chord ring — left stick
    float chordHoldRadius = 0.40f;  // inside this the last chord stays selected
    float chordHysteresis = 6.0f;   // degrees past a boundary before it switches

    // Volume — right trigger. The exponent is below 1 on purpose; see
    // triggerDecibels for why the first version had it backwards.
    float triggerFloorDb = -45.0f;
    float triggerExp     = 0.70f;
};

/** Radial deadzone with the remainder rescaled to the full range, so the first
    usable position is a small value and not a jump. Applied to the vector rather
    than each axis, or the deadzone would be a square hole and diagonals would
    engage before straight pushes. */
inline Vec2 applyDeadzone (float x, float y, float dz) noexcept
{
    const float r = std::sqrt (x * x + y * y);
    if (r <= dz || r <= 1.0e-6f)
        return {};

    const float scaled = std::min ((r - dz) / (1.0f - dz), 1.0f);
    return { x / r * scaled, y / r * scaled };
}

inline float applyDeadzone1D (float v, float dz) noexcept
{
    const float a = std::abs (v);
    if (a <= dz)
        return 0.0f;

    return std::copysign (std::min ((a - dz) / (1.0f - dz), 1.0f), v);
}

/**
    The stick sits in a round gate: pushed to the bottom, X has almost nowhere
    left to go. Untouched, that means damping stops working exactly when the
    strum is fastest, which reads as a bug rather than as physics.

    Scaling the vector by r/max(|x|,|y|) pushes the unit disc out to the unit
    square — a diagonal at the gate edge becomes a corner — so the two axes can
    be read as if they were independent.
*/
inline Vec2 discToSquare (float x, float y) noexcept
{
    const float m = std::max (std::abs (x), std::abs (y));
    if (m <= 1.0e-6f)
        return {};

    const float s = std::sqrt (x * x + y * y) / m;
    return { std::clamp (x * s, -1.0f, 1.0f), std::clamp (y * s, -1.0f, 1.0f) };
}

/** Compass bearing of the stick in degrees, 0 straight up, growing clockwise.
    atan2(x, y) rather than the usual atan2(y, x) because up is the home
    position of the chord ring and clockwise is the direction fifths rise. */
inline float bearingDegrees (float x, float y) noexcept
{
    float deg = std::atan2 (x, y) * 180.0f / kPi;
    return deg < 0.0f ? deg + 360.0f : deg;
}

inline constexpr int kSectors = 8;

/** Which of the eight 45-degree sectors the stick points into. 0 is up, then
    clockwise. */
inline int sectorFor (float x, float y) noexcept
{
    const float deg = bearingDegrees (x, y);
    return ((int) std::floor ((deg + 22.5f) / 45.0f)) % kSectors;
}

/**
    The same, but sticky.

    Two things keep the selection still. Inside chordHoldRadius nothing changes
    at all, so the stick can be released mid-sweep without the chord vanishing —
    a thumb returning to centre passes through no man's land, not through three
    other chords. And once a sector is held, leaving it takes hysteresis degrees
    more than entering it did, so a thumb resting on a boundary does not
    alternate at poll rate.

    Pass -1 as current on the first call to take whatever the stick says.
*/
inline int sectorWithHysteresis (float x, float y, int current, const PadTuning& t) noexcept
{
    if (std::sqrt (x * x + y * y) < t.chordHoldRadius)
        return current;

    const int nominal = sectorFor (x, y);
    if (current < 0 || nominal == current)
        return nominal;

    float delta = bearingDegrees (x, y) - (float) current * 45.0f;
    while (delta >  180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;

    return std::abs (delta) > 22.5f + t.chordHysteresis ? nominal : current;
}

/**
    Notes per second from the right stick's Y deflection.

    Squared rather than linear because a linear map puts everything playable in
    the last quarter of the throw: the useful range for strumming a chord is
    roughly 4 to 12 notes a second, and that has to sit under the middle of the
    thumb's travel, not at the very edge.

    Returns 0 inside the deadzone. Direction is the sign of y and is deliberately
    not folded in here — the caller needs to notice the sign changing, which is
    what fires a single note.
*/
inline float strumDepth (float y, const PadTuning& t) noexcept
{
    const float a = std::abs (y);
    if (a < t.strumDeadzone)
        return 0.0f;

    return std::clamp ((a - t.strumDeadzone) / (1.0f - t.strumDeadzone), 0.0f, 1.0f);
}

inline float strumRate (float y, const PadTuning& t) noexcept
{
    const float p = strumDepth (y, t);
    if (p <= 0.0f)
        return 0.0f;

    // Equal ratios: a rate is heard as how many times faster, so each stretch
    // of the throw doubles it about as often as the last.
    return t.strumRateMin * std::pow (t.strumRateMax / t.strumRateMin, p);
}

/** A hard sweep plucks each string a little lighter, the way a hand does, so
    twelve strings arriving at once do not pile up into a wall. */
inline float strumVelocity (float y, const PadTuning& t) noexcept
{
    return std::clamp (1.0f - t.velocityDropAtFullRate * strumDepth (y, t), 0.1f, 1.0f);
}

/**
    How long a string rings.

    Squeezing shortens: fingers push to stop things, so an untouched trigger is
    the instrument's natural long ring rather than its shortest.

    Two curves stacked. The outer one is exponential in time, so the middle of
    the travel is a musically even distance from either end rather than a
    lopsided one. The inner one bends the travel itself: below 1 it spends more
    of the trigger's throw near the top, so the first centimetre already does
    something audible instead of trimming four seconds down to three.
*/
inline float ringSeconds (float lt01, const PadTuning& t) noexcept
{
    const float squeeze = std::pow (std::clamp (lt01, 0.0f, 1.0f), t.sustainCurve);
    return t.ringOpen * std::pow (t.ringSqueezed / t.ringOpen, squeeze);
}

/**
    Right trigger as a swell pedal, in decibels.

    The first version raised the position to 2.2 and claimed that kept the quiet
    end reachable. It does the opposite: the map is already in decibels, which
    are already logarithmic, so a power above 1 pushes the whole useful range
    into the last few millimetres. Half travel came out at -47 dB — practically
    silent — and the trigger felt like a switch that fired near the end of its
    throw.

    An exponent below 1 leans the other way, so a light squeeze already sounds.
    A plain 1.0 would be the classic fader taper, linear in decibels and roughly
    linear in loudness; 0.7 sits a little ahead of that, which is what a swell
    pedal wants.

    The floor moved up too, from 60 dB of range to 45. Sixty decibels spread
    across two centimetres of trigger means most of the travel is spent on levels
    too quiet to be worth reaching.
*/
/**
    A level control's position, 0..1, as decibels: a fader law, straight in dB
    from -48 at the bottom to 0 at the top, and silence only at the very
    bottom. The level parameters used to be plain gain, which put everything
    anyone would set in the top third of the bar.
*/
inline constexpr float kFaderFloorDb = -48.0f;

inline float faderDecibels (float position) noexcept
{
    return kFaderFloorDb * (1.0f - std::clamp (position, 0.0f, 1.0f));
}

inline float faderGain (float position) noexcept
{
    return position <= 0.0f ? 0.0f : std::pow (10.0f, faderDecibels (position) / 20.0f);
}

inline float triggerDecibels (float t01, const PadTuning& t) noexcept
{
    const float p = std::pow (std::clamp (t01, 0.0f, 1.0f), t.triggerExp);
    return t.triggerFloorDb + (0.0f - t.triggerFloorDb) * p;
}

/**
    The eight chords, in fifths, home at the top.

    Reading clockwise from due west the roots form an unbroken chain — Bb F C G
    D A E B — which is why a progression is a short arc of the thumb rather than
    eight memorised directions. The two banks share those directions: LB colours
    the same chord rather than moving it, so there is only ever one layout to
    learn.

    Written without accidental glyphs on purpose. Scriptor's panel labels avoid
    them too; the default JUCE font is not to be trusted with them.
*/
inline const char* chordNameTriad (int sector) noexcept
{
    static const char* names[kSectors] = { "C", "G", "Dm", "Am", "Em", "Bdim", "Bb", "F" };
    return names[(unsigned) sector % kSectors];
}

inline const char* chordNameSeventh (int sector) noexcept
{
    static const char* names[kSectors] =
        { "Cmaj7", "G7", "Dm7", "Am7", "Em7", "Bm7b5", "Bbmaj7", "Fmaj7" };
    return names[(unsigned) sector % kSectors];
}

inline const char* sectorName (int sector) noexcept
{
    static const char* names[kSectors] = { "N", "NO", "O", "SO", "S", "SV", "V", "NV" };
    return names[(unsigned) sector % kSectors];
}

} // namespace chordtroller::pad

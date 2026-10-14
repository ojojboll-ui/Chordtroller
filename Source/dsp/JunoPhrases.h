#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>

namespace chordtroller::dsp
{

/**
    Eight styles under the right thumb, four variations each on MENY.

    Organised the way the Roland J-6 organises its phrases — by mechanism, not by
    genre. The genre already lives in the chords; the style only has to say *how*
    a chord is played: held, pulsed, arpeggiated, struck in a rhythm, strummed,
    gated, or turned into a line. A variation is direction, division or pattern
    inside that mechanism, which is why division needs no button of its own.

    Every variation sits on the same beat grid. Switching style mid-bar keeps the
    beat and changes only what lands on it, so rotating the thumb round the edge
    of the stick is a change of texture rather than a stumble.
*/
enum class PhraseMode { hold, pulse, arp, strum, melody };

struct PhraseVariation
{
    const char*   name;
    PhraseMode    mode;
    int           stepsPerBeat;     // 1 quarter, 2 eighth, 3 eighth triplet, 4 sixteenth ...
    int           beats;            // length of the figure
    std::uint32_t hits;             // one bit per step
    float         gate;             // in steps
    int           direction;        // arp: +1 up, -1 down, 0 up and down
    float         strumMs;          // strum: spread between notes
    std::array<int, 8> contour;     // melody: chord-tone index per hit
    bool          alternate = false; // strum: down on one hit, up on the next
};

inline std::uint32_t steps (std::initializer_list<int> list) noexcept
{
    std::uint32_t mask = 0;

    for (const int s : list)
        mask |= (std::uint32_t) (1u << s);

    return mask;
}

inline constexpr std::uint32_t kEvery = 0xffffffffu;

inline constexpr int kNumStyles     = 8;
inline constexpr int kNumVariations = 4;

struct PhraseStyle
{
    const char* name;
    std::array<PhraseVariation, kNumVariations> variations;
};

inline const PhraseStyle& phraseStyle (int style) noexcept
{
    using M = PhraseMode;

    static const std::array<PhraseStyle, kNumStyles> styles
    {{
        // N — the chord itself, at a note value. HEL is the pad: held until the
        // thumb lets go.
        { "PULS", {{
            { "HEL",       M::hold,  1, 4, 0,                     0.0f, 0, 0.0f, {} },
            { "HALV",      M::pulse, 1, 2, steps ({ 0 }),         1.85f, 0, 0.0f, {} },
            { "FJARDEDEL", M::pulse, 1, 1, steps ({ 0 }),         0.85f, 0, 0.0f, {} },
            { "ATTONDEL",  M::pulse, 2, 1, steps ({ 0, 1 }),      0.80f, 0, 0.0f, {} } }} },

        // NE — arpeggio in eighths.
        { "ARP 1/8", {{
            { "UPP",       M::arp, 2, 1, kEvery, 0.9f,  1, 0.0f, {} },
            { "NED",       M::arp, 2, 1, kEvery, 0.9f, -1, 0.0f, {} },
            { "UPP/NED",   M::arp, 2, 1, kEvery, 0.9f,  0, 0.0f, {} },
            { "TRIOL",     M::arp, 3, 1, kEvery, 0.9f,  1, 0.0f, {} } }} },

        // E — arpeggio in sixteenths.
        { "ARP 1/16", {{
            { "UPP",       M::arp, 4, 1, kEvery, 0.85f,  1, 0.0f, {} },
            { "NED",       M::arp, 4, 1, kEvery, 0.85f, -1, 0.0f, {} },
            { "UPP/NED",   M::arp, 4, 1, kEvery, 0.85f,  0, 0.0f, {} },
            { "TRIOL",     M::arp, 6, 1, kEvery, 0.85f,  1, 0.0f, {} } }} },

        // SE — one-beat cells of sixteenths, written as the J-6 writes them.
        // __o_ is the house offbeat stab.
        { "RYTM", {{
            { "o_o_",      M::pulse, 4, 1, steps ({ 0, 2 }),    0.90f, 0, 0.0f, {} },
            { "__o_",      M::pulse, 4, 1, steps ({ 2 }),       0.80f, 0, 0.0f, {} },
            { "oo_o",      M::pulse, 4, 1, steps ({ 0, 1, 3 }), 0.70f, 0, 0.0f, {} },
            { "o__o",      M::pulse, 4, 1, steps ({ 0, 3 }),    0.90f, 0, 0.0f, {} } }} },

        // S — two-beat syncopations: the tresillo that is under half of pop and
        // R&B, and three pushes on either side of it.
        { "SYNKOP", {{
            { "3-3-2",     M::pulse, 4, 2, steps ({ 0, 3, 6 }),    1.20f, 0, 0.0f, {} },
            { "3-2-3",     M::pulse, 4, 2, steps ({ 0, 3, 5 }),    1.00f, 0, 0.0f, {} },
            { "KNUFF",     M::pulse, 4, 2, steps ({ 1, 3, 6 }),    0.90f, 0, 0.0f, {} },
            { "STUDS",     M::pulse, 4, 2, steps ({ 0, 2, 5, 7 }), 0.80f, 0, 0.0f, {} } }} },

        // SW — the chord rolled rather than struck.
        { "STRUM", {{
            { "ENKEL",     M::strum, 1, 4, steps ({ 0 }),      3.80f, 0, 28.0f, {} },
            { "RYTMISK",   M::strum, 2, 2, steps ({ 0, 3 }),   1.40f, 0, 22.0f, {}, true },
            { "SNABB",     M::strum, 1, 1, steps ({ 0 }),      0.90f, 0, 12.0f, {} },
            { "LANGSAM",   M::strum, 1, 4, steps ({ 0 }),      3.80f, 0, 70.0f, {} } }} },

        // W — the trance gate: the chord chopped in time.
        { "GATE", {{
            { "1/16",      M::pulse, 4, 1, kEvery,                        0.45f, 0, 0.0f, {} },
            { "HACKA",     M::pulse, 4, 2, steps ({ 0, 1, 3, 4, 5, 7 }),  0.45f, 0, 0.0f, {} },
            { "1/32",      M::pulse, 8, 1, kEvery,                        0.45f, 0, 0.0f, {} },
            { "PUNKT",     M::pulse, 4, 4, steps ({ 0, 3, 6, 9, 12 }),    0.50f, 0, 0.0f, {} } }} },

        // NW — a line made of the chord's own tones. Indices count up the
        // voicing and carry on an octave higher past the top.
        { "FRAS", {{
            { "ENKEL",     M::melody, 2, 4, kEvery,              0.90f, 0, 0.0f, { 0, 1, 2, 3, 4, 3, 2, 1 } },
            { "RYTMISK",   M::melody, 4, 2, steps ({ 0, 3, 6 }), 1.30f, 0, 0.0f, { 3, 2, 4, 3, 2, 4, 3, 2 } },
            { "STIGANDE",  M::melody, 4, 1, kEvery,              0.90f, 0, 0.0f, { 0, 2, 1, 3, 2, 4, 3, 5 } },
            { "TOPP",      M::melody, 2, 2, steps ({ 0, 1, 3 }), 1.20f, 0, 0.0f, { 4, 3, 2, 4, 3, 2, 4, 3 } } }} }
    }};

    return styles[(size_t) (((style % kNumStyles) + kNumStyles) % kNumStyles)];
}

} // namespace chordtroller::dsp

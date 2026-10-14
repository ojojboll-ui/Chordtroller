#pragma once

#include "BowedString.h"
#include "../music/Scales.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace chordtroller::dsp
{

/**
    The fifth instrument: a bowed string, from violin to double bass.

    The left thumb is the left hand — it stops the string at a degree of the
    scale. The right thumb is the bow: how far the stick is pushed up or down is
    how fast the bow travels, and which way is up-bow or down-bow. Sideways is
    what a hand does on top of that, vibrato to the right and tremolo to the
    left. The right trigger is the index finger leaning into the bow, and the
    left trigger is how slowly the finger slides to the next note.

    Everything else is the physics in BowedString. Nothing here makes an
    envelope: a note starts because a bow starts moving across a string, and
    stops because the bow stops on it.
*/
inline constexpr int kNumStringInstruments = 4;

struct StringInstrument
{
    const char* name;
    int   openStrings[4];     // MIDI, lowest first
    float bowPoint;           // bow's distance from the bridge, as a fraction of the sounding length
    int   referenceString;    // the string the ring time is given for
    float ringSeconds;        // a free string at the reference, 60 dB down
    float lowpassHz;          // loss of the high partials at the bridge
    float bodyLow, bodyHigh;  // body mode scaling: the plate modes, and the bridge hill
    float trim;
};

inline const StringInstrument& stringInstrument (int index) noexcept
{
    static const std::array<StringInstrument, kNumStringInstruments> instruments
    {{
        { "FIOL",      { 55, 62, 69, 76 }, 0.10f, 1, 1.1f, 9000.0f, 1.00f, 1.00f, 1.07f },
        { "ALTFIOL",   { 48, 55, 62, 69 }, 0.10f, 1, 1.4f, 7500.0f, 0.80f, 0.88f, 1.43f },
        { "CELLO",     { 36, 43, 50, 57 }, 0.10f, 1, 2.4f, 5000.0f, 0.42f, 0.62f, 1.40f },
        { "KONTRABAS", { 28, 33, 38, 43 }, 0.10f, 1, 3.0f, 3200.0f, 0.27f, 0.48f, 0.83f }
    }};

    return instruments[(size_t) (((index % kNumStringInstruments) + kNumStringInstruments)
                                   % kNumStringInstruments)];
}

inline float midiToHz (float note) noexcept
{
    return 440.0f * std::exp2 ((note - 69.0f) / 12.0f);
}

/** Semitones above the key's root for a degree of the scale, counting on past
    the octave — degree seven of a seven-note scale is the octave, of a
    pentatonic it is the ninth. */
inline int semitonesOfDegree (std::uint16_t mask, int degree) noexcept
{
    int classes[12] {};
    int count = 0;

    for (int pc = 0; pc < 12; ++pc)
        if (((mask >> pc) & 1u) != 0u)
            classes[count++] = pc;

    if (count == 0)
        return degree;

    const int octave = degree >= 0 ? degree / count : -((-degree + count - 1) / count);
    return classes[degree - octave * count] + 12 * octave;
}

/** Where the stick's tonic sits: the key's root at or above the instrument's
    lowest open string, moved by whole octaves of register. */
inline int stringTonic (int instrument, int keyRoot, int registerShift) noexcept
{
    const int lowest = stringInstrument (instrument).openStrings[0];
    return lowest + ((keyRoot - lowest) % 12 + 12) % 12 + 12 * registerShift;
}

inline int stringNoteFor (int instrument, int keyRoot, int registerShift,
                          int scaleIndex, int degree, int octave) noexcept
{
    return stringTonic (instrument, keyRoot, registerShift)
         + semitonesOfDegree (music::scaleAt (scaleIndex).mask, degree) + 12 * octave;
}

/** The open string a player would stop this note on: the highest one at or
    below it. */
inline int openStringFor (int instrument, float note) noexcept
{
    const auto& s = stringInstrument (instrument);
    int open = s.openStrings[0];

    for (const int o : s.openStrings)
        if ((float) o <= note + 0.01f)
            open = o;

    return open;
}

//==============================================================================

/**
    The wooden box. A bank of resonances standing in for the plates and the air
    inside, plus a direct path for everything they do not colour.

    The mode table is a violin's: the air mode A0, the corpus bending mode, the
    two strong plate modes B1- and B1+ either side of 500 Hz, a scatter above,
    and the broad "bridge hill" round 2.5 kHz that makes a violin carry. The
    larger instruments scale the plate modes down with their size and the hill
    down more gently, which is roughly what measured bodies do.

    The sordino is a weight clamped on the bridge. It damps the bridge hill and
    everything above it, which is the whole of its sound.
*/
class StringBody
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = (float) sampleRateIn;
        configure (0, false);
        reset();
    }

    void reset() noexcept
    {
        for (auto& m : modes)
            m.z1 = m.z2 = 0.0f;

        hp1 = hp2 = hpOut1 = hpOut2 = 0.0f;
        lowpass = 0.0f;
    }

    void configure (int instrument, bool sordino) noexcept
    {
        struct Mode { float hz, q, gain; bool hill; };

        static constexpr Mode violin[kModes]
        {
            {  275.0f, 14.0f, 0.90f, false },     // A0, the air in the box
            {  405.0f, 18.0f, 0.30f, false },     // corpus bending
            {  460.0f, 22.0f, 1.10f, false },     // B1-
            {  540.0f, 22.0f, 1.00f, false },     // B1+
            {  700.0f, 16.0f, 0.45f, false },
            {  860.0f, 16.0f, 0.40f, false },
            { 1050.0f, 14.0f, 0.45f, false },
            { 1320.0f, 12.0f, 0.40f, false },
            { 1650.0f, 10.0f, 0.40f, true  },
            { 2100.0f,  6.0f, 0.60f, true  },     // the bridge hill
            { 2650.0f,  6.0f, 0.70f, true  },
            { 3300.0f,  6.0f, 0.45f, true  },
            { 4600.0f,  4.0f, 0.22f, true  }
        };

        const auto& s = stringInstrument (instrument);

        for (int i = 0; i < kModes; ++i)
        {
            const auto& m = violin[i];

            float hz   = m.hz * (m.hill ? s.bodyHigh : s.bodyLow);
            float gain = m.gain;

            if (sordino && m.hill)
            {
                hz   *= 0.9f;
                gain *= 0.3f;
            }

            setMode (modes[(size_t) i], hz, m.q, gain);
        }

        // Below A0 a box radiates little, so the direct path is highpassed just
        // under it — the violin's G is weaker than its body, and that is right.
        setHighpass (190.0f * s.bodyLow);

        const float topHz = (sordino ? 3500.0f : 7500.0f) * s.bodyHigh;
        lowpassCoef = 1.0f - std::exp (-2.0f * kPi * std::min (topHz, 0.45f * sampleRate) / sampleRate);
    }

    float process (float x) noexcept
    {
        // Direct path: second-order highpass.
        const float direct = hpB0 * x + hpB1 * hp1 + hpB2 * hp2 - hpA1 * hpOut1 - hpA2 * hpOut2;
        hp2 = hp1; hp1 = x;
        hpOut2 = hpOut1; hpOut1 = direct;

        float sum = direct * kDirect;

        for (auto& m : modes)
        {
            const float y = m.b0 * x + m.z1;
            m.z1 = -m.a1 * y + m.z2;
            m.z2 = m.b2 * x - m.a2 * y;
            sum += y * m.gain;
        }

        lowpass += (sum - lowpass) * lowpassCoef;
        return lowpass;
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr int   kModes = 13;
    static constexpr float kDirect = 0.35f;

    struct Resonator
    {
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, gain = 0;
        float z1 = 0, z2 = 0;
    };

    /** A constant-peak bandpass: at its centre it passes the input at `gain`. */
    void setMode (Resonator& r, float hz, float q, float gain) noexcept
    {
        hz = std::min (hz, 0.45f * sampleRate);

        const float w     = 2.0f * kPi * hz / sampleRate;
        const float alpha = std::sin (w) / (2.0f * q);
        const float a0    = 1.0f + alpha;

        r.b0 = alpha / a0;
        r.b2 = -alpha / a0;
        r.a1 = -2.0f * std::cos (w) / a0;
        r.a2 = (1.0f - alpha) / a0;
        r.gain = gain;
    }

    void setHighpass (float hz) noexcept
    {
        const float w     = 2.0f * kPi * hz / sampleRate;
        const float alpha = std::sin (w) / (2.0f * 0.7071f);
        const float cw    = std::cos (w);
        const float a0    = 1.0f + alpha;

        hpB0 = (1.0f + cw) * 0.5f / a0;
        hpB1 = -(1.0f + cw) / a0;
        hpB2 = hpB0;
        hpA1 = -2.0f * cw / a0;
        hpA2 = (1.0f - alpha) / a0;
    }

    float sampleRate = 48000.0f;
    std::array<Resonator, kModes> modes {};

    float hpB0 = 1, hpB1 = 0, hpB2 = 0, hpA1 = 0, hpA2 = 0;
    float hp1 = 0, hp2 = 0, hpOut1 = 0, hpOut2 = 0;
    float lowpass = 0.0f, lowpassCoef = 0.5f;
};

//==============================================================================

class StringEngine
{
public:
    static constexpr int kPlayers = 4;

    /** Bow speed at full stick, in metres a second — a fast, full bow. */
    static constexpr float kMaxBowSpeed = 0.5f;

    /**
        The trigger's bow force, as a fraction of the most the hair can hold at
        this speed and contact point — Schelleng's upper limit, F/2Z = vb/(βΔμ).

        Measured on all four instruments across their range (the offline test's
        map): a clean Helmholtz tone from about 0.3 to 0.7 of the limit; below
        it the string slips twice a period, the airy surface sound of a bow too
        light to grip; from about 0.9 up it cannot let go. So the trigger is laid
        over the regions rather than spread evenly: the first quarter of its
        travel is the surface sound, the middle half is the tone, and the last
        quarter leans on a little further while the rasp comes in — see
        kRattleHz. Between the knots the steps are equal ratios, which is how
        pressure is heard.

        The last quarter used to push on past the limit into the real thing,
        and the real thing is chaos: it never repeats, and through a speaker it
        is crackle, not a sound anyone plays with.
    */
    static constexpr float kForceKnots[4] { 0.12f, 0.30f, 0.68f, 0.78f };   // at 0, 1/4, 3/4 and 1

    /** Vibrato width at full lean, and where it starts just past the
        deadzone. In between the steps are equal ratios: a vibrato is heard as
        how many times wider it got, so a linear stick spent half its throw on
        widths that all sounded wide. */
    static constexpr float kVibratoCents    = 32.0f;
    static constexpr float kVibratoMinCents = 3.0f;

    /** The bow's speed range, top to just past the deadzone. Loudness goes
        with bow speed, so this is also the dynamic range of the stick, and it
        is laid out in decibels. */
    static constexpr float kBowRangeDb = 30.0f;

    /** Equal ratios from `low` at 0 to `high` at 1. */
    static float ratioCurve (float low, float high, float x) noexcept
    {
        return low * std::pow (high / low, std::clamp (x, 0.0f, 1.0f));
    }

    /** The same, but starting from nothing: a lean just out of the deadzone
        fades in over the first twentieth rather than jumping to `low`. */
    static float ratioCurveFromZero (float low, float high, float x) noexcept
    {
        return x <= 0.0f ? 0.0f : ratioCurve (low, high, x) * std::min (1.0f, x / 0.05f);
    }

    void prepare (double sampleRateIn)
    {
        sampleRate = (float) sampleRateIn;

        for (auto& p : players)
        {
            p.melody.prepare (sampleRateIn * kOversample);
            p.drone.prepare (sampleRateIn * kOversample);
        }

        bodyLeft.prepare (sampleRateIn);
        bodyRight.prepare (sampleRateIn);

        setInstrument (instrumentIndex);
        allOff();

        noteNow = noteTarget;
        droneNow = droneTarget;
        setGlide (0.0f);

        // Smoothing for what the pad sets once a block.
        controlCoef = 1.0f - std::exp (-1.0f / (0.006f * sampleRate));
        restCoef    = 1.0f - std::exp (-1.0f / (0.015f * sampleRate));
        followCoef  = 1.0f - std::exp (-1.0f / (0.080f * sampleRate));
        levelFall   = std::exp (-1.0f / (0.25f * sampleRate));
        dcCoef      = 1.0f - 2.0f * 3.14159265f * 12.0f / sampleRate;

        rebuildPlayers();
    }

    void seed (std::uint32_t s) noexcept
    {
        random = s != 0u ? s : 0x9e3779b9u;
        rebuildPlayers();
    }

    //== what the pad sets, once a block ======================================

    void setInstrument (int index) noexcept
    {
        instrumentIndex = ((index % kNumStringInstruments) + kNumStringInstruments) % kNumStringInstruments;
        bodyLeft.configure (instrumentIndex, sordino);
        bodyRight.configure (instrumentIndex, sordino);
    }

    int instrument() const noexcept { return instrumentIndex; }

    void setSordino (bool on) noexcept
    {
        sordino = on;
        setInstrument (instrumentIndex);
    }

    bool hasSordino() const noexcept { return sordino; }

    void setSection (bool on) noexcept { section = on; }
    bool isSection() const noexcept { return section; }

    void setPizzicato (bool on) noexcept { pizzicato = on; }
    bool isPizzicato() const noexcept { return pizzicato; }

    /** The stopped note, in MIDI, before vibrato. The glide chases it. */
    void setNote (float midi) noexcept { noteTarget = midi; }

    /** Left trigger: 0 is a finger that lands, 1 a slow slide. */
    void setGlide (float amount) noexcept
    {
        glideAmount = std::clamp (amount, 0.0f, 1.0f);
        const float seconds = glideSeconds();
        glideCoef = 1.0f - std::exp (-1.0f / (seconds * sampleRate));
    }

    /** Right stick up and down, after the deadzone: up-bow is positive. */
    void setBow (float stickY) noexcept { bowStick = std::clamp (stickY, -1.0f, 1.0f); }

    /** Right stick sideways: right is vibrato, left is tremolo. */
    void setHand (float stickX) noexcept
    {
        vibratoTarget = std::max (0.0f, std::min (1.0f, stickX));
        tremoloTarget = std::max (0.0f, std::min (1.0f, -stickX));
    }

    /** Right trigger, 0 to 1. */
    void setPressure (float amount) noexcept
    {
        pressureAmount = std::clamp (amount, 0.0f, 1.0f);
        relativeForce = forceAt (pressureAmount);
        skorr = std::clamp ((pressureAmount - 0.75f) / 0.25f, 0.0f, 1.0f);
    }

    /** A second string held at one note, bowed or plucked with the first. */
    void setDrone (bool on, float midi) noexcept
    {
        droneOn = on;

        if (on)
            droneTarget = midi;
    }

    bool hasDrone() const noexcept { return droneOn; }
    float droneNote() const noexcept { return droneTarget; }

    /** A finger pluck in pizzicato. Strength 0 to 1. Each player of a section
        comes in a little apart. */
    void pluck (float strength) noexcept
    {
        strength = std::clamp (strength, 0.05f, 1.0f);

        for (int i = 0; i < activePlayers(); ++i)
        {
            auto& p = players[(size_t) i];
            p.pluckStrength = strength * p.speedScale;
            p.pluckIn = (int) (p.pluckDelay * sampleRate);
        }
    }

    /** Silence, now: the pad let go of the instrument. */
    void allOff() noexcept
    {
        for (auto& p : players)
        {
            p.melody.reset();
            p.drone.reset();
            p.steady = p.tremolo = 0.0f;
            p.pluckIn = -1;
        }

        bodyLeft.reset();
        bodyRight.reset();
        dcLeft = dcRight = dcInLeft = dcInRight = 0.0f;
        level = 0.0f;
    }

    //== what the screen and the MIDI read =====================================

    float soundingNote() const noexcept { return noteNow; }
    float targetNote()   const noexcept { return noteTarget; }
    float bowSpeed()     const noexcept { return bowTargetSpeed; }             // 0..1 of full
    float bowDirection() const noexcept { return bowStick >= 0.0f ? 1.0f : -1.0f; }
    float vibrato()      const noexcept { return vibratoTarget; }
    float tremolo()      const noexcept { return tremoloTarget; }
    float pressure()     const noexcept { return pressureAmount; }
    float forceRatio()   const noexcept { return relativeForce; }
    float glide()        const noexcept { return glideAmount; }

    /** Three milliseconds, a finger landing, to a quarter-second slide, in
        equal ratios. */
    float glideSeconds() const noexcept { return ratioCurve (0.003f, 0.25f, glideAmount); }
    float outputLevel()  const noexcept { return level; }
    float bowPoint()     const noexcept { return players[0].melody.beta(); }

    /** The soloist's string, for tests that need to see the stick and slip. */
    const BowedString& soloString() const noexcept { return players[0].melody; }

    /** Is a bow moving on the strings — what a note-on means here. */
    bool isBowing() const noexcept { return ! pizzicato && bowTargetSpeed > 0.0f; }

    /** How many plucks the tremolo made this block, for the MIDI. */
    int autoPlucksThisBlock() const noexcept { return autoPlucks; }

    //== audio =================================================================

    /** Once a block, after the controls: the per-note losses, and the bow
        decision that is not worth making per sample. */
    void beginBlock() noexcept
    {
        autoPlucks = 0;

        const auto& inst = stringInstrument (instrumentIndex);

        // How fast, from how far out. With tremolo on the left the stick's
        // sideways lean is also a bow speed, so tremolo plays with nothing
        // pushed up or down.
        const float lean  = std::abs (bowStick);
        const float drive = std::max (lean, tremoloTarget);

        bowTargetSpeed = drive > 0.0f
                           ? std::pow (10.0f, (drive - 1.0f) * kBowRangeDb / 20.0f) * std::min (1.0f, drive / 0.05f)
                           : 0.0f;

        // Tremolo takes over the bow's direction as it comes in.
        const float w = std::min (1.0f, tremoloTarget * 3.0f);
        steadyTarget  = bowTargetSpeed * kMaxBowSpeed * (1.0f - w) * (lean > 0.0f ? bowDirection() : 0.0f);

        // A new stroke: from rest, or a change of bow. The force starts from
        // the middle of the clean window and leans in from there, and the rasp
        // waits for it — see kSettlePeriods.
        const int stroke = steadyTarget > 0.0f ? 1 : steadyTarget < 0.0f ? -1 : 0;

        if (stroke != 0 && stroke != lastStroke)
            settle = 0.0f;

        lastStroke = stroke;
        settleCoef = 1.0f - std::exp (-1.0f / (kSettlePeriods / std::max (midiToHz (noteTarget), 20.0f) * sampleRate));
        tremoloAmp    = bowTargetSpeed * kMaxBowSpeed * w;
        tremoloRate   = ratioCurve (3.5f, 8.0f, tremoloTarget);     // 7 to 16 strokes a second

        // The bow resting on the string. It takes the same share of the wave
        // each time the wave passes, which is once a period, so a fixed share
        // stops a violin in a quarter of a second and a bass in one and a half.
        // Scaled by the period instead, every string stops in about the same
        // time — a bow stopped on a string silences it, whatever the string.
        restTarget = (! pizzicato && bowTargetSpeed <= 0.0f)
                       ? std::clamp (kRestDampingHz / midiToHz (noteTarget), 0.02f, 0.45f)
                       : 0.0f;

        // The bow sits at the same fraction of whatever length is sounding. A
        // fixed distance from the bridge would put it at a twentieth of a low
        // open string, where the window between too light and too heavy is so
        // narrow the model spends most of its time outside it — Fmin/Fmax
        // goes as 1/β. Players move the bow for the same reason.
        bowPosition = inst.bowPoint;

        const float hz  = midiToHz (noteTarget);
        const float dhz = midiToHz (droneTarget);

        rattleHz = hz / (float) std::max (1, (int) std::lround (hz / kRattleHz));

        for (int i = 0; i < kPlayers; ++i)
            players[(size_t) i].bowCoef = 1.0f - std::exp (-1.0f / ((kBowLag[i] + kAttackPeriods / hz) * sampleRate));

        for (auto& p : players)
        {
            p.melody.setLosses (hz,  ringFor (hz),  std::min (inst.lowpassHz, kCornerHarmonics * hz));
            p.drone .setLosses (dhz, ringFor (dhz), std::min (inst.lowpassHz, kCornerHarmonics * dhz));
        }
    }

    void processSample (float& outLeft, float& outRight) noexcept
    {
        noteNow  += (noteTarget - noteNow) * glideCoef;
        droneNow += (droneTarget - droneNow) * 0.02f;

        vibratoNow += (vibratoTarget - vibratoNow) * controlCoef;
        restNow    += (restTarget - restNow) * restCoef;

        const float vibratoCents = ratioCurveFromZero (kVibratoMinCents, kVibratoCents, vibratoNow);
        const float vibratoHz    = ratioCurve (5.2f, 6.4f, vibratoNow);

        settle += (1.0f - settle) * settleCoef;

        const float strokeForce = relativeForce <= kStrokeStartForce
                                    ? relativeForce
                                    : kStrokeStartForce + (relativeForce - kStrokeStartForce) * settle;

        const int count = activePlayers();
        float left = 0.0f, right = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            auto& p = players[(size_t) i];

            // The hand: vibrato, and a slow drift that keeps a section from
            // ever being one instrument played four times.
            p.vibratoPhase += vibratoHz * p.rateScale / sampleRate;
            if (p.vibratoPhase >= 1.0f) p.vibratoPhase -= 1.0f;

            p.driftPhase += p.driftHz / sampleRate;
            if (p.driftPhase >= 1.0f) p.driftPhase -= 1.0f;

            const float cents = p.detuneCents * (section ? 1.0f : 0.0f)
                              + vibratoCents * std::sin (kTwoPi * p.vibratoPhase)
                              + (section ? kDriftCents * std::sin (kTwoPi * p.driftPhase) : 0.0f);

            p.melody.setTuning (midiToHz (noteNow + cents * 0.01f), bowPosition);
            p.drone .setTuning (midiToHz (droneNow + (section ? p.detuneCents * 0.01f : 0.0f)), bowPosition);

            // The bow, each player a little behind or ahead of the others.
            p.steady  += (steadyTarget * p.speedScale - p.steady)  * p.bowCoef;
            p.tremolo += (tremoloAmp   * p.speedScale - p.tremolo) * p.bowCoef;

            float velocity = p.steady;

            if (p.tremolo > 1.0e-5f || tremoloAmp > 0.0f)
            {
                const float before = p.tremoloPhase;
                p.tremoloPhase += tremoloRate * p.rateScale / sampleRate;
                if (p.tremoloPhase >= 1.0f) p.tremoloPhase -= 1.0f;

                // Pizzicato tremolo: a pluck on every stroke, mandolin style.
                if (pizzicato && tremoloAmp > 0.0f
                    && (p.tremoloPhase < before || (before < 0.5f && p.tremoloPhase >= 0.5f)))
                {
                    p.pluckStrength = (0.3f + 0.5f * tremoloTarget) * p.speedScale;
                    p.pluckIn = 0;

                    if (i == 0)
                        ++autoPlucks;
                }

                velocity += p.tremolo * kTremoloShapeNorm
                              * std::tanh (2.5f * std::sin (kTwoPi * p.tremoloPhase));
            }

            if (pizzicato)
                velocity = 0.0f;

            // Force as a fraction of the most the hair can hold: F/2Z at
            // Schelleng's upper limit is vb / (β Δμ). Taken from the speed the
            // stick asks for, not the speed the bow has reached, so the weight
            // is on the string before the bow gets going — a player sets the
            // bow down and then draws it. Force that grows with the bow's own
            // speed starts every note with no grip at all against an
            // accelerating bow, which is Guettler's recipe for a multiple-slip
            // attack.
            const float beta  = p.melody.beta();
            const float force = strokeForce * p.forceScale * p.speedScale
                                * (std::abs (steadyTarget) + tremoloAmp)
                                / (beta * BowedString::kFrictionSpan);

            // Rosin: a little grit on the friction, lowpassed white noise.
            p.grain += (nextNoise() * kGrain - p.grain) * 0.15f;

            if (p.pluckIn == 0)
            {
                const int width = (int) (sampleRate * kOversample * (0.0028f - 0.0018f * p.pluckStrength));
                const float pluckGain = kPluckLevel * std::pow (10.0f, (p.pluckStrength - 1.0f) * kPluckRangeDb / 20.0f);

                p.melody.pluck (pluckGain, width);

                if (droneOn)
                    p.drone.pluck (pluckGain * 0.8f, width);
            }

            if (p.pluckIn >= 0)
                --p.pluckIn;

            // Twice per output sample, averaged down: see kOversample.
            const bool  droneBowed = droneOn && ! pizzicato;
            const float droneV     = droneBowed ? velocity : 0.0f;
            const float droneF     = droneBowed ? force : 0.0f;
            const float droneRest  = droneOn ? restNow : kDroneLetGo;

            float s = 0.0f;

            for (int k = 0; k < kOversample; ++k)
            {
                s += p.melody.process (velocity, force, restNow, p.grain);
                s += p.drone.process (droneV, droneF, droneRest, p.grain);
            }

            s *= 1.0f / (float) kOversample;

            // The rasp. A slow follower keeps the drive the same whatever the
            // bow speed, so it is the trigger and not the loudness that decides
            // how much it rasps.
            p.follow += (std::abs (s) - p.follow) * followCoef;

            if (skorr > 0.0f)
            {
                p.rattlePhase += rattleHz / sampleRate;
                if (p.rattlePhase >= 1.0f) p.rattlePhase -= 1.0f;

                const float pulse = 0.5f + 0.5f * std::cos (kTwoPi * p.rattlePhase);
                const float amount = skorr * settle * ((1.0f - kRattleDepth) + kRattleDepth * pulse);
                const float scale  = std::max (p.follow, 1.0e-4f) * 1.6f;
                const float x      = s / scale * (1.0f + kRaspDrive * amount);

                // Asymmetric: the bias gives the even harmonics of a buzz.
                const float shaped = (std::tanh (x + kBuzzBias) - std::tanh (kBuzzBias)) * scale;

                s += amount * (shaped - s);
            }

            // A soloist stands in the middle; in a section the first chair
            // takes its own place like the others.
            left  += s * (section ? p.panLeft  : 1.0f);
            right += s * (section ? p.panRight : 1.0f);
        }

        const auto& inst = stringInstrument (instrumentIndex);
        const float gain = inst.trim * kOutput * (section ? kSectionTrim : 1.0f);

        float l, r;

        if (section)
        {
            l = bodyLeft.process (left);
            r = bodyRight.process (right);
        }
        else
        {
            l = r = bodyLeft.process (left);
        }

        // The loop carries a little DC after a pluck; the body's highpassed
        // direct path does not remove what the resonators pass.
        const float dl = l - dcInLeft + dcCoef * dcLeft;
        dcInLeft = l; dcLeft = dl;
        const float dr = r - dcInRight + dcCoef * dcRight;
        dcInRight = r; dcRight = dr;

        outLeft  = softCeiling (dl * gain);
        outRight = softCeiling (dr * gain);

        const float peak = std::max (std::abs (outLeft), std::abs (outRight));
        level = peak > level ? peak : level * levelFall;
    }

private:
    static constexpr float kTwoPi = 6.28318530717959f;
    static constexpr float kRestDampingHz = 22.0f;   // share of the wave taken per pass, times the pitch
    static constexpr float kDroneLetGo  = 0.3f;
    static constexpr float kDriftCents  = 2.0f;
    static constexpr float kGrain       = 0.02f;

    /**
        The strings run at twice the host's rate.

        The bow can only let go of the string on a sample, so a period of 129.7
        samples slips after 130, 130, 129 and round again. That fixed pattern is
        a modulation, and it put sidebands 165 Hz either side of a slowly bowed
        G4 only 9 dB under the tone — a rasp no bow makes. At twice the rate the
        same measurement is 30 dB down. The body and everything after it stay
        at the host's rate; the loop's own lowpass keeps what the average folds
        back negligible.
    */
    static constexpr int kOversample = 2;

    /**
        Tuned against the offline test's map and attack times, and each one is
        there for a reason the physics gives:

        - The bowed string loses a fixed small fraction per period, not per
          second. Schelleng's lower limit rises with the loss per period, so a
          ring time that suits a pizzicato gave the bass's open E five times
          the loss of the violin's top — and a window too narrow to play in.
          A plucked string rings for its own time; a bowed one is stopped by
          the resting bow anyway.
        - The high partials go at a fixed multiple of the fundamental. That
          rounds the Helmholtz corner by the same share of a period on every
          note, so the ripples it leaves behind — which knock the string loose
          in the sticking phase — are as tame on a bass as on a violin.
        - The bow comes up to speed over a few periods, not a fixed time. Ten
          milliseconds is a stroke for a violin and a stab for a bass, and a
          stab starts in chaos.
        - Below 200 Hz the loss per period grows in proportion, so the
          irregular start a low string always makes dies away in about the same
          time as a high one's rather than the same number of periods — which
          on a bass was half a second.

        Tried and left out, both measured worse: extra force while the bow is
        still accelerating (Guettler's "bite"), and a bow-point damper for the
        ripples.
    */
    static constexpr float kLossPerPeriod   = 0.003f;   // log10 of the loop's gain, arco
    static constexpr float kLowStringHz     = 200.0f;   // below this the loss per period grows
    static constexpr float kCornerHarmonics = 12.0f;
    static constexpr float kAttackPeriods   = 4.0f;
    static constexpr float kPluckLevel  = 1.2f;
    static constexpr float kPluckRangeDb = 24.0f;   // softest pluck to hardest
    static constexpr float kOutput      = 0.21f;
    static constexpr float kSectionTrim = 0.55f;
    static constexpr float kTremoloShapeNorm = 1.0f / 0.98661f;   // tanh(2.5)

    static constexpr float kBowLag[kPlayers] { 0.010f, 0.024f, 0.036f, 0.017f };

    struct Player
    {
        BowedString melody, drone;

        float detuneCents = 0.0f, driftHz = 0.1f, rateScale = 1.0f;
        float speedScale = 1.0f, forceScale = 1.0f, bowCoef = 0.01f, pluckDelay = 0.0f;
        float panLeft = 1.0f, panRight = 1.0f;

        float vibratoPhase = 0.0f, driftPhase = 0.0f, tremoloPhase = 0.0f;
        float steady = 0.0f, tremolo = 0.0f, grain = 0.0f;

        float pluckStrength = 0.0f;
        int   pluckIn = -1;
        float rattlePhase = 0.0f, follow = 0.0f;
    };

    int activePlayers() const noexcept { return section ? kPlayers : 1; }

    /** Untouched below 0.7, then bent towards 1. Only a full bow crunching
        on the lowest string gets there, and a crunch that clips in the host
        is not the crunch anyone asked for. */
    static float softCeiling (float x) noexcept
    {
        const float a = std::abs (x);

        if (a <= 0.7f)
            return x;

        return std::copysign (0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f), x);
    }

    static float forceAt (float amount) noexcept
    {
        const float x = std::clamp (amount, 0.0f, 1.0f);

        auto between = [] (float a, float b, float t) { return a * std::pow (b / a, t); };

        if (x < 0.25f) return between (kForceKnots[0], kForceKnots[1], x / 0.25f);
        if (x < 0.75f) return between (kForceKnots[1], kForceKnots[2], (x - 0.25f) / 0.5f);
        return between (kForceKnots[2], kForceKnots[3], (x - 0.75f) / 0.25f);
    }

    /** How long the string rings: bowed, the small fixed loss per period; plucked,
        a free string's own time, longer low than high. */
    float ringFor (float hz) const noexcept
    {
        if (! pizzicato)
            return 3.0f / (std::max (hz, 20.0f) * kLossPerPeriod * std::max (1.0f, kLowStringHz / hz));

        const auto& inst = stringInstrument (instrumentIndex);
        const float reference = midiToHz ((float) inst.openStrings[inst.referenceString]);
        return std::clamp (inst.ringSeconds * std::sqrt (reference / hz), 0.3f, 6.0f);
    }

    float nextNoise() noexcept
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return (float) (random >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    /** Four players, each with the small differences that make them a section.
        The first is also the soloist, so it sits in the middle with none. */
    void rebuildPlayers() noexcept
    {
        static constexpr float detune[kPlayers] { 0.0f, 6.0f, -5.0f, 3.0f };
        static constexpr float drift [kPlayers] { 0.07f, 0.11f, 0.09f, 0.13f };
        static constexpr float rate  [kPlayers] { 1.00f, 1.08f, 0.93f, 1.04f };
        static constexpr float speed [kPlayers] { 1.00f, 0.93f, 1.06f, 0.97f };
        static constexpr float force [kPlayers] { 1.00f, 1.08f, 0.92f, 1.03f };
        static constexpr float delay [kPlayers] { 0.000f, 0.011f, 0.023f, 0.006f };
        static constexpr float pan   [kPlayers] { -0.2f, 0.6f, -0.6f, 0.2f };

        for (int i = 0; i < kPlayers; ++i)
        {
            auto& p = players[(size_t) i];

            p.detuneCents = detune[i];
            p.driftHz     = drift[i];
            p.rateScale   = rate[i];
            p.speedScale  = speed[i];
            p.forceScale  = force[i];
            p.pluckDelay  = delay[i];
            p.bowCoef     = 1.0f - std::exp (-1.0f / (kBowLag[i] * sampleRate));

            // Equal power. The soloist is in the middle and sums to the same
            // level in mono as a section player does panned.
            const float angle = (pan[i] + 1.0f) * 0.25f * 3.14159265f;
            p.panLeft  = std::cos (angle) * 1.41421356f;
            p.panRight = std::sin (angle) * 1.41421356f;

            p.vibratoPhase = (nextNoise() + 1.0f) * 0.5f;
            p.driftPhase   = (nextNoise() + 1.0f) * 0.5f;
            p.tremoloPhase = (nextNoise() + 1.0f) * 0.5f;
        }

        players[0].vibratoPhase = players[0].tremoloPhase = 0.0f;
    }

    std::array<Player, kPlayers> players;
    StringBody bodyLeft, bodyRight;

    float sampleRate = 48000.0f;
    int   instrumentIndex = 0;
    bool  sordino = false, section = false, pizzicato = false, droneOn = false;

    float noteTarget = 62.0f, noteNow = 62.0f;
    float droneTarget = 55.0f, droneNow = 55.0f;
    float glideAmount = 0.0f, glideCoef = 1.0f;

    float bowStick = 0.0f, bowTargetSpeed = 0.0f;
    float steadyTarget = 0.0f, tremoloAmp = 0.0f, tremoloRate = 5.0f;
    float vibratoTarget = 0.0f, vibratoNow = 0.0f, tremoloTarget = 0.0f;
    float pressureAmount = 0.0f, relativeForce = kForceKnots[0];
    float skorr = 0.0f, rattleHz = 30.0f, followCoef = 0.001f;
    /**
        The rasp in the last quarter of RT.

        Tried first inside the physics, twice: bursts of force over the limit
        at a rattle rate, and a bow that judders in speed. Both measured as noise
        — every jolt starts a fresh bowed attack, and bowed attacks are chaotic
        and never the same twice. So the string is left in its clean tone and
        the rasp is put on what it sends to the body: an asymmetric saturation,
        the buzz of something loose touching the string, pulsing at a rate near
        28 Hz that divides the pitch — a rolled R. A fixed curve over a periodic
        tone with a periodic pulse is periodic itself, so it rasps with a pitch
        instead of crackling, and the body still colours it.
    */
    /**
        Every stroke starts at no more than a third of the limit and leans in
        over thirty periods, and the rasp comes in with it.

        With RT already down when the bow started, or at a change of bow, the
        full force met a bow barely moving — Guettler's choked attack — and the
        string sat in chaos for most of a second, the rasp turning that into
        exactly the crackle it was built to replace. A slow sweep of RT on a
        sounding note had never shown it, which is why the first listening
        files sounded right and the instrument did not. Measured through the
        engine: a violin A4 from rest with RT down went from 0.25 periodic in
        its first fifth of a second to 0.72, and 0.47 to 0.96 after it; a bass
        A1 from 0.08 to 0.87. Players do the same: the weight goes on once the
        string has caught.
    */
    static constexpr float kStrokeStartForce = 0.35f;
    static constexpr float kSettlePeriods    = 30.0f;

    static constexpr float kRattleHz    = 28.0f;
    static constexpr float kRattleDepth = 0.6f;   // how much of the rasp comes and goes with the rattle
    static constexpr float kRaspDrive   = 4.0f;
    static constexpr float kBuzzBias    = 0.4f;
    float restTarget = 0.0f, restNow = 0.0f, bowPosition = 0.1f;
    float settle = 1.0f, settleCoef = 0.001f;
    int   lastStroke = 0;

    float controlCoef = 0.01f, restCoef = 0.01f, levelFall = 0.999f, level = 0.0f;
    float dcCoef = 0.999f, dcLeft = 0.0f, dcRight = 0.0f, dcInLeft = 0.0f, dcInRight = 0.0f;

    int autoPlucks = 0;
    std::uint32_t random = 0x2545f491u;
};

} // namespace chordtroller::dsp

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace chordtroller::dsp
{

/**
    A delay line read at a fractional position, with four-point Lagrange
    interpolation.

    Linear interpolation would do for a fixed pitch, but a string whose length
    is moved by a vibrato has its read point sweeping across samples all the
    time, and a linear read is a lowpass whose depth depends on where between two
    samples it lands. That is an amplitude wobble on every high partial at the
    vibrato's rate. Lagrange is flat enough that a vibrato is heard as pitch.
*/
class FractionalDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 16;

        while (size < maxDelaySamples + 8)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    }

    void push (float x) noexcept
    {
        buffer[(size_t) write] = x;
        write = (write + 1) & mask;
    }

    /** The value pushed `delay` samples ago. One is the last push. Two is the
        shortest delay the interpolation can read, since it needs a point on
        each side. */
    float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 2.0f, (float) (mask - 4));

        const int   whole = (int) delay;
        const float f     = delay - (float) whole;

        auto at = [this] (int d) { return buffer[(size_t) ((write - d) & mask)]; };

        const float ym1 = at (whole - 1);
        const float y0  = at (whole);
        const float y1  = at (whole + 1);
        const float y2  = at (whole + 2);

        const float fm1 = f + 1.0f, f1 = f - 1.0f, f2 = f - 2.0f;

        return ym1 * (-f * f1 * f2 / 6.0f)
             + y0  * (fm1 * f1 * f2 * 0.5f)
             + y1  * (-fm1 * f * f2 * 0.5f)
             + y2  * (fm1 * f * f1 / 6.0f);
    }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0;
};

/**
    One bowed string: a digital waveguide with the bow as a friction junction.

    The string is a loop of travelling waves split in two where the bow sits.
    One line is the round trip from the bow to the finger and back, the other the
    round trip to the bridge and back. The finger and the bridge both reflect
    with a sign flip, and the bridge takes the losses — it is where the energy
    leaves the string for the body anyway.

    The bow is not a signal generator. It is a friction law, solved every sample
    against the waves arriving at it, and everything a string player recognises
    comes out of that: the stick and slip of the Helmholtz motion, the hiss
    when the pressure is too light for the bow to grip, the crunch when it is too
    heavy to let go, and the scratch on a careless attack. None of those are
    programmed. They are what the same equation does with other numbers.

    The friction curve is the hyperbolic one of Smith and Woodhouse: static
    friction μs while the hair holds the string, falling towards μd as the
    string slides faster past it. With it, the velocity the string ends up with
    is the root of a quadratic, so the junction is solved exactly rather than
    iterated. Where sticking and slipping are both possible, the string stays in
    whichever state it was in — the hysteresis that makes the slip snap.

    Units are the physics' own with the string's impedance set to one: bow
    speed in metres a second, and the bow's force as the velocity it could
    impose, F / 2Z.
*/
class BowedString
{
public:
    static constexpr float kStaticFriction  = 0.8f;
    static constexpr float kDynamicFriction = 0.3f;
    static constexpr float kFrictionSpan    = kStaticFriction - kDynamicFriction;
    static constexpr float kSlipKnee        = 0.08f;   // v0: how fast the grip falls off

    void prepare (double sampleRateIn)
    {
        sampleRate = (float) sampleRateIn;

        // The longest string any register can ask for, at twenty hertz.
        const int longest = (int) (sampleRateIn / 20.0) + 8;

        toFinger.prepare (longest);
        toBridge.prepare (longest);

        reset();
    }

    void reset() noexcept
    {
        toFinger.clear();
        toBridge.clear();
        bridgeLowpass = 0.0f;
        sticking = false;
        pulseLeft = 0;
    }

    /**
        Losses, once a block, for the pitch about to be played.

        `ringSeconds` is how long the free string takes to fall by 60 dB — a
        pizzicato's length, and what is left after the bow lifts. The loop gain
        per round trip follows from it and the period, so a high note does not
        die sooner merely for taking more trips. `lowpassHz` is how fast the high
        partials go, relative to the fundamental.
    */
    void setLosses (float hz, float ringSeconds, float lowpassHz) noexcept
    {
        hz = std::max (hz, 20.0f);

        loopGain = std::pow (10.0f, -3.0f / (std::max (0.05f, ringSeconds) * hz));
        pole     = std::exp (-2.0f * kPi * std::min (lowpassHz, 0.45f * sampleRate) / sampleRate);

        // The lowpass delays the fundamental too, and a loop that ignores it
        // plays flat — by a few cents at the top of a violin. Its phase delay at
        // this pitch is taken off the finger side.
        const float w = 2.0f * kPi * hz / sampleRate;
        lowpassDelay = std::atan2 (pole * std::sin (w), 1.0f - pole * std::cos (w)) / w;
    }

    /** Pitch and bow position, which may change every sample. The bow position
        is a fraction of the vibrating length, measured from the bridge; the
        bridge side of the loop is that fraction of the period, the finger side
        the rest. */
    void setTuning (float hz, float bowPosition) noexcept
    {
        const float period = sampleRate / std::clamp (hz, 20.0f, sampleRate / 12.0f);

        bridgeLength = std::clamp (bowPosition * period, 2.5f, period * 0.4f);
        fingerLength = std::max (2.5f, period - bridgeLength - lowpassDelay);
        betaNow      = bridgeLength / period;
    }

    /** Where the bow sits, as a fraction of the vibrating length from the
        bridge. The bow's usable force scales with its inverse. */
    float beta() const noexcept { return betaNow; }

    /** A finger's pluck, as a velocity pulse through the bow point. Narrower is
        a harder, brighter pluck. */
    void pluck (float amplitude, int widthSamples) noexcept
    {
        pulseAmplitude = amplitude;
        pulseLength    = std::max (4, widthSamples);
        pulseLeft      = pulseLength;
    }

    /**
        One sample.

        `bowVelocity` is signed. `bowForce` is F/2Z and never negative; zero is a
        bow off the string. `restDamping` is the hair resting on a string it is
        not moving — a matched damper at 0.5, nothing at 0. `grain` is the rosin,
        a small noise on the friction.

        Returns the velocity wave arriving at the bridge, which is what drives
        the body.
    */
    float process (float bowVelocity, float bowForce, float restDamping, float grain) noexcept
    {
        const float atBridge = toBridge.read (bridgeLength);
        const float fromFinger = -toFinger.read (fingerLength);

        bridgeLowpass = (1.0f - pole) * atBridge + pole * bridgeLowpass;
        const float fromBridge = -loopGain * bridgeLowpass;

        const float incoming = fromFinger + fromBridge;

        float added = friction (bowVelocity, bowForce, incoming, grain)
                    - restDamping * incoming;

        if (pulseLeft > 0)
        {
            const float n = (float) (pulseLength - pulseLeft) / (float) pulseLength;
            added += pulseAmplitude * 0.5f * (1.0f - std::cos (2.0f * kPi * n));
            --pulseLeft;
        }

        toFinger.push (fromBridge + added);
        toBridge.push (fromFinger + added);

        return atBridge;
    }

    bool isSticking() const noexcept { return sticking; }

private:
    static constexpr float kPi = 3.14159265358979f;

    /**
        What the bow adds to both outgoing waves: the string's velocity at the
        bow minus what the arriving waves would have given it.

        With a = |vb - vh| the slip there would be without friction and
        k = F/2Z, a slipping string satisfies x = a - k μ(x), where x is the
        slip it really has. With the hyperbolic μ that is the quadratic

            x² + (v0 - a + k μd) x + v0 (k μs - a) = 0,

        whose larger root is the slipping branch.
    */
    float friction (float vb, float k, float vh, float grain) noexcept
    {
        if (k <= 0.0f)
        {
            sticking = false;
            return 0.0f;
        }

        const float want = vb - vh;
        const float a    = std::abs (want);
        const float s    = want >= 0.0f ? 1.0f : -1.0f;

        const float muS = kStaticFriction  * (1.0f + grain);
        const float muD = kDynamicFriction * (1.0f + grain);

        const bool canStick = a <= k * muS;

        if (sticking && canStick)
            return want;

        const float b = kSlipKnee - a + k * muD;
        const float c = kSlipKnee * (k * muS - a);
        const float disc = b * b - 4.0f * c;

        if (disc >= 0.0f)
        {
            const float x = 0.5f * (-b + std::sqrt (disc));

            if (x > 0.0f)
            {
                sticking = false;
                return s * (a - x);
            }
        }

        // No slipping solution left: the hair has caught the string.
        sticking = true;
        return want;
    }

    FractionalDelay toFinger, toBridge;

    float sampleRate = 48000.0f;
    float fingerLength = 100.0f, bridgeLength = 10.0f, betaNow = 0.1f;
    float loopGain = 0.99f, pole = 0.2f, lowpassDelay = 0.0f;
    float bridgeLowpass = 0.0f;
    bool  sticking = false;

    float pulseAmplitude = 0.0f;
    int   pulseLength = 0, pulseLeft = 0;
};

} // namespace chordtroller::dsp

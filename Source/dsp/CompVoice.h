#pragma once

#include <algorithm>
#include <cmath>

namespace chordtroller::dsp
{

/**
    The accompaniment's voice: an organ, more or less.

    Nothing here is plucked. The strings decay because a string decays; the
    accompaniment holds a note for as long as the pattern says it holds, and the
    drone holds it until the chord changes. That is a different envelope and a
    different oscillator, which is why this is not PluckVoice with new numbers.

    A handful of sines at whole-number multiples is a drawbar organ in miniature,
    and it has the one property this part of the mix needs above all others: no
    aliasing and no edges, so eight of them sitting under a strum stay behind it
    instead of fighting for the same top octave.
*/
class CompVoice
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate  = sampleRateIn;
        attackStep  = 1.0f / std::max (1.0f, (float) (sampleRateIn * 0.006));
        releaseStep = 1.0f / std::max (1.0f, (float) (sampleRateIn * 0.130));
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        phase = 0.0f;
        active = holding = false;
        gateSamples = -1;
    }

    /** Registration: how much octave and how much twelfth sit over the
        fundamental. A bass wants almost none, a chord wants both. */
    void setRegistration (float octave, float twelfth) noexcept
    {
        second = octave;
        third  = twelfth;
    }

    /** A gate of zero or less holds until noteOff — which is what the drone is. */
    void noteOn (float frequencyHz, float levelIn, float gateSeconds) noexcept
    {
        increment = (float) (frequencyHz / sampleRate);
        level     = levelIn;

        gateSamples = gateSeconds > 0.0f
                        ? (int) (gateSeconds * (float) sampleRate) : -1;

        // Phase is not reset: retriggering the same held note on a chord change
        // would otherwise step the waveform and click, exactly as it would on a
        // string.
        if (! active)
            phase = 0.0f;

        holding = true;
        active  = true;
    }

    void noteOff() noexcept { holding = false; }

    /** Retune without retriggering. A held bass note whose chord has changed
        under it should move to the new root, not sit on the old one until its
        gate runs out — that is what an auto-bass does, and leaving it is what
        makes the accompaniment sound stuck. */
    void setFrequency (float frequencyHz) noexcept
    {
        increment = (float) (frequencyHz / sampleRate);
    }

    bool isActive() const noexcept { return active; }

    float process() noexcept
    {
        if (! active)
            return 0.0f;

        if (gateSamples > 0 && --gateSamples == 0)
            holding = false;

        if (holding)
        {
            env = std::min (1.0f, env + attackStep);
        }
        else
        {
            env -= releaseStep;

            if (env <= 0.0f)
            {
                env = 0.0f;
                active = false;
                return 0.0f;
            }
        }

        const float value = std::sin (kTwoPi * phase)
                              + second * std::sin (kTwoPi * 2.0f * phase)
                              + third  * std::sin (kTwoPi * 3.0f * phase);

        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return value * env * level;
    }

private:
    static constexpr float kTwoPi = 6.28318530717959f;

    double sampleRate = 44100.0;

    float phase = 0.0f, increment = 0.0f;
    float env = 0.0f, level = 1.0f;
    float attackStep = 0.01f, releaseStep = 0.01f;
    float second = 0.0f, third = 0.0f;

    int  gateSamples = -1;
    bool active = false, holding = false;
};

} // namespace chordtroller::dsp

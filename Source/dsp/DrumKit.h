#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chordtroller::dsp
{

/**
    Three drums, made rather than sampled.

    Everything else in this instrument is synthesised, and the drums have no
    business being the one part that ships a megabyte of somebody's recordings.
    They are also the easiest things in the whole project to make from nothing:
    a kick is a sine falling off a cliff, a snare is noise with a tuned thump
    under it, and a hat is noise with the bottom taken out.

    The envelopes are all plain exponentials because that is what a struck thing
    does, and the whole kit is one mono signal because it is accompaniment — it
    sits behind the plate, not beside it.
*/
class DrumKit
{
public:
    enum class Piece { kick, snare, hat };

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;

        kickDecay      = decayCoefficient (0.28);
        kickPitchDecay = decayCoefficient (0.035);   // the drop that makes it a kick
        snareToneDecay = decayCoefficient (0.09);
        snareHissDecay = decayCoefficient (0.16);
        hatDecay       = decayCoefficient (0.045);

        reset();
    }

    void reset() noexcept
    {
        kickEnv = kickPitchEnv = 0.0f;
        snareToneEnv = snareHissEnv = 0.0f;
        hatEnv = 0.0f;
        kickPhase = snarePhase = 0.0f;
        snareLow = hatLow = 0.0f;
    }

    void trigger (Piece piece, float level) noexcept
    {
        switch (piece)
        {
            case Piece::kick:
                kickEnv = level;
                kickPitchEnv = 1.0f;
                break;

            case Piece::snare:
                snareToneEnv = level * 0.8f;
                snareHissEnv = level;
                break;

            case Piece::hat:
                hatEnv = level;
                break;
        }
    }

    float processSample() noexcept
    {
        float out = 0.0f;

        if (kickEnv > kFloor)
        {
            // 150 Hz down to 46 in about a thirtieth of a second. The drop is
            // the drum; a sine at one pitch is a tone.
            const float frequency = 46.0f + 105.0f * kickPitchEnv;

            kickPhase += frequency / (float) sampleRate;
            if (kickPhase >= 1.0f)
                kickPhase -= 1.0f;

            out += std::sin (kTwoPi * kickPhase) * kickEnv;

            kickEnv      *= kickDecay;
            kickPitchEnv *= kickPitchDecay;
        }

        const float noise = whiteNoise();

        if (snareHissEnv > kFloor)
        {
            snareLow += 0.55f * (noise - snareLow);
            out += (noise - snareLow) * snareHissEnv * 0.45f;
            snareHissEnv *= snareHissDecay;
        }

        if (snareToneEnv > kFloor)
        {
            snarePhase += 190.0f / (float) sampleRate;
            if (snarePhase >= 1.0f)
                snarePhase -= 1.0f;

            out += std::sin (kTwoPi * snarePhase) * snareToneEnv * 0.35f;
            snareToneEnv *= snareToneDecay;
        }

        if (hatEnv > kFloor)
        {
            hatLow += 0.82f * (noise - hatLow);
            out += (noise - hatLow) * hatEnv * 0.26f;
            hatEnv *= hatDecay;
        }

        return out;
    }

private:
    static constexpr float kTwoPi = 6.28318530717959f;
    static constexpr float kFloor = 1.0e-4f;

    float decayCoefficient (double seconds) const noexcept
    {
        return std::pow (0.001f, 1.0f / std::max (1.0f, (float) (sampleRate * seconds)));
    }

    /** Xorshift, because a drum kit should not be the reason this allocates or
        calls into the standard library's generator on the audio thread. */
    float whiteNoise() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;

        return (float) (std::int32_t) rng * (1.0f / 2147483648.0f);
    }

    double sampleRate = 44100.0;

    float kickEnv = 0.0f, kickPitchEnv = 0.0f, kickPhase = 0.0f;
    float snareToneEnv = 0.0f, snareHissEnv = 0.0f, snarePhase = 0.0f, snareLow = 0.0f;
    float hatEnv = 0.0f, hatLow = 0.0f;

    float kickDecay = 0.999f, kickPitchDecay = 0.99f;
    float snareToneDecay = 0.999f, snareHissDecay = 0.999f;
    float hatDecay = 0.99f;

    std::uint32_t rng = 0x9e3779b9u;
};

} // namespace chordtroller::dsp

#pragma once

#include "../input/PadMath.h"
#include "../music/ChordRing.h"
#include "../music/Scales.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace chordtroller::dsp
{

/**
    One voice, always sounding, shaped entirely by two hands.

    A theremin has no notes to trigger. There is a tone, and there is a left hand
    deciding how much of it you hear — every attack and every release is a volume
    gesture, and without that control the instrument is a siren. That is why the
    volume sits on the left trigger while the right thumb has the pitch: the same
    division of labour the real thing has, and the reason it can be played at all.

    Pitch runs through three stages before it reaches the oscillator. The stick
    gives a continuous position; the scale pulls it towards a degree by however
    much the right trigger asks for; and a glide chases the result, so even a full
    lock still slurs between notes rather than stepping like a keyboard.
*/
inline constexpr int kNumThereminTimbres = 4;

struct ThereminTimbre
{
    const char* name;
    float second;       // octave
    float third;        // twelfth
    float fifth;        // adds the reedy edge
};

inline const ThereminTimbre& thereminTimbre (int index) noexcept
{
    static const std::array<ThereminTimbre, kNumThereminTimbres> timbres
    {{
        { "REN",       0.00f, 0.00f, 0.00f },   // a sine, and nothing else
        { "KLASSISK",  0.12f, 0.05f, 0.00f },   // the Etherwave's gentle edge
        { "NASAL",     0.30f, 0.22f, 0.10f },
        { "CELLO",     0.45f, 0.30f, 0.20f }
    }};

    return timbres[(size_t) (((index % kNumThereminTimbres) + kNumThereminTimbres)
                               % kNumThereminTimbres)];
}

class ThereminEngine
{
public:
    static constexpr int kMinRangeOctaves = 1;
    static constexpr int kMaxRangeOctaves = 4;

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;
        phase = lfoPhase = 0.0f;
        level = 0.0f;
        sounding = pitchNow = (float) baseMidi();
        setGlide (glideIndex);
        volumeStep = 1.0f / std::max (1.0f, (float) (sampleRateIn * 0.004));
    }

    //== what the pad sets, once per block ====================================

    void setPitchAxis (float squaredY, float squaredX) noexcept
    {
        if (frozen)
            return;

        // The stick spans the range symmetrically about its centre, so the
        // middle of the throw is the middle of the register.
        const float span = (float) (12 * rangeOctaves);

        pitchRaw = (squaredY * 0.5f + 0.5f) * span;
        bend = squaredX * kBendSemitones;
    }

    void setQuantise (float strength) noexcept { quantise = std::clamp (strength, 0.0f, 1.0f); }
    void setVolume (float gain) noexcept { targetLevel = std::clamp (gain, 0.0f, 1.0f); }

    /** Left stick: up is vibrato, down is tremolo, sideways is the rate they
        share. One stick, two modulations, and never both at once — which is what
        a hand wants anyway. */
    void setModulation (float squaredY, float squaredX) noexcept
    {
        vibratoDepth = std::max (0.0f, squaredY);
        tremoloDepth = std::max (0.0f, -squaredY);

        // In octaves: equal throw, equal ratio of speed.
        lfoRate = 0.6f * std::pow (15.0f, squaredX * 0.5f + 0.5f);    // 0.6 to 9 Hz
    }

    float modulationRate() const noexcept { return lfoRate; }

    void setKeyRoot (int pitchClass) noexcept { keyRoot = ((pitchClass % 12) + 12) % 12; }
    void setScale (int index) noexcept { scaleIndex = index; }
    void setTimbre (int index) noexcept { timbreIndex = index; }
    void setOctaveShift (int octaves) noexcept { octaveShift = std::clamp (octaves, -2, 2); }

    void setRangeOctaves (int octaves) noexcept
    {
        rangeOctaves = std::clamp (octaves, kMinRangeOctaves, kMaxRangeOctaves);
    }

    void cycleRange() noexcept
    {
        setRangeOctaves (rangeOctaves >= kMaxRangeOctaves ? kMinRangeOctaves : rangeOctaves + 1);
    }

    /** Short, soft, long. Even a full lock glides — a theremin that steps
        between notes has stopped being one. */
    void setGlide (int index) noexcept
    {
        glideIndex = ((index % 3) + 3) % 3;

        static constexpr double seconds[3] { 0.012, 0.045, 0.150 };
        glideCoefficient = 1.0f - std::exp (-1.0f / (float) (sampleRate * seconds[glideIndex]));
    }

    void cycleGlide() noexcept { setGlide (glideIndex + 1); }

    void setFrozen (bool shouldFreeze) noexcept { frozen = shouldFreeze; }
    bool isFrozen() const noexcept { return frozen; }

    void allOff() noexcept { targetLevel = 0.0f; level = 0.0f; }

    //== state for the screen =================================================

    int   rangeInOctaves() const noexcept { return rangeOctaves; }
    int   scale()         const noexcept { return scaleIndex; }
    int   timbre()        const noexcept { return timbreIndex; }
    int   glide()         const noexcept { return glideIndex; }
    float quantiseAmount() const noexcept { return quantise; }

    /** Where the stick is pointing before the scale touches it, and where it
        ends up after — both in MIDI note numbers, so the screen can draw the
        distance between them. */
    float rawMidi()    const noexcept { return (float) baseMidi() + pitchRaw + bend; }
    float pulledMidi() const noexcept { return pitchNow; }
    float soundingMidi() const noexcept { return sounding; }

    int nearestNote() const noexcept { return (int) std::lround (sounding); }

    float centsOff() const noexcept
    {
        return (sounding - (float) nearestNote()) * 100.0f;
    }

    //== audio ================================================================

    /** Called once per block: the pitch decision is not worth doing per sample,
        only the glide towards it is. */
    void updateTarget() noexcept
    {
        const auto& scale = music::scaleAt (scaleIndex);
        const float pulled = music::pullToScale (pitchRaw + bend, scale.mask, quantise);

        pitchNow = (float) baseMidi() + pulled;
    }

    float processSample() noexcept
    {
        sounding += (pitchNow - sounding) * glideCoefficient;

        lfoPhase += lfoRate / (float) sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;

        const float lfo = std::sin (kTwoPi * lfoPhase);

        // Vibrato in cents rather than hertz, so it is the same width wherever
        // it is played.
        const float cents = lfo * fromZeroInRatios (kVibratoMinCents, kVibratoCents, vibratoDepth);
        const float note  = sounding + cents * 0.01f;

        phase += midiToHz (note) / (float) sampleRate;
        if (phase >= 1.0f)
            phase -= 1.0f;

        const auto& voice = thereminTimbre (timbreIndex);

        float value = std::sin (kTwoPi * phase)
                        + voice.second * std::sin (kTwoPi * 2.0f * phase)
                        + voice.third  * std::sin (kTwoPi * 3.0f * phase)
                        + voice.fifth  * std::sin (kTwoPi * 5.0f * phase);

        level += (targetLevel - level) * volumeStep;

        // The dip in decibels, so the depth is heard evenly across the throw.
        const float dipDb   = fromZeroInRatios (kTremoloMinDb, kTremoloMaxDb, tremoloDepth);
        const float tremolo = std::pow (10.0f, -dipDb * 0.5f * (1.0f - lfo) / 20.0f);

        return value * level * tremolo * kOutputTrim;
    }

private:
    static constexpr float kTwoPi = 6.28318530717959f;
    static constexpr float kBendSemitones = 1.0f;
    static constexpr float kVibratoCents    = 60.0f;
    static constexpr float kVibratoMinCents = 4.0f;
    static constexpr float kTremoloMaxDb    = 24.0f;
    static constexpr float kTremoloMinDb    = 1.0f;

    /** Equal ratios from `low` to `high`, fading in from nothing over the first
        twentieth so a stick just past its deadzone does not jump. */
    static float fromZeroInRatios (float low, float high, float x) noexcept
    {
        if (x <= 0.0f)
            return 0.0f;

        return low * std::pow (high / low, std::min (x, 1.0f)) * std::min (1.0f, x / 0.05f);
    }
    static constexpr float kOutputTrim = 0.32f;

    int baseMidi() const noexcept
    {
        // The register sits under the stick's centre and moves in whole octaves,
        // so the key's root is always the bottom of the throw.
        return 48 + keyRoot + 12 * octaveShift;
    }

    static float midiToHz (float note) noexcept
    {
        return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    }

    double sampleRate = 44100.0;

    float phase = 0.0f, lfoPhase = 0.0f;
    float pitchRaw = 12.0f, bend = 0.0f;
    float pitchNow = 60.0f, sounding = 60.0f;
    float level = 0.0f, targetLevel = 0.0f, volumeStep = 0.01f;
    float glideCoefficient = 0.05f;
    float vibratoDepth = 0.0f, tremoloDepth = 0.0f, lfoRate = 5.0f;
    float quantise = 0.5f;

    int keyRoot = 0, scaleIndex = 0, timbreIndex = 1;
    int octaveShift = 0, rangeOctaves = 2, glideIndex = 1;
    bool frozen = false;
};

} // namespace chordtroller::dsp

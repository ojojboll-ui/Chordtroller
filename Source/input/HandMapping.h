#pragma once

#include "PadState.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chordtroller::hands
{

/**
    Two hands in front of a webcam, turned into the controller the instruments
    already know how to play.

    The hands are the two sticks. The right hand's place around a home position is
    the stick's deflection, and the instrument's own strum clock turns deflection
    into a rate, so the strum goes on by itself for as long as the hand is held
    above or below home and falls silent when it is brought back to the middle.
    A position holds still, so the camera's 30 frames a second and its delay cost
    nothing here, which a stroke measured as speed could not say. The left hand's
    place around its own home is the chord ring's bearing.

    Pure maths over a plain struct, no JUCE, no network, so it runs in the offline
    test with invented hands.
*/
enum class Gesture : std::uint8_t { none, open, fist, point, pinch };

struct HandFeatures
{
    bool    present = false;
    float   x = 0.5f, y = 0.5f;     // palm centre in the mirrored image, 0..1
    float   vx = 0.0f, vy = 0.0f;   // image widths / heights per second; +y is down
    float   curlVel = 0.0f;         // index..pinky closing (+) or opening (-), curl units per second
    float   pinch = 0.0f;           // thumb to index, 1 = touching
    float   openness = 1.0f;
    Gesture gesture = Gesture::none;
};

struct HandTuning
{
    // Where each hand rests. The image is mirrored, so the user's left hand lives
    // on the left of it. The ring selects by direction from home, so home is the
    // middle of the chord's "no man's land" and the hand does not have to be told
    // where it is.
    float leftHomeX  = 0.28f, leftHomeY  = 0.55f;
    float rightHomeX = 0.72f;

    // How far from home is full deflection, as a fraction of the image.
    float leftReach  = 0.16f;
    float rightReach = 0.14f;

    // The right hand is the right stick. Where it sits relative to its home is the
    // stick's deflection: above home strums upward, below strums downward, and the
    // further from home the faster the instrument's own strum clock runs. At home
    // it is silent. Sideways is the sweep window, as on the pad. Nothing here
    // measures motion, so the camera's frame rate and delay do not matter: the hand
    // can hold a position and the strum simply goes on.
    enum class StrumSource : std::uint8_t
    {
        position,   // place around home, like a stick (default)
        speed,      // how fast the palm moves up or down, in image heights per second
        fingers     // closing or opening the fingers; did not track well enough to play
    };

    StrumSource strumSource = StrumSource::position;

    float rightHomeY  = 0.50f;
    float rightReachY = 0.25f;          // image fraction from home to a full-speed strum
    float rightRestZone = 0.26f;        // fraction of the reach that is silent, so a hand hovering near home is quiet

    // How the throw beyond the silent zone becomes stick deflection. Linear gives
    // the middle of the travel a middling strum and crowds the slow end; a taper
    // spends more of the travel on slow strums and saves the fast ones for the far
    // end, like the log taper of an audio pot. 0 is linear, larger tapers harder,
    // and a negative value bends the other way (fast at once, fine detail late).
    float strumTaper = 1.6f;

    // Time constant, in seconds, of the low-pass on the right stick. The camera
    // reports about 30 times a second and each report carries a little noise, so
    // the raw stick steps and wobbles, and the strum rate with it. Smoothing turns
    // the steps into a glide. Longer is softer and lags more; 0 is off.
    float strumSmoothing = 0.08f;

    // The smallest right-stick deflection the instrument will strum at, once its
    // own deadzones (PadTuning::stickDeadzone 0.10, then strumDeadzone 0.12) have
    // taken their share. The taper starts here, so a hand leaving the silent zone
    // begins strumming at once instead of crossing a second dead stretch.
    float strumEngineStart = 0.22f;

    // Whether the hand's sideways place narrows the sweep window, as the right stick
    // does on a pad.
    bool sweepFromHandX = true;

    // Speed mode. The instrument has deadzones of its own on top, so a stroke has
    // to beat about 0.55 image heights per second to be heard at all.
    float strumFullSpeed  = 2.0f;
    float strumNoiseFloor = 0.15f;

    // Finger mode.
    float fingerFullSpeed  = 5.0f;      // curl units per second at a full-throw strum
    float fingerNoiseFloor = 0.7f;      // below this the fingers count as still

    // Right trigger is the volume pedal, and a released one is silence. Hands
    // have no pedal, so a visible right hand is simply "playing".
    float volumeWhilePresent = 0.85f;
};

inline float clamp1 (float v) noexcept { return std::clamp (v, -1.0f, 1.0f); }

/** A deflection from home, kept inside the unit disc like a thumbstick's gate. */
inline void disc (float x, float y, float& ox, float& oy) noexcept
{
    const float r = std::sqrt (x * x + y * y);
    const float s = r > 1.0f ? 1.0f / r : 1.0f;
    ox = x * s;
    oy = y * s;
}

/** One-pole low-pass with a time constant, so the glide is the same whether it is
    stepped every 4 ms or every 10. A step response reaches 63 % after tau and never
    overshoots. */
struct Smoother
{
    float value = 0.0f;

    float step (float target, float dtSeconds, float tau) noexcept
    {
        if (tau <= 0.0f)
            return value = target;

        value += (target - value) * (1.0f - std::exp (-std::max (dtSeconds, 0.0f) / tau));
        return value;
    }

    void reset() noexcept { value = 0.0f; }
};

/**
    Left hand:  place around home -> left stick (chord ring), pinch -> LT (how long
                the strings ring), fist -> LB (the seventh bank).
    Right hand: place around home -> right stick (strum up/down, sweep window),
                presence -> RT (volume).
    A hand that leaves lets go of everything: sticks and triggers return to rest,
    and the chord ring holds the last chord when the left stick is inside its
    hold radius, so losing the hand mid-phrase does not lose the harmony.
*/
inline PadState map (const HandFeatures& left, const HandFeatures& right,
                     const HandTuning& t = {}) noexcept
{
    PadState s;
    s.connected = true;

    if (left.present)
    {
        disc ((left.x - t.leftHomeX) / t.leftReach,
              -(left.y - t.leftHomeY) / t.leftReach, s.lx, s.ly);     // +y is up on a stick

        if (left.gesture == Gesture::fist)
            s.buttons |= padLB;
        else
            s.lt = std::clamp (left.pinch, 0.0f, 1.0f);
    }

    if (right.present)
    {
        s.rt = t.volumeWhilePresent;

        if (t.strumSource == HandTuning::StrumSource::position)
        {
            const float up = (t.rightHomeY - right.y) / t.rightReachY;     // hand up = stick up
            const float a  = std::abs (up);

            if (a < t.rightRestZone)
            {
                s.ry = 0.0f;
            }
            else
            {
                const float travel = std::min ((a - t.rightRestZone) / (1.0f - t.rightRestZone), 1.0f);
                const float bent = std::abs (t.strumTaper) < 1.0e-3f
                                     ? travel
                                     : std::expm1 (t.strumTaper * travel) / std::expm1 (t.strumTaper);

                s.ry = std::copysign (t.strumEngineStart + (1.0f - t.strumEngineStart) * bent, up);
            }
        }
        else
        {
            const bool  fingers = t.strumSource == HandTuning::StrumSource::fingers;
            const float full  = fingers ? t.fingerFullSpeed : t.strumFullSpeed;
            const float still = fingers ? t.fingerNoiseFloor : t.strumNoiseFloor;
            const float speed = (fingers ? -right.curlVel : -right.vy) / full;
            const float noise = still / full;
            const float a = std::abs (speed);

            s.ry = a < noise ? 0.0f : std::copysign (std::min ((a - noise) / (1.0f - noise), 1.0f), speed);
        }

        s.rx = t.sweepFromHandX ? clamp1 ((right.x - t.rightHomeX) / t.rightReach) : 0.0f;
    }

    return s;
}

} // namespace chordtroller::hands

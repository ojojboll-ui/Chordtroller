#pragma once

#include <cstdint>

namespace chordtroller
{

/**
    What a gamepad looks like once the platform is off it.

    Everything here is raw: sticks span -1..1 with no deadzone taken out and no
    gate correction applied, triggers span 0..1. The shaping lives in PadMath.h
    and happens where the values are used, so the monitor can show the untouched
    reading next to the processed one — when a direction feels wrong it matters a
    great deal whether the stick or the maths is at fault.

    +Y is up on both sticks, which is what XInput reports and the opposite of
    what screen coordinates do. Anything that draws these has to flip.
*/
enum PadButton : std::uint16_t
{
    padA        = 1u << 0,
    padB        = 1u << 1,
    padX        = 1u << 2,
    padY        = 1u << 3,
    padLB       = 1u << 4,
    padRB       = 1u << 5,
    padView     = 1u << 6,
    padMenu     = 1u << 7,
    padLS       = 1u << 8,
    padRS       = 1u << 9,
    padDpadUp   = 1u << 10,
    padDpadDown = 1u << 11,
    padDpadLeft = 1u << 12,
    padDpadRight= 1u << 13
};

/** Which way is down, as the controller sees it: a unit vector in its own
    frame, fused from gyro and accelerometer by MotionFusion. SDL's axes — +X
    right across the face, +Y up the face, +Z out of it. */
struct Gravity
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool  valid = false;
};

struct PadState
{
    float lx = 0.0f, ly = 0.0f;     // left stick,  -1..1, +y up
    float rx = 0.0f, ry = 0.0f;     // right stick, -1..1, +y up
    float lt = 0.0f, rt = 0.0f;     // triggers, 0..1

    std::uint16_t buttons = 0;
    bool connected = false;

    /** Motion, from controllers that have it. A Joy-Con pair fills both hands;
        a single controller only the right, because one controller cannot be two
        hands. An Xbox pad fills neither. */
    Gravity leftHand, rightHand;

    /** Whose layout is in the player's hands. Buttons are reported by position
        whichever it is, so this only changes the names the screen prints: the
        bottom face button is A on an Xbox pad and B on a Nintendo one, and a
        Steam Controller has trackpads where the d-pad and the right stick are. */
    enum class Layout : std::uint8_t { xbox, nintendo, steam };

    Layout layout = Layout::xbox;
};

/** A button crossing, stamped when the poll thread saw it rather than when the
    audio thread got round to reading it. Phase 1 only counts these; from phase 2
    the stamp is what places a chord change at the right sample. */
struct PadEvent
{
    std::uint16_t button = 0;
    bool          down   = false;
    std::uint32_t timeMs = 0;
};

} // namespace chordtroller

#pragma once

#include "PadState.h"
#include <memory>

namespace chordtroller
{

/**
    One gamepad, whatever the platform calls it.

    Everything platform-specific in Chordtroller is behind this interface, and it
    is here from the first commit for a reason: the plan puts macOS after v1, and
    the difference between "add a file" and "unpick the input layer" is decided
    now rather than then. Windows speaks XInput, macOS will speak
    GameController.framework, and nothing above this line knows which.

    poll() is called from the reader thread and from nowhere else.
*/
class GamepadSource
{
public:
    virtual ~GamepadSource() = default;

    /** Fills out with the current reading. Returns false when no pad is
        connected, in which case out is left zeroed and disconnected. Must not
        block: a missing device is reported, never waited for. */
    virtual bool poll (PadState& out) = 0;

    /** Both motors, 0..1. Unused until phase 3, but the pad is the only thing
        that can push back on a finger and the interface should not have to grow
        to admit it. */
    virtual void setRumble (float lowFrequency, float highFrequency) = 0;

    /** What this source is, for the monitor to display. */
    virtual const char* name() const = 0;

    /** Called on the reader thread as it stops, for a source that has to let go
        of what it holds on the thread that took hold of it. */
    virtual void threadStopping() {}

    /** The best source this platform has. Never null — platforms with no
        implementation get one that is permanently disconnected, so the app runs
        and says so rather than failing to build. */
    static std::unique_ptr<GamepadSource> createForPlatform();
};

/** Two hands in front of a webcam, read from the handtrack program as OSC over
    UDP on the loopback address (port 9000, or CHORDTROLLER_HAND_PORT). Behind
    every real pad in priority. In HandSource.cpp. */
std::unique_ptr<GamepadSource> createHandSource();

#if CHORDTROLLER_WITH_SDL
/** Controllers with motion sensors — a Joy-Con pair, a Switch Pro Controller, the
    original Steam Controller — through SDL3. In SdlGamepadSource.cpp. */
std::unique_ptr<GamepadSource> createSdlGamepadSource();
#endif

} // namespace chordtroller

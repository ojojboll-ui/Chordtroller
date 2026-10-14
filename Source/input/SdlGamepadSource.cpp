#include "GamepadSource.h"
#include "MotionFusion.h"

#include <juce_core/juce_core.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace chordtroller
{

namespace
{
    /** SDL is one library per process, and its joystick layer is not safe to
        drive from two threads at once. A host can load the plugin twice, and
        each instance polls on its own thread. */
    std::mutex& sdlMutex()
    {
        static std::mutex m;
        return m;
    }

    float stick (Sint16 v) noexcept
    {
        return std::clamp ((float) v / 32767.0f, -1.0f, 1.0f);
    }

    /**
        Controllers with motion sensors — a Joy-Con pair, a Switch Pro Controller,
        the original Steam Controller — through SDL3.

        Xbox pads are read directly through XInput, so every other SDL backend is
        switched off before SDL starts: two readers of one pad would both report
        its buttons.

        SDL reads buttons by position, not by the letter printed on them, so the
        bottom face button arrives as padA on either layout. Only the names on
        the screen differ, and PadState::layout tells the screen which names to use.
    */
    class SdlSource final : public GamepadSource
    {
    public:
        bool poll (PadState& out) override
        {
            out = PadState{};

            const std::lock_guard<std::mutex> lock (sdlMutex());

            if (! ensureInitialised())
                return false;

            // Events are off, so nothing else pumps the HIDAPI devices.
            SDL_UpdateGamepads();

            if (gamepad != nullptr && ! SDL_GamepadConnected (gamepad))
                close();

            if (gamepad == nullptr)
                scan();

            if (gamepad == nullptr)
                return false;

            fill (out);
            return true;
        }

        void setRumble (float low, float high) override
        {
            const std::lock_guard<std::mutex> lock (sdlMutex());

            if (gamepad == nullptr)
                return;

            SDL_RumbleGamepad (gamepad,
                               (Uint16) std::lround (std::clamp (low,  0.0f, 1.0f) * 65535.0f),
                               (Uint16) std::lround (std::clamp (high, 0.0f, 1.0f) * 65535.0f),
                               80);
        }

        const char* name() const override
        {
            return "SDL3";
        }

        /** SDL is let go of on the thread that took hold of it. */
        void threadStopping() override
        {
            const std::lock_guard<std::mutex> lock (sdlMutex());

            close();

            if (initialised)
            {
                SDL_QuitSubSystem (SDL_INIT_GAMEPAD | SDL_INIT_SENSOR);
                initialised = false;
            }
        }

    private:
        bool ensureInitialised()
        {
            if (initialised)
                return true;

            if (failed)
                return false;

            // By name rather than by macro, so a hint this SDL does not know is
            // simply ignored instead of failing the build.
            SDL_SetHint ("SDL_XINPUT_ENABLED", "0");
            SDL_SetHint ("SDL_JOYSTICK_XINPUT", "0");
            SDL_SetHint ("SDL_JOYSTICK_RAWINPUT", "0");
            SDL_SetHint ("SDL_JOYSTICK_WGI", "0");
            SDL_SetHint ("SDL_JOYSTICK_DIRECTINPUT", "0");
            SDL_SetHint ("SDL_JOYSTICK_HIDAPI_XBOX", "0");
            SDL_SetHint ("SDL_JOYSTICK_HIDAPI_COMBINE_JOY_CONS", "1");

            // SDL leaves the Steam Controller's driver off by default, because
            // Steam normally owns the controller. Here nothing else does — and if
            // Steam is running it still will, and the controller arrives as an
            // Xbox pad through XInput instead, without its gyro.
            SDL_SetHint ("SDL_JOYSTICK_HIDAPI_STEAM", "1");
            SDL_SetHint ("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");

            if (! SDL_InitSubSystem (SDL_INIT_GAMEPAD | SDL_INIT_SENSOR))
            {
                failed = true;
                return false;
            }

            // Nobody drains an event queue here; readings are polled instead.
            SDL_SetGamepadEventsEnabled (false);
            SDL_SetJoystickEventsEnabled (false);

            initialised = true;
            return true;
        }

        /** Once a second, like the XInput source: looking for a pad that is not
            there is not free. */
        void scan()
        {
            const auto now = juce::Time::getMillisecondCounter();

            if (now - lastScanMs < 1000)
                return;

            lastScanMs = now;

            int count = 0;
            SDL_JoystickID* ids = SDL_GetGamepads (&count);

            if (ids == nullptr)
                return;

            for (int i = 0; i < count && gamepad == nullptr; ++i)
                gamepad = SDL_OpenGamepad (ids[i]);

            SDL_free (ids);

            if (gamepad == nullptr)
                return;

            for (const auto sensor : { SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO,
                                       SDL_SENSOR_ACCEL_L, SDL_SENSOR_GYRO_L,
                                       SDL_SENSOR_ACCEL_R, SDL_SENSOR_GYRO_R })
            {
                if (SDL_GamepadHasSensor (gamepad, sensor))
                    SDL_SetGamepadSensorEnabled (gamepad, sensor, true);
            }

            const auto type = SDL_GetGamepadType (gamepad);

            // Valve's vendor id covers the Steam Controller in every one of its
            // connections — cable, wireless dongle, Bluetooth.
            constexpr Uint16 kValve = 0x28DE;

            if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO
                 || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT
                 || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT
                 || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)
                layout = PadState::Layout::nintendo;
            else if (SDL_GetGamepadVendor (gamepad) == kValve)
                layout = PadState::Layout::steam;
            else
                layout = PadState::Layout::xbox;

            leftFusion.reset();
            rightFusion.reset();
            lastSensorSeconds = 0.0;
        }

        void close()
        {
            if (gamepad != nullptr)
            {
                SDL_CloseGamepad (gamepad);
                gamepad = nullptr;
            }
        }

        bool has (SDL_SensorType sensor) const
        {
            return SDL_GamepadHasSensor (gamepad, sensor);
        }

        void readHand (SDL_SensorType accel, SDL_SensorType gyro, MotionFusion& fusion,
                       Gravity& hand, float dt)
        {
            float a[3] {}, w[3] {};

            if (! SDL_GetGamepadSensorData (gamepad, accel, a, 3)
                 || ! SDL_GetGamepadSensorData (gamepad, gyro, w, 3))
                return;

            fusion.update (a[0], a[1], a[2], w[0], w[1], w[2], dt);
            hand = fusion.gravity();
        }

        void fill (PadState& out)
        {
            // Sticks: SDL's Y grows downwards, PadState's upwards.
            out.lx =  stick (SDL_GetGamepadAxis (gamepad, SDL_GAMEPAD_AXIS_LEFTX));
            out.ly = -stick (SDL_GetGamepadAxis (gamepad, SDL_GAMEPAD_AXIS_LEFTY));
            out.rx =  stick (SDL_GetGamepadAxis (gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
            out.ry = -stick (SDL_GetGamepadAxis (gamepad, SDL_GAMEPAD_AXIS_RIGHTY));

            // ZL and ZR are buttons on a Joy-Con, so these arrive as nothing or
            // everything. Nothing here can make them analogue.
            out.lt = std::clamp ((float) SDL_GetGamepadAxis (gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)  / 32767.0f, 0.0f, 1.0f);
            out.rt = std::clamp ((float) SDL_GetGamepadAxis (gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f, 0.0f, 1.0f);

            std::uint16_t b = 0;

            auto map = [this, &b] (SDL_GamepadButton from, PadButton to)
            {
                if (SDL_GetGamepadButton (gamepad, from))
                    b |= (std::uint16_t) to;
            };

            map (SDL_GAMEPAD_BUTTON_SOUTH, padA);               map (SDL_GAMEPAD_BUTTON_EAST, padB);
            map (SDL_GAMEPAD_BUTTON_WEST, padX);                map (SDL_GAMEPAD_BUTTON_NORTH, padY);
            map (SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, padLB);      map (SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, padRB);
            map (SDL_GAMEPAD_BUTTON_BACK, padView);             map (SDL_GAMEPAD_BUTTON_START, padMenu);
            map (SDL_GAMEPAD_BUTTON_LEFT_STICK, padLS);         map (SDL_GAMEPAD_BUTTON_RIGHT_STICK, padRS);
            map (SDL_GAMEPAD_BUTTON_DPAD_UP, padDpadUp);        map (SDL_GAMEPAD_BUTTON_DPAD_DOWN, padDpadDown);
            map (SDL_GAMEPAD_BUTTON_DPAD_LEFT, padDpadLeft);    map (SDL_GAMEPAD_BUTTON_DPAD_RIGHT, padDpadRight);

            out.buttons   = b;
            out.connected = true;
            out.layout    = layout;

            // Motion. A Joy-Con pair has one set of sensors in each hand; a
            // single controller has one set, which is the right hand's, and no
            // left hand at all — one controller cannot be two antennas.
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const float  dt  = lastSensorSeconds > 0.0 ? (float) (now - lastSensorSeconds) : 0.0f;
            lastSensorSeconds = now;

            if (has (SDL_SENSOR_ACCEL_R) && has (SDL_SENSOR_GYRO_R))
            {
                readHand (SDL_SENSOR_ACCEL_R, SDL_SENSOR_GYRO_R, rightFusion, out.rightHand, dt);

                if (has (SDL_SENSOR_ACCEL_L) && has (SDL_SENSOR_GYRO_L))
                    readHand (SDL_SENSOR_ACCEL_L, SDL_SENSOR_GYRO_L, leftFusion, out.leftHand, dt);
            }
            else if (has (SDL_SENSOR_ACCEL) && has (SDL_SENSOR_GYRO))
            {
                readHand (SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO, rightFusion, out.rightHand, dt);
            }
        }

        SDL_Gamepad* gamepad = nullptr;
        bool initialised = false, failed = false;
        PadState::Layout layout = PadState::Layout::xbox;
        std::uint32_t lastScanMs = 0;

        MotionFusion leftFusion, rightFusion;
        double lastSensorSeconds = 0.0;
    };
}

std::unique_ptr<GamepadSource> createSdlGamepadSource()
{
    return std::make_unique<SdlSource>();
}

} // namespace chordtroller

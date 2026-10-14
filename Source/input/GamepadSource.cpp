#include "GamepadSource.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include <algorithm>
#include <cmath>

namespace chordtroller
{

#if JUCE_WINDOWS

namespace
{
    // Deliberately not <xinput.h>: linking the import library ties the binary to
    // one XInput version, and a host that loads us on a machine without that
    // exact DLL then fails at load time rather than reporting a missing pad.
    // Bound at runtime instead, newest first.
    constexpr int kMaxPads = 4;

    struct XGamepad
    {
        WORD  wButtons;
        BYTE  bLeftTrigger;
        BYTE  bRightTrigger;
        SHORT sThumbLX;
        SHORT sThumbLY;
        SHORT sThumbRX;
        SHORT sThumbRY;
    };

    struct XState
    {
        DWORD    dwPacketNumber;
        XGamepad Gamepad;
    };

    struct XVibration
    {
        WORD wLeftMotorSpeed;
        WORD wRightMotorSpeed;
    };

    using GetStateFn = DWORD (WINAPI*) (DWORD, XState*);
    using SetStateFn = DWORD (WINAPI*) (DWORD, XVibration*);

    constexpr WORD xDpadUp    = 0x0001, xDpadDown  = 0x0002;
    constexpr WORD xDpadLeft  = 0x0004, xDpadRight = 0x0008;
    constexpr WORD xStart     = 0x0010, xBack      = 0x0020;
    constexpr WORD xLThumb    = 0x0040, xRThumb    = 0x0080;
    constexpr WORD xLShoulder = 0x0100, xRShoulder = 0x0200;
    constexpr WORD xA = 0x1000, xB = 0x2000, xX = 0x4000, xY = 0x8000;

    /** SHORT reaches one step further down than up, so a raw division makes
        full-left read very slightly past full-right. Clamped rather than left
        lopsided, because the chord ring reads a bearing off these. */
    float axis (SHORT v) noexcept
    {
        return std::clamp ((float) v / 32767.0f, -1.0f, 1.0f);
    }

    class XInputSource final : public GamepadSource
    {
    public:
        XInputSource()
        {
            // 1_4 ships with Windows 8 and later; 9_1_0 is the compatibility
            // stub present since Vista and enough for a pad.
            for (const auto* dll : { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" })
            {
                library = LoadLibraryA (dll);

                if (library != nullptr)
                {
                    loadedName = dll;
                    break;
                }
            }

            if (library == nullptr)
                return;

            getState = (GetStateFn) (void*) GetProcAddress (library, "XInputGetState");
            setState = (SetStateFn) (void*) GetProcAddress (library, "XInputSetState");
        }

        ~XInputSource() override
        {
            if (library != nullptr)
                FreeLibrary (library);
        }

        bool poll (PadState& out) override
        {
            out = PadState{};

            if (getState == nullptr)
                return false;

            XState raw {};

            // Polling an empty slot costs milliseconds, not microseconds —
            // Windows walks the driver stack every time. Four of those at 250 Hz
            // would eat the thread whole, so once a pad is found only its slot
            // is read, and the hunt for a new one happens once a second.
            if (activeSlot >= 0)
            {
                if (getState ((DWORD) activeSlot, &raw) == ERROR_SUCCESS)
                {
                    fill (out, raw);
                    return true;
                }

                activeSlot = -1;
            }

            const auto now = juce::Time::getMillisecondCounter();

            if (now - lastScanMs < 1000)
                return false;

            lastScanMs = now;

            for (int slot = 0; slot < kMaxPads; ++slot)
            {
                if (getState ((DWORD) slot, &raw) == ERROR_SUCCESS)
                {
                    activeSlot = slot;
                    fill (out, raw);
                    return true;
                }
            }

            return false;
        }

        void setRumble (float low, float high) override
        {
            if (setState == nullptr || activeSlot < 0)
                return;

            XVibration v {};
            v.wLeftMotorSpeed  = (WORD) std::lround (std::clamp (low,  0.0f, 1.0f) * 65535.0f);
            v.wRightMotorSpeed = (WORD) std::lround (std::clamp (high, 0.0f, 1.0f) * 65535.0f);
            setState ((DWORD) activeSlot, &v);
        }

        const char* name() const override
        {
            return library != nullptr ? loadedName : "XInput saknas";
        }

    private:
        static void fill (PadState& out, const XState& raw) noexcept
        {
            const auto& g = raw.Gamepad;

            out.lx = axis (g.sThumbLX);
            out.ly = axis (g.sThumbLY);
            out.rx = axis (g.sThumbRX);
            out.ry = axis (g.sThumbRY);
            out.lt = (float) g.bLeftTrigger  / 255.0f;
            out.rt = (float) g.bRightTrigger / 255.0f;

            const WORD w = g.wButtons;
            std::uint16_t b = 0;

            auto map = [&b, w] (WORD from, PadButton to)
            {
                if ((w & from) != 0)
                    b |= (std::uint16_t) to;
            };

            map (xA, padA);                map (xB, padB);
            map (xX, padX);                map (xY, padY);
            map (xLShoulder, padLB);       map (xRShoulder, padRB);
            map (xBack, padView);          map (xStart, padMenu);
            map (xLThumb, padLS);          map (xRThumb, padRS);
            map (xDpadUp, padDpadUp);      map (xDpadDown, padDpadDown);
            map (xDpadLeft, padDpadLeft);  map (xDpadRight, padDpadRight);

            out.buttons   = b;
            out.connected = true;
        }

        HMODULE     library    = nullptr;
        const char* loadedName = "XInput";
        GetStateFn  getState   = nullptr;
        SetStateFn  setState   = nullptr;

        int           activeSlot = -1;
        std::uint32_t lastScanMs = 0;
    };
}

namespace
{
    /**
        An Xbox pad when there is one, a Switch controller otherwise.

        XInput is asked first and wins whenever it has a pad, which also means an
        Xbox pad plugged in mid-session takes over. SDL is only polled while
        XInput has nothing, so the two never both report the same buttons.
    */
    class CompositeSource final : public GamepadSource
    {
    public:
        CompositeSource (std::unique_ptr<GamepadSource> firstChoice,
                         std::unique_ptr<GamepadSource> otherwise)
            : first (std::move (firstChoice)), second (std::move (otherwise)) {}

        bool poll (PadState& out) override
        {
            if (first->poll (out))
            {
                active = first.get();
                return true;
            }

            if (second != nullptr && second->poll (out))
            {
                active = second.get();
                return true;
            }

            active = nullptr;
            out = PadState{};
            return false;
        }

        void setRumble (float low, float high) override
        {
            if (active != nullptr)
                active->setRumble (low, high);
        }

        const char* name() const override
        {
            return active != nullptr ? active->name() : first->name();
        }

        void threadStopping() override
        {
            first->threadStopping();

            if (second != nullptr)
                second->threadStopping();
        }

    private:
        std::unique_ptr<GamepadSource> first, second;
        GamepadSource* active = nullptr;
    };
}

std::unique_ptr<GamepadSource> GamepadSource::createForPlatform()
{
    // Pads first, hands last: a pad that is plugged in is a choice made by the
    // person holding it, while the hand stream is on whenever handtrack runs.
   #if CHORDTROLLER_WITH_SDL
    auto rest = std::make_unique<CompositeSource> (createSdlGamepadSource(), createHandSource());
   #else
    auto rest = createHandSource();
   #endif

    return std::make_unique<CompositeSource> (std::make_unique<XInputSource>(), std::move (rest));
}

#else

namespace
{
    /** Stands in wherever there is no implementation yet. The app starts, the
        monitor says there is no source, and nothing has to be #if'd out of the
        rest of the program. */
    class NullSource final : public GamepadSource
    {
    public:
        bool poll (PadState& out) override { out = PadState{}; return false; }
        void setRumble (float, float) override {}
        const char* name() const override { return "ingen kalla pa den har plattformen"; }
    };
}

std::unique_ptr<GamepadSource> GamepadSource::createForPlatform()
{
    // macOS lands here once v1 is done — GameController.framework rather than
    // XInput, same interface, one file.
    return std::make_unique<NullSource>();
}

#endif

} // namespace chordtroller

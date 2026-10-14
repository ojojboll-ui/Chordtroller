#include "GamepadSource.h"
#include "HandMapping.h"
#include "OscMessage.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstring>

namespace chordtroller
{

namespace
{
    constexpr int kDefaultPort = 9000;
    constexpr std::uint32_t kAliveMs   = 1500;  // handtrack sends /hands/alive twice a second
    constexpr std::uint32_t kHandMs    = 400;   // a hand not heard from in this long has gone
    constexpr std::uint32_t kRebindMs  = 1000;
    constexpr int           kMaxPacketsPerPoll = 64;

    int configuredPort()
    {
        const int p = juce::SystemStats::getEnvironmentVariable ("CHORDTROLLER_HAND_PORT", {}).getIntValue();
        return p > 0 && p < 65536 ? p : kDefaultPort;
    }

    hands::Gesture gestureFrom (const char* s) noexcept
    {
        if (std::strcmp (s, "open")  == 0) return hands::Gesture::open;
        if (std::strcmp (s, "fist")  == 0) return hands::Gesture::fist;
        if (std::strcmp (s, "point") == 0) return hands::Gesture::point;
        if (std::strcmp (s, "pinch") == 0) return hands::Gesture::pinch;
        return hands::Gesture::none;
    }

    /**
        Two hands from a webcam, arriving as OSC over UDP from the handtrack
        program. Lowest priority of the sources: a real pad that is plugged in
        wins, so a pad left on the desk never silently takes the instrument from
        the hands, and the other way round.

        Only ever bound to the loopback address. The stream is a controller, and
        nothing about it should be reachable from the network.

        poll() reads whatever has arrived and returns at once. Nothing here may
        wait: a camera that stops sending is reported as gone, never waited for.
    */
    class HandSource final : public GamepadSource
    {
    public:
        HandSource() : port (configuredPort()) {}

        bool poll (PadState& out) override
        {
            out = PadState{};
            const auto now = juce::Time::getMillisecondCounter();

            if (! bound && now - lastBindTry >= kRebindMs)
            {
                lastBindTry = now;
                bound = socket.bindToPort (port, "127.0.0.1");
                boundFlag.store (bound, std::memory_order_relaxed);
            }

            if (! bound)
                return false;

            for (int i = 0; i < kMaxPacketsPerPoll && socket.waitUntilReady (true, 0) == 1; ++i)
            {
                const int n = socket.read (buffer, (int) sizeof (buffer), false);

                if (n <= 0)
                    break;

                osc::parsePacket (buffer, (std::size_t) n, [this, now] (const osc::Message& m)
                                  { handle (m, now); });
            }

            if (now - lastPacketMs > kAliveMs)
                return false;

            hands::HandFeatures l = left, r = right;
            l.present = l.present && now - lastLeftMs  <= kHandMs;
            r.present = r.present && now - lastRightMs <= kHandMs;

            out = hands::map (l, r);

            // Smooth the right stick, and let go of it at once when the hand leaves.
            const float dt = lastPollMs == 0 ? 0.0f : std::min ((float) (now - lastPollMs), 50.0f) * 0.001f;
            lastPollMs = now;

            if (r.present)
            {
                out.ry = ySmooth.step (out.ry, dt, tuning.strumSmoothing);
                out.rx = xSmooth.step (out.rx, dt, tuning.strumSmoothing);
            }
            else
            {
                ySmooth.reset();
                xSmooth.reset();
            }

            return true;
        }

        void setRumble (float, float) override {}      // a hand cannot be pushed back on

        const char* name() const override
        {
            return boundFlag.load (std::memory_order_relaxed) ? "Hander (kamera)"
                                                              : "Hander (porten upptagen)";
        }

    private:
        static float arg (const osc::Message& m, int i) noexcept
        {
            return i < m.argc && m.args[i].kind == osc::Arg::Kind::number ? m.args[i].number : 0.0f;
        }

        void handle (const osc::Message& m, std::uint32_t now) noexcept
        {
            lastPacketMs = now;

            if (m.is ("/hands/alive"))
                return;

            hands::HandFeatures* h = nullptr;
            std::uint32_t* seen = nullptr;
            const char* rest = nullptr;

            if (std::strncmp (m.address, "/hand/left/", 11) == 0)
                { h = &left;  seen = &lastLeftMs;  rest = m.address + 11; }
            else if (std::strncmp (m.address, "/hand/right/", 12) == 0)
                { h = &right; seen = &lastRightMs; rest = m.address + 12; }
            else
                return;

            if (std::strcmp (rest, "present") == 0)
            {
                h->present = arg (m, 0) > 0.5f;

                if (! h->present)
                    *h = hands::HandFeatures{};
                else
                    *seen = now;

                return;
            }

            // Every other message is a hand that exists right now.
            h->present = true;
            *seen = now;

            if      (std::strcmp (rest, "pos")   == 0) { h->x = arg (m, 0); h->y = arg (m, 1); }
            else if (std::strcmp (rest, "vel")   == 0) { h->vx = arg (m, 0); h->vy = arg (m, 1); }
            else if (std::strcmp (rest, "curlvel") == 0) { h->curlVel = arg (m, 0); }
            else if (std::strcmp (rest, "pinch") == 0) { h->pinch = arg (m, 0); }
            else if (std::strcmp (rest, "open")  == 0) { h->openness = arg (m, 0); }
            else if (std::strcmp (rest, "gesture") == 0 && m.argc > 0
                     && m.args[0].kind == osc::Arg::Kind::text)
                h->gesture = gestureFrom (m.args[0].text);
        }

        const int port;
        juce::DatagramSocket socket { false };
        bool bound = false;
        std::atomic<bool> boundFlag { false };
        std::uint32_t lastBindTry = 0, lastPacketMs = 0, lastLeftMs = 0, lastRightMs = 0;

        hands::HandTuning tuning;
        hands::Smoother ySmooth, xSmooth;
        std::uint32_t lastPollMs = 0;

        hands::HandFeatures left, right;
        alignas (4) std::uint8_t buffer[4096] {};
    };
}

std::unique_ptr<GamepadSource> createHandSource()
{
    return std::make_unique<HandSource>();
}

} // namespace chordtroller

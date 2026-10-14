// Offline checks for the hand input: the OSC reader against packets made by
// python-osc itself, the reader against truncated and random bytes, and the
// hand-to-pad mapping with invented hands. No JUCE, no camera, no network.
//   build.bat handtest /fsanitize=address
#include "input/HandMapping.h"
#include "input/PadMath.h"
#include "input/OscMessage.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace chordtroller;

static int failures = 0;

#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define NEAR(a, b, eps) CHECK (std::fabs ((double) (a) - (double) (b)) <= (eps))

// Made with python-osc: /hand/right/pos 0.25 0.5 0.2
static const std::uint8_t posPacket[] = {
    0x2f, 0x68, 0x61, 0x6e, 0x64, 0x2f, 0x72, 0x69, 0x67, 0x68, 0x74, 0x2f, 0x70, 0x6f, 0x73, 0x00,
    0x2c, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x80, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00,
    0x3e, 0x4c, 0xcc, 0xcd };

// /hand/left/gesture "fist"
static const std::uint8_t gesturePacket[] = {
    0x2f, 0x68, 0x61, 0x6e, 0x64, 0x2f, 0x6c, 0x65, 0x66, 0x74, 0x2f, 0x67, 0x65, 0x73, 0x74, 0x75,
    0x72, 0x65, 0x00, 0x00, 0x2c, 0x73, 0x00, 0x00, 0x66, 0x69, 0x73, 0x74, 0x00, 0x00, 0x00, 0x00 };

// A bundle: /hand/right/vel 0.5 -1.5, then /hands/alive 1 (an int)
static const std::uint8_t bundlePacket[] = {
    0x23, 0x62, 0x75, 0x6e, 0x64, 0x6c, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x1c, 0x2f, 0x68, 0x61, 0x6e, 0x64, 0x2f, 0x72, 0x69, 0x67, 0x68, 0x74, 0x2f,
    0x76, 0x65, 0x6c, 0x00, 0x2c, 0x66, 0x66, 0x00, 0x3f, 0x00, 0x00, 0x00, 0xbf, 0xc0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x2f, 0x68, 0x61, 0x6e, 0x64, 0x73, 0x2f, 0x61, 0x6c, 0x69, 0x76, 0x65,
    0x00, 0x00, 0x00, 0x00, 0x2c, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

static std::vector<osc::Message> parse (const std::uint8_t* d, std::size_t n)
{
    std::vector<osc::Message> out;
    osc::parsePacket (d, n, [&] (const osc::Message& m) { out.push_back (m); });
    return out;
}

static void testOsc()
{
    auto a = parse (posPacket, sizeof posPacket);
    CHECK (a.size() == 1 && a[0].is ("/hand/right/pos") && a[0].argc == 3);
    NEAR (a[0].args[0].number, 0.25, 1e-6);
    NEAR (a[0].args[2].number, 0.2, 1e-6);

    auto g = parse (gesturePacket, sizeof gesturePacket);
    CHECK (g.size() == 1 && g[0].args[0].kind == osc::Arg::Kind::text);
    CHECK (std::strcmp (g[0].args[0].text, "fist") == 0);

    auto b = parse (bundlePacket, sizeof bundlePacket);
    CHECK (b.size() == 2 && b[0].is ("/hand/right/vel") && b[1].is ("/hands/alive"));
    NEAR (b[0].args[1].number, -1.5, 1e-6);
    NEAR (b[1].args[0].number, 1.0, 1e-6);
}

static void testOscNeverReadsPastTheEnd()
{
    // Every truncation of every packet. Run under AddressSanitizer this is what
    // proves the length checks; without it it still proves nothing throws.
    for (auto [d, n] : { std::pair { posPacket, sizeof posPacket },
                         std::pair { gesturePacket, sizeof gesturePacket },
                         std::pair { bundlePacket, sizeof bundlePacket } })
        for (std::size_t len = 0; len <= n; ++len)
        {
            std::vector<std::uint8_t> copy (d, d + len);          // exact size, so ASan sees the edge
            parse (copy.data(), copy.size());
        }

    std::mt19937 rng (7);
    for (int i = 0; i < 20000; ++i)
    {
        std::vector<std::uint8_t> junk (rng() % 96);
        for (auto& b : junk) b = (std::uint8_t) rng();
        if (! junk.empty() && i % 3 == 0) junk[0] = '/';
        if (junk.size() > 8 && i % 7 == 0) std::memcpy (junk.data(), "#bundle", 8);
        parse (junk.data(), junk.size());
    }

    // A message that declares a float it does not carry is dropped whole.
    std::vector<std::uint8_t> shortFloat (posPacket, posPacket + sizeof posPacket - 4);
    CHECK (parse (shortFloat.data(), shortFloat.size()).empty());
}

static hands::HandFeatures hand (float x, float y, float curlVel = 0.0f, float vy = 0.0f)
{
    hands::HandFeatures h;
    h.present = true;
    h.x = x;
    h.y = y;
    h.curlVel = curlVel;
    h.vy = vy;
    return h;
}

static void testMapping()
{
    const hands::HandTuning t;
    const hands::HandFeatures none;

    // No hands: a connected pad at rest, so the ring holds and the strum is still.
    auto rest = hands::map (none, none, t);
    CHECK (rest.connected && rest.lx == 0 && rest.ly == 0 && rest.ry == 0 && rest.rt == 0 && rest.buttons == 0);

    // Left hand: home is the centre of the ring, and directions follow the hand.
    auto home = hands::map (hand (t.leftHomeX, t.leftHomeY), none, t);
    NEAR (home.lx, 0, 1e-6); NEAR (home.ly, 0, 1e-6);
    auto up = hands::map (hand (t.leftHomeX, t.leftHomeY - 0.08f), none, t);      // image y is down
    CHECK (up.ly > 0.45f && std::fabs (up.lx) < 1e-6f);
    auto right = hands::map (hand (t.leftHomeX + 0.08f, t.leftHomeY), none, t);
    CHECK (right.lx > 0.45f);
    auto far = hands::map (hand (t.leftHomeX + 0.5f, t.leftHomeY - 0.5f), none, t);
    NEAR (std::sqrt (far.lx * far.lx + far.ly * far.ly), 1.0, 1e-5);              // stays in the gate

    // Left pinch squeezes LT; a fist is LB instead, and does not also damp.
    auto l = hand (t.leftHomeX, t.leftHomeY);
    l.pinch = 0.7f;
    NEAR (hands::map (l, none, t).lt, 0.7, 1e-6);
    l.gesture = hands::Gesture::fist;
    auto fist = hands::map (l, none, t);
    CHECK ((fist.buttons & padLB) != 0 && fist.lt == 0);

    // Right hand: it is the right stick. Above home strums up, below strums down,
    // and at home it is silent.
    using Src = hands::HandTuning::StrumSource;
    const float cx = t.rightHomeX, cy = t.rightHomeY;

    auto centered = hands::map (none, hand (cx, cy), t);
    CHECK (centered.ry == 0 && centered.rx == 0 && centered.rt > 0.5f);
    CHECK (hands::map (none, hand (cx, cy - 0.02f), t).ry == 0);                    // hovering near home is silent
    CHECK (hands::map (none, hand (cx, cy + 0.02f), t).ry == 0);

    auto above = hands::map (none, hand (cx, cy - 0.10f), t);
    auto below = hands::map (none, hand (cx, cy + 0.10f), t);
    CHECK (above.ry > 0.2f && below.ry < -0.2f);                                    // up is stick up
    NEAR (above.ry, -below.ry, 1e-6);
    CHECK (hands::map (none, hand (cx, cy - 0.16f), t).ry > above.ry);              // further is faster
    NEAR (hands::map (none, hand (cx, cy - 0.5f), t).ry, 1.0, 1e-6);                // and it tops out
    NEAR (hands::map (none, hand (cx, cy + 0.5f), t).ry, -1.0, 1e-6);

    // The strum goes on by itself: the same held position gives the same stick, with
    // no motion at all, which is what a camera that only sees 30 frames wants.
    CHECK (hands::map (none, hand (cx, cy - 0.10f, 0.0f, 0.0f), t).ry == above.ry);

    // The deflection has to survive the instrument's own deadzones to start a
    // strum, or a hand held a sensible distance from home would stay silent.
    // These are PadTuning::stickDeadzone and strumDeadzone.
    {
        const float r = hands::map (none, hand (cx, cy - 0.10f), t).ry;
        const float afterStick = (r - 0.10f) / 0.90f;
        const float afterStrum = (afterStick - 0.12f) / 0.88f;
        CHECK (afterStrum > 0.0f);
    }

    // Through the instrument's real strum curve: the silent zone ends exactly where
    // the strum begins, with no second dead stretch, and the rate climbs smoothly
    // from the engine's slowest to its fastest.
    {
        const pad::PadTuning pt;
        auto rateAt = [&] (float dy, const hands::HandTuning& tune)
        {
            const auto s = hands::map (none, hand (cx, cy - dy), tune);
            const auto dz = pad::applyDeadzone (s.rx, s.ry, pt.stickDeadzone);
            return pad::strumRate (pad::discToSquare (dz.x, dz.y).y, pt);
        };

        const float edge = t.rightRestZone * t.rightReachY;
        const float full = t.rightReachY;

        CHECK (rateAt (edge - 0.004f, t) == 0.0f);                       // inside the band: silent
        CHECK (rateAt (edge + 0.004f, t) >= pt.strumRateMin * 0.99f);    // just outside: already strumming
        CHECK (rateAt (edge + 0.004f, t) < pt.strumRateMin * 1.5f);      // ...and slowly
        NEAR (rateAt (full, t), pt.strumRateMax, 1e-3);

        float last = 0.0f;
        bool rising = true;
        for (int i = 0; i <= 40; ++i)
        {
            const float r = rateAt (edge + 0.004f + (full - edge - 0.004f) * (float) i / 40.0f, t);
            rising = rising && r >= last;
            last = r;
        }
        CHECK (rising);

        // The taper spends the first half of the travel on slow strums.
        hands::HandTuning linear = t;
        linear.strumTaper = 0.0f;
        const float mid = edge + (full - edge) * 0.5f;
        CHECK (rateAt (mid, t) < rateAt (mid, linear) * 0.75f);
        CHECK (rateAt (mid, t) > pt.strumRateMin);

        // A negative taper bends the other way: fast at once.
        hands::HandTuning quick = t;
        quick.strumTaper = -2.0f;
        CHECK (rateAt (mid, quick) > rateAt (mid, linear));

        // Both ends are the same whatever the taper.
        NEAR (rateAt (full, quick), pt.strumRateMax, 1e-3);
        NEAR (rateAt (full, linear), pt.strumRateMax, 1e-3);
    }

    // Sideways is the sweep window, around the same home.
    CHECK (hands::map (none, hand (cx + 0.1f, cy), t).rx > 0.5f);
    CHECK (hands::map (none, hand (cx - 0.1f, cy), t).rx < -0.5f);
    hands::HandTuning noSweep;
    noSweep.sweepFromHandX = false;
    CHECK (hands::map (none, hand (cx + 0.1f, cy), noSweep).rx == 0.0f);

    // Speed mode: palm speed, up is stick up. The hand's place no longer strums.
    hands::HandTuning speed;
    speed.strumSource = Src::speed;
    CHECK (hands::map (none, hand (cx, cy - 0.3f, 0.0f, 0.0f), speed).ry == 0);
    CHECK (hands::map (none, hand (cx, cy, 0.0f, -1.2f), speed).ry > 0.4f);
    CHECK (hands::map (none, hand (cx, cy, 0.0f, 1.2f), speed).ry < -0.4f);
    CHECK (hands::map (none, hand (cx, cy, 0.0f, 0.1f), speed).ry == 0);

    // Finger mode: closing is a downstroke, opening an upstroke.
    hands::HandTuning fin;
    fin.strumSource = Src::fingers;
    CHECK (hands::map (none, hand (cx, cy, 4.0f, 0.0f), fin).ry < -0.4f);
    CHECK (hands::map (none, hand (cx, cy, -4.0f, 0.0f), fin).ry > 0.4f);
    CHECK (hands::map (none, hand (cx, cy, 0.3f, 0.0f), fin).ry == 0);
    CHECK (hands::map (none, hand (cx, cy - 0.3f, 0.0f, -2.0f), fin).ry == 0);
}

static void testSmoother()
{
    const float tau = 0.08f;

    // Step response: 63 % after one time constant, rising, never past the target.
    hands::Smoother s;
    float last = 0.0f;
    bool rising = true, under = true;
    for (int i = 0; i < 20; ++i)                         // 20 ms in 1 ms steps
    {
        const float v = s.step (1.0f, 0.001f, tau);
        rising = rising && v > last;
        under = under && v < 1.0f;
        last = v;
    }
    CHECK (rising && under);

    hands::Smoother a, b;                                // 80 ms either way
    float va = 0, vb = 0;
    for (int i = 0; i < 20; ++i) va = a.step (1.0f, 0.004f, tau);       // poll interval 4 ms
    for (int i = 0; i < 8; ++i)  vb = b.step (1.0f, 0.010f, tau);       // or 10 ms
    NEAR (va, 1.0 - std::exp (-1.0), 1e-4);
    NEAR (vb, va, 1e-4);                                 // the glide does not depend on the poll rate

    // A wobble of a few per cent at camera rate comes out much smaller.
    hands::Smoother w;
    float lo = 1.0f, hi = -1.0f;
    for (int i = 0; i < 600; ++i)                         // 2.4 s of 4 ms polls
    {
        const bool high = (i / 8) % 2 == 0;               // flips every 32 ms, about a camera frame
        const float v = w.step (0.5f + (high ? 0.05f : -0.05f), 0.004f, tau);
        if (i > 300) { lo = std::min (lo, v); hi = std::max (hi, v); }
    }
    CHECK (hi - lo < 0.04f);                              // from 0.10 peak to peak

    hands::Smoother off;
    NEAR (off.step (0.7f, 0.004f, 0.0f), 0.7, 1e-6);     // tau 0: no smoothing at all
    off.reset();
    CHECK (off.value == 0.0f);
}

int main()
{
    testSmoother();
    testOsc();
    testOscNeverReadsPastTheEnd();
    testMapping();
    std::printf (failures == 0 ? "handtest: all passed\n" : "handtest: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}

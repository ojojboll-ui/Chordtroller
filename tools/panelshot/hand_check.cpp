// The whole hand path with nothing faked but the camera: OSC packets go over a
// real UDP socket to the real HandSource, through the reader thread and the real
// processor, and what is checked is the MIDI that reaches the host.
#include "PluginProcessor.h"
#include "input/GamepadSource.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace chordtroller
{
    // Stands in for GamepadSource.cpp: the hands are the only source.
    std::unique_ptr<GamepadSource> GamepadSource::createForPlatform() { return createHandSource(); }
}

using namespace chordtroller;

namespace
{
    int failures = 0;
    constexpr int kPort = 9123;

    void check (bool ok, const char* what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FEL ", what);
        if (! ok) ++failures;
    }

    // --- a minimal OSC writer, independent of the reader under test -------------
    void putString (juce::MemoryBlock& b, const char* s)
    {
        const auto n = std::strlen (s) + 1;
        b.append (s, n);
        const char zero[4] = {};
        b.append (zero, (4 - n % 4) % 4);
    }

    void putFloat (juce::MemoryBlock& b, float f)
    {
        std::uint32_t v;
        std::memcpy (&v, &f, 4);
        const unsigned char be[4] = { (unsigned char) (v >> 24), (unsigned char) (v >> 16),
                                      (unsigned char) (v >> 8),  (unsigned char) v };
        b.append (be, 4);
    }

    juce::DatagramSocket sender (false);

    void send (const char* address, std::initializer_list<float> floats)
    {
        juce::MemoryBlock b;
        putString (b, address);
        std::string tags = ",";
        tags.append (floats.size(), 'f');
        putString (b, tags.c_str());
        for (float f : floats) putFloat (b, f);
        sender.write ("127.0.0.1", kPort, b.getData(), (int) b.getSize());
    }

    void sendText (const char* address, const char* text)
    {
        juce::MemoryBlock b;
        putString (b, address);
        putString (b, ",s");
        putString (b, text);
        sender.write ("127.0.0.1", kPort, b.getData(), (int) b.getSize());
    }

    struct Hand { bool present; float x, y, vy, pinch; const char* gesture; };

    void sendHand (const char* id, const Hand& h)
    {
        char a[64];
        auto addr = [&] (const char* leaf) { std::snprintf (a, sizeof a, "/hand/%s/%s", id, leaf); return a; };

        if (! h.present)
        {
            send (addr ("present"), { 0.0f });
            return;
        }

        send (addr ("pos"), { h.x, h.y, 0.2f });
        send (addr ("vel"), { 0.0f, h.vy });
        send (addr ("pinch"), { h.pinch });
        sendText (addr ("gesture"), h.gesture);
    }

    // The camera sends about every 33 ms; the source takes whatever has arrived at
    // each 4 ms poll, so a few frames and a short wait are enough to settle.
    void frames (const Hand& left, const Hand& right, int n = 6)
    {
        for (int i = 0; i < n; ++i)
        {
            send ("/hands/alive", { 1.0f });
            sendHand ("left", left);
            sendHand ("right", right);
            juce::Thread::sleep (33);
        }
    }

    struct Host
    {
        ChordtrollerAudioProcessor& proc;
        juce::AudioBuffer<float> buffer { 2, 512 };
        std::vector<int> ons;
        double sumSquares = 0.0; long samples = 0;

        void run (double seconds)
        {
            for (int b = 0, n = (int) std::ceil (seconds * 48000.0 / 512.0); b < n; ++b)
            {
                juce::MidiBuffer midi;
                proc.processBlock (buffer, midi);

                for (const auto m : midi)
                    if (m.getMessage().isNoteOn())
                        ons.push_back (m.getMessage().getNoteNumber());

                for (int i = 0; i < 512; ++i)
                    sumSquares += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
                samples += 512;
            }
        }

        double takeRms()
        {
            const double r = std::sqrt (sumSquares / std::max (1L, samples));
            sumSquares = 0.0; samples = 0;
            return r;
        }

        std::set<int> pitchClasses() const
        {
            std::set<int> s;
            for (int n : ons) s.insert (n % 12);
            return s;
        }

        void print (const char* label) const
        {
            std::printf ("  %s  on:", label);
            for (int n : ons) std::printf (" %d", n);
            std::printf ("\n");
        }
    };

    const Hand none { false, 0, 0, 0, 0, "none" };
    // Left hand well above home: the ring's top sector, C major. Right hand at its home.
    Hand leftAt (float dx, float dy, const char* g = "open", float pinch = 0.0f)
    {
        return { true, 0.28f + dx, 0.55f + dy, 0.0f, pinch, g };
    }
    // The right hand held dy above (negative) or below (positive) its home: a stick pushed up or down.
    Hand rightAt (float dy) { return { true, 0.72f, 0.5f + dy, 0.0f, 0.0f, "open" }; }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    _putenv_s ("CHORDTROLLER_HAND_PORT", "9123");        // before the reader thread creates the source

    ChordtrollerAudioProcessor processor;
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::chordtroller);
    processor.prepareToPlay (48000.0, 512);

    Host host { processor };
    juce::Thread::sleep (150);

    std::printf ("\n1. Programmet startat men inga hander i bild\n");
    frames (none, none);
    host.run (0.5);
    check (host.ons.empty() && host.takeRms() < 1e-4, "tyst, inga toner");

    std::printf ("\n2. Vanster hand uppat (C-dur), hoger hand hallen over mitten\n");
    host.ons.clear();
    frames (leftAt (0.0f, -0.2f), rightAt (-0.14f));
    host.run (1.0);
    host.print ("nedslag");
    {
        bool cMajor = ! host.ons.empty();
        for (int pc : host.pitchClasses()) cMajor = cMajor && (pc == 0 || pc == 4 || pc == 7);
        check (cMajor, "bara C-durs toner");
        check (host.ons.size() >= 3, "en hand ovanfor mitten strummar av sig sjalv");
        check (host.takeRms() > 0.005, "det hors");
    }

    // A faster hand is a faster strum. Same tuning, hand twice as quick.
    {
        Host faster { processor };
        frames (leftAt (0.0f, -0.2f), rightAt (-0.24f));
        faster.run (1.0);
        faster.print ("dubbel fart");
        check (faster.ons.size() > host.ons.size(), "langre fran mitten ger fler toner pa samma tid");
    }

    std::printf ("\n3. Handen hallen under mitten\n");
    host.ons.clear();
    frames (leftAt (0.0f, -0.2f), rightAt (0.14f));
    host.run (0.6);
    host.print ("uppslag");
    check (! host.ons.empty(), "under mitten strummar ocksa");

    std::printf ("\n4. Handen i mitten\n");
    frames (leftAt (0.0f, -0.2f), rightAt (0.0f));
    host.ons.clear();
    host.run (0.6);
    check (host.ons.empty(), "handen i mitten ar tyst");

    std::printf ("\n5. Vanster hand at hoger byter ackord\n");
    host.ons.clear();
    frames (leftAt (0.25f, 0.0f), rightAt (-0.14f));
    host.run (1.0);
    host.print ("annat ackord");
    check (! host.ons.empty() && host.pitchClasses() != std::set<int> { 0, 4, 7 }, "ett annat ackord an C-dur");

    std::printf ("\n5b. Handen langt at sidan\n");
    host.ons.clear();
    {
        Hand drifting = rightAt (-0.14f);
        drifting.x = 0.40f;
        frames (leftAt (0.25f, 0.0f), drifting);
        host.run (1.0);
        host.print ("sidled");
        check (host.ons.size() >= 3, "omfanget smalnar men strummen gar");
    }

    std::printf ("\n6. Hoger hand lamnar bilden\n");
    frames (leftAt (0.25f, 0.0f), none);
    host.ons.clear();
    host.run (0.6);
    check (host.ons.empty(), "ingen strum utan hand");

    std::printf ("\n7. Programmet slutar skicka\n");
    frames (leftAt (0.0f, -0.2f), rightAt (-0.14f), 3);
    juce::Thread::sleep (2200);                       // longer than the alive timeout
    host.ons.clear();
    host.run (0.6);
    // Notes already struck keep ringing, so only new plucks are the hands' business:
    // a last frame showing a fast stroke must not keep strumming for ever.
    check (host.ons.empty(), "strumningen slutar nar strommen tystnar");

    std::printf (failures == 0 ? "\nAlla kontroller klarade.\n" : "\n%d kontroll(er) FEL.\n", failures);
    return failures == 0 ? 0 : 1;
}

// Plays the bowed strings through the real processor with a scripted pad, and
// checks what reaches the host: the notes, their releases, and the sound.
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>

namespace chordtroller
{
    extern std::mutex fakePadLock;
    extern PadState   fakePad;
}

using namespace chordtroller;

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FEL ", what);
        if (! ok) ++failures;
    }

    void setPad (const std::function<void (PadState&)>& change)
    {
        {
            const std::lock_guard<std::mutex> lock (fakePadLock);
            change (fakePad);
        }

        juce::Thread::sleep (25);     // several polls at 250 Hz
    }

    void press (std::uint16_t button)
    {
        setPad ([button] (PadState& s) { s.buttons |= button; });
        setPad ([button] (PadState& s) { s.buttons &= (std::uint16_t) ~button; });
    }

    struct Host
    {
        ChordtrollerAudioProcessor& proc;
        juce::AudioBuffer<float> buffer { 2, 512 };
        std::multiset<int> sounding;
        std::vector<int> ons, offs;
        double sumSquares = 0.0; long samples = 0; float peak = 0.0f;

        void run (double seconds)
        {
            const int blocks = (int) std::ceil (seconds * 48000.0 / 512.0);

            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                proc.processBlock (buffer, midi);

                for (const auto m : midi)
                {
                    const auto msg = m.getMessage();

                    if (msg.isNoteOn())
                    {
                        ons.push_back (msg.getNoteNumber());
                        sounding.insert (msg.getNoteNumber());
                    }
                    else if (msg.isNoteOff())
                    {
                        offs.push_back (msg.getNoteNumber());
                        const auto it = sounding.find (msg.getNoteNumber());
                        if (it != sounding.end()) sounding.erase (it);
                    }
                }

                for (int i = 0; i < 512; ++i)
                {
                    const float v = buffer.getSample (0, i);
                    sumSquares += (double) v * v;
                    peak = std::max (peak, std::abs (v));
                }

                samples += 512;
            }
        }

        double takeRms()
        {
            const double r = std::sqrt (sumSquares / std::max (1L, samples));
            sumSquares = 0.0; samples = 0; peak = 0.0f;
            return r;
        }

        void clearMidi() { ons.clear(); offs.clear(); }

        bool onlyOns (std::initializer_list<int> notes) const
        {
            return std::multiset<int> (ons.begin(), ons.end()) == std::multiset<int> (notes);
        }

        bool onlyOffs (std::initializer_list<int> notes) const
        {
            return std::multiset<int> (offs.begin(), offs.end()) == std::multiset<int> (notes);
        }

        void print (const char* label) const
        {
            std::printf ("  %s  on:", label);
            for (int n : ons) std::printf (" %d", n);
            std::printf ("  off:");
            for (int n : offs) std::printf (" %d", n);
            std::printf ("  klingar:");
            for (int n : sounding) std::printf (" %d", n);
            std::printf ("\n");
        }
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    ChordtrollerAudioProcessor processor;
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::strings);
    processor.prepareToPlay (48000.0, 512);

    Host host { processor };
    juce::Thread::sleep (100);

    std::printf ("\n1. Vila\n");
    host.run (0.3);
    const double quiet = host.takeRms();
    check (quiet < 1e-4 && host.ons.empty(), "tyst och inga toner med kontrollen i vila");

    std::printf ("\n2. Strakdrag pa grundtonen (C-dur, fiol: C4)\n");
    setPad ([] (PadState& s) { s.ly = 1.0f; s.ry = 0.8f; s.rt = 0.5f; });
    host.run (1.0);
    const double bowed = host.takeRms();
    host.print ("drag");
    check (bowed > 0.01, "strakdraget hors");
    check (host.onlyOns ({ 60 }) && host.offs.empty(), "en ton, C4");

    std::printf ("\n3. Legato till tersen\n");
    host.clearMidi();
    setPad ([] (PadState& s) { s.lx = 1.0f; s.ly = 0.0f; });
    host.run (0.5);
    host.print ("legato");
    check (host.onlyOffs ({ 60 }) && host.onlyOns ({ 64 }), "C4 av, E4 pa");

    std::printf ("\n4. Tummen slapper vanster spak\n");
    host.clearMidi();
    setPad ([] (PadState& s) { s.lx = 0.0f; });
    host.run (0.3);
    check (host.ons.empty() && host.offs.empty(), "fingret ligger kvar: ingen ny ton");

    std::printf ("\n5. Straken stannar\n");
    host.takeRms();
    host.clearMidi();
    setPad ([] (PadState& s) { s.ry = 0.0f; });
    host.run (0.1);
    host.takeRms();
    host.run (0.5);
    const double after = host.takeRms();
    host.print ("stopp");
    check (host.onlyOffs ({ 64 }), "E4 slapps");
    // The string itself stops within 0.3 s (the engine test measures that);
    // what is left here is the room, which the strings play into on purpose.
    std::printf ("  efter 0.1-0.6 s: %.1f dB under strakdraget\n", 20.0 * std::log10 (bowed / std::max (after, 1e-9)));
    check (after < bowed * 0.1, "bara rummets svans kvar: minst 20 dB ner");

    std::printf ("\n6. Bordun: LS laser E4, sedan grundtonen med straken\n");
    host.clearMidi();
    press (padLS);
    setPad ([] (PadState& s) { s.ly = 1.0f; s.ry = -0.8f; });
    host.run (0.6);
    host.print ("bordun");
    check (host.onlyOns ({ 60, 64 }), "C4 och bordunen E4");

    std::printf ("\n7. RB: oktav upp\n");
    host.clearMidi();
    setPad ([] (PadState& s) { s.buttons |= padRB; });
    host.run (0.4);
    host.print ("oktav");
    check (host.onlyOffs ({ 60 }) && host.onlyOns ({ 72 }), "C4 av, C5 pa, bordunen ligger kvar");

    std::printf ("\n8. RS: pizzicato\n");
    host.clearMidi();
    press (padRS);
    host.run (0.1);
    host.print ("byte");
    check (host.onlyOffs ({ 72, 64 }) && host.ons.empty(), "strakade toner slapps, ingen knapp av bytet");

    host.clearMidi();
    setPad ([] (PadState& s) { s.ry = 0.0f; });
    host.run (0.1);
    host.takeRms();
    setPad ([] (PadState& s) { s.ry = 1.0f; });
    host.run (0.3);
    const double plucked = host.takeRms();
    host.print ("knapp");
    check (host.onlyOns ({ 72, 64 }), "snarten knapper C5 och bordunen");
    check (plucked > 0.003, "knappningen hors");

    host.clearMidi();
    host.run (0.5);
    check (host.ons.empty(), "spaken kvar ute: ingen ny knappning");

    std::printf ("\n9. Byte till ett annat instrument\n");
    host.clearMidi();
    setPad ([] (PadState& s) { s = {}; });     // or the Juno would start a phrase of its own
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::juno);
    host.run (0.1);
    host.print ("byte");
    check (host.sounding.empty(), "inga toner hangande i varden");

    std::printf ("\n10. Arpeggiatorn\n");
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::arp);
    host.run (0.1);
    host.clearMidi();
    host.takeRms();

    // C major, RT all the way: up, thirty-seconds.
    setPad ([] (PadState& s) { s = {}; s.ly = 1.0f; s.rt = 1.0f; });
    host.run (1.0);
    const double arpLevel = host.takeRms();
    host.print ("upp");
    {
        bool allC = ! host.ons.empty();
        for (int n : host.ons) allC = allC && (n % 12 == 0 || n % 12 == 4 || n % 12 == 7);
        check (allC, "bara C-durs toner");
        check (host.ons.size() >= 12 && host.ons.size() <= 14, "RT i botten: trettiotvadelar, 12,8 pa en sekund vid appens 96 BPM");
        check (arpLevel > 0.005, "den hors");
    }

    // RT halfway is sixteenths.
    host.clearMidi();
    setPad ([] (PadState& s) { s.rt = 0.6f; });
    host.run (1.0);
    check (host.ons.size() >= 6 && host.ons.size() <= 8, "RT halvvags: sextondelar, 6,4 pa en sekund");

    // The right stick leaning left keeps it to the lowest octave.
    host.clearMidi();
    setPad ([] (PadState& s) { s.rx = -1.0f; });
    host.run (1.0);
    {
        int top = 0;
        for (int n : host.ons) top = std::max (top, n);
        check (! host.ons.empty() && top <= 55, "hoger spak at vanster: bara lagsta oktaven");
    }

    // Y flips between UPP and OKTAVHOPP; A to NER.
    host.clearMidi();
    setPad ([] (PadState& s) { s.rx = 0.0f; });
    press (padA);
    host.clearMidi();
    host.run (0.6);
    host.print ("ner");
    {
        bool falling = host.ons.size() >= 3;
        for (size_t i = 1; i < host.ons.size() && i < 3; ++i)
            falling = falling && (host.ons[i] < host.ons[i - 1] || host.ons[i - 1] == 48);
        check (falling, "A: nedat");
    }

    // The sound changes mid-run without leaving a note behind.
    press (padDpadRight);
    press (padDpadRight);
    host.run (0.5);

    host.clearMidi();
    setPad ([] (PadState& s) { s.rt = 0.0f; });
    host.run (2.0);
    host.print ("slapp");
    check (host.ons.empty() && host.sounding.empty(), "RT slappt: tystnar, inga toner kvar");

    // RS holds it, and switching instrument lets go of everything.
    setPad ([] (PadState& s) { s.rt = 0.6f; });
    host.run (0.3);
    press (padRS);
    setPad ([] (PadState& s) { s.rt = 0.0f; });
    host.clearMidi();
    host.run (0.5);
    check (! host.ons.empty(), "RS haller arpeggiot med RT slappt");

    host.clearMidi();
    setPad ([] (PadState& s) { s = {}; });
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::theremin);
    host.run (0.2);
    host.print ("byte");
    check (host.sounding.empty(), "byte av instrument: inga hangande toner");

    std::printf ("\nKLART: %d fel\n", failures);
    return failures;
}

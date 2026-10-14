// Offline test of the arpeggiator. No JUCE, no pad.
#include "dsp/ArpEngine.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <xmmintrin.h>

using namespace chordtroller::dsp;
using chordtroller::music::Chord;
using chordtroller::music::Quality;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FEL ", what);
    if (! ok) ++failures;
}

static const double fs = 48000.0;

struct Take
{
    std::vector<float> audio;
    std::vector<std::pair<long, int>> ons, offs;   // sample, note
    std::map<int, int> sounding;
    int strayOffs = 0;
};

// The note list is opened at the end of each block, so whatever the controls
// do between blocks lands in the next one -- the order the processor uses.
static void run (ArpEngine& e, Take& t, double seconds, int block = 256)
{
    const long total = (long) (seconds * fs);
    for (long done = 0; done < total; done += block)
    {
        const int n = (int) std::min<long> (block, total - done);
        const long base = (long) t.audio.size();
        for (int i = 0; i < n; ++i) t.audio.push_back (e.processSample());
        for (int i = 0; i < e.eventsThisBlock(); ++i)
        {
            const auto& ev = e.event (i);
            if (ev.on) { t.ons.push_back ({ base + ev.offset, ev.note }); ++t.sounding[ev.note]; }
            else
            {
                t.offs.push_back ({ base + ev.offset, ev.note });
                if (t.sounding[ev.note] > 0) --t.sounding[ev.note]; else ++t.strayOffs;
            }
        }
        e.beginBlock();
    }
}

static int stillOn (const Take& t) { int n = 0; for (auto& kv : t.sounding) n += kv.second; return n; }

static bool finite (const Take& t)
{
    for (float v : t.audio) if (! std::isfinite (v) || std::abs (v) > 4.0f) return false;
    return true;
}

static double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0; for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, to - from));
}

static void setup (ArpEngine& e, int sound = 0)
{
    e.prepare (fs); e.seed (99);
    e.beginBlock();
    e.setTempo (120.0);
    e.setChord ({ 0, Quality::major });     // C: C E G
    e.setRegister (0);
    e.setOctaves (1);
    e.setSound (sound);
    e.setGate (0.5f);
    e.setCutoff (0.6f);
}

static std::string seq (const Take& t, size_t count)
{
    std::string s;
    for (size_t i = 0; i < std::min (count, t.ons.size()); ++i) s += std::to_string (t.ons[i].second) + " ";
    return s;
}

int main()
{
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);

    std::printf ("\n1. Satten pa C-dur, en oktav (C4=60? nej: C3 = 48, E3 52, G3 55)\n");
    const char* expect[kArpModes] {
        "48 52 55 48 52 55 ",          // UPP
        "48 52 48 55 48 52 ",          // PEDAL
        "48 52 55 52 48 52 ",          // PENDEL
        "48 55 52 48 55 52 ",          // KONVERGERA
        "55 52 48 55 52 48 ",          // NER
        "48 60 52 64 55 67 ",          // OKTAVHOPP
        nullptr,                       // SLUMP
        nullptr };                     // ACKORD
    for (int m = 0; m < kArpModes; ++m)
    {
        ArpEngine e; setup (e);
        e.setMode (m); e.setRunning (true, 1);
        Take t; run (e, t, 2.0);
        const auto s = seq (t, 6);
        std::printf ("  %-10s %s\n", arpModeName (m), s.c_str());
        if (expect[m] != nullptr) check (s == expect[m], arpModeName (m));
    }

    {
        ArpEngine e; setup (e); e.setMode (6); e.setRunning (true, 1);
        Take t; run (e, t, 4.0);
        int repeats = 0; for (size_t i = 1; i < t.ons.size(); ++i) repeats += t.ons[i].second == t.ons[i - 1].second;
        check (repeats == 0 && t.ons.size() > 20, "SLUMP: aldrig samma ton tva ganger i rad");
    }
    {
        ArpEngine e; setup (e); e.setMode (7); e.setRunning (true, 0);
        Take t; run (e, t, 0.1);
        check (t.ons.size() == 3, "ACKORD: alla tre tonerna pa en gang");
    }

    std::printf ("\n2. Fart och rutnat, 120 BPM\n");
    for (int r = 0; r < kArpRates; ++r)
        for (bool trip : { false, true })
        {
            ArpEngine e; setup (e); e.setTriplet (trip); e.setMode (0); e.setRunning (true, r);
            Take t; run (e, t, 2.0);
            const double expected = 0.5 / std::pow (2.0, r) * (trip ? 2.0 / 3.0 : 1.0) * 0.5 * fs;   // samples per step
            double worst = 0;
            for (size_t i = 1; i < t.ons.size(); ++i) worst = std::max (worst, std::abs ((t.ons[i].first - t.ons[i - 1].first) - expected));
            std::printf ("  %-6s %3zu toner, avstand %.0f samplar, varst %.1f fel\n", arpRateName (r, trip), t.ons.size(), expected, worst);
            check (worst <= 1.0, "jamnt pa rutnatet");
        }

    std::printf ("\n3. Rytmer\n");
    for (int rh = 0; rh < kArpRhythms; ++rh)
    {
        ArpEngine e; setup (e); e.setRhythm (rh); e.setMode (0); e.setRunning (true, 1);
        Take t; run (e, t, 1.99);      // just under 2 s: 16 sixteenths
        std::printf ("  %-10s %2zu toner pa 16 steg\n", arpRhythmName (rh), t.ons.size());
        if (rh == 2) check (t.ons.size() == 6, "PUNKTERAD: 3+3+2 ger 6 slag pa 16");
        if (rh == 3) check (t.ons.size() == 10, "GATE: 10 slag pa 16");
        if (rh == 2) check (seq (t, 3) == "48 52 55 ", "en paus hoppar inte over nasta ton");
        if (rh == 1)
        {
            const double step = 0.25 * 0.5 * fs;
            const long gap1 = t.ons[1].first - t.ons[0].first, gap2 = t.ons[2].first - t.ons[1].first;
            std::printf ("    swing: %ld och %ld samplar\n", gap1, gap2);
            check (std::abs (gap1 - step * 4.0 / 3.0) <= 1 && std::abs (gap2 - step * 2.0 / 3.0) <= 1, "SWING: lang-kort, 2:1");
        }
    }

    std::printf ("\n4. Tonlangd (LT)\n");
    for (float lt : { 0.0f, 0.5f, 1.0f })
    {
        ArpEngine e; setup (e); e.setGate (lt); e.setMode (0); e.setRunning (true, 1);
        Take t; run (e, t, 1.0);
        const double len = t.offs.empty() ? 0 : (double) (t.offs[0].first - t.ons[0].first);
        std::printf ("  LT %.1f: forsta tonen %.0f samplar av %.0f\n", lt, len, 0.125 * fs);
    }

    std::printf ("\n5. Oktaver, ackordbyte, stopp och hall\n");
    {
        ArpEngine e; setup (e);
        e.cycleOctaves(); e.cycleOctaves();          // 3 octaves
        e.setMode (0); e.setRunning (true, 2);
        Take t; run (e, t, 1.0);
        int top = 0; for (auto& o : t.ons) top = std::max (top, o.second);
        check (e.poolSize() == 9 && top == 79, "tre oktaver: nio toner, hogst G5");

        e.setChord ({ 9, Quality::minor });          // Am
        Take t2; run (e, t2, 0.5);
        bool allAm = true; for (auto& o : t2.ons) { const int pc = o.second % 12; allAm = allAm && (pc == 9 || pc == 0 || pc == 4); }
        check (allAm, "ackordbyte mitt i: bara Am-toner efter");

        e.setRunning (false, 0);
        Take t3; run (e, t3, 1.0);
        check (t3.ons.empty() && stillOn (t3) == 0, "slappt spak: tystnar, inga hangande toner");
        check (! e.isPlaying(), "och star still");

        e.toggleHold(); e.setMode (2); e.setRunning (true, 1); e.setRunning (false, 0);
        Take t4; run (e, t4, 1.0);
        check (t4.ons.size() >= 7, "hall (RS): fortsatter efter slappt spak");
    }

    std::printf ("\n5b. Omfanget (hoger spak i sidled), tre oktaver C-dur\n");
    {
        auto notesWith = [] (float lean)
        {
            ArpEngine e; setup (e); e.setOctaves (3); e.setZone (lean, 0.1f);
            e.setMode (0); e.setRunning (true, 2);
            Take t; run (e, t, 1.0);
            int lo = 999, hi = 0;
            for (auto& o : t.ons) { lo = std::min (lo, o.second); hi = std::max (hi, o.second); }
            return std::make_pair (lo, hi);
        };
        const auto mid = notesWith (0.0f), low = notesWith (-1.0f), high = notesWith (1.0f), half = notesWith (-0.5f);
        std::printf ("  i vila %d-%d, vanster %d-%d, hoger %d-%d, halvvags vanster %d-%d\n",
                     mid.first, mid.second, low.first, low.second, high.first, high.second, half.first, half.second);
        check (mid.first == 48 && mid.second == 79, "i vila: hela spannet");
        check (low.first == 48 && low.second == 55, "fullt vanster: lagsta oktaven");
        check (high.first == 72 && high.second == 79, "fullt hoger: hogsta oktaven");
        check (half.first == 48 && half.second == 67, "halvvags: smalare, fast i den lagre anden");
    }

    std::printf ("\n6. Alla ljud: hors, ingen NaN, alla toner slapps, samma niva\n");
    double lo = 1e9, hi = -1e9;
    for (int s = 0; s < kArpSounds; ++s)
    {
        ArpEngine e; setup (e, s); e.setGate (0.5f); e.setMode (0); e.setRunning (true, 1);
        Take t; run (e, t, 2.0);
        e.setRunning (false, 0);
        run (e, t, 3.0);
        const double level = 20 * std::log10 (rms (t.audio, (size_t) (0.25 * fs), (size_t) (2.0 * fs)) + 1e-12);
        lo = std::min (lo, level); hi = std::max (hi, level);
        double peak = 0; for (float v : t.audio) peak = std::max (peak, (double) std::abs (v));
        std::printf ("  %-9s %6.1f dB  topp %.2f  on %zu off %zu  kvar %d\n", arpSound (s).name, level, peak, t.ons.size(), t.offs.size(), stillOn (t));
        check (finite (t) && level > -40, "spelar");
        check (stillOn (t) == 0 && t.strayOffs == 0, "varje not-on far sin not-off");
        const double tail = rms (t.audio, t.audio.size() - (size_t) fs / 2, t.audio.size());
        check (tail < 1e-3, "tyst nar den slutat");
    }
    std::printf ("  spann %.1f dB\n", hi - lo);
    check (hi - lo < 4.0, "ljuden inom 4 dB av varandra");

    std::printf ("\n7. Varden\n");
    {
        ArpEngine e; setup (e);
        Take t;
        double ppq = 3.37;                            // host somewhere mid-beat
        const int block = 256;
        for (int b = 0; b < (int) (2.0 * fs / block); ++b)
        {
            e.followHost (true, ppq);
            e.beginBlock();
            if (b == 0) e.setMode (0); e.setRunning (true, 1);   // sixteenths, pushed once the host is followed
            const long base = (long) t.audio.size();
            for (int i = 0; i < block; ++i) t.audio.push_back (e.processSample());
            for (int i = 0; i < e.eventsThisBlock(); ++i) if (e.event (i).on) t.ons.push_back ({ base + e.event (i).offset, e.event (i).note });
            ppq += block / fs * 2.0;
        }
        double worst = 0;
        for (auto& o : t.ons)
        {
            const double at = 3.37 + o.first / fs * 2.0;          // in beats
            const double off = std::abs (at * 4.0 - std::round (at * 4.0));
            worst = std::max (worst, off);
        }
        std::printf ("  %zu toner, varst %.4f sextondel fran vardens rutnat\n", t.ons.size(), worst);
        check (worst < 0.01, "pa vardens sextondelar");
    }

    std::printf ("\n8. Blockstorleken far inte andra nagot\n");
    for (int s : { 0, 4, 8, 9 })
    {
        auto play = [s] (int block)
        {
            ArpEngine e; setup (e, s); e.setDrums (true); e.setMode (2); e.setRunning (true, 1);
            Take t; run (e, t, 1.0, block);
            e.setChord ({ 7, Quality::dominant7 }); e.setGate (1.0f); e.setMode (5); e.setRunning (true, 2);
            run (e, t, 1.0, block);
            return t;
        };
        const Take a = play (64), b = play (512);
        double worst = 0;
        for (size_t i = 0; i < std::min (a.audio.size(), b.audio.size()); ++i) worst = std::max (worst, (double) std::abs (a.audio[i] - b.audio[i]));
        std::printf ("  %-9s %g\n", arpSound (s).name, worst);
        check (worst < 1e-6 && a.ons == b.ons, "64 och 512 ger samma");
    }

    std::printf ("\n9. Slumpade kontroller i 30 s\n");
    {
        ArpEngine e; e.prepare (fs); e.seed (5);
        std::uint32_t rnd = 777;
        auto next = [&rnd] { rnd = rnd * 1664525u + 1013904223u; return (rnd >> 8) / 16777216.0f; };
        Take t;
        for (int b = 0; b < (int) (30 * fs / 256); ++b)
        {
            if (next() < 0.05f) e.setSound ((int) (next() * kArpSounds));
            if (next() < 0.1f)  e.setChord ({ (int) (next() * 12), (Quality) (int) (next() * 7) });
            if (next() < 0.2f)  { e.setMode ((int) (next() * 8)); e.setRunning (next() > 0.2f, (int) (next() * 3)); }
            if (next() < 0.1f)  e.setZone (next() * 2 - 1, 0.1f);
            if (next() < 0.1f)  e.setGate (next());
            if (next() < 0.1f)  e.setCutoff (next());
            if (next() < 0.03f) e.setRhythm ((int) (next() * 6));
            if (next() < 0.03f) e.setTriplet (next() < 0.5f);
            if (next() < 0.02f) e.cycleOctaves();
            if (next() < 0.02f) e.toggleHold();
            if (next() < 0.02f) e.setDrums (next() < 0.5f);
            if (next() < 0.02f) e.setRegister ((int) (next() * 5) - 2);
            run (e, t, 256 / fs);
        }
        e.setRunning (false, 0); if (e.isHolding()) e.toggleHold(); e.setRunning (false, 0);
        run (e, t, 6.0);
        { double pk = 0; size_t at = 0; for (size_t i = 0; i < t.audio.size(); ++i) if (std::abs (t.audio[i]) > pk) { pk = std::abs (t.audio[i]); at = i; } std::printf ("  hogsta topp %.2f vid %.2f s\n", pk, at / fs); }
        check (finite (t), "inget NaN, ingen topp over 4");
        std::printf ("  %zu on, %zu off, kvar %d, losa off %d\n", t.ons.size(), t.offs.size(), stillOn (t), t.strayOffs);
        check (stillOn (t) == 0, "inga hangande toner");
    }

    std::printf ("\nKLART: %d fel\n", failures);
    return failures;
}

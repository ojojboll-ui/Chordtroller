#pragma once

#include "AcidVoice.h"
#include "BowedString.h"
#include "DrumKit.h"
#include "JunoVoice.h"
#include "PluckVoice.h"
#include "StringEngine.h"
#include "VoiceSpec.h"
#include "../music/ChordRing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace chordtroller::dsp
{

/**
    The sixth instrument: an arpeggiator played on the chord ring.

    The left thumb holds a chord. The right trigger plays it and says how fast
    — an eighth, a sixteenth or a thirty-second, three wide steps of its travel
    — and letting go stops it, unless the right stick click is holding it. The
    right stick is the range, the Omnichord's zone: at rest the whole span,
    leaning narrows it to the low or the high end, down to one octave. Its
    other axis is how long each note is held. The pattern itself is a button,
    because nothing about choosing one wants a thumb held out in a direction.

    It owns no sound of its own. Every voice is borrowed: the Omnichord plate's
    four, four of the Juno's patches, the 303 and the bowed strings' physical
    pizzicato. Each family is driven the way its own instrument drives it, and
    one trim per family puts them level with each other.
*/

inline constexpr int kArpModes   = 8;
inline constexpr int kArpRates   = 3;
inline constexpr int kArpRhythms = 6;
inline constexpr int kArpSounds  = 10;

/** In stick sectors: 0 is straight up, then clockwise. */
inline const char* arpModeName (int mode) noexcept
{
    static const char* names[kArpModes]
        { "UPP", "PEDAL", "PENDEL", "KONVERGERA", "NER", "OKTAVHOPP", "SLUMP", "ACKORD" };
    return names[((mode % kArpModes) + kArpModes) % kArpModes];
}

inline const char* arpRateName (int rate, bool triplet) noexcept
{
    static const char* straight[kArpRates] { "1/8", "1/16", "1/32" };
    static const char* triplets[kArpRates] { "1/8T", "1/16T", "1/32T" };
    return (triplet ? triplets : straight)[std::clamp (rate, 0, kArpRates - 1)];
}

inline const char* arpRhythmName (int rhythm) noexcept
{
    static const char* names[kArpRhythms] { "RAK", "SWING", "PUNKTERAD", "GATE", "ACCENT", "SLUMP" };
    return names[((rhythm % kArpRhythms) + kArpRhythms) % kArpRhythms];
}

enum class ArpFamily { pluck, juno, acid, pizz };

struct ArpSound
{
    const char* name;
    ArpFamily   family;
    int         index;      // the voice spec or Juno patch it borrows
    float       trim;       // measured: every sound at the same level, filter half open
};

inline const ArpSound& arpSound (int index) noexcept
{
    // Juno patch indices: PAD 0, STRAKAR 1, STOT 2, PLOCK 3, BRASS 4.
    static const std::array<ArpSound, kArpSounds> sounds
    {{
        { "OMNI",     ArpFamily::pluck, 0, 0.41f },
        { "HARPA",    ArpFamily::pluck, 1, 0.80f },
        { "VIBRAFON", ArpFamily::pluck, 2, 0.77f },
        { "GITARR",   ArpFamily::pluck, 3, 0.29f },
        { "PLOCK",    ArpFamily::juno,  3, 0.75f },
        { "STOT",     ArpFamily::juno,  2, 1.33f },
        { "BRASS",    ArpFamily::juno,  4, 0.75f },
        { "PAD",      ArpFamily::juno,  0, 1.03f },
        { "303",      ArpFamily::acid,  0, 1.16f },
        { "PIZZ",     ArpFamily::pizz,  0, 1.12f }
    }};

    return sounds[(size_t) (((index % kArpSounds) + kArpSounds) % kArpSounds)];
}

/** Which steps of a sixteen-step cycle sound, and which are leaned on. A rest
    does not advance the pattern: the next note still comes next. */
struct ArpRhythmStep { bool hit; bool accent; };

inline ArpRhythmStep arpRhythmStep (int rhythm, int position) noexcept
{
    const int p = ((position % 16) + 16) % 16;

    switch (((rhythm % kArpRhythms) + kArpRhythms) % kArpRhythms)
    {
        case 0:  return { true, p % 4 == 0 };                                     // RAK
        case 1:  return { true, p % 2 == 0 };                                     // SWING — the timing is elsewhere
        case 2:  return { p % 8 == 0 || p % 8 == 3 || p % 8 == 6, p % 8 == 0 };   // PUNKTERAD, 3+3+2
        case 3:                                                                   // GATE
        {
            static constexpr bool gate[16] { 1,0,1,1, 0,1,1,0, 1,0,1,1, 0,1,0,1 };
            return { gate[p], p % 4 == 0 };
        }
        case 4:                                                                   // ACCENT, off the beat
        {
            static constexpr bool lean[16] { 1,0,0,1, 0,0,1,0, 0,0,1,0, 1,0,0,0 };
            return { true, lean[p] };
        }
        default: return { true, p % 4 == 0 };                                     // SLUMP: rests drawn at play time
    }
}

//==============================================================================

class ArpEngine
{
public:
    static constexpr int kPool   = 16;      // chord tones across up to four octaves
    static constexpr int kVoices = 8;

    struct NoteEvent
    {
        int   note     = 0;
        float velocity = 0.0f;
        int   offset   = 0;
        bool  on       = true;
    };

    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn;

        for (auto& v : plucks) v.prepare (sampleRateIn);
        for (auto& v : junos)  v.prepare (sampleRateIn);
        for (auto& s : pizz)   s.prepare (sampleRateIn * kPizzOversample);

        acid.prepare (sampleRateIn);
        acid.setEnvMod (0.55f);
        acid.setDecay (0.3f);
        acid.setGlideSeconds (0.05f);

        body.prepare (sampleRateIn);
        body.configure (1, false);          // a viola's box: the arp sits in the middle of the range

        drums.prepare (sampleRateIn);

        // The Juno's patches, with the attack cut to a few milliseconds. A
        // pad's half-second swell never arrives on a sixteenth, so the arp's
        // copy starts at once and keeps everything else — the release still
        // lets a long gate bloom.
        for (int i = 0; i < kNumJunoPatches; ++i)
        {
            arpPatches[(size_t) i] = junoPatch (i);
            arpPatches[(size_t) i].attack = std::min (arpPatches[(size_t) i].attack, 0.004f);
        }

        for (auto& v : junos)
            v.setPatch (arpPatches[(size_t) arpSound (soundIndex).index]);

        filterSmooth = 1.0f - std::exp (-1.0f / (float) (sampleRateIn * 0.01));
        pizzDamp     = std::pow (0.001f, 1.0f / (float) (sampleRateIn * 0.06));
        dcCoef       = 1.0f - 2.0f * 3.14159265f * 12.0f / (float) sampleRateIn;

        setTempo (tempo);
        allOff();
    }

    void seed (std::uint32_t s) noexcept { rng = s != 0u ? s : 0x9e3779b9u; }

    //== what the pad sets, once a block ======================================

    /** The chord, with the bank already chosen by the ring. */
    void setChord (const music::Chord& chord) noexcept
    {
        if (chord != currentChord || ! poolBuilt)
        {
            currentChord = chord;
            rebuildPool();
        }
    }

    void setRegister (int shift) noexcept
    {
        const int r = std::clamp (shift, -2, 2);

        if (r != registerShift)
        {
            registerShift = r;
            rebuildPool();
        }
    }

    int  octaveRange() const noexcept { return octaves; }

    void cycleOctaves() noexcept
    {
        octaves = octaves >= 4 ? 1 : octaves + 1;
        rebuildPool();
    }

    void setOctaves (int count) noexcept
    {
        octaves = std::clamp (count, 1, 4);
        rebuildPool();
    }

    void setMode (int m) noexcept { mode = ((m % kArpModes) + kArpModes) % kArpModes; }

    /** The right trigger: playing or not, and which of the three rates. Held
        by RS, it keeps going at the last rate when the trigger lets go. */
    void setRunning (bool pressed, int rateStep) noexcept
    {
        if (pressed)
            rate = std::clamp (rateStep, 0, kArpRates - 1);

        const bool wanted = pressed || (holding && playing);

        if (wanted && ! playing)
            start();
        else if (! wanted && playing)
            stop();
    }

    /**
        The right stick's sideways lean, the Omnichord's zone. At rest the
        pattern walks the whole span; leaning narrows it, pinned to the end the
        thumb leans towards, down to one octave of chord tones. At full width
        both ends give the same window, which keeps the middle continuous.
    */
    void setZone (float squaredX, float deadzone) noexcept
    {
        zoneLean = std::clamp (squaredX, -1.0f, 1.0f);
        zoneDeadzone = deadzone;
        updateWindow();
    }

    int windowLow()  const noexcept { return windowLo; }
    int windowSize() const noexcept { return windowCount; }

    void toggleHold() noexcept
    {
        holding = ! holding;
    }

    bool isHolding() const noexcept { return holding; }

    void setSound (int index) noexcept
    {
        const int next = ((index % kArpSounds) + kArpSounds) % kArpSounds;

        if (next != soundIndex)
        {
            // Notes on the old family let go now; the new one starts clean.
            releaseEverything();
            soundIndex = next;

            const auto& s = arpSound (soundIndex);

            if (s.family == ArpFamily::juno)
                for (auto& v : junos)
                    v.setPatch (arpPatches[(size_t) s.index]);
        }
    }

    int sound() const noexcept { return soundIndex; }

    void setRhythm (int r) noexcept { rhythm = ((r % kArpRhythms) + kArpRhythms) % kArpRhythms; }
    int  rhythmIndex() const noexcept { return rhythm; }

    void setTriplet (bool on) noexcept { triplet = on; }
    bool isTriplet() const noexcept { return triplet; }

    /** Left trigger: the share of a step a note is held, in equal ratios from a
        blip to legato. At the top the notes tie, and the 303 slides. */
    void setGate (float amount) noexcept
    {
        gateAmount = std::clamp (amount, 0.0f, 1.0f);
        gateShare  = kShortestGate * std::pow (1.0f / kShortestGate, gateAmount);
    }

    float gate() const noexcept { return gateShare; }

    /** Right trigger, already including the host's floor: the filter position,
        0 closed to 1 open. Each family reads it in its own octaves. */
    void setCutoff (float position) noexcept { cutoffTarget = std::clamp (position, 0.0f, 1.0f); }

    void setDrums (bool on) noexcept { drumsOn = on; }
    bool drumsEnabled() const noexcept { return drumsOn; }

    void setTempo (double bpm) noexcept
    {
        tempo = std::clamp (bpm, 30.0, 300.0);
        beatsPerSample = tempo / 60.0 / sampleRate;
    }

    /** A playing host owns the beat: the steps land on its grid, bar-aligned. */
    void followHost (bool hostPlaying, double ppqAtBlockStart) noexcept
    {
        hostDriven = hostPlaying;

        if (! hostPlaying)
            return;

        const double ppq = std::max (0.0, ppqAtBlockStart);

        if (std::abs (ppq - beat) > 0.02)
        {
            beat = ppq;
            lastStep = (std::int64_t) std::floor (beat / stepBeats()) ;
            pendingAt = -1.0;
        }
    }

    bool isHostDriven() const noexcept { return hostDriven; }

    void allOff() noexcept
    {
        releaseEverything();

        for (auto& v : plucks) v.reset();
        for (auto& v : junos)  v.reset();
        for (auto& s : pizz)   s.reset();

        acid.reset();
        drums.reset();
        body.reset();

        playing = false;
        pendingAt = -1.0;
        filterNow = cutoffTarget;
        dcIn = dcOut = 0.0f;
    }

    //== what the screen reads ================================================

    bool isPlaying() const noexcept { return playing; }
    int  modeIndex() const noexcept { return mode; }
    int  rateIndex() const noexcept { return rate; }

    int  poolSize() const noexcept { return poolCount; }
    int  poolNote (int i) const noexcept { return pool[(size_t) std::clamp (i, 0, kPool - 1)]; }

    /** The pool index last struck, or -1; and for ACKORD, every tone. */
    int  lastPoolIndex() const noexcept { return lastIndex; }
    int  rhythmPosition() const noexcept { return (int) (((lastStep % 16) + 16) % 16); }
    bool usesChorus() const noexcept { return arpSound (soundIndex).family == ArpFamily::juno; }

    //== audio =================================================================

    void beginBlock() noexcept
    {
        eventCount  = 0;
        blockOffset = 0;
    }

    float processSample() noexcept
    {
        tickClock();
        tickGates();

        filterNow += (cutoffTarget - filterNow) * filterSmooth;

        const auto& s = arpSound (soundIndex);
        float out = 0.0f;

        switch (s.family)
        {
            case ArpFamily::pluck: out = lowpass (renderPlucks()); break;
            case ArpFamily::juno:  out = renderJunos();            break;
            case ArpFamily::acid:  out = renderAcid();             break;
            case ArpFamily::pizz:  out = lowpass (renderPizz());   break;
        }

        out *= s.trim;

        if (drumsOn)
            out += drums.processSample() * kDrumLevel;

        // ACKORD over four octaves strikes eight voices at once; bend the top
        // rather than let a chord stab clip the host.
        if (std::abs (out) > 0.7f)
            out = std::copysign (0.7f + 0.3f * std::tanh ((std::abs (out) - 0.7f) / 0.3f), out);

        ++blockOffset;
        return out;
    }

    int eventsThisBlock() const noexcept { return eventCount; }
    const NoteEvent& event (int i) const noexcept { return events[(size_t) i]; }

private:
    static constexpr float kShortestGate   = 0.06f;
    static constexpr int   kPizzOversample = 2;
    static constexpr int   kMaxEvents      = 128;

    static constexpr float kDrumLevel = 0.35f;

    //== the clock ============================================================

    double stepBeats() const noexcept
    {
        static constexpr double straight[kArpRates] { 0.5, 0.25, 0.125 };
        return straight[rate] * (triplet ? 2.0 / 3.0 : 1.0);
    }

    void start() noexcept
    {
        playing = true;
        counter = 0;
        lastIndex = -1;

        // Free running, the grid starts at the push. Under a host it stays on
        // the host's grid, and a push just after a line plays that line now.
        const double step = stepBeats();

        if (! hostDriven)
        {
            beat = 0.0;
            lastStep = 0;
            strike (0);
            return;
        }

        const double here  = std::floor (beat / step);
        lastStep = (std::int64_t) here;

        if (beat - here * step < step * 0.15)
            strike (lastStep);
    }

    void stop() noexcept
    {
        playing = false;
        pendingAt = -1.0;
    }

    void tickClock() noexcept
    {
        beat += beatsPerSample;

        if (! playing)
            return;

        const double step = stepBeats();
        const auto   now  = (std::int64_t) std::floor (beat / step);

        if (now != lastStep)
        {
            lastStep = now;

            // Swing: every other step waits a third of a step, the triplet
            // feel of a shuffle.
            const bool late = rhythm == 1 && (now & 1) != 0;

            if (late)
            {
                pendingAt   = (double) now * step + step / 3.0;
                pendingStep = now;
            }
            else
            {
                strike (now);
            }

            drumStep();
        }

        if (pendingAt >= 0.0 && beat >= pendingAt)
        {
            pendingAt = -1.0;
            strike (pendingStep);
        }
    }

    /** The kit plays sixteenths whatever the arp's rate: kick on one and
        three, snare on two and four, the hat on every eighth. */
    void drumStep() noexcept
    {
        if (! drumsOn)
            return;

        const auto sixteenth = (std::int64_t) std::floor (beat / 0.25 + 1.0e-9);

        if (sixteenth == lastDrum)
            return;

        lastDrum = sixteenth;
        const int p = (int) (((sixteenth % 16) + 16) % 16);

        if (p == 0 || p == 8 || p == 10)  drums.trigger (DrumKit::Piece::kick,  p == 10 ? 0.6f : 1.0f);
        if (p == 4 || p == 12)            drums.trigger (DrumKit::Piece::snare, 0.8f);
        if (p % 2 == 0)                   drums.trigger (DrumKit::Piece::hat,   p % 4 == 2 ? 0.55f : 0.35f);
    }

    //== the notes ============================================================

    void rebuildPool() noexcept
    {
        poolBuilt = true;

        int tones[4] {};
        int toneCount = 0;

        tones[toneCount++] = 0;
        tones[toneCount++] = music::chordThird (currentChord.quality);

        const bool seventh = currentChord.quality == music::Quality::major7
                          || currentChord.quality == music::Quality::dominant7
                          || currentChord.quality == music::Quality::minor7
                          || currentChord.quality == music::Quality::halfDiminished;

        const bool flatFifth = currentChord.quality == music::Quality::diminished
                            || currentChord.quality == music::Quality::halfDiminished;

        tones[toneCount++] = flatFifth ? 6 : 7;

        if (seventh)
            tones[toneCount++] = music::chordColourTone (currentChord.quality);

        baseTones = toneCount;

        // The root sits in the octave from C3, moved by the register.
        const int root = 48 + currentChord.rootPitchClass + 12 * registerShift;

        poolCount = 0;

        for (int o = 0; o < octaves && poolCount < kPool; ++o)
            for (int t = 0; t < toneCount && poolCount < kPool; ++t)
                pool[(size_t) poolCount++] = std::clamp (root + tones[t] + 12 * o, 12, 115);

        updateWindow();
    }

    void updateWindow() noexcept
    {
        const float a = std::abs (zoneLean);
        const float p = a <= zoneDeadzone ? 0.0f
                                          : std::clamp ((a - zoneDeadzone) / (1.0f - zoneDeadzone), 0.0f, 1.0f);

        const int widest    = std::max (1, poolCount);
        const int narrowest = std::clamp (baseTones, 1, widest);

        windowCount = std::clamp ((int) std::lround ((float) widest + (float) (narrowest - widest) * p),
                                  narrowest, widest);
        windowLo    = zoneLean < 0.0f ? 0 : widest - windowCount;
    }

    /** Which pool notes the n-th note of the pattern is. Returns how many. */
    int notesFor (std::int64_t n, std::array<int, kVoices>& out) noexcept
    {
        // Positions inside the zone's window; the caller adds where it starts.
        const int count = std::max (1, windowCount);
        auto wrap = [] (std::int64_t i, int m) { return (int) (((i % m) + m) % m); };

        switch (mode)
        {
            case 0:  out[0] = wrap (n, count); return 1;                               // UPP
            case 4:  out[0] = count - 1 - wrap (n, count); return 1;                   // NER

            case 2:                                                                    // PENDEL
            {
                if (count == 1) { out[0] = 0; return 1; }
                const int i = wrap (n, 2 * count - 2);
                out[0] = i < count ? i : 2 * count - 2 - i;
                return 1;
            }

            case 1:                                                                    // PEDAL
            {
                if (count == 1) { out[0] = 0; return 1; }
                out[0] = (n % 2 == 0) ? 0 : 1 + wrap (n / 2, count - 1);
                return 1;
            }

            case 3:                                                                    // KONVERGERA
            {
                const int i = wrap (n, count);
                out[0] = (i % 2 == 0) ? i / 2 : count - 1 - i / 2;
                return 1;
            }

            case 5:                                                                    // OKTAVHOPP
            {
                out[0] = wrap (n / 2, count) + ((n % 2) != 0 ? kOctaveUp : 0);
                return 1;
            }

            case 6:                                                                    // SLUMP
            {
                int pick = (int) (unit() * (float) count);
                pick = std::clamp (pick, 0, count - 1);

                if (count > 1 && pick == lastIndex)
                    pick = (pick + 1 + (int) (unit() * (float) (count - 1))) % count;

                out[0] = pick;
                return 1;
            }

            default:                                                                   // ACKORD
            {
                const int many = std::min ({ count, kVoices, std::max (baseTones, 3) + 1 });

                for (int i = 0; i < many; ++i)
                    out[(size_t) i] = i;

                return many;
            }
        }
    }

    static constexpr int kOctaveUp = 1000;    // a pool index flagged "an octave above"

    void strike (std::int64_t step) noexcept
    {
        const auto r = arpRhythmStep (rhythm, (int) (((step % 16) + 16) % 16));
        bool hit = r.hit;

        if (rhythm == 5 && unit() < 0.25f)
            hit = false;

        if (! hit || poolCount == 0)
            return;

        std::array<int, kVoices> picks {};
        const int many = notesFor (counter++, picks);

        const float velocity = r.accent ? 1.0f : 0.72f;
        const int   gateSamples = std::max (8, (int) (gateShare * stepBeats() / beatsPerSample));
        const bool  legato = gateAmount > 0.97f;

        for (int i = 0; i < many; ++i)
        {
            const int pick = picks[(size_t) i];
            const int idx  = windowLo + (pick >= kOctaveUp ? pick - kOctaveUp : pick);
            const int note = std::clamp (pool[(size_t) std::clamp (idx, 0, poolCount - 1)]
                                           + (pick >= kOctaveUp ? 12 : 0), 0, 127);

            lastIndex = idx;
            play (note, velocity, r.accent, gateSamples, legato && many == 1);
        }
    }

    //== voices ===============================================================

    struct Slot
    {
        int  note = -1;
        int  gateLeft = 0;      // samples until this note's gate closes
        bool held = false;      // a note-on the host has not been told is over
        std::uint64_t age = 0;
    };

    void play (int note, float velocity, bool accent, int gateSamples, bool legato) noexcept
    {
        const auto& s = arpSound (soundIndex);
        const float hz = midiToHz ((float) note);

        // The 303 is one voice: a tie slides, anything else retriggers.
        if (s.family == ArpFamily::acid)
        {
            auto& slot = slots[0];

            if (slot.held)
            {
                emit (slot.note, 0.0f, false);
                slot.held = false;
            }

            if (legato && acid.isGateOpen())
                acid.glideTo (note, accent);
            else
                acid.trigger (note, accent);

            slot.note = note;
            slot.gateLeft = gateSamples;
            slot.held = true;
            slot.age = ++order;
            emit (note, velocity, true);
            return;
        }

        const int v = allocate (note);
        auto& slot = slots[(size_t) v];

        if (slot.held)
        {
            emit (slot.note, 0.0f, false);
            slot.held = false;
        }

        switch (s.family)
        {
            case ArpFamily::pluck:
            {
                const auto& spec = voiceSpec (s.index);
                const float ring = 1.4f * spec.decayScale;
                const float coef = std::pow (0.001f, 1.0f / std::max (1.0f, (float) (sampleRate * ring)));
                plucks[(size_t) v].start (hz, velocity, coef, spec, note, ++order);
                break;
            }

            case ArpFamily::juno:
                junos[(size_t) v].noteOn (note, velocity, gateSamples, ++order);
                break;

            case ArpFamily::pizz:
            {
                auto& str = pizz[(size_t) v];
                const float ring = std::clamp (1.6f * std::sqrt (196.0f / hz), 0.3f, 4.0f);
                str.setLosses (hz, ring, std::min (7500.0f, 12.0f * hz));
                str.setTuning (hz, 0.1f);
                const float level = 1.2f * std::pow (10.0f, (velocity - 1.0f) * 24.0f / 20.0f);
                str.pluck (level, (int) (sampleRate * kPizzOversample * (0.0028f - 0.0018f * velocity)));
                pizzRest[(size_t) v] = 0.0f;
                break;
            }

            case ArpFamily::acid:
                break;
        }

        slot.note = note;
        slot.gateLeft = gateSamples;
        slot.held = true;
        slot.age = order;
        emit (note, velocity, true);
    }

    /** The slot already playing this note, then a free one, then the oldest. */
    int allocate (int note) noexcept
    {
        for (int i = 0; i < kVoices; ++i)
            if (slots[(size_t) i].note == note && (slots[(size_t) i].held || voiceSounding (i)))
                return i;

        for (int i = 0; i < kVoices; ++i)
            if (! slots[(size_t) i].held && ! voiceSounding (i))
                return i;

        int oldest = 0;

        for (int i = 1; i < kVoices; ++i)
            if (slots[(size_t) i].age < slots[(size_t) oldest].age)
                oldest = i;

        return oldest;
    }

    bool voiceSounding (int i) const noexcept
    {
        switch (arpSound (soundIndex).family)
        {
            case ArpFamily::pluck: return plucks[(size_t) i].isActive();
            case ArpFamily::juno:  return junos[(size_t) i].isActive();
            case ArpFamily::acid:  return i == 0 && acid.isSounding();
            case ArpFamily::pizz:  return pizzRest[(size_t) i] < 0.4f;
        }

        return false;
    }

    /** Gates run out on their own sample, and so do their note-offs. */
    void tickGates() noexcept
    {
        const auto family = arpSound (soundIndex).family;

        for (int i = 0; i < kVoices; ++i)
        {
            auto& slot = slots[(size_t) i];

            if (! slot.held || --slot.gateLeft > 0)
                continue;

            slot.held = false;
            emit (slot.note, 0.0f, false);

            switch (family)
            {
                case ArpFamily::pluck: plucks[(size_t) i].setDecayCoefficient (pizzDamp); break;
                case ArpFamily::juno:  break;                    // its own gate closed on the same count
                case ArpFamily::acid:  acid.release();           break;
                case ArpFamily::pizz:  pizzRest[(size_t) i] = std::clamp (22.0f / midiToHz ((float) slot.note), 0.02f, 0.45f); break;
            }
        }
    }

    void releaseEverything() noexcept
    {
        for (auto& slot : slots)
        {
            if (slot.held)
                emit (slot.note, 0.0f, false);

            slot = {};
        }

        for (auto& v : plucks) v.stopQuickly();
        for (auto& v : junos)  v.noteOff();
        acid.release();

        for (auto& r : pizzRest)
            r = 0.3f;
    }

    //== rendering ============================================================

    float renderPlucks() noexcept
    {
        float sum = 0.0f;

        for (auto& v : plucks)
            if (v.isActive())
                sum += v.process();

        return sum;
    }

    float renderJunos() noexcept
    {
        const auto& p = junoPatch (arpSound (soundIndex).index);

        lfoPhase += p.lfoRate / (float) sampleRate;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        const float lfo = std::sin (6.28318530717959f * lfoPhase);
        float sum = 0.0f;

        for (auto& v : junos)
            sum += v.process (lfo, filterNow, kJunoResonance, 0.8f);

        return sum;
    }

    float renderAcid() noexcept
    {
        acid.setFilter (filterNow * 0.85f, kAcidResonance);
        return acid.process();
    }

    float renderPizz() noexcept
    {
        float sum = 0.0f;

        for (int i = 0; i < kVoices; ++i)
        {
            auto& str = pizz[(size_t) i];
            const float rest = pizzRest[(size_t) i];

            for (int k = 0; k < kPizzOversample; ++k)
                sum += str.process (0.0f, 0.0f, rest, 0.0f);
        }

        const float boxed = body.process (sum * (1.0f / (float) kPizzOversample));
        const float out = boxed - dcIn + dcCoef * dcOut;
        dcIn = boxed;
        dcOut = out;
        return out;
    }

    /** The plate's voices and the pizzicato have no filter of their own that a
        trigger can reach, so they share this one: a two-pole lowpass from
        250 Hz closed to 18 kHz open, in octaves. */
    float lowpass (float x) noexcept
    {
        const float hz = std::min (250.0f * std::pow (2.0f, filterNow * 6.2f), 0.45f * (float) sampleRate);
        const float g  = std::tan (3.14159265f * hz / (float) sampleRate);
        const float k  = 1.4142f;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;
    }

    float unit() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float) (rng >> 8) / 16777216.0f;
    }

    void emit (int note, float velocity, bool on) noexcept
    {
        if (eventCount < kMaxEvents)
            events[(size_t) eventCount++] = { note, velocity, blockOffset, on };
    }

    static constexpr float kJunoResonance = 0.25f;
    static constexpr float kAcidResonance = 0.55f;

    double sampleRate = 48000.0, tempo = 120.0, beatsPerSample = 120.0 / 60.0 / 48000.0;
    double beat = 0.0, pendingAt = -1.0;
    std::int64_t lastStep = 0, pendingStep = 0, counter = 0, lastDrum = -1;

    music::Chord currentChord;
    std::array<int, kPool> pool {};
    int poolCount = 0, baseTones = 3, registerShift = 0, octaves = 2;
    int windowLo = 0, windowCount = 1;
    float zoneLean = 0.0f, zoneDeadzone = 0.1f;
    bool poolBuilt = false;

    int mode = 0, rate = 1, rhythm = 0, soundIndex = 0, lastIndex = -1;
    bool playing = false, holding = false, hostDriven = false, triplet = false, drumsOn = false;

    float gateAmount = 0.5f, gateShare = 0.25f;
    float cutoffTarget = 0.0f, filterNow = 0.0f, filterSmooth = 0.01f;
    float ic1 = 0.0f, ic2 = 0.0f;

    std::array<PluckVoice, kVoices> plucks;
    std::array<JunoVoice, kVoices>  junos;
    std::array<JunoPatch, kNumJunoPatches> arpPatches {};
    std::array<BowedString, kVoices> pizz;
    std::array<float, kVoices>      pizzRest {};
    std::array<Slot, kVoices>       slots {};
    AcidVoice  acid;
    StringBody body;
    DrumKit    drums;

    float lfoPhase = 0.0f, pizzDamp = 0.99f;
    float dcCoef = 0.999f, dcIn = 0.0f, dcOut = 0.0f;

    std::array<NoteEvent, kMaxEvents> events {};
    int eventCount = 0, blockOffset = 0;
    std::uint64_t order = 0;
    std::uint32_t rng = 0x1234567u;
};

} // namespace chordtroller::dsp

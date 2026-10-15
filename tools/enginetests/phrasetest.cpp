// Offline test of the Juno's phrase clock. No JUCE, no pad.
//
// Every phrase that strikes must strike again each time its figure comes round.
// A figure one step long (PULS FJARDEDEL, STRUM SNABB) once played a single time
// and then went quiet, because the clock compared the step *within* the figure
// and a one-step figure is on step 0 for ever.
#include <stddef.h>
#include "dsp/JunoEngine.h"

#include <cstdio>
#include <vector>

using namespace chordtroller;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FEL ", what);
    if (! ok) ++failures;
}

static const double fs  = 48000.0;
static const double bpm = 120.0;

/** Plays one style and variation for `beats` beats and returns the sample of
    every note-on. Free-running, or locked to a host whose beat starts at zero. */
static std::vector<long> take (int style, int variation, int beats, bool host)
{
    dsp::JunoEngine e;
    e.prepare (fs);
    e.setTempo (bpm);
    e.setChord (0, music::FunctionClass::major, false, 0);

    // MENY steps the variation of the style that is playing. Letting go
    // afterwards means the take starts the phrase from its first step, rather
    // than switching variation on a step that has already sounded.
    e.beginBlock();
    e.setPhrase (style);
    for (int i = 0; i < variation; ++i)
        e.cycleVariation();
    e.setPhrase (-1);

    std::vector<long> ons;
    const int  block   = 128;
    const long samples = (long) (fs * 60.0 / bpm * beats);
    double ppq = 0.0;

    for (long at = 0; at < samples; at += block)
    {
        e.beginBlock();
        e.followHost (host, ppq);
        e.setPhrase (style);

        for (int i = 0; i < block; ++i)
            e.processSample();

        for (int i = 0; i < e.eventsThisBlock(); ++i)
            if (e.event (i).on)
                ons.push_back (at + e.event (i).offset);

        ppq += block * bpm / 60.0 / fs;
    }

    return ons;
}

int main()
{
    const long samplesPerBeat = (long) (fs * 60.0 / bpm);

    for (int style = 0; style < dsp::kNumStyles; ++style)
    {
        for (int variation = 0; variation < dsp::kNumVariations; ++variation)
        {
            const auto& v = dsp::phraseStyle (style).variations[(size_t) variation];

            if (v.mode == dsp::PhraseMode::hold)
                continue;

            for (const bool host : { false, true })
            {
                // Two full turns of the longest figure, at least eight beats.
                const int turns = 8 / v.beats < 2 ? 2 : 8 / v.beats;
                const auto ons  = take (style, variation, turns * v.beats + 1, host);

                bool everyTurn = true;

                // Windows start half a step early: a hit due on a turn's first
                // sample can land one sample before it, and must still count
                // for that turn rather than the one before.
                const long halfStep = samplesPerBeat / v.stepsPerBeat / 2;

                for (int t = 0; t < turns; ++t)
                {
                    const long from = t * v.beats * samplesPerBeat - halfStep;
                    const long to   = from + v.beats * samplesPerBeat;
                    bool struck = false;

                    for (const long s : ons)
                        struck = struck || (s >= from && s < to);

                    everyTurn = everyTurn && struck;
                }

                char what[96];
                std::snprintf (what, sizeof what, "%s %s strikes every turn%s",
                               dsp::phraseStyle (style).name, v.name, host ? " (host)" : "");
                check (everyTurn, what);
            }
        }
    }

    std::printf (failures == 0 ? "\nAlla gick igenom.\n" : "\n%d fel.\n", failures);
    return failures == 0 ? 0 : 1;
}

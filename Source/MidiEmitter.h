#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>

namespace chordtroller
{

/**
    Notes out, so the performance survives the take.

    A gamepad does not exist in the host's timeline. Record Chordtroller's audio
    and the playing is gone — there is nothing to play back, nothing to edit, and
    nothing to move a bar to the left. Sending the notes as MIDI is what turns it
    from a toy you play into an instrument you record with, and it is why this
    exists before the pretty interface does.

    The awkward part is that the strings decay rather than being released, so
    nothing ever tells us a note ended. Each note-on is therefore paired with a
    countdown to its own note-off, held across blocks, and struck again before it
    expires it is released first — one string cannot be sounding twice.
*/
class MidiEmitter
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;

        for (auto& n : sounding)
            n = {};
    }

    /** Retires whatever ran out during this block, at the sample it ran out.
        Call before adding the block's new notes. */
    void expire (juce::MidiBuffer& out, int numSamples) noexcept
    {
        for (auto& n : sounding)
        {
            if (n.note < 0)
                continue;

            if (n.samplesLeft < numSamples)
            {
                out.addEvent (juce::MidiMessage::noteOff (1, n.note),
                              std::max (0, n.samplesLeft));
                n = {};
            }
            else
            {
                n.samplesLeft -= numSamples;
            }
        }
    }

    void noteOn (juce::MidiBuffer& out, int note, float velocity,
                 int sampleOffset, float ringSeconds, int numSamples) noexcept
    {
        note = std::clamp (note, 0, 127);

        const int level = std::clamp ((int) std::lround (velocity * 126.0f) + 1, 1, 127);
        const int offset = std::clamp (sampleOffset, 0, std::max (0, numSamples - 1));

        // One string cannot be sounding twice, so a re-pluck releases first.
        for (auto& n : sounding)
        {
            if (n.note != note)
                continue;

            out.addEvent (juce::MidiMessage::noteOff (1, note), offset);
            n = {};
            break;
        }

        auto* slot = findFree();

        if (slot == nullptr)
            return;                 // sixty-four ringing at once; the oldest keeps its off

        out.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) level), offset);

        const int ringSamples = (int) (ringSeconds * (float) sampleRate);

        // The countdown starts now, so the rest of this block has already run.
        slot->note = note;
        slot->samplesLeft = std::max (1, ringSamples - (numSamples - offset));
    }

    /** Instant Off, and anything else that has to leave the host quiet. */
    void allOff (juce::MidiBuffer& out, int sampleOffset) noexcept
    {
        for (auto& n : sounding)
        {
            if (n.note < 0)
                continue;

            out.addEvent (juce::MidiMessage::noteOff (1, n.note), sampleOffset);
            n = {};
        }
    }

private:
    struct Sounding
    {
        int note = -1;
        int samplesLeft = 0;
    };

    Sounding* findFree() noexcept
    {
        for (auto& n : sounding)
            if (n.note < 0)
                return &n;

        return nullptr;
    }

    std::array<Sounding, 64> sounding {};
    double sampleRate = 44100.0;
};

} // namespace chordtroller

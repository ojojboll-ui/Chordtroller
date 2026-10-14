// Plays a scripted performance through the real processor, reverb and all, and
// writes what the host would hear to a WAV — so a listening file is the
// instrument itself and not an engine on its own.
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <functional>
#include <mutex>

namespace chordtroller
{
    extern std::mutex fakePadLock;
    extern PadState   fakePad;
}

using namespace chordtroller;

namespace
{
    void setPad (const std::function<void (PadState&)>& change)
    {
        {
            const std::lock_guard<std::mutex> lock (fakePadLock);
            change (fakePad);
        }

        juce::Thread::sleep (25);
    }

    void press (std::uint16_t button)
    {
        setPad ([button] (PadState& s) { s.buttons |= button; });
        setPad ([button] (PadState& s) { s.buttons &= (std::uint16_t) ~button; });
    }

    struct Recorder
    {
        ChordtrollerAudioProcessor& proc;
        juce::AudioBuffer<float> block { 2, 512 };
        juce::AudioBuffer<float> take  { 2, 0 };

        void run (double seconds)
        {
            const int blocks = (int) std::ceil (seconds * 48000.0 / 512.0);

            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                proc.processBlock (block, midi);

                const int at = take.getNumSamples();
                take.setSize (2, at + 512, true);

                for (int ch = 0; ch < 2; ++ch)
                    take.copyFrom (ch, at, block, ch, 0, 512);
            }
        }

        void save (const juce::File& file)
        {
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (new juce::FileOutputStream (file), 48000.0, 2, 16, {}, 0));

            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (take, 0, take.getNumSamples());

            take.setSize (2, 0);
        }
    };

    /** RT down first, then three strokes, up, down, up, and let go. */
    void strokes (Recorder& rec, float rt, float bow)
    {
        setPad ([rt] (PadState& s) { s.ly = 1.0f; s.rt = rt; });
        rec.run (0.3);

        for (const float direction : { 1.0f, -1.0f, 1.0f })
        {
            setPad ([bow, direction] (PadState& s) { s.ry = bow * direction; });
            rec.run (1.2);
        }

        setPad ([] (PadState& s) { s.ry = 0.0f; });
        rec.run (1.0);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir (argc > 1 ? juce::String (argv[1])
                                      : juce::File::getCurrentWorkingDirectory().getFullPathName());

    ChordtrollerAudioProcessor processor;
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::strings);
    processor.prepareToPlay (48000.0, 512);
    juce::Thread::sleep (100);

    Recorder rec { processor };

    // Violin, C4.
    strokes (rec, 1.0f, 1.0f);
    rec.save (outDir.getChildFile ("fiol_C4_skorr_RT_i_botten.wav"));

    strokes (rec, 0.5f, 1.0f);
    rec.save (outDir.getChildFile ("fiol_C4_full_ton_RT_halvvags.wav"));

    // Cello, C3: two instruments on, one register up.
    press (padDpadRight);
    press (padDpadRight);
    press (padView);

    strokes (rec, 1.0f, 1.0f);
    rec.save (outDir.getChildFile ("cello_C3_skorr_RT_i_botten.wav"));

    strokes (rec, 0.5f, 1.0f);
    rec.save (outDir.getChildFile ("cello_C3_full_ton_RT_halvvags.wav"));

    // The arpeggiator, through four of its sounds: a pattern per sound, the
    // chord moving C, G, F, Am under it, the filter opening as it goes.
    setPad ([] (PadState& s) { s = {}; });
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::arp);
    rec.run (0.2);
    rec.take.setSize (2, 0);

    struct Take
    {
        const char*   file;
        int           soundPresses;
        std::uint16_t modeButton;
        int           modePresses;
        float         rt, rx, ry;
    };

    const Take takes[] {
        { "arp_OMNI_upp_1-16.wav",            0, 0,    0, 0.6f,  0.0f,  0.0f },
        { "arp_PLOCK_pedal_1-16.wav",         4, padB, 2, 0.6f,  0.0f,  0.0f },
        { "arp_303_pendel_1-16_lang.wav",     4, padB, 1, 0.6f,  0.0f,  0.8f },
        { "arp_PIZZ_konvergera_1-8_lagt.wav", 1, padA, 2, 0.3f, -0.6f,  0.0f },
    };

    for (const auto& take : takes)
    {
        for (int i = 0; i < take.soundPresses; ++i)
            press (padDpadRight);

        for (int i = 0; i < take.modePresses; ++i)
            press (take.modeButton);

        // Up (C), right-up (G), up-left (F), down-right (Am): the ring's places.
        const float chords[4][2] { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }, { 1.0f, -1.0f } };
        float lt = 0.3f;

        for (const auto& c : chords)
        {
            setPad ([&] (PadState& s) { s.lx = c[0]; s.ly = c[1]; s.rx = take.rx; s.ry = take.ry; s.rt = take.rt; s.lt = lt; });
            rec.run (2.0);
            lt += 0.2f;
        }

        setPad ([] (PadState& s) { s = {}; });
        rec.run (1.5);
        rec.save (outDir.getChildFile (take.file));
    }

    setPad ([] (PadState& s) { s = {}; });
    std::printf ("klart\n");
    return 0;
}

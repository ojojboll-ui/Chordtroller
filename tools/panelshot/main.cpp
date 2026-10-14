// Renders Chordtroller's editor to PNG without a window. States that need a
// pad are painted by writing the processor's published atomics directly.
#include "PluginProcessor.h"

#include <cstdio>

namespace
{
    bool writePng (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);

        if (! image.isValid())
            return false;

        file.deleteFile();
        juce::FileOutputStream out (file);

        if (! out.openedOk())
            return false;

        juce::PNGImageFormat png;
        return png.writeImageToStream (image, out);
    }

    void settle (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    using Published = ChordtrollerAudioProcessor::Published;

    template <typename T, typename V>
    void put (const std::atomic<T>& a, V v) { const_cast<std::atomic<T>&> (a).store ((T) v); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir (argc > 1 ? juce::String (argv[1])
                                      : juce::File::getCurrentWorkingDirectory().getFullPathName());

    ChordtrollerAudioProcessor processor;
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::strings);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    settle (300);

    const auto& p = processor.published();

    // 1. Bowing: a violin up-bow on E4, vibrato, a section with a drone.
    put (p.keyRoot, 2);
    put (p.stringInstrument, 0);
    put (p.stringScale, 0);
    put (p.stringSector, 1);
    put (p.stringNote, 64.1f);
    put (p.stringOpen, 62);
    put (p.stringBow, 0.62f);
    put (p.stringVibrato, 0.4f);
    put (p.stringTremolo, 0.0f);
    put (p.stringPressure, 0.52f);
    put (p.stringGlide, 0.3f);
    put (p.stringLevel, 0.11f);
    put (p.stringBowPoint, 0.1f);
    put (p.stringDrone, 62);
    put (p.stringSection, true);
    settle (200);
    std::printf ("arco: %d\n", (int) writePng (*editor, outDir.getChildFile ("strings_arco.png")));

    // 2. Pizzicato, muted, a cello an octave up, no drone, bow at rest.
    put (p.stringInstrument, 2);
    put (p.stringSector, 4);
    put (p.stringOctave, 1);
    put (p.stringNote, 45.0f);
    put (p.stringOpen, 43);
    put (p.stringBow, 0.0f);
    put (p.stringVibrato, 0.0f);
    put (p.stringTremolo, 0.7f);
    put (p.stringPressure, 0.9f);
    put (p.stringGlide, 0.0f);
    put (p.stringLevel, 0.04f);
    put (p.stringDrone, -1);
    put (p.stringPizz, true);
    put (p.stringSordino, true);
    put (p.stringSection, false);
    put (p.stringRegister, -1);
    settle (200);
    std::printf ("pizz: %d\n", (int) writePng (*editor, outDir.getChildFile ("strings_pizz.png")));

    // 3. The arpeggiator, playing: pedal at sixteenths over two octaves of Am7,
    // held, with the gate rhythm, delay and drums on.
    processor.setInstrument (ChordtrollerAudioProcessor::Instrument::arp);
    settle (200);

    put (p.keyRoot, 0);
    put (p.sector, 3);
    put (p.bank, 1);
    put (p.arpMode, 1);
    put (p.arpRate, 1);
    put (p.arpPlaying, true);
    put (p.arpHold, true);
    put (p.arpOctaves, 2);
    put (p.arpSound, 4);
    put (p.arpRhythm, 3);
    put (p.arpDelay, 2);
    put (p.arpDrums, true);
    put (p.arpCutoff, 0.62f);
    put (p.arpGate, 0.4f);
    put (p.arpGateShare, 0.19f);
    put (p.arpPoolSize, 8);
    put (p.arpWindowLo, 2);
    put (p.arpWindowSize, 5);
    put (p.arpSpeed, 0.62f);

    const int am7[8] { 57, 60, 64, 67, 69, 72, 76, 79 };

    for (int i = 0; i < 16; ++i)
        put (p.arpPool[i], i < 8 ? am7[i] : -1);

    put (p.arpLastIndex, 5);
    put (p.arpRhythmPos, 6);
    settle (200);
    std::printf ("arp: %d\n", (int) writePng (*editor, outDir.getChildFile ("arp.png")));

    editor.reset();
    return 0;
}

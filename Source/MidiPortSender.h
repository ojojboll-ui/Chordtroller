#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>

namespace chordtroller
{

/**
    The notes the plugin plays, also sent to a MIDI port of the system's own.

    Reason 14 does not take MIDI from a VST — its manual says so plainly — so the
    notes Chordtroller hands its host are dropped there. This sends the same notes
    to a port outside the host: a virtual cable such as loopMIDI, which Reason
    reads as a keyboard and plays through whichever instrument track is selected.

    The audio thread only copies each block's messages into a lock-free queue,
    stamped with when they fall due. A thread of its own hands them to the port
    at those times, because a MIDI driver call can take longer than an audio
    thread may ever wait.
*/
class MidiPortSender : private juce::Thread
{
public:
    MidiPortSender() : juce::Thread ("Chordtroller MIDI port") {}

    ~MidiPortSender() override { close(); }

    //== message thread =======================================================

    /** An empty identifier closes the port. */
    bool open (const juce::String& identifier)
    {
        close();

        if (identifier.isEmpty())
            return true;

        auto device = juce::MidiOutput::openDevice (identifier);

        if (device == nullptr)
            return false;

        {
            const juce::ScopedLock lock (deviceLock);
            output = std::move (device);
        }

        current = identifier;
        fifo.reset();
        active.store (true, std::memory_order_relaxed);
        startThread (juce::Thread::Priority::high);
        return true;
    }

    void close()
    {
        active.store (false, std::memory_order_relaxed);
        stopThread (1000);

        const juce::ScopedLock lock (deviceLock);

        if (output != nullptr)
        {
            // Whatever was sounding at the other end of the cable stops with it.
            for (int channel = 1; channel <= 16; ++channel)
                output->sendMessageNow (juce::MidiMessage::allNotesOff (channel));

            output.reset();
        }

        current = {};
    }

    const juce::String& identifier() const noexcept { return current; }

    //== audio thread =========================================================

    void push (const juce::MidiBuffer& block, double sampleRate) noexcept
    {
        if (! active.load (std::memory_order_relaxed) || block.isEmpty() || sampleRate <= 0.0)
            return;

        const double start = juce::Time::getMillisecondCounterHiRes();

        for (const auto metadata : block)
        {
            const int size = metadata.numBytes;

            if (size <= 0 || size > 3)
                continue;                   // notes and controllers, nothing longer

            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            fifo.prepareToWrite (1, start1, size1, start2, size2);

            if (size1 + size2 == 0)
                return;                     // full: drop rather than wait

            auto& item = items[(size_t) (size1 > 0 ? start1 : start2)];
            item.size = size;

            for (int i = 0; i < size; ++i)
                item.bytes[(size_t) i] = metadata.data[i];

            item.dueMs = start + metadata.samplePosition * 1000.0 / sampleRate;
            fifo.finishedWrite (1);
        }
    }

private:
    struct Item
    {
        std::array<juce::uint8, 3> bytes {};
        int    size  = 0;
        double dueMs = 0.0;
    };

    void run() override
    {
        while (! threadShouldExit())
        {
            const double now = juce::Time::getMillisecondCounterHiRes();

            for (;;)
            {
                int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
                fifo.prepareToRead (1, start1, size1, start2, size2);

                if (size1 + size2 == 0)
                    break;

                const auto& item = items[(size_t) (size1 > 0 ? start1 : start2)];

                if (item.dueMs > now + 0.5)
                    break;

                {
                    const juce::ScopedLock lock (deviceLock);

                    if (output != nullptr)
                        output->sendMessageNow (juce::MidiMessage (item.bytes.data(), item.size));
                }

                fifo.finishedRead (1);
            }

            wait (1);
        }
    }

    static constexpr int kCapacity = 1024;

    juce::AbstractFifo fifo { kCapacity };
    std::array<Item, kCapacity> items {};

    std::unique_ptr<juce::MidiOutput> output;
    juce::CriticalSection deviceLock;
    std::atomic<bool> active { false };
    juce::String current;
};

} // namespace chordtroller

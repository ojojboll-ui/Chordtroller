#pragma once

#include "GamepadSource.h"
#include "PadState.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>

namespace chordtroller
{

/**
    The gamepad on its own thread, published to everyone else without locks.

    Two things cross the boundary and they cross it differently. Axis values are
    a *level*: only the newest matters, so they go through a seqlock — the reader
    takes a torn-free copy of the newest snapshot and never waits. Buttons are
    *events*: missing one would swallow a chord change, so they go through a FIFO
    with the time they happened, and from phase 2 that stamp is what places the
    change on the right sample rather than at the top of whatever block noticed.

    Neither path allocates and neither can block the audio thread.

    Why 250 Hz: the strum clock lives in the audio thread and integrates a rate,
    so poll jitter cannot smear the notes inside a sweep — only the moment a
    gesture *starts* is quantised to the poll interval, and 4 ms is under the
    threshold where a starting pluck feels late.
*/
class GamepadReader
{
public:
    GamepadReader();
    ~GamepadReader();

    void startReading();
    void stopReading();

    /** The newest complete reading. Safe from any thread, never blocks. */
    PadState state() const noexcept;

    /** Oldest unread button crossing, if any. */
    bool popEvent (PadEvent& out) noexcept;

    /** What the poll loop actually achieved, which is the whole question phase 1
        exists to answer — a nominal 250 Hz means nothing if the OS timer is
        rounding every wait up to 15 ms. */
    float measuredHz() const noexcept       { return pollHz.load (std::memory_order_relaxed); }
    float worstIntervalMs() const noexcept  { return worstMs.load (std::memory_order_relaxed); }
    void  resetTiming() noexcept            { worstMs.store (0.0f, std::memory_order_relaxed); }

    /** Whether the timing came from a high-resolution timer or the ordinary
        scheduler, which is the difference between 4 ms and about 15. */
    bool usingHighResolutionTimer() const noexcept
    {
        return highResTimer.load (std::memory_order_relaxed);
    }

    const char* sourceName() const noexcept
    {
        return source != nullptr ? source->name() : "ingen";
    }

    /**
        Ask for a haptic click. Safe from the audio thread, which is the point.

        Sending vibration is a driver round trip that can take the best part of a
        millisecond, so the audio thread only leaves a number here and the poll
        thread does the talking. The click is short and decays on its own — a
        motor left running is a buzz, not a string.
    */
    void requestRumble (float amount) noexcept
    {
        auto current = rumbleRequest.load (std::memory_order_relaxed);
        const auto wanted = std::clamp (amount, 0.0f, 1.0f);

        while (wanted > current
               && ! rumbleRequest.compare_exchange_weak (current, wanted,
                                                         std::memory_order_relaxed))
        {
        }
    }

private:
    /**
        A seqlock. The writer bumps the counter to odd, writes, bumps it to even;
        a reader that sees an odd count, or a different count either side of its
        copy, tries again. For one writer and readers that must never wait it
        beats a mutex, and unlike a plain atomic it can carry a struct that has
        to stay internally consistent — a stick's X and Y must come from the same
        instant or a diagonal briefly reads as a straight push.
    */
    class Snapshot
    {
    public:
        void write (const PadState& s) noexcept;
        PadState read() const noexcept;

    private:
        std::atomic<std::uint32_t> seq { 0 };
        PadState data;
    };

    class Ticker;

    void run();

    std::unique_ptr<GamepadSource> source;
    std::unique_ptr<juce::Thread>  thread;

    Snapshot snapshot;

    static constexpr int kEventCapacity = 256;
    juce::AbstractFifo             eventFifo { kEventCapacity };
    std::array<PadEvent, kEventCapacity> events {};

    std::atomic<float> pollHz  { 0.0f };
    std::atomic<float> worstMs { 0.0f };
    std::atomic<bool>  highResTimer { false };
    std::atomic<float> rumbleRequest { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GamepadReader)
};

} // namespace chordtroller

#include "GamepadReader.h"

#include <functional>

#if JUCE_WINDOWS
 // Without this, windows.h defines min and max as macros and every std::max in
 // the file stops compiling in a way that does not mention windows.h at all.
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
  #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
 #endif
#endif

namespace chordtroller
{

namespace
{
    constexpr double kPollIntervalMs = 4.0;     // 250 Hz

    /** Every button we care to report a crossing for, so the diff loop has
        something to walk. */
    constexpr PadButton kAllButtons[] =
    {
        padA, padB, padX, padY,
        padLB, padRB, padView, padMenu, padLS, padRS,
        padDpadUp, padDpadDown, padDpadLeft, padDpadRight
    };

    class ReaderThread final : public juce::Thread
    {
    public:
        ReaderThread (const char* threadName, std::function<void()> body)
            : juce::Thread (threadName), loop (std::move (body)) {}

        void run() override { loop(); }

    private:
        std::function<void()> loop;
    };
}

//==============================================================================

void GamepadReader::Snapshot::write (const PadState& s) noexcept
{
    const auto v = seq.load (std::memory_order_relaxed);

    seq.store (v + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);

    data = s;

    std::atomic_thread_fence (std::memory_order_release);
    seq.store (v + 2, std::memory_order_relaxed);
}

PadState GamepadReader::Snapshot::read() const noexcept
{
    PadState out;

    for (;;)
    {
        const auto before = seq.load (std::memory_order_relaxed);

        if ((before & 1u) != 0u)
            continue;                       // a write is in flight

        std::atomic_thread_fence (std::memory_order_acquire);

        out = data;

        std::atomic_thread_fence (std::memory_order_acquire);

        if (seq.load (std::memory_order_relaxed) == before)
            return out;                     // nothing moved underneath the copy
    }
}

//==============================================================================

/**
    Waits out one poll interval as accurately as the platform allows.

    On Windows an ordinary sleep is rounded up to the scheduler's tick — about
    15.6 ms by default, which would turn a 250 Hz loop into a 64 Hz one without
    saying so. A high-resolution waitable timer gets the real interval without
    calling timeBeginPeriod, which would raise the timer resolution for every
    process on the machine and is not ours to do from inside someone's DAW.
*/
class GamepadReader::Ticker
{
public:
    explicit Ticker (juce::Thread& t) : owner (t)
    {
       #if JUCE_WINDOWS
        handle = CreateWaitableTimerExW (nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);

        // Predates Windows 10 1803: fall back to an ordinary waitable timer,
        // which is still finer-grained than a plain sleep.
        if (handle == nullptr)
        {
            handle = CreateWaitableTimerExW (nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            highResolution = false;
        }
        else
        {
            highResolution = true;
        }

        if (handle != nullptr)
        {
            LARGE_INTEGER due {};
            due.QuadPart = -(LONGLONG) (kPollIntervalMs * 10000.0);   // relative, 100 ns units

            if (! SetWaitableTimer (handle, &due, (LONG) kPollIntervalMs, nullptr, nullptr, FALSE))
            {
                CloseHandle (handle);
                handle = nullptr;
                highResolution = false;
            }
        }
       #endif
    }

    ~Ticker()
    {
       #if JUCE_WINDOWS
        if (handle != nullptr)
        {
            CancelWaitableTimer (handle);
            CloseHandle (handle);
        }
       #endif
    }

    bool isHighResolution() const noexcept { return highResolution; }

    void wait()
    {
       #if JUCE_WINDOWS
        if (handle != nullptr)
        {
            WaitForSingleObject (handle, (DWORD) (kPollIntervalMs * 4.0));
            return;
        }
       #endif

        owner.wait ((int) kPollIntervalMs);
    }

private:
    juce::Thread& owner;
    bool highResolution = false;

   #if JUCE_WINDOWS
    HANDLE handle = nullptr;
   #endif
};

//==============================================================================

GamepadReader::GamepadReader()
    : source (GamepadSource::createForPlatform())
{
    thread = std::make_unique<ReaderThread> ("Chordtroller gamepad", [this] { run(); });
}

GamepadReader::~GamepadReader()
{
    stopReading();
}

void GamepadReader::startReading()
{
    if (! thread->isThreadRunning())
        thread->startThread (juce::Thread::Priority::high);
}

void GamepadReader::stopReading()
{
    if (thread != nullptr && thread->isThreadRunning())
        thread->stopThread (500);
}

PadState GamepadReader::state() const noexcept
{
    return snapshot.read();
}

bool GamepadReader::popEvent (PadEvent& out) noexcept
{
    const auto ready = eventFifo.getNumReady();

    if (ready <= 0)
        return false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    eventFifo.prepareToRead (1, start1, size1, start2, size2);

    if (size1 > 0)
        out = events[(size_t) start1];
    else if (size2 > 0)
        out = events[(size_t) start2];
    else
        return false;

    eventFifo.finishedRead (size1 + size2);
    return true;
}

void GamepadReader::run()
{
    Ticker ticker (*thread);
    highResTimer.store (ticker.isHighResolution(), std::memory_order_relaxed);

    PadState previous;
    auto lastTick    = juce::Time::getHighResolutionTicks();
    auto windowStart = lastTick;
    const auto ticksPerSecond = (double) juce::Time::getHighResolutionTicksPerSecond();

    int   pollsThisWindow = 0;
    float rumbleLevel = 0.0f;
    bool  rumbleWasRunning = false;

    while (! thread->threadShouldExit())
    {
        ticker.wait();

        if (thread->threadShouldExit())
            break;

        PadState now;
        source->poll (now);

        // Button crossings before the snapshot, so anything reading an event has
        // already-current axis values waiting for it rather than the ones from
        // the previous poll.
        if (now.connected && previous.connected)
        {
            const auto changed = (std::uint16_t) (now.buttons ^ previous.buttons);

            if (changed != 0)
            {
                const auto stamp = juce::Time::getMillisecondCounter();

                for (const auto button : kAllButtons)
                {
                    if ((changed & (std::uint16_t) button) == 0)
                        continue;

                    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
                    eventFifo.prepareToWrite (1, start1, size1, start2, size2);

                    // A full queue means nothing has read it for a quarter of a
                    // second of frantic playing. Dropping the oldest would be
                    // worse than dropping this one: a note-off outranks a
                    // note-on that nobody has seen yet.
                    if (size1 + size2 <= 0)
                        break;

                    const int index = size1 > 0 ? start1 : start2;
                    events[(size_t) index] = { (std::uint16_t) button,
                                               (now.buttons & (std::uint16_t) button) != 0,
                                               stamp };
                    eventFifo.finishedWrite (1);
                }
            }
        }

        snapshot.write (now);
        previous = now;

        // Haptics. Sending is only worth a driver call while something is
        // actually shaking, plus the one message that stops it.
        {
            const auto requested = rumbleRequest.exchange (0.0f, std::memory_order_relaxed);
            rumbleLevel = std::max (rumbleLevel, requested);

            if (rumbleLevel > 0.002f || rumbleWasRunning)
            {
                source->setRumble (rumbleLevel, rumbleLevel * 0.55f);
                rumbleWasRunning = rumbleLevel > 0.002f;

                // About twenty-five milliseconds to nothing: long enough to feel
                // as a knock, short enough that twenty a second stay separate.
                rumbleLevel *= 0.72f;

                if (rumbleLevel <= 0.002f)
                    rumbleLevel = 0.0f;
            }
        }

        // Timing. The worst interval is what tells the truth — an average of
        // 250 Hz with one 40 ms stall in it is not a 250 Hz loop.
        const auto tick   = juce::Time::getHighResolutionTicks();
        const auto gapMs  = (float) ((double) (tick - lastTick) * 1000.0 / ticksPerSecond);
        lastTick = tick;

        if (gapMs > worstMs.load (std::memory_order_relaxed))
            worstMs.store (gapMs, std::memory_order_relaxed);

        ++pollsThisWindow;

        const auto windowMs = (double) (tick - windowStart) * 1000.0 / ticksPerSecond;

        if (windowMs >= 500.0)
        {
            pollHz.store ((float) (pollsThisWindow * 1000.0 / windowMs),
                          std::memory_order_relaxed);
            pollsThisWindow = 0;
            windowStart = tick;
        }
    }

    source->threadStopping();
}

} // namespace chordtroller

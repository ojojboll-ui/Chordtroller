#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>

namespace chordtroller
{

/**
    A host parameter the gamepad can also turn.

    Two things move these: the host — automation, a Remote Override, a CV input
    — and the pad. The audio thread owns the value it plays. At the top of each
    block it takes the host's value if the host has moved it since it last
    looked; when the pad turns the value instead, the new value is handed to the
    message thread, which tells the host — begin, set, end — so Reason records
    the pad's moves as automation like any other knob.

    Whoever moved it last wins. The one trap is the echo: the value the message
    thread sends comes straight back as a host change a moment later, possibly
    after the pad has already moved on, and taking it then would snap the knob
    back a little. So a host value equal to the last one sent is ignored.
*/
class PadParameter
{
public:
    explicit PadParameter (juce::AudioParameterFloat& p) noexcept
        : param (p), value (p.get()), seen (p.get()) {}

    //== audio thread =========================================================

    /** Once per block, before get(). */
    void pull() noexcept
    {
        const float host = param.get();

        if (host == seen)
            return;

        seen = host;

        if (std::abs (host - sent.load (std::memory_order_relaxed)) > 1.0e-6f)
            value = host;
    }

    float get() const noexcept { return value; }

    /** The pad moved it. */
    void turn (float newValue) noexcept
    {
        value = juce::jlimit (0.0f, 1.0f, newValue);
        pending.store (value, std::memory_order_relaxed);
        dirty.store (true, std::memory_order_relaxed);
    }

    //== message thread =======================================================

    /** From a timer. Sends the pad's latest value, and closes the gesture once
        the pad has been still for a moment. */
    void flush (juce::uint32 nowMs)
    {
        if (dirty.exchange (false, std::memory_order_relaxed))
        {
            if (! inGesture)
            {
                param.beginChangeGesture();
                inGesture = true;
            }

            const float v = pending.load (std::memory_order_relaxed);
            sent.store (v, std::memory_order_relaxed);
            param.setValueNotifyingHost (param.convertTo0to1 (v));
            lastTurnMs = nowMs;
        }
        else if (inGesture && nowMs - lastTurnMs > 400)
        {
            param.endChangeGesture();
            inGesture = false;
        }
    }

private:
    juce::AudioParameterFloat& param;

    float value = 0.0f, seen = 0.0f;           // audio thread only

    std::atomic<float> pending { 0.0f }, sent { -1.0f };
    std::atomic<bool>  dirty { false };

    bool inGesture = false;                    // message thread only
    juce::uint32 lastTurnMs = 0;
};

} // namespace chordtroller

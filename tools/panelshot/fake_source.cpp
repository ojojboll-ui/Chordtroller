// A scripted gamepad, standing in for GamepadSource.cpp in the check build:
// the reader thread polls it like a real pad, so events and snapshots go
// through exactly the code the instrument uses.
#include "input/GamepadSource.h"

#include <mutex>

namespace chordtroller
{
    std::mutex fakePadLock;
    PadState   fakePad;

    namespace
    {
        class ScriptedSource final : public GamepadSource
        {
        public:
            bool poll (PadState& out) override
            {
                const std::lock_guard<std::mutex> lock (fakePadLock);
                out = fakePad;
                out.connected = true;
                return true;
            }

            void setRumble (float, float) override {}
            const char* name() const override { return "scripted"; }
        };
    }

    std::unique_ptr<GamepadSource> GamepadSource::createForPlatform()
    {
        return std::make_unique<ScriptedSource>();
    }
}

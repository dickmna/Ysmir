#pragma once
#include <cstdint>
#include <string>

namespace ysmir::config
{
    // One INI reader for every adapter. Configuration is read at startup only.
    bool Bool(const char* section, const char* key, bool fallback);
    float Float(const char* section, const char* key, float fallback);
    std::string String(const char* section, const char* key, const char* fallback);
}

namespace ysmir::progression
{
    struct ScalingSnapshot
    {
        std::uint32_t souls{};
        float distance{1}, magnitude{1}, duration{1}, area{1}, collision{1};
        float vfx{1}, density{1}, cooldown{1};
    };

    void Initialize();
    bool Enabled();
    // Capture is the sole soul-count read for a player shout dispatch.
    ScalingSnapshot Capture();
    void UpdateClock();
    double Now();
    bool Paused();
}

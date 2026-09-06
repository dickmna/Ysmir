#include "progression.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace ysmir::config
{
    std::string String(const char* section, const char* key, const char* fallback)
    {
        char value[1024]{};
        GetPrivateProfileStringA(section, key, fallback, value, sizeof(value), ".\\Data\\SKSE\\Plugins\\Ysmir.ini");
        return value;
    }
    bool Bool(const char* section, const char* key, bool fallback)
    {
        auto value = String(section, key, fallback ? "1" : "0");
        if (value == "1" || _stricmp(value.c_str(), "true") == 0) return true;
        if (value == "0" || _stricmp(value.c_str(), "false") == 0) return false;
        return fallback;
    }
    float Float(const char* section, const char* key, float fallback)
    {
        auto value = String(section, key, std::to_string(fallback).c_str());
        char* end{};
        auto parsed = std::strtof(value.c_str(), &end);
        while (end && *end == ' ') ++end;
        return end != value.c_str() && end && *end == '\0' && std::isfinite(parsed) ? parsed : fallback;
    }
}

namespace
{
    bool enabled{true}, countSpent{}, vfxEnabled{true}, cooldownEnabled{true};
    std::array<float,5> rates{};
    float commonRate{.05f}, visualRatio{.6f}, heightAt4x{120}, densityAt4x{2.2f}, cooldownRate{.05f};
    double gameClock{};
    std::chrono::steady_clock::time_point lastTick{};
    bool paused{true};

    std::uint32_t ReadSouls()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;
        auto* owner = player->AsActorValueOwner();
        const auto unspent = owner ? owner->GetActorValue(RE::ActorValue::kDragonSouls) : 0.f;
        double count = std::isfinite(unspent) ? std::floor(std::max(0.f, unspent)) : 0;
        if (countSpent) {
            // Optional SP approximation; count unique words, not duplicate SHOU entries.
            std::unordered_set<RE::TESWordOfPower*> words;
            if (auto* data = RE::TESDataHandler::GetSingleton())
                for (auto* shout : data->GetFormArray<RE::TESShout>())
                    if (shout && player->HasShout(shout))
                        for (const auto& word : shout->variations)
                            if (word.word && word.word->GetKnown()) words.insert(word.word);
            count += static_cast<double>(words.size());
        }
        return static_cast<std::uint32_t>(std::min(count, static_cast<double>(UINT32_MAX)));
    }
}

namespace ysmir::progression
{
    void Initialize()
    {
        enabled = config::Bool("General", "bEnable", true);
        spdlog::set_level(config::Bool("General", "bDebugLogging", false) ? spdlog::level::debug : spdlog::level::info);
        countSpent = config::Bool("DragonSouls", "bApproximateSpentSoulsWithKnownWords", false);
        const std::array switches{"bScaleDistance", "bScaleMagnitude", "bScaleDuration", "bScaleArea", "bScaleCollisionRadius"};
        const std::array keys{"fDistancePerSoul", "fMagnitudePerSoul", "fDurationPerSoul", "fAreaPerSoul", "fCollisionRadiusPerSoul"};
        for (std::size_t i=0; i<rates.size(); ++i)
            rates[i] = config::Bool("DragonSouls", switches[i], true) ? std::clamp(config::Float("DragonSouls", keys[i], .05f), 0.f, 1.f) : 0.f;
        vfxEnabled = config::Bool("DragonSouls", "bScaleVFX", true);
        cooldownEnabled = config::Bool("DragonSouls", "bScaleCooldown", true);
        commonRate = std::clamp(config::Float("DragonSouls", "fCommonMultiplierPerSoul", .05f), 0.f, 1.f);
        visualRatio = std::clamp(config::Float("DragonSouls", "fVFXGrowthRatio", .6f), 0.f, 100.f);
        heightAt4x = std::clamp(config::Float("DragonSouls", "fTrueConeHeightAt4x", 120.f), 0.f, 1000000.f);
        densityAt4x = std::clamp(config::Float("DragonSouls", "fTrueConeDensityAt4x", 2.2f), 1.f, 1000.f);
        cooldownRate = std::clamp(config::Float("DragonSouls", "fCooldownReductionPerSoul", .05f), 0.f, .999999f);
    }
    bool Enabled() { return enabled; }
    ScalingSnapshot Capture()
    {
        ScalingSnapshot s;
        s.souls = ReadSouls();
        if (!enabled) return s;
        const auto linear = [&](std::size_t i) { return 1.f + static_cast<float>(s.souls) * rates[i]; };
        s.distance=linear(0); s.magnitude=linear(1); s.duration=linear(2); s.area=linear(3); s.collision=linear(4);
        if (vfxEnabled) {
            s.vfx = 1.f + visualRatio * static_cast<float>(s.souls) * commonRate;
            const auto progress = (s.vfx-1.f)/3.f;
            s.height = heightAt4x*progress;
            s.density = 1.f + (densityAt4x-1.f)*progress;
        }
        if (cooldownEnabled)
            s.cooldown = static_cast<float>(std::max(std::pow(1.0-static_cast<double>(cooldownRate), s.souls),
                static_cast<double>(std::numeric_limits<float>::denorm_min())));
        return s;
    }
    void UpdateClock()
    {
        const auto tick=std::chrono::steady_clock::now();
        auto* ui=RE::UI::GetSingleton();
        const bool nowPaused = !ui || ui->GameIsPaused();
        if (lastTick != std::chrono::steady_clock::time_point{} && !paused && !nowPaused)
            gameClock += std::chrono::duration<double>(tick-lastTick).count();
        lastTick=tick;
        paused=nowPaused;
    }
    double Now() { return gameClock; }
    bool Paused() { return paused; }
}

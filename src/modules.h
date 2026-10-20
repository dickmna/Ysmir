#pragma once

#include <RE/Skyrim.h>
#include <SKSE/Interfaces.h>
#include "progression.h"

namespace ysmir::storm_call
{
	bool Initialize();
	bool Available();
	void Configure();
	void BeforeShout(RE::Actor& player);
	void HandleMessage(SKSE::MessagingInterface::Message* a_message);
	void FrameUpdate();
}

namespace ysmir::elemental_fury
{
	void HandleMessage(SKSE::MessagingInterface::Message* a_message);
	void OnShout(RE::TESShout* shout, std::uint32_t tier, const progression::ScalingSnapshot& snapshot);
}

namespace ysmir::true_cone
{
    void Initialize();
    bool PatchVisuals();
    struct Geometry { float radius{}; };
    Geometry Capture(RE::ConeProjectile* projectile);
    void ApplyCollision(RE::ConeProjectile* projectile, const Geometry& geometry, const progression::ScalingSnapshot& snapshot);
}

namespace ysmir::projectile_visuals
{
    void Initialize();
    void SetStormBase(RE::BGSProjectile* base);
    void TrackStorm(const RE::ProjectileHandle& handle, const progression::ScalingSnapshot& snapshot);
    void Clear();
    void FrameUpdate();
}

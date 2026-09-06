#pragma once

#include <RE/Skyrim.h>
#include <SKSE/Interfaces.h>
#include "progression.h"

namespace ysmir::storm_call
{
	bool Initialize();
	void HandleMessage(SKSE::MessagingInterface::Message* a_message);
	void FrameUpdate();
	bool TryConsumeVoiceFire(RE::Actor& a_actor, const progression::ScalingSnapshot& snapshot);
}

namespace ysmir::elemental_fury
{
	void HandleMessage(SKSE::MessagingInterface::Message* a_message);
	void OnShout(RE::TESShout* shout, std::uint32_t tier, const progression::ScalingSnapshot& snapshot);
}

namespace ysmir::true_cone
{
    struct Geometry { float radius{}, tangent{}, correctedTangent{}; };
    void Initialize();
    Geometry Capture(RE::ConeProjectile* projectile);
    void ApplyCollision(RE::ConeProjectile* projectile, const Geometry& geometry, const progression::ScalingSnapshot& snapshot);
    float VisualScale(RE::ConeProjectile* projectile, const Geometry& geometry, const progression::ScalingSnapshot& snapshot);
    bool PatchVisuals();
}

namespace ysmir::projectile_visuals
{
    void Initialize();
    void SetStormBase(RE::BGSProjectile* base);
    void TrackStorm(const RE::ProjectileHandle& handle, const progression::ScalingSnapshot& snapshot);
    void Clear();
    void FrameUpdate();
}

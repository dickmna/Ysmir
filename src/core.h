#pragma once

#include <RE/Skyrim.h>
#include <SKSE/Interfaces.h>
#include "progression.h"

namespace ysmir::core
{
	using VoiceExecute = bool (*)(RE::VoiceSpellFireHandler*, RE::Actor&, const RE::BSFixedString&);

	bool Initialize();
	void HandleMessage(SKSE::MessagingInterface::Message* a_message);
	void FrameUpdate();
	progression::ScalingSnapshot CurrentScaling();
	progression::ScalingSnapshot ScalingForSpell(RE::SpellItem* spell);
	progression::ScalingSnapshot ScalingForProjectile(RE::Projectile* projectile);
	void TrackProjectile(const RE::ProjectileHandle& handle, const progression::ScalingSnapshot& snapshot);
	bool CastFeatureSpell(RE::SpellItem* spell, RE::Actor& actor, RE::Actor* target, const progression::ScalingSnapshot& snapshot);
	RE::ProjectileHandle LaunchFeatureSpell(RE::SpellItem* spell, RE::Actor& blame, RE::Actor& target,
		const RE::NiPoint3& origin, const RE::NiPoint3& destination, const progression::ScalingSnapshot& snapshot);
	RE::TESShout* PrivateShout(RE::FormID official);

	bool IsPlayerActor(const RE::Actor* a_actor);
	bool IsPlayerOwnedProjectile(RE::Projectile* a_projectile);
	bool IsPlayerVoiceSpellRuntimeAuthorized(const RE::SpellItem* a_spell);
}

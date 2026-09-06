#include <RE/Skyrim.h>
#include <RE/B/BSVisit.h>
#include <SKSE/SKSE.h>

#include "core.h"
#include "modules.h"
#include "RuntimeForms.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr auto PLUGIN_NAME = "Ysmir.StormCall";
	constexpr auto SKYRIM_ESM = "Skyrim.esm";
	constexpr auto UPDATE_ESM = "Update.esm";

	constexpr RE::FormID STORM_CALL_SHOUT = 0x0007097D;
	constexpr RE::FormID PLAYER_REFERENCE = 0x00000014;
	constexpr RE::FormID STORM_WEATHER = 0x000D4886;
	constexpr RE::FormID STORM_GLOBAL = 0x00101DAB;
	constexpr std::string_view STORM_PROJECTILE_EDITOR_ID = "YSMIR_StormCallProjectile";
	constexpr RE::FormID SHOCK_IMOD = 0x000A9D93;
	constexpr RE::FormID IMPACT_SOUND_NEAR = 0x000EB383;
	constexpr RE::FormID WEATHER_EXCEPTION_LIST = 0x00000998;
	constexpr std::array<RE::FormID, 3> VOICE_SPELLS{
		0x00018609,
		0x0001860A,
		0x0001860D
	};
	constexpr std::uint32_t MakeFourCC(char a, char b, char c, char d) noexcept
	{
		return static_cast<std::uint32_t>(a) |
		       (static_cast<std::uint32_t>(b) << 8) |
		       (static_cast<std::uint32_t>(c) << 16) |
		       (static_cast<std::uint32_t>(d) << 24);
	}

	constexpr std::uint32_t SERIALIZATION_ID = MakeFourCC('Y', 'S', 'M', 'R');
	constexpr std::uint32_t SERIALIZATION_RECORD = MakeFourCC('S', 'T', 'R', 'M');
	constexpr std::uint32_t SERIALIZATION_VERSION = 3;
	constexpr std::uint32_t MAX_SERIALIZED_RECORD_BYTES = 64U * 1024U;

	struct Settings
	{
		bool enable{ true };
		bool pauseInInteriors{ true };
		bool blameCaster{ false };
		float searchRadius{ 12800.0F };
		std::array<float, 3> damage{ 30.0F, 60.0F, 120.0F };
		std::array<float, 3> duration{ 60.0F, 120.0F, 180.0F };

		float initialDelayMin{ 3.0F };
		float initialDelayMax{ 6.0F };
		std::array<float, 3> recastDelayMin{ 3.0F, 0.75F, 0.25F };
		std::array<float, 3> recastDelayMax{ 6.0F, 6.75F, 5.25F };
		float passStaggerMin{ 0.12F };
		float passStaggerMax{ 0.35F };
		float noTargetRetry{ 0.25F };

		std::array<float, 3> visualSpread{ 100.0F, 300.0F, 600.0F };
		float strikeHeight{ 3500.0F };
		float targetHeight{ 96.0F };
		float imodFadeDistance{ 3072.0F };
	};

	struct Forms
	{
		RE::TESShout* stormCall{ nullptr };
		RE::TESWeather* weather{ nullptr };
		RE::TESGlobal* stormGlobal{ nullptr };
		RE::BGSProjectile* projectile{ nullptr };
		RE::TESImageSpaceModifier* shockImod{ nullptr };
		RE::BGSSoundDescriptorForm* impactSoundNear{ nullptr };
		RE::BGSListForm* weatherExceptions{ nullptr };
	};

	Settings g_settings;
	Forms g_forms;
	float g_stormProjectileRange{};
	float g_stormProjectileSpeed{};
	float g_stormProjectileCollision{};
	bool g_formsResolved{ false };

	struct SoulScalingContext
	{
		float distance{ 1.0F };
		float magnitude{ 1.0F };
		float duration{ 1.0F };
		float area{ 1.0F };
		float collision{ 1.0F };
		float vfx{ 1.0F };
	};
	static_assert(std::is_trivially_copyable_v<SoulScalingContext>);

	float SanitizeMultiplier(float a_value)
	{
		return std::isfinite(a_value) && a_value > 0.0F ? a_value : 1.0F;
	}

	using ysmir::config::Bool;
	using ysmir::config::Float;

	float ClampDelay(float a_value)
	{
		return std::clamp(a_value, 0.0F, 3600.0F);
	}

	void NormalizeRange(float& a_min, float& a_max)
	{
		a_min = ClampDelay(a_min);
		a_max = std::max(a_min, ClampDelay(a_max));
	}

	void LoadSettings()
	{
		g_settings.enable = Bool("Storm", "bEnable", g_settings.enable);
		g_settings.pauseInInteriors = Bool(
			"Storm", "bPauseInInteriors", g_settings.pauseInInteriors);
		g_settings.blameCaster = Bool("Storm", "bBlameCaster", g_settings.blameCaster);
		g_settings.searchRadius = std::clamp(
			Float("Storm.Gameplay", "fSearchRadius", g_settings.searchRadius), 0.0F, 1000000.0F);
		g_settings.damage[0] = std::clamp(
			Float("Storm.Gameplay", "fDamageWord1", g_settings.damage[0]), 0.0F, 1000000.0F);
		g_settings.damage[1] = std::clamp(
			Float("Storm.Gameplay", "fDamageWord2", g_settings.damage[1]), 0.0F, 1000000.0F);
		g_settings.damage[2] = std::clamp(
			Float("Storm.Gameplay", "fDamageWord3", g_settings.damage[2]), 0.0F, 1000000.0F);
		g_settings.duration[0] = std::clamp(
			Float("Storm.Gameplay", "fDurationWord1", g_settings.duration[0]), 0.1F, 86400.0F);
		g_settings.duration[1] = std::clamp(
			Float("Storm.Gameplay", "fDurationWord2", g_settings.duration[1]), 0.1F, 86400.0F);
		g_settings.duration[2] = std::clamp(
			Float("Storm.Gameplay", "fDurationWord3", g_settings.duration[2]), 0.1F, 86400.0F);

		g_settings.initialDelayMin = Float(
			"Storm.Timing", "fInitialDelayMin", g_settings.initialDelayMin);
		g_settings.initialDelayMax = Float(
			"Storm.Timing", "fInitialDelayMax", g_settings.initialDelayMax);
		g_settings.recastDelayMin[0] = Float(
			"Storm.Timing", "fRecastDelayMinWord1", g_settings.recastDelayMin[0]);
		g_settings.recastDelayMax[0] = Float(
			"Storm.Timing", "fRecastDelayMaxWord1", g_settings.recastDelayMax[0]);
		g_settings.recastDelayMin[1] = Float(
			"Storm.Timing", "fRecastDelayMinWord2", g_settings.recastDelayMin[1]);
		g_settings.recastDelayMax[1] = Float(
			"Storm.Timing", "fRecastDelayMaxWord2", g_settings.recastDelayMax[1]);
		g_settings.recastDelayMin[2] = Float(
			"Storm.Timing", "fRecastDelayMinWord3", g_settings.recastDelayMin[2]);
		g_settings.recastDelayMax[2] = Float(
			"Storm.Timing", "fRecastDelayMaxWord3", g_settings.recastDelayMax[2]);
		g_settings.passStaggerMin = Float(
			"Storm.Timing", "fPassStaggerMin", g_settings.passStaggerMin);
		g_settings.passStaggerMax = Float(
			"Storm.Timing", "fPassStaggerMax", g_settings.passStaggerMax);
		g_settings.noTargetRetry = std::clamp(
			Float("Storm.Timing", "fNoTargetRetry", g_settings.noTargetRetry), 0.05F, 3600.0F);

		NormalizeRange(g_settings.initialDelayMin, g_settings.initialDelayMax);
		for (std::size_t index = 0; index < 3; ++index) {
			NormalizeRange(g_settings.recastDelayMin[index], g_settings.recastDelayMax[index]);
		}
		NormalizeRange(g_settings.passStaggerMin, g_settings.passStaggerMax);

		g_settings.visualSpread[0] = std::clamp(
			Float("Storm.Visual", "fSpreadWord1", g_settings.visualSpread[0]), 0.0F, 100000.0F);
		g_settings.visualSpread[1] = std::clamp(
			Float("Storm.Visual", "fSpreadWord2", g_settings.visualSpread[1]), 0.0F, 100000.0F);
		g_settings.visualSpread[2] = std::clamp(
			Float("Storm.Visual", "fSpreadWord3", g_settings.visualSpread[2]), 0.0F, 100000.0F);
		g_settings.strikeHeight = std::clamp(
			Float("Storm.Visual", "fStrikeHeight", g_settings.strikeHeight), 128.0F, 100000.0F);
		g_settings.targetHeight = std::clamp(
			Float("Storm.Visual", "fTargetHeight", g_settings.targetHeight), -100000.0F, 100000.0F);
		g_settings.imodFadeDistance = std::clamp(
			Float("Storm.Visual", "fImodFadeDistance", g_settings.imodFadeDistance), 1.0F, 1000000.0F);
	}

	template <class T>
	T* LookupForm(RE::FormID a_formID, const char* a_file)
	{
		const auto dataHandler = RE::TESDataHandler::GetSingleton();
		return dataHandler ? dataHandler->LookupForm<T>(a_formID, a_file) : nullptr;
	}

	bool ResolveForms()
	{
		g_forms.stormCall = LookupForm<RE::TESShout>(STORM_CALL_SHOUT, SKYRIM_ESM);
		g_forms.weather = LookupForm<RE::TESWeather>(STORM_WEATHER, SKYRIM_ESM);
		g_forms.stormGlobal = LookupForm<RE::TESGlobal>(STORM_GLOBAL, SKYRIM_ESM);
		g_forms.projectile = LookupForm<RE::BGSProjectile>(
			ysmir::forms::StormProjectile, ysmir::forms::PluginName);
		g_forms.shockImod = LookupForm<RE::TESImageSpaceModifier>(SHOCK_IMOD, SKYRIM_ESM);
		g_forms.impactSoundNear = LookupForm<RE::BGSSoundDescriptorForm>(IMPACT_SOUND_NEAR, SKYRIM_ESM);
		g_forms.weatherExceptions = LookupForm<RE::BGSListForm>(WEATHER_EXCEPTION_LIST, UPDATE_ESM);
		const bool required =
			g_forms.stormCall &&
			g_forms.weather &&
			g_forms.stormGlobal &&
			g_forms.projectile;
		g_formsResolved = required;
		if (!required) {
			SKSE::log::critical(
				"Required Storm Call forms are missing: shout={}, weather={}, global={}, privateProjectile={} ({})",
				static_cast<const void*>(g_forms.stormCall),
				static_cast<const void*>(g_forms.weather),
				static_cast<const void*>(g_forms.stormGlobal),
				static_cast<const void*>(g_forms.projectile),
				STORM_PROJECTILE_EDITOR_ID);
			return false;
		}

		g_stormProjectileRange = g_forms.projectile->data.range;
		g_stormProjectileSpeed = g_forms.projectile->data.speed;
		g_stormProjectileCollision = g_forms.projectile->data.collisionRadius;
		SKSE::log::info(
			"Resolved the Storm Call runtime form set with private projectile {} ({:08X})",
			STORM_PROJECTILE_EDITOR_ID,
			g_forms.projectile->GetFormID());
		return true;
	}

	struct SavedStorm
	{
		RE::FormID casterFormID{ 0 };
		std::uint32_t word{ 1 };
		float remaining{ 0.0F };
		float cycleDue{ 0.0F };
		std::array<float, 2> pendingPassDue{};
		std::uint32_t pendingPassCount{ 0 };
		SoulScalingContext scaling{};
	};
	static_assert(std::is_trivially_copyable_v<SavedStorm>);

	bool SkipRecordData(SKSE::SerializationInterface* a_interface, std::uint32_t a_length)
	{
		if (!a_interface) {
			return false;
		}
		std::array<std::byte, 256> scratch{};
		std::uint32_t remaining = a_length;
		while (remaining > 0) {
			const auto request = std::min<std::uint32_t>(remaining, static_cast<std::uint32_t>(scratch.size()));
			const auto read = a_interface->ReadRecordData(scratch.data(), request);
			if (read != request) {
				return false;
			}
			remaining -= read;
		}
		return true;
	}

	class StormRuntime
	{
	public:
		static StormRuntime& GetSingleton()
		{
			static StormRuntime singleton;
			return singleton;
		}

		bool HandleVoiceFire(RE::Actor& a_actor, const ysmir::progression::ScalingSnapshot& snapshot)
		{
			if (!g_settings.enable || !g_formsResolved ||
			    std::addressof(a_actor) != RE::PlayerCharacter::GetSingleton() ||
			    a_actor.GetCurrentShout() != g_forms.stormCall ||
			    !ysmir::core::HasPrivatePlayerShoutMapping(g_forms.stormCall)) {
				return false;
			}

			std::uint32_t word = 1;
			if (const auto high = a_actor.GetHighProcess()) {
				const auto variation = static_cast<std::int32_t>(high->currentShoutVariation);
				if (variation >= 0 && variation <= 2) {
					word = static_cast<std::uint32_t>(variation + 1);
				}
			} else {
				const auto variation = a_actor.GetCurrentShoutLevel();
				if (variation >= 0 && variation <= 2) {
					word = static_cast<std::uint32_t>(variation + 1);
				}
			}

			StartStorm(std::addressof(a_actor), word, snapshot);
			return true;
		}

		void FrameUpdate()
		{
			
			Tick();
		}

		void StartStorm(RE::Actor* a_caster, std::uint32_t a_word, const ysmir::progression::ScalingSnapshot& snapshot)
		{
			if (!ysmir::core::IsPlayerActor(a_caster) || !g_formsResolved) {
				return;
			}

			a_word = std::clamp(a_word, 1U, 3U);
			const auto index = static_cast<std::size_t>(a_word - 1);
			_session = {};
			_session.active = true;
			_session.caster = RE::ActorHandle{ a_caster };
			_session.word = a_word;
			_session.scaling = snapshot;
			_session.remaining = g_settings.duration[index] * _session.scaling.duration;
			_session.cycleDue = Random(g_settings.initialDelayMin, g_settings.initialDelayMax);
			_lastTick = ysmir::progression::Now();
			_weatherDue = 0.0F;

			g_forms.stormGlobal->value = 1.0F;
			MaintainStormWeather();

			const auto winningSpell = g_forms.stormCall->variations[index].spell;
			if (!winningSpell || winningSpell->GetFormID() != VOICE_SPELLS[index]) {
				SKSE::log::warn(
					"Storm Call word {} uses conflicting voice spell {:08X}; the DLL controller takes precedence",
					a_word,
					winningSpell ? winningSpell->GetFormID() : 0);
			}
			SKSE::log::info(
				"Started Storm Call word {} for {:08X}: damage={}, duration={}, radius={}, passes={}, soul scaling [mag={}, dur={}, area={}, dist={}, collision={}, vfx={}]",
				a_word,
				a_caster->GetFormID(),
				g_settings.damage[index] * _session.scaling.magnitude,
				_session.remaining,
				CurrentSearchRadius(),
				a_word,
				_session.scaling.magnitude,
				_session.scaling.duration,
				_session.scaling.area,
				_session.scaling.distance,
				_session.scaling.collision,
				_session.scaling.vfx);
		}

		void Tick()
		{
			if (!_session.active) {
				return;
			}

			const auto now = ysmir::progression::Now();
			if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
				_lastTick = now;
				return;
			}
			const float delta = std::clamp(
				static_cast<float>(now - _lastTick),
				0.0F,
				std::numeric_limits<float>::max());
			_lastTick = now;
			_session.remaining -= delta;
			_weatherDue -= delta;

			if (_session.remaining <= 0.0F ||
			    !g_forms.stormGlobal ||
			    g_forms.stormGlobal->value != 1.0F) {
				StopStorm(true);
				return;
			}

			const auto caster = _session.caster.get();
			if (!caster || caster->IsDead() || !ysmir::core::IsPlayerActor(caster.get())) {
				StopStorm(true);
				return;
			}

			if (_weatherDue <= 0.0F) {
				MaintainStormWeather();
				_weatherDue = 1.0F;
			}

			if (ShouldPauseForInterior(caster.get())) {
				if (_session.cycleDue <= 0.0F) {
					const auto index = static_cast<std::size_t>(_session.word - 1);
					_session.cycleDue = Random(
						g_settings.recastDelayMin[index],
						g_settings.recastDelayMax[index]);
				}
				// Freeze attack cadence indoors. Expired stagger passes are nudged into
				// the future so leaving an interior never releases a queued burst.
				for (std::uint32_t index = 0; index < _session.pendingPassCount; ++index) {
					if (_session.pendingPassDue[index] <= 0.0F) {
						_session.pendingPassDue[index] = 0.12F * static_cast<float>(index + 1);
					}
				}
				return;
			}

			_session.cycleDue -= delta;
			for (std::uint32_t index = 0; index < _session.pendingPassCount; ++index) {
				_session.pendingPassDue[index] -= delta;
			}

			RunPendingPasses(caster.get());
			if (_session.cycleDue <= 0.0F) {
				RunCycle(caster.get());
			}
		}

		void StopStorm(bool a_releaseWeather)
		{
			const bool wasActive = _session.active;
			_session = {};

			if (g_forms.stormGlobal) {
				g_forms.stormGlobal->value = 0.0F;
			}
			if (a_releaseWeather && _weatherOwned) {
				if (const auto sky = RE::Sky::GetSingleton();
				    sky && sky->overrideWeather == g_forms.weather) {
					sky->ReleaseWeatherOverride();
				}
			}
			_weatherOwned = false;

			if (wasActive) {
				SKSE::log::info("Stopped Storm Call runtime controller");
			}
		}

		void Save(SKSE::SerializationInterface* a_interface)
		{
			if (!a_interface || !_session.active) {
				return;
			}

			const auto caster = _session.caster.get();
			if (!caster || !ysmir::core::IsPlayerActor(caster.get())) {
				SKSE::log::warn("Refusing to serialize a Storm Call session not owned by the player");
				return;
			}

			SavedStorm saved;
			saved.casterFormID = caster->GetFormID();
			saved.word = _session.word;
			saved.remaining = _session.remaining;
			saved.cycleDue = _session.cycleDue;
			saved.pendingPassDue = _session.pendingPassDue;
			saved.pendingPassCount = _session.pendingPassCount;
			saved.scaling = { _session.scaling.distance, _session.scaling.magnitude, _session.scaling.duration, _session.scaling.area, _session.scaling.collision, _session.scaling.vfx };
			if (!a_interface->WriteRecord(SERIALIZATION_RECORD, SERIALIZATION_VERSION, saved)) {
				SKSE::log::error("Failed to serialize the active Storm Call");
			}
		}

		void Load(SKSE::SerializationInterface* a_interface)
		{
			_pendingResume.reset();
			if (!a_interface) {
				return;
			}

			std::uint32_t type{};
			std::uint32_t version{};
			std::uint32_t length{};
			while (a_interface->GetNextRecordInfo(type, version, length)) {
				if (type != SERIALIZATION_RECORD || version != SERIALIZATION_VERSION ||
				    length != sizeof(SavedStorm)) {
					SKSE::log::warn(
						"Skipping unknown serialization record {:08X} v{} ({} bytes)",
						type,
						version,
						length);
					if (length > MAX_SERIALIZED_RECORD_BYTES) {
						SKSE::log::error(
							"Serialization record exceeds the {} byte safety limit; aborting Storm Call load",
							MAX_SERIALIZED_RECORD_BYTES);
						return;
					}
					if (!SkipRecordData(a_interface, length)) {
						SKSE::log::error("Failed to skip malformed serialization record");
						break;
					}
					continue;
				}

				SavedStorm saved;
				if (a_interface->ReadRecordData(saved) != sizeof(SavedStorm)) {
					SKSE::log::error("Failed to read the serialized Storm Call");
					continue;
				}
				if (!std::isfinite(saved.remaining) || !std::isfinite(saved.cycleDue) || saved.remaining <= 0.0F) {
					SKSE::log::warn("Discarding Storm Call serialization with non-finite or expired timing");
					continue;
				}
				saved.word = std::clamp(saved.word, 1U, 3U);
				saved.cycleDue = std::clamp(saved.cycleDue, 0.0F, saved.remaining);
				saved.pendingPassCount = std::min(saved.pendingPassCount, 2U);
				for (std::uint32_t index = 0; index < saved.pendingPassCount; ++index) {
					if (!std::isfinite(saved.pendingPassDue[index])) {
						saved.pendingPassDue[index] = 0.0F;
					} else {
						saved.pendingPassDue[index] = std::clamp(
							saved.pendingPassDue[index], 0.0F, saved.remaining);
					}
				}
				saved.scaling.distance = SanitizeMultiplier(saved.scaling.distance);
				saved.scaling.magnitude = SanitizeMultiplier(saved.scaling.magnitude);
				saved.scaling.duration = SanitizeMultiplier(saved.scaling.duration);
				saved.scaling.area = SanitizeMultiplier(saved.scaling.area);
				saved.scaling.collision = SanitizeMultiplier(saved.scaling.collision);
				saved.scaling.vfx = SanitizeMultiplier(saved.scaling.vfx);

				RE::FormID resolved{};
				if (!a_interface->ResolveFormID(saved.casterFormID, resolved)) {
					SKSE::log::warn(
						"Could not resolve saved Storm Call caster {:08X}",
						saved.casterFormID);
					continue;
				}
				if (resolved != PLAYER_REFERENCE) {
					SKSE::log::warn(
						"Discarding legacy non-player Storm Call serialization for caster {:08X}",
						resolved);
					continue;
				}
				saved.casterFormID = resolved;
				_pendingResume = saved;
			}
		}

		void Revert()
		{
			StopStorm(true);
			_pendingResume.reset();
			ysmir::projectile_visuals::Clear();
			if (g_forms.projectile) {
				g_forms.projectile->data.range = g_stormProjectileRange;
				g_forms.projectile->data.speed = g_stormProjectileSpeed;
				g_forms.projectile->data.collisionRadius = g_stormProjectileCollision;
			}
		}

		void ResumePending()
		{
			if (!_pendingResume) {
				return;
			}

			const auto saved = *_pendingResume;
			_pendingResume.reset();
			auto* caster = RE::TESForm::LookupByID<RE::Actor>(saved.casterFormID);
			if (!caster || !ysmir::core::IsPlayerActor(caster) || saved.remaining <= 0.0F) {
				SKSE::log::warn("Discarding pending Storm Call state because its caster is not the player");
				return;
			}

			_session = {};
			_session.active = true;
			_session.caster = RE::ActorHandle{ caster };
			_session.word = std::clamp(saved.word, 1U, 3U);
			_session.remaining = saved.remaining;
			_session.cycleDue = saved.cycleDue;
			_session.pendingPassDue = saved.pendingPassDue;
			_session.pendingPassCount = std::min(saved.pendingPassCount, 2U);
			_session.scaling.distance = saved.scaling.distance;
_session.scaling.magnitude = saved.scaling.magnitude;
_session.scaling.duration = saved.scaling.duration;
_session.scaling.area = saved.scaling.area;
_session.scaling.collision = saved.scaling.collision;
_session.scaling.vfx = saved.scaling.vfx;
			_lastTick = ysmir::progression::Now();
			_weatherDue = 0.0F;
			if (!g_forms.stormGlobal) {
				_session = {};
				return;
			}
			g_forms.stormGlobal->value = 1.0F;
			MaintainStormWeather();
			SKSE::log::info(
				"Resumed saved Storm Call word {} with {:.2f}s remaining",
				_session.word,
				_session.remaining);
		}

	private:
		struct Session
		{
			bool active{ false };
			RE::ActorHandle caster;
			std::uint32_t word{ 1 };
			float remaining{ 0.0F };
			float cycleDue{ 0.0F };
			std::array<float, 2> pendingPassDue{};
			std::uint32_t pendingPassCount{ 0 };
			ysmir::progression::ScalingSnapshot scaling{};
		};

		StormRuntime() :
			_rng(std::random_device{}())
		{}

		float Random(float a_min, float a_max)
		{
			if (a_max <= a_min) {
				return a_min;
			}
			return std::uniform_real_distribution<float>(a_min, a_max)(_rng);
		}

		bool ShouldPauseForInterior(RE::Actor* a_caster) const
		{
			if (!g_settings.pauseInInteriors || !a_caster) {
				return false;
			}
			const auto cell = a_caster->GetParentCell();
			return !cell || cell->IsInteriorCell();
		}

		bool CanApplyStormWeather() const
		{
			const auto sky = RE::Sky::GetSingleton();
			if (!sky || sky->mode != RE::Sky::Mode::kFull) {
				return false;
			}
			return !g_forms.weatherExceptions ||
			       !g_forms.weatherExceptions->HasForm(sky->currentWeather);
		}

		void MaintainStormWeather()
		{
			// Retry after returning outdoors. A recast in a protected area must
			// not forget an override this controller already owns.
			if (!g_forms.weather || !CanApplyStormWeather()) {
				return;
			}
			if (const auto sky = RE::Sky::GetSingleton()) {
				if (sky->overrideWeather != g_forms.weather) {
					sky->SetWeather(g_forms.weather, true, true);
				}
				_weatherOwned = true;
			}
		}

		float CurrentSearchRadius() const
		{
			const auto scaled = static_cast<double>(g_settings.searchRadius) * _session.scaling.area;
			return static_cast<float>(std::min(scaled, static_cast<double>(std::numeric_limits<float>::max())));
		}

		bool IsValidTarget(RE::Actor* a_target, RE::Actor* a_caster) const
		{
			if (!a_target || !a_caster || a_target == a_caster ||
			    a_target->IsDead() || !a_target->GetParentCell() ||
			    !a_target->Is3DLoaded()) {
				return false;
			}
			if (!a_target->IsHostileToActor(a_caster)) {
				return false;
			}
			return a_target->GetPosition().GetDistance(a_caster->GetPosition()) <=
			       CurrentSearchRadius();
		}

		std::vector<RE::ActorHandle> FindTargets(RE::Actor* a_caster) const
		{
			std::vector<RE::ActorHandle> targets;
			const auto world = RE::TES::GetSingleton();
			if (!world || !a_caster) {
				return targets;
			}

			world->ForEachReferenceInRange(
				a_caster,
				CurrentSearchRadius(),
				[&](RE::TESObjectREFR* a_reference) {
					auto* actor = a_reference ? a_reference->As<RE::Actor>() : nullptr;
					if (IsValidTarget(actor, a_caster)) {
						targets.emplace_back(actor);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			return targets;
		}

		void PlayFeedback(RE::Actor* a_caster, RE::Actor* a_target)
		{
			if (!a_caster || !a_target) {
				return;
			}

			const auto casterPosition = a_caster->GetPosition();
			const auto targetPosition = a_target->GetPosition();
			const auto dx = targetPosition.x - casterPosition.x;
			const auto dy = targetPosition.y - casterPosition.y;
			const auto dz = targetPosition.z - casterPosition.z;
			const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (g_forms.shockImod) {
				float strength = (g_settings.imodFadeDistance - distance) / g_settings.imodFadeDistance;
				strength = std::clamp(strength, 0.25F, 1.0F);
				RE::ImageSpaceModifierInstanceForm::Trigger(g_forms.shockImod, strength, nullptr);
			}

			if (g_forms.impactSoundNear) {
				if (const auto audio = RE::BSAudioManager::GetSingleton()) {
					RE::BSSoundHandle sound;
					if (audio->GetSoundHandle(
							sound,
							static_cast<RE::BSISoundDescriptor*>(g_forms.impactSoundNear))) {
						sound.SetPosition(targetPosition);
						sound.Play();
					}
				}
			}
		}

		void Strike(RE::Actor* a_caster, RE::Actor* a_target)
		{
			if (!ysmir::core::IsPlayerActor(a_caster) || !IsValidTarget(a_target, a_caster)) {
				return;
			}

			const auto wordIndex = static_cast<std::size_t>(_session.word - 1);
			auto* shooter = g_settings.blameCaster ? a_caster : a_target;
			if (g_forms.projectile && shooter) {
				// This base belongs exclusively to the player Storm Call controller.
				// Keep it prepared for deferred projectile/physics initialization;
				// restoring it immediately after Launch would discard the scaling.
				g_forms.projectile->data.range = g_stormProjectileRange * _session.scaling.distance;
				g_forms.projectile->data.speed = g_stormProjectileSpeed * _session.scaling.distance;
				g_forms.projectile->data.collisionRadius = g_stormProjectileCollision * _session.scaling.collision;
				auto origin = a_target->GetPosition();
				origin.x += Random(-g_settings.visualSpread[wordIndex], g_settings.visualSpread[wordIndex]);
				origin.y += Random(-g_settings.visualSpread[wordIndex], g_settings.visualSpread[wordIndex]);
				origin.z += g_settings.strikeHeight;

				auto destination = a_target->GetPosition();
				destination.z += g_settings.targetHeight;
				const auto dx = destination.x - origin.x;
				const auto dy = destination.y - origin.y;
				const auto dz = destination.z - origin.z;
				const auto horizontal = std::sqrt(dx * dx + dy * dy);
				const RE::Projectile::ProjectileRot rotation{
					std::atan2(-dz, horizontal),
					std::atan2(dx, dy)
				};

				RE::Projectile::LaunchData launchData(
					g_forms.projectile,
					shooter,
					origin,
					rotation);
				launchData.parentCell = a_target->GetParentCell();
				launchData.desiredTarget = a_target;
				launchData.alwaysHit = true;
				launchData.autoAim = true;
				launchData.useOrigin = true;
				RE::ProjectileHandle projectile;
				RE::Projectile::Launch(std::addressof(projectile), launchData);
				ysmir::projectile_visuals::TrackStorm(projectile, _session.scaling);
				if (const auto launched = projectile.get()) {
					launched->GetProjectileRuntimeData().range = g_forms.projectile->data.range;
				}
			}

			auto* blame = g_settings.blameCaster ? a_caster : a_target;
			a_target->HandleHealthDamage(blame, g_settings.damage[wordIndex] * _session.scaling.magnitude);
			PlayFeedback(a_caster, a_target);
		}

		std::size_t StrikePass(RE::Actor* a_caster)
		{
			auto targets = FindTargets(a_caster);
			for (const auto& handle : targets) {
				if (const auto target = handle.get()) {
					Strike(a_caster, target.get());
				}
			}
			return targets.size();
		}

		void RunPendingPasses(RE::Actor* a_caster)
		{
			std::uint32_t writeIndex = 0;
			for (std::uint32_t index = 0; index < _session.pendingPassCount; ++index) {
				if (_session.pendingPassDue[index] <= 0.0F) {
					StrikePass(a_caster);
				} else {
					_session.pendingPassDue[writeIndex++] = _session.pendingPassDue[index];
				}
			}
			_session.pendingPassCount = writeIndex;
		}

		void RunCycle(RE::Actor* a_caster)
		{
			const auto wordIndex = static_cast<std::size_t>(_session.word - 1);
			const auto targetCount = StrikePass(a_caster);
			_session.pendingPassCount = _session.word - 1;

			float accumulatedStagger = 0.0F;
			for (std::uint32_t index = 0; index < _session.pendingPassCount; ++index) {
				accumulatedStagger += Random(
					g_settings.passStaggerMin,
					g_settings.passStaggerMax);
				_session.pendingPassDue[index] = accumulatedStagger;
			}

			const auto nextDelay = targetCount == 0 ?
				g_settings.noTargetRetry :
				Random(
					g_settings.recastDelayMin[wordIndex],
					g_settings.recastDelayMax[wordIndex]);
			_session.cycleDue = accumulatedStagger + nextDelay;
		}

		Session _session;
		std::optional<SavedStorm> _pendingResume;
		std::mt19937 _rng;
		double _lastTick{};
		float _weatherDue{ 0.0F };
		bool _weatherOwned{ false };
	};

	void OnDataLoaded()
	{
		LoadSettings();
		if (!g_settings.enable) {
			SKSE::log::info("Plugin disabled by INI");
			return;
		}

		if (!ResolveForms()) {
			return;
		}

		ysmir::projectile_visuals::SetStormBase(g_forms.projectile);
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		auto& runtime = StormRuntime::GetSingleton();
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			ysmir::projectile_visuals::Clear();
			runtime.StopStorm(true);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			if (g_settings.enable && g_formsResolved) {
				runtime.ResumePending();
			}
			break;
		case SKSE::MessagingInterface::kNewGame:
			ysmir::projectile_visuals::Clear();
			runtime.Revert();
			break;
		default:
			break;
		}
	}

	void SerializationSave(SKSE::SerializationInterface* a_interface)
	{
		StormRuntime::GetSingleton().Save(a_interface);
	}

	void SerializationLoad(SKSE::SerializationInterface* a_interface)
	{
		StormRuntime::GetSingleton().Load(a_interface);
	}

	void SerializationRevert(SKSE::SerializationInterface*)
	{
		StormRuntime::GetSingleton().Revert();
	}

	void SetupSerialization()
	{
		if (const auto serialization = SKSE::GetSerializationInterface()) {
			serialization->SetUniqueID(SERIALIZATION_ID);
			serialization->SetSaveCallback(SerializationSave);
			serialization->SetLoadCallback(SerializationLoad);
			serialization->SetRevertCallback(SerializationRevert);
		} else {
			SKSE::log::critical("SKSE serialization interface is unavailable");
		}
	}

}

namespace ysmir::storm_call
{
	bool Initialize()
	{
		SKSE::log::info("Initializing {} adapter (SCSO v3 scheduling)", PLUGIN_NAME);

		SetupSerialization();
		return true;
	}

	void HandleMessage(SKSE::MessagingInterface::Message* a_message)
	{
		MessageHandler(a_message);
	}

	bool TryConsumeVoiceFire(RE::Actor& a_actor, const progression::ScalingSnapshot& snapshot)
	{
		return StormRuntime::GetSingleton().HandleVoiceFire(a_actor, snapshot);
	}

	void FrameUpdate()
	{
		StormRuntime::GetSingleton().FrameUpdate();
	}
}

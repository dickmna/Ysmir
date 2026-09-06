#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "core.h"
#include "VanillaNifCalibration.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <mutex>
#include <span>
#include <spdlog/sinks/basic_file_sink.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
	using TrueConeShouts::Calibration::CurvePoint;
	using TrueConeShouts::Calibration::kVoiceFlameWaveProject03Curve;
	using TrueConeShouts::Calibration::kVoicePushProjectile03Curve;

	constexpr float kPi = 3.14159265358979323846f;
	constexpr double kCalibratedTcsVisualScale = 4.0;
	constexpr bool kEnableV12CoreNodeTransforms = true;

	[[nodiscard]] constexpr double VisualCalibrationProgress(double a_visualScale)
	{
		return std::max(0.0, (a_visualScale - 1.0) / (kCalibratedTcsVisualScale - 1.0));
	}

	static_assert(VisualCalibrationProgress(1.0) == 0.0);
	static_assert(VisualCalibrationProgress(4.0) == 1.0);
	static_assert(kEnableV12CoreNodeTransforms, "The verified TCS v1.2 core-node path must remain enabled");

	struct Settings
	{
		bool enabled{ true };
		bool patchRuntimeCollision{ true };
		bool patchVisuals{ true };
		bool useVanillaNifCalibrations{ true };
		bool useV12VisualPath{ true };
		bool dumpSceneTree{};
		bool densityCompensation{ true };
		bool distanceVisualScale{ true };
		std::string densityExcludedTokens{ "Cyclone,BendWill" };
		float targetConeDegrees{ 15.0f };
		bool logMatches{};
	};

	struct ProjectilePatchInfo
	{
		RE::ObjectRefHandle instanceHandle;
		std::uintptr_t instancePointer{};
		std::uint32_t baseFormID{};
		float originalConeSpread;
		float originalConeTangent;
		float originalInitialCollisionRadius;
		float targetConeSpread;
		float targetConeTangent;
		std::uint32_t soulCountSnapshot{};
		float collisionSoulMultiplier{ 1.0f };
		float vfxSoulMultiplier{ 1.0f };
		float rootZOffset{};
		float densityGain{ 1.0f };
		float lastDistance{ -1.0f };
		std::uint64_t lastTouched{};
		std::string editorID;
		std::string modelPath;
	};

	struct GeometryWorldScaleState
	{
		float baseline{ 1.0f };
		float lastApplied{ 1.0f };
		bool hasLastApplied{};
	};

	struct BaselineTransform
	{
		RE::NiPoint3 translate;
		float scale{ 1.0f };
	};

	struct VisualProjectileState
	{
		RE::ObjectRefHandle instanceHandle;
		std::uintptr_t instancePointer{};
		std::uintptr_t root{};
		std::uint64_t lastTouched{};
		float lastDistance{ -1.0f };
		float originalRootToProjectileZ{};
		bool hasOriginalRootToProjectileZ{};
		float lastLoggedVisualScale{ -1.0f };
		std::uint8_t loggedPhaseMask{};
		std::unordered_map<std::uintptr_t, GeometryWorldScaleState> geometries;
		std::unordered_map<std::uintptr_t, BaselineTransform> v12Baselines;
	};

	struct GeometryMetrics
	{
		std::uint32_t count{};
		float minWorldScale{ std::numeric_limits<float>::max() };
		float maxWorldScale{};
		float maxWorldBoundRadius{};
	};

	struct VisualScaleResult
	{
		float scale{ 1.0f };
		float originalInitialRadius{};
		float targetInitialRadius{};
		float originalRadius{};
		float targetRadius{};
		float visualTargetRadius{};
		float measuredHalfWidth{};
		float collisionEnvelopeScale{ 1.0f };
		float distanceEnvelopeScale{ 1.0f };
		float collisionSoulMultiplier{ 1.0f };
		float visualCalibrationProgress{};
		float fixedVisualScale{ 1.0f };
		float rootZOffset{};
		float densityGain{ 1.0f };
		float calibrationWeight{};
		std::string_view mode{ "relative-envelope-fallback" };
	};

	enum class DensityTarget : std::uint8_t
	{
		kMaterialAlpha,
		kEffectBaseColorScale,
		kLightingEmissiveMult
	};

	struct DensityShaderState
	{
		DensityTarget target{ DensityTarget::kMaterialAlpha };
		std::uintptr_t material{};
		std::uint64_t lastTouched{};
		float baseline{ 1.0f };
		float lastApplied{ 1.0f };
		bool hasLastApplied{};
		bool logged{};
	};

	struct DensityMetrics
	{
		std::uint32_t opacityTargets{};
		std::uint32_t additiveTargets{};
		float minBefore{ std::numeric_limits<float>::max() };
		float maxBefore{};
		float minAfter{ std::numeric_limits<float>::max() };
		float maxAfter{};
	};

	constexpr std::size_t kSoftMaxTrackedVisualProjectiles = 256;
	constexpr std::size_t kSoftMaxTrackedProjectilePatches = 2048;
	constexpr std::size_t kMaxTrackedDensityShaders = 4096;

	Settings g_settings;
	std::uint64_t g_projectilePatchClock{};
	std::uint64_t g_visualStateClock{};
	std::uint64_t g_densityStateClock{};
	std::size_t g_nextVisualPruneSize{ kSoftMaxTrackedVisualProjectiles };
	std::size_t g_nextPatchPruneSize{ kSoftMaxTrackedProjectilePatches };
	std::unordered_map<std::uintptr_t, VisualProjectileState> g_visualStates;
	std::unordered_map<std::uintptr_t, DensityShaderState> g_densityShaderStates;
	std::unordered_map<std::uintptr_t, ProjectilePatchInfo> g_projectilePatches;
	std::unordered_set<std::uint32_t> g_loggedRejectedProjectiles;
	std::unordered_set<std::string> g_loggedSceneTreeModels;
	std::unordered_set<std::string> g_loggedDensityExclusions;
	std::unordered_set<std::string> g_loggedSoulTearVisualModes;
	std::mutex g_runtimeStateMutex;
	bool g_loggedVisualSoftLimit{};
	bool g_loggedPatchSoftLimit{};
	bool g_hooksInstalled{};
	bool g_updateHookObserved{};
	bool g_3DLoadedHookObserved{};

	struct ProjectileInstanceIdentity
	{
		RE::ObjectRefHandle handle;
		std::uintptr_t key{};
		std::uintptr_t pointer{};
	};

	[[nodiscard]] ProjectileInstanceIdentity GetProjectileInstanceIdentity(RE::ConeProjectile* a_projectile)
	{
		ProjectileInstanceIdentity identity;
		identity.pointer = reinterpret_cast<std::uintptr_t>(a_projectile);
		if (!a_projectile) {
			return identity;
		}
		identity.handle = a_projectile->GetHandle();
		const auto nativeHandle = identity.handle.native_handle();
		identity.key = nativeHandle != 0 ? static_cast<std::uintptr_t>(nativeHandle) : identity.pointer;
		return identity;
	}

	template <class T>
	[[nodiscard]] bool HasSameProjectileIdentity(const T& a_state, const ProjectileInstanceIdentity& a_identity)
	{
		return a_state.instancePointer == a_identity.pointer &&
		       a_state.instanceHandle.native_handle() == a_identity.handle.native_handle();
	}

	template <class T>
	void PruneExpiredProjectileInstances(std::unordered_map<std::uintptr_t, T>& a_states)
	{
		std::erase_if(a_states, [](const auto& a_entry) {
			const auto& state = a_entry.second;
			// A zero handle is a rare fallback keyed by the allocation address. It
			// cannot be resolved safely, so retain it instead of risking eviction of
			// a live projectile and recapturing an already-patched value as baseline.
			if (!state.instanceHandle) {
				return false;
			}
			const auto reference = state.instanceHandle.get();
			return !reference || reinterpret_cast<std::uintptr_t>(reference.get()) != state.instancePointer;
		});
	}

	[[nodiscard]] std::size_t NextPruneSize(std::size_t a_currentSize, std::size_t a_softMinimum)
	{
		// Geometric spacing amortizes handle resolution when a stress test keeps
		// thousands of projectiles alive, while lifecycle messages still clear all
		// caches immediately on load/new game.
		const auto growth = std::max<std::size_t>(64, a_currentSize / 2);
		return std::max(a_softMinimum, a_currentSize + growth);
	}

	[[nodiscard]] float DegToRad(float a_degrees)
	{
		return a_degrees * kPi / 180.0f;
	}

	[[nodiscard]] float HalfAngleTan(float a_degrees)
	{
		return std::tan(DegToRad(a_degrees * 0.5f));
	}

	[[nodiscard]] float FullAngleTan(float a_degrees)
	{
		return std::tan(DegToRad(a_degrees));
	}

	[[nodiscard]] float AbsDiff(float a_lhs, float a_rhs)
	{
		return std::abs(a_lhs - a_rhs);
	}

	[[nodiscard]] float TargetConeTangent(float a_runtimeTangent, float a_originalConeDegrees, float a_targetConeDegrees)
	{
		const auto target = std::max(a_originalConeDegrees, a_targetConeDegrees);
		if (target <= 0.0f) {
			return std::max(0.0f, a_runtimeTangent);
		}

		if (a_originalConeDegrees <= 0.0f || a_runtimeTangent <= 0.0f) {
			return FullAngleTan(target);
		}

		const auto sourceHalf = HalfAngleTan(a_originalConeDegrees);
		const auto sourceFull = FullAngleTan(a_originalConeDegrees);
		const auto looksLikeFullAngle = AbsDiff(a_runtimeTangent, sourceFull) < AbsDiff(a_runtimeTangent, sourceHalf);

		const auto sourceConvention = looksLikeFullAngle ? sourceFull : sourceHalf;
		const auto targetConvention = looksLikeFullAngle ? FullAngleTan(target) : HalfAngleTan(target);
		if (sourceConvention <= 0.0f) {
			return targetConvention;
		}

		return a_runtimeTangent * (targetConvention / sourceConvention);
	}

	[[nodiscard]] std::string Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}

		const auto last = a_text.find_last_not_of(" \t\r\n");
		return std::string(a_text.substr(first, last - first + 1));
	}



	void LoadSettings()
	{
		g_settings.enabled = ysmir::config::Bool("TrueCone", "Enabled", g_settings.enabled);
		g_settings.patchRuntimeCollision = ysmir::config::Bool("TrueCone", "PatchRuntimeCollision", g_settings.patchRuntimeCollision);
		g_settings.patchVisuals = ysmir::config::Bool("TrueCone", "PatchVisuals", g_settings.patchVisuals);
		g_settings.useVanillaNifCalibrations = ysmir::config::Bool("TrueCone", "UseVanillaNifCalibrations", g_settings.useVanillaNifCalibrations);
		g_settings.distanceVisualScale = ysmir::config::Bool("TrueCone", "DistanceVisualScale", g_settings.distanceVisualScale);
		g_settings.densityCompensation = ysmir::config::Bool("TrueCone", "DensityCompensation", g_settings.densityCompensation);
		g_settings.dumpSceneTree = ysmir::config::Bool("TrueCone", "DumpSceneTree", g_settings.dumpSceneTree);
		g_settings.logMatches = ysmir::config::Bool("TrueCone", "LogMatches", g_settings.logMatches);
		g_settings.densityExcludedTokens = ysmir::config::String("TrueCone", "DensityExcludedTokens", "Cyclone,BendWill");
		g_settings.targetConeDegrees = 15.f;
	}

	[[nodiscard]] bool IsShoutConeProjectile(RE::ConeProjectile* a_projectile)
	{
		if (!a_projectile || !ysmir::core::IsPlayerOwnedProjectile(a_projectile)) {
			return false;
		}

		const auto& runtimeData = a_projectile->GetProjectileRuntimeData();
		const auto* spell = runtimeData.spell;
		const auto* voiceSpell = spell ? spell->As<RE::SpellItem>() : nullptr;
		return spell &&
		       spell->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower &&
		       ysmir::core::IsPlayerVoiceSpellRuntimeAuthorized(voiceSpell);
	}

	[[nodiscard]] bool StartsWith(std::string_view a_value, std::string_view a_prefix)
	{
		return a_value.size() >= a_prefix.size() && a_value.substr(0, a_prefix.size()) == a_prefix;
	}

	[[nodiscard]] bool ContainsAsciiInsensitive(std::string_view a_value, std::string_view a_needle)
	{
		if (a_needle.empty() || a_needle.size() > a_value.size()) {
			return false;
		}

		const auto equalsInsensitive = [](char a_lhs, char a_rhs) {
			return std::tolower(static_cast<unsigned char>(a_lhs)) ==
			       std::tolower(static_cast<unsigned char>(a_rhs));
		};
		return std::search(a_value.begin(), a_value.end(), a_needle.begin(), a_needle.end(), equalsInsensitive) != a_value.end();
	}

	[[nodiscard]] bool IsSoulTearProjectile(
		RE::ConeProjectile* a_projectile,
		const ProjectilePatchInfo& a_info)
	{
		std::string_view spellEditorID;
		if (a_projectile) {
			const auto& runtimeData = a_projectile->GetProjectileRuntimeData();
			if (const auto* spell = runtimeData.spell) {
				if (const auto* editorID = spell->GetFormEditorID()) {
					spellEditorID = editorID;
				}
			}
		}

		const std::array<std::string_view, 3> fields{
			a_info.editorID,
			a_info.modelPath,
			spellEditorID
		};
		return std::ranges::any_of(fields, [](const auto field) {
			return ContainsAsciiInsensitive(field, "SoulTear") ||
			       ContainsAsciiInsensitive(field, "SoulRend");
		});
	}

	struct DensityExclusionResult
	{
		bool excluded{};
		std::string token;
		std::string spellEditorID;
	};

	[[nodiscard]] DensityExclusionResult GetDensityExclusion(
		RE::ConeProjectile* a_projectile,
		const ProjectilePatchInfo& a_info)
	{
		DensityExclusionResult result;
		if (!a_projectile || g_settings.densityExcludedTokens.empty()) {
			return result;
		}

		const auto& runtimeData = a_projectile->GetProjectileRuntimeData();
		if (const auto* spell = runtimeData.spell) {
			if (const auto* editorID = spell->GetFormEditorID()) {
				result.spellEditorID = editorID;
			}
		}

		const std::array<std::string_view, 3> fields{
			a_info.editorID,
			a_info.modelPath,
			result.spellEditorID
		};
		const std::string_view tokens{ g_settings.densityExcludedTokens };
		std::size_t start{};
		while (start <= tokens.size()) {
			const auto end = tokens.find_first_of(",;|", start);
			const auto token = Trim(tokens.substr(start, end == std::string_view::npos ? tokens.size() - start : end - start));
			if (!token.empty()) {
				for (const auto field : fields) {
					if (ContainsAsciiInsensitive(field, token)) {
						result.excluded = true;
						result.token = token;
						return result;
					}
				}
			}
			if (end == std::string_view::npos) {
				break;
			}
			start = end + 1;
		}

		return result;
	}

	[[nodiscard]] const char* GetRTTIName(const RE::NiObject* a_object)
	{
		const auto* rtti = a_object ? a_object->GetRTTI() : nullptr;
		const auto* name = rtti ? rtti->GetName() : nullptr;
		return name && name[0] != '\0' ? name : "<unknown-rtti>";
	}

	[[nodiscard]] const char* GetObjectName(const RE::NiObjectNET* a_object)
	{
		const auto* name = a_object ? a_object->name.c_str() : nullptr;
		return name && name[0] != '\0' ? name : "<unnamed>";
	}

	[[nodiscard]] bool IsV12CoreVisualNode(const RE::NiAVObject* a_object)
	{
		if (!a_object) {
			return false;
		}

		const std::string_view name{ GetObjectName(a_object) };
		return StartsWith(name, "GeoSphere") ||
		       StartsWith(name, "HandObject") ||
		       StartsWith(name, "Cylinder") ||
		       StartsWith(name, "Gravity") ||
		       StartsWith(name, "StripParticleMover") ||
		       StartsWith(name, "PArray") ||
		       StartsWith(name, "PBomb");
	}

	[[nodiscard]] float ComputeV12CoreVisualScale(float a_visualScale)
	{
		constexpr float factor = 0.35f;
		constexpr float maxScale = 2.5f;
		const auto unclamped = 1.0f + (std::max(1.0f, a_visualScale) - 1.0f) * factor;
		return std::clamp(unclamped, 1.0f, maxScale);
	}

	std::uint32_t CaptureAndScaleV12Default(
		RE::NiAVObject* a_object,
		std::unordered_map<std::uintptr_t, BaselineTransform>& a_baselines,
		float a_scale,
		bool a_isRoot,
		bool a_rootScaled,
		bool a_coreScaledAncestor,
		bool a_scaleCoreNodes)
	{
		if (!a_object) {
			return 0;
		}

		// This intentionally reproduces the default v1.2 scene-graph path. The
		// root receives the full fixed multiplier; the first matching core node in
		// each branch receives v1.2's limited extra scale and X/Z translation.
		const auto scaleThisRoot = a_isRoot;
		const auto scaleThisCoreChild = a_scaleCoreNodes &&
		                                !a_isRoot &&
		                                a_rootScaled &&
		                                !a_coreScaledAncestor &&
		                                IsV12CoreVisualNode(a_object);
		const auto objectScale = scaleThisCoreChild ? ComputeV12CoreVisualScale(a_scale) : a_scale;

		std::uint32_t count{};
		if (scaleThisRoot || scaleThisCoreChild) {
			// Only capture nodes Ysmir actually modifies. A later runtime-distance
			// reset can then restore our transforms without overwriting controller-
			// animated local transforms on unrelated particle nodes.
			const auto key = reinterpret_cast<std::uintptr_t>(a_object);
			const auto [it, inserted] = a_baselines.try_emplace(
				key,
				BaselineTransform{
					a_object->local.translate,
					a_object->local.scale,
				});
			(void)inserted;
			auto& local = a_object->local;
			if (!a_isRoot) {
				local.translate.x = it->second.translate.x * objectScale;
				local.translate.z = it->second.translate.z * objectScale;
				local.translate.y = it->second.translate.y;
			}
			local.scale = it->second.scale * objectScale;
			++count;
		}

		if (auto* node = a_object->AsNode()) {
			const auto rootScaled = a_rootScaled || scaleThisRoot;
			const auto coreScaledAncestor = a_coreScaledAncestor || scaleThisCoreChild;
			for (auto& childPtr : node->GetChildren()) {
				count += CaptureAndScaleV12Default(
					childPtr.get(),
					a_baselines,
					a_scale,
					false,
					rootScaled,
					coreScaledAncestor,
					a_scaleCoreNodes);
			}
		}

		return count;
	}

	void RestoreCapturedV12Transforms(
		RE::NiAVObject* a_object,
		const std::unordered_map<std::uintptr_t, BaselineTransform>& a_baselines)
	{
		if (!a_object) {
			return;
		}

		if (const auto it = a_baselines.find(reinterpret_cast<std::uintptr_t>(a_object)); it != a_baselines.end()) {
			a_object->local.translate = it->second.translate;
			a_object->local.scale = it->second.scale;
		}
		if (auto* node = a_object->AsNode()) {
			for (auto& childPtr : node->GetChildren()) {
				RestoreCapturedV12Transforms(childPtr.get(), a_baselines);
			}
		}
	}

	struct GeometryBlendInfo
	{
		bool known{};
		bool additive{};
		std::int32_t source{ -1 };
		std::int32_t destination{ -1 };
	};

	[[nodiscard]] RE::BSShaderProperty* GetShaderProperty(RE::BSGeometry* a_geometry)
	{
		if (!a_geometry) {
			return nullptr;
		}

		return a_geometry->GetGeometryRuntimeData().shaderProperty.get();
	}

	[[nodiscard]] GeometryBlendInfo GetGeometryBlendInfo(
		RE::BSGeometry* a_geometry,
		const RE::BSShaderProperty* a_shader)
	{
		GeometryBlendInfo result;
		if (a_geometry) {
			auto& runtimeData = a_geometry->GetGeometryRuntimeData();
			if (auto* alpha = runtimeData.alphaProperty.get();
			    alpha && alpha->GetAlphaBlending()) {
				const auto source = alpha->GetSrcBlendMode();
				const auto destination = alpha->GetDestBlendMode();
				result.known = true;
				result.source = static_cast<std::int32_t>(source);
				result.destination = static_cast<std::int32_t>(destination);
				result.additive = destination == RE::NiAlphaProperty::AlphaFunction::kOne &&
				                  source != RE::NiAlphaProperty::AlphaFunction::kZero;
				return result;
			}
		}

		if (a_shader && a_shader->effectData) {
			const auto source = a_shader->effectData->srcBlend;
			const auto destination = a_shader->effectData->destBlend;
			result.known = true;
			result.source = static_cast<std::int32_t>(source);
			result.destination = static_cast<std::int32_t>(destination);
			result.additive = destination == RE::NiAlphaProperty::AlphaFunction::kOne &&
			                  source != RE::NiAlphaProperty::AlphaFunction::kZero;
		}
		return result;
	}

	[[nodiscard]] const char* DensityTargetName(DensityTarget a_target)
	{
		switch (a_target) {
		case DensityTarget::kEffectBaseColorScale:
			return "effect-base-color-scale";
		case DensityTarget::kLightingEmissiveMult:
			return "lighting-emissive-mult";
		default:
			return "material-alpha";
		}
	}

	[[nodiscard]] DensityTarget SelectDensityTarget(
		RE::BSShaderProperty* a_shader,
		const GeometryBlendInfo& a_blend)
	{
		if (a_blend.additive && a_shader) {
			if (a_shader->material && a_shader->GetMaterialType() == RE::BSShaderMaterial::Type::kEffect) {
				return DensityTarget::kEffectBaseColorScale;
			}
			if (netimmerse_cast<RE::BSLightingShaderProperty*>(a_shader)) {
				return DensityTarget::kLightingEmissiveMult;
			}
		}
		return DensityTarget::kMaterialAlpha;
	}

	[[nodiscard]] bool ReadDensityValue(
		RE::BSShaderProperty* a_shader,
		DensityTarget a_target,
		float& a_value,
		std::uintptr_t& a_material)
	{
		if (!a_shader) {
			return false;
		}

		a_material = reinterpret_cast<std::uintptr_t>(a_shader->material);
		switch (a_target) {
		case DensityTarget::kEffectBaseColorScale:
			if (a_shader->material && a_shader->GetMaterialType() == RE::BSShaderMaterial::Type::kEffect) {
				a_value = static_cast<RE::BSEffectShaderMaterial*>(a_shader->material)->baseColorScale;
				return std::isfinite(a_value);
			}
			return false;
		case DensityTarget::kLightingEmissiveMult:
			if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(a_shader)) {
				a_value = lighting->emissiveMult;
				return std::isfinite(a_value);
			}
			return false;
		default:
			a_value = a_shader->QMaterialAlpha();
			return std::isfinite(a_value);
		}
	}

	void WriteDensityValue(RE::BSShaderProperty* a_shader, DensityTarget a_target, float a_value)
	{
		if (!a_shader || !std::isfinite(a_value)) {
			return;
		}

		switch (a_target) {
		case DensityTarget::kEffectBaseColorScale:
			if (a_shader->material && a_shader->GetMaterialType() == RE::BSShaderMaterial::Type::kEffect) {
				static_cast<RE::BSEffectShaderMaterial*>(a_shader->material)->baseColorScale = a_value;
			}
			break;
		case DensityTarget::kLightingEmissiveMult:
			if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(a_shader)) {
				lighting->emissiveMult = a_value;
			}
			break;
		default:
			a_shader->SetMaterialAlpha(a_value);
			break;
		}
	}

	[[nodiscard]] float ComputeDensityValue(float a_baseline, DensityTarget a_target, float a_gain)
	{
		if (a_target == DensityTarget::kMaterialAlpha) {
			const auto alpha = std::clamp(a_baseline, 0.0f, 1.0f);
			return 1.0f - std::pow(1.0f - alpha, a_gain);
		}

		const auto intensity = std::max(0.0f, a_baseline);
		const auto safetyLimit = std::max(intensity, 64.0f);
		return std::min(intensity * a_gain, safetyLimit);
	}

	void ApplyDensityToShader(
		RE::BSShaderProperty* a_shader,
		const GeometryBlendInfo& a_blend,
		float a_gain,
		DensityMetrics& a_metrics)
	{
		if (!a_shader) {
			return;
		}

		const auto target = SelectDensityTarget(a_shader, a_blend);
		float current{};
		std::uintptr_t material{};
		if (!ReadDensityValue(a_shader, target, current, material)) {
			return;
		}

		const auto shaderKey = reinterpret_cast<std::uintptr_t>(a_shader);
		const auto ownerKey = target == DensityTarget::kLightingEmissiveMult || material == 0 ? shaderKey : material;
		const auto stateKey = ownerKey | static_cast<std::uintptr_t>(target);
		if (!g_densityShaderStates.contains(stateKey) && g_densityShaderStates.size() >= kMaxTrackedDensityShaders) {
			SKSE::log::warn("DENSITY state limit reached; skipping shader={:X}, owner={:X}", shaderKey, ownerKey);
			return;
		}

		// Material instances can be shared by more than one shader property. Key
		// material-backed adjustments by material address so the gain is applied
		// once rather than compounded by each referring geometry.
		auto& state = g_densityShaderStates[stateKey];
		state.lastTouched = ++g_densityStateClock;
		if (!state.hasLastApplied || state.target != target || state.material != material) {
			state.target = target;
			state.material = material;
			state.baseline = current;
			state.hasLastApplied = false;
			state.logged = false;
		} else if (std::abs(current - state.lastApplied) > 1.0e-4f) {
			// A native controller produced a fresh value this frame. Preserve that
			// authored animation as the new input instead of accumulating our gain.
			state.baseline = current;
		}

		const auto applied = ComputeDensityValue(state.baseline, target, a_gain);
		WriteDensityValue(a_shader, target, applied);
		state.lastApplied = applied;
		state.hasLastApplied = true;

		if (target == DensityTarget::kMaterialAlpha) {
			++a_metrics.opacityTargets;
		} else {
			++a_metrics.additiveTargets;
		}
		a_metrics.minBefore = std::min(a_metrics.minBefore, state.baseline);
		a_metrics.maxBefore = std::max(a_metrics.maxBefore, state.baseline);
		a_metrics.minAfter = std::min(a_metrics.minAfter, applied);
		a_metrics.maxAfter = std::max(a_metrics.maxAfter, applied);

		if (!state.logged && g_settings.logMatches) {
			SKSE::log::info(
				"DENSITY shader: address={:X}, stateOwner={:X}, type={}, target={}, blendKnown={}, srcBlend={}, destBlend={}, gain={}, baseline={}, applied={}, material={:X}",
				shaderKey,
				ownerKey,
				GetRTTIName(a_shader),
				DensityTargetName(target),
				a_blend.known,
				a_blend.source,
				a_blend.destination,
				a_gain,
				state.baseline,
				applied,
				material);
			state.logged = true;
		}
	}

	void ApplyVfxDensityCompensation(
		RE::NiAVObject* a_object,
		float a_gain,
		DensityMetrics& a_metrics,
		std::unordered_set<std::uintptr_t>& a_visitedShaders)
	{
		if (!a_object) {
			return;
		}

		if (auto* geometry = a_object->AsGeometry()) {
			if (auto* shader = GetShaderProperty(geometry)) {
				const auto key = reinterpret_cast<std::uintptr_t>(shader);
				if (a_visitedShaders.insert(key).second) {
					ApplyDensityToShader(shader, GetGeometryBlendInfo(geometry, shader), a_gain, a_metrics);
				}
			}
		}

		if (auto* node = a_object->AsNode()) {
			for (auto& childPtr : node->GetChildren()) {
				ApplyVfxDensityCompensation(childPtr.get(), a_gain, a_metrics, a_visitedShaders);
			}
		}
	}

	void LogControllerChain(const RE::NiObjectNET* a_owner, std::string_view a_path, std::string_view a_role)
	{
		constexpr std::uint32_t maxControllers = 64;
		std::unordered_set<std::uintptr_t> visited;
		std::uint32_t index{};
		for (auto* controller = a_owner ? a_owner->GetControllers() : nullptr;
		     controller && index < maxControllers;
		     controller = controller->GetNext(), ++index) {
			const auto key = reinterpret_cast<std::uintptr_t>(controller);
			if (!visited.insert(key).second) {
				SKSE::log::warn(
					"SCENE controller-cycle: path={}, role={}, index={}, address={:X}",
					a_path,
					a_role,
					index,
					key);
				return;
			}

			SKSE::log::info(
				"SCENE controller: path={}, role={}, index={}, type={}, flags={}, frequency={}, phase={}, keyTime=[{},{}], target={:X}, next={:X}",
				a_path,
				a_role,
				index,
				GetRTTIName(controller),
				controller->flags.underlying(),
				controller->frequency,
				controller->phase,
				controller->loKeyTime,
				controller->hiKeyTime,
				reinterpret_cast<std::uintptr_t>(controller->target),
				reinterpret_cast<std::uintptr_t>(controller->GetNext()));
		}

		if (index == maxControllers) {
			SKSE::log::warn("SCENE controller-limit: path={}, role={}, limit={}", a_path, a_role, maxControllers);
		}
	}

	void LogSceneObject(
		RE::NiAVObject* a_object,
		const std::string& a_parentPath,
		std::uint32_t a_childIndex,
		std::uint32_t a_depth,
		std::uint32_t& a_objectCount,
		std::unordered_set<std::uintptr_t>& a_visited)
	{
		constexpr std::uint32_t maxObjects = 512;
		if (!a_object || a_objectCount >= maxObjects) {
			return;
		}

		const auto key = reinterpret_cast<std::uintptr_t>(a_object);
		if (!a_visited.insert(key).second) {
			SKSE::log::warn("SCENE object-cycle: parentPath={}, childIndex={}, address={:X}", a_parentPath, a_childIndex, key);
			return;
		}

		++a_objectCount;
		const auto path = a_parentPath.empty() ?
		                    std::string(GetObjectName(a_object)) + "[0]" :
		                    a_parentPath + "/" + GetObjectName(a_object) + "[" + std::to_string(a_childIndex) + "]";
		auto* geometry = a_object->AsGeometry();
		SKSE::log::info(
			"SCENE object: depth={}, path={}, type={}, address={:X}, parent={:X}, localT=[{},{},{}], localScale={}, worldT=[{},{},{}], worldScale={}, previousWorldScale={}, boundCenter=[{},{},{}], boundRadius={}, isNode={}, isGeometry={}",
			a_depth,
			path,
			GetRTTIName(a_object),
			key,
			reinterpret_cast<std::uintptr_t>(a_object->parent),
			a_object->local.translate.x,
			a_object->local.translate.y,
			a_object->local.translate.z,
			a_object->local.scale,
			a_object->world.translate.x,
			a_object->world.translate.y,
			a_object->world.translate.z,
			a_object->world.scale,
			a_object->previousWorld.scale,
			a_object->worldBound.center.x,
			a_object->worldBound.center.y,
			a_object->worldBound.center.z,
			a_object->worldBound.radius,
			a_object->AsNode() != nullptr,
			geometry != nullptr);

		LogControllerChain(a_object, path, "object");

		if (geometry) {
			const auto& runtimeData = geometry->GetGeometryRuntimeData();
			SKSE::log::info(
				"SCENE geometry: path={}, geometryType={}, rendererData={:X}, skinInstance={:X}",
				path,
				geometry->GetType().underlying(),
				reinterpret_cast<std::uintptr_t>(runtimeData.rendererData),
				reinterpret_cast<std::uintptr_t>(runtimeData.skinInstance.get()));

			const std::array<RE::NiProperty*, 2> properties{
				runtimeData.alphaProperty.get(), runtimeData.shaderProperty.get()
			};
			for (std::size_t propertyIndex = 0; propertyIndex < properties.size(); ++propertyIndex) {
				auto* property = properties[propertyIndex];
				if (!property) {
					continue;
				}

				auto* shader = netimmerse_cast<RE::BSShaderProperty*>(property);
				auto* alphaProperty = netimmerse_cast<RE::NiAlphaProperty*>(property);
				const auto blend = GetGeometryBlendInfo(geometry, shader);
				float effectBaseColorScale = -1.0f;
				float lightingEmissiveMult = -1.0f;
				if (shader && shader->material && shader->GetMaterialType() == RE::BSShaderMaterial::Type::kEffect) {
					effectBaseColorScale = static_cast<RE::BSEffectShaderMaterial*>(shader->material)->baseColorScale;
				}
				if (auto* lighting = shader ? netimmerse_cast<RE::BSLightingShaderProperty*>(shader) : nullptr) {
					lightingEmissiveMult = lighting->emissiveMult;
				}
				SKSE::log::info(
					"SCENE property: path={}, slot={}, type={}, address={:X}, shaderMaterialType={}, shaderAlpha={}, materialAlpha={}, effectBaseColorScale={}, lightingEmissiveMult={}, alphaBlending={}, srcBlend={}, destBlend={}, additive={}",
					path,
					propertyIndex,
					GetRTTIName(property),
					reinterpret_cast<std::uintptr_t>(property),
					shader ? static_cast<std::int32_t>(shader->GetMaterialType()) : -1,
					shader ? shader->alpha : -1.0f,
					shader ? shader->QMaterialAlpha() : -1.0f,
					effectBaseColorScale,
					lightingEmissiveMult,
					alphaProperty ? alphaProperty->GetAlphaBlending() : false,
					blend.source,
					blend.destination,
					blend.additive);
				LogControllerChain(property, path, propertyIndex == 0 ? "property-0" : "property-1");
			}
		}

		if (auto* node = a_object->AsNode()) {
			std::uint32_t childIndex{};
			for (auto& childPtr : node->GetChildren()) {
				LogSceneObject(childPtr.get(), path, childIndex, a_depth + 1, a_objectCount, a_visited);
				++childIndex;
			}
		}
	}

	void DumpSceneTreeOnce(const ProjectilePatchInfo& a_info, RE::NiAVObject* a_root)
	{
		if (!g_settings.dumpSceneTree || !a_root) {
			return;
		}

		const auto key = a_info.modelPath.empty() ? a_info.editorID : a_info.modelPath;
		if (!g_loggedSceneTreeModels.insert(key).second) {
			return;
		}

		SKSE::log::info(
			"SCENE begin: editorID={}, model={}, root={:X}",
			a_info.editorID,
			a_info.modelPath,
			reinterpret_cast<std::uintptr_t>(a_root));
		std::uint32_t objectCount{};
		std::unordered_set<std::uintptr_t> visited;
		LogSceneObject(a_root, {}, 0, 0, objectCount, visited);
		SKSE::log::info("SCENE end: editorID={}, model={}, objectCount={}", a_info.editorID, a_info.modelPath, objectCount);
	}

	[[nodiscard]] std::uint8_t PhaseMask(std::string_view a_phase)
	{
		if (a_phase == "3d-loaded") {
			return 1u << 0;
		}
		if (a_phase == "pre-update") {
			return 1u << 1;
		}
		return 1u << 2;
	}

	VisualProjectileState& GetVisualState(RE::ConeProjectile* a_projectile, RE::NiAVObject* a_root)
	{
		const auto identity = GetProjectileInstanceIdentity(a_projectile);
		const auto projectileKey = identity.key;
		const auto rootKey = reinterpret_cast<std::uintptr_t>(a_root);

		auto it = g_visualStates.find(projectileKey);
		if (it != g_visualStates.end() && !HasSameProjectileIdentity(it->second, identity)) {
			g_visualStates.erase(it);
			it = g_visualStates.end();
		}
		if (it == g_visualStates.end()) {
			if (g_visualStates.size() >= g_nextVisualPruneSize) {
				PruneExpiredProjectileInstances(g_visualStates);
				g_nextVisualPruneSize = NextPruneSize(g_visualStates.size(), kSoftMaxTrackedVisualProjectiles);
				if (g_visualStates.size() >= kSoftMaxTrackedVisualProjectiles && !g_loggedVisualSoftLimit) {
					g_loggedVisualSoftLimit = true;
					SKSE::log::warn(
						"True Cone visual-state soft limit {} reached entirely by live/unresolvable projectiles; growing the cache instead of evicting a live baseline",
						kSoftMaxTrackedVisualProjectiles);
				}
			}
			it = g_visualStates.try_emplace(projectileKey).first;
			it->second.instanceHandle = identity.handle;
			it->second.instancePointer = identity.pointer;
		}

		auto& state = it->second;
		state.lastTouched = ++g_visualStateClock;
		if (state.root != rootKey) {
			state.root = rootKey;
			state.lastDistance = -1.0f;
			state.originalRootToProjectileZ = 0.0f;
			state.hasOriginalRootToProjectileZ = false;
			state.lastLoggedVisualScale = -1.0f;
			state.loggedPhaseMask = 0;
			state.geometries.clear();
			state.v12Baselines.clear();
		}

		return state;
	}

	[[nodiscard]] bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::abs(a_lhs - a_rhs) <= 1.0e-4f;
	}

	std::uint32_t ApplyGeometryWorldScale(
		RE::NiAVObject* a_object,
		VisualProjectileState& a_state,
		float a_scale)
	{
		if (!a_object) {
			return 0;
		}

		std::uint32_t count{};
		if (auto* geometry = a_object->AsGeometry()) {
			const auto key = reinterpret_cast<std::uintptr_t>(geometry);
			auto [it, inserted] = a_state.geometries.try_emplace(key);
			auto& geometryState = it->second;

			// Native UpdateImpl owns local transforms and controller simulation. This
			// path touches only the final geometry world scale after that update. If
			// native code produced a fresh world scale, accept it as this frame's base;
			// otherwise reuse the saved base to prevent multiplication accumulation.
			if (inserted || !geometryState.hasLastApplied || !NearlyEqual(geometryState.lastApplied, geometry->world.scale)) {
				geometryState.baseline = geometry->world.scale;
			}

			geometry->world.scale = geometryState.baseline * a_scale;
			geometryState.lastApplied = geometry->world.scale;
			geometryState.hasLastApplied = true;
			geometry->UpdateWorldBound();
			++count;
		}

		if (auto* node = a_object->AsNode()) {
			for (auto& childPtr : node->GetChildren()) {
				count += ApplyGeometryWorldScale(childPtr.get(), a_state, a_scale);
			}
		}

		return count;
	}

	void AccumulateGeometryMetrics(RE::NiAVObject* a_object, GeometryMetrics& a_metrics)
	{
		if (!a_object) {
			return;
		}

		if (a_object->AsGeometry()) {
			++a_metrics.count;
			a_metrics.minWorldScale = std::min(a_metrics.minWorldScale, a_object->world.scale);
			a_metrics.maxWorldScale = std::max(a_metrics.maxWorldScale, a_object->world.scale);
			a_metrics.maxWorldBoundRadius = std::max(a_metrics.maxWorldBoundRadius, a_object->worldBound.radius);
		}

		if (auto* node = a_object->AsNode()) {
			for (auto& childPtr : node->GetChildren()) {
				AccumulateGeometryMetrics(childPtr.get(), a_metrics);
			}
		}
	}

	[[nodiscard]] GeometryMetrics GetGeometryMetrics(RE::NiAVObject* a_root)
	{
		GeometryMetrics metrics;
		AccumulateGeometryMetrics(a_root, metrics);
		if (metrics.count == 0) {
			metrics.minWorldScale = 0.0f;
		}
		return metrics;
	}

	[[nodiscard]] bool EndsWithInsensitive(std::string_view a_value, std::string_view a_suffix)
	{
		if (a_suffix.size() > a_value.size()) {
			return false;
		}

		const auto tail = a_value.substr(a_value.size() - a_suffix.size());
		return std::ranges::equal(tail, a_suffix, [](unsigned char a_lhs, unsigned char a_rhs) {
			return std::tolower(a_lhs) == std::tolower(a_rhs);
		});
	}

	[[nodiscard]] float SampleCurve(std::span<const CurvePoint> a_curve, float a_distance)
	{
		if (a_curve.empty()) {
			return 0.0f;
		}
		if (a_distance <= a_curve.front().distance) {
			return a_curve.front().halfWidth;
		}
		if (a_distance >= a_curve.back().distance) {
			return a_curve.back().halfWidth;
		}

		const auto right = std::ranges::upper_bound(a_curve, a_distance, {}, &CurvePoint::distance);
		const auto& lhs = *(right - 1);
		const auto& rhs = *right;
		const auto span = rhs.distance - lhs.distance;
		const auto alpha = span > 0.0f ? (a_distance - lhs.distance) / span : 0.0f;
		return std::lerp(lhs.halfWidth, rhs.halfWidth, alpha);
	}

	[[nodiscard]] bool IsVoicePushProjectile03Model(const ProjectilePatchInfo& a_info)
	{
		return EndsWithInsensitive(a_info.modelPath, "magic\\voicepushprojectile03.nif") ||
		       EndsWithInsensitive(a_info.modelPath, "magic/voicepushprojectile03.nif");
	}

	[[nodiscard]] bool IsVoiceFlameWaveProject03Model(const ProjectilePatchInfo& a_info)
	{
		return EndsWithInsensitive(a_info.modelPath, "magic\\voiceflamewaveproject03.nif") ||
		       EndsWithInsensitive(a_info.modelPath, "magic/voiceflamewaveproject03.nif");
	}

	[[nodiscard]] float GetCalibrationWeight(const ProjectilePatchInfo& a_info, float a_distance)
	{
		if (!g_settings.useVanillaNifCalibrations) {
			return 0.0f;
		}

		if (IsVoicePushProjectile03Model(a_info)) {
			return a_distance <= kVoicePushProjectile03Curve.back().distance ? 1.0f : 0.0f;
		}
		if (IsVoiceFlameWaveProject03Model(a_info)) {
			constexpr float fadeStart = 2000.0f;
			const auto fadeEnd = kVoiceFlameWaveProject03Curve.back().distance;
			if (a_distance <= fadeStart) {
				return 1.0f;
			}
			if (a_distance < fadeEnd) {
				return (fadeEnd - a_distance) / (fadeEnd - fadeStart);
			}
		}

		return 0.0f;
	}

	[[nodiscard]] float GetMeasuredVanillaHalfWidth(const ProjectilePatchInfo& a_info, float a_distance)
	{
		if (GetCalibrationWeight(a_info, a_distance) <= 0.0f) {
			return 0.0f;
		}

		if (IsVoicePushProjectile03Model(a_info)) {
			return SampleCurve(kVoicePushProjectile03Curve, a_distance);
		}
		if (IsVoiceFlameWaveProject03Model(a_info)) {
			return SampleCurve(kVoiceFlameWaveProject03Curve, a_distance);
		}

		return 0.0f;
	}

	[[nodiscard]] RE::BGSProjectile* GetProjectileBase(RE::ConeProjectile* a_projectile)
	{
		if (!a_projectile) {
			return nullptr;
		}

		auto* baseObject = a_projectile->GetObjectReference();
		return baseObject ? baseObject->As<RE::BGSProjectile>() : nullptr;
	}

	[[nodiscard]] ProjectilePatchInfo& GetOrCreatePatchInfo(RE::ConeProjectile* a_projectile, RE::BGSProjectile* a_base)
	{
		const auto identity = GetProjectileInstanceIdentity(a_projectile);
		const auto key = identity.key;
		const auto currentDistance = std::max(0.0f, a_projectile->GetProjectileRuntimeData().distanceMoved);
		auto it = g_projectilePatches.find(key);
		// The native reference handle includes the engine's reuse/age bits. Validate
		// both it and the allocation address so same-base projectiles created near
		// zero distance can never inherit another cast's soul snapshot.
		if (it != g_projectilePatches.end() &&
		    (!HasSameProjectileIdentity(it->second, identity) ||
		     it->second.baseFormID != a_base->formID)) {
			g_projectilePatches.erase(it);
			it = g_projectilePatches.end();
		}
		const auto distanceReset = it != g_projectilePatches.end() &&
		                           currentDistance + 0.01f < it->second.lastDistance;

		bool inserted{ false };
		if (it == g_projectilePatches.end()) {
			if (g_projectilePatches.size() >= g_nextPatchPruneSize) {
				PruneExpiredProjectileInstances(g_projectilePatches);
				g_nextPatchPruneSize = NextPruneSize(g_projectilePatches.size(), kSoftMaxTrackedProjectilePatches);
				if (g_projectilePatches.size() >= kSoftMaxTrackedProjectilePatches && !g_loggedPatchSoftLimit) {
					g_loggedPatchSoftLimit = true;
					SKSE::log::warn(
						"True Cone collision-state soft limit {} reached entirely by live/unresolvable projectiles; growing the cache instead of evicting a live baseline",
						kSoftMaxTrackedProjectilePatches);
				}
			}
			it = g_projectilePatches.try_emplace(key).first;
			inserted = true;
		}

		auto& info = it->second;
		if (inserted) {
			info.instanceHandle = identity.handle;
			info.instancePointer = identity.pointer;
			const auto& coneData = a_projectile->GetConeRuntimeData();
			const auto originalConeSpread = std::max(0.0f, a_base->data.coneSpread);
			const auto originalConeTangent = std::max(0.0f, coneData.coneAngleTangent > 0.0f ? coneData.coneAngleTangent : HalfAngleTan(originalConeSpread));
			// Capture the active projectile's original runtime value before Ysmir
			// touches it. If the runtime has not populated a positive radius yet, use
			// the authored PROJ radius; unlike TCSF 1.3.1, do not impose a 32-unit
			// (or even a 1-unit) floor.
			const auto originalInitialCollisionRadius = std::max(
				0.0f,
				coneData.initialCollisionSphereRadius > 0.0f ?
					coneData.initialCollisionSphereRadius : a_base->data.collisionRadius);

			const auto targetConeSpread = std::max(originalConeSpread, g_settings.targetConeDegrees);
			const auto targetConeTangent = std::max(originalConeTangent, TargetConeTangent(originalConeTangent, originalConeSpread, targetConeSpread));
			const auto scaling = ysmir::core::ScalingForProjectile(a_projectile);
			const auto collisionSoulMultiplier = scaling.collision;
			const auto vfxSoulMultiplier = scaling.vfx;
			const auto soulCountSnapshot = scaling.souls;

			const auto* editorID = a_base->GetFormEditorID();
			const auto* modelPath = a_base->GetModel();
			info.baseFormID = a_base->formID;
			info.originalConeSpread = originalConeSpread;
			info.originalConeTangent = originalConeTangent;
			info.originalInitialCollisionRadius = originalInitialCollisionRadius;
			info.targetConeSpread = targetConeSpread;
			info.targetConeTangent = targetConeTangent;
			info.soulCountSnapshot = soulCountSnapshot;
			info.collisionSoulMultiplier = collisionSoulMultiplier;
			info.vfxSoulMultiplier = vfxSoulMultiplier;
			info.rootZOffset = scaling.height;
			info.densityGain = scaling.density;
			info.lastDistance = currentDistance;
			info.editorID = editorID ? editorID : "";
			info.modelPath = modelPath ? modelPath : "";

			if (g_settings.logMatches) {
				SKSE::log::info(
					"Tracking player shout cone projectile instance {} [{:08X}]: model={}, cone {} -> {}, tangent {} -> {}, originalInitialCollision={}, soulSnapshot={}, collisionSoulSnapshot={}, vfxSoulSnapshot={}, rootZSnapshot={}, densitySnapshot={} (no minimum floor)",
					info.editorID,
					a_base->formID,
					info.modelPath,
					info.originalConeSpread,
					info.targetConeSpread,
					info.originalConeTangent,
					info.targetConeTangent,
					info.originalInitialCollisionRadius,
					info.soulCountSnapshot,
					info.collisionSoulMultiplier,
					info.vfxSoulMultiplier,
					info.rootZOffset,
					info.densityGain);
			}
		}
		if (!inserted && distanceReset && !identity.handle) {
			// A zero-handle fallback cannot carry engine generation bits. Treat a
			// distance reset as a new cast for the soul snapshot, but retain the first
			// pristine cone/radius baseline so an already-patched runtime radius is
			// never captured and multiplied again.
			const auto scaling = ysmir::core::ScalingForProjectile(a_projectile);
			info.soulCountSnapshot = scaling.souls;
			info.collisionSoulMultiplier = scaling.collision;
			info.vfxSoulMultiplier = scaling.vfx;
			info.rootZOffset = scaling.height;
			info.densityGain = scaling.density;
		}
		info.lastTouched = ++g_projectilePatchClock;
		info.lastDistance = distanceReset ? currentDistance : std::max(info.lastDistance, currentDistance);

		return info;
	}

	void PatchRuntimeCollision(RE::ConeProjectile* a_projectile, const ProjectilePatchInfo& a_info)
	{
		if (g_settings.patchRuntimeCollision) {
			const auto soulCollision = a_info.collisionSoulMultiplier;
			const auto targetInitialRadius = a_info.originalInitialCollisionRadius * soulCollision;
			auto& coneData = a_projectile->GetConeRuntimeData();
			coneData.coneAngleTangent = std::max(coneData.coneAngleTangent, a_info.targetConeTangent);
			coneData.initialCollisionSphereRadius = std::max(coneData.initialCollisionSphereRadius, targetInitialRadius);
			if (coneData.collisionShape) {
				coneData.collisionShape->radius = std::max(coneData.collisionShape->radius, targetInitialRadius);
			}
		}

	}

	[[nodiscard]] VisualScaleResult ComputeVisualScale(RE::ConeProjectile* a_projectile, const ProjectilePatchInfo& a_info)
	{
		VisualScaleResult result;
		if (!g_settings.patchVisuals) {
			return result;
		}

		const auto& runtimeData = a_projectile->GetProjectileRuntimeData();
		const auto distance = std::max(0.0f, runtimeData.distanceMoved);
		const auto soulCollision = a_info.collisionSoulMultiplier;
		const auto fixedVisualScale = static_cast<double>(a_info.vfxSoulMultiplier);
		const auto visualCalibrationProgress = VisualCalibrationProgress(fixedVisualScale);
		const auto targetInitialRadius = a_info.originalInitialCollisionRadius * soulCollision;
		result.originalInitialRadius = a_info.originalInitialCollisionRadius;
		result.targetInitialRadius = targetInitialRadius;
		result.collisionSoulMultiplier = soulCollision;
		result.visualCalibrationProgress = static_cast<float>(visualCalibrationProgress);
		result.fixedVisualScale = static_cast<float>(std::min(
			fixedVisualScale,
			static_cast<double>(std::numeric_limits<float>::max())));
		result.rootZOffset = a_info.rootZOffset;
		result.densityGain = a_info.densityGain;
		result.originalRadius = std::max(0.0f, a_info.originalInitialCollisionRadius + distance * a_info.originalConeTangent);
		result.targetRadius = std::max(0.0f, targetInitialRadius + distance * a_info.targetConeTangent);
		// VFX has its own soul curve. Collision-radius growth remains independent and
		// must not be multiplied into the visible birth size a second time.
		result.visualTargetRadius = std::max(
			0.0f,
			a_info.originalInitialCollisionRadius + distance * a_info.targetConeTangent);
		result.collisionEnvelopeScale = result.originalRadius > std::numeric_limits<float>::epsilon() ?
			result.targetRadius / result.originalRadius : 1.0f;
		result.distanceEnvelopeScale = result.originalRadius > std::numeric_limits<float>::epsilon() ?
			result.visualTargetRadius / result.originalRadius : 1.0f;
		if (g_settings.useV12VisualPath) {
			if (g_settings.distanceVisualScale) {
				result.mode = "v1.2-local-reference-scale-plus-angle-envelope";
				// Player birth scale is the core V multiplier snapshot. Distance
				// correction contains only the fixed 15-degree angle envelope; the
				// independent collision-soul multiplier never double-scales VFX.
				result.scale = result.fixedVisualScale * result.distanceEnvelopeScale;
			} else {
				result.mode = "v1.2-local-reference-scale";
				result.scale = result.fixedVisualScale;
			}
			return result;
		}

		result.calibrationWeight = GetCalibrationWeight(a_info, distance);
		result.measuredHalfWidth = GetMeasuredVanillaHalfWidth(a_info, distance);

		if (result.measuredHalfWidth > 1.0f && result.calibrationWeight > 0.0f) {
			result.mode = "measured-nif-with-runtime-original-radius";
			const auto geometryScale = std::max(result.distanceEnvelopeScale, result.visualTargetRadius / result.measuredHalfWidth);
			result.scale = std::lerp(result.distanceEnvelopeScale, geometryScale, result.calibrationWeight);
		} else {
			result.scale = result.distanceEnvelopeScale;
		}
		result.scale = std::max(1.0f, result.scale) * result.fixedVisualScale;

		return result;
	}

	[[nodiscard]] ProjectilePatchInfo* GetPatchInfo(RE::ConeProjectile* a_projectile)
	{
		if (!g_settings.enabled || !a_projectile) {
			return nullptr;
		}

		auto* projectileBase = GetProjectileBase(a_projectile);
		if (!projectileBase) {
			return nullptr;
		}

		if (!IsShoutConeProjectile(a_projectile)) {
			if (g_settings.logMatches && g_loggedRejectedProjectiles.insert(projectileBase->formID).second) {
				const auto& runtimeData = a_projectile->GetProjectileRuntimeData();
				const auto* spell = runtimeData.spell;
				const auto* editorID = projectileBase->GetFormEditorID();
				SKSE::log::info(
					"Rejected cone projectile {} [{:08X}]: spell={:08X}, spellType={}, hardPlayerOwned=1, hardVoicePower=1",
					editorID ? editorID : "",
					projectileBase->formID,
					spell ? spell->formID : 0,
					spell ? static_cast<std::int32_t>(spell->GetSpellType()) : -1);
			}
			return nullptr;
		}

		return std::addressof(GetOrCreatePatchInfo(a_projectile, projectileBase));
	}

	void ApplyRuntimePatch(RE::ConeProjectile* a_projectile, std::string_view a_phase)
	{
		// Update3D and 3D-loaded callbacks are not assumed to share a thread. Keep
		// instance caches and scene-graph baseline bookkeeping coherent if the
		// target runtime dispatches them concurrently.
		std::scoped_lock lock(g_runtimeStateMutex);
		if (a_phase == "pre-update" && !g_updateHookObserved) {
			g_updateHookObserved = true;
			SKSE::log::info("ConeProjectile::UpdateImpl hook observed at runtime");
		}
		if (a_phase == "3d-loaded" && !g_3DLoadedHookObserved) {
			g_3DLoadedHookObserved = true;
			SKSE::log::info("ConeProjectile::Handle3DLoaded hook observed at runtime");
		}
		auto* info = GetPatchInfo(a_projectile);
		if (!info) {
			return;
		}

		PatchRuntimeCollision(a_projectile, *info);
		if (a_phase == "pre-update" && !g_settings.useV12VisualPath) {
			return;
		}

		if (g_settings.patchVisuals) {
			const auto isSoulTear = IsSoulTearProjectile(a_projectile, *info);
			if (isSoulTear && g_settings.logMatches &&
			    g_loggedSoulTearVisualModes.insert(info->editorID).second) {
				SKSE::log::info(
					"Soul Tear player cone {} keeps root V/H and 15-degree correction but excludes the TCS core-node transform",
					info->editorID);
			}
			auto* root = a_projectile->Get3D();
			if (!root) {
				return;
			}

			DumpSceneTreeOnce(*info, root);
			const auto scaleResult = ComputeVisualScale(a_projectile, *info);
			auto& visualState = GetVisualState(a_projectile, root);
			const auto& runtimeData = a_projectile->GetProjectileRuntimeData();
			if (visualState.lastDistance >= 0.0f && runtimeData.distanceMoved + 0.01f < visualState.lastDistance) {
				// Restore every still-live node in the current tree before discarding
				// captured transforms; otherwise an allocation/runtime reset could make
				// an already-scaled transform the next baseline.
				RestoreCapturedV12Transforms(root, visualState.v12Baselines);
				visualState.geometries.clear();
				visualState.v12Baselines.clear();
				visualState.originalRootToProjectileZ = 0.0f;
				visualState.hasOriginalRootToProjectileZ = false;
				visualState.lastLoggedVisualScale = -1.0f;
				visualState.loggedPhaseMask = 0;
			}
			visualState.lastDistance = runtimeData.distanceMoved;
			const auto beforeMetrics = g_settings.logMatches ? GetGeometryMetrics(root) : GeometryMetrics{};

			std::uint32_t visualTargetCount{};
			if (g_settings.useV12VisualPath) {
				if (!visualState.hasOriginalRootToProjectileZ) {
					visualState.originalRootToProjectileZ =
						root->local.translate.z - a_projectile->GetPositionZ();
					visualState.hasOriginalRootToProjectileZ = true;
				}
				visualTargetCount = CaptureAndScaleV12Default(
					root,
					visualState.v12Baselines,
					scaleResult.scale,
					true,
					false,
					false,
					kEnableV12CoreNodeTransforms && !isSoulTear);
				// The projectile root/reference receives the exact V snapshot. Preserve
				// TCS v1.2's first-core-node X/Z/scale transform for all player cones
				// except Soul Tear; H and density share the uncapped core Q snapshot.
				root->local.translate.z =
					a_projectile->GetPositionZ() +
					visualState.originalRootToProjectileZ +
					scaleResult.rootZOffset;
				RE::NiUpdateData updateData{};
				root->UpdateTransformAndBounds(updateData);
			} else {
				visualTargetCount = ApplyGeometryWorldScale(root, visualState, scaleResult.scale);
			}
			const auto afterMetrics = g_settings.logMatches ? GetGeometryMetrics(root) : GeometryMetrics{};
			DensityMetrics densityMetrics;
			const auto densityExclusion = GetDensityExclusion(a_projectile, *info);
			if (g_settings.densityCompensation &&
			    densityExclusion.excluded &&
			    a_phase != "pre-update") {
				const auto exclusionKey = info->editorID + "|" + densityExclusion.spellEditorID + "|" + densityExclusion.token;
				if (g_loggedDensityExclusions.insert(exclusionKey).second) {
					SKSE::log::info(
						"DENSITY excluded: projectileEditorID={}, spellEditorID={}, model={}, matchedToken={}, visualScale={} and collision correction remain active",
						info->editorID,
						densityExclusion.spellEditorID,
						info->modelPath,
						densityExclusion.token,
						scaleResult.scale);
				}
			}
			if (g_settings.useV12VisualPath &&
			    g_settings.densityCompensation &&
			    !densityExclusion.excluded &&
			    a_phase != "pre-update") {
				std::unordered_set<std::uintptr_t> visitedShaders;
				ApplyVfxDensityCompensation(root, scaleResult.densityGain, densityMetrics, visitedShaders);
			}
			const auto densityTargetCount = densityMetrics.opacityTargets + densityMetrics.additiveTargets;
			if (densityTargetCount == 0) {
				densityMetrics.minBefore = 0.0f;
				densityMetrics.minAfter = 0.0f;
			}

			const auto phaseMask = PhaseMask(a_phase);
			const auto shouldLogFixedScalePhase = (visualState.loggedPhaseMask & phaseMask) == 0;
			const auto shouldLogNormalScale =
				visualState.lastLoggedVisualScale < 0.0f ||
				std::abs(scaleResult.scale - visualState.lastLoggedVisualScale) >= 0.1f;
			const auto hasDynamicV12Scale =
				g_settings.useV12VisualPath &&
				g_settings.distanceVisualScale;
			if (g_settings.logMatches &&
			    (g_settings.useV12VisualPath && !hasDynamicV12Scale ? shouldLogFixedScalePhase : shouldLogNormalScale)) {
				SKSE::log::info(
					"{} VFX correction applied: phase={}, mode={}, model={}, distanceMoved={}, correctionScale={}, soulSnapshot={}, visualCalibrationProgress={}, fixedVisualScale={}, originalInitialRadius={}, targetInitialRadius={}, collisionSoulMultiplier={}, appliedRootZOffset={}, collisionEnvelopeScale={}, distanceEnvelopeScale={}, calibrationWeight={}, originalRadius={}, targetRadius={}, visualTargetRadius={}, measuredHalfWidth={}, visualTargets={}, densityEnabled={}, densityExcluded={}, densityExclusionToken={}, densityGain={}, densityOpacityTargets={}, densityAdditiveTargets={}, densityBefore=[{},{}], densityAfter=[{},{}], geometryCount={}, geometryWorldScaleBefore=[{},{}], geometryWorldScaleAfter=[{},{}], maxWorldBoundRadiusBefore={}, maxWorldBoundRadiusAfter={}, rootLocalScale={}, rootWorldScale={}, rootPreviousWorldScale={}",
					info->editorID,
					a_phase,
					scaleResult.mode,
					info->modelPath,
					runtimeData.distanceMoved,
					scaleResult.scale,
					info->soulCountSnapshot,
					scaleResult.visualCalibrationProgress,
					scaleResult.fixedVisualScale,
					scaleResult.originalInitialRadius,
					scaleResult.targetInitialRadius,
					scaleResult.collisionSoulMultiplier,
					scaleResult.rootZOffset,
					scaleResult.collisionEnvelopeScale,
					scaleResult.distanceEnvelopeScale,
					scaleResult.calibrationWeight,
					scaleResult.originalRadius,
					scaleResult.targetRadius,
					scaleResult.visualTargetRadius,
					scaleResult.measuredHalfWidth,
					visualTargetCount,
					g_settings.densityCompensation,
					densityExclusion.excluded,
					densityExclusion.token,
					scaleResult.densityGain,
					densityMetrics.opacityTargets,
					densityMetrics.additiveTargets,
					densityMetrics.minBefore,
					densityMetrics.maxBefore,
					densityMetrics.minAfter,
					densityMetrics.maxAfter,
					afterMetrics.count,
					beforeMetrics.minWorldScale,
					beforeMetrics.maxWorldScale,
					afterMetrics.minWorldScale,
					afterMetrics.maxWorldScale,
					beforeMetrics.maxWorldBoundRadius,
					afterMetrics.maxWorldBoundRadius,
					root->local.scale,
					root->world.scale,
					root->previousWorld.scale);
				visualState.lastLoggedVisualScale = scaleResult.scale;
				visualState.loggedPhaseMask |= phaseMask;
			}
		}
	}

	struct ConeProjectileUpdateHook
	{
		static void thunk(RE::ConeProjectile* a_projectile, float a_delta)
		{
			// Patch collision before native UpdateImpl, then reapply the verified v1.2
			// local-transform path before and after the native projectile update.
			ApplyRuntimePatch(a_projectile, "pre-update");
			func(a_projectile, a_delta);
			ApplyRuntimePatch(a_projectile, "post-update");
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ConeProjectileHandle3DLoadedHook
	{
		static void thunk(RE::ConeProjectile* a_projectile)
		{
			func(a_projectile);
			ApplyRuntimePatch(a_projectile, "3d-loaded");
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		if (g_hooksInstalled) {
			return;
		}

		auto vtable = REL::Relocation<std::uintptr_t>(RE::ConeProjectile::VTABLE[0]);
		ConeProjectileUpdateHook::func = vtable.write_vfunc(0xAB, ConeProjectileUpdateHook::thunk);
		ConeProjectileHandle3DLoadedHook::func = vtable.write_vfunc(0xC0, ConeProjectileHandle3DLoadedHook::thunk);
		g_hooksInstalled = true;
		SKSE::log::info("Installed True Cone Shouts ConeProjectile hooks during plugin load");
	}
}

namespace ysmir::true_cone
{
	bool Initialize()
	{
		LoadSettings();
		if (!g_settings.enabled) {
			SKSE::log::info("True Cone Shouts runtime disabled by Ysmir-TrueCone.ini; hooks were not installed");
			return true;
		}
		InstallHooks();
		return true;
	}

	void HandleMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}
		if (a_message->type == SKSE::MessagingInterface::kPreLoadGame ||
		    a_message->type == SKSE::MessagingInterface::kNewGame) {
			std::scoped_lock lock(g_runtimeStateMutex);
			g_visualStates.clear();
			g_densityShaderStates.clear();
			g_projectilePatches.clear();
			g_loggedRejectedProjectiles.clear();
			g_projectilePatchClock = 0;
			g_visualStateClock = 0;
			g_densityStateClock = 0;
			g_nextVisualPruneSize = kSoftMaxTrackedVisualProjectiles;
			g_nextPatchPruneSize = kSoftMaxTrackedProjectilePatches;
			g_loggedVisualSoftLimit = false;
			g_loggedPatchSoftLimit = false;
		}
	}
}

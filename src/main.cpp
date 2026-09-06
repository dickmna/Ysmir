#include "core.h"
#include "modules.h"
#include "RuntimeCompatibility.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <memory>

namespace
{
	constexpr auto PLUGIN_NAME = "Ysmir";
	bool g_frameHookInstalled{};

	bool ValidateRuntimeHooks()
	{
		using namespace ysmir::runtime;
		const auto base = REL::Module::get().base();
		const auto mainUpdate = REL::RelocationID(35565, 36564).address();
		if (mainUpdate != base + MainUpdateRva ||
		    std::memcmp(reinterpret_cast<const void*>(mainUpdate + FrameCallOffset),
			    FrameCallBytes.data(), FrameCallBytes.size()) != 0) {
			SKSE::log::critical("Main update call differs from verified Steam 1.7.104; no hooks installed");
			return false;
		}
		const std::array tables{
			std::pair{ RE::VTABLE_VoiceSpellFireHandler[0].address(), VoiceVTableRva },
			std::pair{ RE::VTABLE_Projectile[0].address(), ProjectileVTableRva },
			std::pair{ RE::VTABLE_BeamProjectile[0].address(), BeamVTableRva },
			std::pair{ RE::VTABLE_MissileProjectile[0].address(), MissileVTableRva },
			std::pair{ RE::VTABLE_ConeProjectile[0].address(), ConeVTableRva },
			std::pair{ RE::VTABLE_ScriptEffect[0].address(), ScriptEffectVTableRva },
			std::pair{ RE::VTABLE_CloakEffect[0].address(), CloakEffectVTableRva }
		};
		for (const auto& [actual, expected] : tables) {
			if (actual != base + expected) {
				SKSE::log::critical("Vtable {:X} differs from verified Steam 1.7.104; no hooks installed", expected);
				return false;
			}
		}
		const auto launch=REL::RelocationID(42928,44108).address();
		if (launch!=base+ProjectileLaunchRva || std::memcmp(reinterpret_cast<const void*>(launch),
			ProjectileLaunchBytes.data(),ProjectileLaunchBytes.size())!=0) {
			SKSE::log::critical("Projectile launch differs from verified Steam 1.7.104; no hooks installed");
			return false;
		}
		SKSE::log::info("Verified Steam 1.7.104 main call, projectile launch and seven vtables");
		return true;
	}

	struct MainUpdateHook
	{
		static void thunk()
		{
			func();
			ysmir::core::FrameUpdate();
			ysmir::storm_call::FrameUpdate();
			ysmir::projectile_visuals::FrameUpdate();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallFrameHook()
	{
		if (g_frameHookInstalled) {
			return;
		}
		SKSE::AllocTrampoline(14);
		auto& trampoline = SKSE::GetTrampoline();
		// At +0xC33 the engine clears a thread-local cache: no arguments or return value.
		// +0xC26 now returns a pointer consumed by the following call, so a void thunk
		// at that old offset would corrupt the caller even though it still sees E8.
		const auto callSite = REL::RelocationID(35565, 36564).address() + ysmir::runtime::FrameCallOffset;
		MainUpdateHook::func = trampoline.write_call<5>(callSite, MainUpdateHook::thunk);
		g_frameHookInstalled = MainUpdateHook::func.address() != 0;
		if (!g_frameHookInstalled) {
			SKSE::log::critical("Failed to install the unified main-frame update hook");
			return;
		}
		SKSE::log::info("Installed the unified main-frame update hook");
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_message)
	{
		ysmir::core::HandleMessage(a_message);
		ysmir::storm_call::HandleMessage(a_message);
	}

	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "Ysmir.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	}
}

SKSE_PLUGIN_VERSION = [] {
	SKSE::PluginVersionData version;
	version.PluginVersion({ 0, 5, 0, 0 });
	version.PluginName(PLUGIN_NAME);
	version.AuthorName("dickmna and Codex");
	version.UsesAddressLibrary();
	version.UsesUpdatedStructs();
	version.CompatibleVersions({ REL::Version{ 1, 7, 104, 0 } });
	version.MinimumRequiredXSEVersion({ 2, 3, 1, 0 });
	return version;
}();

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// Reject unverified runtimes before resolving addresses or writing any hooks.
	if (a_skse->RuntimeVersion() != REL::Version{ 1, 7, 104, 0 }) {
		return false;
	}
	SKSE::Init(a_skse);
	SetupLog();
	SKSE::log::info("{} v0.5.0 loading", PLUGIN_NAME);
	if (!ValidateRuntimeHooks()) {
		return false;
	}
	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler)) {
		SKSE::log::critical("Failed to register the unified SKSE messaging listener");
		return false;
	}

	if (!ysmir::core::Initialize()) {
		return false;
	}
	if (!ysmir::storm_call::Initialize()) {
		return false;
	}
	ysmir::projectile_visuals::Initialize();
	ysmir::true_cone::Initialize();
	InstallFrameHook();
	if (!g_frameHookInstalled) {
		return false;
	}

	SKSE::log::info("{} v0.5.0 initialized", PLUGIN_NAME);
	return true;
}

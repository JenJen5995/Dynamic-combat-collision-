#include "Collision.h"
#include "Hooks.h"
#include "ScaleMath.h"
#include "WallClip.h"
#include "Settings.h"
#include "TDM_API.h"
#include "TrueHUD_API.h"
#ifdef DCC_MENU_UI
#	include "MenuUI.h"
#endif

#include <filesystem>

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			if (TDM_API::Resolve()) {
				logger::debug("True Directional Movement API ready");
			} else {
				logger::warn("True Directional Movement API not found - lock-on only will do nothing");
			}
			TRUEHUD::Resolve();
			if (GetModuleHandleA("VariadicCollisionDynamics.dll")) {
				Collision::SetVcdFightOverride(true);
				logger::info("Variadic Collision Dynamics detected");
			} else {
				Collision::SetVcdFightOverride(false);
			}
			if (GetModuleHandleA("SkyParkourNG.dll") || GetModuleHandleA("SkyParkour.dll")) {
				Collision::SetSkyParkourPresent(true);
				logger::info("SkyParkour detected");
			} else {
				Collision::SetSkyParkourPresent(false);
			}
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			Hooks::Install();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Settings::Load();
			Collision::Reset();
			Collision::InitWeaponKeywords();
			if (std::filesystem::exists(L"Data/Interface/Translations/DynamicCombatCollision_ENGLISH.txt")) {
				SKSE::Translation::ParseTranslation("DynamicCombatCollision");
			}
#ifdef DCC_MENU_UI
			MenuUI::Register();
#endif
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Collision::Reset();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Settings::Load();
			Collision::Reset();
			break;
		default:
			break;
		}
	}

	void InitializeLog()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= fmt::format("{}.log"sv, Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
#ifndef NDEBUG
		log->set_level(spdlog::level::trace);
#else
		log->set_level(spdlog::level::info);
#endif
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION.pack();

	if (a_skse->IsEditor()) {
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
#if defined(ENABLE_SKYRIM_VR) && !defined(ENABLE_SKYRIM_SE) && !defined(ENABLE_SKYRIM_AE)
	if (ver >= SKSE::RUNTIME_VR_1_4_15) {
		return true;
	}
#else
	if (ver >= SKSE::RUNTIME_SSE_1_5_39) {
		return true;
	}
#endif

	return false;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData data;
	data.PluginVersion(Plugin::VERSION);
	data.PluginName(Plugin::NAME);
	data.AuthorName("typiak");
	data.UsesAddressLibrary();
#if defined(ENABLE_SKYRIM_VR) && !defined(ENABLE_SKYRIM_SE) && !defined(ENABLE_SKYRIM_AE)
	data.CompatibleVersions({ SKSE::RUNTIME_VR_1_4_15 });
#else
	data.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
#endif
	data.UsesNoStructs();
	return data;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	REL::Module::reset();

	InitializeLog();
	logger::info(
		"{} v{}.{}.{} loading ({})"sv,
		Plugin::NAME,
		Plugin::VERSION[0],
		Plugin::VERSION[1],
		Plugin::VERSION[2],
		REL::Module::get().version().string());

	if (!WallClip::RunSelfTest()) {
		logger::error("WallClip self-test failed");
		return false;
	}

	if (!ScaleMath::SelfTest()) {
		logger::error("ScaleMath self-test failed");
		return false;
	}

	if (!Collision::WeaponKeywordTableSelfTest()) {
		logger::error("weapon keyword cache self-test failed");
		return false;
	}

	SKSE::Init(a_skse);

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	return true;
}


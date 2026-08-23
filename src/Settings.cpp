#include "Settings.h"
#include "ScaleMath.h"

#include <filesystem>

namespace Settings
{
	namespace
	{
		constexpr auto kDefaultSettings = L"Data/MCM/Config/DynamicCombatCollision/settings.ini"sv;
		constexpr auto kMCMSettings = L"Data/MCM/Settings/DynamicCombatCollision.ini"sv;
		constexpr auto kMCMSettingsDir = L"Data/MCM/Settings"sv;

		std::filesystem::file_time_type g_userIniWrite{};

		void ClampAll()
		{
			fCombatScale = ScaleMath::ClampScale(fCombatScale);
			fFist = ScaleMath::ClampScale(fFist);
			fDagger = ScaleMath::ClampScale(fDagger);
			fSword = ScaleMath::ClampScale(fSword);
			fLongsword = ScaleMath::ClampScale(fLongsword);
		}

		void ReadIni(const std::wstring_view a_path)
		{
			CSimpleIniA ini;
			ini.SetUnicode();
			if (ini.LoadFile(a_path.data()) < 0) {
				return;
			}

			if (ini.GetSection("Collision")) {
				bEnabled = ini.GetBoolValue("Collision", "bEnabled", bEnabled);
				bLockTargetOnly = ini.GetBoolValue("Collision", "bLockTargetOnly", bLockTargetOnly);
				bDebugDraw = ini.GetBoolValue("Collision", "bDebugDraw", bDebugDraw);
				bShrinkWhenPinched = ini.GetBoolValue("Collision", "bShrinkWhenPinched", bShrinkWhenPinched);
				bPlayerImmovable = ini.GetBoolValue("Collision", "bPlayerImmovable", bPlayerImmovable);
				bAllyCombatCollision = ini.GetBoolValue("Collision", "bAllyCombatCollision", bAllyCombatCollision);
				fCombatScale = static_cast<float>(ini.GetDoubleValue("Collision", "fCombatScale", fCombatScale));
				fFist = static_cast<float>(ini.GetDoubleValue("Collision", "fFist", fFist));
				fDagger = static_cast<float>(ini.GetDoubleValue("Collision", "fDagger", fDagger));
				fSword = static_cast<float>(ini.GetDoubleValue("Collision", "fSword", fSword));
				fLongsword = static_cast<float>(ini.GetDoubleValue("Collision", "fLongsword", fLongsword));
			}
		}

		void RefreshUserIniTimestamp()
		{
			std::error_code ec;
			g_userIniWrite = std::filesystem::last_write_time(kMCMSettings, ec);
			if (ec) {
				g_userIniWrite = {};
			}
		}

		bool UserIniChanged()
		{
			std::error_code ec;
			const auto current = std::filesystem::last_write_time(kMCMSettings, ec);
			if (ec) {
				return false;
			}
			if (current == g_userIniWrite) {
				return false;
			}
			g_userIniWrite = current;
			return true;
		}
	}

	void Load()
	{
		ReadIni(kDefaultSettings);
		ReadIni(kMCMSettings);
		ClampAll();
		RefreshUserIniTimestamp();

		logger::info(
			"Dynamic Combat Collision: enabled={} lockTargetOnly={} allyCombat={} debugDraw={} shrinkWhenPinched={} playerImmovable={} default={:.2f} fist={:.2f} dagger={:.2f} sword={:.2f} longsword={:.2f}",
			bEnabled,
			bLockTargetOnly,
			bAllyCombatCollision,
			bDebugDraw,
			bShrinkWhenPinched,
			bPlayerImmovable,
			fCombatScale,
			fFist,
			fDagger,
			fSword,
			fLongsword);
	}

	void Save()
	{
		ClampAll();

		std::error_code ec;
		std::filesystem::create_directories(kMCMSettingsDir, ec);

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetBoolValue("Collision", "bEnabled", bEnabled);
		ini.SetBoolValue("Collision", "bLockTargetOnly", bLockTargetOnly);
		ini.SetBoolValue("Collision", "bDebugDraw", bDebugDraw);
		ini.SetBoolValue("Collision", "bShrinkWhenPinched", bShrinkWhenPinched);
		ini.SetBoolValue("Collision", "bPlayerImmovable", bPlayerImmovable);
		ini.SetBoolValue("Collision", "bAllyCombatCollision", bAllyCombatCollision);
		ini.SetDoubleValue("Collision", "fCombatScale", fCombatScale);
		ini.SetDoubleValue("Collision", "fFist", fFist);
		ini.SetDoubleValue("Collision", "fDagger", fDagger);
		ini.SetDoubleValue("Collision", "fSword", fSword);
		ini.SetDoubleValue("Collision", "fLongsword", fLongsword);

		if (ini.SaveFile("Data/MCM/Settings/DynamicCombatCollision.ini") < 0) {
			logger::warn("failed to save settings ini");
			return;
		}

		RefreshUserIniTimestamp();
	}

	void SyncFromDisk()
	{
		if (!UserIniChanged()) {
			return;
		}
		ReadIni(kMCMSettings);
		ClampAll();
	}

	void ReloadIfChanged()
	{
		static std::uint32_t framesUntilCheck = 0;
		if (framesUntilCheck > 0) {
			--framesUntilCheck;
			return;
		}
		framesUntilCheck = 30;

		SyncFromDisk();
	}
}

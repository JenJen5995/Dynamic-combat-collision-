#include "Settings.h"
#include "ScaleMath.h"

#include <filesystem>

namespace Settings
{
	namespace
	{
		std::filesystem::file_time_type g_userIniWrite{};

		std::filesystem::path GameDataDir()
		{
			const auto* exe = REL::Module::get().filePath().data();
			if (exe && exe[0] != L'\0') {
				return std::filesystem::path(exe).parent_path() / L"Data";
			}
			return std::filesystem::path(L"Data");
		}

		std::filesystem::path DefaultIniPath()
		{
			return GameDataDir() / L"MCM" / L"Config" / L"DynamicCombatCollision" / L"settings.ini";
		}

		std::filesystem::path UserIniPath()
		{
			return GameDataDir() / L"MCM" / L"Settings" / L"DynamicCombatCollision.ini";
		}

		void ClampAll()
		{
			if (iCombatNpcCap < 1) {
				iCombatNpcCap = 1;
			}
			if (iCombatNpcCap > 11) {
				iCombatNpcCap = 11;
			}
			fCombatScale = ScaleMath::ClampScale(fCombatScale);
			fFist = ScaleMath::ClampScale(fFist);
			fDagger = ScaleMath::ClampScale(fDagger);
			fWarAxe = ScaleMath::ClampScale(fWarAxe);
			fMace = ScaleMath::ClampScale(fMace);
			fSword = ScaleMath::ClampScale(fSword);
			fLongsword = ScaleMath::ClampScale(fLongsword);
			fWarhammer = ScaleMath::ClampScale(fWarhammer);
			fBattleaxe = ScaleMath::ClampScale(fBattleaxe);
			fPolearm = ScaleMath::ClampScale(fPolearm);
			fQuarterstaff = ScaleMath::ClampScale(fQuarterstaff);
			fRapier = ScaleMath::ClampScale(fRapier);
			fKatana = ScaleMath::ClampScale(fKatana);
			fClaw = ScaleMath::ClampScale(fClaw);
			fWhip = ScaleMath::ClampScale(fWhip);
		}

		void ReadIni(const std::filesystem::path& a_path)
		{
			CSimpleIniA ini;
			ini.SetUnicode();
			const auto wide = a_path.wstring();
			if (ini.LoadFile(wide.c_str()) < 0) {
				return;
			}

			if (ini.GetSection("Collision")) {
				bEnabled = ini.GetBoolValue("Collision", "bEnabled", bEnabled);
				bLockTargetOnly = ini.GetBoolValue("Collision", "bLockTargetOnly", bLockTargetOnly);
				bDebugDraw = ini.GetBoolValue("Collision", "bDebugDraw", bDebugDraw);
				bPlayerImmovable = ini.GetBoolValue("Collision", "bPlayerImmovable", bPlayerImmovable);
				bTranslationHelper = ini.GetBoolValue("Collision", "bTranslationHelper", bTranslationHelper);
				bWorldClipHelper = ini.GetBoolValue("Collision", "bWorldClipHelper", bWorldClipHelper);
				bAllyCombatCollision = ini.GetBoolValue("Collision", "bAllyCombatCollision", bAllyCombatCollision);
				bNpcOwnWeapon = ini.GetBoolValue("Collision", "bNpcOwnWeapon", bNpcOwnWeapon);
				iCombatNpcCap = ini.GetLongValue("Collision", "iCombatNpcCap", iCombatNpcCap);
				fCombatScale = static_cast<float>(ini.GetDoubleValue("Collision", "fCombatScale", fCombatScale));
				fFist = static_cast<float>(ini.GetDoubleValue("Collision", "fFist", fFist));
				fDagger = static_cast<float>(ini.GetDoubleValue("Collision", "fDagger", fDagger));
				fWarAxe = static_cast<float>(ini.GetDoubleValue("Collision", "fWarAxe", fWarAxe));
				fMace = static_cast<float>(ini.GetDoubleValue("Collision", "fMace", fMace));
				fSword = static_cast<float>(ini.GetDoubleValue("Collision", "fSword", fSword));
				fLongsword = static_cast<float>(ini.GetDoubleValue("Collision", "fLongsword", fLongsword));
				fWarhammer = static_cast<float>(ini.GetDoubleValue("Collision", "fWarhammer", fWarhammer));
				fBattleaxe = static_cast<float>(ini.GetDoubleValue("Collision", "fBattleaxe", fBattleaxe));
				fPolearm = static_cast<float>(ini.GetDoubleValue("Collision", "fPolearm", fPolearm));
				fQuarterstaff = static_cast<float>(ini.GetDoubleValue("Collision", "fQuarterstaff", fQuarterstaff));
				fRapier = static_cast<float>(ini.GetDoubleValue("Collision", "fRapier", fRapier));
				fKatana = static_cast<float>(ini.GetDoubleValue("Collision", "fKatana", fKatana));
				fClaw = static_cast<float>(ini.GetDoubleValue("Collision", "fClaw", fClaw));
				fWhip = static_cast<float>(ini.GetDoubleValue("Collision", "fWhip", fWhip));
			}
		}

		void RefreshUserIniTimestamp()
		{
			std::error_code ec;
			g_userIniWrite = std::filesystem::last_write_time(UserIniPath(), ec);
			if (ec) {
				g_userIniWrite = {};
			}
		}

		bool UserIniChanged()
		{
			std::error_code ec;
			const auto current = std::filesystem::last_write_time(UserIniPath(), ec);
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
		ReadIni(DefaultIniPath());
		ReadIni(UserIniPath());
		ClampAll();
		RefreshUserIniTimestamp();

		logger::info("settings file {}", UserIniPath().string());
		logger::info(
			"Dynamic Combat Collision: enabled={} lockTargetOnly={} allyCombat={} npcOwnWeapon={} combatNpcCap={} debugDraw={} playerImmovable={} translationHelper={} worldClip={} default={:.2f} fist={:.2f} dagger={:.2f} waraxe={:.2f} mace={:.2f} sword={:.2f} longsword={:.2f} warhammer={:.2f} battleaxe={:.2f} polearm={:.2f} quarterstaff={:.2f} rapier={:.2f} katana={:.2f} claw={:.2f} whip={:.2f}",
			bEnabled,
			bLockTargetOnly,
			bAllyCombatCollision,
			bNpcOwnWeapon,
			iCombatNpcCap,
			bDebugDraw,
			bPlayerImmovable,
			bTranslationHelper,
			bWorldClipHelper,
			fCombatScale,
			fFist,
			fDagger,
			fWarAxe,
			fMace,
			fSword,
			fLongsword,
			fWarhammer,
			fBattleaxe,
			fPolearm,
			fQuarterstaff,
			fRapier,
			fKatana,
			fClaw,
			fWhip);
	}

	void Save()
	{
		ClampAll();

		const auto path = UserIniPath();
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetBoolValue("Collision", "bEnabled", bEnabled);
		ini.SetBoolValue("Collision", "bLockTargetOnly", bLockTargetOnly);
		ini.SetBoolValue("Collision", "bDebugDraw", bDebugDraw);
		ini.SetBoolValue("Collision", "bPlayerImmovable", bPlayerImmovable);
		ini.SetBoolValue("Collision", "bTranslationHelper", bTranslationHelper);
		ini.SetBoolValue("Collision", "bWorldClipHelper", bWorldClipHelper);
		ini.SetBoolValue("Collision", "bAllyCombatCollision", bAllyCombatCollision);
		ini.SetBoolValue("Collision", "bNpcOwnWeapon", bNpcOwnWeapon);
		ini.SetLongValue("Collision", "iCombatNpcCap", iCombatNpcCap);
		ini.SetDoubleValue("Collision", "fCombatScale", fCombatScale);
		ini.SetDoubleValue("Collision", "fFist", fFist);
		ini.SetDoubleValue("Collision", "fDagger", fDagger);
		ini.SetDoubleValue("Collision", "fWarAxe", fWarAxe);
		ini.SetDoubleValue("Collision", "fMace", fMace);
		ini.SetDoubleValue("Collision", "fSword", fSword);
		ini.SetDoubleValue("Collision", "fLongsword", fLongsword);
		ini.SetDoubleValue("Collision", "fWarhammer", fWarhammer);
		ini.SetDoubleValue("Collision", "fBattleaxe", fBattleaxe);
		ini.SetDoubleValue("Collision", "fPolearm", fPolearm);
		ini.SetDoubleValue("Collision", "fQuarterstaff", fQuarterstaff);
		ini.SetDoubleValue("Collision", "fRapier", fRapier);
		ini.SetDoubleValue("Collision", "fKatana", fKatana);
		ini.SetDoubleValue("Collision", "fClaw", fClaw);
		ini.SetDoubleValue("Collision", "fWhip", fWhip);

		const auto wide = path.wstring();
		if (ini.SaveFile(wide.c_str()) < 0) {
			logger::warn("failed to save settings ini {}", path.string());
			return;
		}

		static bool s_loggedSave = false;
		if (!s_loggedSave) {
			s_loggedSave = true;
			logger::info("saved settings to {}", path.string());
		}

		RefreshUserIniTimestamp();
	}

	void SyncFromDisk()
	{
		if (!UserIniChanged()) {
			return;
		}

		const bool prevEnabled = bEnabled;
		const bool prevDraw = bDebugDraw;
		const float prevDefault = fCombatScale;
		const float prevSword = fSword;
		ReadIni(UserIniPath());
		ClampAll();
		if (prevEnabled != bEnabled ||
			prevDraw != bDebugDraw ||
			std::fabs(prevDefault - fCombatScale) > 0.001f ||
			std::fabs(prevSword - fSword) > 0.001f) {
			logger::info(
				"settings reloaded from disk: enabled={} debugDraw={} default={:.2f} sword={:.2f}",
				bEnabled,
				bDebugDraw,
				fCombatScale,
				fSword);
		}
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

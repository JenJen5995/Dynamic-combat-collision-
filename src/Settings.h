#pragma once

#include <cstdint>

namespace Settings
{
	void Load();
	void Save();
	void SyncFromDisk();
	void ReloadIfChanged();

	inline bool  bEnabled = true;
	inline bool  bLockTargetOnly = false;
	inline bool  bDebugDraw = false;
	inline bool  bPlayerImmovable = true;
	inline bool  bTranslationHelper = true;
	inline bool  bWorldClipHelper = true;
	inline bool  bAllyCombatCollision = false;
	inline bool  bNpcOwnWeapon = false;
	inline std::int32_t iCombatNpcCap = 11;
	inline float fCombatScale = 1.50f;
	inline float fFist = 1.50f;
	inline float fDagger = 1.50f;
	inline float fWarAxe = 1.50f;
	inline float fMace = 1.50f;
	inline float fSword = 3.00f;
	inline float fLongsword = 3.00f;
	inline float fWarhammer = 3.00f;
	inline float fBattleaxe = 3.00f;
	inline float fPolearm = 3.00f;
	inline float fQuarterstaff = 3.00f;
	inline float fRapier = 3.00f;
	inline float fKatana = 3.00f;
	inline float fClaw = 1.50f;
	inline float fWhip = 3.00f;
}

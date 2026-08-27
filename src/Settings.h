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
	inline std::int32_t iCombatNpcCap = 11;
	inline float fCombatScale = 1.50f;
	inline float fFist = 1.50f;
	inline float fDagger = 1.50f;
	inline float fSword = 3.00f;
	inline float fLongsword = 3.00f;
	inline float fWarhammer = 3.00f;
	inline float fBattleaxe = 3.00f;
}

#pragma once

namespace Settings
{
	void Load();
	void Save();
	void SyncFromDisk();
	void ReloadIfChanged();

	inline bool  bEnabled = true;
	inline bool  bLockTargetOnly = true;
	inline bool  bDebugDraw = false;
	inline bool  bShrinkWhenPinched = true;
	inline bool  bAllyCombatCollision = false;
	inline float fCombatScale = 1.50f;
	inline float fFist = 1.50f;
	inline float fDagger = 1.50f;
	inline float fSword = 3.00f;
	inline float fLongsword = 3.00f;
}

#pragma once

namespace Collision
{
	bool WeaponKeywordTableSelfTest();
	void InitWeaponKeywords();
	void Reset();
	void Update(float a_delta);
	void SetVcdFightOverride(bool a_present);
	void SetSkyParkourPresent(bool a_present);
	void InstallProxyHooks();
}

#pragma once

namespace Collision
{
	void InitWeaponKeywords();
	void Reset(const char* a_reason);
	void Update(float a_delta);
	void SetVcdFightOverride(bool a_present);
	void SetSkyParkourPresent(bool a_present);
	void InstallProxyHooks();
}

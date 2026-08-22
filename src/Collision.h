#pragma once

namespace Collision
{
	void Reset();
	void Update();
	void SetVcdFightOverride(bool a_present);
	void SetSkyParkourPresent(bool a_present);
	void InstallProxyHooks();
}

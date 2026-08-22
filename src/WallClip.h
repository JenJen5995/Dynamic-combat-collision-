#pragma once

#include <cmath>

namespace WallClip
{
	inline bool NormalizeXY(float& a_x, float& a_y)
	{
		const float len2 = a_x * a_x + a_y * a_y;
		if (!std::isfinite(len2) || len2 < 1.0e-8f) {
			return false;
		}
		const float inv = 1.0f / std::sqrt(len2);
		a_x *= inv;
		a_y *= inv;
		return true;
	}

	inline float VanillaWorldStopDistanceHk(float a_alongHk, float a_vanillaHk, float a_keepDistance)
	{
		if (!std::isfinite(a_alongHk) || !std::isfinite(a_vanillaHk) || !std::isfinite(a_keepDistance) ||
			a_vanillaHk <= 0.0f) {
			return a_alongHk;
		}
		return a_alongHk - a_vanillaHk - a_keepDistance;
	}

	inline bool RunSelfTest()
	{
		float x = 3.0f;
		float y = 4.0f;
		if (!NormalizeXY(x, y)) {
			return false;
		}
		if (std::fabs(x - 0.6f) > 0.001f || std::fabs(y - 0.8f) > 0.001f) {
			return false;
		}
		x = 0.0f;
		y = 0.0f;
		if (NormalizeXY(x, y)) {
			return false;
		}
		if (std::fabs(VanillaWorldStopDistanceHk(0.900f, 0.257f, 0.100f) - 0.543f) > 0.001f) {
			return false;
		}
		if (std::fabs(VanillaWorldStopDistanceHk(0.257f, 0.257f, 0.100f) + 0.100f) > 0.001f) {
			return false;
		}
		if (std::fabs(VanillaWorldStopDistanceHk(0.500f, 0.0f, 0.100f) - 0.500f) > 0.001f) {
			return false;
		}
		return true;
	}
}

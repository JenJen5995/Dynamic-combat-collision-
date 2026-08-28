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

	inline constexpr float kWalkableNormalZ = 0.50f;

	inline bool IsWalkableSupportNormal(float a_nx, float a_ny, float a_nz)
	{
		if (!std::isfinite(a_nx) || !std::isfinite(a_ny) || !std::isfinite(a_nz)) {
			return false;
		}
		const float n2 = a_nx * a_nx + a_ny * a_ny + a_nz * a_nz;
		if (n2 < 1.0e-8f) {
			return false;
		}
		return std::fabs(a_nz) >= kWalkableNormalZ * std::sqrt(n2);
	}

	inline constexpr float kSatisfiedSimplexPlaneW = 1.0f;
	inline constexpr float kPlayerIntoActorSpeedHk = 0.05f;

	inline bool KeepPlayerActorConstraint(
		float a_vx,
		float a_vy,
		float a_nx,
		float a_ny,
		float a_inwardMinHk = kPlayerIntoActorSpeedHk)
	{
		if (!std::isfinite(a_vx) || !std::isfinite(a_vy) || !std::isfinite(a_nx) || !std::isfinite(a_ny) ||
			!std::isfinite(a_inwardMinHk) || a_inwardMinHk < 0.0f) {
			return false;
		}
		float nx = a_nx;
		float ny = a_ny;
		if (!NormalizeXY(nx, ny)) {
			return false;
		}
		return (a_vx * nx + a_vy * ny) < -a_inwardMinHk;
	}
}

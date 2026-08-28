#pragma once

#include <cmath>

namespace ScaleMath
{
	inline constexpr float kMinScale = 1.10f;
	inline constexpr float kMaxScale = 5.00f;
	inline constexpr float kDefaultScale = 1.50f;
	inline constexpr float kVanilla = 1.00f;
	inline constexpr float kEpsilon = 0.01f;

	inline bool IsFinitePositive(float a_value)
	{
		return std::isfinite(a_value) && a_value > kEpsilon;
	}

	inline float ClampScale(float a_scale)
	{
		if (!std::isfinite(a_scale)) {
			return kDefaultScale;
		}
		if (a_scale < kMinScale) {
			return kMinScale;
		}
		if (a_scale > kMaxScale) {
			return kMaxScale;
		}
		return a_scale;
	}

	inline bool NeedsApply(float a_applied, float a_target)
	{
		return std::fabs(a_applied - a_target) > kEpsilon;
	}

	inline float ScaleForRelation(bool a_isPlayer, bool a_isHostile, float a_combatScale)
	{
		if (a_isPlayer || a_isHostile) {
			return a_combatScale;
		}
		return kDefaultScale;
	}

	inline float ClampGrowFactor(float a_factor, float a_maxGrow)
	{
		if (!std::isfinite(a_factor) || a_factor <= 0.0f) {
			return 0.0f;
		}
		if (a_factor > a_maxGrow) {
			return a_maxGrow;
		}
		return a_factor;
	}

	inline float ClampDepenetration(float a_pen, float a_max)
	{
		if (!std::isfinite(a_pen) || a_pen <= 0.0f) {
			return 0.0f;
		}
		if (!std::isfinite(a_max) || a_max <= 0.0f) {
			return 0.0f;
		}
		if (a_pen > a_max) {
			return a_max;
		}
		return a_pen;
	}

	inline void ScaleXY(float& a_x, float& a_y, float a_factor)
	{
		a_x *= a_factor;
		a_y *= a_factor;
	}

	inline float CombatExtraWorld(float a_slider, float a_humanXY)
	{
		if (!IsFinitePositive(a_humanXY) || a_slider <= kVanilla + kEpsilon) {
			return 0.0f;
		}
		const float extra = a_humanXY * (a_slider - kVanilla);
		if (!std::isfinite(extra) || extra <= 0.0f) {
			return 0.0f;
		}
		return extra;
	}

	inline float MaxCombatExtraWorld(float a_humanXY)
	{
		return CombatExtraWorld(kMaxScale, a_humanXY);
	}

	inline bool ScaleDeltaAllowed(float a_liveRadius, float a_wantRadius, float a_maxDelta)
	{
		if (!IsFinitePositive(a_liveRadius) || !IsFinitePositive(a_wantRadius) ||
			!IsFinitePositive(a_maxDelta)) {
			return false;
		}
		return std::fabs(a_wantRadius - a_liveRadius) <= a_maxDelta + 1.0f;
	}

	inline float FightOverrideWantedRadius(
		float a_liveRadius,
		float a_baseRadius,
		float a_slider,
		float a_humanXY)
	{
		if (!IsFinitePositive(a_liveRadius) || !IsFinitePositive(a_humanXY)) {
			return 0.0f;
		}
		if (a_slider <= kVanilla + kEpsilon) {
			if (IsFinitePositive(a_baseRadius)) {
				return a_baseRadius;
			}
			return a_humanXY;
		}
		const float base = IsFinitePositive(a_baseRadius) ? a_baseRadius : a_humanXY;
		const float want = base + CombatExtraWorld(a_slider, a_humanXY);
		if (!std::isfinite(want) || want <= kEpsilon) {
			return 0.0f;
		}
		return want;
	}

	inline float RadiusScaleFactor(float a_liveRadius, float a_wantRadius)
	{
		if (!IsFinitePositive(a_liveRadius) || !IsFinitePositive(a_wantRadius)) {
			return 0.0f;
		}
		return a_wantRadius / a_liveRadius;
	}

	inline float CapsuleRadiusForXYFactor(float a_radiusHk, float a_axisXYHk, float a_factor)
	{
		if (!IsFinitePositive(a_radiusHk) || !IsFinitePositive(a_factor)) {
			return 0.0f;
		}
		const float axis = (std::isfinite(a_axisXYHk) && a_axisXYHk > 0.0f) ? a_axisXYHk : 0.0f;
		const float live = a_radiusHk + axis;
		const float newR = a_factor * live - axis;
		if (!std::isfinite(newR) || newR <= kEpsilon) {
			return 0.0f;
		}
		return newR;
	}

	inline bool RadiusNeedsScale(float a_liveRadius, float a_wantRadius)
	{
		if (!IsFinitePositive(a_liveRadius) || !IsFinitePositive(a_wantRadius)) {
			return false;
		}
		return NeedsApply(1.0f, a_wantRadius / a_liveRadius);
	}

	inline float ShrinkFactor(float a_live, float a_want)
	{
		if (!IsFinitePositive(a_live) || !IsFinitePositive(a_want)) {
			return 1.0f;
		}
		if (a_live <= a_want + kEpsilon) {
			return 1.0f;
		}
		return a_want / a_live;
	}

	inline float CompressZFromMin(float a_z, float a_minZ, float a_sz)
	{
		return a_minZ + (a_z - a_minZ) * a_sz;
	}

	inline float SlideZTranslate(float a_minZ, float a_sz)
	{
		return (1.0f - a_sz) * a_minZ;
	}

	inline void ScaleSlidePlane(
		float& a_nx,
		float& a_ny,
		float& a_nz,
		float& a_w,
		float a_sx,
		float a_sy,
		float a_sz,
		float a_tz)
	{
		if (IsFinitePositive(a_sx)) {
			a_nx /= a_sx;
		}
		if (IsFinitePositive(a_sy)) {
			a_ny /= a_sy;
		}
		if (IsFinitePositive(a_sz)) {
			a_nz /= a_sz;
		}
		a_w -= a_nz * a_tz;
	}
}

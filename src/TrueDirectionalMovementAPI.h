#pragma once
#include <functional>
#include <stdint.h>

/*
* True Directional Movement public API header.
* Copyright (c) Ershin. GPL-3.0-or-later WITH Modding Exception and
* GPL-3.0 Linking Exception (with Corresponding Source). See COPYING and
* EXCEPTIONS in the upstream repository.
* Source: https://github.com/ersh1/TrueDirectionalMovement
* For modders: Copy this file into your own project if you wish to use this API
*/
	namespace TDM_API
{
	constexpr const auto TDMPluginName = "TrueDirectionalMovement";

	using PluginHandle = SKSE::PluginHandle;
	using ActorHandle = RE::ActorHandle;

	enum class InterfaceVersion : uint8_t
	{
		V1,
		V2,
		V3,
		V4,
		V5
	};

	enum class APIResult : uint8_t
	{
		OK,

		NotOwner,

		MustKeep,

		AlreadyGiven,

		AlreadyTaken,

		BadThread,
	};

	enum class DirectionalMovementMode : uint8_t
	{
		kDisabled,

		kVanillaStyle,

		kDirectional,
		
		kTargetLock
	};

	class IVTDM1
	{
	public:
		[[nodiscard]] virtual unsigned long GetTDMThreadId() const noexcept = 0;

		[[nodiscard]] virtual bool GetDirectionalMovementState() const noexcept = 0;

		[[nodiscard]] virtual bool GetTargetLockState() const noexcept = 0;

		[[nodiscard]] virtual ActorHandle GetCurrentTarget() const noexcept = 0;

		[[nodiscard]] virtual APIResult RequestDisableDirectionalMovement(PluginHandle a_myPluginHandle) noexcept = 0;

		[[nodiscard]] virtual APIResult RequestDisableHeadtracking(PluginHandle a_myPluginHandle) noexcept = 0;

		virtual PluginHandle GetDisableDirectionalMovementOwner() const noexcept = 0;

		virtual PluginHandle GetDisableHeadtrackingOwner() const noexcept = 0;

		virtual APIResult ReleaseDisableDirectionalMovement(PluginHandle a_myPluginHandle) noexcept = 0;

		virtual APIResult ReleaseDisableHeadtracking(PluginHandle a_myPluginHandle) noexcept = 0;
	};

	class IVTDM2 : public IVTDM1
	{
	public:
		[[nodiscard]] virtual APIResult RequestYawControl(PluginHandle a_myPluginHandle, float a_yawRotationSpeedMultiplier) noexcept = 0;

		virtual APIResult SetPlayerYaw(PluginHandle a_myPluginHandle, float a_desiredYaw) noexcept = 0;

		virtual APIResult ReleaseYawControl(PluginHandle a_myPluginHandle) noexcept = 0;
	};

	class IVTDM3 : public IVTDM2
	{
	public:
		[[nodiscard]] virtual DirectionalMovementMode GetDirectionalMovementMode() const noexcept = 0;

		[[nodiscard]] virtual RE::NiPoint2 GetActualMovementInput() const noexcept = 0;
	};

	class IVTDM4 : public IVTDM3
	{
	public:
		[[nodiscard]] virtual bool IsTargetLockBehindTarget() const noexcept = 0;
	};

	class IVTDM5 : public IVTDM4
	{
	public:
		[[nodiscard]] virtual APIResult RequestDisableTargetLock(PluginHandle a_myPluginHandle) noexcept = 0;

		virtual APIResult ReleaseDisableTargetLock(PluginHandle a_myPluginHandle) noexcept = 0;

		virtual PluginHandle GetDisableTargetLockOwner() const noexcept = 0;
	};

	typedef void* (*_RequestPluginAPI)(const InterfaceVersion interfaceVersion);

	[[nodiscard]] inline void* RequestPluginAPI(const InterfaceVersion a_interfaceVersion = InterfaceVersion::V5)
	{
		auto pluginHandle = GetModuleHandle("TrueDirectionalMovement.dll");
		_RequestPluginAPI requestAPIFunction = (_RequestPluginAPI)GetProcAddress(pluginHandle, "RequestPluginAPI");
		if (requestAPIFunction) {
			return requestAPIFunction(a_interfaceVersion);
		}
		return nullptr;
	}
}

#pragma once
#include <functional>
#include <queue>
#include <stdint.h>

/*
* TrueHUD public API header. Source: https://github.com/ersh1/TrueHUD
* Copyright (c) Ershin. GPL-3.0-or-later WITH Modding Exception and
* GPL-3.0 Linking Exception (with Corresponding Source). See COPYING and
* EXCEPTIONS in the upstream repository.
* For modders: Copy this file into your own project if you wish to use this API
*/
namespace TRUEHUD_API
{
	constexpr const auto TrueHUDPluginName = "TrueHUD";

	enum class InterfaceVersion : uint8_t
	{
		V1,
		V2,
		V3
	};

	enum class APIResult : uint8_t
	{
		OK,

		NotOwner,

		MustKeep,

		AlreadyGiven,

		AlreadyTaken,

		WidgetFailedToLoad,

		BadThread,
	};

	enum class WidgetRemovalMode : std::uint8_t
	{
		Immediate,

		Normal,

		Delayed
	};

	enum class PlayerWidgetBarType : std::uint8_t
	{
		HealthBar,
		MagickaBar,
		StaminaBar,
		SpecialBar
	};

	enum class BarColorType : std::uint8_t
	{
		BarColor,
		PhantomColor,
		BackgroundColor,
		PenaltyColor,
		FlashColor
	};

	using SpecialResourceCallback = std::function<float(RE::Actor* a_actor)>;
	using APIResultCallback = std::function<void(APIResult)>;

	class WidgetBase
	{
	public:
		using WidgetTask = std::function<void()>;
		using Lock = std::recursive_mutex;
		using Locker = std::lock_guard<Lock>;

		enum WidgetState : std::uint8_t
		{
			kActive = 0,
			kPendingHide = 1,
			kHidden = 2,
			kDelayedRemoval = 3,
			kPendingRemoval = 4,
			kRemoved = 5
		};

		WidgetBase() = default;
		virtual ~WidgetBase() = default;

		WidgetBase(RE::GPtr<RE::GFxMovieView> a_view) :
			_view(a_view),
			_widgetID(0)
		{}

		WidgetBase(uint32_t a_widgetID) :
			_widgetID(a_widgetID)
		{}

		WidgetBase(RE::GPtr<RE::GFxMovieView> a_view, uint32_t a_widgetID) :
			_view(a_view),
			_widgetID(a_widgetID)
		{}

		virtual void Update(float a_deltaTime) = 0;
		virtual void Initialize() = 0;
		virtual void Dispose() = 0;
		virtual void SetWidgetState(WidgetState a_newWidgetState)
		{
			_widgetState = a_newWidgetState;
		}

		void AddWidgetTask(WidgetTask a_task)
		{
			Locker locker(_lock);
			_taskQueue.push(std::move(a_task));
		}

		void ProcessDelegates()
		{
			while (!_taskQueue.empty()) {
				auto& task = _taskQueue.front();
				task();
				_taskQueue.pop();
			}
		}

		RE::GPtr<RE::GFxMovieView> _view;
		RE::GFxValue _object;
		uint32_t _widgetID;

		mutable Lock _lock;
		std::queue<WidgetTask> _taskQueue;

		WidgetState _widgetState = WidgetState::kHidden;

		float _depth = 0;
	};

	class IVTrueHUD1
	{
	public:
		[[nodiscard]] virtual unsigned long GetTrueHUDThreadId() const noexcept = 0;

		[[nodiscard]] virtual APIResult RequestTargetControl(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;

		[[nodiscard]] virtual APIResult RequestSpecialResourceBarsControl(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;

		virtual APIResult SetTarget(SKSE::PluginHandle a_myPluginHandle, RE::ActorHandle a_actorHandle) noexcept = 0;

		virtual APIResult SetSoftTarget(SKSE::PluginHandle a_myPluginHandle, RE::ActorHandle a_actorHandle) noexcept = 0;

		virtual void AddActorInfoBar(RE::ActorHandle a_actorHandle) noexcept = 0;

		virtual void RemoveActorInfoBar(RE::ActorHandle a_actorHandle, WidgetRemovalMode a_removalMode) noexcept = 0;

		virtual void AddBoss(RE::ActorHandle a_actorHandle) noexcept = 0;

		virtual void RemoveBoss(RE::ActorHandle a_actorHandle, WidgetRemovalMode a_removalMode) noexcept = 0;

		virtual void FlashActorValue(RE::ActorHandle a_actorHandle, RE::ActorValue a_actorValue, bool a_bLong) noexcept = 0;

		virtual APIResult FlashActorSpecialBar(SKSE::PluginHandle a_myPluginHandle, RE::ActorHandle a_actorHandle, bool a_bLong) noexcept = 0;

		virtual APIResult RegisterSpecialResourceFunctions(SKSE::PluginHandle a_myPluginHandle, SpecialResourceCallback&& a_getCurrentSpecialResource, SpecialResourceCallback&& a_getMaxSpecialResource, bool a_bSpecialMode, bool a_bDisplaySpecialForPlayer = true) noexcept = 0;

		virtual void LoadCustomWidgets(SKSE::PluginHandle a_myPluginHandle, std::string_view a_filePath, APIResultCallback&& a_successCallback) noexcept = 0;

		virtual void RegisterNewWidgetType(SKSE::PluginHandle a_myPluginHandle, uint32_t a_widgetType) noexcept = 0;

		virtual void AddWidget(SKSE::PluginHandle a_myPluginHandle, uint32_t a_widgetType, uint32_t a_widgetID, std::string_view a_symbolIdentifier, std::shared_ptr<WidgetBase> a_widget) noexcept = 0;

		virtual void RemoveWidget(SKSE::PluginHandle a_myPluginHandle, uint32_t a_widgetType, uint32_t a_widgetID, WidgetRemovalMode a_removalMode) noexcept = 0;

		virtual SKSE::PluginHandle GetTargetControlOwner() const noexcept = 0;

		virtual SKSE::PluginHandle GetPlayerWidgetBarColorsControlOwner() const noexcept = 0;

		virtual SKSE::PluginHandle GetSpecialResourceBarControlOwner() const noexcept = 0;

		virtual APIResult ReleaseTargetControl(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;

		virtual APIResult ReleaseSpecialResourceBarControl(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;
	};

	class IVTrueHUD2 : public IVTrueHUD1
	{
	public:
		virtual void OverrideBarColor(RE::ActorHandle a_actorHandle, RE::ActorValue a_actorValue, BarColorType a_colorType, uint32_t a_color) noexcept = 0;

		virtual void OverrideSpecialBarColor(RE::ActorHandle a_actorHandle, BarColorType a_colorType, uint32_t a_color) noexcept = 0;

		virtual void RevertBarColor(RE::ActorHandle a_actorHandle, RE::ActorValue a_actorValue, BarColorType a_colorType) noexcept = 0;

		virtual void RevertSpecialBarColor(RE::ActorHandle a_actorHandle, BarColorType a_colorType) noexcept = 0;
	};

	class IVTrueHUD3 : public IVTrueHUD2
	{
	public:
		virtual void DrawLine(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawPoint(const RE::NiPoint3& a_position, float a_size, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF) noexcept = 0;
		virtual void DrawArrow(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_size = 10.f, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawBox(const RE::NiPoint3& a_center, const RE::NiPoint3& a_extent, const RE::NiQuaternion& a_rotation, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawCircle(const RE::NiPoint3& a_center, const RE::NiPoint3& a_x, const RE::NiPoint3& a_y, float a_radius, uint32_t a_segments, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawHalfCircle(const RE::NiPoint3& a_center, const RE::NiPoint3& a_x, const RE::NiPoint3& a_y, float a_radius, uint32_t a_segments, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawSphere(const RE::NiPoint3& a_origin, float a_radius, uint32_t a_segments = 16, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawCylinder(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_radius, uint32_t a_segments, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawCone(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_direction, float a_length, float a_angleWidth, float a_angleHeight, uint32_t a_segments, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;
		virtual void DrawCapsule(const RE::NiPoint3& a_origin, float a_halfHeight, float a_radius, const RE::NiQuaternion& a_rotation, float a_duration = 0.f, uint32_t a_color = 0xFF0000FF, float a_thickness = 1.f) noexcept = 0;

		[[nodiscard]] virtual bool HasInfoBar(RE::ActorHandle a_actorHandle, bool a_bFloatingOnly = false) const noexcept = 0;
	};

	typedef void* (*_RequestPluginAPI)(const InterfaceVersion interfaceVersion);

	[[nodiscard]] inline void* RequestPluginAPI(const InterfaceVersion a_interfaceVersion = InterfaceVersion::V3)
	{
		auto pluginHandle = GetModuleHandle("TrueHUD.dll");
		_RequestPluginAPI requestAPIFunction = (_RequestPluginAPI)GetProcAddress(pluginHandle, "RequestPluginAPI");
		if (requestAPIFunction) {
			return requestAPIFunction(a_interfaceVersion);
		}
		return nullptr;
	}
}

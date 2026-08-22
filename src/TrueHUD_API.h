#pragma once

#include "TrueHUDAPI.h"

namespace TRUEHUD
{
	namespace detail
	{
		inline TRUEHUD_API::IVTrueHUD3* g_interface = nullptr;
		inline bool g_resolved = false;
	}

	inline TRUEHUD_API::IVTrueHUD3* Resolve()
	{
		if (detail::g_resolved) {
			return detail::g_interface;
		}
		detail::g_resolved = true;

		if (!GetModuleHandleA("TrueHUD.dll")) {
			return nullptr;
		}

		detail::g_interface = static_cast<TRUEHUD_API::IVTrueHUD3*>(
			TRUEHUD_API::RequestPluginAPI(TRUEHUD_API::InterfaceVersion::V3));
		return detail::g_interface;
	}

	inline TRUEHUD_API::IVTrueHUD3* GetInterface()
	{
		return detail::g_interface;
	}
}

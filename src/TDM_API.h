#pragma once

#include <Windows.h>
#include "TrueDirectionalMovementAPI.h"

namespace TDM_API
{
	namespace detail
	{
		inline IVTDM5* g_interface = nullptr;
		inline bool g_resolved = false;
	}

	inline IVTDM5* Resolve()
	{
		if (detail::g_resolved) {
			return detail::g_interface;
		}
		detail::g_resolved = true;

		if (!GetModuleHandleA("TrueDirectionalMovement.dll")) {
			return nullptr;
		}

		detail::g_interface = static_cast<IVTDM5*>(RequestPluginAPI(InterfaceVersion::V5));
		return detail::g_interface;
	}

	inline IVTDM5* GetInterface()
	{
		return detail::g_interface;
	}
}

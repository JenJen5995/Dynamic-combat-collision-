#pragma once

#include <string_view>

namespace Plugin
{
	using namespace std::literals;

#ifndef DCC_VERSION_MAJOR
#	define DCC_VERSION_MAJOR 1
#	define DCC_VERSION_MINOR 4
#	define DCC_VERSION_PATCH 4
#endif

	inline constexpr std::string_view NAME = "DynamicCombatCollision"sv;
	inline constexpr REL::Version     VERSION{ DCC_VERSION_MAJOR, DCC_VERSION_MINOR, DCC_VERSION_PATCH };
}

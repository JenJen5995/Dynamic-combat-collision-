#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <SimpleIni.h>
#pragma warning(pop)

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std::literals;
namespace logger = SKSE::log;

#define DLLEXPORT __declspec(dllexport)

#include "Plugin.h"

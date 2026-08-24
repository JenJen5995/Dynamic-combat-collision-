cmake_minimum_required(VERSION 3.22)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(DynamicCombatCollision VERSION 1.3.3 LANGUAGES CXX)

# Keep src/Plugin.h fallback macros and vcpkg.json version in sync with this project() VERSION.
# directxtk and rapidcsv are required by CommonLibSSE-NG, not by this plugin's own sources.

add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_compile_options(/W4 /permissive- /Zc:preprocessor /utf-8)
endif()

if(NOT COMMONLIB_SSE_PATH)
  if(DEFINED ENV{CommonLibSSEPath_NG})
    set(COMMONLIB_SSE_PATH "$ENV{CommonLibSSEPath_NG}")
  elseif(DEFINED ENV{COMMONLIB_SSE_PATH})
    set(COMMONLIB_SSE_PATH "$ENV{COMMONLIB_SSE_PATH}")
  endif()
endif()

if(NOT COMMONLIB_SSE_PATH OR NOT EXISTS "${COMMONLIB_SSE_PATH}/CMakeLists.txt")
  message(FATAL_ERROR "CommonLibSSE-NG not found. Set -DCOMMONLIB_SSE_PATH=... or the CommonLibSSEPath_NG environment variable.")
endif()

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

option(ENABLE_SKYRIM_SE "Enable Skyrim SE support" ON)
option(ENABLE_SKYRIM_AE "Enable Skyrim AE support" ON)
option(ENABLE_SKYRIM_VR "Enable Skyrim VR support" OFF)

if(NOT ENABLE_SKYRIM_SE AND NOT ENABLE_SKYRIM_AE AND NOT ENABLE_SKYRIM_VR)
  message(FATAL_ERROR "Enable at least one Skyrim runtime (SE, AE, or VR).")
endif()

if(MSVC AND NOT DEFINED CMAKE_VS_GLOBALS)
  set(CMAKE_VS_GLOBALS "VCToolsVersion=14.44.35207" CACHE STRING "Match vcpkg MSVC toolset for spdlog linking")
endif()

add_subdirectory("${COMMONLIB_SSE_PATH}" CommonLibSSE EXCLUDE_FROM_ALL)

if(ENABLE_SKYRIM_VR)
  find_path(OPENVR_HEADERS NAMES openvr.h REQUIRED)
  target_include_directories(CommonLibSSE PRIVATE "${OPENVR_HEADERS}")
endif()

add_library(${PROJECT_NAME} SHARED
  src/main.cpp
  src/Settings.cpp
  src/Collision.cpp
  src/Hooks.cpp
)

if(ENABLE_SKYRIM_SE OR ENABLE_SKYRIM_AE)
  target_sources(${PROJECT_NAME} PRIVATE src/MenuUI.cpp)
  target_compile_definitions(${PROJECT_NAME} PRIVATE DCC_MENU_UI)
endif()

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  DCC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  DCC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  DCC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)

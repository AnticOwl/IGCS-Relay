cmake_minimum_required(VERSION 3.24)
project(IGCSDOFUniversalBridge VERSION 0.6.7 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This ReShade add-on must be built on Windows.")
endif()

set(RESHADE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/reshade" CACHE PATH "Vendored ReShade source/SDK root")
set(RESHADE_INCLUDE_DIR "${RESHADE_ROOT}/include")
set(IMGUI_INCLUDE_DIR "${RESHADE_ROOT}/deps/imgui")

foreach(required_file
  "${RESHADE_INCLUDE_DIR}/reshade.hpp"
  "${RESHADE_INCLUDE_DIR}/reshade_api.hpp"
  "${RESHADE_INCLUDE_DIR}/reshade_events.hpp"
  "${RESHADE_INCLUDE_DIR}/reshade_overlay.hpp"
  "${IMGUI_INCLUDE_DIR}/imgui.h"
  "${IMGUI_INCLUDE_DIR}/imconfig.h")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "Missing dependency: ${required_file}\nRun scripts\\Setup-All.cmd first.")
  endif()
endforeach()

add_library(IGCSDOF_UniversalBridge SHARED
  src/Main.cpp
  src/Exports.cpp
  src/BridgeState.cpp
  src/NamedPipeServer.cpp
  src/IgcsConnectorLink.cpp
  src/EngineMath.cpp)

# Keep the IGCS export names exact on both architectures. MSVC decorates C
# symbols on Win32, so the x86 .def aliases them back to the names expected by
# IGCS Connector.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_sources(IGCSDOF_UniversalBridge PRIVATE src/exports_x64.def)
else()
  target_sources(IGCSDOF_UniversalBridge PRIVATE src/exports_x86.def)
endif()

target_include_directories(IGCSDOF_UniversalBridge PRIVATE
  src
  "${RESHADE_INCLUDE_DIR}"
  "${IMGUI_INCLUDE_DIR}")

target_compile_features(IGCSDOF_UniversalBridge PRIVATE cxx_std_20)
target_compile_definitions(IGCSDOF_UniversalBridge PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(IGCSDOF_UniversalBridge PRIVATE psapi)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ADDON_SUFFIX ".addon64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(ADDON_SUFFIX ".addon32")
else()
  message(FATAL_ERROR "Unsupported pointer size: ${CMAKE_SIZEOF_VOID_P}")
endif()

set_target_properties(IGCSDOF_UniversalBridge PROPERTIES
  PREFIX ""
  SUFFIX "${ADDON_SUFFIX}"
  OUTPUT_NAME "IGCSRelay"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(MSVC)
  target_compile_options(IGCSDOF_UniversalBridge PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

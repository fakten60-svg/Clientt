# ============================================================================
#  woke.wtf — native C++ injection utility client (Minecraft 1.21.11 / Fabric)
#  Top-level build. Produces: libwoke.so  (x86_64 Linux, C++20)
#
#  Prerequisite:  scripts/bootstrap_deps.sh   (vendors third_party/ + mappings.json)
#  Presets:       cmake --preset linux-gcc-debug | linux-gcc-release | linux-clang-release
# ============================================================================
cmake_minimum_required(VERSION 3.27)

project(woke
  VERSION 0.1.0
  DESCRIPTION "woke.wtf native utility client"
  LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global language & hygiene defaults
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

option(WOKE_ENABLE_ASAN "Enable AddressSanitizer instrumentation" OFF)
option(WOKE_ENABLE_LTO  "Enable link-time optimization for release builds" ON)
option(WOKE_BUNDLE_GLFW_HEADERS "Fall back to vendored GLFW headers when no system GLFW exists" ON)

if(WOKE_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

# ---------------------------------------------------------------------------
# Vendored dependency guard — scripts/bootstrap_deps.sh populates these
# ---------------------------------------------------------------------------
set(WOKE_TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(_woke_missing "")
foreach(_rel IN ITEMS
    minhook/include/MinHook.h
    minhook/src/hook.c
    imgui/imgui.cpp
    imgui/imgui.h
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    glfw/include/GLFW/glfw3.h
    nlohmann/json.hpp)
  if(NOT EXISTS "${WOKE_TP}/${_rel}")
    list(APPEND _woke_missing "  third_party/${_rel}")
  endif()
endforeach()
if(_woke_missing)
  list(JOIN _woke_missing "\n" _woke_missing_fmt)
  message(FATAL_ERROR "Missing vendored dependencies:\n${_woke_missing_fmt}\nRun:  scripts/bootstrap_deps.sh")
endif()

# ---------------------------------------------------------------------------
# MinHook — Linux/POSIX-ported fork (static, PIC; linked into libwoke.so)
# ---------------------------------------------------------------------------
add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde32.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC "${WOKE_TP}/minhook/include")
target_include_directories(minhook PRIVATE "${WOKE_TP}/minhook/src")
# _GNU_SOURCE: MAP_FIXED_NOREPLACE (buffer.c) and gettid()/tgkill (hook.c)
target_compile_definitions(minhook PRIVATE _GNU_SOURCE)
set_target_properties(minhook PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Dear ImGui (docking tag) + GLFW / OpenGL3 backends
# ---------------------------------------------------------------------------
add_library(imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_demo.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_glfw.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC
  "${WOKE_TP}/imgui"
  "${WOKE_TP}/imgui/backends")
# Never drag system GL headers through glfw3.h: the OpenGL3 backend ships its
# own embedded loader, and libGL resolves from the target game process at runtime.
target_compile_definitions(imgui PRIVATE GLFW_INCLUDE_NONE)

# GLFW resolution: prefer the system/shared library (matches the game's own
# libglfw at runtime); otherwise compile against vendored headers only — glfw*
# symbols then resolve from the injected host process.
set(_woke_glfw "")
find_package(glfw3 QUIET)
if(TARGET glfw)
  set(_woke_glfw glfw)
elseif(TARGET glfw3::glfw)
  set(_woke_glfw glfw3::glfw)
elseif(WOKE_BUNDLE_GLFW_HEADERS)
  target_include_directories(imgui PUBLIC "${WOKE_TP}/glfw/include")
  message(STATUS "woke: system GLFW not found — using vendored headers (glfw* resolve from host at runtime)")
else()
  message(FATAL_ERROR "System GLFW not found and WOKE_BUNDLE_GLFW_HEADERS=OFF")
endif()
if(_woke_glfw)
  target_link_libraries(imgui PUBLIC ${_woke_glfw})
endif()

# OpenGL is optional at build time — libGL is always present in the target
# JVM/game process when libwoke.so is injected.
find_package(OpenGL QUIET)
if(TARGET OpenGL::GL)
  target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

# ---------------------------------------------------------------------------
# libwoke.so — the injected client
# ---------------------------------------------------------------------------
file(GLOB_RECURSE WOKE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(NOT WOKE_SOURCES)
  # Phase 1 placeholder: keeps toolchain + vendored libs + link line verified
  # from day one. Disappears automatically once the first src/**/*.cpp lands.
  set(_woke_stub "${CMAKE_CURRENT_BINARY_DIR}/woke_placeholder.cpp")
  file(WRITE "${_woke_stub}"
    "// Generated by CMake — temporary Phase-1 placeholder (no src/ yet).\n"
    "extern \"C\" const char* woke_version() { return \"${PROJECT_VERSION}\"; }\n")
  list(APPEND WOKE_SOURCES "${_woke_stub}")
endif()

find_package(Threads REQUIRED)
add_library(woke SHARED ${WOKE_SOURCES})
target_include_directories(woke PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(woke PRIVATE WOKE_VERSION="${PROJECT_VERSION}")
target_compile_options(woke PRIVATE -Wall -Wextra)
target_link_libraries(woke PRIVATE minhook imgui Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(woke PROPERTIES
  OUTPUT_NAME "woke"
  PREFIX "lib"
  SUFFIX ".so"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

# ---------------------------------------------------------------------------
# LTO (release configs only; skipped gracefully if unsupported)
# ---------------------------------------------------------------------------
if(WOKE_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _woke_ipo OUTPUT _woke_ipo_msg LANGUAGES C CXX)
  if(_woke_ipo)
    set_property(TARGET woke minhook imgui PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "woke: LTO enabled for release builds")
  else()
    message(STATUS "woke: LTO unavailable, building without it: ${_woke_ipo_msg}")
  endif()
endif()

message(STATUS "woke: v${PROJECT_VERSION} | build=${CMAKE_BUILD_TYPE} asan=${WOKE_ENABLE_ASAN} lto=${WOKE_ENABLE_LTO}")

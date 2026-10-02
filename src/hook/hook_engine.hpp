// ============================================================================
//  woke.wtf — src/hook/hook_engine.hpp
//  Thin, defensive C++ wrapper over MinHook (Linux/POSIX port):
//    * engine_init()/engine_shutdown() — MH_Initialize / MH_Uninitialize
//    * create_and_enable()             — MH_CreateHook + MH_EnableHook with
//                                        status-string logging, one call site
//  Only the MinHook header lives in the .cpp; callers see plain void* targets.
// ============================================================================
#pragma once

namespace woke::hook {

// MH_Initialize — idempotent (re-initialization counts as success).
bool engine_init();

// True between a successful engine_init() and engine_shutdown().
bool engine_ready();

// MH_STATUS -> readable text (own switch; no dependency on MinHook helpers).
const char* status_string(int status);

// Hooks `target`, enables the detour, and writes the trampoline into
// *original. Logs and returns false on any MinHook failure.
bool create_and_enable(void* target, void* detour, void** original, const char* name);

// MH_DisableHook(all) + MH_Uninitialize — removes every installed hook.
void engine_shutdown();

} // namespace woke::hook

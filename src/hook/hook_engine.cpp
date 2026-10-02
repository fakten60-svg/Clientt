// ============================================================================
//  woke.wtf — src/hook/hook_engine.cpp
//  MinHook wrapper implementation. Every status is converted to text before
//  logging so failures are diagnosable from the session log alone.
// ============================================================================
#include "hook/hook_engine.hpp"

#include <atomic>

#include <MinHook.h>

#include "core/logger.hpp"

namespace woke::hook {

namespace {
std::atomic<bool> g_engine_ready{false};
} // namespace

const char* status_string(int status) {
    switch (static_cast<MH_STATUS>(status)) {
        case MH_OK:                 return "MH_OK";
        case MH_ERROR_ALREADY_INITIALIZED: return "MH_ERROR_ALREADY_INITIALIZED";
        case MH_ERROR_NOT_INITIALIZED:   return "MH_ERROR_NOT_INITIALIZED";
        case MH_ERROR_ALREADY_CREATED:   return "MH_ERROR_ALREADY_CREATED";
        case MH_ERROR_NOT_CREATED:       return "MH_ERROR_NOT_CREATED";
        case MH_ERROR_ENABLED:           return "MH_ERROR_ENABLED";
        case MH_ERROR_DISABLED:          return "MH_ERROR_DISABLED";
        case MH_ERROR_NOT_EXECUTABLE:    return "MH_ERROR_NOT_EXECUTABLE";
        case MH_ERROR_UNSUPPORTED_FUNCTION: return "MH_ERROR_UNSUPPORTED_FUNCTION";
        case MH_ERROR_MEMORY_ALLOC:      return "MH_ERROR_MEMORY_ALLOC";
        case MH_ERROR_MEMORY_PROTECT:    return "MH_ERROR_MEMORY_PROTECT";
        case MH_ERROR_MODULE_NOT_FOUND:  return "MH_ERROR_MODULE_NOT_FOUND";
        case MH_ERROR_FUNCTION_NOT_FOUND: return "MH_ERROR_FUNCTION_NOT_FOUND";
        default:                         break;
    }
    static thread_local char buf[32];
    std::snprintf(buf, sizeof buf, "MH_STATUS(%d)", status);
    return buf;
}

bool engine_init() {
    if (g_engine_ready.load(std::memory_order_acquire)) {
        return true;
    }
    const MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        WOKE_ERROR("hook", "MH_Initialize failed: %s", status_string(st));
        return false;
    }
    g_engine_ready.store(true, std::memory_order_release);
    WOKE_DEBUG("hook", "MinHook engine initialized (%s)", status_string(st));
    return true;
}

bool engine_ready() {
    return g_engine_ready.load(std::memory_order_acquire);
}

bool create_and_enable(void* target, void* detour, void** original, const char* name) {
    if (target == nullptr || detour == nullptr || original == nullptr) {
        WOKE_ERROR("hook", "create_and_enable(%s): null argument", name);
        return false;
    }
    if (!engine_init()) {
        return false;
    }

    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st != MH_OK) {
        WOKE_ERROR("hook", "MH_CreateHook(%s) failed: %s", name, status_string(st));
        return false;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        MH_RemoveHook(target);
        WOKE_ERROR("hook", "MH_EnableHook(%s) failed: %s", name, status_string(st));
        return false;
    }
    return true;
}

void engine_shutdown() {
    if (!g_engine_ready.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const MH_STATUS disable = MH_DisableHook(MH_ALL_HOOKS);
    const MH_STATUS uninit = MH_Uninitialize();
    WOKE_INFO("hook", "MinHook engine shut down (disable=%s, uninit=%s)",
              status_string(disable), status_string(uninit));
}

} // namespace woke::hook

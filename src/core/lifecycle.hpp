// ============================================================================
//  woke.wtf — src/core/lifecycle.hpp
//  Internal lifecycle core of libwoke.so, shared by the split implementation
//  files (libwoke.cpp + src/export/*.cpp). Not installed, not exported.
//
//  State machine:  kDetached -> kAttached (thread env + JavaVM) -> kReady
//                  (mappings parsed + reflection cache populated).
//
//  Files:
//    src/libwoke.cpp        constructor/destructor, JVM probe, deferred worker
//    src/export/lifecycle.cpp  JNI_OnLoad/OnUnload + status/mappings/cache API
//    src/export/modules.cpp    module/category/setting/keybind exports
//    src/export/game_gui.cpp   client-state, click-gui, ui-subsystem exports
// ============================================================================
#pragma once

#include <atomic>
#include <mutex>

#include <jni.h>

#include "jni/mappings.hpp"

namespace woke::lifecycle {

enum jni_state : int {
    kDetached = 0,
    kAttached = 1,   // thread env + JavaVM captured
    kReady = 2,      // mappings parsed + reflection cache populated
};

// ---- shared state (defined in src/libwoke.cpp) ------------------------------

extern std::atomic<int> g_state;
extern std::mutex g_lifecycle_mutex;
extern JavaVM* g_vm;                                  // guarded by g_lifecycle_mutex
extern jni::mappings_db g_mappings;                   // survives unload (reused)

// ---- deferred-init worker (pure-injection mode) ------------------------------

// Arms the worker once. Safe from the loader constructor: it only spawns a
// thread that sleeps before touching the JVM.
void start_deferred_init(JavaVM* vm);

// Wakes and joins the worker. Idempotent; never holds g_lifecycle_mutex, so it
// cannot deadlock against a worker that is inside jni_startup().
void stop_deferred_init();

// 1 while the pure-injection deferred-init worker is armed.
bool deferred_init_armed();

// Grace period (ms) the worker waits for a JNI_OnLoad before taking over.
// WOKE_DEFER_GRACE_MS overrides; default 1500, clamped to [0, 60000].
int defer_grace_ms();

// ---- startup / shutdown (defined in src/libwoke.cpp) -------------------------

// Attach `vm` (or reuse this thread's env), parse mappings.json, populate the
// reflection cache, then install the hook engine. Idempotent — guarded by
// g_lifecycle_mutex. Used by JNI_OnLoad and the deferred-init worker.
bool jni_startup(JavaVM* vm);

// Shared by JNI_OnUnload and the dlclose destructor — idempotent.
void jni_shutdown();

} // namespace woke::lifecycle

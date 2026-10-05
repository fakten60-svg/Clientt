// ============================================================================
//  woke.wtf — src/libwoke.cpp
//  libwoke.so lifecycle, executed by the dynamic loader / JVM:
//
//    __attribute__((constructor))  — unique timestamped session log under
//                                    logs/ + latest.log symlink + bootstrap
//                                    messages, then auto-attach to a running
//                                    JVM if one already exists (injection).
//    JNI_OnLoad()                  — JVM-driven entry (System.loadLibrary and
//                                    manual test calls): attach the thread,
//                                    parse mappings.json, populate the JNI
//                                    reflection cache, then install the hook
//                                    engine (glXSwapBuffers detour + bare
//                                    ImGui context). Returns JNI_VERSION_1_8
//                                    or JNI_ERR.
//    JNI_OnUnload()                — release all cached global refs.
//    __attribute__((destructor))   — idempotent JNI shutdown + logger detach.
//
//  Exported woke_* functions (visibility("default") despite -fvisibility=
//  hidden) let dlopen-based tests query the reflection cache.
// ============================================================================
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <unistd.h>

#include <jni.h>

#include "core/filesystem.hpp"
#include "core/logger.hpp"
#include "hook/hook_engine.hpp"
#include "hook/present_hook.hpp"
#include "jni/mappings.hpp"
#include "jni/reflection_cache.hpp"

#ifndef WOKE_VERSION
#define WOKE_VERSION "0.1.0-dev"
#endif

// Must survive -fvisibility=hidden: the JVM and tests resolve these by name.
#define WOKE_API __attribute__((visibility("default")))

namespace {

constexpr const char* kLogDir = "logs";
constexpr const char* kLatestLink = "logs/latest.log";

enum jni_state : int {
    kDetached = 0,
    kAttached = 1,   // thread env + JavaVM captured
    kReady = 2,      // mappings parsed + reflection cache populated
};

std::atomic<int> g_state{kDetached};
std::mutex g_lifecycle_mutex;
JavaVM* g_vm = nullptr;                            // guarded by g_lifecycle_mutex
woke::jni::mappings_db g_mappings;                 // survives unload (reused)

const char* mappings_path() {
    const char* p = std::getenv("WOKE_MAPPINGS_PATH");
    return (p != nullptr && p[0] != '\0') ? p : "mappings.json";
}

// logs/latest.log -> "<timestamp>.log" (relative target, same directory).
void update_latest_symlink(const char* session_path) {
    const char* base = std::strrchr(session_path, '/');
    base = (base != nullptr) ? base + 1 : session_path;

    if (::unlink(kLatestLink) != 0 && errno != ENOENT) {
        WOKE_WARN("core", "could not remove stale %s (errno=%d) — continuing",
                  kLatestLink, errno);
    }
    if (::symlink(base, kLatestLink) != 0) {
        WOKE_WARN("core", "symlink %s -> %s failed (errno=%d)",
                  kLatestLink, base, errno);
        return;
    }
    WOKE_INFO("core", "latest.log symlink: %s -> %s", kLatestLink, base);
}

// Attach `vm` (or reuse this thread's env), parse mappings.json, populate the
// reflection cache. Idempotent — guarded by g_lifecycle_mutex.
bool jni_startup(JavaVM* vm) {
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);

    if (g_state.load(std::memory_order_acquire) >= kAttached) {
        WOKE_DEBUG("jni", "jni_startup: already initialized (state=%d)",
                   g_state.load(std::memory_order_relaxed));
        return true;
    }

    // ---- 1) thread attachment ---------------------------------------------
    void* env_ptr = nullptr;
    jint rs = vm->GetEnv(&env_ptr, JNI_VERSION_1_8);
    if (rs == JNI_EDETACHED) {
        rs = vm->AttachCurrentThread(&env_ptr, nullptr);
        if (rs != JNI_OK || env_ptr == nullptr) {
            WOKE_ERROR("jni", "AttachCurrentThread failed (%d)", rs);
            return false;
        }
        WOKE_INFO("jni", "current thread attached to the JVM");
    } else if (rs != JNI_OK || env_ptr == nullptr) {
        WOKE_ERROR("jni", "JavaVM::GetEnv failed (%d)", rs);
        return false;
    }
    auto* env = static_cast<JNIEnv*>(env_ptr);
    g_vm = vm;
    g_state.store(kAttached, std::memory_order_release);
    WOKE_INFO("jni", "JVM attached (JNI version 0x%04x)", JNI_VERSION_1_8);

    // ---- 2) mappings.json ---------------------------------------------------
    if (g_mappings.class_count() == 0) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string err;
        if (!g_mappings.load(mappings_path(), &err)) {
            WOKE_ERROR("jni", "mappings load failed (%s): %s", mappings_path(), err.c_str());
            return false;
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        WOKE_INFO("jni", "mappings.json parsed: %zu classes, %zu methods, %zu fields (%.1f ms)",
                  g_mappings.class_count(), g_mappings.method_count(),
                  g_mappings.field_count(), ms);
    }

    // ---- 3) reflection cache ------------------------------------------------
    const woke::jni::populate_stats stats =
        woke::jni::reflection_cache::instance().populate(env, g_mappings);
    WOKE_INFO("jni",
              "reflection cache ready: classes %ld/%ld, methods %ld ok / %ld missing, "
              "fields %ld ok / %ld missing (%.1f ms)",
              stats.classes_ok, stats.classes_ok + stats.classes_missing,
              stats.methods_ok, stats.methods_missing,
              stats.fields_ok, stats.fields_missing, stats.elapsed_ms);

    // ---- 4) hook engine: glXSwapBuffers detour + bare ImGui context ---------
    if (woke::hook::present_startup()) {
        WOKE_INFO("hook", "hook engine initialized: %s active, ImGui %s",
                  woke::hook::present_target_name(),
                  woke::hook::imgui_initialized() ? "ready" : "missing");
    } else {
        WOKE_WARN("hook", "present hook unavailable (no glXSwapBuffers) — continuing without overlay");
    }

    g_state.store(kReady, std::memory_order_release);
    return true;
}

// Shared by JNI_OnUnload and the dlclose destructor — idempotent.
void jni_shutdown() {
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
    if (g_state.load(std::memory_order_acquire) == kDetached) {
        return;
    }

    auto& cache = woke::jni::reflection_cache::instance();
    JNIEnv* env = nullptr;
    if (g_vm != nullptr &&
        g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) == JNI_OK &&
        env != nullptr) {
        cache.release(env);
    } else {
        cache.clear();
    }

    // Hooks were installed by the same startup path — tear them down here.
    // Engine first (restores the original prologue), then the ImGui context,
    // so no detour can fire into a destroyed context.
    woke::hook::engine_shutdown();
    woke::hook::present_shutdown();

    g_vm = nullptr;
    g_state.store(kDetached, std::memory_order_release);
    WOKE_INFO("jni", "JNI shutdown complete — reflection cache released "
                     "(%zu mapping classes kept for re-attach)",
              g_mappings.class_count());
}

// Detect-only probe for the injection case (JNI_OnLoad never fires for an
// injected .so). CRITICAL: this must NOT run JVM classloading — FindClass
// inside a dlopen constructor deadlocks against glibc's loader lock (the
// classloader may need to dlopen while our constructor holds the lock).
// All heavy initialization therefore happens in JNI_OnLoad; a deferred-init
// worker thread for pure-injection setups lands in a later phase.
void probe_jvm_for_deferred_init() {
    using get_created_vms = jint(JNICALL*)(JavaVM**, jsize, jsize*);
    auto get_vms = reinterpret_cast<get_created_vms>(
        ::dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    if (get_vms == nullptr) {
        WOKE_INFO("jni", "no JVM present in this process — deferred to JNI_OnLoad");
        return;
    }
    JavaVM* vms[1] = {};
    jsize count = 0;
    if (get_vms(vms, 1, &count) != JNI_OK || count < 1 || vms[0] == nullptr) {
        WOKE_INFO("jni", "JVM not created yet — deferred to JNI_OnLoad");
        return;
    }
    WOKE_INFO("jni", "JVM present — deferring initialization to JNI_OnLoad "
                     "(no JVM work in the load constructor)");
}

} // namespace

// ----------------------------------------------------------------------------
// Executed by ld.so when libwoke.so is loaded.
// ----------------------------------------------------------------------------
__attribute__((constructor)) static void woke_on_load() {
    char session_path[512] = {};

    if (!woke::fs::get_timestamp_path(session_path, sizeof session_path, kLogDir, ".log")) {
        WOKE_ERROR("core", "bootstrap: cannot build session log path in %s — stderr only",
                   kLogDir);
        return;
    }
    if (!woke::fs::file_logger_attach(session_path)) {
        WOKE_ERROR("core", "bootstrap: cannot open session log '%s' (errno=%d) — stderr only",
                   session_path, errno);
        return;
    }

    update_latest_symlink(session_path);

    WOKE_INFO("core", "woke.wtf v%s native client loaded", WOKE_VERSION);
    WOKE_INFO("core", "session log attached: %s", session_path);
    WOKE_INFO("core", "logger initialized — core bootstrap complete");

    probe_jvm_for_deferred_init();
}

// ----------------------------------------------------------------------------
// Executed on dlclose / process exit.
// ----------------------------------------------------------------------------
__attribute__((destructor)) static void woke_on_unload() {
    jni_shutdown();   // no-op if JNI_OnUnload already ran
    WOKE_INFO("core", "libwoke unloading — detaching session logger");
    woke::fs::file_logger_detach();
}

// ----------------------------------------------------------------------------
// JVM-driven lifecycle + exported cache introspection (dlsym-able)
// ----------------------------------------------------------------------------
extern "C" {

WOKE_API jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    if (vm == nullptr) {
        WOKE_ERROR("jni", "JNI_OnLoad called with null JavaVM");
        return JNI_ERR;
    }
    if (!jni_startup(vm)) {
        return JNI_ERR;
    }
    WOKE_INFO("jni", "JNI_OnLoad complete — returning JNI_VERSION_1_8 (0x%04x)",
              JNI_VERSION_1_8);
    return JNI_VERSION_1_8;
}

WOKE_API void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    WOKE_INFO("jni", "JNI_OnUnload called — releasing reflection cache");
    jni_shutdown();
}

WOKE_API int woke_jni_status() {
    return g_state.load(std::memory_order_acquire);
}

WOKE_API long woke_mappings_class_count() {
    return static_cast<long>(g_mappings.class_count());
}
WOKE_API long woke_mappings_method_count() {
    return static_cast<long>(g_mappings.method_count());
}
WOKE_API long woke_mappings_field_count() {
    return static_cast<long>(g_mappings.field_count());
}

WOKE_API long woke_jni_cached_class_count() {
    return woke::jni::reflection_cache::instance().class_count();
}
WOKE_API long woke_jni_cached_method_count() {
    return woke::jni::reflection_cache::instance().method_count();
}
WOKE_API long woke_jni_cached_field_count() {
    return woke::jni::reflection_cache::instance().field_count();
}

// Yarn class name -> runtime intermediary name ("" when mappings lack it).
WOKE_API const char* woke_reflection_intermediary_class(const char* yarn_class) {
    if (yarn_class == nullptr) {
        return nullptr;
    }
    const std::string* s =
        woke::jni::reflection_cache::instance().intermediary_class(yarn_class);
    return (s != nullptr) ? s->c_str() : nullptr;
}

// Yarn class name -> cached jclass (global ref; null when unresolved).
WOKE_API void* woke_reflection_class(const char* yarn_class) {
    if (yarn_class == nullptr) {
        return nullptr;
    }
    return static_cast<void*>(
        woke::jni::reflection_cache::instance().find_class(yarn_class));
}

// Yarn class + method -> cached jmethodID. `descriptor` may be null to get
// the first resolved overload (pass the exact descriptor to disambiguate).
WOKE_API void* woke_reflection_method(const char* yarn_class, const char* method,
                                      const char* descriptor) {
    if (yarn_class == nullptr || method == nullptr) {
        return nullptr;
    }
    const std::string desc = (descriptor != nullptr) ? descriptor : std::string{};
    return static_cast<void*>(woke::jni::reflection_cache::instance().find_method(
        yarn_class, method, (descriptor != nullptr) ? &desc : nullptr));
}

WOKE_API void* woke_reflection_field(const char* yarn_class, const char* field,
                                     const char* descriptor) {
    if (yarn_class == nullptr || field == nullptr) {
        return nullptr;
    }
    const std::string desc = (descriptor != nullptr) ? descriptor : std::string{};
    return static_cast<void*>(woke::jni::reflection_cache::instance().find_field(
        yarn_class, field, (descriptor != nullptr) ? &desc : nullptr));
}

// ---- hook engine / present-hook introspection ------------------------------
WOKE_API int woke_hook_status() {
    return woke::hook::present_installed() ? 1 : 0;
}
WOKE_API int woke_hook_imgui_initialized() {
    return woke::hook::imgui_initialized() ? 1 : 0;
}
WOKE_API const char* woke_hook_target() {
    return woke::hook::present_target_name();
}
WOKE_API long long woke_hook_present_count() {
    return woke::hook::present_hit_count();
}
WOKE_API long long woke_hook_suppressed_count() {
    return woke::hook::present_suppressed_count();
}
WOKE_API long long woke_hook_imgui_frame_count() {
    return woke::hook::present_imgui_frame_count();
}
WOKE_API long long woke_hook_last_frame_ns() {
    return woke::hook::present_last_frame_ns();
}
WOKE_API long long woke_hook_total_frame_ns() {
    return woke::hook::present_total_frame_ns();
}
WOKE_API void woke_hook_set_gui_open(int open) {
    woke::hook::set_gui_open(open != 0);
}

} // extern "C"

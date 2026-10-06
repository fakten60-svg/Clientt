// ============================================================================
//  woke.wtf — src/libwoke.cpp
//  libwoke.so lifecycle core, executed by the dynamic loader / JVM:
//
//    __attribute__((constructor))  — unique timestamped session log under
//                                    logs/ + latest.log symlink + bootstrap
//                                    messages, then arm the deferred-init
//                                    worker when a JVM already exists
//                                    (pure injection: no JNI_OnLoad coming).
//    JNI_OnLoad()                  — JVM-driven entry (see export/lifecycle.cpp):
//                                    attach the thread, parse mappings.json,
//                                    populate the JNI reflection cache, then
//                                    install the hook engine (glXSwapBuffers
//                                    detour + bare ImGui context).
//    JNI_OnUnload()                — release all cached global refs.
//    __attribute__((destructor))   — idempotent JNI shutdown + logger detach.
//
//  The exported woke_* functions live in src/export/*.cpp, split by topic:
//  lifecycle.cpp (JNI entry + cache introspection), modules.cpp (module/
//  setting/keybind API) and game_gui.cpp (client-state, gui, ui, tasks, config).
// ============================================================================
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#include <jni.h>

#include "core/config.hpp"
#include "core/event_bus.hpp"
#include "core/filesystem.hpp"
#include "core/lifecycle.hpp"
#include "core/logger.hpp"
#include "core/task_queue.hpp"
#include "game/game_state.hpp"
#include "gui/gui.hpp"
#include "hook/hook_engine.hpp"
#include "hook/imgui_backend.hpp"
#include "hook/present_hook.hpp"
#include "jni/reflection_cache.hpp"
#include "modules/module.hpp"
#include "ui/animation.hpp"
#include "ui/notifications.hpp"

#ifndef WOKE_VERSION
#define WOKE_VERSION "0.1.0-dev"
#endif

namespace woke::lifecycle {

constexpr const char* kLogDir = "logs";
constexpr const char* kLatestLink = "logs/latest.log";

// ---- shared state (declared in core/lifecycle.hpp) ---------------------------

std::atomic<int> g_state{kDetached};
std::mutex g_lifecycle_mutex;
JavaVM* g_vm = nullptr;                            // guarded by g_lifecycle_mutex
jni::mappings_db g_mappings;                       // survives unload (reused)

// ---- deferred-init worker state ----------------------------------------------
// When the client is dlopen'd into an already-running JVM, JNI_OnLoad never
// fires, so nothing would ever call jni_startup(). The constructor cannot do
// the work itself: it runs under glibc's loader lock and the reflection cache
// calls FindClass, which may need to dlopen a jar while we hold that lock.
//
// The worker therefore waits out a grace period first — long enough for a
// plain System.loadLibrary to deliver its JNI_OnLoad, and long enough that our
// own dlopen has returned before any JNI call happens. Whatever runs first
// wins; jni_startup() is mutex-guarded and idempotent either way.
std::mutex g_defer_mutex;
std::condition_variable g_defer_cv;
std::atomic<bool> g_defer_stop{false};
std::atomic<bool> g_defer_armed{false};
std::thread g_defer_thread;

namespace {

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

void deferred_init_worker(JavaVM* vm) {
    {
        std::unique_lock<std::mutex> lock(g_defer_mutex);
        if (g_defer_cv.wait_for(lock, std::chrono::milliseconds(defer_grace_ms()),
                                [] { return g_defer_stop.load(std::memory_order_acquire); })) {
            return;   // JNI_OnLoad or unload claimed the process first
        }
    }
    if (g_defer_stop.load(std::memory_order_acquire) ||
        g_state.load(std::memory_order_acquire) != kDetached) {
        return;
    }
    WOKE_INFO("jni", "no JNI_OnLoad after %d ms — pure injection detected, "
                     "running deferred initialization",
              defer_grace_ms());
    if (!jni_startup(vm)) {
        WOKE_ERROR("jni", "deferred initialization failed for JVM %p",
                   static_cast<void*>(vm));
        return;
    }
    WOKE_INFO("jni", "deferred initialization complete (state=%d) — injected client ready",
              g_state.load(std::memory_order_acquire));
}

// Probe for the injection case (JNI_OnLoad never fires for an injected .so).
// CRITICAL: this must NOT run JVM classloading — FindClass inside a dlopen
// constructor deadlocks against glibc's loader lock (the classloader may need
// to dlopen while our constructor holds the lock). So a JVM found here only
// arms the deferred-init worker, which sleeps before doing any JNI work.
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
    start_deferred_init(vms[0]);
    WOKE_INFO("jni", "deferred-init worker armed (grace %d ms) — covers pure-injection "
                     "setups where JNI_OnLoad never fires",
              defer_grace_ms());
}

} // namespace

constexpr int kDeferGraceDefaultMs = 1500;

int defer_grace_ms() {
    const char* env = std::getenv("WOKE_DEFER_GRACE_MS");
    if (env != nullptr && env[0] != '\0') {
        char* end = nullptr;
        const long value = std::strtol(env, &end, 10);
        if (end != env && value >= 0 && value <= 60000) {
            return static_cast<int>(value);
        }
    }
    return kDeferGraceDefaultMs;
}

void start_deferred_init(JavaVM* vm) {
    std::lock_guard<std::mutex> lock(g_defer_mutex);
    if (g_defer_armed.load(std::memory_order_relaxed)) {
        return;
    }
    g_defer_stop.store(false, std::memory_order_relaxed);
    g_defer_thread = std::thread(deferred_init_worker, vm);
    g_defer_armed.store(true, std::memory_order_release);
}

void stop_deferred_init() {
    {
        std::lock_guard<std::mutex> lock(g_defer_mutex);
        if (!g_defer_armed.load(std::memory_order_relaxed)) {
            return;
        }
        g_defer_stop.store(true, std::memory_order_release);
    }
    g_defer_cv.notify_all();
    if (g_defer_thread.joinable()) {
        g_defer_thread.join();
    }
    g_defer_armed.store(false, std::memory_order_release);
}

bool deferred_init_armed() {
    return g_defer_armed.load(std::memory_order_acquire);
}

// ----------------------------------------------------------------------------
// Executed by ld.so when libwoke.so is loaded.
// ----------------------------------------------------------------------------
__attribute__((constructor)) static void woke_on_load() {
    char session_path[512] = {};

    if (!fs::get_timestamp_path(session_path, sizeof session_path, kLogDir, ".log")) {
        WOKE_ERROR("core", "bootstrap: cannot build session log path in %s — stderr only",
                   kLogDir);
        return;
    }
    if (!fs::file_logger_attach(session_path)) {
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
    fs::file_logger_detach();
}

// ----------------------------------------------------------------------------
// Startup / shutdown (shared with src/export/*.cpp via core/lifecycle.hpp)
// ----------------------------------------------------------------------------

// Attach `vm` (or reuse this thread's env), parse mappings.json, populate the
// reflection cache. Idempotent — guarded by g_lifecycle_mutex.
bool jni_startup(JavaVM* vm) {
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);

    if (g_state.load(std::memory_order_acquire) >= kAttached) {
        WOKE_DEBUG("jni", "jni_startup: already initialized (state=%d)",
                   g_state.load(std::memory_order_relaxed));
        return true;
    }

    // ---- 1) thread attachment -----------------------------------------------
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

    // ---- 2) mappings.json -----------------------------------------------------
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

    // ---- 3) reflection cache ----------------------------------------------------
    const jni::populate_stats stats = jni::reflection_cache::instance().populate(env, g_mappings);
    WOKE_INFO("jni",
              "reflection cache ready: classes %ld/%ld, methods %ld ok / %ld missing, "
              "fields %ld ok / %ld missing (%.1f ms)",
              stats.classes_ok, stats.classes_ok + stats.classes_missing,
              stats.methods_ok, stats.methods_missing,
              stats.fields_ok, stats.fields_missing, stats.elapsed_ms);

    // ---- 4) modules + config (client-state framework) ----------------------------
    game::game_state::instance().set_vm(vm);
    modules::register_builtins();
    config::load();   // applies saved module states + keybind

    // ---- 5) hook engine: glXSwapBuffers detour + ImGui context --------------------
    if (hook::present_startup()) {
        WOKE_INFO("hook", "hook engine initialized: %s active, ImGui %s",
                  hook::present_target_name(),
                  hook::imgui_initialized() ? "ready" : "missing");
    } else {
        WOKE_WARN("hook", "present hook unavailable (no glXSwapBuffers) — continuing without overlay");
    }

    g_state.store(kReady, std::memory_order_release);
    return true;
}

// Shared by JNI_OnUnload and the dlclose destructor — idempotent.
void jni_shutdown() {
    // Join the worker first: it may be inside jni_startup() holding
    // g_lifecycle_mutex, and it must not race the teardown below.
    stop_deferred_init();
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
    if (g_state.load(std::memory_order_acquire) == kDetached) {
        return;
    }

    // Modules first: on_disable() callbacks (e.g. Fullbright's gamma restore)
    // still need the reflection cache and an attached env. Module state
    // changes at shutdown are NOT persisted — the config file keeps the
    // user's states for the next attach.
    auto& registry = modules::module_registry::instance();
    for (modules::module* m : registry.all()) {
        m->set_enabled(false);
    }
    registry.clear();
    game::game_state::instance().set_vm(nullptr);

    auto& cache = jni::reflection_cache::instance();
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
    hook::engine_shutdown();
    hook::present_shutdown();

    // UI subsystems hold no JVM state, but they must not survive the unload:
    // toasts and animation channels would otherwise reference unmapped code.
    ui::notification_queue::instance().clear();
    ui::animation_controller::instance().clear();
    core::task_queue::instance().clear();
    core::event_bus::clear();

    g_vm = nullptr;
    g_state.store(kDetached, std::memory_order_release);
    WOKE_INFO("jni", "JNI shutdown complete — reflection cache released "
                     "(%zu mapping classes kept for re-attach)",
              g_mappings.class_count());
}

} // namespace woke::lifecycle

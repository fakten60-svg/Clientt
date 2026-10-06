// ============================================================================
//  woke.wtf — src/hook/present_hook.cpp
//  The glXSwapBuffers detour, bare ImGui frame, and overhead measurement.
//
//  No GL is touched by OUR code: the renderer (imgui_impl_opengl3) is attached
//  lazily by woke::hook::backend on the first frame whose GLX context is
//  current; everywhere else NewFrame/Render run on pure CPU so the hook stays
//  testable headlessly. The detour always chains to the original via the
//  MinHook trampoline.
// ============================================================================
#include "hook/present_hook.hpp"

#include <atomic>
#include <chrono>
#include <dlfcn.h>

#include <imgui.h>

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "core/task_queue.hpp"
#include "game/game_state.hpp"
#include "gui/gui.hpp"
#include "hook/hook_engine.hpp"
#include "hook/imgui_backend.hpp"
#include "modules/module.hpp"

namespace woke::hook {

namespace {

// ABI of glXSwapBuffers(Display*, GLXDrawable) without an X11 dependency:
// opaque display pointer + XID (unsigned long on LP64).
using swap_buffers_fn = void (*)(void*, unsigned long);

std::atomic<bool> g_installed{false};
// GUI open state lives in woke::gui (single source of truth).
std::atomic<long long> g_hits{0};
std::atomic<long long> g_suppressed{0};
std::atomic<long long> g_imgui_frames{0};
std::atomic<long long> g_last_ns{0};
std::atomic<long long> g_total_ns{0};

ImGuiContext* g_imgui = nullptr;
void* g_original = nullptr;                       // MinHook trampoline
void* g_libgl_handle = nullptr;                   // kept while hooked
std::chrono::steady_clock::time_point g_last_frame{};

// ---- the detour ------------------------------------------------------------
void swap_buffers_detour(void* dpy, unsigned long drawable) {
    using clock = std::chrono::steady_clock;
    const clock::time_point t0 = clock::now();

    const long long hit = g_hits.fetch_add(1, std::memory_order_relaxed);

    // This thread is the game/frame thread: bind it and run whatever other
    // threads queued up (JNI work is only ever done here).
    auto& tasks = core::task_queue::instance();
    tasks.mark_game_thread();
    tasks.drain();

    // Client-state work (not draw calls): keybind edge-detection + module
    // ticks run regardless of GUI visibility.
    gui::poll_keybind(dpy);
    modules::module_registry::instance().tick_all(game::game_state::instance());

    const clock::time_point now = clock::now();
    float dt = std::chrono::duration<float>(now - g_last_frame).count();
    g_last_frame = now;
    if (dt < 0.001f || dt > 0.1f) {
        dt = 1.0f / 60.0f;                   // clamp first/odd frames
    }
    core::event_bus::emit(core::frame_tick{static_cast<double>(dt), hit});

    bool drew_frame = false;
    if (gui::wants_frames() && g_imgui != nullptr) {
        ImGui::GetIO().DeltaTime = dt;

        // Decides gl/bare on the first frame; false while GLX is reachable but
        // no context is current yet — then this present stays suppressed.
        if (backend::begin_present_frame(dpy, drawable)) {
            ImGui::NewFrame();
            gui::draw();                      // dashboard (or toasts only)
            ImGui::Render();
            backend::end_frame();             // no-op unless the GL renderer is attached
            drew_frame = true;
        } else if (gui::is_open()) {
            drew_frame = true;                // GUI open: not a suppressed frame
        }
    }
    if (drew_frame) {
        g_imgui_frames.fetch_add(1, std::memory_order_relaxed);
    } else {
        // Draw-call suppression: GUI closed (or frame not ready) -> zero ImGui work.
        g_suppressed.fetch_add(1, std::memory_order_relaxed);
    }

    const long long ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - t0).count();
    g_last_ns.store(ns, std::memory_order_relaxed);
    g_total_ns.fetch_add(ns, std::memory_order_relaxed);

    // Always chain — the game must keep presenting.
    reinterpret_cast<swap_buffers_fn>(g_original)(dpy, drawable);
}

void* resolve_swap_buffers() {
    void* fn = ::dlsym(RTLD_DEFAULT, "glXSwapBuffers");
    if (fn != nullptr) {
        return fn;
    }
    // Not in the link map yet — pull libGL in if the system has one.
    if (g_libgl_handle == nullptr) {
        g_libgl_handle = ::dlopen("libGL.so.1", RTLD_LAZY);
    }
    if (g_libgl_handle != nullptr) {
        return ::dlsym(g_libgl_handle, "glXSwapBuffers");
    }
    return nullptr;
}

} // namespace

bool present_startup() {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }

    // ---- 1) ImGui context (renderer + input attach lazily, see backend) -----
    if (g_imgui == nullptr) {
        IMGUI_CHECKVERSION();
        g_imgui = ImGui::CreateContext();
        if (g_imgui == nullptr) {
            WOKE_ERROR("hook", "ImGui::CreateContext failed");
            return false;
        }
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;                            // no imgui.ini side effects
        io.DisplaySize = ImVec2(1920.0f, 1080.0f);           // default until XGetGeometry reports
        io.DeltaTime = 1.0f / 60.0f;
        WOKE_INFO("hook", "ImGui context created (bare frame, renderer attaches on first present)");
        backend::prepare_at_startup();
    }

    // ---- 2) resolve + install the present detour ---------------------------
    void* target = resolve_swap_buffers();
    if (target == nullptr) {
        WOKE_WARN("hook", "glXSwapBuffers not found in this process — present hook skipped");
        return false;
    }
    if (!create_and_enable(target, reinterpret_cast<void*>(&swap_buffers_detour),
                           &g_original, "glXSwapBuffers")) {
        return false;
    }

    g_hits.store(0);
    g_suppressed.store(0);
    g_imgui_frames.store(0);
    g_last_ns.store(0);
    g_total_ns.store(0);
    g_installed.store(true, std::memory_order_release);
    WOKE_INFO("hook", "present hook installed: glXSwapBuffers @ %p (trampoline %p)",
              target, g_original);
    return true;
}

void present_shutdown() {
    const bool was_installed = g_installed.exchange(false, std::memory_order_acq_rel);
    g_original = nullptr;
    gui::set_open(false);
    backend::shutdown();   // must precede DestroyContext: it needs the context alive
    if (g_imgui != nullptr) {
        ImGui::DestroyContext(g_imgui);
        g_imgui = nullptr;
        WOKE_INFO("hook", "ImGui context destroyed");
    }
    if (was_installed) {
        WOKE_INFO("hook", "present hook state released (detour removed by engine shutdown)");
    }
    // g_libgl_handle intentionally kept: dlclose'ing libGL while its code may
    // still be mapped into the game is never safe.
}

bool present_installed() {
    return g_installed.load(std::memory_order_acquire);
}

bool imgui_initialized() {
    return g_imgui != nullptr;
}

const char* present_target_name() {
    return "glXSwapBuffers";
}

void set_gui_open(bool open) {
    gui::set_open(open);   // delegated: gui owns the open state
}

bool gui_open() {
    return gui::is_open();
}

long long present_hit_count() {
    return g_hits.load(std::memory_order_relaxed);
}
long long present_suppressed_count() {
    return g_suppressed.load(std::memory_order_relaxed);
}
long long present_imgui_frame_count() {
    return g_imgui_frames.load(std::memory_order_relaxed);
}
long long present_last_frame_ns() {
    return g_last_ns.load(std::memory_order_relaxed);
}
long long present_total_frame_ns() {
    return g_total_ns.load(std::memory_order_relaxed);
}

} // namespace woke::hook

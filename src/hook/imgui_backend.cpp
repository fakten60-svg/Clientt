// ============================================================================
//  woke.wtf — src/hook/imgui_backend.cpp
//  See imgui_backend.hpp for the mode contract.
//
//  Nothing here uses GL unless the GLX context check says we are inside the
//  game's live frame, and nothing here links libX11/libGL: both are pulled in
//  with dlopen(RTLD_LAZY | RTLD_NOLOAD) + dlsym, mirroring src/gui/gui.cpp.
// ============================================================================
#include "hook/imgui_backend.hpp"

#include <atomic>
#include <cstdlib>
#include <dlfcn.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include "core/logger.hpp"

namespace woke::hook::backend {

namespace {

std::atomic<int> g_mode{static_cast<int>(mode::idle)};
std::atomic<int> g_display_w{1920};
std::atomic<int> g_display_h{1080};
std::atomic<long long> g_input_updates{0};
bool g_atlas_built = false;   // render thread only

// Presents that found GLX reachable but no current context before we give up
// and settle for the CPU-only overlay.
constexpr int kGlAttachRetryLimit = 300;
int g_gl_retries = 0;   // render thread only

// Escape hatch for headless CI and overlay debugging.
bool force_cpu_overlay() {
    const char* env = std::getenv("WOKE_FORCE_CPU_OVERLAY");
    if (env == nullptr || env[0] == '\0' || (env[0] == '0' && env[1] == '\0')) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Lazy X11 / GLX resolution (never hard-linked into libwoke.so)
// ---------------------------------------------------------------------------
// Opaque ABI without an X11/GL dependency: Display* is void*, Window/Drawable
// are XIDs (unsigned long on LP64), Status/Bool are int.
using x_get_geometry_fn = int (*)(void*, unsigned long, unsigned long*, int*, int*,
                                  unsigned int*, unsigned int*, unsigned int*, unsigned int*);
using x_query_pointer_fn = int (*)(void*, unsigned long, unsigned long*, unsigned long*,
                                   int*, int*, int*, int*, unsigned int*);
using glx_get_current_context_fn = void* (*)();

// X11 masks: Button1Mask << 8, Button2Mask << 9, Button3Mask << 10.
constexpr unsigned int kButton1Mask = 1u << 8;
constexpr unsigned int kButton2Mask = 1u << 9;
constexpr unsigned int kButton3Mask = 1u << 10;

struct resolvers {
    x_get_geometry_fn get_geometry = nullptr;
    x_query_pointer_fn query_pointer = nullptr;
    glx_get_current_context_fn glx_get_current_context = nullptr;
    void* x11_handle = nullptr;   // held for the process lifetime (never closed)
    void* gl_handle = nullptr;
};

// Process-wide dlsym first (the game already mapped the library), then
// RTLD_NOLOAD so we never *force* a library the host did not want.
void* load_symbol(const char* symbol, const char* library, void** handle) {
    if (void* sym = ::dlsym(RTLD_DEFAULT, symbol)) {
        return sym;
    }
    void* lib = *handle;
    if (lib == nullptr) {
        lib = ::dlopen(library, RTLD_LAZY | RTLD_NOLOAD);
        if (lib == nullptr) {
            lib = ::dlopen(library, RTLD_LAZY);
        }
        *handle = lib;
    }
    return (lib != nullptr) ? ::dlsym(lib, symbol) : nullptr;
}

// Thread-safe one-shot resolution (function-local static).
const resolvers& resolved() {
    static const resolvers r = [] {
        resolvers out;
        out.get_geometry = reinterpret_cast<x_get_geometry_fn>(
            load_symbol("XGetGeometry", "libX11.so.6", &out.x11_handle));
        out.query_pointer = reinterpret_cast<x_query_pointer_fn>(
            load_symbol("XQueryPointer", "libX11.so.6", &out.x11_handle));
        out.glx_get_current_context = reinterpret_cast<glx_get_current_context_fn>(
            load_symbol("glXGetCurrentContext", "libGL.so.1", &out.gl_handle));
        return out;
    }();
    return r;
}

// ---------------------------------------------------------------------------
//  Frame helpers
// ---------------------------------------------------------------------------
void store_display_size(int w, int h) {
    if (w > 0 && h > 0) {
        g_display_w.store(w, std::memory_order_relaxed);
        g_display_h.store(h, std::memory_order_relaxed);
    }
}

void refresh_display_size(const resolvers& r, void* display, unsigned long drawable) {
    if (display == nullptr || drawable == 0UL || r.get_geometry == nullptr) {
        return;
    }
    unsigned long root = 0;
    int x = 0;
    int y = 0;
    unsigned int w = 0;
    unsigned int h = 0;
    unsigned int border = 0;
    unsigned int depth = 0;
    if (r.get_geometry(display, drawable, &root, &x, &y, &w, &h, &border, &depth) != 0) {
        store_display_size(static_cast<int>(w), static_cast<int>(h));
    }
}

void apply_display_size_to_io() {
    ImGui::GetIO().DisplaySize =
        ImVec2(static_cast<float>(g_display_w.load(std::memory_order_relaxed)),
               static_cast<float>(g_display_h.load(std::memory_order_relaxed)));
}

// Feeds pointer state from the X server. No-op without a real display
// (headless frames keep the last known display size).
void refresh_pointer(const resolvers& r, void* display, unsigned long drawable) {
    if (display == nullptr || drawable == 0UL || r.query_pointer == nullptr) {
        return;
    }
    unsigned long root = 0;
    unsigned long child = 0;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int mask = 0;
    if (r.query_pointer(display, drawable, &root, &child, &root_x, &root_y,
                        &win_x, &win_y, &mask) == 0) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(win_x), static_cast<float>(win_y));
    io.AddMouseButtonEvent(0, (mask & kButton1Mask) != 0);
    io.AddMouseButtonEvent(1, (mask & kButton2Mask) != 0);
    io.AddMouseButtonEvent(2, (mask & kButton3Mask) != 0);
    g_input_updates.fetch_add(1, std::memory_order_relaxed);
}

// Legacy (non-RendererHasTextures) atlas for the CPU-only path. Must only run
// while the mode is `bare` — see the mixing note in the header.
void build_bare_atlas() {
    if (g_atlas_built) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int w = 0;
    int h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);   // builds + preloads glyphs
    io.Fonts->SetTexID(static_cast<ImTextureID>(1)); // CPU-only marker id
    g_atlas_built = true;
    WOKE_INFO("hook", "ImGui font atlas built headlessly: %dx%d (CPU-only, no GL context)", w, h);
}

void enter_bare_mode() {
    g_mode.store(static_cast<int>(mode::bare), std::memory_order_release);
    build_bare_atlas();
    apply_display_size_to_io();
}

} // namespace

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
void prepare_at_startup() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    if (force_cpu_overlay()) {
        enter_bare_mode();
        WOKE_INFO("hook", "WOKE_FORCE_CPU_OVERLAY set — ImGui stays CPU-only (bare mode)");
        return;
    }
    const resolvers& r = resolved();
    if (r.glx_get_current_context == nullptr) {
        // No GLX anywhere in this process: the CPU-only atlas is the only
        // possible path, so build it now (outside any present frame).
        enter_bare_mode();
        WOKE_INFO("hook", "no GLX entry point reachable — ImGui stays CPU-only (bare mode)");
    } else {
        WOKE_INFO("hook", "GLX available — ImGui renderer attaches on the first drawn frame");
    }
}

bool begin_present_frame(void* display, unsigned long drawable) {
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    const resolvers& r = resolved();

    int m = g_mode.load(std::memory_order_acquire);
    if (m == static_cast<int>(mode::idle)) {
        const bool glx_reachable = r.glx_get_current_context != nullptr;
        if (force_cpu_overlay()) {
            enter_bare_mode();
            m = static_cast<int>(mode::bare);
        } else if (glx_reachable && r.glx_get_current_context() != nullptr) {
            g_gl_retries = 0;
            if (ImGui_ImplOpenGL3_Init(nullptr)) {
                g_mode.store(static_cast<int>(mode::gl), std::memory_order_release);
                m = static_cast<int>(mode::gl);
                WOKE_INFO("hook", "ImGui OpenGL3 renderer attached (GLX context current)");
            } else {
                WOKE_WARN("hook", "ImGui OpenGL3 renderer unavailable — falling back to CPU-only");
                enter_bare_mode();
                m = static_cast<int>(mode::bare);
            }
        } else if (glx_reachable && ++g_gl_retries >= kGlAttachRetryLimit) {
            // GLX exists but never became current on this thread: settle for the
            // CPU-only overlay instead of suppressing every frame forever.
            WOKE_WARN("hook", "GLX context never became current in %d presents — CPU-only overlay",
                      kGlAttachRetryLimit);
            g_gl_retries = 0;
            enter_bare_mode();
            m = static_cast<int>(mode::bare);
        }
        // GLX reachable but nothing current yet: stay idle and retry next
        // present instead of feeding ImGui an unbuilt atlas.
    }

    if (m == static_cast<int>(mode::gl)) {
        refresh_display_size(r, display, drawable);
        apply_display_size_to_io();
        refresh_pointer(r, display, drawable);
        ImGui_ImplOpenGL3_NewFrame();
        return true;
    }
    if (m == static_cast<int>(mode::bare)) {
        refresh_display_size(r, display, drawable);
        apply_display_size_to_io();
        refresh_pointer(r, display, drawable);
        return true;
    }
    return false;
}

bool begin_headless_frame() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    if (g_mode.load(std::memory_order_acquire) == static_cast<int>(mode::gl)) {
        return false;   // rendered through GL; a headless caller must not touch it
    }
    if (g_mode.load(std::memory_order_relaxed) != static_cast<int>(mode::bare)) {
        enter_bare_mode();
    }
    return true;
}

void end_frame() {
    if (g_mode.load(std::memory_order_acquire) != static_cast<int>(mode::gl)) {
        return;
    }
    if (ImDrawData* draw_data = ImGui::GetDrawData()) {
        ImGui_ImplOpenGL3_RenderDrawData(draw_data);
    }
}

void shutdown() {
    const int m = g_mode.exchange(static_cast<int>(mode::idle), std::memory_order_acq_rel);
    if (m == static_cast<int>(mode::gl)) {
        const resolvers& r = resolved();
        if (r.glx_get_current_context != nullptr && r.glx_get_current_context() != nullptr) {
            ImGui_ImplOpenGL3_Shutdown();
            WOKE_INFO("hook", "ImGui OpenGL3 renderer detached");
        } else {
            // Shutting down from a thread without a current context (JNI_OnUnload
            // / dlclose). The GL objects die with the game's context; releasing
            // them here would call GL without a context.
            WOKE_WARN("hook", "no GLX context current at shutdown — GL resources left to the host");
        }
    }
    g_atlas_built = false;
    g_gl_retries = 0;
    g_input_updates.store(0, std::memory_order_relaxed);
    g_display_w.store(1920, std::memory_order_relaxed);
    g_display_h.store(1080, std::memory_order_relaxed);
}

mode current_mode() {
    return static_cast<mode>(g_mode.load(std::memory_order_acquire));
}

bool gl_attached() {
    return g_mode.load(std::memory_order_acquire) == static_cast<int>(mode::gl);
}

int display_width() {
    return g_display_w.load(std::memory_order_relaxed);
}

int display_height() {
    return g_display_h.load(std::memory_order_relaxed);
}

long long input_update_count() {
    return g_input_updates.load(std::memory_order_relaxed);
}

} // namespace woke::hook::backend

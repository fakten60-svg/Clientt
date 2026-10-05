// ============================================================================
//  woke.wtf — src/gui/gui.cpp
//  Click-gui implementation. X11 is resolved lazily via dlsym (XQueryKeymap)
//  so libwoke never hard-links libX11 — in the real game the symbol comes
//  from the host process; in tests dpy is null and the poll is skipped.
// ============================================================================
#include "gui/gui.hpp"

#include <atomic>
#include <dlfcn.h>
#include <string>

#include <imgui.h>

#include "core/config.hpp"
#include "core/logger.hpp"
#include "game/game_state.hpp"
#include "hook/present_hook.hpp"
#include "modules/module.hpp"

namespace woke::gui {

namespace {

std::atomic<bool> g_open{false};
std::atomic<long long> g_draws{0};
bool g_key_was_down = false;   // edge detection (render thread only)

// XQueryKeymap(Display*, char[32]) — Status.
using xquery_keymap_fn = int (*)(void*, char*);

xquery_keymap_fn resolve_xquery_keymap() {
    static xquery_keymap_fn fn = [] {
        void* sym = ::dlsym(RTLD_DEFAULT, "XQueryKeymap");
        if (sym == nullptr) {
            void* x11 = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_NOLOAD);
            if (x11 == nullptr) {
                x11 = ::dlopen("libX11.so.6", RTLD_LAZY);
            }
            if (x11 != nullptr) {
                sym = ::dlsym(x11, "XQueryKeymap");
            }
        }
        return reinterpret_cast<xquery_keymap_fn>(sym);
    }();
    return fn;
}

} // namespace

void set_open(bool open) {
    const bool was = g_open.exchange(open, std::memory_order_acq_rel);
    if (was != open) {
        WOKE_INFO("gui", "click-gui %s", open ? "opened" : "closed");
    }
}

bool is_open() {
    return g_open.load(std::memory_order_relaxed);
}

void toggle() {
    set_open(!is_open());
}

void poll_keybind(void* display) {
    if (display == nullptr) {
        return;
    }
    xquery_keymap_fn query = resolve_xquery_keymap();
    if (query == nullptr) {
        return;
    }
    char keys[32] = {};
    if (query(display, keys) != 0) {
        return;
    }
    const int code = config::keybind();
    const bool down = ((keys[code >> 3] >> (code & 7)) & 1) != 0;
    if (down && !g_key_was_down) {
        toggle();
    }
    g_key_was_down = down;
}

draw_stats draw() {
    draw_stats stats;
    if (ImGui::GetCurrentContext() == nullptr) {
        return stats;
    }

    ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("woke.wtf", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted("Minecraft 1.21.11 (Fabric) — client-state QoL");
        ImGui::TextDisabled("private/local use only - no packet generation");
        ImGui::Separator();

        std::string last_category;
        for (modules::module* m : modules::module_registry::instance().all()) {
            if (m->category() != last_category) {
                last_category = m->category();
                ImGui::SeparatorText(last_category.c_str());
            }
            bool on = m->enabled();
            if (ImGui::Checkbox(m->name().c_str(), &on)) {
                m->set_enabled(on);
                config::save();   // persist immediately
                ++stats.toggles;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", m->description().c_str());
            }
            ++stats.modules_shown;
        }

        ImGui::Separator();
        ImGui::Text("presents %lld - suppressed %lld - overhead avg %.1f us",
                    woke::hook::present_hit_count(), woke::hook::present_suppressed_count(),
                    woke::hook::present_hit_count() > 0
                        ? static_cast<double>(woke::hook::present_total_frame_ns()) /
                              static_cast<double>(woke::hook::present_hit_count()) / 1000.0
                        : 0.0);
        ImGui::TextDisabled("config: %s (keybind keycode %d)", config::path(), config::keybind());
    }
    ImGui::End();

    g_draws.fetch_add(1, std::memory_order_relaxed);
    return stats;
}

draw_stats render_frame() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.DeltaTime <= 0.0f) {
        io.DeltaTime = 1.0f / 60.0f;
    }
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
        io.DisplaySize = ImVec2(1920.0f, 1080.0f);
    }
    ImGui::NewFrame();
    draw_stats stats = draw();
    ImGui::Render();
    return stats;
}

long long draw_count() {
    return g_draws.load(std::memory_order_relaxed);
}

} // namespace woke::gui

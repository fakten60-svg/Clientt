// ============================================================================
//  woke.wtf — src/gui/gui.cpp
//  macOS-style ClickGUI dashboard — slim orchestrator.
//
//  Drawing is pure ImGui CPU work: no GL call is made here, so the dashboard
//  renders identically in the bare (headless/test) and gl (in-game) renderer
//  modes. The implementation is split by region (~200 lines per file, per the
//  engineering standards):
//
//    internal.cpp     shared state + one-time widget configuration
//    keybind.cpp      X11 keybind polling + the zero-alloc search matcher
//    sidebar.cpp      navigation sidebar + the five GENERAL pages
//    card.cpp         module card widget + list/grid card layout
//    setting_row.cpp  the generic BaseSetting<T> editor row
//    gui.cpp          window chrome, page orchestration, public API (this file)
//
//  Open state lives here (single source of truth). The present detour asks
//  wants_frames() to decide between drawing and the draw-call-suppressed fast
//  path: that is true while the window is open, while its close animation is
//  still running, or while a toast is on screen. In the steady closed state
//  it is false, so the detour performs zero ImGui work.
// ============================================================================
#include "gui/gui.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "gui/internal.hpp"
#include "hook/imgui_backend.hpp"
#include "modules/module.hpp"
#include "ui/animation.hpp"
#include "ui/notifications.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"

namespace woke::gui {

using detail::g_close_light;
using detail::g_content_fade;
using detail::g_general_entries;
using detail::g_grid;
using detail::g_min_light;
using detail::g_page;
using detail::g_search;
using detail::g_search_field;
using detail::g_view_toggle;
using detail::g_window_pop;
using detail::g_zoom_light;
using detail::kGeneralCount;
using detail::kGeneralSub;

// ---- public API: visibility + draw-call suppression -------------------------

void set_open(bool open) {
    const bool was = detail::g_open.exchange(open, std::memory_order_acq_rel);
    if (was != open) {
        WOKE_INFO("gui", "click-gui %s", open ? "opened" : "closed");
        core::event_bus::emit(core::gui_visibility_changed{open});
    }
}

bool is_open() {
    return detail::g_open.load(std::memory_order_relaxed);
}

void toggle() {
    set_open(!is_open());
}

bool wants_frames() {
    if (is_open()) {
        return true;
    }
    if (ui::notification_queue::instance().active() > 0) {
        return true;
    }
    return !g_window_pop.settled(0.005f) || !g_content_fade.settled(0.005f);
}

// ---- frame build ------------------------------------------------------------

draw_stats draw() {
    draw_stats stats;
    if (ImGui::GetCurrentContext() == nullptr) {
        return stats;
    }
    ui::theme::ensure_initialized();
    detail::ensure_widgets();
    detail::install_notification_listener();

    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    if (!(dt > 0.0f) || dt > 0.25f) {
        dt = 1.0f / 60.0f;
    }
    // One central tick advances the window pop, the fade and every widget.
    ui::animation_controller::instance().tick(dt);
    g_window_pop.set_target(is_open() ? 1.0f : 0.0f);
    g_content_fade.set_target(is_open() ? 1.0f : 0.0f);

    const float pop = utils::clamp01(g_window_pop.value());
    const float alpha = utils::clamp01(g_content_fade.value());
    const bool window_visible = is_open() || pop > 0.02f;

    if (window_visible) {
        const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        const float slide = (1.0f - utils::ease_out_cubic(pop)) * 18.0f;
        ImGui::SetNextWindowSize(ImVec2(ui::theme::window_width, ui::theme::window_height),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(center.x, center.y + slide), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##woke_dashboard", nullptr, flags);
        ImGui::PopStyleVar();

        const ImVec2 wpos = ImGui::GetWindowPos();
        const ImVec2 wsize = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 hit_bg =
            utils::with_alpha(ui::theme::window_bg, ui::theme::window_alpha * pop);

        // Shadow behind the frame (background list: never over the content).
        utils::render::shadow(ImGui::GetBackgroundDrawList(), wpos,
                              ImVec2(wpos.x + wsize.x, wpos.y + wsize.y),
                              ui::theme::window_rounding, 26.0f, 0.35f * pop);
        // Translucent charcoal glass + the soft outline from the spec.
        utils::render::rounded_rect(dl, wpos, ImVec2(wpos.x + wsize.x, wpos.y + wsize.y),
                                    ui::theme::window_rounding, hit_bg);
        utils::render::rounded_border(dl, ImVec2(wpos.x + 0.5f, wpos.y + 0.5f),
                                      ImVec2(wpos.x + wsize.x - 0.5f, wpos.y + wsize.y - 0.5f),
                                      ui::theme::window_rounding,
                                      utils::with_alpha(ui::theme::window_border, pop), 1.0f);

        // ---- title bar: traffic lights + centered title ----------------------
        const float light_y = wpos.y + ui::theme::titlebar_h * 0.5f;
        g_close_light.set_center(ImVec2(wpos.x + 20.0f, light_y), ui::theme::traffic_r);
        g_min_light.set_center(ImVec2(wpos.x + 42.0f, light_y), ui::theme::traffic_r);
        g_zoom_light.set_center(ImVec2(wpos.x + 64.0f, light_y), ui::theme::traffic_r);
        g_close_light.set_alpha(pop);
        g_min_light.set_alpha(pop);
        g_zoom_light.set_alpha(pop);

        if (g_close_light.handle_input()) {
            set_open(false);
        }
        if (g_min_light.handle_input()) {
            ui::notification_queue::instance().push(
                ui::toast_kind::info, "Minimize",
                "The dashboard is not a windowed app — press the keybind to reopen it");
        }
        if (g_zoom_light.handle_input()) {
            ui::notification_queue::instance().push(ui::toast_kind::info, "Expand",
                                                    "Layout is fixed to the dashboard size");
        }
        g_close_light.render(dl);
        g_min_light.render(dl);
        g_zoom_light.render(dl);

        const char* title = "woke.wtf - Utility Client";
        const float title_w = utils::render::text_width(title);
        const ImVec2 title_pos(wpos.x + (wsize.x - title_w) * 0.5f,
                               wpos.y + ui::theme::titlebar_h * 0.5f - 8.0f);
        dl->AddText(title_pos, utils::with_alpha(ui::theme::text_muted, alpha), title);
        dl->AddLine(ImVec2(wpos.x, wpos.y + ui::theme::titlebar_h),
                    ImVec2(wpos.x + wsize.x, wpos.y + ui::theme::titlebar_h),
                    utils::with_alpha(ui::theme::divider, alpha), 1.0f);

        detail::draw_sidebar(dl, wpos, wsize, alpha, stats);

        // ---- content header --------------------------------------------------
        const float cx0 = wpos.x + ui::theme::sidebar_w + ui::theme::content_pad;
        const float cx1 = wpos.x + wsize.x - ui::theme::content_pad;
        float y = wpos.y + ui::theme::titlebar_h + 14.0f;

        bool category_page = false;
        for (const char* c : modules::kCategories) {
            if (std::strcmp(c, g_page) == 0) {
                category_page = true;
                break;
            }
        }

        dl->AddText(ImVec2(cx0, y), utils::with_alpha(ui::theme::text_primary, alpha), g_page);
        char sub[96];
        if (category_page) {
            const int total = modules::category_module_count(g_page);
            const int on = modules::category_enabled_count(g_page);
            std::snprintf(sub, sizeof sub, "%d modules . %d enabled", total, on);
        } else {
            std::size_t idx = 0;
            for (std::size_t i = 0; i < kGeneralCount; ++i) {
                if (std::strcmp(detail::kGeneralPages[i], g_page) == 0) {
                    idx = i;
                }
            }
            std::snprintf(sub, sizeof sub, "%s", kGeneralSub[idx]);
        }
        dl->AddText(ImVec2(cx0 + utils::render::text_width(g_page) + 10.0f, y + 3.0f),
                    utils::with_alpha(ui::theme::text_dim, alpha), sub);

        if (category_page) {
            g_view_toggle.set_rect(ImVec2(cx1 - 56.0f, y - 2.0f), 24.0f);
            g_view_toggle.set_alpha(alpha);
            (void)g_view_toggle.handle_input();
            g_grid = g_view_toggle.grid();
            g_view_toggle.render(dl);

            g_search_field.set_rect(ImVec2(cx1 - 56.0f - 12.0f - 240.0f, y - 2.0f), 240.0f);
            g_search_field.set_alpha(alpha);
            (void)g_search_field.handle_input();
            if (std::strcmp(g_search_field.text(), g_search) != 0) {
                std::snprintf(g_search, sizeof g_search, "%s", g_search_field.text());
            }
            g_search_field.render(dl);
        }

        y += 44.0f;

        if (category_page) {
            detail::draw_module_cards(dl, cx0, cx1, y, wpos, wsize, dt, alpha, stats);
        } else {
            detail::draw_general_page(g_page, ImVec2(cx0, y), cx1 - cx0, alpha, stats);
        }

        // Module draw hooks run only while the dashboard is actually open.
        if (is_open()) {
            modules::module_registry::instance().render_all();
        }

        ImGui::End();
        stats.drew = true;
    }

    // Toasts are drawn even when the dashboard is closed.
    stats.notifications =
        static_cast<int>(ui::notification_queue::instance().render(io.DisplaySize.x, dt));

    if (stats.drew || stats.notifications > 0) {
        detail::g_draws.fetch_add(1, std::memory_order_relaxed);
    }
    return stats;
}

draw_stats render_frame() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }
    if (!woke::hook::backend::begin_headless_frame()) {
        return {};   // client renders through GL — no headless frame for this context
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
    return detail::g_draws.load(std::memory_order_relaxed);
}

// ---- dashboard controls (also exported through the woke_* C API) ------------

bool select_page(const char* page) {
    if (page == nullptr || page[0] == '\0') {
        return false;
    }
    for (const char* c : modules::kCategories) {
        if (std::strcmp(c, page) == 0) {
            std::snprintf(g_page, sizeof detail::g_page, "%s", page);
            return true;
        }
    }
    for (std::size_t i = 0; i < kGeneralCount; ++i) {
        if (std::strcmp(detail::kGeneralPages[i], page) == 0) {
            std::snprintf(g_page, sizeof detail::g_page, "%s", page);
            return true;
        }
    }
    return false;
}

const char* current_page() {
    return g_page;
}

void set_search(const char* text) {
    std::snprintf(g_search, sizeof detail::g_search, "%s", (text != nullptr) ? text : "");
    g_search_field.set_text(g_search);
}

const char* search_query() {
    return g_search;
}

void set_grid_view(bool grid) {
    g_grid = grid;
    g_view_toggle.set_grid(grid);
}

bool grid_view() {
    return g_grid;
}

void set_expanded(const char* module_name) {
    std::snprintf(detail::g_expanded, sizeof detail::g_expanded, "%s",
                  (module_name != nullptr) ? module_name : "");
}

const char* expanded() {
    return detail::g_expanded;
}

} // namespace woke::gui

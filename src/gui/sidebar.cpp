// ============================================================================
//  woke.wtf — src/gui/sidebar.cpp
//  Left navigation sidebar (logo, MODULES, GENERAL) and the five GENERAL
//  pages: Settings, Theme, Configs, Socials, Keybinds.
//
//  Sidebar rows and GENERAL pages compose the same ui:: components as the
//  module cards (sidebar_entry widgets with animated selection), so no render
//  code is duplicated between regions.
// ============================================================================
#include "gui/internal.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/config.hpp"
#include "core/logger.hpp"
#include "modules/module.hpp"
#include "ui/notifications.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"

#ifndef WOKE_VERSION
#define WOKE_VERSION "0.1.0-dev"
#endif

namespace woke::gui {

namespace detail {

void draw_sidebar(ImDrawList* dl, ImVec2 wpos, ImVec2 wsize, float alpha, draw_stats& stats) {
    const ImVec2 min(wpos.x, wpos.y + ui::theme::titlebar_h);
    const ImVec2 max(wpos.x + ui::theme::sidebar_w, wpos.y + wsize.y);
    dl->AddRectFilled(min, max, utils::with_alpha(ui::theme::sidebar_bg, alpha),
                      ui::theme::window_rounding, ImDrawFlags_RoundCornersBottomLeft);
    dl->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, max.y),
                utils::with_alpha(ui::theme::divider, alpha), 1.0f);

    // Logo + version metadata.
    dl->AddText(ImVec2(min.x + 16.0f, min.y + 14.0f),
                utils::with_alpha(ui::theme::text_primary, alpha), "woke.wtf");
    dl->AddText(ImVec2(min.x + 16.0f, min.y + 30.0f),
                utils::with_alpha(ui::theme::text_dim, alpha), "v" WOKE_VERSION " - native client");

    float y = min.y + 58.0f;
    const float row_h = 30.0f;

    auto draw_section = [&](const char* caption) {
        if (caption != nullptr) {
            dl->AddText(ImVec2(min.x + 16.0f, y + 6.0f),
                        utils::with_alpha(ui::theme::text_dim, alpha), caption);
            y += 24.0f;
        }
    };

    // MODULES: one row per spec category, badge = module count.
    for (std::size_t i = 0; i < modules::kCategoryCount; ++i) {
        draw_section((i == 0) ? "MODULES" : nullptr);
        ui::sidebar_entry& entry = g_category_entries[i];
        entry.configure(modules::kCategories[i], nullptr,
                        modules::category_module_count(modules::kCategories[i]));
        entry.set_rect(ImVec2(min.x + 8.0f, y), ImVec2(ui::theme::sidebar_w - 16.0f, row_h));
        entry.set_selected(std::strcmp(g_page, modules::kCategories[i]) == 0);
        entry.set_alpha(alpha);
        const bool clicked = entry.handle_input();
        entry.render(dl);
        if (clicked) {
            std::snprintf(g_page, sizeof g_page, "%s", modules::kCategories[i]);
        }
        y += row_h + 2.0f;
        ++stats.categories_shown;
    }

    y += 8.0f;
    for (std::size_t i = 0; i < kGeneralCount; ++i) {
        draw_section((i == 0) ? "GENERAL" : nullptr);
        ui::sidebar_entry& entry = g_general_entries[i];
        entry.set_rect(ImVec2(min.x + 8.0f, y), ImVec2(ui::theme::sidebar_w - 16.0f, row_h));
        entry.set_selected(std::strcmp(g_page, kGeneralPages[i]) == 0);
        entry.set_alpha(alpha);
        const bool clicked = entry.handle_input();
        entry.render(dl);
        if (clicked) {
            std::snprintf(g_page, sizeof g_page, "%s", kGeneralPages[i]);
        }
        y += row_h + 2.0f;
    }
}

void draw_general_page(const char* page, ImVec2 min, float width, float alpha,
                       draw_stats& stats) {
    (void)stats;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = min.y;

    auto heading = [&](const char* text) {
        dl->AddText(ImVec2(min.x, y), utils::with_alpha(ui::theme::text_primary, alpha), text);
        y += 24.0f;
    };
    auto note = [&](const char* text) {
        dl->AddText(ImVec2(min.x, y), utils::with_alpha(ui::theme::text_muted, alpha), text);
        y += 20.0f;
    };

    if (std::strcmp(page, "Settings") == 0) {
        heading("Client settings");
        note("Settings are edited from a module card's chevron; the rows below");
        note("control the interface itself.");
        y += 6.0f;
        ImGui::SetCursorScreenPos(ImVec2(min.x, y));
        ImGui::SetNextItemWidth(width * 0.4f);
        int kb = config::keybind();
        if (ImGui::SliderInt("##gui_keybind", &kb, 0, 255, "GUI keycode %d")) {
            config::set_keybind(kb);
        }
        y += 34.0f;
        ImGui::SetCursorScreenPos(ImVec2(min.x, y));
        if (ImGui::Button("Reset all module settings", ImVec2(240.0f, 26.0f))) {
            const int n = config::reset_settings(nullptr);
            ui::notification_queue::instance().push(ui::toast_kind::info, "Settings reset",
                                                    "All module settings restored to defaults");
            WOKE_INFO("ui", "reset %d module settings from the dashboard", n);
        }
    } else if (std::strcmp(page, "Theme") == 0) {
        heading("Theme");
        note("Every color below is a token in ui/theme.hpp — the dashboard reads");
        note("them for cards, badges, toggles and notifications.");
        y += 6.0f;
        struct swatch {
            const char* name;
            ImU32 color;
        };
        const swatch swatches[] = {
            {"window bg", ui::theme::window_bg},     {"border", ui::theme::window_border},
            {"card", ui::theme::card_bg},            {"accent", ui::theme::accent},
            {"accent cyan", ui::theme::accent_cyan}, {"text muted", ui::theme::text_muted},
            {"traffic close", ui::theme::traffic_close},
            {"traffic min", ui::theme::traffic_min},
            {"traffic zoom", ui::theme::traffic_zoom},
        };
        float x = min.x;
        for (const swatch& s : swatches) {
            utils::render::rounded_rect(dl, ImVec2(x, y), ImVec2(x + 96.0f, y + 44.0f), 8.0f,
                                        utils::with_alpha(s.color, alpha));
            dl->AddText(ImVec2(x + 4.0f, y + 48.0f),
                        utils::with_alpha(ui::theme::text_muted, alpha), s.name);
            x += 106.0f;
            if (x > min.x + width - 100.0f) {
                x = min.x;
                y += 74.0f;
            }
        }
        y += 80.0f;
        note("Rounding: window 14 px, frames 8 px. Motion: springs and eased");
        note("interpolation driven by the frame delta time.");
    } else if (std::strcmp(page, "Configs") == 0) {
        heading("Configs");
        note(config::path());
        note("Every toggle and setting is written immediately.");
        y += 6.0f;
        ImGui::SetCursorScreenPos(ImVec2(min.x, y));
        if (ImGui::Button("Reload from disk", ImVec2(160.0f, 26.0f))) {
            const bool ok = config::load();
            ui::notification_queue::instance().push(
                ok ? ui::toast_kind::success : ui::toast_kind::warning, "Config reload",
                ok ? "Configuration reloaded" : "No config file found — defaults kept");
        }
        ImGui::SameLine();
        if (ImGui::Button("Save now", ImVec2(120.0f, 26.0f))) {
            const bool ok = config::save();
            ui::notification_queue::instance().push(
                ok ? ui::toast_kind::success : ui::toast_kind::error, "Config save",
                ok ? "Configuration written" : "Write failed — see the session log");
        }
    } else if (std::strcmp(page, "Socials") == 0) {
        heading("Socials");
        note("No community endpoints are configured for this build.");
        note("Add them to ui/theme.cpp-style constants before shipping a release.");
    } else if (std::strcmp(page, "Keybinds") == 0) {
        heading("Keybinds");
        note("Keycodes are X11 keycodes (62 = Right Shift). 0 clears a bind.");
        y += 4.0f;
        for (modules::module* m : modules::module_registry::instance().all()) {
            if (y > min.y + 360.0f) {
                break;
            }
            ImGui::PushID(m);
            ImGui::SetCursorScreenPos(ImVec2(min.x, y));
            ImGui::TextUnformatted(m->name().c_str());
            ImGui::SameLine(width * 0.3f);
            int code = config::module_keybind(m->name().c_str());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::SliderInt("##bind", &code, 0, 255, "%d")) {
                config::set_module_keybind(m->name().c_str(), code);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                config::set_module_keybind(m->name().c_str(), 0);
            }
            ImGui::PopID();
            y += 30.0f;
        }
    }
}

} // namespace detail

} // namespace woke::gui

// ============================================================================
//  woke.wtf — src/gui/gui.cpp
//  macOS-style ClickGUI dashboard.
//
//  The window is chrome-less (ImGui only provides the frame + input plumbing);
//  the title bar, traffic lights, sidebar, cards and toggles are drawn with
//  RenderUtils on the window draw list, and every transition runs through the
//  animation engine using the frame's delta time.
//
//  Drawing is pure ImGui CPU work: no GL call is made here, so the dashboard
//  renders identically in the bare (headless/test) and gl (in-game) renderer
//  modes. X11 is touched only for keybind polling, through dlsym, so libwoke
//  never hard-links libX11.
// ============================================================================
#include "gui/gui.hpp"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>

#include "core/event_bus.hpp"

#include <imgui.h>

#include "core/config.hpp"
#include "core/logger.hpp"
#include "game/game_state.hpp"
#include "hook/imgui_backend.hpp"
#include "hook/present_hook.hpp"
#include "modules/module.hpp"
#include "ui/animation.hpp"
#include "ui/component.hpp"
#include "ui/notifications.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"

#ifndef WOKE_VERSION
#define WOKE_VERSION "0.1.0-dev"
#endif

namespace woke::gui {

namespace {

// ---- state -----------------------------------------------------------------

std::atomic<bool> g_open{false};
std::atomic<long long> g_draws{0};
bool g_key_was_down = false;              // GUI keybind edge detection
bool g_module_key_down[64] = {};          // per-module keybind edge detection
constexpr std::size_t kMaxCards = 64;

char g_page[24] = "Visual";
char g_search[64] = "";
char g_expanded[32] = "";
bool g_grid = false;
bool g_widgets_ready = false;

// Window open/close motion: a spring for the pop, an eased fade for content.
ui::spring_value g_window_pop{0.0f, 190.0f, 24.0f};
ui::animated_value g_content_fade{0.0f, 0.06f};
ui::sidebar_entry g_category_entries[modules::kCategoryCount];
ui::sidebar_entry g_general_entries[5];
ui::search_field g_search_field;
ui::view_toggle g_view_toggle;
ui::traffic_light g_close_light{ui::traffic_light::kind::close};
ui::traffic_light g_min_light{ui::traffic_light::kind::minimize};
ui::traffic_light g_zoom_light{ui::traffic_light::kind::zoom};

ui::toggle_switch g_toggles[kMaxCards];
ui::chevron g_chevrons[kMaxCards];
ui::animated_value g_card_hover[kMaxCards];
int g_card_hover_bound = -1;               // how many hover channels are registered
bool g_toggle_state[kMaxCards] = {};       // last state pushed into the widget

constexpr const char* kGeneralPages[] = {"Settings", "Theme", "Configs", "Socials", "Keybinds"};
constexpr const char* kGeneralSub[] = {
    "Client preferences",
    "Palette and motion",
    "Load, save and reset",
    "Community links",
    "Module and GUI keybinds",
};
constexpr std::size_t kGeneralCount = sizeof(kGeneralPages) / sizeof(kGeneralPages[0]);

// ---- X11 keybind resolution ------------------------------------------------

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

bool key_down(const char* keys, int code) {
    if (code <= 0 || code > 255) {
        return false;
    }
    return ((keys[code >> 3] >> (code & 7)) & 1) != 0;
}

// ---- widget setup ----------------------------------------------------------

void ensure_widgets() {
    if (g_widgets_ready) {
        return;
    }
    g_widgets_ready = true;

    auto& controller = ui::animation_controller::instance();
    controller.add(g_window_pop);
    controller.add(g_content_fade);

    for (std::size_t i = 0; i < modules::kCategoryCount; ++i) {
        const char* category = modules::kCategories[i];
        g_category_entries[i].configure(category, (i == 0) ? "MODULES" : nullptr,
                                         modules::category_module_count(category));
    }
    for (std::size_t i = 0; i < kGeneralCount; ++i) {
        g_general_entries[i].configure(kGeneralPages[i], (i == 0) ? "GENERAL" : nullptr, -1);
    }
}

void bind_card_channels(std::size_t count) {
    if (static_cast<int>(count) <= g_card_hover_bound) {
        return;
    }
    for (std::size_t i = static_cast<std::size_t>(g_card_hover_bound < 0 ? 0 : g_card_hover_bound);
         i < count && i < kMaxCards; ++i) {
        ui::animation_controller::instance().add(g_card_hover[i]);
    }
    g_card_hover_bound = static_cast<int>(count);
    WOKE_DEBUG("ui", "dashboard bound %zu animation channels", count);
}

// ---- generic BaseSetting editor -------------------------------------------

// Draws one setting row (label left, editor right) at an absolute position.
// Returns true when the user changed the value this frame.
bool setting_row(core::base_setting& s, ImVec2 pos, float width) {
    ImGui::PushID(&s);
    ImGui::SetCursorScreenPos(pos);
    ImGui::TextUnformatted(s.name().c_str());
    if (ImGui::IsItemHovered() && !s.description().empty()) {
        ImGui::SetTooltip("%s", s.description().c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x + width * 0.5f, pos.y));
    ImGui::SetNextItemWidth(width * 0.5f);

    bool changed = false;
    switch (s.type()) {
        case core::setting_type::boolean: {
            core::setting_value v = s.to_value();
            bool on = v.boolean;
            if (ImGui::Checkbox("##bool", &on)) {
                v.boolean = on;
                s.from_value(v);
                changed = true;
            }
            break;
        }
        case core::setting_type::integer:
        case core::setting_type::color: {
            core::setting_value v = s.to_value();
            int iv = static_cast<int>(v.integer);
            if (s.type() == core::setting_type::color) {
                float col[4] = {static_cast<float>((iv >> 16) & 0xFF) / 255.0f,
                                static_cast<float>((iv >> 8) & 0xFF) / 255.0f,
                                static_cast<float>(iv & 0xFF) / 255.0f, 1.0f};
                if (ImGui::ColorEdit4("##color", col, ImGuiColorEditFlags_NoInputs)) {
                    const long long packed =
                        (static_cast<long long>(col[0] * 255.0f + 0.5f) << 16) |
                        (static_cast<long long>(col[1] * 255.0f + 0.5f) << 8) |
                        static_cast<long long>(col[2] * 255.0f + 0.5f);
                    v.integer = packed;
                    s.from_value(v);
                    changed = true;
                }
            } else if (ImGui::SliderInt("##int", &iv, -255, 255)) {
                v.integer = iv;
                s.from_value(v);
                changed = true;
            }
            break;
        }
        case core::setting_type::decimal: {
            core::setting_value v = s.to_value();
            float fv = static_cast<float>(v.decimal);
            if (ImGui::SliderFloat("##float", &fv, 0.0f, 24.0f, "%.2f")) {
                v.decimal = static_cast<double>(fv);
                s.from_value(v);
                changed = true;
            }
            break;
        }
        case core::setting_type::text: {
            core::setting_value v = s.to_value();
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s", v.text.c_str());
            if (ImGui::InputText("##text", buf, sizeof buf)) {
                v.text = buf;
                s.from_value(v);
                changed = true;
            }
            break;
        }
    }
    if (changed) {
        s.clear_dirty();
    }
    ImGui::PopID();
    return changed;
}

// ---- card ------------------------------------------------------------------

struct card_result {
    bool toggled = false;
    bool expanded_changed = false;
};

// Draws one module card at `min`. `index` indexes the persistent widget arrays.
card_result module_card(modules::module& m, ImVec2 min, ImVec2 size, int index, float dt,
                        float alpha) {
    (void)dt;
    card_result result;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size.x, min.y + size.y);

    ImGui::PushID(index);
    ImGui::SetCursorScreenPos(min);
    const bool card_clicked = ImGui::InvisibleButton("##card", size);
    const bool card_hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ui::animated_value& hover = g_card_hover[static_cast<std::size_t>(index)];
    hover.set_target(card_hovered ? 1.0f : 0.0f);
    const float h = utils::clamp01(hover.value());

    const ImU32 bg = utils::mix(ui::theme::card_bg, ui::theme::card_bg_hover, h);
    utils::render::shadow(dl, min, max, ui::theme::frame_rounding, 6.0f * h, 0.30f);
    utils::render::rounded_rect(dl, min, max, ui::theme::frame_rounding,
                                utils::with_alpha(bg, alpha));
    if (h > 0.01f) {
        utils::render::rounded_border(dl, min, max, ui::theme::frame_rounding,
                                      utils::with_alpha(ui::theme::accent_soft, alpha * h * 0.5f),
                                      1.0f);
    }

    // Left: name + description + keybind badge.
    const float text_x = min.x + 16.0f;
    const float right_reserved = 130.0f;
    utils::render::text_ellipsized(dl, ImVec2(text_x, min.y + 12.0f),
                                   utils::with_alpha(ui::theme::text_primary, alpha),
                                   m.name().c_str(), size.x - right_reserved);
    utils::render::text_ellipsized(dl, ImVec2(text_x, min.y + 32.0f),
                                   utils::with_alpha(ui::theme::text_muted, alpha),
                                   m.description().c_str(), size.x - right_reserved);

    const int key = config::module_keybind(m.name().c_str());
    if (key > 0) {
        char badge[24];
        std::snprintf(badge, sizeof badge, "[KEY: %d]", key);
        const float w = utils::render::text_width(badge) + 14.0f;
        const ImVec2 bmin(max.x - right_reserved - w + 8.0f, min.y + 12.0f);
        const ImVec2 bmax(bmin.x + w, bmin.y + 18.0f);
        utils::render::rounded_rect(dl, bmin, bmax, 9.0f,
                                    utils::with_alpha(ui::theme::badge_bg, alpha));
        dl->AddText(ImVec2(bmin.x + 7.0f, bmin.y + 1.0f),
                    utils::with_alpha(ui::theme::accent_soft, alpha), badge);
    }

    // Right: the Apple-style pill toggle.
    ui::toggle_switch& toggle = g_toggles[static_cast<std::size_t>(index)];
    if (g_toggle_state[static_cast<std::size_t>(index)] != m.enabled()) {
        toggle.set_on(m.enabled());   // external change (config load, API) - snap
        g_toggle_state[static_cast<std::size_t>(index)] = m.enabled();
    }
    const ImVec2 pill_size(ui::theme::pill_w, ui::theme::pill_h);
    toggle.set_rect(ImVec2(max.x - pill_size.x - 44.0f,
                           min.y + (size.y - pill_size.y) * 0.5f),
                    pill_size);
    toggle.set_alpha(alpha);

    // Chevron (card expander) sits just right of the toggle.
    ui::chevron& chevron = g_chevrons[static_cast<std::size_t>(index)];
    chevron.set_rect(ImVec2(max.x - 24.0f, min.y + (size.y - 18.0f) * 0.5f), 18.0f);
    chevron.set_alpha(alpha);
    chevron.set_expanded(std::strcmp(g_expanded, m.name().c_str()) == 0);

    const bool chevron_clicked = chevron.handle_input();
    const bool toggle_clicked = toggle.handle_input();

    if (chevron_clicked || card_clicked) {
        if (std::strcmp(g_expanded, m.name().c_str()) == 0) {
            g_expanded[0] = '\0';
        } else {
            std::snprintf(g_expanded, sizeof g_expanded, "%s", m.name().c_str());
        }
        result.expanded_changed = true;
    }
    if (toggle_clicked) {
        m.set_enabled(toggle.on());
        g_toggle_state[static_cast<std::size_t>(index)] = m.enabled();
        result.toggled = true;
    }

    toggle.render(dl);
    chevron.render(dl);
    return result;
}

// ---- sidebar ---------------------------------------------------------------

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

    // MODULES: one row per spec category, badge = enabled count.
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

// ---- GENERAL pages ---------------------------------------------------------

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
        note("Every color below is a token in ui/theme.hpp - the dashboard reads");
        note("them for cards, badges, toggles and notifications.");
        y += 6.0f;
        struct swatch {
            const char* name;
            ImU32 color;
        };
        const swatch swatches[] = {
            {"window bg", ui::theme::window_bg},   {"border", ui::theme::window_border},
            {"card", ui::theme::card_bg},          {"accent", ui::theme::accent},
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
                ok ? "Configuration reloaded" : "No config file found - defaults kept");
        }
        ImGui::SameLine();
        if (ImGui::Button("Save now", ImVec2(120.0f, 26.0f))) {
            const bool ok = config::save();
            ui::notification_queue::instance().push(
                ok ? ui::toast_kind::success : ui::toast_kind::error, "Config save",
                ok ? "Configuration written" : "Write failed - see the session log");
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

// ---- toast bridge ----------------------------------------------------------

void install_notification_listener() {
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    core::event_bus::subscribe<core::module_toggled>([](const core::module_toggled& e) {
        ui::notification_queue::instance().push(
            e.enabled ? ui::toast_kind::success : ui::toast_kind::info,
            e.name != nullptr ? e.name : "module",
            e.enabled ? "Module enabled" : "Module disabled");
    });
}

} // namespace

// ---- public API ------------------------------------------------------------

void set_open(bool open) {
    const bool was = g_open.exchange(open, std::memory_order_acq_rel);
    if (was != open) {
        WOKE_INFO("gui", "click-gui %s", open ? "opened" : "closed");
        core::event_bus::emit(core::gui_visibility_changed{open});
    }
}

bool is_open() {
    return g_open.load(std::memory_order_relaxed);
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
    const bool down = key_down(keys, code);
    if (down && !g_key_was_down) {
        toggle();
    }
    g_key_was_down = down;

    // Module keybinds: one edge-detected toggle per bound module.
    const auto all = modules::module_registry::instance().all();
    for (std::size_t i = 0; i < all.size() && i < 64; ++i) {
        const int bind = config::module_keybind(all[i]->name().c_str());
        if (bind <= 0) {
            continue;
        }
        const bool mod_down = key_down(keys, bind);
        if (mod_down && !g_module_key_down[i]) {
            all[i]->set_enabled(!all[i]->enabled());
            config::save();
        }
        g_module_key_down[i] = mod_down;
    }
}

draw_stats draw() {
    draw_stats stats;
    if (ImGui::GetCurrentContext() == nullptr) {
        return stats;
    }
    ui::theme::ensure_initialized();
    ensure_widgets();
    install_notification_listener();

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
        const ImU32 hit_bg = utils::with_alpha(ui::theme::window_bg,
                                              ui::theme::window_alpha * pop);

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

        // ---- title bar: traffic lights + centered title --------------------
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
            ui::notification_queue::instance().push(ui::toast_kind::info, "Minimize",
                                                    "The dashboard is not a windowed app - "
                                                    "press the keybind to reopen it");
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

        draw_sidebar(dl, wpos, wsize, alpha, stats);

        // ---- content -------------------------------------------------------
        const float cx0 = wpos.x + ui::theme::sidebar_w + ui::theme::content_pad;
        const float cx1 = wpos.x + wsize.x - ui::theme::content_pad;
        const float content_w = cx1 - cx0;
        float y = wpos.y + ui::theme::titlebar_h + 14.0f;

        bool category_page = false;
        for (const char* c : modules::kCategories) {
            if (std::strcmp(c, g_page) == 0) {
                category_page = true;
                break;
            }
        }

        // Page header: title, sub badge, search field, view toggles.
        dl->AddText(ImVec2(cx0, y), utils::with_alpha(ui::theme::text_primary, alpha), g_page);
        char sub[96];
        if (category_page) {
            const int total = modules::category_module_count(g_page);
            const int on = modules::category_enabled_count(g_page);
            std::snprintf(sub, sizeof sub, "%d modules . %d enabled", total, on);
        } else {
            std::size_t idx = 0;
            for (std::size_t i = 0; i < kGeneralCount; ++i) {
                if (std::strcmp(kGeneralPages[i], g_page) == 0) {
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
            if (g_view_toggle.grid() != g_grid) {
                g_grid = g_view_toggle.grid();
            }
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
            const auto all = modules::module_registry::instance().by_category(g_page);
            const std::size_t count = (all.size() < kMaxCards) ? all.size() : kMaxCards;
            bind_card_channels(count);

            const float card_w = g_grid ? (content_w - ui::theme::card_gap) * 0.5f : content_w;
            const float card_h = g_grid ? 56.0f : ui::theme::card_h;
            float x = cx0;
            std::size_t shown = 0;

            for (std::size_t i = 0; i < count; ++i) {
                modules::module* m = all[i];
                if (g_search[0] != '\0') {
                    // Case-insensitive substring match on name and description.
                    auto icontains = [](const std::string& hay, const char* needle) {
                        std::string h = hay;
                        std::string n = needle;
                        for (char& c : h) {
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        }
                        for (char& c : n) {
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        }
                        return h.find(n) != std::string::npos;
                    };
                    if (!icontains(m->name(), g_search) &&
                        !icontains(m->description(), g_search)) {
                        continue;
                    }
                }

                const ImVec2 card_min(x, y);
                const ImVec2 card_max(x + card_w, y + card_h);
                const card_result r =
                    module_card(*m, card_min, ImVec2(card_w, card_h), static_cast<int>(i), dt,
                                alpha);
                if (r.toggled) {
                    ++stats.toggles;
                    config::save();
                    ui::notification_queue::instance().push(
                        m->enabled() ? ui::toast_kind::success : ui::toast_kind::info,
                        m->name().c_str(), m->enabled() ? "Enabled" : "Disabled", 2.0f);
                }
                ++shown;
                ++stats.modules_shown;

                // Expanded settings, if this card is the open one.
                const bool expanded = std::strcmp(g_expanded, m->name().c_str()) == 0;
                float advance = card_h + ui::theme::card_gap;
                if (expanded && m->settings().size() > 0) {
                    const float panel_h = 14.0f + static_cast<float>(m->settings().size()) * 30.0f;
                    const ImVec2 pmin(x, y + card_h + 2.0f);
                    utils::render::rounded_rect(dl, pmin,
                                                ImVec2(pmin.x + card_w, pmin.y + panel_h),
                                                ui::theme::frame_rounding,
                                                utils::with_alpha(ui::theme::card_bg, alpha * 0.75f));
                    bool changed = false;
                    float row_y = pmin.y + 10.0f;
                    m->settings().for_each([&](core::base_setting& s) {
                        if (setting_row(s, ImVec2(pmin.x + 14.0f, row_y), card_w - 28.0f)) {
                            changed = true;
                        }
                        row_y += 30.0f;
                    });
                    if (changed) {
                        config::save();
                    }
                    advance += panel_h + 4.0f;
                }

                y += advance;
                if (g_grid) {
                    if (x == cx0) {
                        x = cx0 + card_w + ui::theme::card_gap;
                    } else {
                        x = cx0;
                    }
                }
                (void)card_max;
                if (y > wpos.y + wsize.y - 20.0f) {
                    break;
                }
            }
            (void)shown;

            if (stats.modules_shown == 0) {
                dl->AddText(ImVec2(cx0, y + 4.0f), utils::with_alpha(ui::theme::text_dim, alpha),
                            "No modules match this page/filter yet.");
            }
        } else {
            draw_general_page(g_page, ImVec2(cx0, y), content_w, alpha, stats);
        }

        // Module draw hooks run only while the dashboard is actually open.
        if (is_open()) {
            modules::module_registry::instance().render_all();
        }

        ImGui::End();
        stats.drew = true;
    }

    // Toasts are drawn even when the dashboard is closed.
    stats.notifications = static_cast<int>(
        ui::notification_queue::instance().render(io.DisplaySize.x, dt));

    if (stats.drew || stats.notifications > 0) {
        g_draws.fetch_add(1, std::memory_order_relaxed);
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
    return g_draws.load(std::memory_order_relaxed);
}

bool select_page(const char* page) {
    if (page == nullptr || page[0] == '\0') {
        return false;
    }
    for (const char* c : modules::kCategories) {
        if (std::strcmp(c, page) == 0) {
            std::snprintf(g_page, sizeof g_page, "%s", page);
            return true;
        }
    }
    for (std::size_t i = 0; i < kGeneralCount; ++i) {
        if (std::strcmp(kGeneralPages[i], page) == 0) {
            std::snprintf(g_page, sizeof g_page, "%s", page);
            return true;
        }
    }
    return false;
}

const char* current_page() {
    return g_page;
}

void set_search(const char* text) {
    std::snprintf(g_search, sizeof g_search, "%s", (text != nullptr) ? text : "");
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
    std::snprintf(g_expanded, sizeof g_expanded, "%s",
                  (module_name != nullptr) ? module_name : "");
}

const char* expanded() {
    return g_expanded;
}

} // namespace woke::gui

// ============================================================================
//  woke.wtf — src/gui/internal.hpp
//  Internal shared state of the dashboard implementation (gui/*.cpp).
//
//  The dashboard is split into small single-purpose files; they communicate
//  through the single set of globals defined in internal.cpp. Nothing here is
//  exported outside libwoke.so — the public surface is gui/gui.hpp.
// ============================================================================
#pragma once

#include <atomic>
#include <cstddef>

#include <imgui.h>

#include "gui/gui.hpp"
#include "modules/module.hpp"
#include "ui/animation.hpp"
#include "ui/component.hpp"

namespace woke::gui::detail {

constexpr std::size_t kMaxCards = 64;          // widget arrays bound to card slots
constexpr std::size_t kMaxModuleBinds = 64;    // per-module keybind edge memory

// GENERAL pages of the dashboard (sidebar bottom section).
inline constexpr const char* kGeneralPages[] = {"Settings", "Theme", "Configs", "Socials",
                                                "Keybinds"};
inline constexpr const char* kGeneralSub[] = {
    "Client preferences",
    "Palette and motion",
    "Load, save and reset",
    "Community links",
    "Module and GUI keybinds",
};
inline constexpr std::size_t kGeneralCount = sizeof(kGeneralPages) / sizeof(kGeneralPages[0]);

// ---- single-source-of-truth state (defined in internal.cpp) ----------------

extern std::atomic<bool> g_open;
extern std::atomic<long long> g_draws;
extern bool g_key_was_down;                            // GUI keybind edge memory
extern bool g_module_key_down[kMaxModuleBinds];        // module keybind edge memory

extern char g_page[24];                                // active sidebar page
extern char g_search[64];                              // module card filter
extern char g_expanded[32];                            // expanded card (module name)
extern bool g_grid;                                    // list (false) / grid (true)
extern bool g_widgets_ready;                           // one-time widget configuration

// Window open/close motion: a spring for the pop, an eased fade for content.
extern ui::spring_value g_window_pop;
extern ui::animated_value g_content_fade;
extern ui::sidebar_entry g_category_entries[modules::kCategoryCount];
extern ui::sidebar_entry g_general_entries[5];
extern ui::search_field g_search_field;
extern ui::view_toggle g_view_toggle;
extern ui::traffic_light g_close_light;
extern ui::traffic_light g_min_light;
extern ui::traffic_light g_zoom_light;

extern ui::toggle_switch g_toggles[kMaxCards];
extern ui::chevron g_chevrons[kMaxCards];
extern ui::animated_value g_card_hover[kMaxCards];
extern int g_card_hover_bound;                         // hover channels registered so far
extern bool g_toggle_state[kMaxCards];                 // last state pushed into the widget

// ---- widget setup ----------------------------------------------------------

void ensure_widgets();
void bind_card_channels(std::size_t count);
void install_notification_listener();

// ---- X11 keybind resolution ------------------------------------------------

using xquery_keymap_fn = int (*)(void*, char*);
xquery_keymap_fn resolve_xquery_keymap();
bool key_down(const char* keys, int code);

// Case-insensitive substring test with zero allocations (search filter runs
// per card per frame — no std::string copies allowed here).
bool icontains(const char* hay, const char* needle);

// ---- card drawing ----------------------------------------------------------

struct card_result {
    bool toggled = false;
    bool expanded_changed = false;
};

// Draws one module card at `min`. `index` indexes the persistent widget arrays.
card_result module_card(modules::module& m, ImVec2 min, ImVec2 size, int index, float dt,
                        float alpha);

// Lays out and draws the module cards of the active category page, honoring
// the grid/list switch, the search filter and the expanded settings panel.
void draw_module_cards(ImDrawList* dl, float cx0, float cx1, float y, const ImVec2& wpos,
                       const ImVec2& wsize, float dt, float alpha, draw_stats& stats);

// ---- other dashboard regions ----------------------------------------------

void draw_sidebar(ImDrawList* dl, ImVec2 wpos, ImVec2 wsize, float alpha, draw_stats& stats);
void draw_general_page(const char* page, ImVec2 min, float width, float alpha,
                       draw_stats& stats);
bool setting_row(core::base_setting& s, ImVec2 pos, float width);

} // namespace woke::gui::detail

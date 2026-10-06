// ============================================================================
//  woke.wtf — src/gui/internal.cpp
//  The single set of dashboard globals and the one-time widget configuration.
//  Split out of the old monolithic gui.cpp so every file stays small
//  (~200 lines, per the engineering standards).
// ============================================================================
#include "gui/internal.hpp"

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "modules/module.hpp"   // IWYU pragma: keep (kCategoryCount via internal.hpp)
#include "ui/notifications.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"       // IWYU pragma: keep (clamp01 used by components)

namespace woke::gui::detail {

// ---- state -----------------------------------------------------------------

std::atomic<bool> g_open{false};
std::atomic<long long> g_draws{0};
bool g_key_was_down = false;
bool g_module_key_down[kMaxModuleBinds] = {};

char g_page[24] = "Visual";
char g_search[64] = "";
char g_expanded[32] = "";
bool g_grid = false;
bool g_widgets_ready = false;

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
int g_card_hover_bound = -1;
bool g_toggle_state[kMaxCards] = {};

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

} // namespace woke::gui::detail

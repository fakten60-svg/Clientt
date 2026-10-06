// ============================================================================
//  woke.wtf — src/export/game_gui.cpp
//  Client-state, click-gui, UI-subsystem and task/event C API.
//
//  All module behavior drives standard client-side game state (gamma, fov,
//  sprint/sneak) — zero packet generation (project scope).
// ============================================================================
#include <string>

#include "core/config.hpp"
#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "core/task_queue.hpp"
#include "game/game_state.hpp"
#include "gui/gui.hpp"
#include "hook/imgui_backend.hpp"
#include "hook/present_hook.hpp"
#include "ui/animation.hpp"
#include "ui/notifications.hpp"

#define WOKE_API __attribute__((visibility("default")))

extern "C" {

// ---- client-state access ------------------------------------------------------

WOKE_API int woke_game_client_ready() {
    return woke::game::game_state::instance().client_ready() ? 1 : 0;
}
WOKE_API int woke_game_current_fps() {
    return woke::game::game_state::instance().current_fps();
}
WOKE_API double woke_game_gamma() {
    return woke::game::game_state::instance().gamma();
}
WOKE_API int woke_game_set_gamma(double gamma) {
    return woke::game::game_state::instance().set_gamma(gamma) ? 1 : 0;
}
WOKE_API int woke_game_is_sprinting() {
    return woke::game::game_state::instance().is_sprinting() ? 1 : 0;
}
WOKE_API int woke_game_set_sprinting(int on) {
    return woke::game::game_state::instance().set_sprinting(on != 0) ? 1 : 0;
}
WOKE_API int woke_game_fov() {
    return woke::game::game_state::instance().fov();
}
WOKE_API int woke_game_set_fov(int fov) {
    return woke::game::game_state::instance().set_fov(fov) ? 1 : 0;
}
WOKE_API int woke_game_is_sneaking() {
    return woke::game::game_state::instance().is_sneaking() ? 1 : 0;
}
WOKE_API int woke_game_set_sneaking(int on) {
    return woke::game::game_state::instance().set_sneaking(on != 0) ? 1 : 0;
}

// ---- hook engine / present-hook introspection -----------------------------------

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

// Renderer mode: 0 = idle (no frame drawn yet), 1 = OpenGL3 attached,
// 2 = CPU-only bare mode.
WOKE_API int woke_hook_backend_status() {
    return static_cast<int>(woke::hook::backend::current_mode());
}

// Last known drawable size (XGetGeometry of the swap drawable).
WOKE_API void woke_hook_display_size(int* width, int* height) {
    if (width != nullptr) {
        *width = woke::hook::backend::display_width();
    }
    if (height != nullptr) {
        *height = woke::hook::backend::display_height();
    }
}

WOKE_API long long woke_hook_input_updates() {
    return woke::hook::backend::input_update_count();
}

// ---- click-gui ------------------------------------------------------------------

WOKE_API int woke_gui_is_open() {
    return woke::gui::is_open() ? 1 : 0;
}
WOKE_API void woke_gui_set_open(int open) {
    woke::gui::set_open(open != 0);
}
WOKE_API void woke_gui_toggle() {
    woke::gui::toggle();
}
// Headless frame cycle (NewFrame -> draw -> Render). Returns 1 on success;
// *modules_shown / *toggles may be null.
WOKE_API int woke_gui_draw_frame(int* modules_shown, int* toggles) {
    if (!woke::hook::imgui_initialized()) {
        return 0;   // no ImGui context — nothing to draw
    }
    const long long draws_before = woke::gui::draw_count();
    const woke::gui::draw_stats st = woke::gui::render_frame();
    if (woke::gui::draw_count() == draws_before) {
        return 0;   // refused: the client renders through GL, no headless frame
    }
    if (modules_shown != nullptr) {
        *modules_shown = st.modules_shown;
    }
    if (toggles != nullptr) {
        *toggles = st.toggles;
    }
    return 1;
}
WOKE_API long long woke_gui_draw_count() {
    return woke::gui::draw_count();
}

WOKE_API int woke_gui_select_page(const char* page) {
    return woke::gui::select_page(page) ? 1 : 0;
}
WOKE_API const char* woke_gui_page() {
    return woke::gui::current_page();
}
WOKE_API void woke_gui_set_search(const char* text) {
    woke::gui::set_search(text);
}
WOKE_API const char* woke_gui_search() {
    return woke::gui::search_query();
}
WOKE_API void woke_gui_set_grid(int grid) {
    woke::gui::set_grid_view(grid != 0);
}
WOKE_API int woke_gui_grid() {
    return woke::gui::grid_view() ? 1 : 0;
}
WOKE_API void woke_gui_expand(const char* module_name) {
    woke::gui::set_expanded(module_name);
}
WOKE_API const char* woke_gui_expanded() {
    return woke::gui::expanded();
}
WOKE_API int woke_gui_wants_frames() {
    return woke::gui::wants_frames() ? 1 : 0;
}

// ---- UI subsystems ---------------------------------------------------------------

// Toast notifications (kind: 0 info, 1 success, 2 warning, 3 error).
WOKE_API int woke_notify(const char* title, const char* message, int kind) {
    const auto k = (kind >= 0 && kind <= 3) ? static_cast<woke::ui::toast_kind>(kind)
                                            : woke::ui::toast_kind::info;
    woke::ui::notification_queue::instance().push(k, title, message);
    return 1;
}
WOKE_API int woke_notifications_active() {
    return static_cast<int>(woke::ui::notification_queue::instance().active());
}
WOKE_API long long woke_notifications_pushed() {
    return woke::ui::notification_queue::instance().pushed_count();
}

// Animation engine introspection.
WOKE_API int woke_animations_channels() {
    return static_cast<int>(woke::ui::animation_controller::instance().channel_count());
}
WOKE_API long long woke_animations_ticks() {
    return woke::ui::animation_controller::instance().tick_count();
}
WOKE_API float woke_animations_last_dt() {
    return woke::ui::animation_controller::instance().last_dt();
}

// ---- game-thread task queue --------------------------------------------------------

// woke_tasks_post_probe enqueues a job that only increments the executed
// counter — used by tests to prove that work posted from a foreign thread runs
// on the frame thread.
WOKE_API int woke_tasks_post_probe() {
    return woke::core::task_queue::instance().post(
               [] { WOKE_DEBUG("core", "probe task executed on the game thread"); })
               ? 1
               : 0;
}
WOKE_API long long woke_tasks_pending() {
    return static_cast<long long>(woke::core::task_queue::instance().pending());
}
WOKE_API long long woke_tasks_posted() {
    return woke::core::task_queue::instance().posted_count();
}
WOKE_API long long woke_tasks_executed() {
    return woke::core::task_queue::instance().executed_count();
}
WOKE_API long long woke_tasks_dropped() {
    return woke::core::task_queue::instance().dropped_count();
}
WOKE_API int woke_tasks_drain() {
    return static_cast<int>(woke::core::task_queue::instance().drain());
}
WOKE_API int woke_tasks_on_game_thread() {
    return woke::core::task_queue::instance().on_game_thread() ? 1 : 0;
}

// ---- event bus -----------------------------------------------------------------------

// Listener counts by event name ("module_toggled", "frame_tick",
// "gui_visibility_changed", "config_persisted"); -1 for an unknown name.
WOKE_API int woke_event_listeners(const char* event_name) {
    if (event_name == nullptr) {
        return -1;
    }
    const std::string name = event_name;
    if (name == "module_toggled") {
        return static_cast<int>(woke::core::event_bus::listener_count<woke::core::module_toggled>());
    }
    if (name == "frame_tick") {
        return static_cast<int>(woke::core::event_bus::listener_count<woke::core::frame_tick>());
    }
    if (name == "gui_visibility_changed") {
        return static_cast<int>(
            woke::core::event_bus::listener_count<woke::core::gui_visibility_changed>());
    }
    if (name == "config_persisted") {
        return static_cast<int>(
            woke::core::event_bus::listener_count<woke::core::config_persisted>());
    }
    return -1;
}

// ---- config ----------------------------------------------------------------------------

WOKE_API const char* woke_config_path() {
    return woke::config::path();
}
WOKE_API int woke_config_load() {
    return woke::config::load() ? 1 : 0;
}
WOKE_API int woke_config_save() {
    return woke::config::save() ? 1 : 0;
}
WOKE_API int woke_config_keybind() {
    return woke::config::keybind();
}
WOKE_API void woke_config_set_keybind(int keycode) {
    woke::config::set_keybind(keycode);
}

} // extern "C"

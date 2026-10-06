// ============================================================================
//  woke.wtf — src/gui/gui.hpp
//  The click-gui: a macOS-style dashboard (traffic lights, sidebar, search,
//  module cards, pill toggles) drawn with pure ImGui CPU work — no GL.
//
//  Open state lives here (single source of truth). The present detour asks
//  wants_frames() to decide between drawing and the draw-call-suppressed fast
//  path: that is true while the window is open, while its close animation is
//  still running, or while a toast is on screen. In the steady closed state
//  it is false, so the detour performs zero ImGui work.
//
//  poll_keybind() edge-triggers both the GUI keybind and every module keybind
//  from the game's X11 Display (keycodes come from the config file).
// ============================================================================
#pragma once

namespace woke::gui {

struct draw_stats {
    int modules_shown = 0;      // module cards drawn on the active page
    int categories_shown = 0;   // sidebar categories drawn
    int toggles = 0;            // toggles flipped this frame
    int notifications = 0;      // toasts drawn this frame
    bool drew = false;          // true when at least one thing was drawn
};

// GUI visibility (also drives draw-call suppression in the present detour).
void set_open(bool open);
bool is_open();
void toggle();

// True while the detour must run an ImGui frame: the window is open, its
// open/close animation is still settling, or notifications are pending.
bool wants_frames();

// Edge-triggered keybind check against the X11 keymap. `display` is the
// Display* handed to glXSwapBuffers (may be null — then this is a no-op).
// Cheap enough to call every present.
void poll_keybind(void* display);

// Builds the GUI content inside the current ImGui frame (NewFrame..Render).
draw_stats draw();

// Full standalone frame cycle (NewFrame -> draw -> Render) for headless use
// and tests. No-op when no ImGui context exists.
draw_stats render_frame();

// Number of frames in which draw() built the GUI.
long long draw_count();

// ---- dashboard controls (also exported through the woke_* C API) -----------

// Switches the active sidebar page (a module category or a GENERAL page).
// Returns false for an unknown page.
bool select_page(const char* page);
const char* current_page();

// Search filter applied to the module cards.
void set_search(const char* text);
const char* search_query();

// List (false) or grid (true) card layout.
void set_grid_view(bool grid);
bool grid_view();

// Expands one module card so its settings are editable.
void set_expanded(const char* module_name);
const char* expanded();

} // namespace woke::gui

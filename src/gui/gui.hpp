// ============================================================================
//  woke.wtf — src/gui/gui.hpp
//  The click-gui: a single ImGui window listing modules by category with
//  toggles, plus a metrics footer. Pure immediate-mode CPU work — no GL.
//
//  Open state lives here (single source of truth). The present detour reads
//  is_open() to decide between drawing and the draw-call-suppressed fast
//  path, and poll_keybind() edge-triggers the toggle from the game's X11
//  Display (the exact key comes from the config file, default Right Shift).
// ============================================================================
#pragma once

namespace woke::gui {

struct draw_stats {
    int modules_shown = 0;   // module rows drawn
    int toggles = 0;         // checkboxes flipped this frame
};

// GUI visibility (also drives draw-call suppression in the present detour).
void set_open(bool open);
bool is_open();
void toggle();

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

} // namespace woke::gui

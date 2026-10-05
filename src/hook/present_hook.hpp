// ============================================================================
//  woke.wtf — src/hook/present_hook.hpp
//  glXSwapBuffers present hook + bare ImGui frame + draw-suppression metrics.
//
//  Startup resolves glXSwapBuffers from the process (the game's libGL, or a
//  test binary that exports it), installs a MinHook detour, and creates a
//  headless ImGui context (pure CPU — no GL/GLFW backend yet; those attach
//  later, inside the real game process).
//
//  Per-frame measurement: the detour timestamps its own work BEFORE chaining
//  to the original swap — that delta is the client's rendering overhead
//  (blueprint budget: < 0.5 ms / frame). When the GUI is closed the detour
//  performs zero ImGui work (draw-call suppression) and only counts/times.
// ============================================================================
#pragma once

namespace woke::hook {

// Creates the bare ImGui context, then hooks glXSwapBuffers (via the
// MinHook wrapper). Returns false when no glXSwapBuffers exists in the
// process (non-fatal: e.g. EGL/Wayland hosts are wired in a later phase).
bool present_startup();

// Removes the detour state and destroys the ImGui context. Idempotent.
// Call engine_shutdown() first so no detour can fire mid-destruction.
void present_shutdown();

bool present_installed();
bool imgui_initialized();
const char* present_target_name();     // "glXSwapBuffers"

// GUI visibility — suppresses the ImGui frame entirely when closed.
void set_gui_open(bool open);
bool gui_open();

// Lock-free metrics (safe to read from any thread while the hook runs).
long long present_hit_count();         // detour invocations
long long present_suppressed_count();  // GUI-closed fast paths
long long present_imgui_frame_count(); // bare ImGui frames executed
long long present_last_frame_ns();     // overhead of the last detour body
long long present_total_frame_ns();    // summed overhead (avg = total / hits)

} // namespace woke::hook

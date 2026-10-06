// ============================================================================
//  woke.wtf — src/hook/imgui_backend.hpp
//  Renderer attach + input plumbing for the present hook.
//
//  Three frame modes, decided once and then sticky:
//
//    idle  — no frame drawn yet.
//    gl    — a GLX context was current on the first present frame (the game's
//            render thread inside its own glXSwapBuffers call): attach
//            imgui_impl_opengl3 and draw the overlay straight into the game's
//            back buffer.
//    bare  — no GLX context is reachable (headless tests, EGL-only hosts):
//            build the legacy CPU font atlas and keep the overlay CPU-only.
//
//  The two render paths cannot be mixed: Dear ImGui 1.92 asserts in
//  ImFontAtlasUpdateNewFrame() when an atlas was built for a legacy
//  (non-RendererHasTextures) backend and is later handed to a texture-aware
//  one, so whichever path runs first owns the process.
//
//  Display size and pointer state come from the X11 display handed to
//  glXSwapBuffers (XGetGeometry / XQueryPointer), resolved with dlsym so
//  libwoke never hard-links libX11 or libGL.
// ============================================================================
#pragma once

namespace woke::hook::backend {

enum class mode : int {
    idle = 0,   // no frame drawn yet
    gl = 1,     // imgui_impl_opengl3 attached
    bare = 2,   // CPU-only legacy atlas
};

// Called once from present_startup(), right after the ImGui context exists.
// When this process has no reachable GLX entry point the CPU-only atlas is
// built immediately (the mode can never become `gl`); otherwise the decision
// is deferred to the first present frame, whose GLX context is current.
void prepare_at_startup();

// Prepares one present frame. `display`/`drawable` are the glXSwapBuffers
// arguments (may be null/0). Returns false when no frame can be produced yet
// (GLX is reachable but nothing is current) — the caller then skips the frame
// entirely rather than feeding ImGui an unbuilt atlas.
bool begin_present_frame(void* display, unsigned long drawable);

// Prepares one headless frame (tests, exported woke_gui_draw_frame). Always
// CPU-only. Returns false when the client already renders through GL, in which
// case a headless caller must not touch the atlas.
bool begin_headless_frame();

// Submits the rendered draw data to GL. No-op unless the GL backend is attached.
void end_frame();

// Releases the renderer. Safe when nothing was attached; skipped (with a
// warning) when a GL backend is attached but no GLX context is current.
void shutdown();

mode current_mode();
bool gl_attached();

// Last known drawable size; starts at 1920x1080 until a real drawable is seen.
int display_width();
int display_height();

// Frames in which pointer state was fed from XQueryPointer.
long long input_update_count();

} // namespace woke::hook::backend

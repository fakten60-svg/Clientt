// ============================================================================
//  woke.wtf — src/utils/render.hpp
//  RenderUtils: pure, stateless ImGui draw-list helpers.
//
//  Every function is a free function taking an ImDrawList* and plain values —
//  no state, no allocation, no ImGui context required beyond the draw list.
//  They are the only place rounded corners, borders, shadows, gradients and
//  text clipping are implemented, so the ClickGUI and the toast queue share
//  one visual language (DRY) instead of repeating path math.
// ============================================================================
#pragma once

#include <imgui.h>

#include "utils/math.hpp"

namespace woke::utils::render {

// Soft drop shadow: layered rounded rects expanding outwards with a decaying
// alpha. Kept to a fixed 4 layers so the cost is constant per call.
void shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread,
            float strength);

// Filled rounded rectangle (flat color).
void rounded_rect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImU32 fill);

// Rounded outline. `thickness` is the stroke width in pixels.
void rounded_border(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImU32 col,
                    float thickness);

// Horizontal fill gradient (left -> right).
void gradient_h(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 left, ImU32 right,
                float rounding);

// Vertical fill gradient (top -> bottom).
void gradient_v(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom);

// Text with a CPU-side clip rectangle, so long labels are cut instead of
// spilling out of their card.
void text_clipped(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text,
                  const ImVec4& clip);

// Single line of glyphs, left-aligned at `pos`, ellipsized by the clip rect.
void text_ellipsized(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text,
                     float max_width);

// Circular traffic-light style button with an optional hover ring.
void dot(ImDrawList* dl, ImVec2 center, float radius, ImU32 fill, bool hovered);

// Rounded pill background + sliding nub used by the toggle switches.
void pill_toggle(ImDrawList* dl, ImVec2 min, ImVec2 max, float t, ImU32 off_col,
                 ImU32 on_col, ImU32 nub_col);

// ---- measurement -----------------------------------------------------------

float text_width(const char* text);          // uses the current ImGui font
float text_height(const char* text);

} // namespace woke::utils::render

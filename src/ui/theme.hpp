// ============================================================================
//  woke.wtf — src/ui/theme.hpp
//  Single source of truth for the ClickGUI's macOS (Sequoia/Sonoma) look:
//  palette, geometry tokens and the ImGuiStyle application.
//
//  Nothing else in the UI hardcodes a color or a radius — the whole dashboard
//  can be restyled from this file, and the theme page in the sidebar reads
//  the same tokens to render its swatches.
// ============================================================================
#pragma once

#include <imgui.h>

#include "utils/math.hpp"

namespace woke::ui::theme {

// ---- palette (packed RGBA, ImGui layout) -----------------------------------

inline const ImU32 window_bg      = utils::hex("#0B0E14");   // charcoal glass
inline const ImU32 window_border  = utils::hex("#2A3548");   // soft outline
inline const ImU32 sidebar_bg     = utils::hex("#0E121A");
inline const ImU32 card_bg        = utils::hex("#151A24");   // dark slate card
inline const ImU32 card_bg_hover  = utils::hex("#1B2231");
inline const ImU32 card_bg_active = utils::hex("#202A3C");
inline const ImU32 divider        = utils::hex("#1E2635");

inline const ImU32 text_primary   = utils::hex("#FFFFFF");
inline const ImU32 text_muted     = utils::hex("#8A96A8");   // title bar gray
inline const ImU32 text_dim       = utils::hex("#5A6577");

inline const ImU32 accent          = utils::hex("#0A84FF");  // Apple blue
inline const ImU32 accent_soft     = utils::hex("#54A9FF");
inline const ImU32 accent_cyan     = utils::hex("#32D6E0");
inline const ImU32 badge_bg        = utils::hex("#1D2635");

inline const ImU32 traffic_close   = utils::hex("#FF5F56");
inline const ImU32 traffic_min     = utils::hex("#FFBD2E");
inline const ImU32 traffic_zoom    = utils::hex("#27C93F");

inline const ImU32 pill_off        = utils::hex("#2C3340");
inline const ImU32 pill_nub        = utils::hex("#F4F6FB");

inline const ImU32 toast_bg        = utils::hex("#141A26");
inline const ImU32 toast_ok        = utils::hex("#27C93F");
inline const ImU32 toast_warn      = utils::hex("#FFBD2E");
inline const ImU32 toast_error     = utils::hex("#FF5F56");
inline const ImU32 toast_info      = utils::hex("#0A84FF");

// ---- geometry --------------------------------------------------------------

inline constexpr float window_rounding = 14.0f;   // spec: style.window_rounding
inline constexpr float frame_rounding  = 8.0f;    // spec: style.frame_rounding
inline constexpr float window_alpha    = 0.90f;   // 90% glass backdrop

inline constexpr float window_width   = 940.0f;
inline constexpr float window_height  = 560.0f;
inline constexpr float titlebar_h     = 38.0f;
inline constexpr float sidebar_w      = 210.0f;
inline constexpr float content_pad    = 14.0f;
inline constexpr float card_h         = 62.0f;
inline constexpr float card_gap       = 8.0f;
inline constexpr float traffic_r      = 6.0f;
inline constexpr float traffic_gap    = 20.0f;
inline constexpr float pill_w         = 34.0f;
inline constexpr float pill_h         = 18.0f;

// ---- ImGui style -----------------------------------------------------------

// Applies the theme to the current ImGui context. Safe to call repeatedly
// (guarded internally) and a no-op when no context exists.
void apply_style();

// One-time setup that needs a live ImGui context (style + keybind).
void ensure_initialized();

} // namespace woke::ui::theme

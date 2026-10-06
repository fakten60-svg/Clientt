// ============================================================================
//  woke.wtf — src/ui/theme.cpp
//  ImGuiStyle application for the macOS window aesthetic. Only style values
//  the dashboard actually relies on are touched, so the host game's context
//  keeps everything else it had.
// ============================================================================
#include "ui/theme.hpp"

#include "core/logger.hpp"

namespace woke::ui::theme {

namespace {

bool g_applied = false;

} // namespace

void apply_style() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = window_rounding;   // spec: 14.0
    style.FrameRounding     = frame_rounding;    // spec: 8.0
    style.GrabRounding      = frame_rounding;
    style.ChildRounding     = frame_rounding;
    style.PopupRounding     = frame_rounding;
    style.ScrollbarRounding = frame_rounding;
    style.TabRounding       = frame_rounding;

    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(content_pad, content_pad);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 8.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.AntiAliasedFill   = true;
    style.AntiAliasedLines  = true;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]        = ImGui::ColorConvertU32ToFloat4(utils::set_alpha(window_bg, 255));
    c[ImGuiCol_ChildBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg]         = ImGui::ColorConvertU32ToFloat4(utils::set_alpha(card_bg, 245));
    c[ImGuiCol_Border]          = ImGui::ColorConvertU32ToFloat4(window_border);
    c[ImGuiCol_Text]            = ImGui::ColorConvertU32ToFloat4(text_primary);
    c[ImGuiCol_TextDisabled]    = ImGui::ColorConvertU32ToFloat4(text_muted);
    c[ImGuiCol_FrameBg]         = ImGui::ColorConvertU32ToFloat4(card_bg);
    c[ImGuiCol_FrameBgHovered]  = ImGui::ColorConvertU32ToFloat4(card_bg_hover);
    c[ImGuiCol_FrameBgActive]   = ImGui::ColorConvertU32ToFloat4(card_bg_active);
    c[ImGuiCol_SliderGrab]      = ImGui::ColorConvertU32ToFloat4(accent);
    c[ImGuiCol_SliderGrabActive]= ImGui::ColorConvertU32ToFloat4(accent_soft);
    c[ImGuiCol_Button]          = ImGui::ColorConvertU32ToFloat4(card_bg_hover);
    c[ImGuiCol_ButtonHovered]   = ImGui::ColorConvertU32ToFloat4(card_bg_active);
    c[ImGuiCol_ButtonActive]    = ImGui::ColorConvertU32ToFloat4(accent);
    c[ImGuiCol_Header]          = ImGui::ColorConvertU32ToFloat4(card_bg_hover);
    c[ImGuiCol_HeaderHovered]   = ImGui::ColorConvertU32ToFloat4(card_bg_active);
    c[ImGuiCol_HeaderActive]    = ImGui::ColorConvertU32ToFloat4(accent);
    c[ImGuiCol_Separator]       = ImGui::ColorConvertU32ToFloat4(divider);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]   = ImGui::ColorConvertU32ToFloat4(badge_bg);
    c[ImGuiCol_CheckMark]       = ImGui::ColorConvertU32ToFloat4(accent);
    c[ImGuiCol_TitleBg]         = ImGui::ColorConvertU32ToFloat4(sidebar_bg);
    c[ImGuiCol_TitleBgActive]   = ImGui::ColorConvertU32ToFloat4(sidebar_bg);

    g_applied = true;
}

void ensure_initialized() {
    if (g_applied || ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    apply_style();
    WOKE_INFO("ui", "macOS theme applied (window rounding %.0f, frame rounding %.0f)",
              static_cast<double>(window_rounding), static_cast<double>(frame_rounding));
}

} // namespace woke::ui::theme

// ============================================================================
//  woke.wtf — src/ui/component.cpp
//  Widget implementations. Drawing is delegated to RenderUtils, motion to the
//  AnimationController primitives, hit-testing to a single invisible ImGui
//  button per widget.
// ============================================================================
#include "ui/component.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "ui/theme.hpp"
#include "utils/render.hpp"

namespace woke::ui {

namespace {

// Color for a traffic light kind.
ImU32 light_color(traffic_light::kind k) {
    switch (k) {
        case traffic_light::kind::close:    return theme::traffic_close;
        case traffic_light::kind::minimize: return theme::traffic_min;
        case traffic_light::kind::zoom:     return theme::traffic_zoom;
    }
    return theme::traffic_close;
}

const char* hover_tip(traffic_light::kind k) {
    switch (k) {
        case traffic_light::kind::close:    return "Close (Esc)";
        case traffic_light::kind::minimize: return "Minimize";
        case traffic_light::kind::zoom:     return "Expand";
    }
    return "";
}

} // namespace

// ---- traffic light ---------------------------------------------------------

traffic_light::traffic_light(kind k) : kind_(k) {
    animation_controller::instance().add(glow_);
}

void traffic_light::set_center(ImVec2 center, float radius) {
    center_ = center;
    radius_ = radius;
}

void traffic_light::animate(float dt) {
    (void)dt;   // glow_ is ticked by the controller
}

void traffic_light::render(ImDrawList* dl) const {
    if (!visible_) {
        return;
    }
    const ImU32 col = light_color(kind_);
    utils::render::dot(dl, center_, radius_, utils::with_alpha(col, alpha_),
                       hovered_);
    if (hovered_ && glow_.value() > 0.01f) {
        dl->AddCircle(center_, radius_ + 3.5f, utils::with_alpha(col, alpha_ * 0.45f), 24, 1.2f);
    }
}

bool traffic_light::handle_input() {
    if (!visible_) {
        return false;
    }
    const float d = radius_ * 2.0f + 6.0f;
    ImGui::PushID(this);   // one widget instance per ImGui ID, no name clashes
    ImGui::SetCursorScreenPos(ImVec2(center_.x - d * 0.5f, center_.y - d * 0.5f));
    const bool clicked = ImGui::InvisibleButton("##traffic", ImVec2(d, d));
    hovered_ = ImGui::IsItemHovered();
    glow_.set_target(hovered_ ? 1.0f : 0.0f);
    if (hovered_) {
        ImGui::SetTooltip("%s", hover_tip(kind_));
    }
    ImGui::PopID();
    return clicked;
}

// ---- sidebar entry ---------------------------------------------------------

void sidebar_entry::configure(const char* label, const char* section, int badge) {
    label_ = (label != nullptr) ? label : "";
    section_ = section;
    badge_ = badge;
}

void sidebar_entry::set_rect(ImVec2 min, ImVec2 size) {
    min_ = min;
    size_ = size;
}

void sidebar_entry::animate(float dt) {
    (void)dt;   // hover_ is ticked by the controller
}

void sidebar_entry::render(ImDrawList* dl) const {
    if (!visible_) {
        return;
    }
    const ImVec2 max(min_.x + size_.x, min_.y + size_.y);
    const float t = utils::clamp01(hover_.value());
    if (selected_) {
        utils::render::rounded_rect(dl, min_, max, theme::frame_rounding,
                                    utils::with_alpha(theme::card_bg_active, alpha_));
        // Accent rail on the left edge marks the active page.
        dl->AddRectFilled(ImVec2(min_.x, min_.y + 8.0f), ImVec2(min_.x + 2.5f, max.y - 8.0f),
                          utils::with_alpha(theme::accent, alpha_), 1.2f);
    } else if (t > 0.01f) {
        utils::render::rounded_rect(dl, min_, max, theme::frame_rounding,
                                    utils::with_alpha(theme::card_bg_hover, alpha_ * t * 0.75f));
    }

    const ImU32 label_col = selected_ ? theme::text_primary : utils::mix(theme::text_muted,
                                                          theme::text_primary, t);
    utils::render::text_ellipsized(dl, ImVec2(min_.x + 16.0f, min_.y + size_.y * 0.5f - 7.0f),
                                   utils::with_alpha(label_col, alpha_), label_, size_.x - 60.0f);

    if (badge_ >= 0) {
        char badge[16];
        std::snprintf(badge, sizeof badge, "%d", badge_);
        const float w = utils::render::text_width(badge) + 14.0f;
        const ImVec2 bmin(max.x - w - 12.0f, min_.y + size_.y * 0.5f - 9.0f);
        const ImVec2 bmax(max.x - 12.0f, min_.y + size_.y * 0.5f + 9.0f);
        utils::render::rounded_rect(dl, bmin, bmax, 9.0f,
                                    utils::with_alpha(theme::badge_bg, alpha_));
        dl->AddText(ImVec2(bmin.x + 7.0f, bmin.y + 1.0f),
                    utils::with_alpha(theme::text_muted, alpha_), badge);
    }
}

bool sidebar_entry::handle_input() {
    if (!visible_) {
        return false;
    }
    ImGui::PushID(this);
    ImGui::SetCursorScreenPos(min_);
    const bool clicked = ImGui::InvisibleButton("##sidebar", size_);
    hovered_ = ImGui::IsItemHovered();
    hover_.set_target(hovered_ ? 1.0f : 0.0f);
    ImGui::PopID();
    return clicked;
}

// ---- pill toggle -----------------------------------------------------------

toggle_switch::toggle_switch() {
    animation_controller::instance().add(knob_);
}

void toggle_switch::set_rect(ImVec2 min, ImVec2 size) {
    min_ = min;
    size_ = size;
}

void toggle_switch::set_on(bool on) {
    on_ = on;
    knob_.snap(on ? 1.0f : 0.0f);
}

void toggle_switch::animate(float dt) {
    (void)dt;
}

void toggle_switch::render(ImDrawList* dl) const {
    if (!visible_) {
        return;
    }
    const ImVec2 max(min_.x + size_.x, min_.y + size_.y);
    utils::render::pill_toggle(dl, min_, max, knob_.value(),
                               utils::with_alpha(theme::pill_off, alpha_),
                               utils::with_alpha(theme::accent, alpha_),
                               utils::with_alpha(theme::pill_nub, alpha_));
    if (hovered_) {
        dl->AddRect(min_, max, utils::with_alpha(theme::accent_soft, alpha_ * 0.6f),
                    (max.y - min_.y) * 0.5f, 0, 1.0f);
    }
}

bool toggle_switch::handle_input() {
    if (!visible_) {
        return false;
    }
    ImGui::PushID(this);
    ImGui::SetCursorScreenPos(min_);
    const bool clicked = ImGui::InvisibleButton("##pill", size_);
    hovered_ = ImGui::IsItemHovered();
    ImGui::PopID();
    if (clicked) {
        on_ = !on_;
        knob_.set_target(on_ ? 1.0f : 0.0f);
        return true;
    }
    return false;
}

// ---- search field ----------------------------------------------------------

void search_field::set_rect(ImVec2 min, float width) {
    min_ = min;
    width_ = width;
}

void search_field::set_text(const char* text) {
    std::snprintf(buffer_, sizeof buffer_, "%s", (text != nullptr) ? text : "");
}

void search_field::animate(float dt) {
    (void)dt;
}

void search_field::render(ImDrawList* dl) const {
    if (!visible_) {
        return;
    }
    const ImVec2 max(min_.x + width_, min_.y + 26.0f);
    utils::render::rounded_rect(dl, min_, max, 13.0f,
                                utils::with_alpha(theme::card_bg, alpha_));
    if (focus_.value() > 0.01f) {
        utils::render::rounded_border(dl, min_, max, 13.0f,
                                      utils::with_alpha(theme::accent, alpha_ * focus_.value()),
                                      1.2f);
    }
    // Magnifier substitute: a small ring + handle, drawn from the draw list.
    const ImVec2 c(min_.x + 14.0f, min_.y + 12.0f);
    dl->AddCircle(c, 4.0f, utils::with_alpha(theme::text_dim, alpha_), 12, 1.3f);
    dl->AddLine(ImVec2(c.x + 3.0f, c.y + 3.0f), ImVec2(c.x + 6.5f, c.y + 6.5f),
                utils::with_alpha(theme::text_dim, alpha_), 1.3f);

    const char* shown = (buffer_[0] != '\0') ? buffer_ : "Search modules";
    const ImU32 col = (buffer_[0] != '\0') ? theme::text_primary : theme::text_dim;
    utils::render::text_ellipsized(dl, ImVec2(min_.x + 26.0f, min_.y + 5.0f),
                                   utils::with_alpha(col, alpha_), shown, width_ - 36.0f);
}

bool search_field::handle_input() {
    if (!visible_) {
        return false;
    }
    const ImVec2 max(min_.x + width_, min_.y + 26.0f);
    ImGui::PushID(this);
    ImGui::SetCursorScreenPos(min_);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::SetNextItemWidth(max.x - min_.x);
    const bool changed = ImGui::InputText("##woke_search", buffer_, sizeof buffer_,
                                          ImGuiInputTextFlags_None);
    focused_ = ImGui::IsItemActive();
    focus_.set_target(focused_ ? 1.0f : 0.0f);
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return changed;
}

// ---- view toggle -----------------------------------------------------------

void view_toggle::set_rect(ImVec2 min, float button_size) {
    min_ = min;
    size_ = button_size;
}

void view_toggle::animate(float dt) {
    (void)dt;
}

void view_toggle::render(ImDrawList* dl) const {
    if (!visible_) {
        return;
    }
    const ImVec2 min = min_;
    const ImVec2 max(min_.x + size_ * 2.0f + 4.0f, min_.y + size_);
    utils::render::rounded_rect(dl, min, max, theme::frame_rounding,
                                utils::with_alpha(theme::card_bg, alpha_));
    const float t = utils::clamp01(slide_.value());
    const ImVec2 pill_min(min.x + 2.0f + t * (size_ + 0.0f), min.y + 2.0f);
    const ImVec2 pill_max(pill_min.x + size_, min.y + size_ - 2.0f);
    utils::render::rounded_rect(dl, pill_min, pill_max, theme::frame_rounding - 2.0f,
                                utils::with_alpha(theme::accent, alpha_));

    // List glyph (three bars) and grid glyph (four squares).
    const ImU32 on_col = utils::with_alpha(theme::pill_nub, alpha_);
    const ImU32 off_col = utils::with_alpha(theme::text_muted, alpha_);
    const float cx0 = min.x + 2.0f + size_ * 0.5f;
    const float cx1 = min.x + 6.0f + size_ * 1.5f;
    const float cy = min.y + size_ * 0.5f;
    for (int i = -1; i <= 1; ++i) {
        const float iy = static_cast<float>(i) * 4.0f;
        dl->AddRectFilled(ImVec2(cx0 - 5.0f, cy + iy - 1.0f),
                          ImVec2(cx0 + 5.0f, cy + iy + 1.0f), t < 0.5f ? on_col : off_col);
    }
    for (int i = 0; i < 4; ++i) {
        const float ox = (i % 2 == 0) ? -5.0f : 1.0f;
        const float oy = (i < 2) ? -5.0f : 1.0f;
        dl->AddRectFilled(ImVec2(cx1 + ox, cy + oy), ImVec2(cx1 + ox + 4.0f, cy + oy + 4.0f),
                          t >= 0.5f ? on_col : off_col);
    }
}

bool view_toggle::handle_input() {
    if (!visible_) {
        return false;
    }
    ImGui::PushID(this);
    ImGui::SetCursorScreenPos(min_);
    const bool clicked_list = ImGui::InvisibleButton("##view_list", ImVec2(size_, size_));
    ImGui::SetCursorScreenPos(ImVec2(min_.x + size_, min_.y));
    const bool clicked_grid = ImGui::InvisibleButton("##view_grid", ImVec2(size_, size_));
    ImGui::PopID();
    if (clicked_list) {
        grid_ = false;
        slide_.set_target(0.0f);
        return true;
    }
    if (clicked_grid) {
        grid_ = true;
        slide_.set_target(1.0f);
        return true;
    }
    return false;
}

// ---- chevron ---------------------------------------------------------------

void chevron::set_rect(ImVec2 min, float size) {
    min_ = min;
    size_ = size;
}

void chevron::set_expanded(bool expanded) {
    expanded_ = expanded;
    rotation_.set_target(expanded ? 1.0f : 0.0f);
}

void chevron::animate(float dt) {
    (void)dt;
}

void chevron::render(ImDrawList* dl) const {
    if (!visible_) {
        return;
    }
    const ImVec2 c(min_.x + size_ * 0.5f, min_.y + size_ * 0.5f);
    const float angle = utils::lerp(0.0f, utils::kPi * 0.5f, rotation_.value());
    const float r = size_ * 0.22f;
    const ImU32 col = utils::with_alpha(hovered_ ? theme::text_primary : theme::text_muted, alpha_);
    const ImVec2 a(c.x + std::cos(angle) * r, c.y + std::sin(angle) * r);
    const ImVec2 b(c.x + std::cos(angle + utils::kPi * 0.66f) * r,
                   c.y + std::sin(angle + utils::kPi * 0.66f) * r);
    const ImVec2 d(c.x + std::cos(angle - utils::kPi * 0.66f) * r,
                   c.y + std::sin(angle - utils::kPi * 0.66f) * r);
    dl->AddTriangleFilled(a, b, d, col);
}

bool chevron::handle_input() {
    if (!visible_) {
        return false;
    }
    ImGui::PushID(this);
    ImGui::SetCursorScreenPos(min_);
    const bool clicked = ImGui::InvisibleButton("##chevron", ImVec2(size_, size_));
    hovered_ = ImGui::IsItemHovered();
    ImGui::PopID();
    if (clicked) {
        expanded_ = !expanded_;
        rotation_.set_target(expanded_ ? 1.0f : 0.0f);
        return true;
    }
    return false;
}

} // namespace woke::ui

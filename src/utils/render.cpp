// ============================================================================
//  woke.wtf — src/utils/render.cpp
//  RenderUtils implementation. All drawing goes through the ImDrawList the
//  caller owns, so nothing here allocates or touches GPU state.
// ============================================================================
#include "utils/render.hpp"

#include <cstring>

namespace woke::utils::render {

namespace {

// Same rounding on all four corners unless the rect is smaller than 2*rounding.
float fit_rounding(ImVec2 min, ImVec2 max, float rounding) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float limit = (w < h ? w : h) * 0.5f;
    return rounding > limit ? (limit > 0.0f ? limit : 0.0f) : rounding;
}

} // namespace

void shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread,
            float strength) {
    if (dl == nullptr || strength <= 0.0f) {
        return;
    }
    constexpr int kLayers = 4;
    for (int i = kLayers; i >= 1; --i) {
        const float t = static_cast<float>(i) / static_cast<float>(kLayers);   // 1 = outermost
        const float grow = spread * t;
        const float a = strength * (1.0f - t) * 0.55f + strength * 0.10f;
        const ImVec2 lo(min.x - grow, min.y - grow + 1.0f);
        const ImVec2 hi(max.x + grow, max.y + grow + 1.0f);
        dl->AddRectFilled(lo, hi, utils::rgba(0, 0, 0, static_cast<int>(a * 255.0f)),
                          fit_rounding(lo, hi, rounding + grow));
    }
}

void rounded_rect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImU32 fill) {
    if (dl == nullptr) {
        return;
    }
    dl->AddRectFilled(min, max, fill, fit_rounding(min, max, rounding));
}

void rounded_border(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImU32 col,
                    float thickness) {
    if (dl == nullptr) {
        return;
    }
    dl->AddRect(min, max, col, fit_rounding(min, max, rounding), ImDrawFlags_None, thickness);
}

void gradient_h(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 left, ImU32 right,
                float rounding) {
    if (dl == nullptr) {
        return;
    }
    const float r = fit_rounding(min, max, rounding);
    if (r <= 0.5f) {
        dl->AddRectFilledMultiColor(min, max, left, right, right, left);
        return;
    }
    // Rounded shape in the left color, then the gradient laid into the inset
    // (square-cornered) middle so the radius survives at both ends.
    dl->AddRectFilled(min, max, left, r);
    const ImVec2 mid_lo(min.x + r, min.y);
    const ImVec2 mid_hi(max.x - r, max.y);
    if (mid_hi.x > mid_lo.x) {
        dl->AddRectFilledMultiColor(mid_lo, mid_hi, left, right, right, left);
    }
}

void gradient_v(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom) {
    if (dl == nullptr) {
        return;
    }
    dl->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
}

void text_clipped(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text,
                  const ImVec4& clip) {
    if (dl == nullptr || text == nullptr) {
        return;
    }
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, col, text, nullptr, 0.0f, &clip);
}

void text_ellipsized(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text,
                     float max_width) {
    if (dl == nullptr || text == nullptr || max_width <= 0.0f) {
        return;
    }
    if (text_width(text) <= max_width) {
        dl->AddText(pos, col, text);
        return;
    }
    // Shrink into a fixed stack buffer (no heap traffic) and append "..." until
    // the label fits. The default ImGui font is ASCII, so ASCII it is.
    char buf[128];
    const std::size_t len = std::strlen(text);
    std::size_t take = (len < sizeof(buf) - 4) ? len : (sizeof(buf) - 4);
    while (take > 0) {
        std::memcpy(buf, text, take);
        buf[take] = '.';
        buf[take + 1] = '.';
        buf[take + 2] = '.';
        buf[take + 3] = '\0';
        if (text_width(buf) <= max_width) {
            dl->AddText(pos, col, buf);
            return;
        }
        --take;
    }
    dl->AddText(pos, col, "...");
}

void dot(ImDrawList* dl, ImVec2 center, float radius, ImU32 fill, bool hovered) {
    if (dl == nullptr) {
        return;
    }
    if (hovered) {
        dl->AddCircleFilled(center, radius + 2.5f,
                            utils::with_alpha(fill, 0.30f), 24);
    }
    dl->AddCircleFilled(center, radius, fill, 24);
}

void pill_toggle(ImDrawList* dl, ImVec2 min, ImVec2 max, float t, ImU32 off_col,
                 ImU32 on_col, ImU32 nub_col) {
    if (dl == nullptr) {
        return;
    }
    t = utils::clamp01(t);
    const float h = max.y - min.y;
    const float r = h * 0.5f;
    const ImU32 track = utils::mix(off_col, on_col, utils::ease_out_cubic(t));
    dl->AddRectFilled(min, max, track, r);

    const float pad = 2.0f;
    const float nub_r = r - pad;
    const float travel = (max.x - min.x) - h;
    const float nub_x = min.x + r + travel * utils::ease_out_back(t);
    dl->AddCircleFilled(ImVec2(nub_x, min.y + r), nub_r, nub_col, 24);
}

float text_width(const char* text) {
    if (text == nullptr) {
        return 0.0f;
    }
    return ImGui::CalcTextSize(text).x;
}

float text_height(const char* text) {
    if (text == nullptr) {
        return ImGui::GetFontSize();
    }
    return ImGui::CalcTextSize(text).y;
}

} // namespace woke::utils::render

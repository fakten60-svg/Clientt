// ============================================================================
//  woke.wtf — src/gui/setting_row.cpp
//  The generic BaseSetting<T> editor row: one row per setting with the label
//  on the left and the matching editor widget on the right. Every setting
//  kind has exactly one branch here, so the module cards and the settings
//  pages never repeat editor code (DRY).
//
//  Editors are driven by the setting's own metadata: declared min/max bounds
//  drive the sliders and the named choices of a mode setting drive the
//  dropdown — no per-module editor code is possible.
// ============================================================================
#include "gui/internal.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/setting.hpp"

namespace woke::gui::detail {

bool setting_row(core::base_setting& s, ImVec2 pos, float width) {
    ImGui::PushID(&s);
    ImGui::SetCursorScreenPos(pos);
    ImGui::TextUnformatted(s.name().c_str());
    if (ImGui::IsItemHovered() && !s.description().empty()) {
        ImGui::SetTooltip("%s", s.description().c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x + width * 0.5f, pos.y));
    ImGui::SetNextItemWidth(width * 0.5f);

    bool changed = false;
    switch (s.type()) {
        case core::setting_type::boolean: {
            core::setting_value v = s.to_value();
            bool on = v.boolean;
            if (ImGui::Checkbox("##bool", &on)) {
                v.boolean = on;
                s.from_value(v);
                changed = true;
            }
            break;
        }
        case core::setting_type::integer: {
            core::setting_value v = s.to_value();
            int iv = static_cast<int>(v.integer);
            const double lo = s.numeric_min();
            const double hi = s.numeric_max();
            if (s.choice_count() > 0) {
                // Mode dropdown: pick from the setting's named choices.
                const char* current = (s.choice_label(iv) != nullptr) ? s.choice_label(iv) : "?";
                if (ImGui::BeginCombo("##mode", current)) {
                    for (int c = 0; c < s.choice_count(); ++c) {
                        const bool selected = (c == iv);
                        if (ImGui::Selectable(s.choice_label(c), selected)) {
                            v.integer = c;
                            s.from_value(v);
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (hi > lo) {
                // Numeric slider driven by the setting's declared bounds.
                const int ilo = static_cast<int>(lo);
                const int ihi = static_cast<int>(hi);
                if (ImGui::SliderInt("##int", &iv, ilo, ihi)) {
                    v.integer = iv;
                    s.from_value(v);
                    changed = true;
                }
            } else {
                // Unranged integer: plain input, no bogus ±255 bounds.
                if (ImGui::InputInt("##int", &iv)) {
                    v.integer = iv;
                    s.from_value(v);
                    changed = true;
                }
            }
            break;
        }
        case core::setting_type::color: {
            core::setting_value v = s.to_value();
            int iv = static_cast<int>(v.integer);
            float col[4] = {static_cast<float>((iv >> 16) & 0xFF) / 255.0f,
                            static_cast<float>((iv >> 8) & 0xFF) / 255.0f,
                            static_cast<float>(iv & 0xFF) / 255.0f, 1.0f};
            if (ImGui::ColorEdit4("##color", col, ImGuiColorEditFlags_NoInputs)) {
                const long long packed =
                    (static_cast<long long>(col[0] * 255.0f + 0.5f) << 16) |
                    (static_cast<long long>(col[1] * 255.0f + 0.5f) << 8) |
                    static_cast<long long>(col[2] * 255.0f + 0.5f);
                v.integer = packed;
                s.from_value(v);
                changed = true;
            }
            break;
        }
        case core::setting_type::decimal: {
            core::setting_value v = s.to_value();
            float fv = static_cast<float>(v.decimal);
            const double lo = s.numeric_min();
            const double hi = s.numeric_max();
            const float flo = (hi > lo) ? static_cast<float>(lo) : 0.0f;
            const float fhi = (hi > lo) ? static_cast<float>(hi) : 24.0f;
            if (ImGui::SliderFloat("##float", &fv, flo, fhi, "%.2f")) {
                v.decimal = static_cast<double>(fv);
                s.from_value(v);
                changed = true;
            }
            break;
        }
        case core::setting_type::text: {
            core::setting_value v = s.to_value();
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s", v.text.c_str());
            if (ImGui::InputText("##text", buf, sizeof buf)) {
                v.text = buf;
                s.from_value(v);
                changed = true;
            }
            break;
        }
    }
    if (changed) {
        s.clear_dirty();
    }
    ImGui::PopID();
    return changed;
}

} // namespace woke::gui::detail

// ============================================================================
//  woke.wtf — src/gui/card.cpp
//  One reusable module card widget + the layout that places them.
//
//  module_card() draws a single card: hover-eased background, name +
//  description + keybind badge on the left, the Apple-style pill toggle and
//  the expand chevron on the right. It is the BaseUIComponent composition the
//  spec asks for — cards never re-implement widgets, they compose them.
//
//  draw_module_cards() lays out the cards of the active category page in list
//  or grid mode, applies the search filter through the zero-alloc matcher,
//  renders the expanded settings panel via the generic setting_row editor and
//  persists + toasts on every change.
// ============================================================================
#include "gui/internal.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/config.hpp"
#include "core/setting.hpp"
#include "ui/notifications.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"

namespace woke::gui {

namespace detail {

card_result module_card(modules::module& m, ImVec2 min, ImVec2 size, int index, float dt,
                        float alpha) {
    (void)dt;
    card_result result;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size.x, min.y + size.y);

    ImGui::PushID(index);
    ImGui::SetCursorScreenPos(min);
    const bool card_clicked = ImGui::InvisibleButton("##card", size);
    const bool card_hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ui::animated_value& hover = g_card_hover[static_cast<std::size_t>(index)];
    hover.set_target(card_hovered ? 1.0f : 0.0f);
    const float h = utils::clamp01(hover.value());

    const ImU32 bg = utils::mix(ui::theme::card_bg, ui::theme::card_bg_hover, h);
    utils::render::shadow(dl, min, max, ui::theme::frame_rounding, 6.0f * h, 0.30f);
    utils::render::rounded_rect(dl, min, max, ui::theme::frame_rounding,
                                utils::with_alpha(bg, alpha));
    if (h > 0.01f) {
        utils::render::rounded_border(dl, min, max, ui::theme::frame_rounding,
                                      utils::with_alpha(ui::theme::accent_soft, alpha * h * 0.5f),
                                      1.0f);
    }

    // Left: name + description + keybind badge.
    const float text_x = min.x + 16.0f;
    const float right_reserved = 130.0f;
    utils::render::text_ellipsized(dl, ImVec2(text_x, min.y + 12.0f),
                                   utils::with_alpha(ui::theme::text_primary, alpha),
                                   m.name().c_str(), size.x - right_reserved);
    utils::render::text_ellipsized(dl, ImVec2(text_x, min.y + 32.0f),
                                   utils::with_alpha(ui::theme::text_muted, alpha),
                                   m.description().c_str(), size.x - right_reserved);

    const int key = config::module_keybind(m.name().c_str());
    if (key > 0) {
        char badge[24];
        std::snprintf(badge, sizeof badge, "[KEY: %d]", key);
        const float w = utils::render::text_width(badge) + 14.0f;
        const ImVec2 bmin(max.x - right_reserved - w + 8.0f, min.y + 12.0f);
        const ImVec2 bmax(bmin.x + w, bmin.y + 18.0f);
        utils::render::rounded_rect(dl, bmin, bmax, 9.0f,
                                    utils::with_alpha(ui::theme::badge_bg, alpha));
        dl->AddText(ImVec2(bmin.x + 7.0f, bmin.y + 1.0f),
                    utils::with_alpha(ui::theme::accent_soft, alpha), badge);
    }

    // Right: the Apple-style pill toggle.
    ui::toggle_switch& toggle = g_toggles[static_cast<std::size_t>(index)];
    if (g_toggle_state[static_cast<std::size_t>(index)] != m.enabled()) {
        toggle.set_on(m.enabled());   // external change (config load, API) — snap
        g_toggle_state[static_cast<std::size_t>(index)] = m.enabled();
    }
    const ImVec2 pill_size(ui::theme::pill_w, ui::theme::pill_h);
    toggle.set_rect(ImVec2(max.x - pill_size.x - 44.0f, min.y + (size.y - pill_size.y) * 0.5f),
                    pill_size);
    toggle.set_alpha(alpha);

    // Chevron (card expander) sits just right of the toggle.
    ui::chevron& chevron = g_chevrons[static_cast<std::size_t>(index)];
    chevron.set_rect(ImVec2(max.x - 24.0f, min.y + (size.y - 18.0f) * 0.5f), 18.0f);
    chevron.set_alpha(alpha);
    chevron.set_expanded(std::strcmp(g_expanded, m.name().c_str()) == 0);

    const bool chevron_clicked = chevron.handle_input();
    const bool toggle_clicked = toggle.handle_input();

    if (chevron_clicked || card_clicked) {
        if (std::strcmp(g_expanded, m.name().c_str()) == 0) {
            g_expanded[0] = '\0';
        } else {
            std::snprintf(g_expanded, sizeof g_expanded, "%s", m.name().c_str());
        }
        result.expanded_changed = true;
    }
    if (toggle_clicked) {
        m.set_enabled(toggle.on());
        g_toggle_state[static_cast<std::size_t>(index)] = m.enabled();
        result.toggled = true;
    }

    toggle.render(dl);
    chevron.render(dl);
    return result;
}

void draw_module_cards(ImDrawList* dl, float cx0, float cx1, float y, const ImVec2& wpos,
                       const ImVec2& wsize, float dt, float alpha, draw_stats& stats) {
    const float content_w = cx1 - cx0;
    const auto all = modules::module_registry::instance().by_category(g_page);
    const std::size_t count = (all.size() < kMaxCards) ? all.size() : kMaxCards;
    bind_card_channels(count);

    const float card_w = g_grid ? (content_w - ui::theme::card_gap) * 0.5f : content_w;
    const float card_h = g_grid ? 56.0f : ui::theme::card_h;
    float x = cx0;

    for (std::size_t i = 0; i < count; ++i) {
        modules::module* m = all[i];
        if (g_search[0] != '\0' && !icontains(m->name().c_str(), g_search) &&
            !icontains(m->description().c_str(), g_search)) {
            continue;
        }

        const card_result r =
            module_card(*m, ImVec2(x, y), ImVec2(card_w, card_h), static_cast<int>(i), dt, alpha);
        if (r.toggled) {
            ++stats.toggles;
            config::save();
            ui::notification_queue::instance().push(m->enabled() ? ui::toast_kind::success
                                                                 : ui::toast_kind::info,
                                                    m->name().c_str(),
                                                    m->enabled() ? "Enabled" : "Disabled", 2.0f);
        }
        ++stats.modules_shown;

        // Expanded settings, if this card is the open one.
        const bool expanded = std::strcmp(g_expanded, m->name().c_str()) == 0;
        float advance = card_h + ui::theme::card_gap;
        if (expanded && m->settings().size() > 0) {
            const float panel_h = 14.0f + static_cast<float>(m->settings().size()) * 30.0f;
            const ImVec2 pmin(x, y + card_h + 2.0f);
            utils::render::rounded_rect(dl, pmin, ImVec2(pmin.x + card_w, pmin.y + panel_h),
                                        ui::theme::frame_rounding,
                                        utils::with_alpha(ui::theme::card_bg, alpha * 0.75f));
            bool changed = false;
            float row_y = pmin.y + 10.0f;
            m->settings().for_each([&](core::base_setting& s) {
                if (setting_row(s, ImVec2(pmin.x + 14.0f, row_y), card_w - 28.0f)) {
                    changed = true;
                }
                row_y += 30.0f;
            });
            if (changed) {
                config::save();
            }
            advance += panel_h + 4.0f;
        }

        y += advance;
        if (g_grid) {
            x = (x == cx0) ? (cx0 + card_w + ui::theme::card_gap) : cx0;
        }
        if (y > wpos.y + wsize.y - 20.0f) {
            break;
        }
    }

    if (stats.modules_shown == 0) {
        dl->AddText(ImVec2(cx0, y + 4.0f), utils::with_alpha(ui::theme::text_dim, alpha),
                    "No modules match this page/filter yet.");
    }
}

} // namespace detail

} // namespace woke::gui

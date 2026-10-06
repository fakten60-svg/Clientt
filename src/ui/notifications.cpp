// ============================================================================
//  woke.wtf — src/ui/notifications.cpp
//  Toast queue implementation. Fixed storage, animated slide-in from the
//  top-right, a fading lifetime bar, then an animated slide-out.
// ============================================================================
#include "ui/notifications.hpp"

#include <cstdio>
#include <cstring>

#include "core/logger.hpp"
#include "ui/theme.hpp"
#include "utils/render.hpp"

namespace woke::ui {

namespace {

constexpr float kToastWidth = 306.0f;
constexpr float kToastHeight = 56.0f;
constexpr float kToastGap = 10.0f;
constexpr float kMargin = 18.0f;
constexpr float kSlideIn = 0.20f;    // seconds of enter animation
constexpr float kFadeOut = 0.35f;    // seconds of exit animation

struct slot {
    bool used = false;
    toast_kind kind = toast_kind::info;
    char title[48] = {};
    char message[168] = {};
    float lifetime = 3.5f;     // fully visible seconds
    float elapsed = 0.0f;      // since the enter animation finished
    animated_value slide{0.0f, 0.055f};   // 0 = off-screen, 1 = docked
};

slot g_slots[notification_queue::capacity()];
long long g_pushed = 0;
long long g_expired = 0;

slot* find_free() {
    for (auto& s : g_slots) {
        if (!s.used) {
            return &s;
        }
    }
    return nullptr;
}

ImU32 kind_color(toast_kind kind) {
    switch (kind) {
        case toast_kind::success: return theme::toast_ok;
        case toast_kind::warning: return theme::toast_warn;
        case toast_kind::error:   return theme::toast_error;
        case toast_kind::info:    break;
    }
    return theme::toast_info;
}

} // namespace

const char* toast_kind_name(toast_kind kind) {
    switch (kind) {
        case toast_kind::info:    return "info";
        case toast_kind::success: return "success";
        case toast_kind::warning: return "warning";
        case toast_kind::error:   return "error";
    }
    return "info";
}

notification_queue& notification_queue::instance() {
    static notification_queue queue;
    return queue;
}

void notification_queue::push(toast_kind kind, const char* title, const char* message,
                              float lifetime) {
    slot* s = find_free();
    if (s == nullptr) {
        // Every slot busy: reuse the oldest so the newest message is never lost.
        s = &g_slots[0];
        for (auto& candidate : g_slots) {
            if (candidate.elapsed > s->elapsed) {
                s = &candidate;
            }
        }
        ++g_expired;
    }
    *s = slot{};
    s->used = true;
    s->kind = kind;
    s->lifetime = (lifetime > 0.1f) ? lifetime : 0.1f;
    std::snprintf(s->title, sizeof s->title, "%s", (title != nullptr) ? title : "");
    std::snprintf(s->message, sizeof s->message, "%s", (message != nullptr) ? message : "");
    s->slide.set_target(1.0f);
    ++g_pushed;
    WOKE_DEBUG("ui", "toast [%s] %s - %s", toast_kind_name(kind), s->title, s->message);
}

std::size_t notification_queue::render(float display_width, float dt) {
    std::size_t drawn = 0;
    float y = kMargin;

    for (auto& s : g_slots) {
        if (!s.used) {
            continue;
        }

        s.elapsed += dt;
        // Keep the slide animated for a moment after the lifetime ends so the
        // exit motion is actually visible before the slot is freed.
        const bool expiring = s.elapsed >= (kSlideIn + s.lifetime);
        s.slide.set_target(expiring ? 0.0f : 1.0f);
        s.slide.update(dt);
        if (expiring && s.slide.settled() && s.elapsed >= (kSlideIn + s.lifetime + kFadeOut)) {
            s.used = false;
            ++g_expired;
            continue;
        }

        const float t = utils::clamp01(s.slide.value());
        const float ease = utils::ease_out_cubic(t);
        const float x = display_width - kMargin - kToastWidth + (1.0f - ease) * 40.0f;
        const ImVec2 min(x, y);
        const ImVec2 max(x + kToastWidth, y + kToastHeight);

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        utils::render::shadow(dl, min, max, theme::frame_rounding, 16.0f, 0.35f * ease);
        dl->AddRectFilled(min, max, utils::with_alpha(theme::toast_bg, ease),
                          theme::frame_rounding);
        utils::render::rounded_border(dl, min, max, theme::frame_rounding,
                                      utils::with_alpha(theme::window_border, ease), 1.0f);
        // Accent strip on the left edge, icon substitute for the toast kind.
        dl->AddRectFilled(ImVec2(min.x + 10.0f, min.y + 12.0f),
                          ImVec2(min.x + 13.0f, max.y - 12.0f),
                          utils::with_alpha(kind_color(s.kind), ease), 1.5f);

        const float text_x = min.x + 24.0f;
        const ImU32 title_col = utils::with_alpha(theme::text_primary, ease);
        const ImU32 msg_col = utils::with_alpha(theme::text_muted, ease);
        utils::render::text_ellipsized(dl, ImVec2(text_x, min.y + 10.0f), title_col, s.title,
                                      kToastWidth - 40.0f);
        utils::render::text_ellipsized(dl, ImVec2(text_x, min.y + 30.0f), msg_col, s.message,
                                      kToastWidth - 40.0f);

        // Lifetime bar: eases back to zero as the toast ages out.
        const float life = utils::clamp01(1.0f - (s.elapsed / (kSlideIn + s.lifetime)));
        const float bar_w = (kToastWidth - 34.0f) * utils::ease_out_quad(life);
        if (bar_w > 1.0f) {
            dl->AddRectFilled(ImVec2(text_x, max.y - 12.0f),
                              ImVec2(text_x + bar_w, max.y - 10.0f),
                              utils::with_alpha(kind_color(s.kind), ease * 0.85f), 1.0f);
        }

        y += kToastHeight + kToastGap;
        ++drawn;
    }
    return drawn;
}

std::size_t notification_queue::active() const {
    std::size_t n = 0;
    for (const auto& s : g_slots) {
        if (s.used) {
            ++n;
        }
    }
    return n;
}

long long notification_queue::pushed_count() const {
    return g_pushed;
}

long long notification_queue::expired_count() const {
    return g_expired;
}

void notification_queue::clear() {
    for (auto& s : g_slots) {
        s.used = false;
    }
}

} // namespace woke::ui

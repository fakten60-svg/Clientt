// ============================================================================
//  woke.wtf — src/ui/notifications.hpp
//  NotificationQueue: reusable toast manager.
//
//  Any subsystem (module toggles, config writes, hook warnings) pushes a
//  generic title/message/icon tuple and forgets about it — the queue owns
//  stacking, layout, timing and the slide-in/slide-out animation, and draws
//  through RenderUtils. Storage is a fixed ring of POD toasts with inline
//  character buffers, so a toast never allocates.
//
//  Toasts stay visible even when the ClickGUI is closed: the present detour
//  keeps drawing while the queue is non-empty, so a notification is never
//  silently swallowed by draw-call suppression.
// ============================================================================
#pragma once

#include <cstddef>

#include <imgui.h>

#include "ui/animation.hpp"

namespace woke::ui {

enum class toast_kind : int {
    info = 0,
    success = 1,
    warning = 2,
    error = 3,
};

class notification_queue {
public:
    static constexpr std::size_t capacity() { return 5; }

    static notification_queue& instance();

    // Enqueues a toast. Long strings are truncated, never heap-allocated.
    // `lifetime` is the fully-visible time in seconds (fade in/out excluded).
    void push(toast_kind kind, const char* title, const char* message,
              float lifetime = 3.5f);

    // Draws the stack anchored under the top-right corner of `display_width` /
    // `display_height`, advances its animations by `dt` and returns how many
    // toasts were drawn. Call once per frame, GUI open or not.
    std::size_t render(float display_width, float dt);

    std::size_t active() const;
    long long pushed_count() const;
    long long expired_count() const;

    // Immediately drops every toast (shutdown path).
    void clear();

private:
    notification_queue() = default;
};

const char* toast_kind_name(toast_kind kind);

} // namespace woke::ui

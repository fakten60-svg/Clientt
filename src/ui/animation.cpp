// ============================================================================
//  woke.wtf — src/ui/animation.cpp
//  AnimationController: one tick per frame drives every registered channel.
// ============================================================================
#include "ui/animation.hpp"

#include "core/logger.hpp"

namespace woke::ui {

namespace {

// Anything outside this window is a stall (breakpoint, alt-tab, first frame).
constexpr float kMinDt = 1.0f / 1000.0f;
constexpr float kMaxDt = 1.0f / 15.0f;

} // namespace

animation_controller& animation_controller::instance() {
    static animation_controller controller;
    return controller;
}

void animation_controller::add(animated_value& v) {
    for (std::size_t i = 0; i < value_count_; ++i) {
        if (values_[i] == &v) {
            return;
        }
    }
    if (value_count_ < capacity()) {
        values_[value_count_++] = &v;
    } else {
        WOKE_WARN("ui", "animation channel list full (%zu) — value not ticked", capacity());
    }
}

void animation_controller::add(spring_value& v) {
    for (std::size_t i = 0; i < spring_count_; ++i) {
        if (springs_[i] == &v) {
            return;
        }
    }
    if (spring_count_ < capacity()) {
        springs_[spring_count_++] = &v;
    } else {
        WOKE_WARN("ui", "animation channel list full (%zu) — spring not ticked", capacity());
    }
}

void animation_controller::add(easing_curve& c) {
    for (std::size_t i = 0; i < curve_count_; ++i) {
        if (curves_[i] == &c) {
            return;
        }
    }
    if (curve_count_ < capacity()) {
        curves_[curve_count_++] = &c;
    } else {
        WOKE_WARN("ui", "animation channel list full (%zu) — curve not ticked", capacity());
    }
}

void animation_controller::tick(float dt) {
    if (dt < kMinDt) {
        dt = kMinDt;
    } else if (dt > kMaxDt) {
        dt = kMaxDt;   // a longer stall snaps, it does not animate
    }
    last_dt_ = dt;
    ++ticks_;

    for (std::size_t i = 0; i < value_count_; ++i) {
        values_[i]->update(dt);
    }
    for (std::size_t i = 0; i < spring_count_; ++i) {
        springs_[i]->update(dt);
    }
    for (std::size_t i = 0; i < curve_count_; ++i) {
        curves_[i]->update(dt);
    }
}

float animation_controller::last_dt() const {
    return last_dt_;
}

long long animation_controller::tick_count() const {
    return static_cast<long long>(ticks_);
}

std::size_t animation_controller::channel_count() const {
    return value_count_ + spring_count_ + curve_count_;
}

void animation_controller::clear() {
    for (std::size_t i = 0; i < value_count_; ++i) {
        values_[i] = nullptr;
    }
    for (std::size_t i = 0; i < spring_count_; ++i) {
        springs_[i] = nullptr;
    }
    for (std::size_t i = 0; i < curve_count_; ++i) {
        curves_[i] = nullptr;
    }
    value_count_ = 0;
    spring_count_ = 0;
    curve_count_ = 0;
}

} // namespace woke::ui

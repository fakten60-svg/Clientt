// ============================================================================
//  woke.wtf — src/ui/animation.hpp
//  UI animation engine.
//
//  Every visual transition in the dashboard (window open/close, hover
//  brightening, pill-knob sliding, toast slide-in/out) is driven by one of the
//  three primitives below and advanced exactly once per frame by the central
//  AnimationController, using the frame's delta time. That keeps motion
//  identical on a 30 Hz and a 240 Hz monitor instead of scaling with FPS.
//
//    animated_value — exponential approach (critically damped feel, no
//                     overshoot) for hover/alpha/color interpolation
//    spring_value   — semi-implicit spring for the window pop and the toggle
//                     nub (both want a little overshoot)
//    easing_curve   — fixed-duration progress 0..1 with an easing function,
//                     used by the toast lifetime bars
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "utils/math.hpp"

namespace woke::ui {

class animated_value {
public:
    explicit animated_value(float initial = 0.0f, float halflife = 0.06f)
        : value_(initial), target_(initial), halflife_(halflife) {}

    void set_halflife(float seconds) { halflife_ = seconds; }
    float halflife() const { return halflife_; }

    void snap(float v) {
        value_ = v;
        target_ = v;
    }
    void set_target(float t) { target_ = t; }
    float target() const { return target_; }
    float value() const { return value_; }

    void update(float dt) { value_ = utils::exp_approach(value_, target_, halflife_, dt); }

    bool settled(float epsilon = 0.0005f) const {
        const float d = value_ - target_;
        return (d < 0.0f ? -d : d) < epsilon;
    }

private:
    float value_;
    float target_;
    float halflife_;
};

class spring_value {
public:
    spring_value(float initial = 0.0f, float stiffness = 190.0f, float damping = 24.0f)
        : state_{initial, 0.0f}, target_(initial), stiffness_(stiffness), damping_(damping) {}

    void snap(float v) {
        state_.value = v;
        state_.velocity = 0.0f;
        target_ = v;
    }
    void set_target(float t) { target_ = t; }
    float target() const { return target_; }
    float value() const { return state_.value; }

    void update(float dt) { utils::spring_step(state_, target_, stiffness_, damping_, dt); }

    bool settled(float epsilon = 0.0005f) const {
        const float d = state_.value - target_;
        return (d < 0.0f ? -d : d) < epsilon && (state_.velocity < 0.0f ? -state_.velocity
                                                                       : state_.velocity) < epsilon;
    }

private:
    utils::spring_state state_;
    float target_;
    float stiffness_;
    float damping_;
};

class easing_curve {
public:
    using curve_fn = float (*)(float);

    easing_curve(float duration_seconds, curve_fn curve, bool loop = false)
        : duration_(duration_seconds > 0.0f ? duration_seconds : 0.001f),
          curve_(curve != nullptr ? curve : &utils::ease_out_cubic),
          loop_(loop) {}

    void restart() {
        elapsed_ = 0.0f;
        done_ = false;
        progress_ = 0.0f;
    }

    void update(float dt) {
        if (done_ && !loop_) {
            return;
        }
        elapsed_ += dt;
        if (elapsed_ >= duration_) {
            if (loop_) {
                elapsed_ -= duration_ * static_cast<float>(static_cast<int>(elapsed_ / duration_));
            } else {
                elapsed_ = duration_;
                done_ = true;
            }
        }
        progress_ = elapsed_ / duration_;
    }

    float progress() const { return progress_; }       // linear 0..1
    float eased() const { return curve_(progress_); }  // curve applied
    bool done() const { return done_; }
    bool looping() const { return loop_; }

private:
    float duration_;
    curve_fn curve_;
    bool loop_;
    float elapsed_ = 0.0f;
    float progress_ = 0.0f;
    bool done_ = false;
};

// Advances every registered animation once per frame from a single call.
class animation_controller {
public:
    static constexpr std::size_t kCapacity = 128;
    static constexpr std::size_t capacity() { return kCapacity; }

    static animation_controller& instance();

    // Registers an animated value to be ticked. Idempotent per pointer.
    void add(animated_value& v);
    void add(spring_value& v);
    void add(easing_curve& c);

    // Clamps dt to a sane window so a stall never teleports the animations.
    void tick(float dt);

    float last_dt() const;
    long long tick_count() const;
    std::size_t channel_count() const;

    void clear();

private:
    animation_controller() = default;

    animated_value* values_[kCapacity] = {};
    std::size_t value_count_ = 0;
    spring_value* springs_[kCapacity] = {};
    std::size_t spring_count_ = 0;
    easing_curve* curves_[kCapacity] = {};
    std::size_t curve_count_ = 0;

    float last_dt_ = 0.0f;
    std::int64_t ticks_ = 0;
};

} // namespace woke::ui

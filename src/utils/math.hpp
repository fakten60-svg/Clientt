// ============================================================================
//  woke.wtf — src/utils/math.hpp
//  MathUtils: pure, stateless math helpers (header-only, zero allocation).
//
//  Everything here is constexpr/inline and takes/returns plain floats or
//  ImU32 colors, so it can be called from inside on_render()/on_tick()
//  hot paths without any heap traffic. The easing curves are the ones the
//  UI animation engine and the ClickGUI motion system use.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace woke::utils {

// ---- scalars ---------------------------------------------------------------

constexpr float kPi = 3.14159265358979323846f;

template <typename T>
constexpr T clamp(T v, T lo, T hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

constexpr float clamp01(float v) noexcept { return clamp(v, 0.0f, 1.0f); }

// Linear interpolation. t is clamped so callers can pass raw animation
// progress without guarding.
constexpr float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * clamp01(t);
}

// Frame-rate independent exponential approach: `current` chases `target` and
// arrives at the same place after `dt` seconds regardless of the FPS.
// `halflife` is the time (seconds) in which half the remaining distance is
// covered — 0 snaps immediately.
inline float exp_approach(float current, float target, float halflife, float dt) noexcept {
    if (halflife <= 0.0f) {
        return target;
    }
    const float decay = 0.6931472f / halflife;      // ln(2) / halflife
    const float factor = 1.0f - std::exp(-decay * dt);
    return current + (target - current) * clamp01(factor);
}

// Critically-damped spring step (semi-implicit Euler). Used for the window
// open/close and toggle-nub motion so the result is smooth on any refresh
// rate instead of frame-count dependent.
struct spring_state {
    float value = 0.0f;
    float velocity = 0.0f;
};

inline void spring_step(spring_state& s, float target, float stiffness, float damping,
                        float dt) noexcept {
    const float accel = (target - s.value) * stiffness - s.velocity * damping;
    s.velocity += accel * dt;
    s.value += s.velocity * dt;
}

// ---- easing curves ---------------------------------------------------------

constexpr float ease_in_quad(float t) noexcept {
    t = clamp01(t);
    return t * t;
}

constexpr float ease_out_quad(float t) noexcept {
    t = clamp01(t);
    return t * (2.0f - t);
}

constexpr float ease_out_cubic(float t) noexcept {
    t = clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

constexpr float ease_in_out_cubic(float t) noexcept {
    t = clamp01(t);
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    const float u = -2.0f * t + 2.0f;
    return 1.0f - (u * u * u) * 0.5f;
}

// Overshooting ease-out — the "Apple" pop used by the pill toggle nub.
constexpr float ease_out_back(float t) noexcept {
    t = clamp01(t);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// ---- angles ----------------------------------------------------------------

constexpr float deg_to_rad(float deg) noexcept { return deg * (kPi / 180.0f); }
constexpr float rad_to_deg(float rad) noexcept { return rad * (180.0f / kPi); }

// ---- color helpers (ImU32-packed AABBGGRR, same layout ImGui uses) ---------

constexpr std::uint32_t rgba(int r, int g, int b, int a = 255) noexcept {
    return (static_cast<std::uint32_t>(a & 0xFF) << 24) |
           (static_cast<std::uint32_t>(b & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(r & 0xFF);
}

// "#RRGGBB" / "RRGGBB" -> packed color. Malformed input returns fallback.
inline std::uint32_t hex(const char* text, std::uint32_t fallback = 0) noexcept {
    if (text == nullptr) {
        return fallback;
    }
    if (text[0] == '#') {
        ++text;
    }
    auto digit = [](char c, int& out) {
        if (c >= '0' && c <= '9') { out = c - '0'; return true; }
        if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
        if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
        return false;
    };
    std::uint32_t value = 0;
    for (int i = 0; i < 6; ++i) {
        int d = 0;
        if (!digit(text[i], d)) {
            return fallback;
        }
        value = (value << 4) | static_cast<std::uint32_t>(d);
    }
    const std::uint32_t r = (value >> 16) & 0xFFu;
    const std::uint32_t g = (value >> 8) & 0xFFu;
    const std::uint32_t b = value & 0xFFu;
    return rgba(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), 255);
}

constexpr int color_alpha(std::uint32_t c) noexcept {
    return static_cast<int>((c >> 24) & 0xFFu);
}

// Replace the alpha channel of a packed color (t is 0..1 and multiplies a).
inline std::uint32_t with_alpha(std::uint32_t c, float t) noexcept {
    const int a = static_cast<int>(static_cast<float>(color_alpha(c)) * clamp01(t) + 0.5f);
    return (c & 0x00FFFFFFu) | (static_cast<std::uint32_t>(a & 0xFF) << 24);
}

constexpr std::uint32_t set_alpha(std::uint32_t c, int a) noexcept {
    return (c & 0x00FFFFFFu) | (static_cast<std::uint32_t>(a & 0xFF) << 24);
}

// Channel-wise blend: t = 0 -> a, t = 1 -> b.
constexpr std::uint32_t mix(std::uint32_t a, std::uint32_t b, float t) noexcept {
    t = clamp01(t);
    const auto ch = [t](std::uint32_t x, std::uint32_t y, int shift) {
        const float xv = static_cast<float>((x >> shift) & 0xFFu);
        const float yv = static_cast<float>((y >> shift) & 0xFFu);
        return static_cast<std::uint32_t>(xv + (yv - xv) * t + 0.5f) & 0xFFu;
    };
    return (ch(a, b, 24) << 24) | (ch(a, b, 16) << 16) | (ch(a, b, 8) << 8) | ch(a, b, 0);
}

// Brightness amplification for hover states (capped at white).
constexpr std::uint32_t brighten(std::uint32_t c, float amount) noexcept {
    return mix(c, rgba(255, 255, 255, color_alpha(c)), amount);
}

} // namespace woke::utils

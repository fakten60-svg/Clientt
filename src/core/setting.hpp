// ============================================================================
//  woke.wtf — src/core/setting.hpp
//  BaseSetting<T>: templated, strongly-typed settings with one uniform
//  serialization path (header-only).
//
//  A setting owns its value and knows how to convert itself to/from the small
//  setting_value POD below, so the config engine never needs a switch per
//  setting kind and modules never repeat load/save code:
//
//      core::setting<bool>    enabled{"Enabled", "Toggle the module", false};
//      core::setting<double>  gamma  {"Gamma",   "Target gamma", 16.0, 0.0, 24.0};
//      core::setting<int>     key    {"Key",     "X11 keycode",  62, 0, 255};
//      core::setting<std::uint32_t> tint{"Tint", "Card color", 0xFF8A96A8u};
//
//  Supported T: bool (switch), integral (slider/mode/keybind), floating point
//  (slider), std::uint32_t (packed color) and std::string (free text/mode).
//  Values are compared by the subclass on set(), so `dirty()` tells the config
//  engine whether a write is needed — the frame loops only read value().
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace woke::core {

enum class setting_type : int {
    boolean = 0,
    integer = 1,
    decimal = 2,
    color = 3,
    text = 4,
};

// Type-erased storage used by the config engine.
struct setting_value {
    setting_type type = setting_type::boolean;
    bool boolean = false;
    long long integer = 0;
    double decimal = 0.0;
    std::string text{};
};

class base_setting {
public:
    base_setting(std::string name, std::string description, setting_type type)
        : name_(std::move(name)), description_(std::move(description)), type_(type) {}
    virtual ~base_setting() = default;

    base_setting(const base_setting&) = delete;
    base_setting& operator=(const base_setting&) = delete;

    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    setting_type type() const { return type_; }

    virtual setting_value to_value() const = 0;
    virtual void from_value(const setting_value& v) = 0;
    virtual void reset() = 0;

    // True when the value changed since the last clear_dirty().
    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

protected:
    void mark_dirty() { dirty_ = true; }

private:
    std::string name_;
    std::string description_;
    setting_type type_;
    bool dirty_ = false;
};

template <typename T>
constexpr setting_type type_of() {
    if constexpr (std::is_same_v<T, bool>) {
        return setting_type::boolean;
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        return setting_type::color;
    } else if constexpr (std::is_integral_v<T>) {
        return setting_type::integer;
    } else if constexpr (std::is_floating_point_v<T>) {
        return setting_type::decimal;
    } else {
        return setting_type::text;
    }
}

template <typename T>
class setting final : public base_setting {
public:
    setting(std::string name, std::string description, T default_value, T min_value = T{},
            T max_value = T{})
        : base_setting(std::move(name), std::move(description), type_of<T>()),
          default_(default_value),
          value_(default_value),
          min_(min_value),
          max_(max_value) {}

    T value() const { return value_; }
    T default_value() const { return default_; }
    T min_value() const { return min_; }
    T max_value() const { return max_; }

    // Returns true when the value actually changed.
    bool set(T v) {
        v = clamp_to_range(v);
        if (v == value_) {
            return false;
        }
        value_ = v;
        mark_dirty();
        if (on_change_) {
            on_change_();
        }
        return true;
    }

    void on_change(std::function<void()> fn) { on_change_ = std::move(fn); }

    setting_value to_value() const override {
        setting_value out;
        out.type = type();
        if constexpr (std::is_same_v<T, bool>) {
            out.boolean = value_;
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            out.integer = static_cast<long long>(value_);
        } else if constexpr (std::is_integral_v<T>) {
            out.integer = static_cast<long long>(value_);
        } else if constexpr (std::is_floating_point_v<T>) {
            out.decimal = static_cast<double>(value_);
        } else {
            out.text = value_;
        }
        return out;
    }

    void from_value(const setting_value& v) override {
        if constexpr (std::is_same_v<T, bool>) {
            set(static_cast<T>(v.boolean));
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            set(static_cast<T>(v.integer));
        } else if constexpr (std::is_integral_v<T>) {
            set(static_cast<T>(v.integer));
        } else if constexpr (std::is_floating_point_v<T>) {
            set(static_cast<T>(v.decimal));
        } else {
            set(v.text);
        }
        clear_dirty();   // loading is not a user edit
    }

    void reset() override { set(default_); }

private:
    T clamp_to_range(T v) const {
        if constexpr (std::is_arithmetic_v<T>) {
            if (max_ != min_) {   // callers pass min/max only for ranged settings
                if (v < min_) {
                    return min_;
                }
                if (v > max_) {
                    return max_;
                }
            }
        }
        return v;
    }

    T default_;
    T value_;
    T min_;
    T max_;
    std::function<void()> on_change_;
};

// Non-owning grouping of settings (the owner outlives the group). Modules
// keep their settings as members and register them here once at construction.
class setting_group {
public:
    void add(base_setting& s) { settings_.push_back(&s); }

    std::size_t size() const { return settings_.size(); }
    base_setting* at(std::size_t i) const { return (i < settings_.size()) ? settings_[i] : nullptr; }

    base_setting* find(const std::string& name) const {
        for (base_setting* s : settings_) {
            if (s->name() == name) {
                return s;
            }
        }
        return nullptr;
    }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (base_setting* s : settings_) {
            if (s != nullptr) {
                fn(*s);
            }
        }
    }

    bool any_dirty() const {
        for (base_setting* s : settings_) {
            if (s != nullptr && s->dirty()) {
                return true;
            }
        }
        return false;
    }

    void clear_dirty() {
        for (base_setting* s : settings_) {
            if (s != nullptr) {
                s->clear_dirty();
            }
        }
    }

    void reset_all() {
        for (base_setting* s : settings_) {
            if (s != nullptr) {
                s->reset();
            }
        }
    }

private:
    std::vector<base_setting*> settings_;
};

} // namespace woke::core

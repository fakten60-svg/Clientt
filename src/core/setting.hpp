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

#include <array>
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

    // Numeric bounds for slider rendering (max <= min means "unranged").
    virtual double numeric_min() const { return 0.0; }
    virtual double numeric_max() const { return 0.0; }

    // Mode dropdowns expose their named choices here; 0 choices means the
    // setting renders as a slider/checkbox instead of a combo.
    virtual int choice_count() const { return 0; }
    virtual const char* choice_label(int index) const {
        (void)index;
        return nullptr;
    }

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

    double numeric_min() const override { return static_cast<double>(min_); }
    double numeric_max() const override { return static_cast<double>(max_); }

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

    template <typename T>
    setting<T>* find_typed(const std::string& name) {
        base_setting* s = find(name);
        return (s != nullptr && s->type() == type_of<T>()) ? dynamic_cast<setting<T>*>(s)
                                                          : nullptr;
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

// Mode dropdown: an integer-backed setting whose value indexes a fixed list of
// named choices. Serializes as its integer index, so the config engine needs
// no special handling and the dashboard renders a combo box instead of a
// numeric slider (choice_count() > 0 drives that switch).
class mode_setting final : public base_setting {
public:
    mode_setting(std::string name, std::string description, std::initializer_list<const char*> choices,
                 int default_index)
        : base_setting(std::move(name), std::move(description), setting_type::integer),
          count_(choices.size() > kMaxChoices ? kMaxChoices : choices.size()) {
        int i = 0;
        for (const char* c : choices) {
            if (static_cast<std::size_t>(i) >= count_) {
                break;
            }
            choices_[static_cast<std::size_t>(i)] = c;
            ++i;
        }
        default_ = (default_index >= 0 && static_cast<std::size_t>(default_index) < count_)
                       ? default_index
                       : 0;
        value_ = default_;
    }

    int value() const { return value_; }
    int default_index() const { return default_; }
    int count() const { return static_cast<int>(count_); }

    const char* label() const {
        return (count_ == 0) ? "" : choices_[static_cast<std::size_t>(value_)].c_str();
    }
    const char* label(int index) const {
        return (index >= 0 && static_cast<std::size_t>(index) < count_)
                   ? choices_[static_cast<std::size_t>(index)].c_str()
                   : nullptr;
    }

    // Selects a choice by index. false only when out of range; re-selecting
    // the current value is a successful no-op.

    bool set(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= count_) {
            return false;   // out of range: rejected
        }
        if (index == value_) {
            return true;    // already selected — no-op, not an error
        }
        value_ = index;
        mark_dirty();
        if (on_change_) {
            on_change_();
        }
        return true;
    }

    void on_change(std::function<void()> fn) { on_change_ = std::move(fn); }

    setting_value to_value() const override {
        setting_value out;
        out.type = setting_type::integer;
        out.integer = static_cast<long long>(value_);
        return out;
    }

    void from_value(const setting_value& v) override {
        const long long idx = v.integer;
        if (idx >= 0 && static_cast<std::size_t>(idx) < count_) {
            set(static_cast<int>(idx));
        }
        clear_dirty();   // loading is not a user edit
    }

    void reset() override { set(default_); }

    int choice_count() const override { return static_cast<int>(count_); }
    const char* choice_label(int index) const override { return label(index); }

private:
    static constexpr std::size_t kMaxChoices = 8;

    std::array<std::string, kMaxChoices> choices_{};
    std::size_t count_ = 0;
    int default_ = 0;
    int value_ = 0;
    std::function<void()> on_change_;
};

} // namespace woke::core

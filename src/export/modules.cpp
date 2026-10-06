// ============================================================================
//  woke.wtf — src/export/modules.cpp
//  Module-registry C API: enumeration, enable/disable, per-category views,
//  settings introspection (incl. mode-dropdown choices) and keybinds.
// ============================================================================
#include <string>

#include "core/config.hpp"
#include "core/setting.hpp"
#include "game/game_state.hpp"
#include "modules/builtin.hpp"
#include "modules/module.hpp"

#define WOKE_API __attribute__((visibility("default")))

namespace {

using woke::core::base_setting;
using woke::modules::module;
using woke::modules::module_registry;

const module* find_module(const char* module_name) {
    return (module_name != nullptr) ? module_registry::instance().find(module_name) : nullptr;
}

base_setting* find_setting(const char* module_name, const char* setting_name) {
    module* m = (module_name != nullptr) ? module_registry::instance().find(module_name) : nullptr;
    if (m == nullptr || setting_name == nullptr) {
        return nullptr;
    }
    return m->settings().find(setting_name);
}

} // namespace

extern "C" {

// ---- module registry --------------------------------------------------------

WOKE_API int woke_module_count() {
    return static_cast<int>(module_registry::instance().all().size());
}

WOKE_API const char* woke_module_name(int index) {
    const auto all = module_registry::instance().all();
    return (index >= 0 && index < static_cast<int>(all.size()))
               ? all[static_cast<std::size_t>(index)]->name().c_str()
               : nullptr;
}

WOKE_API const char* woke_module_category(int index) {
    const auto all = module_registry::instance().all();
    return (index >= 0 && index < static_cast<int>(all.size()))
               ? all[static_cast<std::size_t>(index)]->category().c_str()
               : nullptr;
}

WOKE_API int woke_module_enabled(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    const module* m = module_registry::instance().find(name);
    return (m != nullptr) ? (m->enabled() ? 1 : 0) : -1;
}

// Toggles a module and persists the config immediately. 1 = ok, 0 = unknown.
WOKE_API int woke_module_set_enabled(const char* name, int enabled) {
    if (name == nullptr) {
        return 0;
    }
    if (!module_registry::instance().set_enabled(name, enabled != 0)) {
        return 0;
    }
    woke::config::save();
    return 1;
}

WOKE_API void woke_modules_tick() {
    module_registry::instance().tick_all(woke::game::game_state::instance());
}

// ---- module categories -------------------------------------------------------

WOKE_API int woke_module_category_count_total() {
    return static_cast<int>(woke::modules::kCategoryCount);
}

WOKE_API const char* woke_module_category_at(int index) {
    return (index >= 0 && index < static_cast<int>(woke::modules::kCategoryCount))
               ? woke::modules::kCategories[index]
               : nullptr;
}

WOKE_API int woke_module_count_in_category(const char* category) {
    return (category != nullptr) ? woke::modules::category_module_count(category) : 0;
}

WOKE_API int woke_module_enabled_in_category(const char* category) {
    return (category != nullptr) ? woke::modules::category_enabled_count(category) : 0;
}

// ---- settings (0=boolean 1=integer 2=decimal 3=color 4=text, -1 unknown) -----

WOKE_API int woke_module_setting_count(const char* module_name) {
    const module* m = find_module(module_name);
    return (m != nullptr) ? static_cast<int>(m->settings().size()) : -1;
}

WOKE_API const char* woke_module_setting_name(const char* module_name, int index) {
    const module* m = find_module(module_name);
    if (m == nullptr || index < 0) {
        return nullptr;
    }
    const base_setting* s = m->settings().at(static_cast<std::size_t>(index));
    return (s != nullptr) ? s->name().c_str() : nullptr;
}

WOKE_API int woke_module_setting_kind(const char* module_name, const char* setting_name) {
    const base_setting* s = find_setting(module_name, setting_name);
    return (s != nullptr) ? static_cast<int>(s->type()) : -1;
}

WOKE_API int woke_module_setting_bool(const char* module_name, const char* setting_name) {
    const base_setting* s = find_setting(module_name, setting_name);
    if (s == nullptr || s->type() != woke::core::setting_type::boolean) {
        return -1;
    }
    return s->to_value().boolean ? 1 : 0;
}

WOKE_API double woke_module_setting_double(const char* module_name, const char* setting_name) {
    const base_setting* s = find_setting(module_name, setting_name);
    if (s == nullptr) {
        return 0.0;
    }
    const woke::core::setting_value v = s->to_value();
    return (v.type == woke::core::setting_type::decimal)
               ? v.decimal
               : static_cast<double>(v.integer);
}

WOKE_API int woke_module_set_setting_double(const char* module_name, const char* setting_name,
                                            double value) {
    base_setting* s = find_setting(module_name, setting_name);
    if (s == nullptr) {
        return 0;
    }
    woke::core::setting_value v = s->to_value();
    if (v.type == woke::core::setting_type::decimal) {
        v.decimal = value;
    } else {
        v.integer = static_cast<long long>(value);
    }
    s->from_value(v);
    return 1;
}

// ---- mode dropdowns -----------------------------------------------------------

// Number of named choices of a mode-dropdown setting; 0 when it is not one.
WOKE_API int woke_module_setting_choice_count(const char* module_name,
                                              const char* setting_name) {
    const base_setting* s = find_setting(module_name, setting_name);
    return (s != nullptr) ? s->choice_count() : 0;
}

// Choice label at `index`; null when out of range or not a dropdown.
WOKE_API const char* woke_module_setting_choice_label(const char* module_name,
                                                      const char* setting_name, int index) {
    const base_setting* s = find_setting(module_name, setting_name);
    return (s != nullptr) ? s->choice_label(index) : nullptr;
}

// Current dropdown selection (integer index); -1 when not a mode setting.
WOKE_API int woke_module_setting_choice(const char* module_name, const char* setting_name) {
    const base_setting* s = find_setting(module_name, setting_name);
    if (s == nullptr || s->choice_count() == 0 || s->type() != woke::core::setting_type::integer) {
        return -1;
    }
    return static_cast<int>(s->to_value().integer);
}

// Select a dropdown entry by index; 1 = ok, 0 = rejected (out of range).
WOKE_API int woke_module_set_setting_choice(const char* module_name, const char* setting_name,
                                            int index) {
    base_setting* s = find_setting(module_name, setting_name);
    if (s == nullptr || s->type() != woke::core::setting_type::integer) {
        return 0;
    }
    auto* mode = dynamic_cast<woke::core::mode_setting*>(s);
    return (mode != nullptr && mode->set(index)) ? 1 : 0;
}

// ---- keybinds + reset ----------------------------------------------------------

WOKE_API int woke_module_keybind(const char* module_name) {
    return (module_name != nullptr) ? woke::config::module_keybind(module_name) : 0;
}

WOKE_API int woke_module_set_keybind(const char* module_name, int keycode) {
    return woke::config::set_module_keybind(module_name, keycode) ? 1 : 0;
}

WOKE_API int woke_config_reset_settings(const char* module_name) {
    return woke::config::reset_settings(module_name);
}

} // extern "C"

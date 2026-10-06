// ============================================================================
//  woke.wtf — src/core/config.cpp
//  Load/save implementation. Writes are atomic-ish (single fwrite of the
//  serialized document) and every failure path keeps in-memory state intact.
//
//  Settings are converted through core::base_setting::to_value()/
//  from_value(), so this file never grows a switch when a module adds a
//  setting — that is the whole point of the BaseSetting<T> abstraction.
// ============================================================================
#include "core/config.hpp"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "modules/module.hpp"

namespace woke::config {

namespace {

constexpr const char* kSchema = "woke.wtf/config.v1";
constexpr int kDefaultKeybind = 62;   // X11 keycode: Right Shift

std::mutex g_mutex;
int g_keybind = kDefaultKeybind;
std::unordered_map<std::string, int> g_module_keybinds;

nlohmann::json setting_to_json(const core::setting_value& v) {
    switch (v.type) {
        case core::setting_type::boolean: return v.boolean;
        case core::setting_type::integer: return v.integer;
        case core::setting_type::color:   return v.integer;
        case core::setting_type::decimal: return v.decimal;
        case core::setting_type::text:    return v.text;
    }
    return nullptr;
}

bool json_to_setting(const nlohmann::json& j, core::setting_value& out) {
    switch (out.type) {
        case core::setting_type::boolean:
            if (!j.is_boolean()) return false;
            out.boolean = j.get<bool>();
            return true;
        case core::setting_type::integer:
        case core::setting_type::color:
            if (!j.is_number_integer() && !j.is_number()) return false;
            out.integer = j.get<long long>();
            return true;
        case core::setting_type::decimal:
            if (!j.is_number()) return false;
            out.decimal = j.get<double>();
            return true;
        case core::setting_type::text:
            if (!j.is_string()) return false;
            out.text = j.get<std::string>();
            return true;
    }
    return false;
}

} // namespace

const char* path() {
    const char* p = std::getenv("WOKE_CONFIG_PATH");
    return (p != nullptr && p[0] != '\0') ? p : "woke_config.json";
}

int keybind() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_keybind;
}

void set_keybind(int keycode) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (keycode < 0 || keycode > 255) {
            keycode = kDefaultKeybind;
        }
        g_keybind = keycode;
    }
    save();
}

int module_keybind(const char* module_name) {
    if (module_name == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_module_keybinds.find(module_name);
    return (it != g_module_keybinds.end()) ? it->second : 0;
}

bool set_module_keybind(const char* module_name, int keycode, bool persist) {
    if (module_name == nullptr || module_name[0] == '\0') {
        return false;
    }
    if (keycode < 0 || keycode > 255) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (keycode == 0) {
            g_module_keybinds.erase(module_name);
        } else {
            g_module_keybinds[module_name] = keycode;
        }
    }
    if (persist) {
        save();
    }
    return true;
}

int reset_settings(const char* module_name) {
    int changed = 0;
    for (modules::module* m : modules::module_registry::instance().all()) {
        if (module_name != nullptr && m->name() != module_name) {
            continue;
        }
        m->settings().for_each([&changed](core::base_setting& s) {
            const core::setting_value before = s.to_value();
            s.reset();
            const core::setting_value after = s.to_value();
            if (before.type != after.type || before.boolean != after.boolean ||
                before.integer != after.integer || before.decimal != after.decimal ||
                before.text != after.text) {
                ++changed;
            }
            s.clear_dirty();
        });
    }
    if (changed > 0) {
        save();
    }
    return changed;
}

bool load() {
    std::FILE* f = std::fopen(path(), "rb");
    if (f == nullptr) {
        WOKE_INFO("config", "no config at %s — using defaults", path());
        return false;
    }
    std::string raw;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        raw.append(buf, n);
    }
    std::fclose(f);

    nlohmann::json doc = nlohmann::json::object();
    try {
        doc = nlohmann::json::parse(raw);
    } catch (const std::exception& e) {
        WOKE_WARN("config", "config parse failed (%s) — using defaults", e.what());
        return false;
    }
    if (!doc.is_object()) {
        WOKE_WARN("config", "config root is not an object — using defaults");
        return false;
    }

    if (doc.contains("gui") && doc["gui"].is_object()) {
        const auto& gui = doc["gui"];
        if (gui.contains("keybind") && gui["keybind"].is_number_integer()) {
            std::lock_guard<std::mutex> lock(g_mutex);
            const int code = gui["keybind"].get<int>();
            g_keybind = (code >= 0 && code <= 255) ? code : kDefaultKeybind;
        }
    }

    long applied = 0;
    if (doc.contains("modules") && doc["modules"].is_object()) {
        auto& registry = modules::module_registry::instance();
        for (auto it = doc["modules"].begin(); it != doc["modules"].end(); ++it) {
            if (!it.value().is_boolean()) {
                continue;
            }
            if (registry.set_enabled(it.key(), it.value().get<bool>())) {
                ++applied;
            }
        }
    }

    // ---- settings (generic, keyed by module then setting name) --------------
    long settings_applied = 0;
    if (doc.contains("settings") && doc["settings"].is_object()) {
        for (modules::module* m : modules::module_registry::instance().all()) {
            const auto mod_it = doc["settings"].find(m->name());
            if (mod_it == doc["settings"].end() || !mod_it->is_object()) {
                continue;
            }
            m->settings().for_each([&](core::base_setting& s) {
                const auto set_it = mod_it->find(s.name());
                if (set_it == mod_it->end()) {
                    return;
                }
                core::setting_value v = s.to_value();
                if (json_to_setting(*set_it, v)) {
                    s.from_value(v);
                    ++settings_applied;
                }
            });
        }
    }

    // ---- keybinds ----------------------------------------------------------
    std::size_t keybinds_applied = 0;
    if (doc.contains("keybinds") && doc["keybinds"].is_object()) {
        for (auto it = doc["keybinds"].begin(); it != doc["keybinds"].end(); ++it) {
            if (!it.value().is_number_integer()) {
                continue;
            }
            if (set_module_keybind(it.key().c_str(), it.value().get<int>(), false)) {
                ++keybinds_applied;
            }
        }
    }

    WOKE_INFO("config",
              "config loaded from %s (keybind=%d, %ld module states, %ld settings, %zu keybinds)",
              path(), keybind(), applied, settings_applied, keybinds_applied);
    return true;
}

bool save() {
    nlohmann::json doc;
    doc["schema"] = kSchema;
    doc["gui"] = {{"keybind", keybind()}};

    nlohmann::json modules = nlohmann::json::object();
    nlohmann::json settings = nlohmann::json::object();
    for (const modules::module* m : modules::module_registry::instance().all()) {
        modules[m->name()] = m->enabled();

        if (m->settings().size() == 0) {
            continue;
        }
        nlohmann::json block = nlohmann::json::object();
        m->settings().for_each([&block](const core::base_setting& s) {
            block[s.name()] = setting_to_json(s.to_value());
        });
        settings[m->name()] = std::move(block);
    }
    doc["modules"] = std::move(modules);
    doc["settings"] = std::move(settings);

    nlohmann::json keybinds = nlohmann::json::object();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& [name, code] : g_module_keybinds) {
            keybinds[name] = code;
        }
    }
    doc["keybinds"] = std::move(keybinds);

    std::FILE* f = std::fopen(path(), "wb");
    if (f == nullptr) {
        WOKE_WARN("config", "cannot open %s for writing", path());
        return false;
    }
    const std::string out = doc.dump();
    const std::size_t written = std::fwrite(out.data(), 1, out.size(), f);
    const bool ok = (written == out.size()) && std::fclose(f) == 0;
    if (!ok) {
        WOKE_WARN("config", "short write to %s", path());
        return false;
    }
    for (modules::module* m : modules::module_registry::instance().all()) {
        m->settings().clear_dirty();
    }
    WOKE_DEBUG("config", "config saved to %s (%zu bytes)", path(), out.size());
    core::event_bus::emit(core::config_persisted{path()});
    return true;
}

} // namespace woke::config

// ============================================================================
//  woke.wtf — src/core/config.cpp
//  Load/save implementation. Writes are atomic-ish (single fwrite of the
//  serialized document) and every failure path keeps in-memory state intact.
// ============================================================================
#include "core/config.hpp"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "core/logger.hpp"
#include "modules/module.hpp"

namespace woke::config {

namespace {

constexpr const char* kSchema = "woke.wtf/config.v1";
constexpr int kDefaultKeybind = 62;   // X11 keycode: Right Shift

std::mutex g_mutex;
int g_keybind = kDefaultKeybind;

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
    WOKE_INFO("config", "config loaded from %s (keybind=%d, %ld module states applied)",
              path(), keybind(), applied);
    return true;
}

bool save() {
    nlohmann::json doc;
    doc["schema"] = kSchema;
    doc["gui"] = {{"keybind", keybind()}};

    nlohmann::json modules = nlohmann::json::object();
    for (const modules::module* m : modules::module_registry::instance().all()) {
        modules[m->name()] = m->enabled();
    }
    doc["modules"] = std::move(modules);

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
    WOKE_DEBUG("config", "config saved to %s (%zu bytes)", path(), out.size());
    return true;
}

} // namespace woke::config

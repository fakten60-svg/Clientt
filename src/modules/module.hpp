// ============================================================================
//  woke.wtf — src/modules/module.hpp
//  Module framework: base class + process-wide registry.
//
//  A module is a named, categorized toggle with the full lifecycle hook set
//  from the blueprint and its own typed settings:
//
//    on_enable()  — activation side effects (e.g. read-modify-restore a
//                   client setting)
//    on_disable() — exact inverse of on_enable()
//    on_tick()    — per-present maintenance while enabled
//    on_render()  — draw calls while enabled; only runs when the ClickGUI is
//                   open, so a closed GUI still costs zero draw work
//
//  Settings live in a core::setting_group; the config engine serializes them
//  generically, so a module never writes load/save code.
//
//  Modules are client-state only: they mutate game state through
//  woke::game::game_state (cached JNI handles) and never generate, send, or
//  inspect network packets.
// ============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/setting.hpp"

namespace woke::game {
class game_state;
}

namespace woke::modules {

// The six module sections the ClickGUI sidebar renders, in display order.
// A category with no modules is still listed (with a 0 badge) — the sidebar
// mirrors the client's structure, not just what happens to be loaded.
inline constexpr const char* kCategories[] = {
    "Combat", "Mace", "Misc", "Movement", "Spear", "Visual",
};
inline constexpr std::size_t kCategoryCount = sizeof(kCategories) / sizeof(kCategories[0]);

class module {
public:
    module(std::string name, std::string category, std::string description);
    virtual ~module() = default;

    module(const module&) = delete;
    module& operator=(const module&) = delete;

    const std::string& name() const { return name_; }
    const std::string& category() const { return category_; }
    const std::string& description() const { return description_; }

    // Optional second line shown on the card next to the keybind badge.
    const std::string& keybind_label() const { return keybind_label_; }
    void set_keybind_label(std::string label) { keybind_label_ = std::move(label); }

    bool enabled() const { return enabled_; }

    // Flips the flag and runs the matching lifecycle hook. No-op when the
    // state does not change. Logging lives here so every toggle is visible
    // in the session log.
    void set_enabled(bool on);

    // Typed settings, serialized generically by the config engine.
    core::setting_group& settings() { return settings_; }
    const core::setting_group& settings() const { return settings_; }

    virtual void on_enable() {}
    virtual void on_disable() {}
    virtual void on_tick(game::game_state& gs) { (void)gs; }
    virtual void on_render() {}

protected:
    // Attaches one setting to this module. Called from the subclass
    // constructor, before any load.
    void register_setting(core::base_setting& s) { settings_.add(s); }

private:
    std::string name_;
    std::string category_;
    std::string description_;
    std::string keybind_label_;
    bool enabled_ = false;
    core::setting_group settings_;
};

class module_registry {
public:
    static module_registry& instance();

    // Registration order is the display order. find() is by exact name.
    void register_module(std::unique_ptr<module> m);
    module* find(const std::string& name) const;
    std::vector<module*> all() const;

    // Modules registered under one of the spec categories, in display order.
    std::vector<module*> by_category(const char* category) const;

    // true when the module exists (state may or may not have changed).
    bool set_enabled(const std::string& name, bool on);

    // Runs on_tick() for every enabled module. Cheap when all are disabled.
    void tick_all(game::game_state& gs);

    // Runs on_render() for every enabled module (ClickGUI open only).
    void render_all();

    // Destroys all modules (shutdown path).
    void clear();

private:
    module_registry() = default;

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<module>> modules_;
};

// Counts used by the sidebar badges.
int category_module_count(const char* category);
int category_enabled_count(const char* category);

// Registers the built-in set. Idempotent.
void register_builtins();

} // namespace woke::modules

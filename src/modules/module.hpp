// ============================================================================
//  woke.wtf — src/modules/module.hpp
//  Module framework: base class + process-wide registry.
//
//  A module is a named, categorized toggle with three lifecycle hooks:
//    on_enable()  — activation side effects (e.g. read-modify-restore a
//                   client setting)
//    on_disable() — exact inverse of on_enable()
//    on_tick()    — per-present-frame maintenance while enabled
//
//  Modules are client-state only: they mutate game state through
//  woke::game::game_state (cached JNI handles) and never generate, send, or
//  inspect network packets.
// ============================================================================
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace woke::game {
class game_state;
}

namespace woke::modules {

class module {
public:
    module(std::string name, std::string category, std::string description);
    virtual ~module() = default;

    module(const module&) = delete;
    module& operator=(const module&) = delete;

    const std::string& name() const { return name_; }
    const std::string& category() const { return category_; }
    const std::string& description() const { return description_; }

    bool enabled() const { return enabled_; }

    // Flips the flag and runs the matching lifecycle hook. No-op when the
    // state does not change. Logging lives here so every toggle is visible
    // in the session log.
    void set_enabled(bool on);

    virtual void on_enable() {}
    virtual void on_disable() {}
    virtual void on_tick(game::game_state& gs) { (void)gs; }

private:
    std::string name_;
    std::string category_;
    std::string description_;
    bool enabled_ = false;
};

class module_registry {
public:
    static module_registry& instance();

    // Registration order is the display order. find() is by exact name.
    void register_module(std::unique_ptr<module> m);
    module* find(const std::string& name) const;
    std::vector<module*> all() const;

    // true when the module exists (state may or may not have changed).
    bool set_enabled(const std::string& name, bool on);

    // Runs on_tick() for every enabled module. Cheap when all are disabled.
    void tick_all(game::game_state& gs);

    // Destroys all modules (shutdown path).
    void clear();

private:
    module_registry() = default;

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<module>> modules_;
};

// Registers the built-in set (HUD, Fullbright, Sprint). Idempotent.
void register_builtins();

} // namespace woke::modules

// ============================================================================
//  woke.wtf — src/modules/builtin.cpp
//  The built-in client-state modules:
//
//    HUD        Render    — session/overlay info inside the click-gui
//    Fullbright Render    — raises the gamma video setting; restores the
//                           previous value on disable (read-modify-restore)
//    Sprint     Movement  — keeps the player sprinting via Entity#setSprinting
//
//  All touches to game state go through woke::game::game_state — pure
//  client-state read/write, zero packet involvement (project scope).
// ============================================================================
#include "modules/builtin.hpp"

#include "core/logger.hpp"
#include "game/game_state.hpp"

namespace woke::modules {

namespace {

class hud_module final : public module {
public:
    hud_module()
        : module("HUD", "Render",
                 "Shows session metrics (presents, overhead) in the click-gui.") {}
};

class fullbright_module final : public module {
public:
    fullbright_module()
        : module("Fullbright", "Render",
                 "Raises the gamma video setting while enabled; restores the "
                 "previous value on disable.") {}

    void on_enable() override {
        auto& gs = game::game_state::instance();
        saved_gamma_ = gs.gamma();
        have_saved_ = true;
        if (!gs.set_gamma(kGamma)) {
            WOKE_WARN("module", "Fullbright: game gamma not reachable (world/menu state)");
        }
    }

    void on_disable() override {
        if (!have_saved_) {
            return;
        }
        auto& gs = game::game_state::instance();
        if (!gs.set_gamma(saved_gamma_)) {
            WOKE_WARN("module", "Fullbright: could not restore gamma to %.2f", saved_gamma_);
        }
        have_saved_ = false;
    }

private:
    static constexpr double kGamma = 16.0;   // Minecraft gamma maximum
    double saved_gamma_ = 0.0;
    bool have_saved_ = false;
};

class sprint_module final : public module {
public:
    sprint_module()
        : module("Sprint", "Movement",
                 "Keeps the player sprinting (client movement state only).") {}

    void on_tick(game::game_state& gs) override {
        if (!gs.is_sprinting()) {
            gs.set_sprinting(true);
        }
    }
};

} // namespace

void register_builtins() {
    auto& registry = module_registry::instance();
    if (registry.find("HUD") != nullptr) {
        return;   // idempotent
    }
    registry.register_module(std::make_unique<hud_module>());
    registry.register_module(std::make_unique<fullbright_module>());
    registry.register_module(std::make_unique<sprint_module>());
    WOKE_INFO("module", "registered %zu built-in modules (hud, fullbright, sprint)",
              registry.all().size());
}

} // namespace woke::modules

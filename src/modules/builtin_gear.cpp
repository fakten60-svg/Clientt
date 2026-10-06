// ============================================================================
//  woke.wtf — src/modules/builtin_gear.cpp
//  The weapon-specific automation modules (Mace + Spear categories).
//  Client-state only, per project scope:
//
//    Auto Mace   per-tick automation — performs the vanilla attack on the
//                crosshair target while the player is falling at least the
//                configured distance (the mace smite window); optionally
//                requires the mace and the full vanilla charge
//    Spear Lunge per-tick automation — attacks the crosshair target and
//                boosts the player's own velocity along the look vector
//                (Entity.setVelocity — the vanilla movement-state write the
//                knockback code uses) after a successful attack
//
//  No packet generation: the attack travels through the vanilla interaction
//  manager exactly like a mouse click and the velocity write is plain local
//  Entity state. Every toggle is a visible GUI switch.
// ============================================================================
#include "modules/builtin.hpp"

#include <chrono>
#include <cmath>

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "game/game_state.hpp"

namespace woke::modules {

namespace {

using game::game_state;

// ---- Mace: Auto Mace ----------------------------------------------------------

class auto_mace_module final : public module {
public:
    auto_mace_module()
        : module("Auto Mace", "Mace",
                 "Attacks the crosshair target while falling at least the "
                 "configured distance — timed for the mace smite window.") {
        register_setting(min_fall_);
        register_setting(cps_);
        register_setting(require_mace_);
        register_setting(require_charge_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        const long long interval_ns = 1000000000LL / static_cast<long long>(cps_.value());
        if (last_attack_.time_since_epoch().count() > 0 &&
            (now - last_attack_) < std::chrono::nanoseconds(interval_ns)) {
            return;   // rate limit
        }

        const double fall = gs.player_fall_distance();
        if (fall < 0.0 || fall < min_fall_.value()) {
            return;   // not falling (enough) — no smite window
        }
        if (require_mace_.value() && !gs.main_hand_item_is("MACE")) {
            return;
        }
        if (require_charge_.value() && gs.attack_cooldown_progress() < 0.99f) {
            return;   // respect the vanilla attack charge
        }

        game_state::combat_target_info data;
        if (!gs.combat_target(data, nullptr, 0) || !data.entity || !data.living ||
            !data.alive) {
            return;
        }
        if (gs.client_attack()) {
            last_attack_ = now;
            core::event_bus::emit(core::combat_attack_performed{"Auto Mace"});
        }
    }

    void on_disable() override { last_attack_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<double> min_fall_{"Min Fall Distance", "Required fall in blocks",
                                    1.5, 0.5, 10.0};
    core::setting<int> cps_{"CPS", "Attacks per second while enabled", 6, 1, 20};
    core::setting<bool> require_mace_{"Require Mace", "Only attack while a mace is held",
                                      true};
    core::setting<bool> require_charge_{"Require Full Charge",
                                        "Only attack when the vanilla cooldown is full",
                                        true};
    clock::time_point last_attack_{};
};

// ---- Spear: Spear Lunge ----------------------------------------------------------

class spear_lunge_module final : public module {
public:
    spear_lunge_module()
        : module("Spear Lunge", "Spear",
                 "Attacks the crosshair target and boosts the player's own "
                 "velocity along the look vector — the vanilla movement-state "
                 "write, no packets.") {
        register_setting(item_mode_);
        register_setting(strength_);
        register_setting(cooldown_ms_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        if (last_lunge_.time_since_epoch().count() > 0 &&
            (now - last_lunge_) < std::chrono::milliseconds(cooldown_ms_.value())) {
            return;   // rate limit: one lunge per window
        }
        if (item_mode_.value() == 1 && !gs.main_hand_item_is("TRIDENT")) {
            return;   // mode "Trident" requires the trident in hand
        }

        game_state::combat_target_info data;
        if (!gs.combat_target(data, nullptr, 0) || !data.entity || !data.living ||
            !data.alive) {
            return;
        }
        if (!gs.client_attack()) {
            return;
        }

        // Lunge along the local look vector: v = look * strength. The same
        // yaw/pitch-to-direction math the client camera uses, written through
        // Entity.setVelocity (movement state, not a packet).
        float yaw = 0.0f;
        float pitch = 0.0f;
        if (gs.player_rotation(yaw, pitch)) {
            constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
            const double yaw_rad = static_cast<double>(yaw) * kDegToRad;
            const double pitch_rad = static_cast<double>(pitch) * kDegToRad;
            const double s = strength_.value();
            const double vx = -std::sin(yaw_rad) * std::cos(pitch_rad) * s;
            const double vy = -std::sin(pitch_rad) * s;
            const double vz = std::cos(yaw_rad) * std::cos(pitch_rad) * s;
            gs.boost_player(vx, vy, vz);
        }
        last_lunge_ = now;
        core::event_bus::emit(core::combat_attack_performed{"Spear Lunge"});
    }

    void on_disable() override { last_lunge_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::mode_setting item_mode_{"Item", "Which held item triggers the lunge",
                                  {"Any", "Trident"}, 0};
    core::setting<double> strength_{"Lunge Strength", "Velocity boost along the look vector",
                                    1.2, 0.2, 3.0};
    core::setting<int> cooldown_ms_{"Cooldown", "Pause between lunges (ms)",
                                    1000, 100, 5000};
    clock::time_point last_lunge_{};
};

} // namespace

void register_gear_builtins() {
    auto& registry = module_registry::instance();
    if (registry.find("Auto Mace") != nullptr) {
        return;   // idempotent
    }
    registry.register_module(std::make_unique<auto_mace_module>());
    registry.register_module(std::make_unique<spear_lunge_module>());
    WOKE_INFO("module", "registered 2 weapon modules (Auto Mace, Spear Lunge)");
}

} // namespace woke::modules

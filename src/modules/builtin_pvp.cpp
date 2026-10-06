// ============================================================================
//  woke.wtf — src/modules/builtin_pvp.cpp
//  The PvP automation half of the Combat category (the six "classic" combat
//  modules live in builtin_combat.cpp). Client-state only, per project scope:
//
//    Triggerbot        per-tick automation — performs the vanilla attack the
//                      moment the crosshair rests on a valid target and the
//                      configured charge threshold is met
//    AimAssist         per-tick automation — steers the LOCAL view rotation
//                      toward the nearest target in reach, bounded by an FOV
//                      cone and a max degrees-per-tick budget (no snap, no
//                      packets; the rotation is the same local Entity state
//                      mouse-look writes)
//    Auto Hit Crystal  per-tick automation — scans the world for the nearest
//                      End Crystal in reach and attacks it through the
//                      vanilla attack call pair (crystals are not living, so
//                      no health gate applies)
//    Anchor Macro      per-tick automation — repeats the vanilla use-click
//                      (interactionManager.interactBlock with the crosshair
//                      BlockHitResult) while a respawn anchor is held; the
//                      game itself decides place / charge / detonate
//    SafeAnchor        per-tick watchdog — disables Anchor Macro once the
//                      player's health drops below the configured threshold
//                      (defense layer on top of the macro, client state only)
//
//  No packet generation: every attack/use travels through the vanilla
//  interaction manager exactly like a mouse click, and the aim writes are
//  plain local view state. Every toggle is a visible GUI switch.
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

// ---- Combat: Triggerbot ------------------------------------------------------

class triggerbot_module final : public module {
public:
    triggerbot_module()
        : module("Triggerbot", "Combat",
                 "Attacks the moment the crosshair rests on a valid target "
                 "and the vanilla charge passes the threshold.") {
        register_setting(min_charge_);
        register_setting(only_living_);
    }

    void on_tick(game_state& gs) override {
        const float charge = gs.attack_cooldown_progress();
        if (charge >= 0.0f && charge < static_cast<float>(min_charge_.value())) {
            return;   // below the configured vanilla charge
        }
        game_state::combat_target_info data;
        if (!gs.combat_target(data, nullptr, 0) || !data.entity || !data.alive) {
            return;   // crosshair is not on a usable target
        }
        if (only_living_.value() && !data.living) {
            return;
        }
        if (gs.client_attack()) {
            core::event_bus::emit(core::combat_attack_performed{"Triggerbot"});
        }
    }

private:
    core::setting<double> min_charge_{"Min Charge", "Required vanilla attack charge",
                                      0.95, 0.5, 1.0};
    core::setting<bool> only_living_{"Only Living", "Ignore non-living targets", true};
};

// ---- Combat: AimAssist ---------------------------------------------------------

class aim_assist_module final : public module {
public:
    aim_assist_module()
        : module("AimAssist", "Combat",
                 "Steers the local view toward the nearest target in reach — "
                 "bounded by an FOV cone and a max step per tick, never a "
                 "snap. Client rotation state only.") {
        register_setting(reach_);
        register_setting(strength_);
        register_setting(max_step_);
        register_setting(fov_limit_);
    }

    void on_tick(game_state& gs) override {
        game_state::combat_target_info info;
        if (!gs.nearest_combat_target(static_cast<float>(reach_.value()), info, nullptr, 0) ||
            !info.entity || !info.living || !info.alive || info.target == nullptr) {
            return;
        }
        // Measure first: the aim only engages inside the configured FOV cone,
        // then applies one bounded step (gain = fraction of the remaining
        // delta) — the per-tick budget keeps the movement human-visible.
        double delta = 0.0;
        const double gain = strength_.value();
        const double max_step = max_step_.value();
        if (!gs.aim_angle_to(info.target, max_step, gain, false, delta)) {
            return;
        }
        if (delta > fov_limit_.value()) {
            return;   // outside the cone — the player is not fighting it
        }
        gs.aim_angle_to(info.target, max_step, gain, true, delta);
    }

private:
    core::setting<double> reach_{"Reach", "Target acquisition range in blocks",
                                 4.0, 1.0, 6.0};
    core::setting<double> strength_{"Strength", "Fraction of the remaining angle per tick",
                                    0.3, 0.05, 1.0};
    core::setting<double> max_step_{"Max Step", "Hard cap of degrees per tick",
                                    15.0, 1.0, 60.0};
    core::setting<double> fov_limit_{"FOV Limit", "Only aim inside this cone (degrees)",
                                     30.0, 5.0, 180.0};
};

// ---- Combat: Auto Hit Crystal ---------------------------------------------------

class auto_hit_crystal_module final : public module {
public:
    auto_hit_crystal_module()
        : module("Auto Hit Crystal", "Combat",
                 "Attacks the nearest End Crystal in reach through the "
                 "vanilla attack call pair (crystal scan, no living gate).") {
        register_setting(reach_);
        register_setting(cps_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        const long long interval_ns = 1000000000LL / static_cast<long long>(cps_.value());
        if (last_attack_.time_since_epoch().count() > 0 &&
            (now - last_attack_) < std::chrono::nanoseconds(interval_ns)) {
            return;   // rate limit
        }

        game_state::combat_target_info info;
        if (!gs.nearest_crystal_target(static_cast<float>(reach_.value()), info, nullptr, 0) ||
            !info.entity || !info.alive || info.target == nullptr) {
            return;   // no crystal in reach (or the scan is unavailable)
        }
        if (gs.client_attack_entity(info.target)) {
            last_attack_ = now;
            core::event_bus::emit(core::combat_attack_performed{"Auto Hit Crystal"});
        }
    }

    void on_disable() override { last_attack_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<double> reach_{"Reach", "Crystal scan range in blocks", 4.0, 1.0, 6.0};
    core::setting<int> cps_{"CPS", "Attacks per second while enabled", 8, 1, 20};
    clock::time_point last_attack_{};
};

// ---- Combat: Anchor Macro --------------------------------------------------------

class anchor_macro_module final : public module {
public:
    anchor_macro_module()
        : module("Anchor Macro", "Combat",
                 "Repeats the vanilla use-click on the crosshair block while "
                 "a respawn anchor is held — the game decides place, charge "
                 "and detonate itself.") {
        register_setting(use_rate_);
        register_setting(only_anchor_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        const long long interval_ns = 1000000000LL / static_cast<long long>(use_rate_.value());
        if (last_use_.time_since_epoch().count() > 0 &&
            (now - last_use_) < std::chrono::nanoseconds(interval_ns)) {
            return;   // rate limit
        }
        if (only_anchor_.value() && !gs.main_hand_item_is("RESPAWN_ANCHOR")) {
            return;   // macro only while the anchor is actually held
        }
        if (gs.client_use_block()) {
            last_use_ = now;
        }
    }

    void on_disable() override { last_use_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<int> use_rate_{"Use Rate", "Use-clicks per second while enabled",
                                 4, 1, 20};
    core::setting<bool> only_anchor_{"Only Holding Anchor",
                                     "Require a respawn anchor in the main hand", true};
    clock::time_point last_use_{};
};

// ---- Combat: SafeAnchor -----------------------------------------------------------

class safe_anchor_module final : public module {
public:
    safe_anchor_module()
        : module("SafeAnchor", "Combat",
                 "Watchdog: turns Anchor Macro off once the player's health "
                 "drops below the threshold — no more detonations at low HP.") {
        register_setting(min_health_);
        register_setting(recheck_ms_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        if (last_check_.time_since_epoch().count() > 0 &&
            (now - last_check_) < std::chrono::milliseconds(recheck_ms_.value())) {
            return;   // rate limit: the probe reads player state
        }
        last_check_ = now;

        const float health = gs.player_health();
        if (health < 0.0f || health >= static_cast<float>(min_health_.value())) {
            return;   // healthy (or the read is unavailable)
        }
        module* anchor = module_registry::instance().find("Anchor Macro");
        if (anchor == nullptr || !anchor->enabled()) {
            return;   // nothing to guard
        }
        // Flips the toggle through the registry: lifecycle hook, session log
        // and the dashboard toast all fire like a manual switch-off.
        module_registry::instance().set_enabled("Anchor Macro", false);
        WOKE_INFO("module", "SafeAnchor: health %.1f below %.1f — Anchor Macro disabled",
                  static_cast<double>(health), min_health_.value());
    }

    void on_disable() override { last_check_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<double> min_health_{"Min Health", "Disable Anchor Macro below this HP",
                                      6.0, 1.0, 19.0};
    core::setting<int> recheck_ms_{"Recheck Delay", "Pause between health checks (ms)",
                                   250, 50, 2000};
    clock::time_point last_check_{};
};

} // namespace

void register_pvp_builtins() {
    auto& registry = module_registry::instance();
    if (registry.find("Triggerbot") != nullptr) {
        return;   // idempotent
    }
    registry.register_module(std::make_unique<triggerbot_module>());
    registry.register_module(std::make_unique<aim_assist_module>());
    registry.register_module(std::make_unique<auto_hit_crystal_module>());
    registry.register_module(std::make_unique<anchor_macro_module>());
    registry.register_module(std::make_unique<safe_anchor_module>());
    WOKE_INFO("module", "registered 5 pvp combat modules (Triggerbot, AimAssist, "
                        "Auto Hit Crystal, Anchor Macro, SafeAnchor)");
}

} // namespace woke::modules

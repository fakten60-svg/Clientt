// ============================================================================
//  woke.wtf — src/modules/builtin_macro.cpp
//  The macro-automation combat modules (own TU to keep every file within the
//  hygiene size). Client-state only, per project scope:
//
//    Safe Anchor Macro  per-tick automation — wraps the Anchor Macro flow:
//                       before every detonation volley it swaps to a Glowstone
//                       hotbar slot, places one block between the player and
//                       the anchor through the vanilla use-click, swaps back
//                       and only then runs the anchor use-clicks, so the
//                       explosion is shielded
//    Shield Breaker     per-tick automation — attacks the crosshair target
//                       the moment it raises a shield (isUsingItem) while an
//                       axe is held; the vanilla axe attack disables shields
//    Pearl Catch        per-tick automation — throws an ender pearl, then
//                       scans the world for the pearl entity, computes the
//                       interception angle each tick and fires a wind charge
//                       once the view is on target (the wind-charge blast
//                       knocks the pearl back mid-air)
//
//  No packet generation: every use/attack travels through the vanilla
//  interaction manager exactly like a mouse click, and the rotation writes
//  are plain local Entity state. Every toggle is a visible GUI switch.
// ============================================================================
#include "modules/builtin.hpp"

#include <chrono>
#include <cstddef>

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "game/game_state.hpp"

namespace woke::modules {

namespace {

using game::game_state;

// ---- Combat: Safe Anchor Macro ---------------------------------------------------

class safe_anchor_macro_module final : public module {
public:
    safe_anchor_macro_module()
        : module("Safe Anchor Macro", "Combat",
                 "Anchor Macro with a Glowstone shield: before every "
                 "detonation volley it places one Glowstone between the "
                 "player and the anchor, swaps back to the anchor and only "
                 "then runs the use-clicks.") {
        register_setting(use_rate_);
        register_setting(place_delay_ms_);
        register_setting(detonate_delay_ms_);
        register_setting(max_uses_);
        register_setting(require_anchor_);
    }

    void on_tick(game_state& gs) override {
        switch (stage_) {
            case stage::idle:
                run_idle_stage(gs);
                break;
            case stage::glow:
                run_glow_stage(gs);
                break;
            case stage::swap_back:
                run_swap_stage(gs);
                break;
            case stage::detonate:
                run_detonate_stage(gs);
                break;
        }
    }

    void on_disable() override { reset(); }

private:
    enum class stage { idle, glow, swap_back, detonate };
    using clock = std::chrono::steady_clock;

    bool elapsed_ms(const clock::time_point& since, int ms) const {
        return since.time_since_epoch().count() > 0 &&
               (clock::now() - since) >= std::chrono::milliseconds(ms);
    }

    void reset() {
        stage_ = stage::idle;
        glow_slot_ = -1;
        anchor_slot_ = -1;
        placed_ = false;
        uses_ = 0;
        stamp_ = clock::time_point{};
        last_use_ = clock::time_point{};
    }

    // idle: an anchor in hand + a Glowstone in the hotbar start a cycle by
    // selecting the Glowstone (the game then holds it, like a number key).
    void run_idle_stage(game_state& gs) {
        if (require_anchor_.value() && !gs.main_hand_item_is("RESPAWN_ANCHOR")) {
            return;
        }
        if (!gs.find_inventory_slot("GLOWSTONE", true, glow_slot_)) {
            return;   // no Glowstone carried — nothing to shield the blast with
        }
        if (!gs.find_inventory_slot("RESPAWN_ANCHOR", true, anchor_slot_)) {
            return;   // anchor not in the hotbar — cannot swap back reliably
        }
        if (!gs.select_hotbar_slot(glow_slot_)) {
            return;
        }
        stage_ = stage::glow;
        placed_ = false;
        stamp_ = clock::now();
    }

    // glow: one vanilla use-click places the Glowstone on the crosshair face
    // (between the player and the anchor), then the anchor slot comes back.
    void run_glow_stage(game_state& gs) {
        if (!placed_ && gs.client_use_block()) {
            placed_ = true;
        }
        if (placed_ && elapsed_ms(stamp_, place_delay_ms_.value())) {
            gs.select_hotbar_slot(anchor_slot_);
            stage_ = stage::swap_back;
            stamp_ = clock::now();
        }
    }

    // swap_back: brief settle window so the game registers the held item.
    void run_swap_stage(game_state&) {
        if (elapsed_ms(stamp_, detonate_delay_ms_.value())) {
            stage_ = stage::detonate;
            uses_ = 0;
            last_use_ = clock::time_point{};
        }
    }

    // detonate: the Anchor Macro use-click volley, capped per cycle; when it
    // ends (or the anchor is gone) the next cycle re-places a Glowstone.
    void run_detonate_stage(game_state& gs) {
        if (require_anchor_.value() && !gs.main_hand_item_is("RESPAWN_ANCHOR")) {
            reset();
            return;
        }
        const auto now = clock::now();
        const long long interval_ns = 1000000000LL / static_cast<long long>(use_rate_.value());
        if (last_use_.time_since_epoch().count() > 0 &&
            (now - last_use_) < std::chrono::nanoseconds(interval_ns)) {
            return;   // rate limit
        }
        if (gs.client_use_block()) {
            last_use_ = now;
            ++uses_;
            if (uses_ >= max_uses_.value()) {
                reset();
            }
        }
    }

    core::setting<int> use_rate_{"Use Rate", "Detonation use-clicks per second",
                                 6, 1, 20};
    core::setting<int> place_delay_ms_{"Place Delay", "Pause after the Glowstone place (ms)",
                                       300, 50, 2000};
    core::setting<int> detonate_delay_ms_{"Detonate Delay",
                                          "Pause after swapping back to the anchor (ms)",
                                          200, 50, 2000};
    core::setting<int> max_uses_{"Max Uses", "Anchor use-clicks per Glowstone layer",
                                 4, 1, 10};
    core::setting<bool> require_anchor_{"Only Holding Anchor",
                                        "Require a respawn anchor in the main hand", true};

    stage stage_ = stage::idle;
    int glow_slot_ = -1;
    int anchor_slot_ = -1;
    bool placed_ = false;
    int uses_ = 0;
    clock::time_point stamp_{};
    clock::time_point last_use_{};
};

// ---- Combat: Shield Breaker ------------------------------------------------------

// The four axe tiers: the vanilla axe attack is what disables a raised shield.
constexpr const char* kShieldBreakerAxes[] = {
    "NETHERITE_AXE", "DIAMOND_AXE", "IRON_AXE", "GOLDEN_AXE",
};

class shield_breaker_module final : public module {
public:
    shield_breaker_module()
        : module("Shield Breaker", "Combat",
                 "Attacks the crosshair target the moment it raises a shield "
                 "while an axe is held — the vanilla axe attack disables it.") {
        register_setting(cooldown_ms_);
        register_setting(min_charge_);
        register_setting(require_axe_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        if (last_break_.time_since_epoch().count() > 0 &&
            (now - last_break_) < std::chrono::milliseconds(cooldown_ms_.value())) {
            return;   // rate limit
        }
        // The target must be blocking right now (LivingEntity.isUsingItem on
        // the crosshair entity — the same state a raised shield shows).
        if (!gs.crosshair_target_using_item()) {
            return;
        }
        if (require_axe_.value() &&
            !gs.main_hand_item_any(kShieldBreakerAxes,
                                   sizeof(kShieldBreakerAxes) / sizeof(kShieldBreakerAxes[0]))) {
            return;   // only the axe tiers break shields
        }
        if (gs.attack_cooldown_progress() < static_cast<float>(min_charge_.value())) {
            return;   // respect the vanilla attack charge
        }
        if (gs.client_attack()) {
            last_break_ = now;
            core::event_bus::emit(core::combat_attack_performed{"Shield Breaker"});
        }
    }

    void on_disable() override { last_break_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<int> cooldown_ms_{"Cooldown", "Pause between break attempts (ms)",
                                    600, 100, 5000};
    core::setting<double> min_charge_{"Min Charge", "Required vanilla attack charge",
                                      0.9, 0.5, 1.0};
    core::setting<bool> require_axe_{"Require Axe", "Only break while an axe is held", true};
    clock::time_point last_break_{};
};

// ---- Combat: Pearl Catch ---------------------------------------------------------

class pearl_catch_module final : public module {
public:
    pearl_catch_module()
        : module("Pearl Catch", "Combat",
                 "Throws an ender pearl, then scans the world for the pearl "
                 "entity, computes the interception angle each tick and fires "
                 "a wind charge once the view is on target — the blast knocks "
                 "the pearl back mid-air.") {
        register_setting(cooldown_ms_);
        register_setting(reach_);
        register_setting(aim_gain_);
        register_setting(max_step_);
        register_setting(aim_threshold_);
    }

    void on_tick(game_state& gs) override {
        // Phase 1: a pearl is already in flight — aim at it and fire the
        // wind charge once the computed angle is on target.
        game_state::combat_target_info pearl;
        if (gs.nearest_entity_of_class(static_cast<float>(reach_.value()),
                                       "net/minecraft/entity/projectile/thrown/EnderPearlEntity",
                                       pearl) &&
            pearl.entity && pearl.target != nullptr) {
            double delta = 0.0;
            if (!gs.aim_at_entity(pearl.target, max_step_.value(), aim_gain_.value(), true,
                                  delta)) {
                return;
            }
            int wind_slot = -1;
            if (!gs.find_inventory_slot("WIND_CHARGE", true, wind_slot)) {
                return;   // no wind charge in the hotbar — nothing to catch with
            }
            gs.select_hotbar_slot(wind_slot);
            if (delta > aim_threshold_.value()) {
                return;   // still turning — fire next tick
            }
            const auto now = clock::now();
            if (last_shot_.time_since_epoch().count() > 0 &&
                (now - last_shot_) < std::chrono::milliseconds(cooldown_ms_.value())) {
                return;   // do not waste charges on one pearl
            }
            if (gs.client_use_item()) {
                last_shot_ = now;
            }
            return;
        }

        // Phase 2: no pearl in flight — throw one to start the catch cycle.
        const auto now = clock::now();
        if (last_throw_.time_since_epoch().count() > 0 &&
            (now - last_throw_) < std::chrono::milliseconds(cooldown_ms_.value())) {
            return;   // rate limit the throws
        }
        int pearl_slot = -1;
        if (!gs.find_inventory_slot("ENDER_PEARL", true, pearl_slot)) {
            return;
        }
        if (gs.select_hotbar_slot(pearl_slot) && gs.client_use_item()) {
            last_throw_ = now;
        }
    }

    void on_disable() override {
        last_throw_ = clock::time_point{};
        last_shot_ = clock::time_point{};
    }

private:
    using clock = std::chrono::steady_clock;

    core::setting<int> cooldown_ms_{"Cooldown", "Pause between throws / catch shots (ms)",
                                    800, 100, 5000};
    core::setting<double> reach_{"Reach", "Pearl scan range in blocks", 48.0, 8.0, 96.0};
    core::setting<double> aim_gain_{"Aim Strength", "Fraction of the remaining angle per tick",
                                    0.9, 0.1, 1.0};
    core::setting<double> max_step_{"Max Step", "Hard cap of degrees per tick", 30.0, 5.0, 90.0};
    core::setting<double> aim_threshold_{"Fire Threshold",
                                         "Fire the wind charge within this aim error (deg)",
                                         6.0, 1.0, 45.0};
    clock::time_point last_throw_{};
    clock::time_point last_shot_{};
};

} // namespace

void register_macro_builtins() {
    auto& registry = module_registry::instance();
    if (registry.find("Safe Anchor Macro") != nullptr) {
        return;   // idempotent
    }
    registry.register_module(std::make_unique<safe_anchor_macro_module>());
    registry.register_module(std::make_unique<shield_breaker_module>());
    registry.register_module(std::make_unique<pearl_catch_module>());
    WOKE_INFO("module", "registered 3 macro combat modules (Safe Anchor Macro, "
                        "Shield Breaker, Pearl Catch)");
}

} // namespace woke::modules

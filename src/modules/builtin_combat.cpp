// ============================================================================
//  woke.wtf — src/modules/builtin_combat.cpp
//  The built-in Combat modules. Client-state only, per the project scope:
//
//    Target HUD       render-only — shows the entity under the crosshair
//                     (type + health bar) from the client's own raycast
//    Attack Cooldown  render-only — vanilla attack-charge indicator near the
//                     crosshair
//    Auto Clicker     per-tick automation — performs the vanilla attack call
//                     pair (attackEntity + swingHand) on the crosshair target
//                     at a configurable rate; optionally waits for the
//                     vanilla attack charge, so it never out-clicks the game
//                     mechanics
//    KillAura         per-tick automation — attacks the NEAREST entity in
//                     reach through the same vanilla call pair (no aim
//                     assist, no rotation snap; you still have to aim)
//    W-Tap            per-tick automation — taps sprint off/on around the
//                     vanilla attack for the vanilla sprint-knockback bonus;
//                     reuses the existing sprint client-state path
//    Auto Totem       per-tick automation — swaps a totem of undying from
//                     the main inventory into the offhand through the
//                     vanilla clickSlot(SWAP) inventory exchange
//
//  No packet generation, no aim assist: every attack travels through the
//  vanilla interaction manager exactly like a mouse click, and every toggle
//  is a visible GUI switch (usage scope: private/QoL testing).
// ============================================================================
#include "modules/builtin.hpp"

#include <chrono>
#include <cstdio>

#include <imgui.h>

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "game/game_state.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"

namespace woke::modules {

namespace {

using game::game_state;

// ---- Combat: Target HUD ----------------------------------------------------

class target_hud_module final : public module {
public:
    target_hud_module()
        : module("Target HUD", "Combat",
                 "Shows the entity under the crosshair (type + health) using "
                 "the client's own raycast.") {
        register_setting(show_name_);
        register_setting(show_max_);
    }

    void on_render() override {
        auto& gs = game_state::instance();
        game_state::combat_target_info data;
        char name[64] = {};
        if (!gs.combat_target(data, name, sizeof name) || !data.entity) {
            return;
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImGuiIO& io = ImGui::GetIO();
        const float w = 150.0f;
        const float h = data.living ? 38.0f : 22.0f;
        const float x = io.DisplaySize.x * 0.5f - w * 0.5f;
        const float y = io.DisplaySize.y * 0.5f + 18.0f;

        const ImVec2 min(x, y);
        const ImVec2 max(x + w, y + h);
        utils::render::shadow(dl, min, max, 6.0f, 8.0f, 0.30f);
        utils::render::rounded_rect(dl, min, max, 6.0f,
                                    utils::with_alpha(ui::theme::window_bg, 0.90f));

        if (show_name_.value() && name[0] != '\0') {
            utils::render::text_ellipsized(dl, ImVec2(x + 8.0f, y + 5.0f),
                                           ui::theme::text_primary, name, w - 16.0f);
        }

        if (data.living) {
            const float frac = (data.max_health > 0.0f)
                                   ? utils::clamp01(data.health / data.max_health)
                                   : 0.0f;
            const float bar_y = y + (show_name_.value() ? 24.0f : 6.0f);
            const float bar_w = w - 16.0f;
            utils::render::rounded_rect(dl, ImVec2(x + 8.0f, bar_y),
                                        ImVec2(x + 8.0f + bar_w, bar_y + 6.0f), 3.0f,
                                        utils::with_alpha(ui::theme::badge_bg, 0.90f));
            if (frac > 0.001f) {
                utils::render::gradient_h(dl, ImVec2(x + 8.0f, bar_y),
                                          ImVec2(x + 8.0f + bar_w * frac, bar_y + 6.0f),
                                          utils::with_alpha(ui::theme::traffic_close, 0.95f),
                                          utils::with_alpha(ui::theme::traffic_zoom, 0.95f),
                                          3.0f);
            }
            if (show_max_.value()) {
                char hp[32];                std::snprintf(hp, sizeof hp, "%.0f / %.0f",
                              static_cast<double>(data.health),
                              static_cast<double>(data.max_health));
                const float tw = utils::render::text_width(hp);
                dl->AddText(ImVec2(x + w * 0.5f - tw * 0.5f, bar_y + 8.0f),
                            utils::with_alpha(ui::theme::text_muted, 0.95f), hp);
            }
        }
    }

private:
    core::setting<bool> show_name_{"Show Name", "Show the target type above the bar", true};
    core::setting<bool> show_max_{"Show Health", "Print the health numbers", true};
};

// ---- Combat: Attack Cooldown -------------------------------------------------

class attack_cooldown_module final : public module {
public:
    attack_cooldown_module()
        : module("Attack Cooldown", "Combat",
                 "Draws the vanilla attack-charge indicator near the crosshair "
                 "(full bar = next hit at full damage).") {
        register_setting(show_percent_);
    }

    void on_render() override {
        const float progress = game_state::instance().attack_cooldown_progress();
        if (progress < 0.0f) {
            return;   // cooldown API not reachable (menu, fixture, mappings)
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImGuiIO& io = ImGui::GetIO();
        const float bar_w = 46.0f;
        const float bar_h = 4.0f;
        const float x = io.DisplaySize.x * 0.5f - bar_w * 0.5f;
        const float y = io.DisplaySize.y * 0.5f + 8.0f;

        utils::render::rounded_rect(dl, ImVec2(x, y), ImVec2(x + bar_w, y + bar_h), 2.0f,
                                    utils::with_alpha(ui::theme::badge_bg, 0.85f));
        const float filled = bar_w * utils::clamp01(progress);
        if (filled > 0.5f) {
            const ImU32 front = (progress >= 0.999f) ? ui::theme::traffic_zoom
                                                     : ui::theme::accent;
            utils::render::rounded_rect(dl, ImVec2(x, y), ImVec2(x + filled, y + bar_h), 2.0f,
                                        utils::with_alpha(front, 0.95f));
        }
        if (show_percent_.value()) {
            char pct[16];
            std::snprintf(pct, sizeof pct, "%d%%",
                          static_cast<int>(utils::clamp01(progress) * 100.0f + 0.5f));
            const float tw = utils::render::text_width(pct);
            dl->AddText(ImVec2(x + bar_w * 0.5f - tw * 0.5f, y + bar_h + 2.0f),
                        utils::with_alpha(ui::theme::text_muted, 0.95f), pct);
        }
    }

private:
    core::setting<bool> show_percent_{"Show Percent", "Print the charge percentage", false};
};

// ---- Combat: Auto Clicker -----------------------------------------------------

class auto_clicker_module final : public module {
public:
    auto_clicker_module()
        : module("Auto Clicker", "Combat",
                 "Repeats the vanilla attack (interactionManager.attackEntity + "
                 "swing) on the entity under the crosshair at a fixed rate. "
                 "No packets of its own, no target scan.") {
        register_setting(cps_);
        register_setting(require_charge_);
    }

    void on_tick(game_state& gs) override {
        const auto now = std::chrono::steady_clock::now();
        const long long interval_ns = 1000000000LL / static_cast<long long>(cps_.value());
        if (last_attack_.time_since_epoch().count() > 0 &&
            (now - last_attack_) < std::chrono::nanoseconds(interval_ns)) {
            return;   // rate limit
        }
        if (require_charge_.value() && gs.attack_cooldown_progress() < 0.99f) {
            return;   // respect the vanilla attack charge
        }
        if (gs.client_attack()) {
            last_attack_ = now;
            core::event_bus::emit(core::combat_attack_performed{"Auto Clicker"});
        }
    }

    void on_disable() override { last_attack_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<int> cps_{"CPS", "Attacks per second while enabled", 8, 1, 20};
    core::setting<bool> require_charge_{"Require Full Charge",
                                        "Only attack when the vanilla cooldown is full", true};
    clock::time_point last_attack_{};
};

// ---- Combat: KillAura --------------------------------------------------------

class kill_aura_module final : public module {
public:
    kill_aura_module()
        : module("KillAura", "Combat",
                 "Attacks the nearest entity in reach through the vanilla "
                 "attack call pair. No aim assist — you still aim yourself.") {
        register_setting(reach_);
        register_setting(cps_);
        register_setting(require_charge_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        const long long interval_ns = 1000000000LL / static_cast<long long>(cps_.value());
        if (last_attack_.time_since_epoch().count() > 0 &&
            (now - last_attack_) < std::chrono::nanoseconds(interval_ns)) {
            return;   // rate limit
        }
        if (require_charge_.value() && gs.attack_cooldown_progress() < 0.99f) {
            return;   // respect the vanilla attack charge
        }

        game_state::combat_target_info info;
        if (!gs.nearest_combat_target(static_cast<float>(reach_.value()), info, nullptr, 0) ||
            !info.entity || !info.living || !info.alive || info.target == nullptr) {
            return;   // nothing in reach (or the scan is unavailable)
        }
        if (gs.client_attack_entity(info.target)) {
            last_attack_ = now;
            core::event_bus::emit(core::combat_attack_performed{"KillAura"});
        }
    }

    void on_disable() override { last_attack_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<double> reach_{"Reach", "Attack range in blocks", 4.0, 1.0, 6.0};
    core::setting<int> cps_{"CPS", "Attacks per second while enabled", 6, 1, 20};
    core::setting<bool> require_charge_{"Require Full Charge",
                                        "Only attack when the vanilla cooldown is full", true};
    clock::time_point last_attack_{};
};

// ---- Combat: W-Tap ------------------------------------------------------------

class w_tap_module final : public module {
public:
    w_tap_module()
        : module("W-Tap", "Combat",
                 "Taps sprint off/on around the vanilla attack for the vanilla "
                 "sprint-knockback bonus (client movement state only).") {
        register_setting(tap_ms_);
    }

    void on_tick(game_state& gs) override {
        if (!tap_active_) {
            return;   // the attack hook flips this on
        }
        const auto now = clock::now();
        const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(now - tap_start_);
        if (held < std::chrono::milliseconds(tap_ms_.value())) {
            return;
        }
        // Sprint back on; the vanilla client re-asserts forward movement itself.
        gs.set_sprinting(true);
        tap_active_ = false;
    }

    // Called (via the event bus) right after an automated attack went out:
    // drop sprint for a short window, then re-assert it (vanilla W-tap).
    void request_tap(game_state& gs, const char* /*attacker*/) {
        if (!enabled() || tap_active_) {
            return;
        }
        if (gs.is_sprinting()) {
            gs.set_sprinting(false);
            tap_active_ = true;
            tap_start_ = clock::now();
        }
    }

    void on_disable() override {
        tap_active_ = false;
    }

private:
    using clock = std::chrono::steady_clock;

    core::setting<int> tap_ms_{"Tap Duration", "How long sprint stays off (ms)", 80, 20, 250};

    bool tap_active_ = false;
    clock::time_point tap_start_{};
};

// ---- Combat: Auto Totem -------------------------------------------------------

class auto_totem_module final : public module {
public:
    auto_totem_module()
        : module("Auto Totem", "Combat",
                 "Restocks the offhand with a totem of undying through the "
                 "vanilla inventory swap whenever it is empty.") {
        register_setting(recheck_ms_);
    }

    void on_tick(game_state& gs) override {
        const auto now = clock::now();
        if (last_attempt_.time_since_epoch().count() > 0 &&
            (now - last_attempt_) < std::chrono::milliseconds(recheck_ms_.value())) {
            return;   // rate limit: the offhand probe reads the inventory
        }
        last_attempt_ = now;
        if (gs.offhand_totem()) {
            return;   // offhand is stocked with a totem
        }
        gs.move_totem_to_offhand();
    }

    void on_disable() override { last_attempt_ = clock::time_point{}; }

private:
    using clock = std::chrono::steady_clock;

    core::setting<int> recheck_ms_{"Recheck Delay", "Pause between offhand checks (ms)",
                                   200, 10, 1000};
    clock::time_point last_attempt_{};
};

// Shared by the attack automations: one W-Tap instance serves them all.
w_tap_module* g_w_tap = nullptr;

// Auto Clicker + KillAura publish combat_attack_performed after each vanilla
// attack; the W-Tap module listens and does its sprint tap. Decoupled via the
// event bus — neither attacker knows about W-Tap.
void install_wtap_listener() {
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    core::event_bus::subscribe<core::combat_attack_performed>(
        [](const core::combat_attack_performed& e) {
            if (g_w_tap != nullptr) {
                g_w_tap->request_tap(game_state::instance(), e.module);
            }
        });
}

} // namespace

void register_combat_builtins() {
    auto& registry = module_registry::instance();
    if (registry.find("Target HUD") != nullptr) {
        return;   // idempotent
    }
    auto w_tap = std::make_unique<w_tap_module>();
    g_w_tap = w_tap.get();
    registry.register_module(std::make_unique<target_hud_module>());
    registry.register_module(std::make_unique<attack_cooldown_module>());
    registry.register_module(std::make_unique<auto_clicker_module>());
    registry.register_module(std::make_unique<kill_aura_module>());
    registry.register_module(std::move(w_tap));
    registry.register_module(std::make_unique<auto_totem_module>());
    install_wtap_listener();
    WOKE_INFO("module", "registered 6 combat modules (Target HUD, Attack Cooldown, Auto "
                        "Clicker, KillAura, W-Tap, Auto Totem)");
}

} // namespace woke::modules

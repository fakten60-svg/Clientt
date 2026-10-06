// ============================================================================
//  woke.wtf — src/modules/builtin.cpp
//  The built-in client-state modules:
//
//    HUD        Visual    — watermark/fps overlay drawn while enabled
//    Fullbright Visual    — raises the gamma video setting; restores the
//                           previous value on disable (read-modify-restore)
//    Zoom       Visual    — narrows the FOV video setting; restores on disable
//    Sprint     Movement  — keeps the player sprinting via Entity#setSprinting
//    Sneak      Movement  — keeps the player sneaking via Entity#setSneaking
//
//  The combat, pvp and weapon sets live in builtin_combat.cpp,
//  builtin_pvp.cpp and builtin_gear.cpp; register_builtins() chains all
//  four registration passes (display order follows the call order).
//
//  All touches to game state go through woke::game::game_state — pure
//  client-state read/write, zero packet involvement (project scope). Each
//  module declares its tunables as core::setting<T>, which the config engine
//  serializes without any per-module load/save code.
// ============================================================================
#include "modules/builtin.hpp"

#include <cstdio>

#include <imgui.h>

#include "core/config.hpp"
#include "core/logger.hpp"
#include "game/game_state.hpp"
#include "hook/present_hook.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"

namespace woke::modules {

namespace {

// ---- Visual: HUD -----------------------------------------------------------

class hud_module final : public module {
public:
    hud_module()
        : module("HUD", "Visual",
                 "Draws the session watermark (fps, present overhead) on screen.") {
        register_setting(watermark_);
        register_setting(show_fps_);
        register_setting(corner_);
    }

    void on_render() override {
        if (!watermark_.value()) {
            return;
        }
        const float pad = 12.0f;
        const ImGuiIO& io = ImGui::GetIO();
        float x = pad;
        float y = pad;
        switch (corner_.value()) {
            case 0: x = pad; y = pad; break;
            case 1: x = io.DisplaySize.x - 210.0f; y = pad; break;
            case 2: x = pad; y = io.DisplaySize.y - 74.0f; break;
            default: x = io.DisplaySize.x - 210.0f; y = io.DisplaySize.y - 74.0f; break;
        }

        char line[64];
        if (show_fps_.value()) {
            std::snprintf(line, sizeof line, "woke.wtf  |  %d fps", game::game_state::instance().current_fps());
        } else {
            std::snprintf(line, sizeof line, "woke.wtf");
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 min(x, y);
        const ImVec2 max(x + 190.0f, y + 24.0f);
        utils::render::shadow(dl, min, max, 6.0f, 10.0f, 0.30f);
        utils::render::gradient_h(dl, min, max,
                                  utils::with_alpha(ui::theme::window_bg, 0.92f),
                                  utils::with_alpha(ui::theme::accent, 0.55f), 6.0f);
        dl->AddText(ImVec2(min.x + 10.0f, min.y + 5.0f), ui::theme::text_primary, line);
    }

private:
    core::setting<bool> watermark_{"Watermark", "Draw the on-screen watermark", true};
    core::setting<bool> show_fps_{"Show FPS", "Include the client FPS readout", true};
    core::mode_setting corner_{"Corner", "Watermark position on screen",
                               {"Top Left", "Top Right", "Bottom Left", "Bottom Right"}, 0};
};

// ---- Visual: Fullbright ----------------------------------------------------

class fullbright_module final : public module {
public:
    fullbright_module()
        : module("Fullbright", "Visual",
                 "Raises the gamma video setting while enabled; restores the "
                 "previous value on disable.") {
        register_setting(gamma_);
        register_setting(restore_);
    }

    void on_enable() override {
        auto& gs = game::game_state::instance();
        saved_gamma_ = gs.gamma();
        have_saved_ = true;
        if (!gs.set_gamma(gamma_.value())) {
            WOKE_WARN("module", "Fullbright: game gamma not reachable (world/menu state)");
        }
    }

    void on_disable() override {
        if (!have_saved_) {
            return;
        }
        if (restore_.value()) {
            auto& gs = game::game_state::instance();
            if (!gs.set_gamma(saved_gamma_)) {
                WOKE_WARN("module", "Fullbright: could not restore gamma to %.2f", saved_gamma_);
            }
        }
        have_saved_ = false;
    }

private:
    core::setting<double> gamma_{"Gamma", "Target gamma while enabled", 16.0, 1.0, 24.0};
    core::setting<bool> restore_{"Restore", "Restore the previous gamma on disable", true};
    double saved_gamma_ = 0.0;
    bool have_saved_ = false;
};

// ---- Visual: Zoom ----------------------------------------------------------

class zoom_module final : public module {
public:
    zoom_module()
        : module("Zoom", "Visual",
                 "Narrows the FOV video setting while enabled; restores the "
                 "previous value on disable.") {
        register_setting(fov_);
    }

    void on_enable() override {
        auto& gs = game::game_state::instance();
        saved_fov_ = gs.fov();
        have_saved_ = true;
        if (!gs.set_fov(fov_.value())) {
            WOKE_WARN("module", "Zoom: game fov not reachable (world/menu state)");
        }
    }

    void on_disable() override {
        if (!have_saved_) {
            return;
        }
        auto& gs = game::game_state::instance();
        if (!gs.set_fov(saved_fov_)) {
            WOKE_WARN("module", "Zoom: could not restore fov to %d", saved_fov_);
        }
        have_saved_ = false;
    }

private:
    core::setting<int> fov_{"FOV", "Field of view while enabled", 30, 10, 110};
    int saved_fov_ = 0;
    bool have_saved_ = false;
};

// ---- Movement: Sprint ------------------------------------------------------

class sprint_module final : public module {
public:
    sprint_module()
        : module("Sprint", "Movement",
                 "Keeps the player sprinting (client movement state only).") {
        register_setting(keep_alive_);
    }

    void on_tick(game::game_state& gs) override {
        if (!keep_alive_.value()) {
            return;
        }
        if (!gs.is_sprinting()) {
            gs.set_sprinting(true);
        }
    }

private:
    core::setting<bool> keep_alive_{"Keep Alive", "Re-assert sprinting every tick", true};
};

// ---- Movement: Sneak -------------------------------------------------------

class sneak_module final : public module {
public:
    sneak_module()
        : module("Sneak", "Movement",
                 "Keeps the player sneaking (client movement state only).") {
        register_setting(keep_alive_);
    }

    void on_tick(game::game_state& gs) override {
        if (!keep_alive_.value()) {
            return;
        }
        if (!gs.is_sneaking()) {
            gs.set_sneaking(true);
        }
    }

private:
    core::setting<bool> keep_alive_{"Keep Alive", "Re-assert sneaking every tick", true};
};

} // namespace

void register_builtins() {
    auto& registry = module_registry::instance();
    if (registry.find("HUD") != nullptr) {
        return;   // idempotent
    }
    registry.register_module(std::make_unique<hud_module>());
    registry.register_module(std::make_unique<fullbright_module>());
    registry.register_module(std::make_unique<zoom_module>());
    registry.register_module(std::make_unique<sprint_module>());
    registry.register_module(std::make_unique<sneak_module>());
    register_combat_builtins();
    register_pvp_builtins();
    register_gear_builtins();
    WOKE_INFO("module", "registered %zu built-in modules across %zu categories",
              registry.all().size(), kCategoryCount);
}

} // namespace woke::modules

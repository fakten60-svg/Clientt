// ============================================================================
//  woke.wtf — src/modules/builtin_visual.cpp
//  The world-overlay render modules (own TU to keep every file within the
//  hygiene size). Client-state only, per project scope:
//
//    Player ESP   snapshots every other player each tick and draws a box
//                 (+ optional health bar) around them on the overlay
//    Storage ESP  snapshots the world's storage-like block entities and
//                 draws a labeled box at each position
//    Name Tags    draws the other players' GameProfile names above their
//                 heads
//    Tracers      draws a line from the bottom-center of the screen to
//                 every other player
//
//  The snapshots are taken in on_tick (client world reads only) and the
//  draws happen in on_render, so the render path never touches JNI. All
//  screen positions come from game_state::project_world_to_screen — the
//  view-basis + perspective approximation documented in the README. Like
//  every on_render hook these draw while the ClickGUI is open (the
//  framework's draw window), same as Target HUD.
// ============================================================================
#include <memory>
#include <vector>

#include <imgui.h>

#include "core/logger.hpp"
#include "game/game_state.hpp"
#include "modules/builtin.hpp"
#include "ui/theme.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"

namespace woke::modules {

namespace {

using game::game_state;

struct screen_pos {
    float x = 0.0f;
    float y = 0.0f;
    bool visible = false;
};

// Projects one world point through the game_state approximation; returns a
// non-visible pos instead of failing so callers can skip single points.
screen_pos project(game_state& gs, double wx, double wy, double wz) {
    screen_pos out;
    const ImGuiIO& io = ImGui::GetIO();
    double sx = 0.0;
    double sy = 0.0;
    if (!gs.project_world_to_screen(wx, wy, wz, io.DisplaySize.x, io.DisplaySize.y, sx, sy,
                                    out.visible)) {
        out.visible = false;
        return out;
    }
    out.x = static_cast<float>(sx);
    out.y = static_cast<float>(sy);
    return out;
}

// ---- Visual: Player ESP ------------------------------------------------------

class player_esp_module final : public module {
public:
    player_esp_module()
        : module("Player ESP", "Visual",
                 "Draws a box (+ optional health bar) around every other "
                 "player in reach — positions from the client world scan.") {
        register_setting(reach_);
        register_setting(health_bar_);
    }

    void on_tick(game_state& gs) override {
        gs.esp_scan_players(reach_.value(), players_);
    }

    void on_render() override {
        if (players_.empty()) {
            return;
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        auto& gs = game_state::instance();
        for (const game_state::esp_player& p : players_) {
            const screen_pos feet = project(gs, p.x, p.y, p.z);
            const screen_pos head = project(gs, p.x, p.y + 1.8, p.z);
            if (!feet.visible || !head.visible) {
                continue;
            }
            const float height = feet.y - head.y;
            if (height <= 2.0f) {
                continue;   // too far away to be readable
            }
            const float width = height * 0.45f;
            const ImVec2 min(feet.x - width * 0.5f, head.y);
            const ImVec2 max(feet.x + width * 0.5f, feet.y);
            utils::render::rounded_border(dl, min, max, 2.0f,
                                          utils::with_alpha(ui::theme::accent, 0.9f), 1.5f);
            if (health_bar_.value() && p.max_health > 0.0f) {
                const float frac = utils::clamp01(p.health / p.max_health);
                const float bar_h = height * frac;
                const ImVec2 bmin(min.x - 5.0f, feet.y - bar_h);
                const ImVec2 bmax(min.x - 2.0f, feet.y);
                utils::render::rounded_rect(dl, bmin, bmax, 1.0f,
                                            utils::mix(ui::theme::toast_error,
                                                       ui::theme::toast_ok, frac));
            }
        }
    }

private:
    core::setting<double> reach_{"Reach", "Player scan range in blocks", 64.0, 8.0, 128.0};
    core::setting<bool> health_bar_{"Health Bar", "Draw a health bar next to the box", true};
    std::vector<game_state::esp_player> players_;
};

// ---- Visual: Storage ESP --------------------------------------------------------

class storage_esp_module final : public module {
public:
    storage_esp_module()
        : module("Storage ESP", "Visual",
                 "Draws a labeled box at every storage block entity in reach "
                 "(chests, barrels, shulker boxes, furnaces, ...).") {
        register_setting(reach_);
        register_setting(show_label_);
    }

    void on_tick(game_state& gs) override {
        gs.esp_scan_block_entities(reach_.value(), storages_);
    }

    void on_render() override {
        if (storages_.empty()) {
            return;
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        auto& gs = game_state::instance();
        for (const game_state::esp_storage& s : storages_) {
            const screen_pos bottom = project(gs, s.x - 0.5, s.y - 0.5, s.z - 0.5);
            const screen_pos top = project(gs, s.x + 0.5, s.y + 0.5, s.z + 0.5);
            if (!bottom.visible || !top.visible) {
                continue;
            }
            const float box_h = bottom.y - top.y;
            if (box_h <= 2.0f) {
                continue;   // too far away to be readable
            }
            const float box_w = box_h * 0.9f;
            const ImVec2 min(bottom.x - box_w * 0.5f, top.y);
            const ImVec2 max(bottom.x + box_w * 0.5f, bottom.y);
            utils::render::rounded_border(dl, min, max, 2.0f,
                                          utils::with_alpha(ui::theme::toast_warn, 0.85f), 1.5f);
            if (show_label_.value()) {
                const char* label = game::esp_storage_kind_label(s.kind);
                if (label != nullptr) {
                    dl->AddText(ImVec2(min.x, min.y - 14.0f),
                                utils::with_alpha(ui::theme::text_primary, 0.9f), label);
                }
            }
        }
    }

private:
    core::setting<double> reach_{"Reach", "Storage scan range in blocks", 48.0, 8.0, 128.0};
    core::setting<bool> show_label_{"Show Label", "Draw the storage kind label", true};
    std::vector<game_state::esp_storage> storages_;
};

// ---- Visual: Name Tags ----------------------------------------------------------

class name_tags_module final : public module {
public:
    name_tags_module()
        : module("Name Tags", "Visual",
                 "Draws every other player's profile name above their head.") {
        register_setting(reach_);
    }

    void on_tick(game_state& gs) override {
        gs.esp_scan_players(reach_.value(), players_);
    }

    void on_render() override {
        if (players_.empty()) {
            return;
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        auto& gs = game_state::instance();
        for (const game_state::esp_player& p : players_) {
            if (p.name[0] == '\0') {
                continue;   // name unreadable — nothing to draw
            }
            const screen_pos tag = project(gs, p.x, p.y + 2.2, p.z);
            if (!tag.visible) {
                continue;
            }
            const float w = utils::render::text_width(p.name) + 10.0f;
            const float h = utils::render::text_height(p.name) + 4.0f;
            const ImVec2 min(tag.x - w * 0.5f, tag.y);
            const ImVec2 max(tag.x + w * 0.5f, tag.y + h);
            utils::render::rounded_rect(dl, min, max, 3.0f,
                                        utils::with_alpha(ui::theme::window_bg, 0.75f));
            dl->AddText(ImVec2(min.x + 5.0f, min.y + 2.0f),
                        utils::with_alpha(ui::theme::text_primary, 0.95f), p.name);
        }
    }

private:
    core::setting<double> reach_{"Reach", "Player scan range in blocks", 64.0, 8.0, 128.0};
    std::vector<game_state::esp_player> players_;
};

// ---- Visual: Tracers --------------------------------------------------------------

class tracers_module final : public module {
public:
    tracers_module()
        : module("Tracers", "Visual",
                 "Draws a line from the bottom-center of the screen to every "
                 "other player in reach.") {
        register_setting(reach_);
    }

    void on_tick(game_state& gs) override {
        gs.esp_scan_players(reach_.value(), players_);
    }

    void on_render() override {
        if (players_.empty()) {
            return;
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        auto& gs = game_state::instance();
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 origin(io.DisplaySize.x * 0.5f, io.DisplaySize.y);
        for (const game_state::esp_player& p : players_) {
            const screen_pos feet = project(gs, p.x, p.y, p.z);
            if (!feet.visible) {
                continue;
            }
            dl->AddLine(origin, ImVec2(feet.x, feet.y),
                        utils::with_alpha(ui::theme::accent, 0.8f), 1.2f);
        }
    }

private:
    core::setting<double> reach_{"Reach", "Player scan range in blocks", 64.0, 8.0, 128.0};
    std::vector<game_state::esp_player> players_;
};

} // namespace

void register_visual_builtins() {
    auto& registry = module_registry::instance();
    if (registry.find("Player ESP") != nullptr) {
        return;   // idempotent
    }
    registry.register_module(std::make_unique<player_esp_module>());
    registry.register_module(std::make_unique<storage_esp_module>());
    registry.register_module(std::make_unique<name_tags_module>());
    registry.register_module(std::make_unique<tracers_module>());
    WOKE_INFO("module", "registered 4 visual overlay modules (Player ESP, "
                        "Storage ESP, Name Tags, Tracers)");
}

} // namespace woke::modules

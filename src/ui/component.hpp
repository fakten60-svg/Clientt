// ============================================================================
//  woke.wtf — src/ui/component.hpp
//  BaseUIComponent + the reusable widgets the dashboard is built from.
//
//  Every widget implements the same three-part contract, so the ClickGUI never
//  repeats rendering, hit-testing or animation code:
//
//      animate(dt)   advance the widget's own motion (registered with the
//                    AnimationController at construction)
//      render(dl)    pure drawing through RenderUtils (no ImGui item state)
//      handle_input() issue the widget's ImGui item and report a state change
//
//  Widgets own their geometry and their animation state; the caller owns
//  layout and decides what a state change means.
// ============================================================================
#pragma once

#include <cstddef>

#include <imgui.h>

#include "ui/animation.hpp"
#include "utils/math.hpp"

namespace woke::ui {

class base_component {
public:
    virtual ~base_component() = default;

    virtual void animate(float dt) = 0;
    virtual void render(ImDrawList* dl) const = 0;

    // Issues the ImGui item for this widget and returns true when the user
    // changed its state this frame.
    virtual bool handle_input() = 0;

    // Content alpha used by the window open/close fade (1 = fully visible).
    void set_alpha(float a) { alpha_ = utils::clamp01(a); }
    float alpha() const { return alpha_; }

    void set_visible(bool v) { visible_ = v; }
    bool visible() const { return visible_; }

protected:
    float alpha_ = 1.0f;
    bool visible_ = true;
};

// ---- macOS traffic light ---------------------------------------------------

class traffic_light final : public base_component {
public:
    enum class kind : int { close = 0, minimize = 1, zoom = 2 };

    explicit traffic_light(kind k);

    void set_center(ImVec2 center, float radius);
    bool hovered() const { return hovered_ && visible_; }

    void animate(float dt) override;
    void render(ImDrawList* dl) const override;
    bool handle_input() override;

private:
    kind kind_;
    ImVec2 center_{};
    float radius_ = 6.0f;
    bool hovered_ = false;
    animated_value glow_{0.0f, 0.04f};
};

// ---- sidebar entry ---------------------------------------------------------

class sidebar_entry final : public base_component {
public:
    // `section` is the small caption line ("MODULES" / "GENERAL"); pass null
    // for a plain row.
    void configure(const char* label, const char* section, int badge);
    void set_rect(ImVec2 min, ImVec2 size);
    void set_selected(bool selected) { selected_ = selected; }
    void set_badge(int badge) { badge_ = badge; }

    const char* label() const { return label_; }
    bool selected() const { return selected_; }
    bool hovered() const { return hovered_; }

    void animate(float dt) override;
    void render(ImDrawList* dl) const override;
    bool handle_input() override;

private:
    const char* label_ = "";
    const char* section_ = nullptr;
    ImVec2 min_{};
    ImVec2 size_{};
    int badge_ = -1;
    bool selected_ = false;
    bool hovered_ = false;
    animated_value hover_{0.0f, 0.05f};
};

// ---- Apple-style pill toggle ----------------------------------------------

class toggle_switch final : public base_component {
public:
    toggle_switch();

    void set_rect(ImVec2 min, ImVec2 size);
    void set_on(bool on);
    bool on() const { return on_; }
    bool hovered() const { return hovered_; }

    void animate(float dt) override;
    void render(ImDrawList* dl) const override;
    bool handle_input() override;

private:
    ImVec2 min_{};
    ImVec2 size_{};
    bool on_ = false;
    bool hovered_ = false;
    animated_value knob_{0.0f, 0.09f};
};

// ---- search field ----------------------------------------------------------

class search_field final : public base_component {
public:
    void set_rect(ImVec2 min, float width);
    void animate(float dt) override;
    void render(ImDrawList* dl) const override;
    bool handle_input() override;

    char* buffer() { return buffer_; }
    const char* text() const { return buffer_; }
    std::size_t capacity() const { return sizeof buffer_; }
    void set_text(const char* text);
    void clear() { buffer_[0] = '\0'; }

private:
    ImVec2 min_{};
    float width_ = 220.0f;
    char buffer_[64] = {};
    bool focused_ = false;
    animated_value focus_{0.0f, 0.06f};
};

// ---- list/grid view toggle -------------------------------------------------

class view_toggle final : public base_component {
public:
    // `button_size` applies to each of the two halves.
    void set_rect(ImVec2 min, float button_size);
    void set_grid(bool grid) { grid_ = grid; }
    bool grid() const { return grid_; }

    void animate(float dt) override;
    void render(ImDrawList* dl) const override;
    bool handle_input() override;

private:
    ImVec2 min_{};
    float size_ = 24.0f;
    bool grid_ = false;
    animated_value slide_{0.0f, 0.07f};
};

// ---- chevron (module card expander) ---------------------------------------

class chevron final : public base_component {
public:
    void set_rect(ImVec2 min, float size);
    void set_expanded(bool expanded);
    bool expanded() const { return expanded_; }

    void animate(float dt) override;
    void render(ImDrawList* dl) const override;
    bool handle_input() override;

private:
    ImVec2 min_{};
    float size_ = 18.0f;
    bool expanded_ = false;
    bool hovered_ = false;
    animated_value rotation_{0.0f, 0.07f};
};

} // namespace woke::ui

#include "presentation/ui/ui_element.h"

#include <utility>

namespace gitman::ui {
    bool rect_f::contains(const float point_x, const float point_y) const noexcept
    {
        return point_x >= x && point_x < x + width && point_y >= y && point_y < y + height;
    }

    ui_element::ui_element(ui_element_id id) noexcept
        : id_ { std::move(id) }
    {}

    const ui_element_id& ui_element::id() const noexcept
    {
        return id_;
    }

    const rect_f& ui_element::bounds() const noexcept
    {
        return bounds_;
    }

    bool ui_element::enabled() const noexcept
    {
        return enabled_;
    }

    bool ui_element::visible() const noexcept
    {
        return visible_;
    }

    const std::u8string& ui_element::tooltip() const noexcept
    {
        return tooltip_;
    }

    const ui_action* ui_element::action(const ui_trigger trigger) const noexcept
    {
        const ui_action& stored { actions_[static_cast<std::size_t>(trigger)] };
        return stored ? &stored : nullptr;
    }

    const drag_source* ui_element::drag() const noexcept
    {
        return drag_source_.has_value() ? &*drag_source_ : nullptr;
    }

    const drop_target* ui_element::drop() const noexcept
    {
        return drop_target_.has_value() ? &*drop_target_ : nullptr;
    }

    bool ui_element::interactive() const noexcept
    {
        if (tooltip_.empty() == false || drag_source_.has_value() || drop_target_.has_value())
            return true;
        for (const ui_action& stored : actions_)
            if (stored)
                return true;
        return false;
    }

    std::span<const std::unique_ptr<ui_element>> ui_element::children() const noexcept
    {
        return children_;
    }

    void ui_element::set_bounds(const rect_f& bounds) noexcept
    {
        bounds_ = bounds;
    }

    void ui_element::set_enabled(const bool value) noexcept
    {
        enabled_ = value;
    }

    void ui_element::set_visible(const bool value) noexcept
    {
        visible_ = value;
    }

    void ui_element::set_tooltip(std::u8string text)
    {
        tooltip_ = std::move(text);
    }

    void ui_element::set_action(const ui_trigger trigger, ui_action action)
    {
        actions_[static_cast<std::size_t>(trigger)] = std::move(action);
    }

    void ui_element::clear_action(const ui_trigger trigger) noexcept
    {
        actions_[static_cast<std::size_t>(trigger)] = {};
    }

    void ui_element::set_drag_source(std::optional<drag_source> source)
    {
        drag_source_ = std::move(source);
    }

    void ui_element::set_drop_target(std::optional<drop_target> target)
    {
        drop_target_ = std::move(target);
    }

    const ui_element* ui_element::hit_test(const float x, const float y) const
    {
        if (visible_ == false)
            return nullptr;

        // 뒤에 추가된 자식이 위에 그려지므로 역순으로 검사한다. 스크롤로 자식이
        // 부모 slot 밖에 걸칠 수 있어 자식 탐색은 자기 bounds로 막지 않는다.
        for (std::size_t index = children_.size(); index > 0; --index)
        {
            const ui_element* const hit { children_[index - 1]->hit_test(x, y) };
            if (hit != nullptr)
                return hit;
        }

        if (interactive() && bounds_.contains(x, y))
            return this;
        return nullptr;
    }

    void ui_element::add_child(std::unique_ptr<ui_element> child)
    {
        children_.push_back(std::move(child));
    }

    void ui_element::draw_children(draw_context& context, const interaction_snapshot& interaction) const
    {
        for (const std::unique_ptr<ui_element>& child : children_)
            if (child->visible())
                child->draw(context, interaction);
    }
} // namespace gitman::ui

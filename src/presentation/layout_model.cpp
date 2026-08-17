#include "presentation/layout_model.h"

namespace gitman {
    bool rect_f::contains(const float point_x, const float point_y) const noexcept
    {
        return point_x >= x && point_x < x + width && point_y >= y && point_y < y + height;
    }

    float card_list_content_height(const std::size_t card_count, const float scale) noexcept
    {
        if (card_count == 0)
            return layout_margin * 2.0f * scale;
        const float cards { static_cast<float>(card_count) * layout_card_height + static_cast<float>(card_count - 1) * layout_card_gap };
        return (cards + layout_margin * 2.0f) * scale;
    }

    float clamp_scroll_offset(const float offset, const float content_height, const float viewport_height) noexcept
    {
        const float maximum { content_height - viewport_height };
        if (maximum <= 0.0f)
            return 0.0f;
        if (offset < 0.0f)
            return 0.0f;
        return offset > maximum ? maximum : offset;
    }

    layout_snapshot compute_layout(const view_snapshot& view)
    {
        layout_snapshot layout {};
        layout.window_width = view.window_width;
        layout.window_height = view.window_height;
        layout.scale = view.scale;
        layout.content_height = card_list_content_height(view.cards.size(), view.scale);

        const float scale { view.scale };
        const float caption_height { layout_caption_height * scale };
        const float toolbar_height { layout_toolbar_height * scale };
        const float margin { layout_margin * scale };
        const float button { layout_button_size * scale };
        const float list_top { caption_height + toolbar_height };
        const float viewport_height { view.window_height - list_top };
        layout.scroll_offset = clamp_scroll_offset(view.scroll_offset * scale, layout.content_height, viewport_height);

        // toolbar: caption 바로 아래 줄이며 전체 refresh와, 문서가 없을 때의 열기
        // 버튼을 담는다.
        const float button_y { caption_height + (toolbar_height - button) / 2.0f };
        layout.areas.push_back({ hit_target_kind::toolbar_refresh_all, {}, { view.window_width - margin - button, button_y, button, button } });
        if (view.empty_state == view_empty_state::no_document)
            layout.areas.push_back({ hit_target_kind::toolbar_open_document, {}, { view.window_width - margin - button * 2.0f - margin, button_y, button, button } });

        const float card_height { layout_card_height * scale };
        const float card_gap { layout_card_gap * scale };
        const float card_width { view.window_width - margin * 2.0f };
        for (std::size_t index = 0; index < view.cards.size(); ++index)
        {
            const float top { list_top + margin + static_cast<float>(index) * (card_height + card_gap) - layout.scroll_offset };
            if (top + card_height < list_top || top > view.window_height)
                continue;

            const card_view_model& card { view.cards[index] };
            const rect_f body { margin, top, card_width, card_height };
            layout.areas.push_back({ hit_target_kind::card_body, card.id, body });

            // 카드 오른쪽 끝에 버튼 3개를 세로 중앙 정렬로 둔다. update와 switch는
            // 단계 7 전까지 비활성이다.
            const float buttons_y { top + (card_height - button) / 2.0f };
            float button_x { body.x + body.width - margin - button };
            layout.areas.push_back({ hit_target_kind::card_switch_disabled, card.id, { button_x, buttons_y, button, button } });
            button_x -= button + card_gap;
            layout.areas.push_back({ hit_target_kind::card_update_disabled, card.id, { button_x, buttons_y, button, button } });
            button_x -= button + card_gap;
            layout.areas.push_back({ hit_target_kind::card_refresh, card.id, { button_x, buttons_y, button, button } });
        }
        return layout;
    }

    hit_area hit_test(const layout_snapshot& layout, const float x, const float y) noexcept
    {
        // 나중에 넣은 영역이 위에 그려지므로 뒤에서부터 검사한다. 카드 body보다 그 위의
        // 버튼이 먼저 걸린다.
        for (std::size_t index = layout.areas.size(); index > 0; --index)
        {
            const hit_area& area { layout.areas[index - 1] };
            if (area.bounds.contains(x, y))
                return area;
        }
        return {};
    }
} // namespace gitman

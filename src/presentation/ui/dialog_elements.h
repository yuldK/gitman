#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace gitman::ui {
    // in-app dialog(update overlay, switch dialog)가 공유하는 글자 버튼이다.
    // 아이콘 전용 button_element와 달리 텍스트를 중앙에 그린다. hover·눌림·비활성
    // 표시는 같은 규칙을 쓴다.
    class text_button_element final : public ui_element
    {
    public:
        text_button_element(ui_element_id id, std::u8string text, bool accent);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        std::u8string text_ {};
        bool accent_ { false };
    };

    // dialog가 공유하는 한 줄 텍스트 박스다 (환경설정의 제한 시간 칸, switch
    // dialog의 질의 막대). 상태를 담지 않는다 — 글·caret·선택은 logic의 초안이고
    // 초점은 interaction snapshot이 가진다.
    //
    // 포인터 x를 글 안의 offset으로 옮기는 것(`offset_at`)과 가로 스크롤을 이
    // element가 모두 계산해 그리기와 hit test가 어긋날 수 없다
    // (switch-dialog-query-bar-design R2.3).
    class text_input_element final : public ui_element
    {
    public:
        text_input_element(ui_element_id id, text_input_view value, std::u8string placeholder);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<std::size_t> offset_at(float x, const text_measurer& measurer) const override;
        [[nodiscard]] std::optional<text_input_snapshot> text_input() const override;

    private:
        // 글자 크기(논리 11 × scale)다. arrange가 정하고 그리기와 hit test가 같이 쓴다.
        [[nodiscard]] float font_size() const noexcept;
        // 글 왼쪽 시작 x와 글이 보이는 안쪽 폭이다.
        [[nodiscard]] float text_left() const noexcept;
        [[nodiscard]] float inner_width() const noexcept;
        // caret이 보이도록 최소한만 민 가로 스크롤이다. 글·caret·폭만으로 정해진다.
        [[nodiscard]] float scroll_for(const std::function<float(std::u8string_view)>& measure) const;

        text_input_view value_ {};
        std::u8string placeholder_ {};
        float scale_ { 1.0f };
    };
} // namespace gitman::ui

#pragma once

#include "presentation/ui/ui_element.h"

#include <string>

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
    // dialog의 질의 막대). 상태를 담지 않는다 — 글은 logic의 초안이고 초점은
    // interaction snapshot이 가진다. 커서 이동은 지원하지 않아 caret은 항상 글
    // 끝에서 깜빡인다.
    class text_input_element final : public ui_element
    {
    public:
        text_input_element(ui_element_id id, std::u8string text, std::u8string placeholder);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        std::u8string text_ {};
        std::u8string placeholder_ {};
    };
} // namespace gitman::ui

#pragma once

#include "presentation/ui/ui_common.h"

#include <string>

namespace gitman::ui {
    // caption 아래 도구 막대다. 문서 경로와 전체 새로 고침, 문서가 없을 때의
    // 열기 버튼, 문서가 열려 있을 때의 닫기 버튼을 담는다. 새 문서 만들기는
    // 시작 페이지가 유일한 진입점이라 여기에 두지 않는다 (2026-08-22 사용자 지시).
    //
    // 좁을 때 오른쪽부터 버튼을 숨기는 규칙 때문에 luil stack으로는 표현할 수
    // 없어 커스텀으로 남긴다 (설계 L5). 버튼·라벨만 luil 표준 요소다.
    class toolbar_element final : public ui_element
    {
    public:
        // `document_open`은 열린 문서가 필요한 버튼(환경설정, 탐색 등록, 닫기)의
        // 활성 조건이다. 문서 열기는 반대로 문서가 없을 때만 둔다.
        // `discovery_start_folder`는 탐색 등록 버튼이 여는 폴더 dialog의 시작 자리다
        // (열린 문서가 있는 폴더). UI thread 명령의 인자로 실려 간다 (설계 L7) —
        // element는 view를 들지 않으므로 tree를 짓는 쪽이 넣어 준다.
        // `can_update_all`과 `any_change_running`은 일괄 업데이트 버튼이 무엇으로
        // 보일지를 정한다 (REQ-018). 하나라도 돌고 있으면 같은 자리가 "모두 중지"다.
        toolbar_element(
            std::u8string document_text, bool show_open_button, bool relative_paths, bool document_open, std::u8string discovery_start_folder, bool can_update_all, bool any_change_running);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        ui_element* document_label_ { nullptr };
        ui_element* refresh_all_ { nullptr };
        ui_element* update_all_ { nullptr };
        ui_element* open_document_ { nullptr };
        ui_element* close_document_ { nullptr };
        ui_element* toggle_path_display_ { nullptr };
        ui_element* settings_ { nullptr };
        ui_element* discover_ { nullptr };
        // 문서가 없을 때만 열기 버튼을, 있을 때만 닫기 버튼을 둔다. arrange가
        // 자리 부족으로 숨긴 것과 구분해야 해서 생성 시 결정을 따로 기억한다.
        bool show_open_button_ { false };
        bool document_open_ { false };
    };
} // namespace gitman::ui

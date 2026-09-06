#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace gitman::ui {
    // 환경설정 항목 열의 스크롤 치수다 (text-input-and-font-fallback-design E1.3).
    // 값은 모두 **논리 픽셀**이다. logic의 스크롤 한계와 element의 배치가 이 함수
    // 하나를 함께 써야 휠이 헛돌거나 마지막 항목이 잘리지 않는다.
    struct settings_dialog_scroll_metrics
    {
        // panel 높이를 정하는 값이다 — **가장 높은 탭**의 내용 높이다. 탭을 옮길 때
        // dialog 크기가 튀지 않게 고정한다 (S1.3).
        float panel_content_height { 0.0f };
        // **지금 보고 있는 탭**의 내용 높이다. 스크롤 한계는 이 값으로 센다 —
        // 가장 높은 탭으로 세면 짧은 탭에서 빈 공간까지 스크롤된다 (2026-08-23 지시).
        float content_height { 0.0f };
        // 항목 열이 실제로 보이는 높이다. 창이 낮으면 chrome을 뺀 나머지다.
        float viewport_height { 0.0f };
        // 스크롤할 수 있는 최대치다. 0이면 다 보이므로 막대도 만들지 않는다.
        float maximum_scroll { 0.0f };
    };

    // `window_height`는 논리 픽셀이다.
    [[nodiscard]] settings_dialog_scroll_metrics settings_dialog_metrics(const settings_dialog_view& dialog, float window_height);

    // 환경설정 dialog의 **본체**다 (REQ-017, stage-8-plan 5.1,
    // docs/settings-tabs-and-appearance-scope-design.md S1). luil `modal_host_element`의
    // content로 들어간다 — scrim·가운데 배치·초점 가둠·Esc는 host가 맡고 (설계 D2-A)
    // 본체는 host가 잡아 준 panel 상자를 그대로 받아 왼쪽 탭 rail과 고른 탭의
    // 섹션·항목만 담는다. 경로는 자유 입력 없이 찾아보기(파일 선택)와 지우기로만
    // 바꾼다.
    class settings_dialog_element final : public ui_element
    {
    public:
        // `window_height`는 논리 픽셀이며 항목 열의 스크롤 치수를 세는 데만 쓴다.
        settings_dialog_element(settings_dialog_view dialog, float window_height);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        settings_dialog_view dialog_ {};
        // panel은 활성 탭의 모델과 배치를 함께 들고 있다. dialog는 panel이 계산한
        // 항목 사각형 위에 컨트롤을 얹으므로 그리기와 배치가 어긋나지 않는다.
        ui_element* panel_ { nullptr };
        // panel 높이(네 탭 중 가장 높은 내용 + chrome)와 창에 맞춘 줄임은 factory가
        // 세어 host에 넘긴다 (S1.3, E1). 본체는 그 상자를 받기만 한다.
        settings_dialog_scroll_metrics metrics_ {};
        // 내용이 넘칠 때만 보인다.
        ui_element* scrollbar_ { nullptr };
        std::vector<ui_element*> tab_items_ {};
        // 활성 탭의 항목과 같은 순서다. 항목 하나가 컨트롤을 0~2개 갖는다.
        std::vector<std::vector<ui_element*>> item_controls_ {};
        // 문서가 덮어쓴 항목의 `덮어씀` 배지다 (항목 index, element). 문서 모드에서
        // 덮어쓴 항목에만 만들어지고 클릭이 그 항목의 문서 정의를 지운다 (S4.2).
        std::vector<std::pair<std::size_t, ui_element*>> badges_ {};
        // 외양 탭의 컨트롤이다. 테마 세 칸과 색 동그라미들이며 index가 아니라 이
        // 목록으로 배치한다.
        std::vector<ui_element*> theme_options_ {};
        std::vector<ui_element*> swatches_ {};
        ui_element* confirm_ { nullptr };
        ui_element* cancel_ { nullptr };
    };

    // 환경설정 dialog를 host째 만든다 (설계 D2-A). `window_width`·`window_height`는
    // **논리 픽셀**이며, panel이 창을 넘지 않게 여기서 미리 줄여 host에 넘긴다.
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_settings_dialog(const settings_dialog_view& view, float window_width, float window_height);
} // namespace gitman::ui

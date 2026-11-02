#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include <memory>
#include <optional>

namespace gitman::ui {
    // view snapshot에서 배치가 끝난 불변 tree를 만드는 순수 함수다. 같은 snapshot
    // 이면 같은 tree가 나온다. 액션·tooltip·활성 상태 등록도 여기서 끝난다
    // (docs/ui-element-design.md 3). logic thread가 게시 직전에 호출한다.
    // `measurer`는 배치가 글 폭을 재는 함수다. 비어 있으면 그리기와 같은 UI 글꼴로 잰다 — test는 고정 폭
    // 측정기를 넣어 기능 줄 버튼 폭(글 폭에 달렸다)을 결정적으로 본다.
    [[nodiscard]] std::shared_ptr<const ui_tree> build_ui_tree(const view_snapshot& view, const text_measurer* measurer = nullptr);

    // 기능 줄 초점 옮기기(function-buttons-design 2.2.5)가 갈 element다. 후보 순서는 줄에서 사라진 버튼 뒤에 있던
    // 버튼들 → 줄의 `⋯` → 그 카드의 `⋯`이고, tree에 있고 보이며 활성이고 Tab 자리인 첫 후보다. 없으면 값이 없다 —
    // 사라진 버튼의 초점은 luil이 이미 거뒀으므로 그대로 둔다.
    [[nodiscard]] std::optional<ui_element_id> function_focus_target(const ui_tree& tree, const function_focus_request& request);
} // namespace gitman::ui

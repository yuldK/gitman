#pragma once

#include "presentation/ui/ui_tree.h"
#include "presentation/view_snapshot.h"

#include <memory>

namespace gitman::ui {
    // view snapshot에서 배치가 끝난 불변 tree를 만드는 순수 함수다. 같은 snapshot
    // 이면 같은 tree가 나온다. 액션·tooltip·활성 상태 등록도 여기서 끝난다
    // (docs/ui-element-design.md 3). logic thread가 게시 직전에 호출한다.
    [[nodiscard]] std::shared_ptr<const ui_tree> build_ui_tree(const view_snapshot& view);
} // namespace gitman::ui

#pragma once

#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "presentation/status_presentation.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace gitman {
    enum class card_sort_key
    {
        name,
        status,
        // 문서의 프로젝트 순서 그대로다. 카드 drag & drop 순서 변경이 이 모드로
        // 전환한다.
        custom,
    };

    // 렌더러가 그대로 그리는 카드 하나의 불변 표시 모델이다 (REQ-005).
    struct card_view_model
    {
        project_id id {};
        std::u8string display_name {};
        std::u8string path {};
        repository_kind kind { repository_kind::unknown };
        card_view_state state { card_view_state::loading };
        status_glyph status {};
        // Git은 현재 branch, SVN은 URL이다. provider가 채운 current_reference를 쓴다.
        std::u8string reference {};
        std::u8string revision {};
        std::u8string working_tree_text {};
        std::u8string comparison_target {};
        std::optional<std::chrono::system_clock::time_point> local_checked_at {};
        std::optional<std::chrono::system_clock::time_point> remote_checked_at {};
        bool busy { false };
        bool selected { false };
        bool enabled { true };
    };

    enum class view_empty_state
    {
        none,
        no_document,
        document_loading,
        no_projects,
        no_filter_match,
    };

    // logic thread가 게시하고 UI thread가 그대로 그리는 불변 snapshot이다 (ADR-004).
    // 렌더러와 input thread가 같은 layout을 계산할 수 있도록 창 크기와 스크롤 값을
    // 함께 담는다.
    struct view_snapshot
    {
        std::u8string document_path {};
        std::vector<card_view_model> cards {};
        std::optional<project_id> selected {};
        std::u8string filter_text {};
        card_sort_key sort { card_sort_key::name };
        view_empty_state empty_state { view_empty_state::no_document };
        // 문서 수준 진단의 한국어 요약이다.
        std::vector<std::u8string> notices {};
        float window_width { 0.0f };
        float window_height { 0.0f };
        float scale { 1.0f };
        float scroll_offset { 0.0f };
        // `.version-list` 생성이 진행 중이다. toolbar 생성 버튼을 비활성화한다.
        bool document_generating { false };
        bool shutting_down { false };
    };
} // namespace gitman

#pragma once

#include "domain/diagnostic.h"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // switch dialog를 SVN 저장소 브라우저로 초기화하는 데 필요한 로컬 정보다.
    // 두 URL은 `svn info --show-item`으로 작업 복사본에서 얻는다.
    struct svn_repository_browser_info
    {
        std::u8string repository_root_url {};
        std::u8string current_url {};
    };

    enum class svn_browser_query_error
    {
        none,
        authentication_required,
        offline,
        repository_not_found,
        timed_out,
        cancelled,
        failed,
    };

    // 한 디렉터리에 대한 `svn ls` 결과다. `directories`에는 파일이 들어가지 않는다.
    struct svn_directory_query_result
    {
        std::vector<std::u8string> directories {};
        svn_browser_query_error error { svn_browser_query_error::none };
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool succeeded() const noexcept;
    };

    enum class svn_browser_node_load_state
    {
        not_loaded,
        loading,
        loaded,
        failed,
    };

    // URL을 key로 삼는 평면 트리 노드다. `child_urls`가 서버 응답 순서를 보존하며,
    // loaded 노드는 dialog가 닫힐 때까지 다시 조회하지 않는다.
    struct svn_repository_browser_node
    {
        std::u8string name {};
        std::u8string url {};
        std::u8string parent_url {};
        std::vector<std::u8string> child_urls {};
        svn_browser_node_load_state load_state { svn_browser_node_load_state::not_loaded };
        bool expanded { false };
        std::u8string error_message {};
    };

    struct svn_repository_browser_state
    {
        std::u8string root_url {};
        std::u8string current_url {};
        std::u8string selected_url {};
        // 사용자가 주소 막대로 요청한 이동 목표다. dialog를 열 때는 비어 있어
        // root 한 번만 조회한다 (switch-dialog-query-bar-design Q3). 도달하거나
        // 실패하면 비운다.
        std::u8string reveal_url {};
        std::map<std::u8string, svn_repository_browser_node, std::less<>> nodes {};
    };

    // reveal 한 걸음의 결과다. 조회가 더 필요한지, 도달했는지, 조회 목록에 그
    // 경로가 없어 포기했는지를 호출자가 구분해야 안내 문구를 정할 수 있다.
    struct svn_browser_reveal_progress
    {
        std::optional<std::u8string> query_url {};
        bool reached { false };
        bool not_found { false };
    };

    enum class svn_browser_row_kind
    {
        directory,
        loading,
        error,
    };

    // 펼쳐진 노드만 깊이 우선으로 편 평면 표시 행이다. loading/error는 펼친
    // 디렉터리 바로 아래에 나타나는 상태 행이라 선택하거나 펼칠 수 없다.
    struct svn_repository_browser_row
    {
        svn_browser_row_kind kind { svn_browser_row_kind::directory };
        std::u8string url {};
        std::u8string text {};
        std::size_t depth { 0 };
        bool expanded { false };
        bool can_expand { false };
        bool current { false };
        bool selected { false };
    };

    [[nodiscard]] std::u8string normalize_svn_browser_url(std::u8string_view url);
    [[nodiscard]] std::u8string append_svn_browser_url(std::u8string_view parent_url, std::u8string_view directory_name);
    [[nodiscard]] bool svn_browser_url_contains(std::u8string_view parent_url, std::u8string_view candidate_url) noexcept;
    [[nodiscard]] std::u8string_view svn_browser_query_error_message(svn_browser_query_error error) noexcept;

    [[nodiscard]] svn_repository_browser_state make_svn_repository_browser(const svn_repository_browser_info& info);
    [[nodiscard]] const svn_repository_browser_node* find_svn_browser_node(const svn_repository_browser_state& state, std::u8string_view url) noexcept;

    // 아직 cache되지 않은 노드를 loading으로 바꾼다. 실제 조회가 필요할 때만 true다.
    [[nodiscard]] bool begin_svn_browser_query(svn_repository_browser_state& state, std::u8string_view url);

    // 글리프 클릭을 적용한다. 새 조회가 필요하면 그 URL을 돌려주고, cache된 노드는
    // process 요청 없이 다시 펼친다.
    [[nodiscard]] std::optional<std::u8string> toggle_svn_browser_node(svn_repository_browser_state& state, std::u8string_view url);

    // 한 노드의 조회 결과를 cache한다. 진행 중인 reveal 목표가 있으면 그 쪽으로 한
    // 걸음 더 내려간다. 목표가 없으면 cache만 하고 끝난다.
    [[nodiscard]] svn_browser_reveal_progress complete_svn_browser_query(svn_repository_browser_state& state, std::u8string_view url, const svn_directory_query_result& result);

    // 이동 목표를 세우고 cache된 만큼 펼친다. 목표가 root 밖이면 `not_found`다.
    [[nodiscard]] svn_browser_reveal_progress begin_svn_browser_reveal(svn_repository_browser_state& state, std::u8string_view url);

    // 한 노드의 cache와 그 아래 노드를 모두 버리고 다시 조회할 수 있게 만든다.
    // 펼침 상태는 유지한다. 선택이 버려진 하위에 있었으면 이 노드로 되돌린다.
    [[nodiscard]] bool reset_svn_browser_node(svn_repository_browser_state& state, std::u8string_view url);

    // 저장소 root를 기준으로 한 표시 경로다. root 자신은 `/`, 아래는 `/trunk/source`
    // 형태다. root 밖이면 빈 값이다.
    [[nodiscard]] std::u8string svn_browser_relative_path(std::u8string_view root_url, std::u8string_view url);

    // 주소 막대의 글을 URL로 바꾼다. root로 시작하는 전체 URL은 그대로 정규화하고,
    // 그 밖에는 `/`로 잘라 조각마다 인코딩해 붙인다. 해석할 수 없으면 빈 값이다.
    [[nodiscard]] std::u8string resolve_svn_browser_path(const svn_repository_browser_state& state, std::u8string_view text);

    [[nodiscard]] bool select_svn_browser_node(svn_repository_browser_state& state, std::u8string_view url);
    [[nodiscard]] std::vector<svn_repository_browser_row> build_svn_repository_browser_rows(const svn_repository_browser_state& state);
} // namespace gitman

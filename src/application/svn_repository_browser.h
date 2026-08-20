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
        std::map<std::u8string, svn_repository_browser_node, std::less<>> nodes {};
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

    // 한 노드의 조회 결과를 cache한다. 현재 위치가 더 아래라면 다음 조상을 자동으로
    // 펼치고, 조회가 필요한 첫 URL을 돌려준다.
    [[nodiscard]] std::optional<std::u8string> complete_svn_browser_query(svn_repository_browser_state& state, std::u8string_view url, const svn_directory_query_result& result);

    [[nodiscard]] bool select_svn_browser_node(svn_repository_browser_state& state, std::u8string_view url);
    [[nodiscard]] std::vector<svn_repository_browser_row> build_svn_repository_browser_rows(const svn_repository_browser_state& state);
} // namespace gitman

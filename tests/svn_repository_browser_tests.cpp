#include "application/svn_repository_browser.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {
    constexpr std::u8string_view root_url { u8"https://svn.example.com/repo" };
    constexpr std::u8string_view trunk_url { u8"https://svn.example.com/repo/trunk" };
    constexpr std::u8string_view current_url { u8"https://svn.example.com/repo/trunk/source" };

    gitman::svn_repository_browser_state browser()
    {
        return gitman::make_svn_repository_browser({ std::u8string { root_url }, std::u8string { current_url } });
    }

    gitman::svn_directory_query_result listing(std::initializer_list<std::u8string_view> names)
    {
        gitman::svn_directory_query_result result {};
        for (const std::u8string_view name : names)
            result.directories.emplace_back(name);
        return result;
    }
} // namespace

TEST_CASE("SVN browser does not descend without a reveal target", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { browser() };
    REQUIRE(state.selected_url == current_url);
    REQUIRE(state.reveal_url.empty());
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));

    {
        const std::vector<gitman::svn_repository_browser_row> rows { gitman::build_svn_repository_browser_rows(state) };
        REQUIRE(rows.size() == 2u);
        REQUIRE(rows[0].kind == gitman::svn_browser_row_kind::directory);
        REQUIRE(rows[1].kind == gitman::svn_browser_row_kind::loading);
        REQUIRE(rows[1].text == u8"조회 중…");
    }

    // 현재 위치가 더 아래여도 자동으로 내려가지 않는다. root 목록 하나로 끝난다.
    const gitman::svn_browser_reveal_progress progress { gitman::complete_svn_browser_query(state, root_url, listing({ u8"branches", u8"tags", u8"trunk" })) };
    REQUIRE_FALSE(progress.query_url.has_value());
    REQUIRE_FALSE(progress.reached);
    REQUIRE_FALSE(progress.not_found);

    const std::vector<gitman::svn_repository_browser_row> rows { gitman::build_svn_repository_browser_rows(state) };
    REQUIRE(rows.size() == 4u);
    REQUIRE(rows[3].text == u8"trunk");
    REQUIRE_FALSE(rows[3].expanded);
}

TEST_CASE("SVN browser reveals a requested path one level at a time", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { browser() };
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, listing({ u8"branches", u8"tags", u8"trunk" })).query_url.has_value());

    const gitman::svn_browser_reveal_progress started { gitman::begin_svn_browser_reveal(state, current_url) };
    REQUIRE(started.query_url == trunk_url);
    REQUIRE_FALSE(started.reached);

    const gitman::svn_browser_reveal_progress reached { gitman::complete_svn_browser_query(state, *started.query_url, listing({ u8"docs", u8"source" })) };
    REQUIRE(reached.reached);
    REQUIRE_FALSE(reached.query_url.has_value());
    REQUIRE(state.selected_url == current_url);
    REQUIRE(state.reveal_url.empty());

    const std::vector<gitman::svn_repository_browser_row> rows { gitman::build_svn_repository_browser_rows(state) };
    REQUIRE(rows.size() == 6u);
    REQUIRE(rows[3].text == u8"trunk");
    REQUIRE(rows[3].expanded);
    REQUIRE(rows[5].text == u8"source");
    REQUIRE(rows[5].current);
    REQUIRE(rows[5].selected);
    REQUIRE(rows[5].depth == 2u);
    // 목표 자신은 펼치지 않아 그 아래 조회가 나가지 않는다.
    REQUIRE_FALSE(rows[5].expanded);
}

TEST_CASE("SVN browser reports a reveal target that the listing does not contain", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { browser() };
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, listing({ u8"branches", u8"trunk" })).query_url.has_value());

    const gitman::svn_browser_reveal_progress missing { gitman::begin_svn_browser_reveal(state, u8"https://svn.example.com/repo/stages/x") };
    REQUIRE(missing.not_found);
    REQUIRE_FALSE(missing.reached);
    REQUIRE(state.reveal_url.empty());

    // root 밖의 URL도 같은 실패다.
    REQUIRE(gitman::begin_svn_browser_reveal(state, u8"https://other.example.com/repo").not_found);
}

TEST_CASE("SVN browser reveal gives up when a step fails", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { browser() };
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, listing({ u8"trunk" })).query_url.has_value());
    REQUIRE(gitman::begin_svn_browser_reveal(state, current_url).query_url == trunk_url);

    gitman::svn_directory_query_result failure {};
    failure.error = gitman::svn_browser_query_error::offline;
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, trunk_url, failure).reached);
    REQUIRE(state.reveal_url.empty());
    REQUIRE(state.selected_url == current_url);
}

TEST_CASE("SVN browser reset drops a subtree so it can be queried again", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { browser() };
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, listing({ u8"trunk" })).query_url.has_value());
    REQUIRE(gitman::begin_svn_browser_reveal(state, current_url).query_url == trunk_url);
    REQUIRE(gitman::complete_svn_browser_query(state, trunk_url, listing({ u8"source" })).reached);
    REQUIRE(state.nodes.size() == 3u);

    REQUIRE(gitman::reset_svn_browser_node(state, trunk_url));
    // 하위 node가 사라지고 선택이 그 노드로 되돌아간다.
    REQUIRE(state.nodes.size() == 2u);
    REQUIRE(state.selected_url == trunk_url);
    REQUIRE(gitman::begin_svn_browser_query(state, trunk_url));
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, trunk_url, listing({ u8"docs", u8"source" })).query_url.has_value());
    REQUIRE(gitman::build_svn_repository_browser_rows(state).size() == 4u);
}

TEST_CASE("SVN browser reuses a loaded node after collapse", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { gitman::make_svn_repository_browser({ std::u8string { root_url }, std::u8string { root_url } }) };
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, listing({ u8"branches", u8"trunk" })).query_url.has_value());

    // 첫 toggle은 접기, 두 번째는 cache로 다시 펼치기다. 새 query URL이 생기지 않는다.
    REQUIRE_FALSE(gitman::toggle_svn_browser_node(state, root_url).has_value());
    REQUIRE(gitman::build_svn_repository_browser_rows(state).size() == 1u);
    REQUIRE_FALSE(gitman::toggle_svn_browser_node(state, root_url).has_value());
    REQUIRE(gitman::build_svn_repository_browser_rows(state).size() == 3u);
}

TEST_CASE("SVN browser renders authentication failure under the expanded node", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { browser() };
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));
    gitman::svn_directory_query_result failure {};
    failure.error = gitman::svn_browser_query_error::authentication_required;
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, failure).query_url.has_value());

    const std::vector<gitman::svn_repository_browser_row> rows { gitman::build_svn_repository_browser_rows(state) };
    REQUIRE(rows.size() == 2u);
    REQUIRE(rows[1].kind == gitman::svn_browser_row_kind::error);
    REQUIRE(rows[1].text == u8"인증이 필요해 조회하지 못했습니다");
}

TEST_CASE("SVN browser URL joining escapes one directory segment", "[application][svn][browser]")
{
    REQUIRE(gitman::append_svn_browser_url(root_url, u8"공용 자료") == u8"https://svn.example.com/repo/%EA%B3%B5%EC%9A%A9%20%EC%9E%90%EB%A3%8C");
    REQUIRE(gitman::append_svn_browser_url(root_url, u8"back\\slash") == u8"https://svn.example.com/repo/back%5Cslash");
    REQUIRE(gitman::append_svn_browser_url(root_url, u8"../outside").empty());
    REQUIRE(gitman::normalize_svn_browser_url(u8"file:///") == u8"file:///");
    REQUIRE(gitman::append_svn_browser_url(u8"file:///", u8"repo") == u8"file:///repo");
    REQUIRE(gitman::svn_browser_url_contains(u8"file:///", u8"file:///repo/trunk"));
    REQUIRE(gitman::normalize_svn_browser_url(u8"https://host/repo/%ea%b3%b5/") == u8"https://host/repo/%EA%B3%B5");
    REQUIRE(gitman::svn_browser_url_contains(root_url, current_url));
    REQUIRE_FALSE(gitman::svn_browser_url_contains(root_url, u8"https://svn.example.com/repository"));
}

TEST_CASE("SVN browser translates between display paths and URLs", "[application][svn][browser]")
{
    const gitman::svn_repository_browser_state state { browser() };
    REQUIRE(gitman::svn_browser_relative_path(root_url, root_url) == u8"/");
    REQUIRE(gitman::svn_browser_relative_path(root_url, current_url) == u8"/trunk/source");
    REQUIRE(gitman::svn_browser_relative_path(root_url, u8"https://other.example.com/x").empty());
    REQUIRE(gitman::svn_browser_relative_path(u8"file:///", u8"file:///repo/trunk") == u8"/repo/trunk");

    REQUIRE(gitman::resolve_svn_browser_path(state, u8"/trunk/source") == current_url);
    REQUIRE(gitman::resolve_svn_browser_path(state, u8"  trunk/source/  ") == current_url);
    REQUIRE(gitman::resolve_svn_browser_path(state, u8"/") == root_url);
    REQUIRE(gitman::resolve_svn_browser_path(state, current_url) == current_url);
    REQUIRE(gitman::resolve_svn_browser_path(state, u8"/공용 자료") == u8"https://svn.example.com/repo/%EA%B3%B5%EC%9A%A9%20%EC%9E%90%EB%A3%8C");
    REQUIRE(gitman::resolve_svn_browser_path(state, u8"/trunk/../etc").empty());
    REQUIRE(gitman::resolve_svn_browser_path(state, u8"   ").empty());
}

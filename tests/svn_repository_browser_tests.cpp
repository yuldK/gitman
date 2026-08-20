#include "application/svn_repository_browser.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {
    constexpr std::u8string_view root_url { u8"https://svn.example.com/repo" };
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

TEST_CASE("SVN browser expands the path to the current directory lazily", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { browser() };
    REQUIRE(state.selected_url == current_url);
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));

    {
        const std::vector<gitman::svn_repository_browser_row> rows { gitman::build_svn_repository_browser_rows(state) };
        REQUIRE(rows.size() == 2u);
        REQUIRE(rows[0].kind == gitman::svn_browser_row_kind::directory);
        REQUIRE(rows[1].kind == gitman::svn_browser_row_kind::loading);
        REQUIRE(rows[1].text == u8"조회 중…");
    }

    const std::optional<std::u8string> trunk_query { gitman::complete_svn_browser_query(state, root_url, listing({ u8"branches", u8"tags", u8"trunk" })) };
    REQUIRE(trunk_query == u8"https://svn.example.com/repo/trunk");

    const std::optional<std::u8string> next { gitman::complete_svn_browser_query(state, *trunk_query, listing({ u8"docs", u8"source" })) };
    REQUIRE_FALSE(next.has_value());

    const std::vector<gitman::svn_repository_browser_row> rows { gitman::build_svn_repository_browser_rows(state) };
    REQUIRE(rows.size() == 6u);
    REQUIRE(rows[0].text == root_url);
    REQUIRE(rows[3].text == u8"trunk");
    REQUIRE(rows[3].expanded);
    REQUIRE(rows[5].text == u8"source");
    REQUIRE(rows[5].current);
    REQUIRE(rows[5].selected);
    REQUIRE(rows[5].depth == 2u);
}

TEST_CASE("SVN browser reuses a loaded node after collapse", "[application][svn][browser]")
{
    gitman::svn_repository_browser_state state { gitman::make_svn_repository_browser({ std::u8string { root_url }, std::u8string { root_url } }) };
    REQUIRE(gitman::begin_svn_browser_query(state, root_url));
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, listing({ u8"branches", u8"trunk" })).has_value());

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
    REQUIRE_FALSE(gitman::complete_svn_browser_query(state, root_url, failure).has_value());

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

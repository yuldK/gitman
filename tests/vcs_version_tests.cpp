#include "domain/vcs_tool.h"
#include "infrastructure/vcs_version.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    bool parses_to(const gitman::repository_kind kind, const std::u8string_view line, const gitman::vcs_tool_version& expected)
    {
        const auto parsed { gitman::parse_vcs_tool_version(kind, line) };
        return parsed.has_value() && *parsed == expected;
    }
} // namespace

TEST_CASE("Tool versions expose the approved minimums and stable formatting", "[infrastructure][vcs][version]")
{
    REQUIRE(gitman::minimum_supported_version(gitman::repository_kind::git) == gitman::vcs_tool_version { 2u, 43u, 0u });
    REQUIRE(gitman::minimum_supported_version(gitman::repository_kind::subversion) == gitman::vcs_tool_version { 1u, 14u, 5u });
    // 알 수 없는 종류는 Git 기준을 쓴다. 판정 자체가 없는 것보다 안전하다.
    REQUIRE(gitman::minimum_supported_version(gitman::repository_kind::unknown) == gitman::vcs_tool_version { 2u, 43u, 0u });

    REQUIRE(u8_equal(gitman::format_vcs_tool_version({ 2u, 52u, 0u }), u8"2.52.0"));
    REQUIRE(u8_equal(gitman::format_vcs_tool_version({}), u8"0.0.0"));
    REQUIRE(u8_equal(gitman::format_vcs_tool_version({ 10u, 100u, 1000u }), u8"10.100.1000"));
}

TEST_CASE("Version parsing accepts the real Git and Subversion banners", "[infrastructure][vcs][version]")
{
    REQUIRE(parses_to(gitman::repository_kind::git, u8"git version 2.52.0.windows.1", { 2u, 52u, 0u }));
    REQUIRE(parses_to(gitman::repository_kind::git, u8"git version 2.43.0", { 2u, 43u, 0u }));
    REQUIRE(parses_to(gitman::repository_kind::subversion, u8"svn, version 1.14.5 (r1922182)", { 1u, 14u, 5u }));
    REQUIRE(parses_to(gitman::repository_kind::subversion, u8"svn, version 1.14.5-SlikSvn (r1922182)", { 1u, 14u, 5u }));

    // patch가 없는 표기도 받아 준다. 이때 patch는 0이다.
    REQUIRE(parses_to(gitman::repository_kind::git, u8"git version 2.43", { 2u, 43u, 0u }));

    // 네 번째 이후 구성 요소는 배포판마다 달라 비교 기준이 될 수 없으므로 버린다.
    REQUIRE(parses_to(gitman::repository_kind::git, u8"git version 2.52.0.windows.1", { 2u, 52u, 0u }));
}

TEST_CASE("Version parsing rejects output without a version token", "[infrastructure][vcs][version]")
{
    REQUIRE_FALSE(gitman::parse_vcs_tool_version(gitman::repository_kind::git, u8"").has_value());
    REQUIRE_FALSE(gitman::parse_vcs_tool_version(gitman::repository_kind::git, u8"git version unknown").has_value());
    REQUIRE_FALSE(gitman::parse_vcs_tool_version(gitman::repository_kind::git, u8"git").has_value());
    // 소수점 없는 단일 숫자는 버전 토큰으로 보지 않는다.
    REQUIRE_FALSE(gitman::parse_vcs_tool_version(gitman::repository_kind::git, u8"git version 2").has_value());
    REQUIRE_FALSE(gitman::parse_vcs_tool_version(gitman::repository_kind::subversion, u8"'svn'은(는) 내부 또는 외부 명령이 아닙니다").has_value());
}

TEST_CASE("Version parsing uses only the first output line", "[infrastructure][vcs][version]")
{
    constexpr std::u8string_view subversion_banner {
        u8"svn, version 1.14.5 (r1922182)\r\n   compiled Apr 1 2025\r\n\r\nCopyright (C) 2025 The Apache Software Foundation.\r\n",
    };
    const auto parsed { gitman::parse_vcs_tool_version_output(gitman::repository_kind::subversion, subversion_banner) };
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == gitman::vcs_tool_version { 1u, 14u, 5u });

    // 첫 줄에 버전이 없으면 뒤에 있어도 찾지 않는다. 첫 줄이 계약이기 때문이다.
    REQUIRE_FALSE(gitman::parse_vcs_tool_version_output(gitman::repository_kind::git, u8"banner\ngit version 2.52.0").has_value());
}

TEST_CASE("Minimum version comparison covers the approved boundaries", "[infrastructure][vcs][version]")
{
    REQUIRE(gitman::meets_minimum_vcs_version(gitman::repository_kind::git, { 2u, 43u, 0u }));
    REQUIRE(gitman::meets_minimum_vcs_version(gitman::repository_kind::git, { 2u, 43u, 1u }));
    REQUIRE(gitman::meets_minimum_vcs_version(gitman::repository_kind::git, { 3u, 0u, 0u }));
    REQUIRE_FALSE(gitman::meets_minimum_vcs_version(gitman::repository_kind::git, { 2u, 42u, 9u }));
    REQUIRE_FALSE(gitman::meets_minimum_vcs_version(gitman::repository_kind::git, { 1u, 99u, 99u }));

    REQUIRE(gitman::meets_minimum_vcs_version(gitman::repository_kind::subversion, { 1u, 14u, 5u }));
    REQUIRE(gitman::meets_minimum_vcs_version(gitman::repository_kind::subversion, { 1u, 15u, 0u }));
    REQUIRE_FALSE(gitman::meets_minimum_vcs_version(gitman::repository_kind::subversion, { 1u, 14u, 4u }));
    REQUIRE_FALSE(gitman::meets_minimum_vcs_version(gitman::repository_kind::subversion, { 1u, 13u, 9u }));
}

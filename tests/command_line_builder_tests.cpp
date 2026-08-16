#include "infrastructure/command_line_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace {
    std::u8string quote_argument(const std::u8string_view argument)
    {
        std::u8string target {};
        gitman::append_windows_command_line_argument(target, argument);
        return target;
    }

    std::u8string build(const std::u8string_view executable, const std::vector<std::u8string>& arguments)
    {
        return gitman::build_windows_command_line(executable, std::span<const std::u8string> { arguments });
    }
} // namespace

TEST_CASE("Plain arguments are passed without quotes", "[infrastructure][command_line]")
{
    REQUIRE(quote_argument(u8"status") == u8"status");
    REQUIRE(quote_argument(u8"--porcelain=v2") == u8"--porcelain=v2");
    REQUIRE(quote_argument(u8"C:\\work\\repository") == u8"C:\\work\\repository");
    REQUIRE(quote_argument(u8"한글-브랜치") == u8"한글-브랜치");
}

TEST_CASE("Arguments with separators are quoted", "[infrastructure][command_line]")
{
    REQUIRE(quote_argument(u8"a b") == u8"\"a b\"");
    REQUIRE(quote_argument(u8"a\tb") == u8"\"a\tb\"");
    REQUIRE(quote_argument(u8"a\nb") == u8"\"a\nb\"");
    REQUIRE(quote_argument(u8"a\vb") == u8"\"a\vb\"");
    // 빈 인자를 인용하지 않으면 자식의 argv에서 사라진다.
    REQUIRE(quote_argument(u8"") == u8"\"\"");
}

TEST_CASE("Quotes and backslashes follow the argv escaping rules", "[infrastructure][command_line]")
{
    // 따옴표는 `\"`가 되고 앞선 backslash는 두 배로 늘어난다.
    REQUIRE(quote_argument(u8"say \"hi\"") == u8"\"say \\\"hi\\\"\"");
    REQUIRE(quote_argument(u8"a\"b") == u8"\"a\\\"b\"");
    REQUIRE(quote_argument(u8"a\\\"b") == u8"\"a\\\\\\\"b\"");
    // 인용이 필요 없으면 backslash를 늘리지 않는다.
    REQUIRE(quote_argument(u8"a\\b") == u8"a\\b");
    REQUIRE(quote_argument(u8"trailing\\") == u8"trailing\\");
    // 닫는 따옴표 바로 앞의 backslash만 두 배로 늘린다.
    REQUIRE(quote_argument(u8"with space\\") == u8"\"with space\\\\\"");
    REQUIRE(quote_argument(u8"with space\\\\") == u8"\"with space\\\\\\\\\"");
}

TEST_CASE("Shell metacharacters stay literal", "[infrastructure][command_line]")
{
    // 셸을 거치지 않으므로 재지향과 연결 문자는 값의 일부다.
    REQUIRE(quote_argument(u8"a&b|c^d>e") == u8"a&b|c^d>e");
    REQUIRE(quote_argument(u8"%PATH%") == u8"%PATH%");
    REQUIRE(quote_argument(u8"$(command)") == u8"$(command)");
}

TEST_CASE("Command lines always quote the executable and separate arguments", "[infrastructure][command_line]")
{
    REQUIRE(build(u8"C:/Program Files/Git/cmd/git.exe", {}) == u8"\"C:/Program Files/Git/cmd/git.exe\"");
    REQUIRE(build(u8"C:/tools/git.exe", { u8"status" }) == u8"\"C:/tools/git.exe\" status");

    const std::vector<std::u8string> arguments { u8"switch", u8"--no-guess", u8"feature/한글 이름", u8"" };
    REQUIRE(build(u8"C:/tools/git.exe", arguments) == u8"\"C:/tools/git.exe\" switch --no-guess \"feature/한글 이름\" \"\"");
}

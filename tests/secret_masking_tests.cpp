#include "infrastructure/secret_masking.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {
    void check_masked(const std::u8string_view input, const std::u8string_view expected)
    {
        const std::u8string actual { gitman::mask_secrets(input) };
        REQUIRE(actual == expected);
        // 이미 가려진 문자열을 다시 넣어도 결과가 같아야 한다.
        REQUIRE(gitman::mask_secrets(actual) == actual);
    }

    void check_unchanged(const std::u8string_view input)
    {
        check_masked(input, input);
    }
} // namespace

TEST_CASE("URL user information is masked", "[infrastructure][masking]")
{
    check_masked(u8"https://user:secret@example.com/repo.git", u8"https://user:***@example.com/repo.git");
    check_masked(u8"svn+ssh://user:pw@svn.example.com/trunk", u8"svn+ssh://user:***@svn.example.com/trunk");
    // 사용자 이름 없이 들어간 값은 대개 token이므로 전체를 가린다.
    check_masked(u8"https://ghp_0123456789abcdef@github.com/o/r.git", u8"https://***@github.com/o/r.git");
    check_masked(u8"fatal: could not read from https://a:b@h/x", u8"fatal: could not read from https://a:***@h/x");
    // percent-encoding 없이 password에 `@`가 들어가도 마지막 `@`까지 전부 가린다.
    check_masked(u8"https://user:p@ss@example.com/repo.git", u8"https://user:***@example.com/repo.git");
    check_masked(u8"remote: https://user:pw@a/x and https://other:pw@b/y", u8"remote: https://user:***@a/x and https://other:***@b/y");
}

TEST_CASE("URLs without user information stay intact", "[infrastructure][masking]")
{
    check_unchanged(u8"https://example.com/repo.git");
    check_unchanged(u8"https://example.com/path?query=1#fragment");
    // SSH 축약 형태에는 `://`가 없으므로 URL 규칙이 동작하지 않는다.
    check_unchanged(u8"git@github.com:owner/repo.git");
}

TEST_CASE("Credential option values are masked", "[infrastructure][masking]")
{
    check_masked(u8"svn update --username kim --password s3cr3t --non-interactive", u8"svn update --username kim --password *** --non-interactive");
    check_masked(u8"--password=s3cr3t", u8"--password=***");
    check_masked(u8"--private-token=abc123", u8"--private-token=***");
    check_masked(u8"--access-token xyz", u8"--access-token ***");
    // 명령줄 인용이 남아 있으면 따옴표 안쪽만 가린다.
    check_masked(u8"--password \"quoted secret\" --next", u8"--password \"***\" --next");
    // 값이 없으면 가릴 것도 없다.
    check_unchanged(u8"--password");
}

TEST_CASE("Option names that only share a prefix are untouched", "[infrastructure][masking]")
{
    check_unchanged(u8"--password-from-stdin");
    check_unchanged(u8"--passwordless");
    check_unchanged(u8"--no-password=keep");
}

TEST_CASE("Header style credentials are masked to the end of the line", "[infrastructure][masking]")
{
    check_masked(u8"Authorization: Basic dXNlcjpwYXNz", u8"Authorization: ***");
    check_masked(u8"authorization: Bearer abcdef", u8"authorization: ***");
    check_masked(u8"PRIVATE-TOKEN: glpat-abcdefghijkl", u8"PRIVATE-TOKEN: ***");
    check_masked(u8"x-access-token: abcdef", u8"x-access-token: ***");
    check_masked(u8"Basic dXNlcjpwYXNzd29yZA==", u8"Basic ***");
    // 짧은 값은 자격 증명으로 보지 않는다.
    check_unchanged(u8"Basic auth");
}

TEST_CASE("Known token prefixes are masked", "[infrastructure][masking]")
{
    check_masked(u8"token ghp_0123456789abcdefghij expired", u8"token *** expired");
    check_masked(u8"github_pat_11ABCDEFG0abcdefg", u8"***");
    check_masked(u8"glpat-ABCDEFGHIJ", u8"***");
    check_masked(u8"gho_abcdef ghs_123456", u8"*** ***");
    // 값이 없는 접두어와 다른 단어의 일부는 건드리지 않는다.
    check_unchanged(u8"ghp_");
    check_unchanged(u8"xghp_abcdefg");
}

TEST_CASE("Ordinary output is never altered", "[infrastructure][masking]")
{
    check_unchanged(u8"");
    check_unchanged(u8"branch main is up to date");
    check_unchanged(u8"한글 출력과 경로 C:/work/repo");
    check_unchanged(u8"1 file changed, 2 insertions(+), 1 deletion(-)");
    check_unchanged(u8"commit 0123456789abcdef0123456789abcdef01234567");
}

TEST_CASE("Several secrets on one line are all masked", "[infrastructure][masking]")
{
    check_masked(u8"clone https://u:p@h --password=x ghp_abcdefghij", u8"clone https://u:***@h --password=*** ***");
}

TEST_CASE("Masking prefers hiding adjacent punctuation over leaking a secret", "[infrastructure][masking]")
{
    REQUIRE(gitman::secret_mask == u8"***");
    // 값의 끝은 공백이나 줄 끝으로 판정하므로 붙어 있는 구두점도 함께 가려진다.
    check_masked(u8"[--password=hunter2]", u8"[--password=***");
    check_masked(u8"url=https://u:p@h,", u8"url=https://u:***@h,");
}

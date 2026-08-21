#include "presentation/status_presentation.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }
} // namespace

TEST_CASE("Sync state glyphs follow the plan table with Korean tooltips", "[presentation][app]")
{
    // plan.md 3.2의 표 전수다. 아이콘만으로 의미를 전달하지 않도록 툴팁을 함께
    // 확인한다.
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::up_to_date, 0, 0).codicon, u8"pass"));
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::up_to_date, 0, 0).tooltip == u8"최신 상태");
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::behind, 0, 3).codicon, u8"arrow-down"));
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::behind, 0, 3).tooltip == u8"원격보다 뒤처짐: 3");
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::ahead, 2, 0).codicon, u8"arrow-up"));
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::ahead, 2, 0).tooltip == u8"로컬이 앞섬: 2");
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::diverged, 2, 3).codicon, u8"git-compare"));
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::diverged, 2, 3).tooltip == u8"분기됨: 로컬 2 · 원격 3");
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::unknown, 0, 0).codicon, u8"question"));
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::authentication_required, 0, 0).codicon, u8"key"));
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::authentication_required, 0, 0).tooltip == u8"인증 필요");
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::local_only, 0, 0).codicon, u8"home"));
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::remote_target_missing, 0, 0).codicon, u8"warning"));
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::offline, 0, 0).codicon, u8"debug-disconnect"));
    REQUIRE(u8_equal(gitman::sync_state_glyph(gitman::remote_sync_state::error, 0, 0).codicon, u8"error"));
}

TEST_CASE("Availability glyphs cover every unqueryable state", "[presentation][app]")
{
    REQUIRE(u8_equal(gitman::availability_glyph(gitman::repository_availability::tool_unavailable).codicon, u8"circle-slash"));
    REQUIRE(u8_equal(gitman::availability_glyph(gitman::repository_availability::not_a_repository).codicon, u8"error"));
    REQUIRE(gitman::availability_glyph(gitman::repository_availability::not_a_repository).tooltip == u8"저장소가 아님");
    REQUIRE(u8_equal(gitman::availability_glyph(gitman::repository_availability::unsupported_layout).codicon, u8"warning"));
    REQUIRE(u8_equal(gitman::availability_glyph(gitman::repository_availability::path_unavailable).codicon, u8"warning"));
    REQUIRE(u8_equal(gitman::availability_glyph(gitman::repository_availability::unknown).codicon, u8"question"));
}

TEST_CASE("Only the question glyphs are marked undetermined", "[presentation][app]")
{
    // 확인되지 않은 상태(`?`)만 undetermined다. 렌더러가 이 flag로 상태 강조색
    // 대신 비활성 계열을 고른다 (global-settings-and-ui-fixes-design G5).
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::unknown, 0, 0).undetermined);
    REQUIRE(gitman::availability_glyph(gitman::repository_availability::unknown).undetermined);
    REQUIRE(gitman::availability_glyph(gitman::repository_availability::ready).undetermined);

    // 의미가 판정된 상태는 강조색을 유지한다. 오류·경고도 undetermined가 아니다.
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::up_to_date, 0, 0).undetermined == false);
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::behind, 0, 3).undetermined == false);
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::error, 0, 0).undetermined == false);
    REQUIRE(gitman::sync_state_glyph(gitman::remote_sync_state::offline, 0, 0).undetermined == false);
    REQUIRE(gitman::availability_glyph(gitman::repository_availability::not_a_repository).undetermined == false);
    REQUIRE(gitman::availability_glyph(gitman::repository_availability::tool_unavailable).undetermined == false);
}

TEST_CASE("Working tree summaries collapse to readable Korean text", "[presentation][app]")
{
    gitman::working_tree_summary summary {};
    REQUIRE(gitman::working_tree_summary_text(summary).empty());

    summary.modified_count = 2;
    summary.untracked_count = 5;
    REQUIRE(gitman::working_tree_summary_text(summary) == u8"변경 2 · 미추적 5");

    summary.conflicted_count = 1;
    summary.operation_in_progress = true;
    summary.is_detached = true;
    REQUIRE(gitman::working_tree_summary_text(summary) == u8"변경 2 · 미추적 5 · 충돌 1 · 진행 중 작업 있음 · detached HEAD");
}

TEST_CASE("Git revisions are shortened only for display", "[presentation][app]")
{
    constexpr std::u8string_view sha256 { u8"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" };
    REQUIRE(gitman::revision_display_text(gitman::repository_kind::git, sha256) == u8"0123456");

    // 표시 계층은 값의 형식을 검증하지 않고 길이만 줄인다.
    REQUIRE(gitman::revision_display_text(gitman::repository_kind::git, u8"not-a-hash") == u8"not-a-h");
    REQUIRE(gitman::revision_display_text(gitman::repository_kind::git, u8"abc123") == u8"abc123");
    REQUIRE(gitman::revision_display_text(gitman::repository_kind::subversion, u8"123456789") == u8"123456789");
}

TEST_CASE("Card view state names stay stable", "[presentation][app]")
{
    REQUIRE(u8_equal(gitman::card_view_state_name(gitman::card_view_state::loading), u8"loading"));
    REQUIRE(u8_equal(gitman::card_view_state_name(gitman::card_view_state::ready), u8"ready"));
    REQUIRE(u8_equal(gitman::card_view_state_name(gitman::card_view_state::running), u8"running"));
    REQUIRE(u8_equal(gitman::card_view_state_name(gitman::card_view_state::warning), u8"warning"));
    REQUIRE(u8_equal(gitman::card_view_state_name(gitman::card_view_state::failed), u8"failed"));
    REQUIRE(u8_equal(gitman::card_view_state_name(gitman::card_view_state::disabled), u8"disabled"));
    REQUIRE(u8_equal(gitman::card_view_state_name(static_cast<gitman::card_view_state>(-1)), u8"unknown"));
}

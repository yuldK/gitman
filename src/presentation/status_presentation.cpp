#include "presentation/status_presentation.h"

namespace gitman {
    namespace {
        std::u8string append_count(std::u8string text, const std::uint64_t count)
        {
            const std::string digits { std::to_string(count) };
            text.append(digits.begin(), digits.end());
            return text;
        }

        void append_counted_part(std::u8string& text, const std::u8string_view label, const std::uint64_t count)
        {
            if (count == 0)
                return;
            if (text.empty() == false)
                text.append(u8" · ");
            text.append(label);
            const std::string digits { std::to_string(count) };
            text.append(digits.begin(), digits.end());
        }
    } // namespace

    status_glyph sync_state_glyph(const remote_sync_state state, const std::uint64_t ahead_count, const std::uint64_t behind_count)
    {
        switch (state)
        {
        case remote_sync_state::up_to_date:
            return { u8"pass", u8"최신 상태" };
        case remote_sync_state::behind:
            return { u8"arrow-down", append_count(u8"원격보다 뒤처짐: ", behind_count) };
        case remote_sync_state::ahead:
            return { u8"arrow-up", append_count(u8"로컬이 앞섬: ", ahead_count) };
        case remote_sync_state::diverged:
            return { u8"git-compare", append_count(append_count(u8"분기됨: 로컬 ", ahead_count) + u8" · 원격 ", behind_count) };
        case remote_sync_state::local_only:
            return { u8"home", u8"로컬 저장소 기준" };
        case remote_sync_state::remote_target_missing:
            return { u8"warning", u8"비교할 원격 브랜치 없음" };
        case remote_sync_state::authentication_required:
            return { u8"key", u8"인증 필요" };
        case remote_sync_state::offline:
            return { u8"debug-disconnect", u8"오프라인" };
        case remote_sync_state::error:
            return { u8"error", u8"오류로 판정 불가" };
        case remote_sync_state::unknown:
            break;
        }
        return { u8"question", u8"확인되지 않음" };
    }

    status_glyph availability_glyph(const repository_availability availability)
    {
        switch (availability)
        {
        case repository_availability::tool_unavailable:
            return { u8"circle-slash", u8"VCS 도구를 사용할 수 없음" };
        case repository_availability::not_a_repository:
            return { u8"error", u8"저장소가 아님" };
        case repository_availability::unsupported_layout:
            return { u8"warning", u8"지원하지 않는 저장소 배치" };
        case repository_availability::path_unavailable:
            return { u8"warning", u8"경로를 사용할 수 없음" };
        case repository_availability::ready:
        case repository_availability::unknown:
            break;
        }
        return { u8"question", u8"확인되지 않음" };
    }

    std::u8string working_tree_summary_text(const working_tree_summary& summary)
    {
        std::u8string text {};
        append_counted_part(text, u8"변경 ", summary.modified_count);
        append_counted_part(text, u8"미추적 ", summary.untracked_count);
        append_counted_part(text, u8"충돌 ", summary.conflicted_count);

        if (summary.operation_in_progress)
        {
            if (text.empty() == false)
                text.append(u8" · ");
            text.append(u8"진행 중 작업 있음");
        }
        if (summary.is_detached)
        {
            if (text.empty() == false)
                text.append(u8" · ");
            text.append(u8"detached HEAD");
        }
        return text;
    }

    std::u8string_view revision_display_text(const repository_kind kind, const std::u8string_view revision) noexcept
    {
        if (kind == repository_kind::git && revision.size() > displayed_git_revision_length)
            return revision.substr(0, displayed_git_revision_length);
        return revision;
    }

    std::u8string_view card_view_state_name(const card_view_state state) noexcept
    {
        switch (state)
        {
        case card_view_state::loading:
            return u8"loading";
        case card_view_state::ready:
            return u8"ready";
        case card_view_state::running:
            return u8"running";
        case card_view_state::warning:
            return u8"warning";
        case card_view_state::failed:
            return u8"failed";
        case card_view_state::disabled:
            return u8"disabled";
        }
        return u8"unknown";
    }
} // namespace gitman

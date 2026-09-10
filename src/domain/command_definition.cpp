#include "domain/command_definition.h"

#include <algorithm>
#include <cstddef>

namespace gitman {
    namespace {
        // 경로 비교의 정규화다. 사용자는 문서에 `\`와 `/`를 섞어 쓰고 Windows는
        // 대소문자를 구분하지 않으므로, 패턴과 대상을 같은 모양으로 만든 뒤 비교한다.
        char8_t normalize_pattern_character(const char8_t value) noexcept
        {
            if (value == u8'\\')
                return u8'/';
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }
    } // namespace

    bool svn_hook_settings::is_default() const noexcept
    {
        return use_tortoise_svn && hooks.empty();
    }

    bool command_catalog::is_default() const noexcept
    {
        return custom.empty() && svn_hooks.is_default();
    }

    bool command_catalog_overrides::empty() const noexcept
    {
        return custom.has_value() == false && svn_hooks.has_value() == false;
    }

    command_catalog apply_overrides(const command_catalog& base, const command_catalog_overrides& overrides)
    {
        command_catalog result { base };
        if (overrides.custom.has_value())
            result.custom = *overrides.custom;
        if (overrides.svn_hooks.has_value())
            result.svn_hooks = *overrides.svn_hooks;
        return result;
    }

    bool command_pattern_matches(const std::u8string_view pattern, const std::u8string_view text)
    {
        // 빈 패턴은 거르지 않는다는 뜻이다.
        if (pattern.empty())
            return true;

        // `*`를 만나면 그 자리를 기억해 두고, 뒤가 어긋나면 한 글자씩 미뤄 다시
        // 맞춰 본다. 재귀 없이 도는 표준 backtracking이다.
        std::size_t pattern_index { 0 };
        std::size_t text_index { 0 };
        std::size_t star_index { pattern.size() };
        std::size_t star_text_index { 0 };

        while (text_index < text.size())
        {
            const bool in_pattern { pattern_index < pattern.size() };
            if (in_pattern && pattern[pattern_index] == u8'?')
            {
                ++pattern_index;
                ++text_index;
                continue;
            }
            if (in_pattern && pattern[pattern_index] == u8'*')
            {
                star_index = pattern_index;
                star_text_index = text_index;
                ++pattern_index;
                continue;
            }
            if (in_pattern && normalize_pattern_character(pattern[pattern_index]) == normalize_pattern_character(text[text_index]))
            {
                ++pattern_index;
                ++text_index;
                continue;
            }
            if (star_index == pattern.size())
                return false;

            pattern_index = star_index + 1;
            ++star_text_index;
            text_index = star_text_index;
        }

        while (pattern_index < pattern.size() && pattern[pattern_index] == u8'*')
            ++pattern_index;
        return pattern_index == pattern.size();
    }

    bool command_matches(const custom_command& command, const command_match_context& context)
    {
        if (custom_command_error(command).empty() == false)
            return false;

        // VCS를 지정한 명령은 카드의 종류가 확인된 뒤에만 보인다. `any`인 맥락은
        // 아직 조회가 끝나지 않았다는 뜻이라, 눌러 봐야 실패를 아는 상태를 피한다.
        if (command.vcs != command_target_vcs::any && command.vcs != context.vcs)
            return false;

        if (command.projects.empty() == false)
        {
            const bool listed { std::ranges::find(command.projects, context.project_id) != command.projects.end() };
            if (listed == false)
                return false;
        }

        if (command.path_pattern.empty())
            return true;
        // 경로와 표시 이름 중 하나만 맞아도 된다. 사용자는 둘 중 익숙한 쪽으로 쓴다.
        return command_pattern_matches(command.path_pattern, context.repository_path) || command_pattern_matches(command.path_pattern, context.display_name);
    }

    bool svn_hook_matches(const svn_hook& hook, const std::u8string_view repository_path)
    {
        if (hook.enabled == false || svn_hook_error(hook).empty() == false)
            return false;
        return command_pattern_matches(hook.path_pattern, repository_path);
    }

    std::u8string_view custom_command_error(const custom_command& command) noexcept
    {
        if (command.id.empty())
            return u8"명령 id가 비어 있습니다.";
        // id는 메뉴 key와 element id의 재료다. 구분자로 쓰는 제어 문자가 들어오면
        // 다른 명령과 id가 겹칠 수 있다.
        for (const char8_t value : command.id)
            if (value < u8' ')
                return u8"명령 id에 제어 문자를 쓸 수 없습니다.";
        if (command.label.empty())
            return u8"명령 이름이 비어 있습니다.";
        if (command.script.empty())
            return u8"실행할 스크립트 경로가 비어 있습니다.";
        if (command.timeout_seconds < 0)
            return u8"제한 시간은 음수일 수 없습니다.";
        return {};
    }

    std::u8string_view svn_hook_error(const svn_hook& hook) noexcept
    {
        if (hook.script.empty())
            return u8"실행할 훅 스크립트 경로가 비어 있습니다.";
        if (hook.timeout_seconds < 0)
            return u8"제한 시간은 음수일 수 없습니다.";
        return {};
    }

    std::optional<command_target_vcs> parse_command_target_vcs(const std::u8string_view value) noexcept
    {
        if (value == u8"any")
            return command_target_vcs::any;
        if (value == u8"git")
            return command_target_vcs::git;
        if (value == u8"subversion")
            return command_target_vcs::subversion;
        return std::nullopt;
    }

    std::u8string_view command_target_vcs_name(const command_target_vcs value) noexcept
    {
        switch (value)
        {
        case command_target_vcs::git:
            return u8"git";
        case command_target_vcs::subversion:
            return u8"subversion";
        case command_target_vcs::any:
        default:
            return u8"any";
        }
    }

    std::optional<command_working_directory> parse_command_working_directory(const std::u8string_view value) noexcept
    {
        if (value == u8"repository")
            return command_working_directory::repository;
        if (value == u8"document")
            return command_working_directory::document;
        if (value == u8"script")
            return command_working_directory::script;
        return std::nullopt;
    }

    std::u8string_view command_working_directory_name(const command_working_directory value) noexcept
    {
        switch (value)
        {
        case command_working_directory::document:
            return u8"document";
        case command_working_directory::script:
            return u8"script";
        case command_working_directory::repository:
        default:
            return u8"repository";
        }
    }

    std::optional<svn_hook_phase> parse_svn_hook_phase(const std::u8string_view value) noexcept
    {
        if (value == u8"start_update")
            return svn_hook_phase::start_update;
        if (value == u8"pre_update")
            return svn_hook_phase::pre_update;
        if (value == u8"post_update")
            return svn_hook_phase::post_update;
        return std::nullopt;
    }

    std::u8string_view svn_hook_phase_name(const svn_hook_phase value) noexcept
    {
        switch (value)
        {
        case svn_hook_phase::start_update:
            return u8"start_update";
        case svn_hook_phase::post_update:
            return u8"post_update";
        case svn_hook_phase::pre_update:
        default:
            return u8"pre_update";
        }
    }
} // namespace gitman

#include "infrastructure/tortoise_svn_hooks.h"

#include <array>
#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        // TortoiseSVN이 쓰는 종류 이름 전부다. update 계열만 단계로 옮기고 나머지는
        // 버리되, record 경계를 다시 맞추려면 "이것도 종류 이름이다"를 알아야 한다.
        constexpr std::array<std::u8string_view, 11> tortoise_hook_types {
            u8"start_commit_hook",
            u8"check_commit_hook",
            u8"pre_commit_hook",
            u8"post_commit_hook",
            u8"start_update_hook",
            u8"pre_update_hook",
            u8"post_update_hook",
            u8"pre_connect_hook",
            u8"manual_precommit_hook",
            u8"pre_lock_hook",
            u8"post_lock_hook",
        };

        std::u8string_view trim(std::u8string_view value) noexcept
        {
            while (value.empty() == false && (value.front() == u8' ' || value.front() == u8'\t' || value.front() == u8'\r'))
                value.remove_prefix(1);
            while (value.empty() == false && (value.back() == u8' ' || value.back() == u8'\t' || value.back() == u8'\r'))
                value.remove_suffix(1);
            return value;
        }

        char8_t lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            if (value == u8'\\')
                return u8'/';
            return value;
        }

        // Windows 경로 비교다. 대소문자를 무시하고 구분자를 같게 보며, 끝의 구분자를
        // 무시한다.
        bool same_path(std::u8string_view left, std::u8string_view right) noexcept
        {
            while (left.empty() == false && (left.back() == u8'\\' || left.back() == u8'/'))
                left.remove_suffix(1);
            while (right.empty() == false && (right.back() == u8'\\' || right.back() == u8'/'))
                right.remove_suffix(1);
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
                if (lower(left[index]) != lower(right[index]))
                    return false;
            return true;
        }

        // 경로의 부모다. 더 올라갈 곳이 없으면 빈 값이다.
        std::u8string_view parent_of(std::u8string_view path) noexcept
        {
            while (path.empty() == false && (path.back() == u8'\\' || path.back() == u8'/'))
                path.remove_suffix(1);
            const std::size_t separator { path.find_last_of(u8"/\\") };
            if (separator == std::u8string_view::npos)
                return {};
            return path.substr(0, separator);
        }

        std::vector<std::u8string_view> split_lines(const std::u8string_view value)
        {
            std::vector<std::u8string_view> lines {};
            std::size_t begin { 0 };
            for (std::size_t index = 0; index <= value.size(); ++index)
            {
                if (index != value.size() && value[index] != u8'\n')
                    continue;
                lines.push_back(value.substr(begin, index - begin));
                begin = index + 1;
            }
            // 마지막 개행 뒤의 빈 줄은 record가 아니다.
            while (lines.empty() == false && trim(lines.back()).empty())
                lines.pop_back();
            return lines;
        }
    } // namespace

    std::optional<svn_hook_phase> parse_tortoise_hook_type(const std::u8string_view value) noexcept
    {
        if (value == u8"start_update_hook")
            return svn_hook_phase::start_update;
        if (value == u8"pre_update_hook")
            return svn_hook_phase::pre_update;
        if (value == u8"post_update_hook")
            return svn_hook_phase::post_update;
        return std::nullopt;
    }

    bool is_tortoise_hook_type(const std::u8string_view value) noexcept
    {
        for (const std::u8string_view name : tortoise_hook_types)
            if (name == value)
                return true;
        return false;
    }

    std::vector<tortoise_svn_hook> parse_tortoise_svn_hooks(const std::u8string_view registry_value)
    {
        const std::vector<std::u8string_view> lines { split_lines(registry_value) };
        std::vector<tortoise_svn_hook> hooks {};

        std::size_t index { 0 };
        while (index < lines.size())
        {
            const std::u8string_view type { trim(lines[index]) };
            if (is_tortoise_hook_type(type) == false)
            {
                // 종류 이름이 아니면 record 경계가 어긋난 것이다. 한 줄씩 밀며 다시
                // 맞춘다 — 옛 판이 다섯 줄을 쓰던 흔적(ticket #482)이 남아 있을 수 있다.
                ++index;
                continue;
            }
            if (index + 3 >= lines.size())
                break;

            tortoise_svn_hook hook {};
            hook.working_copy_path = trim(lines[index + 1]);
            hook.command_line = trim(lines[index + 2]);
            hook.wait = trim(lines[index + 3]) != u8"false";

            std::size_t consumed { 4 };
            if (index + 4 < lines.size() && is_tortoise_hook_type(trim(lines[index + 4])) == false)
            {
                hook.show_console = trim(lines[index + 4]) == u8"show";
                consumed = 5;
                // 여섯 번째 줄은 판번에 따라 없다. 다음 줄이 종류 이름이면 그것이
                // 다음 record의 시작이다.
                if (index + 5 < lines.size() && is_tortoise_hook_type(trim(lines[index + 5])) == false)
                {
                    hook.enforce = trim(lines[index + 5]) == u8"enforce";
                    consumed = 6;
                }
            }

            // gitman이 하지 않는 작업의 훅은 버린다. 자리는 그대로 넘긴다.
            if (const std::optional<svn_hook_phase> phase { parse_tortoise_hook_type(type) }; phase.has_value() && hook.command_line.empty() == false)
            {
                hook.phase = *phase;
                hooks.push_back(std::move(hook));
            }
            index += consumed;
        }
        return hooks;
    }

    const tortoise_svn_hook* select_tortoise_svn_hook(const std::vector<tortoise_svn_hook>& hooks, const svn_hook_phase phase, const std::u8string_view working_copy_path)
    {
        // 경로에서 부모로 올라가며 정확히 일치하는 것을 먼저 찾는다. 더 가까운
        // 정의가 이긴다.
        std::u8string_view current { working_copy_path };
        while (current.empty() == false)
        {
            for (const tortoise_svn_hook& hook : hooks)
                if (hook.phase == phase && same_path(hook.working_copy_path, current))
                    return &hook;
            current = parent_of(current);
        }

        for (const tortoise_svn_hook& hook : hooks)
            if (hook.phase == phase && hook.working_copy_path == u8"*")
                return &hook;
        return nullptr;
    }

    tortoise_command_line split_tortoise_command_line(const std::u8string_view command_line)
    {
        tortoise_command_line result {};
        std::size_t index { 0 };
        const auto skip_spaces = [&command_line, &index]() {
            while (index < command_line.size() && (command_line[index] == u8' ' || command_line[index] == u8'\t'))
                ++index;
        };

        // 따옴표 안의 공백은 구분자가 아니다. 그 밖의 규칙(역슬래시 이스케이프)은
        // TortoiseSVN 설정 화면이 만들지 않으므로 다루지 않는다.
        const auto next_token = [&command_line, &index]() {
            std::u8string token {};
            bool quoted { false };
            while (index < command_line.size())
            {
                const char8_t value { command_line[index] };
                if (value == u8'"')
                {
                    quoted = quoted == false;
                    ++index;
                    continue;
                }
                if (quoted == false && (value == u8' ' || value == u8'\t'))
                    break;
                token.push_back(value);
                ++index;
            }
            return token;
        };

        skip_spaces();
        result.program = next_token();
        while (true)
        {
            skip_spaces();
            if (index >= command_line.size())
                break;
            std::u8string token { next_token() };
            if (token.empty())
                continue;
            result.arguments.push_back(std::move(token));
        }
        return result;
    }
} // namespace gitman

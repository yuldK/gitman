#include "infrastructure/vcs_tool_discovery.h"

#include "application/process_request.h"
#include "application/vcs_tool_registry.h"
#include "infrastructure/vcs_command_runner.h"
#include "infrastructure/vcs_execution_policy.h"
#include "infrastructure/vcs_version.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace gitman {
    namespace {
        char8_t ascii_uppercase(const char8_t value) noexcept
        {
            if (value >= u8'a' && value <= u8'z')
                return static_cast<char8_t>(value - u8'a' + u8'A');
            return value;
        }

        bool ascii_equal_ignoring_case(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
                if (ascii_uppercase(left[index]) != ascii_uppercase(right[index]))
                    return false;
            return true;
        }

        bool is_separator(const char8_t value) noexcept
        {
            return value == u8'\\' || value == u8'/';
        }

        std::u8string_view trim_ascii_spaces(std::u8string_view value) noexcept
        {
            while (value.empty() == false && (value.front() == u8' ' || value.front() == u8'\t'))
                value.remove_prefix(1);
            while (value.empty() == false && (value.back() == u8' ' || value.back() == u8'\t'))
                value.remove_suffix(1);
            return value;
        }

        std::u8string strip_quotes(const std::u8string_view value)
        {
            std::u8string result {};
            result.reserve(value.size());
            for (const char8_t character : value)
                if (character != u8'"')
                    result.push_back(character);
            return result;
        }

        void remove_trailing_separators(std::u8string& value) noexcept
        {
            // 드라이브 루트(`C:\`)는 마지막 구분자를 남겨야 경로가 유효하다.
            while (value.size() > 1 && is_separator(value.back()))
            {
                if (value.size() == 3 && value[1] == u8':')
                    return;
                value.pop_back();
            }
        }

        std::u8string join_path(const std::u8string_view directory, const std::u8string_view file)
        {
            std::u8string result { directory };
            if (result.empty() == false && is_separator(result.back()) == false)
                result.push_back(u8'\\');
            result.append(file);
            return result;
        }

        void add_unique_candidate(std::vector<vcs_tool_candidate>& candidates, std::u8string executable)
        {
            const auto duplicate {
                std::ranges::find_if(candidates, [&executable](const vcs_tool_candidate& value) { return ascii_equal_ignoring_case(value.executable, executable); }),
            };
            if (duplicate != candidates.end())
                return;
            candidates.push_back({ std::move(executable), false });
        }

        // 알려진 기본 설치 위치다. `PATH`에 등록하지 않고 설치하는 배포판이 있어
        // 마지막 순서로만 확인한다.
        std::vector<std::u8string> default_relative_directories(const repository_kind kind)
        {
            if (kind == repository_kind::subversion)
            {
                return {
                    std::u8string { u8"TortoiseSVN\\bin" },
                    std::u8string { u8"SlikSvn\\bin" },
                    std::u8string { u8"Subversion\\bin" },
                };
            }
            return {
                std::u8string { u8"Git\\cmd" },
                std::u8string { u8"Git\\bin" },
            };
        }

        diagnostic make_tool_diagnostic(const diagnostic_code code, const diagnostic_severity severity, const std::u8string_view message)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = message;
            return value;
        }

        vcs_tool_info make_unavailable_tool(const repository_kind kind, const vcs_tool_availability availability, const diagnostic_code code, const bool manually_configured, std::u8string executable)
        {
            vcs_tool_info info {};
            info.kind = kind;
            info.availability = availability;
            info.executable = std::move(executable);
            info.manually_configured = manually_configured;
            // 도구 부재는 앱을 멈추는 오류가 아니다. 해당 VCS 동작만 비활성화되므로
            // warning으로 보고하고 사용자는 다른 VCS를 계속 사용할 수 있다.
            info.diagnostics.push_back(make_tool_diagnostic(code, diagnostic_severity::warning, vcs_tool_unavailable_message(kind, availability)));
            return info;
        }

        std::optional<vcs_tool_candidate> find_existing_candidate(const std::vector<vcs_tool_candidate>& candidates, const vcs_file_probe& probe)
        {
            for (const vcs_tool_candidate& candidate : candidates)
                if (probe.probe(candidate.executable) == vcs_path_kind::file)
                    return candidate;
            return std::nullopt;
        }

        std::u8string find_auxiliary_executable(const repository_kind kind, const std::u8string_view executable, const vcs_file_probe& probe)
        {
            const std::u8string_view auxiliary_name { vcs_auxiliary_executable_name(kind) };
            if (auxiliary_name.empty())
                return {};

            std::u8string auxiliary { join_path(vcs_executable_directory(executable), auxiliary_name) };
            if (probe.probe(auxiliary) != vcs_path_kind::file)
                return {};
            return auxiliary;
        }

        vcs_tool_info probe_tool_version(const repository_kind kind, const vcs_tool_candidate& candidate, process_runner& runner, const vcs_file_probe& probe, const process_cancellation_token& token)
        {
            std::vector<std::u8string> arguments {};
            arguments.push_back(std::u8string { u8"--version" });

            // 작업 디렉터리는 실행 파일이 있는 디렉터리를 쓴다. 단계 3 계약이 절대
            // 경로를 요구하고, 이 시점에는 대상 저장소가 정해지지 않았기 때문이다.
            const process_request request {
                make_vcs_process_request(kind, candidate.executable, vcs_executable_directory(candidate.executable), std::move(arguments), vcs_command_class::tool_probe),
            };
            const vcs_command_result result { run_vcs_command(runner, request, token) };

            if (result.succeeded() == false)
            {
                // 사용자가 직접 지정한 경로가 실행되지 않으면 자동 탐색으로 물러서지
                // 않는다. 의도한 값이 조용히 무시되는 편이 더 위험하다.
                const vcs_tool_availability availability { candidate.manually_configured ? vcs_tool_availability::path_invalid : vcs_tool_availability::version_unreadable };
                const diagnostic_code code { candidate.manually_configured ? diagnostic_code::vcs_tool_path_invalid : diagnostic_code::vcs_tool_version_unreadable };
                return make_unavailable_tool(kind, availability, code, candidate.manually_configured, candidate.executable);
            }

            const std::u8string first_line { result.first_output_line() };
            const std::optional<vcs_tool_version> version { parse_vcs_tool_version(kind, first_line) };
            if (version.has_value() == false)
            {
                vcs_tool_info info {
                    make_unavailable_tool(kind, vcs_tool_availability::version_unreadable, diagnostic_code::vcs_tool_version_unreadable, candidate.manually_configured, candidate.executable),
                };
                info.reported_version = first_line;
                return info;
            }

            vcs_tool_info info {};
            info.kind = kind;
            info.executable = candidate.executable;
            info.manually_configured = candidate.manually_configured;
            info.reported_version = first_line;
            info.version = *version;
            if (meets_minimum_vcs_version(kind, *version) == false)
            {
                info.availability = vcs_tool_availability::too_old;
                info.diagnostics.push_back(make_tool_diagnostic(diagnostic_code::vcs_tool_too_old, diagnostic_severity::warning, vcs_tool_unavailable_message(kind, vcs_tool_availability::too_old)));
                return info;
            }

            info.availability = vcs_tool_availability::available;
            info.auxiliary_executable = find_auxiliary_executable(kind, candidate.executable, probe);
            return info;
        }

        vcs_tool_info resolve_vcs_tool_impl(const repository_kind kind, const std::u8string_view configured_executable, const vcs_tool_environment& environment, process_runner& runner,
            const vcs_file_probe& probe, const process_cancellation_token& token)
        {
            const bool manually_configured { configured_executable.empty() == false };
            if (manually_configured && is_absolute_windows_path(configured_executable) == false)
                return make_unavailable_tool(kind, vcs_tool_availability::path_invalid, diagnostic_code::vcs_tool_path_invalid, true, std::u8string { configured_executable });

            const std::vector<vcs_tool_candidate> candidates { vcs_tool_candidates(kind, configured_executable, environment) };
            const std::optional<vcs_tool_candidate> existing { find_existing_candidate(candidates, probe) };
            if (existing.has_value() == false)
            {
                const vcs_tool_availability availability { manually_configured ? vcs_tool_availability::path_invalid : vcs_tool_availability::not_found };
                const diagnostic_code code { manually_configured ? diagnostic_code::vcs_tool_path_invalid : diagnostic_code::vcs_tool_not_found };
                return make_unavailable_tool(kind, availability, code, manually_configured, manually_configured ? std::u8string { configured_executable } : std::u8string {});
            }

            return probe_tool_version(kind, *existing, runner, probe, token);
        }
    } // namespace

    std::u8string_view vcs_tool_executable_name(const repository_kind kind) noexcept
    {
        return kind == repository_kind::subversion ? u8"svn.exe" : u8"git.exe";
    }

    std::u8string_view vcs_auxiliary_executable_name(const repository_kind kind) noexcept
    {
        return kind == repository_kind::subversion ? u8"svnversion.exe" : u8"";
    }

    std::vector<std::u8string> split_search_path(const std::u8string_view path_environment)
    {
        std::vector<std::u8string> directories {};
        std::size_t begin { 0 };
        while (begin <= path_environment.size())
        {
            const std::size_t end { std::min(path_environment.find(u8';', begin), path_environment.size()) };
            std::u8string entry { strip_quotes(trim_ascii_spaces(path_environment.substr(begin, end - begin))) };
            begin = end + 1;

            remove_trailing_separators(entry);
            if (entry.empty() || is_absolute_windows_path(entry) == false)
                continue;
            const auto duplicate {
                std::ranges::find_if(directories, [&entry](const std::u8string& value) { return ascii_equal_ignoring_case(value, entry); }),
            };
            if (duplicate == directories.end())
                directories.push_back(std::move(entry));
        }
        return directories;
    }

    std::vector<vcs_tool_candidate> vcs_tool_candidates(const repository_kind kind, const std::u8string_view configured_executable, const vcs_tool_environment& environment)
    {
        std::vector<vcs_tool_candidate> candidates {};
        if (configured_executable.empty() == false)
        {
            // 지정 경로가 있으면 다른 후보를 만들지 않는다. 자동 탐색으로 조용히
            // 물러서면 사용자가 지정한 값이 무시된 사실을 알 수 없다.
            candidates.push_back({ std::u8string { configured_executable }, true });
            return candidates;
        }

        const std::u8string_view executable_name { vcs_tool_executable_name(kind) };
        for (const std::u8string& directory : split_search_path(environment.path_environment))
            add_unique_candidate(candidates, join_path(directory, executable_name));

        for (const std::u8string& program_files : environment.program_files_directories)
        {
            if (program_files.empty() || is_absolute_windows_path(program_files) == false)
                continue;
            for (const std::u8string& relative : default_relative_directories(kind))
                add_unique_candidate(candidates, join_path(join_path(program_files, relative), executable_name));
        }
        return candidates;
    }

    std::u8string vcs_executable_directory(const std::u8string_view executable)
    {
        std::u8string value { executable };
        remove_trailing_separators(value);
        const std::size_t separator { value.find_last_of(u8"/\\") };
        if (separator == std::u8string::npos)
            return value;
        if (separator == 2 && value.size() >= 3 && value[1] == u8':')
        {
            value.resize(3);
            return value;
        }
        value.resize(separator);
        return value;
    }

    vcs_tool_info resolve_vcs_tool(const repository_kind kind, const std::u8string_view configured_executable, const vcs_tool_environment& environment, process_runner& runner,
        const vcs_file_probe& probe, const process_cancellation_token& token) noexcept
    {
        try
        {
            return resolve_vcs_tool_impl(kind, configured_executable, environment, runner, probe, token);
        }
        catch (...)
        {
            vcs_tool_info info {};
            info.kind = kind;
            info.availability = vcs_tool_availability::unknown;
            return info;
        }
    }

    vcs_tool_set resolve_vcs_tools(
        const workspace_settings& settings, const vcs_tool_environment& environment, process_runner& runner, const vcs_file_probe& probe, const process_cancellation_token& token) noexcept
    {
        vcs_tool_set tools {};
        tools.git = resolve_vcs_tool(repository_kind::git, settings.git_executable, environment, runner, probe, token);
        tools.subversion = resolve_vcs_tool(repository_kind::subversion, settings.svn_executable, environment, runner, probe, token);
        return tools;
    }
} // namespace gitman

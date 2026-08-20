#include "infrastructure/local_change_reader.h"

#include "domain/local_changes.h"

#include <utility>

namespace gitman {
    namespace {
        bool looks_binary(const std::u8string_view bytes) noexcept
        {
            for (const char8_t value : bytes)
                if (value == u8'\0')
                    return true;
            return false;
        }

        diagnostic make_plain_diagnostic(const diagnostic_code code, std::u8string message)
        {
            diagnostic value {};
            value.code = code;
            value.severity = diagnostic_severity::error;
            value.message = std::move(message);
            return value;
        }
    } // namespace

    file_diff_result read_untracked_file_diff(const vcs_file_probe& probe, const std::u8string_view working_directory, const std::u8string_view relative_path)
    {
        file_diff_result result {};
        const std::u8string absolute { join_local_change_path(working_directory, relative_path) };
        const vcs_file_content content { probe.read_prefix(absolute, local_change_diff_display_limit) };

        if (content.kind == vcs_path_kind::directory)
        {
            result.directory = true;
            return result;
        }
        if (content.kind == vcs_path_kind::missing)
        {
            result.diagnostics.push_back(make_plain_diagnostic(diagnostic_code::operation_failed, std::u8string { u8"파일을 읽을 수 없습니다: " } + std::u8string { relative_path }));
            return result;
        }
        if (looks_binary(content.bytes))
        {
            result.binary = true;
            return result;
        }

        result.truncated = content.truncated;
        // 전체 내용을 "추가"로 표시한다. diff와 같은 색 규칙을 그대로 쓴다.
        std::u8string line { u8"+" };
        for (const char8_t value : content.bytes)
        {
            if (value == u8'\n')
            {
                if (line.size() > 1 && line.back() == u8'\r')
                    line.pop_back();
                result.lines.push_back(std::move(line));
                line = u8"+";
                continue;
            }
            line.push_back(value);
        }
        if (line.size() > 1)
            result.lines.push_back(std::move(line));
        return result;
    }

    void append_diff_lines_limited(file_diff_result& result, std::vector<std::u8string> lines)
    {
        std::size_t total { 0 };
        for (std::u8string& line : lines)
        {
            total += line.size() + 1;
            if (total > local_change_diff_display_limit)
            {
                result.truncated = true;
                return;
            }
            result.lines.push_back(std::move(line));
        }
    }
} // namespace gitman

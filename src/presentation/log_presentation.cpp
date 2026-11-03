#include "presentation/log_presentation.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace gitman {
    bool log_entry_matches_filter(const operation_log_entry& entry, const log_stream_filter filter) noexcept
    {
        switch (filter)
        {
        case log_stream_filter::all:
            return true;
        case log_stream_filter::output:
            return entry.kind != log_entry_kind::standard_error;
        case log_stream_filter::errors:
            return entry.kind == log_entry_kind::standard_error || entry.severity != diagnostic_severity::information;
        }
        return true;
    }

    log_stream_filter next_log_filter(const log_stream_filter filter) noexcept
    {
        switch (filter)
        {
        case log_stream_filter::all:
            return log_stream_filter::output;
        case log_stream_filter::output:
            return log_stream_filter::errors;
        case log_stream_filter::errors:
            return log_stream_filter::all;
        }
        return log_stream_filter::all;
    }

    std::u8string_view log_stream_filter_label(const log_stream_filter filter) noexcept
    {
        switch (filter)
        {
        case log_stream_filter::all:
            return u8"전체";
        case log_stream_filter::output:
            return u8"출력만";
        case log_stream_filter::errors:
            return u8"오류만";
        }
        return u8"전체";
    }

    std::u8string format_log_timestamp(const std::chrono::system_clock::time_point time)
    {
        const std::time_t seconds { std::chrono::system_clock::to_time_t(time) };
        std::tm parts {};
        if (localtime_s(&parts, &seconds) != 0)
            return std::u8string { u8"--:--:--" };

        char text[16] {};
        const int written { std::snprintf(text, sizeof(text), "%02d:%02d:%02d", parts.tm_hour, parts.tm_min, parts.tm_sec) };
        if (written != 8)
            return std::u8string { u8"--:--:--" };
        return std::u8string { reinterpret_cast<const char8_t*>(text), 8 };
    }

    std::u8string format_elapsed_time(const std::chrono::steady_clock::duration elapsed)
    {
        std::int64_t total_seconds { std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() };
        if (total_seconds < 0)
            total_seconds = 0;
        const std::int64_t minutes { total_seconds / 60 };
        const std::int64_t seconds { total_seconds % 60 };

        char text[24] {};
        const int written { std::snprintf(text, sizeof(text), "%02lld:%02lld", static_cast<long long>(minutes), static_cast<long long>(seconds)) };
        if (written < 5)
            return std::u8string { u8"00:00" };
        return std::u8string { reinterpret_cast<const char8_t*>(text), static_cast<std::size_t>(written) };
    }

    std::vector<log_display_line> build_log_display_lines(const std::deque<operation_log_record>& records, const log_stream_filter filter)
    {
        std::vector<log_display_line> lines {};
        // 연속된 progress record의 진행 중인 run이다. run이 끝나면 마지막 record만
        // 표시 줄이 되고 나머지는 접힌 수로 남는다 (stage-8-plan 5.3).
        std::size_t progress_run { 0 };
        const operation_log_record* last_progress { nullptr };
        const auto flush_run = [&lines, &progress_run, &last_progress]() {
            if (progress_run == 0)
                return;
            log_display_line line {};
            line.record = *last_progress;
            line.collapsed = progress_run - 1;
            lines.push_back(std::move(line));
            progress_run = 0;
            last_progress = nullptr;
        };

        for (const operation_log_record& record : records)
        {
            if (log_entry_matches_filter(record.entry, filter) == false)
                continue;
            if (record.entry.progress)
            {
                ++progress_run;
                last_progress = &record;
                continue;
            }
            flush_run();
            log_display_line line {};
            line.record = record;
            lines.push_back(std::move(line));
        }
        flush_run();
        return lines;
    }

    std::size_t log_display_line_count(const std::deque<operation_log_record>& records, const log_stream_filter filter) noexcept
    {
        std::size_t count { 0 };
        bool in_progress_run { false };
        for (const operation_log_record& record : records)
        {
            if (log_entry_matches_filter(record.entry, filter) == false)
                continue;
            if (record.entry.progress)
            {
                // run 전체가 표시 줄 하나다. run의 첫 record에서만 센다.
                if (in_progress_run == false)
                {
                    ++count;
                    in_progress_run = true;
                }
                continue;
            }
            in_progress_run = false;
            ++count;
        }
        return count;
    }

    std::u8string format_log_copy_text(const log_view_model& log)
    {
        std::u8string text {};
        for (const operation_log_record& record : log.records)
        {
            text += u8"[";
            text += format_log_timestamp(record.entry.time);
            text += u8"][";
            text += log_entry_kind_name(record.entry.kind);
            text += u8"] ";
            text += record.entry.text;
            text += u8"\r\n";
        }
        return text;
    }

    std::u8string format_log_line_text(const log_display_line& line)
    {
        std::u8string text { line.record.entry.text };
        if (line.collapsed == 0)
            return text;
        text += u8" (진행 표시 ";
        std::u8string digits {};
        std::size_t remaining { line.collapsed };
        do
        {
            digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
            remaining /= 10;
        } while (remaining > 0);
        text += digits;
        text += u8"줄 접힘)";
        return text;
    }

    std::u8string log_line_access_name(const log_display_line& line)
    {
        std::u8string name { format_log_timestamp(line.record.entry.time) };
        name += u8" ";
        if (line.record.entry.severity == diagnostic_severity::error)
            name += u8"오류: ";
        else if (line.record.entry.severity == diagnostic_severity::warning)
            name += u8"경고: ";
        name += format_log_line_text(line);
        return name;
    }

    std::u8string_view log_empty_hint(const log_view_model& log) noexcept
    {
        if (log.card.value.empty())
            return u8"카드를 선택하면 그 카드에서 실행한 작업의 로그가 여기에 표시됩니다.";
        if (log.filter == log_stream_filter::all)
            return u8"이 카드에서 실행한 작업의 로그가 여기에 표시됩니다.";
        return u8"필터와 일치하는 로그가 없습니다.";
    }

    std::u8string log_list_access_name(const log_view_model& log)
    {
        std::u8string name { u8"로그" };
        // 선택이 없을 때의 제목은 `로그` 그대로라 되풀이하지 않는다.
        if (log.card.value.empty() == false)
        {
            name += u8": ";
            name += log.title;
            if (log.truncated)
                name += u8" (오래된 로그 제거됨)";
        }
        name += u8", ";
        if (log.records.empty())
        {
            name += log_empty_hint(log);
            return name;
        }
        const std::string digits { std::to_string(log.lines.size()) };
        name.append(digits.begin(), digits.end());
        name += u8"줄";
        return name;
    }
} // namespace gitman

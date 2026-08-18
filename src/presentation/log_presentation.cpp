#include "presentation/log_presentation.h"

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
} // namespace gitman

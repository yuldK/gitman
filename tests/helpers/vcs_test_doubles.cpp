#include "helpers/vcs_test_doubles.h"

#include <chrono>
#include <utility>

namespace gitman::testing {
    namespace {
        char8_t ascii_lowercase(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        char8_t normalize_path_character(const char8_t value) noexcept
        {
            return value == u8'/' ? u8'\\' : ascii_lowercase(value);
        }

        bool path_equal(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
                if (normalize_path_character(left[index]) != normalize_path_character(right[index]))
                    return false;
            return true;
        }

        bool ends_with_ignoring_case(const std::u8string_view value, const std::u8string_view suffix) noexcept
        {
            if (value.size() < suffix.size())
                return false;
            return path_equal(value.substr(value.size() - suffix.size()), suffix);
        }

        // 단계 3의 파이프라인과 같은 줄 분할 규칙을 흉내 낸다. 마지막 줄에 줄 끝이
        // 없어도 레코드 하나로 flush한다.
        void emit_lines(process_output_sink* const sink, const process_stream stream, const std::u8string_view text, std::uint64_t& sequence, std::uint64_t& record_count)
        {
            if (text.empty())
                return;

            std::size_t begin { 0 };
            while (begin <= text.size())
            {
                const std::size_t end { text.find(u8'\n', begin) };
                const bool final_segment { end == std::u8string_view::npos };
                std::u8string_view line { final_segment ? text.substr(begin) : text.substr(begin, end - begin) };
                if (line.empty() == false && line.back() == u8'\r')
                    line.remove_suffix(1);

                if (final_segment && line.empty())
                    return;

                process_output_record record {};
                record.sequence = sequence++;
                record.stream = stream;
                record.text = line;
                ++record_count;
                if (sink != nullptr)
                    sink->on_record(record);

                if (final_segment)
                    return;
                begin = end + 1;
            }
        }
    } // namespace

    void fake_process_runner::push_response(response value)
    {
        responses_.push_back(std::move(value));
    }

    void fake_process_runner::set_default_response(response value)
    {
        default_response_ = std::move(value);
    }

    const std::vector<process_request>& fake_process_runner::requests() const noexcept
    {
        return requests_;
    }

    std::size_t fake_process_runner::request_count() const noexcept
    {
        return requests_.size();
    }

    const process_request& fake_process_runner::request(const std::size_t index) const
    {
        return requests_.at(index);
    }

    std::size_t fake_process_runner::requests_for_executable_suffix(const std::u8string_view suffix) const
    {
        std::size_t count { 0 };
        for (const process_request& value : requests_)
            if (ends_with_ignoring_case(value.executable, suffix))
                ++count;
        return count;
    }

    process_result fake_process_runner::run(const process_request& request, process_output_sink* const sink, const process_cancellation_token& token) noexcept
    {
        try
        {
            requests_.push_back(request);

            const response& scripted { next_response_ < responses_.size() ? responses_[next_response_++] : default_response_ };

            process_result result {};
            result.completion = scripted.completion;
            result.masked_command_line = request.executable;
            if (token.cancelled())
                result.completion = process_completion::cancelled;
            if (result.completion == process_completion::exited)
                result.exit_code = scripted.exit_code;

            std::uint64_t sequence { 0 };
            std::uint64_t record_count { 0 };
            emit_lines(sink, process_stream::standard_output, scripted.standard_output, sequence, record_count);
            emit_lines(sink, process_stream::standard_error, scripted.standard_error, sequence, record_count);
            result.record_count = record_count;
            result.duration = std::chrono::milliseconds { 0 };
            return result;
        }
        catch (...)
        {
            process_result result {};
            result.completion = process_completion::internal_error;
            return result;
        }
    }

    void fake_vcs_file_probe::add_file(const std::u8string_view path)
    {
        entries_.push_back({ std::u8string { path }, vcs_path_kind::file, {} });
    }

    void fake_vcs_file_probe::add_file(const std::u8string_view path, const std::u8string_view content)
    {
        entries_.push_back({ std::u8string { path }, vcs_path_kind::file, std::u8string { content } });
    }

    void fake_vcs_file_probe::add_directory(const std::u8string_view path)
    {
        entries_.push_back({ std::u8string { path }, vcs_path_kind::directory, {} });
    }

    vcs_path_kind fake_vcs_file_probe::probe(const std::u8string_view absolute_path) const noexcept
    {
        for (const entry& value : entries_)
            if (path_equal(value.path, absolute_path))
                return value.kind;
        return vcs_path_kind::missing;
    }

    vcs_file_content fake_vcs_file_probe::read_prefix(const std::u8string_view absolute_path, const std::size_t maximum_bytes) const noexcept
    {
        vcs_file_content content {};
        for (const entry& value : entries_)
        {
            if (path_equal(value.path, absolute_path) == false)
                continue;
            content.kind = value.kind;
            if (value.kind != vcs_path_kind::file)
                return content;
            content.bytes = value.content;
            if (content.bytes.size() > maximum_bytes)
            {
                content.bytes.resize(maximum_bytes);
                content.truncated = true;
            }
            return content;
        }
        return content;
    }
} // namespace gitman::testing

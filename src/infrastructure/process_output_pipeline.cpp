#include "infrastructure/process_output_pipeline.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace gitman {
    namespace {
        constexpr std::u8string_view utf8_replacement_character { u8"�" };

        constexpr bool is_continuation_byte(const char8_t value) noexcept
        {
            return (value & 0xC0u) == 0x80u;
        }

        // lead byte가 요구하는 전체 sequence 길이다. 잘못된 lead byte는 0이다.
        constexpr std::size_t utf8_expected_length(const char8_t lead) noexcept
        {
            if (lead < 0x80u)
                return 1;
            if (lead >= 0xC2u && lead <= 0xDFu)
                return 2;
            if (lead >= 0xE0u && lead <= 0xEFu)
                return 3;
            if (lead >= 0xF0u && lead <= 0xF4u)
                return 4;
            return 0;
        }

        // 유효한 UTF-8 sequence면 길이를, 아니면 0을 돌려준다. overlong, surrogate와
        // U+10FFFF 초과 표현도 거부한다.
        std::size_t utf8_sequence_length(const std::u8string_view input, const std::size_t index) noexcept
        {
            const char8_t lead { input[index] };
            const std::size_t length { utf8_expected_length(lead) };
            if (length == 0)
                return 0;
            if (length == 1)
                return 1;
            if (index + length > input.size())
                return 0;

            char8_t first_minimum { 0x80u };
            char8_t first_maximum { 0xBFu };
            if (lead == 0xE0u)
                first_minimum = 0xA0u;
            else if (lead == 0xEDu)
                first_maximum = 0x9Fu;
            else if (lead == 0xF0u)
                first_minimum = 0x90u;
            else if (lead == 0xF4u)
                first_maximum = 0x8Fu;

            for (std::size_t offset = 1; offset < length; ++offset)
            {
                const char8_t value { input[index + offset] };
                const char8_t minimum { offset == 1 ? first_minimum : char8_t { 0x80u } };
                const char8_t maximum { offset == 1 ? first_maximum : char8_t { 0xBFu } };
                if (value < minimum || value > maximum)
                    return 0;
            }
            return length;
        }

        // 강제 분할 위치를 정한다. 마지막 sequence의 남은 byte가 아직 도착하지 않았으면
        // 그 sequence 시작에서 끊어 다음 레코드로 넘긴다.
        std::size_t utf8_split_position(const std::u8string_view text) noexcept
        {
            if (text.empty())
                return 0;

            std::size_t start { text.size() - 1 };
            for (std::size_t step = 0; step < 3 && start > 0 && is_continuation_byte(text[start]); ++step)
                --start;

            const std::size_t expected { utf8_expected_length(text[start]) };
            if (expected != 0 && start > 0 && start + expected > text.size())
                return start;
            return text.size();
        }
    } // namespace

    std::u8string normalize_utf8_text(const std::u8string_view input, bool& replaced_invalid_bytes)
    {
        std::u8string output {};
        output.reserve(input.size());

        std::size_t index { 0 };
        while (index < input.size())
        {
            const std::size_t length { utf8_sequence_length(input, index) };
            if (length == 0)
            {
                output.append(utf8_replacement_character);
                replaced_invalid_bytes = true;
                ++index;
                continue;
            }
            output.append(input.substr(index, length));
            index += length;
        }
        return output;
    }

    bool is_valid_utf8_text(const std::u8string_view input) noexcept
    {
        std::size_t index { 0 };
        while (index < input.size())
        {
            const std::size_t length { utf8_sequence_length(input, index) };
            if (length == 0)
                return false;
            index += length;
        }
        return true;
    }

    process_output_pipeline::process_output_pipeline(
        const process_stream stream, const std::size_t maximum_record_bytes, const std::size_t maximum_captured_bytes, const process_text_encoding encoding, text_transcoder* const transcoder)
        : stream_ { stream }
        , encoding_ { encoding }
        , transcoder_ { transcoder }
        , maximum_record_bytes_ { maximum_record_bytes }
        , maximum_captured_bytes_ { maximum_captured_bytes }
    {}

    void process_output_pipeline::append(const std::u8string_view bytes, const record_handler& handler)
    {
        if (truncated_ || bytes.empty())
            return;

        const std::size_t remaining { maximum_captured_bytes_ - captured_bytes_ };
        const std::u8string_view usable { bytes.substr(0, std::min(remaining, bytes.size())) };
        captured_bytes_ += usable.size();
        consume(usable, handler);

        if (usable.size() < bytes.size())
        {
            // 상한을 넘긴 이후 byte는 버리지만 pipe는 runner가 계속 비운다.
            truncated_ = true;
            flush(handler);
        }
    }

    void process_output_pipeline::flush(const record_handler& handler)
    {
        const bool progress { pending_carriage_return_ };
        pending_carriage_return_ = false;
        if (pending_.empty() == false)
            emit(progress, false, handler);
    }

    std::size_t process_output_pipeline::captured_bytes() const noexcept
    {
        return captured_bytes_;
    }

    bool process_output_pipeline::truncated() const noexcept
    {
        return truncated_;
    }

    void process_output_pipeline::fill_text(process_output_record& record)
    {
        if (encoding_ == process_text_encoding::active_code_page_fallback && is_valid_utf8_text(pending_) == false && transcoder_ != nullptr)
        {
            // UTF-8로 해석되지 않는 레코드만 활성 code page로 다시 해석한다. 이미 유효한
            // UTF-8은 건드리지 않아 정상 출력이 code page 해석으로 훼손되지 않는다.
            if (std::optional<std::u8string> transcoded { transcoder_->to_utf8(pending_) }; transcoded.has_value())
            {
                record.text = std::move(*transcoded);
                record.transcoded_from_active_code_page = true;
                return;
            }
        }
        record.text = normalize_utf8_text(pending_, record.replaced_invalid_bytes);
    }

    void process_output_pipeline::emit(const bool progress, const bool continued, const record_handler& handler)
    {
        // 내용 없는 진행 표시는 정보가 없으므로 레코드를 만들지 않는다. 빈 줄은 보존한다.
        if (progress && pending_.empty())
            return;

        process_output_record record {};
        record.stream = stream_;
        record.progress = progress;
        record.continued = continued;
        fill_text(record);
        pending_.clear();

        if (handler)
            handler(record);
    }

    void process_output_pipeline::consume(const std::u8string_view bytes, const record_handler& handler)
    {
        for (const char8_t value : bytes)
        {
            if (pending_carriage_return_)
            {
                pending_carriage_return_ = false;
                if (value == u8'\n')
                {
                    // `\r\n`은 한 줄의 끝이다.
                    emit(false, false, handler);
                    continue;
                }
                // 단독 `\r`은 Git 진행 표시처럼 같은 줄을 다시 쓰는 출력이다.
                emit(true, false, handler);
            }

            if (value == u8'\n')
            {
                emit(false, false, handler);
                continue;
            }
            if (value == u8'\r')
            {
                pending_carriage_return_ = true;
                continue;
            }

            pending_.push_back(value);
            if (pending_.size() < maximum_record_bytes_)
                continue;

            const std::size_t split { utf8_split_position(pending_) };
            std::u8string remainder { pending_.substr(split) };
            pending_.resize(split);
            emit(false, true, handler);
            pending_ = std::move(remainder);
        }
    }
} // namespace gitman

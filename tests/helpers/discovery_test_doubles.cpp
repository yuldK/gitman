#include "helpers/discovery_test_doubles.h"

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
    } // namespace

    void fake_directory_enumerator::set_listing(const std::u8string_view absolute_directory, directory_listing listing)
    {
        for (entry& value : entries_)
        {
            if (path_equal(value.path, absolute_directory))
            {
                value.listing = std::move(listing);
                return;
            }
        }
        entries_.push_back({ std::u8string { absolute_directory }, std::move(listing) });
    }

    std::size_t fake_directory_enumerator::enumeration_count() const noexcept
    {
        return enumeration_count_;
    }

    directory_listing fake_directory_enumerator::enumerate(const std::u8string_view absolute_directory) const noexcept
    {
        try
        {
            ++enumeration_count_;
            for (const entry& value : entries_)
                if (path_equal(value.path, absolute_directory))
                    return value.listing;

            // 등록하지 않은 경로는 Windows의 경로 없음 오류(ERROR_PATH_NOT_FOUND)로
            // 실패한다.
            directory_listing missing {};
            missing.native_error = { 3u };
            return missing;
        }
        catch (...)
        {
            return {};
        }
    }
} // namespace gitman::testing

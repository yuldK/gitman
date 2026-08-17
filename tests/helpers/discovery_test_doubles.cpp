#include "helpers/discovery_test_doubles.h"

#include <chrono>
#include <fstream>
#include <system_error>
#include <utility>

namespace gitman::testing {
    scoped_scan_directory::scoped_scan_directory()
    {
        std::error_code error {};
        const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
        if (static_cast<bool>(error))
            return;

        const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
        for (std::size_t attempt = 0; attempt < 100; ++attempt)
        {
            error.clear();
            const std::filesystem::path candidate { base / (L"gitman-discovery-tests-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
            if (std::filesystem::create_directory(candidate, error))
            {
                root_ = candidate;
                break;
            }
        }
    }

    scoped_scan_directory::~scoped_scan_directory()
    {
        if (root_.empty())
            return;

        std::error_code error {};
        std::filesystem::remove_all(root_, error);
    }

    bool scoped_scan_directory::available() const noexcept
    {
        return root_.empty() == false;
    }

    std::u8string scoped_scan_directory::root() const
    {
        return root_.u8string();
    }

    std::u8string scoped_scan_directory::path_of(const std::u8string_view relative) const
    {
        return (root_ / std::filesystem::path { std::u8string { relative } }).u8string();
    }

    std::u8string scoped_scan_directory::make_directory(const std::u8string_view relative) const
    {
        const std::filesystem::path path { root_ / std::filesystem::path { std::u8string { relative } } };
        std::error_code error {};
        std::filesystem::create_directories(path, error);
        return path.u8string();
    }

    std::u8string scoped_scan_directory::make_file(const std::u8string_view relative) const
    {
        const std::filesystem::path path { root_ / std::filesystem::path { std::u8string { relative } } };
        std::ofstream stream { path, std::ios::binary };
        stream << "gitman discovery test";
        return path.u8string();
    }

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

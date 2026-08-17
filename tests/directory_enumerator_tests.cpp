#include "helpers/discovery_test_doubles.h"
#include "platform/win32/win32_directory_enumerator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {
    using scoped_test_directory = gitman::testing::scoped_scan_directory;

    const gitman::directory_entry* find_entry(const gitman::directory_listing& listing, const std::u8string_view name)
    {
        for (const gitman::directory_entry& entry : listing.entries)
            if (entry.name == name)
                return &entry;
        return nullptr;
    }
} // namespace

TEST_CASE("Win32 enumeration lists depth one entries with attributes", "[discovery][enumerator]")
{
    const scoped_test_directory directory {};
    REQUIRE(directory.available());
    (void)directory.make_directory(u8"repo");
    (void)directory.make_directory(u8"nested\\child");
    (void)directory.make_file(u8"note.txt");

    const gitman::directory_listing listing { gitman::win32::enumerate_directory(directory.root()) };
    REQUIRE(listing.succeeded);
    REQUIRE_FALSE(listing.native_error.has_value());
    REQUIRE(listing.unreadable_name_count == 0u);

    // 깊이 1 항목만 나온다. `nested` 아래의 `child`는 목록에 없어야 한다.
    REQUIRE(listing.entries.size() == 3u);
    REQUIRE(find_entry(listing, u8"child") == nullptr);
    REQUIRE(find_entry(listing, u8".") == nullptr);
    REQUIRE(find_entry(listing, u8"..") == nullptr);

    const gitman::directory_entry* const repo { find_entry(listing, u8"repo") };
    REQUIRE(repo != nullptr);
    REQUIRE(repo->is_directory);
    REQUIRE_FALSE(repo->is_reparse_point);

    const gitman::directory_entry* const note { find_entry(listing, u8"note.txt") };
    REQUIRE(note != nullptr);
    REQUIRE_FALSE(note->is_directory);
    REQUIRE_FALSE(note->is_reparse_point);
}

TEST_CASE("Win32 enumeration keeps non ASCII names intact", "[discovery][enumerator]")
{
    const scoped_test_directory directory {};
    REQUIRE(directory.available());
    (void)directory.make_directory(u8"한글 저장소");
    (void)directory.make_directory(u8"emoji 📁 dir");
    (void)directory.make_file(u8"공백 파일.txt");

    const gitman::directory_listing listing { gitman::win32::enumerate_directory(directory.root()) };
    REQUIRE(listing.succeeded);
    REQUIRE(listing.entries.size() == 3u);
    REQUIRE(listing.unreadable_name_count == 0u);

    const gitman::directory_entry* const korean { find_entry(listing, u8"한글 저장소") };
    REQUIRE(korean != nullptr);
    REQUIRE(korean->is_directory);

    const gitman::directory_entry* const emoji { find_entry(listing, u8"emoji 📁 dir") };
    REQUIRE(emoji != nullptr);
    REQUIRE(emoji->is_directory);

    const gitman::directory_entry* const spaced { find_entry(listing, u8"공백 파일.txt") };
    REQUIRE(spaced != nullptr);
    REQUIRE_FALSE(spaced->is_directory);
}

TEST_CASE("Win32 enumeration reports an empty directory as success", "[discovery][enumerator]")
{
    const scoped_test_directory directory {};
    REQUIRE(directory.available());
    const std::u8string empty { directory.make_directory(u8"empty") };

    const gitman::directory_listing listing { gitman::win32::enumerate_directory(empty) };
    REQUIRE(listing.succeeded);
    REQUIRE(listing.entries.empty());
    REQUIRE_FALSE(listing.native_error.has_value());
}

TEST_CASE("Win32 enumeration fails with the native error for a missing path", "[discovery][enumerator]")
{
    const scoped_test_directory directory {};
    REQUIRE(directory.available());
    const std::u8string missing { std::u8string { directory.root() } + u8"\\does-not-exist" };

    const gitman::directory_listing listing { gitman::win32::enumerate_directory(missing) };
    REQUIRE_FALSE(listing.succeeded);
    REQUIRE(listing.entries.empty());
    REQUIRE(listing.native_error.has_value());
    // ERROR_PATH_NOT_FOUND다. 사라진 스캔 루트를 다른 실패와 구분할 수 있어야 한다.
    REQUIRE(*listing.native_error == 3u);
}

TEST_CASE("Win32 enumeration fails when the root is a file", "[discovery][enumerator]")
{
    const scoped_test_directory directory {};
    REQUIRE(directory.available());
    const std::u8string file { directory.make_file(u8"plain.txt") };

    const gitman::directory_listing listing { gitman::win32::enumerate_directory(file) };
    REQUIRE_FALSE(listing.succeeded);
    REQUIRE(listing.entries.empty());
    REQUIRE(listing.native_error.has_value());
}

TEST_CASE("Win32 enumeration rejects a relative path without touching the OS", "[discovery][enumerator]")
{
    const gitman::directory_listing listing { gitman::win32::enumerate_directory(u8"relative\\path") };
    REQUIRE_FALSE(listing.succeeded);
    REQUIRE(listing.entries.empty());
    // Win32 오류 번호가 없다. 호출 형식 위반은 OS 실패와 구분한다.
    REQUIRE_FALSE(listing.native_error.has_value());
}

TEST_CASE("The directory enumerator contract object forwards to the Win32 implementation", "[discovery][enumerator]")
{
    const scoped_test_directory directory {};
    REQUIRE(directory.available());
    (void)directory.make_directory(u8"repo");

    const auto enumerator { gitman::win32::make_directory_enumerator() };
    REQUIRE(enumerator != nullptr);

    const gitman::directory_listing listing { enumerator->enumerate(directory.root()) };
    REQUIRE(listing.succeeded);
    REQUIRE(listing.entries.size() == 1u);
    REQUIRE(listing.entries.front().name == u8"repo");
}

TEST_CASE("The fake directory enumerator replays registered listings deterministically", "[discovery][enumerator]")
{
    gitman::testing::fake_directory_enumerator enumerator {};

    gitman::directory_listing scripted {};
    scripted.succeeded = true;
    scripted.entries.push_back({ u8"repo", true, false });
    scripted.entries.push_back({ u8"link", true, true });
    enumerator.set_listing(u8"C:\\scan", std::move(scripted));

    // 구분자와 ASCII 대소문자가 달라도 같은 경로로 본다. Windows 경로 비교와 같다.
    const gitman::directory_listing replayed { enumerator.enumerate(u8"c:/SCAN") };
    REQUIRE(replayed.succeeded);
    REQUIRE(replayed.entries.size() == 2u);
    REQUIRE(replayed.entries[1].is_reparse_point);

    // 등록하지 않은 경로는 경로 없음 오류로 실패해 실패 경로 test를 결정적으로 만든다.
    const gitman::directory_listing missing { enumerator.enumerate(u8"C:\\other") };
    REQUIRE_FALSE(missing.succeeded);
    REQUIRE(missing.native_error.has_value());
    REQUIRE(*missing.native_error == 3u);

    REQUIRE(enumerator.enumeration_count() == 2u);
}

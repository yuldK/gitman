#include "platform/win32/workspace_document_file_system.h"

#include "helpers/discovery_test_doubles.h"
#include "helpers/document_file_watch.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

// 저장을 기다리는 driver test가 문서를 어떻게 지켜봐야 하는지다 (function-buttons-design "구현 결과" S5). 부하 중
// `[driver]` test가 `REQUIRE(persisted)`에서 간헐로 실패한 원인은, 저장을 기다리며 문서를 되풀이해 읽던 test의 handle이
// store의 rename 교체와 겹친 것이었다 (알림 `작업공간 문서 원본을 교체하지 못했습니다.`).
namespace {
    void write_text(const std::u8string& path, const std::string& content)
    {
        std::ofstream stream { std::filesystem::path { path }, std::ios::binary };
        stream << content;
    }

    [[nodiscard]] std::string read_text(const std::u8string& path)
    {
        std::ifstream stream { std::filesystem::path { path }, std::ios::binary };
        return std::string { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    }
}

TEST_CASE("A reader holding the document makes the atomic replace fail", "[function_buttons][s5]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string document_path { directory.path_of(u8"held.version-list") };
    write_text(document_path, "old");
    gitman::win32::workspace_document_file_system file_system {};

    {
        // `std::ifstream`은 삭제 공유 없이 열지만, 삭제 공유를 줘도 열린 handle이 있으면 교체는 거부된다(실측).
        std::ifstream reader { std::filesystem::path { document_path }, std::ios::binary };
        REQUIRE(reader.is_open());
        const gitman::workspace_file_commit_result held { file_system.atomic_commit(document_path, u8"new", true) };
        // 이 기계(Windows 11 빌드 26300)의 코드는 `ERROR_ACCESS_DENIED`였다. 코드는 OS 빌드가 정하므로 실패한 단계만 단언한다.
        INFO("native_error " << held.native_error.value_or(0u));
        REQUIRE(held.failure == gitman::workspace_file_commit_failure::replace);
    }

    REQUIRE(file_system.atomic_commit(document_path, u8"new", true).succeeded());
    REQUIRE(read_text(document_path) == "new");
}

TEST_CASE("Stamping the document never blocks the atomic replace", "[function_buttons][s5]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string document_path { directory.path_of(u8"watched.version-list") };
    write_text(document_path, "0");
    gitman::win32::workspace_document_file_system file_system {};

    // 쉬지 않고 문서를 지켜보는 thread 옆에서 교체를 되풀이한다. 지켜보기가 문서를 한 번이라도 열면 몇 번은 겹쳐 실패한다.
    std::atomic<bool> stop { false };
    std::atomic<std::size_t> stamps { 0 };
    std::thread watcher { [&]
        {
            while (stop.load() == false)
            {
                static_cast<void>(gitman::testing::stamp_document_file(document_path));
                stamps.fetch_add(1);
            }
        }
    };

    std::size_t failures { 0 };
    std::string content {};
    for (std::size_t index = 0; index < 300; ++index)
    {
        content.push_back('x');
        const std::u8string bytes { content.begin(), content.end() };
        if (file_system.atomic_commit(document_path, bytes, true).succeeded() == false)
            ++failures;
    }
    stop.store(true);
    watcher.join();

    REQUIRE(stamps.load() > 0);
    REQUIRE(failures == 0);
    REQUIRE(read_text(document_path) == content);
}

TEST_CASE("Waiting for the replacement sees a save made after the stamp", "[function_buttons][s5]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string document_path { directory.path_of(u8"saved.version-list") };
    write_text(document_path, "before");
    gitman::win32::workspace_document_file_system file_system {};

    const gitman::testing::document_file_stamp before { gitman::testing::stamp_document_file(document_path) };
    REQUIRE(before.exists);
    REQUIRE(before.size == 6u);
    // 저장하지 않으면 기다림은 시간을 다 쓰고 실패한다.
    REQUIRE(gitman::testing::wait_for_document_replacement(document_path, before, std::chrono::milliseconds { 100 }) == false);

    // 크기가 같은 교체도 본다. 카드 순서만 바뀐 저장이 그렇다.
    bool committed { false };
    std::thread saver { [&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds { 50 });
            committed = file_system.atomic_commit(document_path, u8"after!", true).succeeded();
        }
    };
    const bool replaced { gitman::testing::wait_for_document_replacement(document_path, before, std::chrono::milliseconds { 15000 }) };
    saver.join();

    REQUIRE(committed);
    REQUIRE(replaced);
    REQUIRE(read_text(document_path) == "after!");
}

TEST_CASE("Waiting for content skips a replacement that does not carry it yet", "[function_buttons][s5]")
{
    // 기다리던 저장보다 다른 저장이 먼저 교체해도 놓치지 않는다. 처음 본 교체만 보던 도우미는 여기서 실패한다(S5 검토).
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string document_path { directory.path_of(u8"twice.version-list") };
    write_text(document_path, "first");
    gitman::win32::workspace_document_file_system file_system {};

    const gitman::testing::document_file_stamp before { gitman::testing::stamp_document_file(document_path) };
    std::atomic<std::size_t> checks { 0 };
    bool committed { false };
    std::thread saver { [&]
        {
            committed = file_system.atomic_commit(document_path, u8"another", true).succeeded();
            // 기다리는 쪽이 첫 교체를 읽고 물리친 뒤에야 기다리던 저장을 한다.
            const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 15 } };
            while (checks.load() == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
            committed = committed && file_system.atomic_commit(document_path, u8"wanted", true).succeeded();
        }
    };
    const auto wanted = [&checks](const std::string& content)
    {
        checks.fetch_add(1);
        return content == "wanted";
    };
    const std::optional<std::string> content { gitman::testing::wait_for_document_content(document_path, before, std::chrono::milliseconds { 15000 }, wanted) };
    saver.join();

    REQUIRE(committed);
    REQUIRE(content == std::optional<std::string> { "wanted" });
    REQUIRE(checks.load() == 2u);
}

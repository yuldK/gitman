#include "platform/win32/win32_app_runtime.h"

#include "helpers/discovery_test_doubles.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace {
    // 창 없이 조립체 전체(스레드 4종, store, executor, provider)를 실제로 돌린다.
    // acquire_view 호출자가 UI thread 역할이다.
    std::shared_ptr<const gitman::view_snapshot> wait_for_view(
        gitman::win32::app_runtime& runtime, const std::chrono::milliseconds limit, const std::function<bool(const gitman::view_snapshot&)>& ready)
    {
        const auto deadline { std::chrono::steady_clock::now() + limit };
        while (std::chrono::steady_clock::now() < deadline)
        {
            const std::shared_ptr<const gitman::view_snapshot> view { runtime.acquire_view() };
            if (view != nullptr && ready(*view))
                return view;
            std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
        }
        return nullptr;
    }

    bool initial_query_settled(const gitman::view_snapshot& value)
    {
        return value.cards.size() == 1u && value.cards.front().busy == false && value.cards.front().state != gitman::card_view_state::loading;
    }

    bool any_view(const gitman::view_snapshot&)
    {
        return true;
    }
} // namespace

TEST_CASE("The assembled runtime loads a real document and finishes the initial query", "[runtime][integration]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string project_path { directory.make_directory(u8"plain-project") };
    const std::u8string document_path { directory.path_of(u8"workspace.verison-list") };
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary };
        std::string json { "{\"schema_version\":1,\"projects\":[{\"id\":\"plain\",\"path\":\"" };
        std::string path_utf8 { project_path.begin(), project_path.end() };
        for (char& value : path_utf8)
            if (value == '\\')
                value = '/';
        json += path_utf8;
        json += "\"}]}";
        stream << json;
    }

    gitman::win32::app_runtime runtime { nullptr, 0, 0 };
    runtime.post_logic(gitman::logic_message { gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f } });
    runtime.post_logic(gitman::logic_message { gitman::open_document_intent { document_path } });

    // 문서가 열려 카드가 생기고, 초기 로컬 조회가 끝나 loading/running이 풀릴 때까지
    // 기다린다. 등록 경로는 저장소가 아니므로 실제 provider가 not_a_repository로
    // 판정해야 한다.
    const auto view { wait_for_view(runtime, std::chrono::milliseconds { 15000 }, &initial_query_settled) };
    REQUIRE(view != nullptr);
    REQUIRE(view->document_path == document_path);
    REQUIRE(view->cards.front().state == gitman::card_view_state::failed);
    REQUIRE(view->cards.front().id.value == u8"plain");

    // 종료가 스레드를 모두 join하고 멱등으로 끝난다. test가 끝나는 것 자체가 검증이다.
    runtime.shutdown();
    runtime.shutdown();
}

TEST_CASE("The runtime publishes an initial empty snapshot and shuts down cleanly without work", "[runtime][integration]")
{
    gitman::win32::app_runtime runtime { nullptr, 0, 0 };
    const auto view { wait_for_view(runtime, std::chrono::milliseconds { 5000 }, &any_view) };
    REQUIRE(view != nullptr);
    REQUIRE(view->empty_state == gitman::view_empty_state::no_document);
    runtime.shutdown();
}

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
    const std::u8string document_path { directory.path_of(u8"workspace.version-list") };
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

TEST_CASE("A reorder intent persists the new order into the document file", "[runtime][integration]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string first_path { directory.make_directory(u8"one-project") };
    const std::u8string second_path { directory.make_directory(u8"two-project") };
    const std::u8string document_path { directory.path_of(u8"reorder.version-list") };
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary };
        std::string json { "{\"schema_version\":1,\"projects\":[" };
        const auto append_project { [&json](const std::string& id, const std::u8string& path) {
            std::string path_utf8 { path.begin(), path.end() };
            for (char& value : path_utf8)
                if (value == '\\')
                    value = '/';
            json += "{\"id\":\"" + id + "\",\"path\":\"" + path_utf8 + "\"}";
        }
        };
        append_project("alpha", first_path);
        json += ',';
        append_project("beta", second_path);
        json += "]}";
        stream << json;
    }

    gitman::win32::app_runtime runtime { nullptr, 0, 0 };
    runtime.post_logic(gitman::logic_message { gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f } });
    runtime.post_logic(gitman::logic_message { gitman::open_document_intent { document_path } });

    const auto both_cards { [](const gitman::view_snapshot& value) { return value.cards.size() == 2u; } };
    REQUIRE(wait_for_view(runtime, std::chrono::milliseconds { 15000 }, both_cards) != nullptr);

    // alpha를 beta 뒤로 옮긴다. 화면 순서가 바뀌고 문서 정렬이 custom이 된다.
    runtime.post_logic(gitman::logic_message { gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"beta" }, true } });
    const auto reordered { [](const gitman::view_snapshot& value) {
        return value.sort == gitman::card_sort_key::custom && value.cards.size() == 2u && value.cards.front().id.value == u8"beta" && value.cards.back().id.value == u8"alpha";
    }
    };
    REQUIRE(wait_for_view(runtime, std::chrono::milliseconds { 15000 }, reordered) != nullptr);

    // 실제 store가 문서를 새 순서로 다시 쓸 때까지 파일을 읽는다.
    bool persisted { false };
    const auto deadline { std::chrono::steady_clock::now() + std::chrono::milliseconds { 15000 } };
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::ifstream stream { std::filesystem::path { document_path }, std::ios::binary };
        const std::string content { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
        const std::size_t beta_position { content.find("\"beta\"") };
        const std::size_t alpha_position { content.find("\"alpha\"") };
        if (beta_position != std::string::npos && alpha_position != std::string::npos && beta_position < alpha_position)
        {
            persisted = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
    }
    REQUIRE(persisted);

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

namespace {
    bool card_seven_selected(const gitman::view_snapshot& value)
    {
        return value.selected.has_value() && value.selected->value == u8"card-7";
    }

    bool storm_settled(const gitman::view_snapshot& value)
    {
        if (value.cards.size() != 120u)
            return false;
        for (const gitman::card_view_model& card : value.cards)
            if (card.busy || card.state == gitman::card_view_state::loading)
                return false;
        return true;
    }
} // namespace

TEST_CASE("A hundred plus cards keep the snapshot flow alive under real load", "[runtime][integration][stress]")
{
    // ADR-004의 검증 항목이다. 실제 provider가 카드 120개를 병렬 조회하는 동안
    // UI(acquire)와 input(post) 역할이 계속 동작해야 한다. 등록 경로가 저장소가
    // 아니므로 카드마다 실제 판정이 일어난다.
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());

    std::string projects {};
    for (int index = 0; index < 120; ++index)
    {
        const std::string digits { std::to_string(index) };
        const std::u8string path { directory.make_directory(std::u8string { u8"p" } + std::u8string { digits.begin(), digits.end() }) };
        std::string path_utf8 { path.begin(), path.end() };
        for (char& value : path_utf8)
            if (value == '\\')
                value = '/';
        if (projects.empty() == false)
            projects += ',';
        projects += "{\"id\":\"card-" + digits + "\",\"path\":\"" + path_utf8 + "\"}";
    }
    const std::u8string document_path { directory.path_of(u8"storm.version-list") };
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary };
        stream << "{\"schema_version\":1,\"projects\":[" << projects << "]}";
    }

    gitman::win32::app_runtime runtime { nullptr, 0, 0 };
    runtime.post_logic(gitman::logic_message { gitman::window_metrics_intent { 1200.0f, 800.0f, 1.0f } });
    runtime.post_logic(gitman::logic_message { gitman::open_document_intent { document_path } });

    // 폭풍이 진행되는 동안 input 역할로 선택과 스크롤 intent를 계속 흘려 보낸다.
    // 게시가 블로킹되거나 snapshot 흐름이 멎으면 아래 대기가 실패한다.
    std::uint64_t interactions { 0 };
    const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 90 } };
    std::shared_ptr<const gitman::view_snapshot> settled {};
    while (std::chrono::steady_clock::now() < deadline)
    {
        runtime.post_logic(gitman::logic_message { gitman::select_card_intent { gitman::project_id { u8"card-7" } } });
        runtime.post_logic(gitman::logic_message { gitman::scroll_intent { 24.0f } });
        ++interactions;

        const std::shared_ptr<const gitman::view_snapshot> view { runtime.acquire_view() };
        if (view != nullptr && storm_settled(*view))
        {
            settled = view;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds { 25 });
    }

    REQUIRE(settled != nullptr);
    REQUIRE(interactions > 0u);
    REQUIRE(settled->cards.size() == 120u);

    // 폭풍 중에 보낸 선택이 상태에 반영되어 있어야 한다. intent 경로가 살아 있었다는
    // 직접 증거다.
    runtime.post_logic(gitman::logic_message { gitman::select_card_intent { gitman::project_id { u8"card-7" } } });
    const auto selected_view { wait_for_view(runtime, std::chrono::milliseconds { 5000 }, &card_seven_selected) };
    REQUIRE(selected_view != nullptr);

    runtime.shutdown();
}

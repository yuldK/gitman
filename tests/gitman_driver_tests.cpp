#include "platform/win32/gitman_driver.h"

#include "helpers/discovery_test_doubles.h"
#include "helpers/document_file_watch.h"
#include "helpers/ui_test_support.h"

#include "application/app_messages.h"
#include "presentation/ui/context_menu_popup.h"
#include "presentation/ui/gitman_policy.h"

#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

namespace {
    // 창 없이 조립체 전체(worker·store·executor·provider)를 실제로 돌린다. 예전
    // `app_runtime` test와 같은 시험이며 조립만 luil의 headless `app_host`다:
    // driver를 logic thread에 꽂고, delegate의 `on_started`가 하던 bind·startup
    // 게시를 여기서 대신한다 (설계 L3).
    struct driver_harness
    {
        gitman::win32::gitman_driver driver {};
        gitman::ui::gitman_policy policy {};
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, &policy };

        driver_harness()
        {
            // `gitman_delegate::on_started`와 같은 순서다. bind가 먼저라야 묶이기
            // 전에 끝난 worker 결과도 깨우기를 받는다.
            driver.bind_host(host);
            post(gitman::startup_intent {});
        }

        driver_harness(const driver_harness&) = delete;
        driver_harness(driver_harness&&) = delete;
        driver_harness& operator=(const driver_harness&) = delete;
        driver_harness& operator=(driver_harness&&) = delete;

        ~driver_harness()
        {
            // 멱등이라 test가 이미 불렀어도 안전하다.
            host.shutdown();
        }

        template<typename intent_type>
        void post(intent_type intent)
        {
            host.post_app_message(luil::app_message { gitman::logic_message { std::move(intent) } });
        }

        // driver가 마지막으로 만든 view를 폴링한다. frame은 메시지를 처리한 뒤에만
        // 게시되므로 worker 결과가 도는 동안 여러 번 갱신된다.
        [[nodiscard]] std::shared_ptr<const gitman::view_snapshot> wait_for_view(const std::chrono::milliseconds limit, const std::function<bool(const gitman::view_snapshot&)>& ready) const
        {
            const auto deadline { std::chrono::steady_clock::now() + limit };
            while (std::chrono::steady_clock::now() < deadline)
            {
                const std::shared_ptr<const gitman::view_snapshot> view { driver.last_view() };
                if (view != nullptr && ready(*view))
                    return view;
                std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
            }
            return nullptr;
        }
    };

    bool initial_query_settled(const gitman::view_snapshot& value)
    {
        return value.cards.size() == 1u && value.cards.front().busy == false && value.cards.front().state != gitman::card_view_state::loading;
    }

    bool any_view(const gitman::view_snapshot&)
    {
        return true;
    }

    // JSON 문서에 실을 경로다. 역슬래시는 이스케이프 없이 쓸 수 없다.
    [[nodiscard]] std::string json_path(const std::u8string& path)
    {
        std::string converted { path.begin(), path.end() };
        for (char& value : converted)
            if (value == '\\')
                value = '/';
        return converted;
    }

    void write_document(const std::u8string& document_path, const std::string& projects)
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary };
        stream << "{\"schema_version\":1,\"projects\":[" << projects << "]}";
    }

    [[nodiscard]] std::string project_entry(const std::string& id, const std::u8string& path)
    {
        return "{\"id\":\"" + id + "\",\"path\":\"" + json_path(path) + "\"}";
    }
} // namespace

TEST_CASE("The assembled driver loads a real document and finishes the initial query", "[driver][integration]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string project_path { directory.make_directory(u8"plain-project") };
    const std::u8string document_path { directory.path_of(u8"workspace.version-list") };
    write_document(document_path, project_entry("plain", project_path));

    driver_harness harness {};
    harness.post(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    harness.post(gitman::open_document_intent { document_path });

    // 문서가 열려 카드가 생기고, 초기 로컬 조회가 끝나 loading/running이 풀릴 때까지
    // 기다린다. 등록 경로는 저장소가 아니므로 실제 provider가 not_a_repository로
    // 판정해야 한다.
    const auto view { harness.wait_for_view(std::chrono::milliseconds { 15000 }, &initial_query_settled) };
    REQUIRE(view != nullptr);
    REQUIRE(view->document_path == document_path);
    REQUIRE(view->cards.front().state == gitman::card_view_state::failed);
    REQUIRE(view->cards.front().id.value == u8"plain");

    // 종료가 스레드를 모두 join하고 멱등으로 끝난다. test가 끝나는 것 자체가 검증이다.
    harness.host.shutdown();
    harness.host.shutdown();
}

TEST_CASE("A reorder intent persists the new order into the document file", "[driver][integration]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string first_path { directory.make_directory(u8"one-project") };
    const std::u8string second_path { directory.make_directory(u8"two-project") };
    const std::u8string document_path { directory.path_of(u8"reorder.version-list") };
    write_document(document_path, project_entry("alpha", first_path) + "," + project_entry("beta", second_path));

    driver_harness harness {};
    harness.post(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    harness.post(gitman::open_document_intent { document_path });

    const auto both_cards { [](const gitman::view_snapshot& value) { return value.cards.size() == 2u; } };
    REQUIRE(harness.wait_for_view(std::chrono::milliseconds { 15000 }, both_cards) != nullptr);

    // alpha를 beta 뒤로 옮긴다. 화면 순서 = 문서 순서가 함께 바뀐다. 저장이 끝나기 전에 문서를 열면 store의 rename 교체가
    // 실패하므로(열린 handle이 있으면 공유 모드와 상관없이 거부된다) 디렉터리 항목으로 교체를 확인한 뒤 한 번만 읽는다 (S5).
    const gitman::testing::document_file_stamp before_save { gitman::testing::stamp_document_file(document_path) };
    harness.post(gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"beta" }, true });
    const auto reordered = [](const gitman::view_snapshot& value) {
        return value.cards.size() == 2u && value.cards.front().id.value == u8"beta" && value.cards.back().id.value == u8"alpha";
    };
    REQUIRE(harness.wait_for_view(std::chrono::milliseconds { 15000 }, reordered) != nullptr);

    // 다른 저장이 먼저 교체하더라도 새 순서가 담긴 교체를 마감까지 기다린다.
    const auto beta_first = [](const std::string& content)
    {
        const std::size_t beta_position { content.find("\"beta\"") };
        const std::size_t alpha_position { content.find("\"alpha\"") };
        return beta_position != std::string::npos && alpha_position != std::string::npos && beta_position < alpha_position;
    };
    REQUIRE(gitman::testing::wait_for_document_content(document_path, before_save, std::chrono::milliseconds { 15000 }, beta_first).has_value());

    harness.host.shutdown();
}

TEST_CASE("The driver publishes an initial empty snapshot and shuts down cleanly without work", "[driver][integration]")
{
    driver_harness harness {};
    const auto view { harness.wait_for_view(std::chrono::milliseconds { 5000 }, &any_view) };
    REQUIRE(view != nullptr);
    REQUIRE(view->empty_state == gitman::view_empty_state::no_document);
    harness.host.shutdown();
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

TEST_CASE("A hundred plus cards keep the snapshot flow alive under real load", "[driver][integration][stress]")
{
    // ADR-004의 검증 항목이다. 실제 provider가 카드 120개를 병렬 조회하는 동안
    // frame 게시와 intent 게시가 계속 동작해야 한다. 등록 경로가 저장소가
    // 아니므로 카드마다 실제 판정이 일어난다.
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());

    std::string projects {};
    for (int index = 0; index < 120; ++index)
    {
        const std::string digits { std::to_string(index) };
        const std::u8string path { directory.make_directory(std::u8string { u8"p" } + std::u8string { digits.begin(), digits.end() }) };
        if (projects.empty() == false)
            projects += ',';
        projects += project_entry("card-" + digits, path);
    }
    const std::u8string document_path { directory.path_of(u8"storm.version-list") };
    write_document(document_path, projects);

    driver_harness harness {};
    harness.post(gitman::window_metrics_intent { 1200.0f, 800.0f, 1.0f });
    harness.post(gitman::open_document_intent { document_path });

    // 폭풍이 진행되는 동안 선택과 스크롤 intent를 계속 흘려 보낸다. 게시가
    // 블로킹되거나 frame 흐름이 멎으면 아래 대기가 실패한다.
    std::uint64_t interactions { 0 };
    const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 90 } };
    std::shared_ptr<const gitman::view_snapshot> settled {};
    while (std::chrono::steady_clock::now() < deadline)
    {
        harness.post(gitman::select_card_intent { gitman::project_id { u8"card-7" } });
        harness.post(gitman::scroll_intent { 24.0f });
        ++interactions;

        const std::shared_ptr<const gitman::view_snapshot> view { harness.driver.last_view() };
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
    harness.post(gitman::select_card_intent { gitman::project_id { u8"card-7" } });
    REQUIRE(harness.wait_for_view(std::chrono::milliseconds { 5000 }, &card_seven_selected) != nullptr);

    harness.host.shutdown();
}

TEST_CASE("The frame carries the tree, appearance, fonts and placement revision of its view", "[driver][frame]")
{
    // host 없이 logic thread 역할을 test가 맡는다. `make_frame`이 view의 어떤 값을
    // frame으로 옮기는지가 관심사다 (계약 3장 make_frame).
    gitman::win32::gitman_driver driver {};
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f } } });

    const std::shared_ptr<const luil::win32::ui_frame> frame { driver.make_frame() };
    REQUIRE(frame != nullptr);
    const std::shared_ptr<const gitman::view_snapshot> view { driver.last_view() };
    REQUIRE(view != nullptr);

    // tree는 view로 지은 것이고 중복 id·미배치가 없다.
    REQUIRE(frame->tree != nullptr);
    gitman::testing::require_clean_tree(*frame->tree);
    REQUIRE(gitman::testing::find(*frame->tree, luil::ui_element_kind::root) != nullptr);
    REQUIRE(gitman::testing::find(*frame->tree, luil::ui_element_kind::caption) != nullptr);

    // 외양·글꼴·배치 개정은 view의 값 그대로다. UI thread는 frame만 보고 판단한다.
    REQUIRE(frame->appearance == view->appearance);
    REQUIRE(frame->fonts == view->fonts);
    REQUIRE(frame->window_placement_revision == view->window_placement_revision);
    REQUIRE(frame->window_placement_request.has_value() == view->window_placement_request.has_value());

    // 문서가 없으므로 창 배치 요청도 없다 (열 때 비로소 생긴다).
    REQUIRE(frame->window_placement_request.has_value() == false);

    // 메뉴가 없으면 popup 목록도 비어 있다.
    REQUIRE(frame->popups.empty());
}

TEST_CASE("An open context menu rides the frame as a popup", "[driver][frame][popup]")
{
    // 컨텍스트 메뉴는 주 tree의 overlay가 아니라 popup 창이다 (설계 D3-A). 앞 사례와
    // 같이 host 없이 driver를 직접 굴리고, 문서는 적재 결과를 곧바로 넣어 카드를 세운다.
    gitman::win32::gitman_driver driver {};
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f } } });

    gitman::document_loaded_event loaded {};
    gitman::workspace_document document {};
    document.document_path = u8"C:\\work\\p.version-list";
    gitman::project_definition project {};
    project.id.value = u8"alpha";
    project.display_name = u8"alpha";
    project.path.original = u8"C:\\work\\alpha";
    project.path.normalized = project.path.original;
    document.projects.push_back(std::move(project));
    loaded.document = { std::move(document) };
    driver.handle(luil::app_message { gitman::logic_message { std::move(loaded) } });
    driver.handle(luil::app_message { gitman::logic_message { gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 120.0f, 240.0f } } });

    const std::shared_ptr<const luil::win32::ui_frame> frame { driver.make_frame() };
    REQUIRE(frame != nullptr);
    REQUIRE(frame->popups.size() == 1u);
    REQUIRE(frame->popups.front().id == gitman::ui::card_menu_popup_id);
    REQUIRE(frame->popups.front().tree != nullptr);
    // 주 tree에는 메뉴가 없다 — popup tree가 통째로 들고 있다.
    REQUIRE(frame->tree->ids_of_kind(luil::ui_element_kind::menu).empty());

    // 배율이 바뀌면 물리 앵커를 새 배율로 나눈 자리에 popup이 선다.
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 800.0f, 600.0f, 2.0f } } });
    const std::shared_ptr<const luil::win32::ui_frame> scaled { driver.make_frame() };
    REQUIRE(scaled->popups.size() == 1u);
    REQUIRE(scaled->popups.front().x == 60.0f);
    REQUIRE(scaled->popups.front().y == 120.0f);

    // 닫으면 popup도 함께 빠진다.
    driver.handle(luil::app_message { gitman::logic_message { gitman::close_context_menu_intent {} } });
    REQUIRE(driver.make_frame()->popups.empty());
}

TEST_CASE("A bar button that leaves the bar rides the frame as a focus request", "[driver][frame][function_buttons][t10]")
{
    // 초점 옮기기의 대상은 driver가 지은 tree에서 고른다 (function-buttons-design 2.2.5). logic은 후보만 싣는다.
    // 문서에 revision이 없어 배치 변경의 저장은 파일을 건드리지 않고 거절된다.
    gitman::win32::gitman_driver driver {};
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f } } });

    gitman::document_loaded_event loaded {};
    gitman::workspace_document document {};
    document.document_path = u8"C:\\work\\p.version-list";
    gitman::project_definition project {};
    project.id.value = u8"alpha";
    project.display_name = u8"alpha";
    project.path.original = u8"C:\\work\\alpha";
    project.path.normalized = project.path.original;
    project.path.state = gitman::configured_path_state::available;
    project.enabled = false;
    document.projects.push_back(std::move(project));
    gitman::function_buttons_definition definition {};
    for (const std::u8string_view id : { std::u8string_view { u8"b" }, std::u8string_view { u8"c" } })
    {
        gitman::function_button button {};
        button.id = id;
        button.label = id;
        button.program = u8"C:\\Tools\\run.exe";
        button.placement = std::u8string { u8"bar" };
        definition.buttons.emplace_back(std::move(button));
    }
    document.function_buttons = std::move(definition);
    loaded.document = { std::move(document) };
    driver.handle(luil::app_message { gitman::logic_message { std::move(loaded) } });
    driver.handle(luil::app_message { gitman::logic_message { gitman::select_card_intent { gitman::project_id { u8"alpha" } } } });

    // 요청이 없으면 frame에도 없다.
    REQUIRE(driver.make_frame()->focus.has_value() == false);

    driver.handle(luil::app_message { gitman::logic_message { gitman::open_function_placement_menu_intent { gitman::project_id { u8"alpha" }, u8"b", true, 0.0f, 0.0f } } });
    driver.handle(luil::app_message { gitman::logic_message { gitman::select_context_menu_item_intent { gitman::context_menu_entry::place_function_in_menu, u8"b" } } });
    const std::shared_ptr<const luil::win32::ui_frame> frame { driver.make_frame() };
    REQUIRE(frame->focus.has_value());
    REQUIRE(frame->focus->id == driver.last_view()->function_focus->id);
    REQUIRE(frame->focus->surface.empty());
    REQUIRE(frame->focus->target == luil::ui_element_id { gitman::ui::kind_function_bar_button, u8"alpha:c" });
    REQUIRE(frame->tree->find(frame->focus->target) != nullptr);
}

TEST_CASE("Cancel shortens handling to the close message only", "[driver][shutdown]")
{
    // `cancel()`은 UI thread가 종료를 알릴 때 부른다. 그 뒤로 logic은 close만
    // 처리하고 tree도 다시 짓지 않아 종료 예산 안에 빠져나온다 (설계 L3).
    gitman::win32::gitman_driver driver {};
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 1200.0f, 900.0f, 1.0f } } });
    const std::shared_ptr<const luil::win32::ui_frame> before { driver.make_frame() };
    REQUIRE(driver.last_view()->window_width == 1200.0f);
    REQUIRE(driver.shutdown_completed() == false);

    driver.cancel();

    // 종료 중에는 close가 아닌 메시지가 controller에 닿지 않고 frame도 그대로다.
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 640.0f, 480.0f, 1.0f } } });
    REQUIRE(driver.make_frame() == before);
    REQUIRE(driver.last_view()->window_width == 1200.0f);
    REQUIRE(driver.shutdown_completed() == false);

    // close는 종료 중에도 통과한다 — `cancel()`이 close 게시보다 먼저 불리기 때문이다.
    const luil::app_message close { driver.make_close_message() };
    REQUIRE(gitman::testing::unwrap(close) != nullptr);
    REQUIRE(std::holds_alternative<gitman::close_intent>(*gitman::testing::unwrap(close)));
    driver.handle(close);
    REQUIRE(driver.shutdown_completed());

    // 두 번째 close도 안전하다 (종료 경로는 여러 번 지날 수 있다).
    driver.handle(driver.make_close_message());
    REQUIRE(driver.shutdown_completed());
}

TEST_CASE("The driver asks the controller when to wake only before shutdown", "[driver][shutdown][function_buttons][s3]")
{
    // luil은 frame을 게시할 때마다 `next_tick`을 묻는다. 기다리는 일이 없으면 깨울 시각이 없고, 종료 중에는 controller에
    // 묻지도 넘기지도 않는다 (S3). 기다리는 일이 있을 때 값을 넘기는 것은 logic test가 controller에서 본다 — driver의
    // 실제 worker가 낸 답이 relay에 언제 닿을지 test가 정할 수 없어서다.
    gitman::win32::gitman_driver driver {};
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f } } });
    REQUIRE(driver.next_tick().has_value() == false);
    const std::shared_ptr<const luil::win32::ui_frame> before { driver.make_frame() };
    driver.tick(std::chrono::steady_clock::now() + std::chrono::hours { 1 });
    REQUIRE(driver.last_view()->function_confirm_dialog.has_value() == false);

    driver.cancel();
    REQUIRE(driver.next_tick().has_value() == false);
    driver.tick(std::chrono::steady_clock::now() + std::chrono::hours { 1 });
    REQUIRE(driver.make_frame() == before);
    driver.handle(driver.make_close_message());
    REQUIRE(driver.shutdown_completed());
}

TEST_CASE("Messages that are not logic messages are ignored", "[driver][frame]")
{
    // luil은 앱 메시지의 내용을 모른다. 다른 앱 타입이나 빈 상자가 와도 driver는
    // relay만 비우고 조용히 넘어간다.
    gitman::win32::gitman_driver driver {};
    driver.handle(luil::app_message {});
    driver.handle(luil::app_message { std::u8string { u8"not a logic message" } });
    REQUIRE(driver.shutdown_completed() == false);
    REQUIRE(driver.make_frame() != nullptr);
}

#include "platform/win32/gitman_driver.h"

#include "helpers/discovery_test_doubles.h"
#include "helpers/ui_test_support.h"

#include "application/app_messages.h"
#include "presentation/ui/gitman_policy.h"

#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

// 조립한 driver로 보는 다시 읽기와 종료다 (function-buttons-design 4.9, 4.10, 6.1 T11). 실제 store로 손 편집 뒤의 저장이
// 다시 읽기로 회복되는 것과, 출력 수집 기능이 실행 중일 때 종료를 확인하면 worker join이 매달리지 않는 것을 본다.
// 실제 프로세스는 `gitman_process_test_child`만 띄운다.
namespace {
    constexpr std::chrono::milliseconds wait_limit { 15000 };
    constexpr std::u8string_view conflict_notice { u8"문서가 밖에서 바뀌어 저장하지 않았습니다. 문서 다시 읽기로 불러오십시오." };
    constexpr std::u8string_view reload_failed_notice { u8"문서를 다시 읽지 못해 지금 문서를 그대로 둡니다." };

    std::u8string test_child_path()
    {
        std::array<wchar_t, MAX_PATH> module_path {};
        const DWORD length { GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size())) };
        REQUIRE(length > 0);

        std::filesystem::path path { std::wstring_view { module_path.data(), static_cast<std::size_t>(length) } };
        path.replace_filename(L"gitman_process_test_child.exe");
        REQUIRE(std::filesystem::exists(path));
        return path.u8string();
    }

    // `gitman_driver_tests.cpp`의 조립과 같다. 창 없이 luil의 headless `app_host`에 driver를 꽂는다.
    struct reload_harness
    {
        gitman::win32::gitman_driver driver {};
        gitman::ui::gitman_policy policy {};
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, &policy };

        reload_harness()
        {
            driver.bind_host(host);
            post(gitman::startup_intent {});
        }

        reload_harness(const reload_harness&) = delete;
        reload_harness(reload_harness&&) = delete;
        reload_harness& operator=(const reload_harness&) = delete;
        reload_harness& operator=(reload_harness&&) = delete;

        ~reload_harness()
        {
            host.shutdown();
        }

        template<typename intent_type>
        void post(intent_type intent)
        {
            host.post_app_message(luil::app_message { gitman::logic_message { std::move(intent) } });
        }

        [[nodiscard]] std::shared_ptr<const gitman::view_snapshot> wait_for_view(const std::function<bool(const gitman::view_snapshot&)>& ready) const
        {
            const auto deadline { std::chrono::steady_clock::now() + wait_limit };
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

    [[nodiscard]] std::string json_path(const std::u8string& path)
    {
        std::string converted { path.begin(), path.end() };
        for (char& value : converted)
            if (value == '\\')
                value = '/';
        return converted;
    }

    void write_file(const std::u8string& path, const std::string& content)
    {
        std::ofstream stream { std::filesystem::path { path }, std::ios::binary };
        stream << content;
    }

    [[nodiscard]] std::string read_file(const std::u8string& path)
    {
        std::ifstream stream { std::filesystem::path { path }, std::ios::binary };
        return std::string { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    }

    [[nodiscard]] std::string document_text(const std::u8string& alpha, const std::u8string& beta, const std::string& alpha_name)
    {
        return "{\"schema_version\":1,\"projects\":["
            "{\"id\":\"alpha\",\"display_name\":\"" + alpha_name + "\",\"path\":\"" + json_path(alpha) + "\"},"
            "{\"id\":\"beta\",\"path\":\"" + json_path(beta) + "\"}]}";
    }

    [[nodiscard]] bool front_notice_is(const gitman::view_snapshot& view, const std::u8string_view text)
    {
        return view.notices.empty() == false && view.notices.front().text == text;
    }
} // namespace

TEST_CASE("A corrupt reload keeps the document and a reload after a hand edit recovers saving", "[driver][integration][function_buttons][t11]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string alpha { directory.make_directory(u8"alpha-project") };
    const std::u8string beta { directory.make_directory(u8"beta-project") };
    const std::u8string document_path { directory.path_of(u8"reload.version-list") };
    write_file(document_path, document_text(alpha, beta, "alpha"));

    reload_harness harness {};
    harness.post(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    harness.post(gitman::open_document_intent { document_path });
    REQUIRE(harness.wait_for_view([](const gitman::view_snapshot& view) { return view.cards.size() == 2u; }) != nullptr);

    // 손 편집 중 JSON이 깨진 채 다시 읽으면 지금 문서와 카드가 남고 알림 띠에 실패가 선다 (4.9의 4).
    write_file(document_path, "{\"schema_version\":1,\"projects\":[");
    harness.post(gitman::reload_document_intent {});
    const auto failed { harness.wait_for_view([](const gitman::view_snapshot& view) { return front_notice_is(view, reload_failed_notice); }) };
    REQUIRE(failed != nullptr);
    REQUIRE(failed->document_path == document_path);
    REQUIRE(failed->cards.size() == 2u);
    REQUIRE(failed->cards.front().display_name == u8"alpha");

    // 고친 문서를 다시 읽기 전의 저장은 낙관적 잠금에 걸리고, 다시 읽기로 불러오라고 알린다.
    write_file(document_path, document_text(alpha, beta, "Alpha Edited"));
    harness.post(gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"beta" }, true });
    REQUIRE(harness.wait_for_view([](const gitman::view_snapshot& view) { return front_notice_is(view, conflict_notice); }) != nullptr);
    REQUIRE(read_file(document_path).find("Alpha Edited") != std::string::npos);

    // 저장하지 못한 카드 순서가 사라진다고 먼저 묻는다 (D28). 확인하면 손 편집이 화면에 오고 알림이 걷힌다.
    harness.post(gitman::reload_document_intent {});
    REQUIRE(harness.wait_for_view([](const gitman::view_snapshot& view) { return view.function_confirm_dialog.has_value() && view.function_confirm_dialog->unsaved_changes; }) != nullptr);
    harness.post(gitman::confirm_function_run_intent {});
    const auto reloaded = [](const gitman::view_snapshot& view)
    {
        return view.notices.empty() && view.cards.size() == 2u && view.cards.front().display_name == u8"Alpha Edited";
    };
    REQUIRE(harness.wait_for_view(reloaded) != nullptr);

    // 이제 저장이 된다. 손 편집도 함께 남는다.
    harness.post(gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"beta" }, true });
    bool persisted { false };
    const auto deadline { std::chrono::steady_clock::now() + wait_limit };
    while (std::chrono::steady_clock::now() < deadline)
    {
        const std::string content { read_file(document_path) };
        const std::size_t beta_position { content.find("\"beta\"") };
        const std::size_t alpha_position { content.find("\"alpha\"") };
        if (beta_position != std::string::npos && alpha_position != std::string::npos && beta_position < alpha_position && content.find("Alpha Edited") != std::string::npos)
        {
            persisted = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
    }
    REQUIRE(persisted);
    const auto settled { harness.driver.last_view() };
    REQUIRE(settled != nullptr);
    REQUIRE(settled->notices.empty());

    harness.host.shutdown();
}

TEST_CASE("Confirming exit with a running capture does not hang the worker join", "[driver][function_buttons][t11][process]")
{
    // host 없이 logic thread 역할을 test가 맡는다 (`gitman_driver_tests.cpp`의 frame test와 같다). 기능 실행은 driver의
    // 실제 풀과 runner로 test child를 30초 재운다. 종료 순서가 그 실행을 취소하지 않으면 `stop_workers`의 풀 join이
    // 그만큼 매달린다.
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string project_path { directory.make_directory(u8"sleeper") };

    gitman::win32::gitman_driver driver {};
    driver.handle(luil::app_message { gitman::logic_message { gitman::window_metrics_intent { 1280.0f, 720.0f, 1.0f } } });

    gitman::document_loaded_event loaded {};
    gitman::workspace_document document {};
    document.document_path = directory.path_of(u8"exit.version-list");
    gitman::project_definition project {};
    project.id.value = u8"alpha";
    project.display_name = u8"alpha";
    project.path.original = project_path;
    project.path.normalized = project_path;
    project.path.state = gitman::configured_path_state::available;
    // 조회하지 않게 제외한다. 문서 출처 변수만 쓰는 기능은 제외된 카드에서도 실행된다 (D17).
    project.enabled = false;
    document.projects.push_back(std::move(project));
    gitman::function_button button {};
    button.id = u8"sleep";
    button.label = u8"잠자기";
    button.program = test_child_path();
    button.args = { u8"sleep", u8"30000" };
    gitman::function_buttons_definition definition {};
    definition.buttons.emplace_back(std::move(button));
    document.function_buttons = std::move(definition);
    loaded.document = { std::move(document) };
    driver.handle(luil::app_message { gitman::logic_message { std::move(loaded) } });
    driver.handle(luil::app_message { gitman::logic_message { gitman::select_card_intent { gitman::project_id { u8"alpha" } } } });

    // 실행 중인 출력 수집 기능이 없으면 닫힘 훅이 없다. luil이 지금처럼 곧바로 끝낸다.
    REQUIRE(driver.make_frame()->close == nullptr);

    // 앱 설정을 읽기 전이라 신뢰는 메모리에만 남고 파일에 쓰지 않는다.
    driver.handle(luil::app_message { gitman::logic_message { gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"sleep" } } });
    REQUIRE(driver.make_frame() != nullptr);
    REQUIRE(driver.last_view()->function_confirm_dialog.has_value());
    REQUIRE(driver.last_view()->function_confirm_dialog->mode == gitman::function_confirm_mode::trust);
    driver.handle(luil::app_message { gitman::logic_message { gitman::confirm_function_run_intent {} } });

    // 자식이 실제로 돌 때까지 기다린다. 첫 줄 `sleeping`이 주기 flush로 카드 로그에 온다.
    const auto sleeping = [&driver]
    {
        const std::shared_ptr<const gitman::view_snapshot> view { driver.last_view() };
        if (view == nullptr || view->log.has_value() == false)
            return false;
        for (const gitman::operation_log_record& record : view->log->records)
        {
            if (record.entry.text == u8"[잠자기] sleeping")
                return true;
        }
        return false;
    };
    const auto deadline { std::chrono::steady_clock::now() + wait_limit };
    while (std::chrono::steady_clock::now() < deadline)
    {
        // 빈 메시지도 relay를 먼저 비운다.
        driver.handle(luil::app_message {});
        static_cast<void>(driver.make_frame());
        if (sleeping())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
    }
    REQUIRE(sleeping());

    // 실행 중이면 frame이 닫힘 훅을 들고, 훅은 종료 확인 intent를 낸다 (4.10).
    const std::shared_ptr<const luil::win32::ui_frame> frame { driver.make_frame() };
    REQUIRE(frame->close != nullptr);
    const luil::input_action action { frame->close() };
    const luil::app_message* const message { std::get_if<luil::app_message>(&action) };
    REQUIRE(message != nullptr);
    const gitman::logic_message* const intent { gitman::testing::unwrap(*message) };
    REQUIRE(intent != nullptr);
    REQUIRE(std::holds_alternative<gitman::request_exit_intent>(*intent));
    driver.handle(*message);
    static_cast<void>(driver.make_frame());
    REQUIRE(driver.last_view()->function_confirm_dialog.has_value());
    REQUIRE(driver.last_view()->function_confirm_dialog->exit_application);

    // 확인 버튼의 앱 끝내기 명령 뒤 luil이 밟는 종료 순서다: `cancel` → close → `stop_workers`. `cancel`의 registry 취소가
    // 30초 자는 자식을 끝내므로 풀 join이 곧바로 돌아온다.
    const auto begin { std::chrono::steady_clock::now() };
    driver.cancel();

    SECTION("close 메시지까지 밟는다")
    {
        driver.handle(driver.make_close_message());
        REQUIRE(driver.shutdown_completed());
    }

    SECTION("close 메시지가 유실된다")
    {
        // `begin_shutdown`이 실행을 취소하지 못하는 경로다. registry 취소만으로 join이 돌아와야 한다 (4.10).
        REQUIRE(driver.shutdown_completed() == false);
    }

    driver.stop_workers();
    const auto elapsed { std::chrono::steady_clock::now() - begin };
    REQUIRE(elapsed < std::chrono::seconds { 10 });
}

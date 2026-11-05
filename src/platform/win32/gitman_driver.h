#pragma once

#include "application/app_messages.h"
#include "application/cancellation_registry.h"
#include "application/directory_enumerator.h"
#include "application/function_execution_pool.h"
#include "application/logic_controller.h"
#include "application/process_launcher.h"
#include "application/process_runner.h"
#include "application/project_path_resolver.h"
#include "application/task_scheduler.h"
#include "application/vcs_file_probe.h"
#include "infrastructure/file_log_writer.h"
#include "infrastructure/function_execution_policy.h"
#include "infrastructure/function_process_executor.h"
#include "infrastructure/json_app_settings_store.h"
#include "infrastructure/json_project_store.h"
#include "infrastructure/vcs_operation_executor.h"
#include "platform/win32/operation_result_relay.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/win32_directory_enumerator.h"
#include "platform/win32/win32_log_file_system.h"
#include "platform/win32/win32_process_launcher.h"
#include "platform/win32/win32_process_runner.h"
#include "platform/win32/win32_vcs_file_probe.h"
#include "platform/win32/workspace_document_file_system.h"
#include "presentation/view_snapshot.h"

#include "luil/win32/app_host.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace gitman::win32 {
    // luil logic thread에 꽂는 gitman 구동기다 (설계 L3). 예전 `app_runtime::assembly`가
    // 소유하던 그래프를 그대로 멤버로 든다. 객체는 UI thread에서 만들지만
    // `start`·`handle`·`make_frame`·`tick`은 logic thread에서만 불린다
    // (`cancel`·`make_close_message`·`shutdown_completed`는 UI thread도 부르므로
    // atomic·mutex로 지킨다).
    class gitman_driver final : public luil::win32::logic_driver
    {
    public:
        gitman_driver();
        // 실행 경계를 바꾼 조립이다. executor는 driver보다 오래 살아야 하며 worker thread에서 호출된다.
        explicit gitman_driver(operation_executor& executor);
        ~gitman_driver() override;

        // delegate의 `on_started` 첫 줄이다 (UI thread). 이 뒤부터 worker 결과가
        // 깨우기를 낼 수 있다.
        void bind_host(luil::win32::app_host& host);

        void handle(luil::app_message message) override;
        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override;
        void cancel() noexcept override;
        [[nodiscard]] luil::app_message make_close_message() override;
        [[nodiscard]] bool shutdown_completed() const override;
        void stop_workers() override;
        // 종료 확인이 저장을 기다리는 상한처럼 시간이 흘러야 바뀌는 logic 상태의 예고다 (function-buttons-design 4.10, S3).
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_tick() override;
        void tick(std::chrono::steady_clock::time_point now) override;

        // 마지막으로 만든 view다 (test 관찰용). logic thread 밖에서 읽어도 되도록
        // shared_ptr 사본을 돌려준다.
        [[nodiscard]] std::shared_ptr<const view_snapshot> last_view() const;

    private:
        void initialize(operation_executor& executor);
        // relay 큐를 비워 logic에 순서대로 넘긴다.
        void drain_relay();
        // 종료 규칙을 적용해 한 건을 controller에 넘긴다.
        void deliver(logic_message message);
        void update_shutdown_storage();
        void wait_for_shutdown_storage();

        // 선언 순서가 곧 파괴의 역순이다. controller가 scheduler·log_writer·registry를
        // 참조로 들기 때문에 마지막에 선다.
        workspace_document_file_system file_system_ {};
        std::unique_ptr<project_path_resolver> resolver_ { make_project_path_resolver() };
        std::unique_ptr<process_runner> runner_ { make_process_runner() };
        std::unique_ptr<process_launcher> launcher_ { make_process_launcher() };
        std::unique_ptr<vcs_file_probe> probe_ { make_vcs_file_probe() };
        std::unique_ptr<directory_enumerator> enumerator_ { make_directory_enumerator() };
        json_project_store store_ { file_system_, *resolver_ };
        json_app_settings_store app_settings_store_ { file_system_ };
        // 카드 로그를 문서 폴더에 적재하는 writer다 (app-shell-design A4). 전용
        // 스레드를 소유하며 logic은 큐에 넣기만 한다.
        win32::log_file_system log_files_ {};
        file_log_writer log_writer_ { log_files_ };
        vcs_operation_executor executor_ { store_, *runner_, *probe_, *enumerator_, *resolver_, current_vcs_tool_environment() };
        cancellation_registry registry_ {};
        operation_result_relay relay_ {};
        std::unique_ptr<task_scheduler> scheduler_ {};
        // 기능 버튼 전용 실행 풀이다 (function-buttons-design 4.4). scheduler와 따로 돈다.
        // controller보다 먼저 선언해 controller가 사라진 뒤에 사라진다.
        function_process_executor function_executor_ { *runner_, *launcher_ };
        std::unique_ptr<function_execution_pool> function_pool_ {};
        std::unique_ptr<logic_controller> controller_ {};

        // host는 logic join 뒤에 사라진다. UI thread의 bind·종료와 logic의 명령 게시 사이에서 포인터만 원자적으로 나른다.
        std::atomic<luil::win32::app_host*> host_ { nullptr };
        // logic thread 전용이다. 같은 종료 상태로 frame을 다시 지어도 명령은 한 번만 낸다.
        bool exit_command_posted_ { false };

        // 종료가 시작됐다. UI thread의 `cancel()`이 세우고 logic thread가 읽는다.
        std::atomic<bool> shutting_down_ { false };
        // close와 마지막 저장 결과까지 처리했다 (luil의 종료 완료 조건).
        std::atomic<bool> shutdown_completed_ { false };
        // close 접수와 저장 완료를 나눈다. worker 큐는 이어 낼 저장까지 logic이 처리한 뒤에 닫는다.
        std::atomic<bool> close_received_ { false };
        std::mutex shutdown_storage_mutex_ {};
        std::condition_variable shutdown_storage_changed_ {};
        // cancel 뒤 close보다 먼저 도착한 결과다. 종료 상태에 들어간 뒤 처리해 조회·기능을 다시 시작하지 않는다.
        std::vector<logic_message> deferred_shutdown_storage_ {};

        // 마지막 frame·view다. `make_frame`(logic)과 `last_view`(임의 thread)가
        // 함께 보므로 mutex로 지킨다.
        mutable std::mutex frame_mutex_ {};
        std::shared_ptr<const luil::win32::ui_frame> last_frame_ {};
        std::shared_ptr<const view_snapshot> last_view_ {};
    };
} // namespace gitman::win32

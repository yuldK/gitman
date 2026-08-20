#include "platform/win32/win32_app_runtime.h"

#include "application/logic_controller.h"
#include "application/task_scheduler.h"
#include "infrastructure/json_project_store.h"
#include "infrastructure/vcs_operation_executor.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/win32_directory_enumerator.h"
#include "platform/win32/win32_process_runner.h"
#include "platform/win32/win32_vcs_file_probe.h"
#include "platform/win32/workspace_document_file_system.h"
#include "presentation/ui/build_ui_tree.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace gitman::win32 {
    namespace {
        // 단계 6 계획 4.4의 초기값이다. 원격 조회는 대기 중심이라 소수로 충분하다.
        [[nodiscard]] std::size_t default_worker_count()
        {
            const unsigned int hardware { std::thread::hardware_concurrency() };
            return static_cast<std::size_t>(std::min(4u, hardware == 0 ? 1u : hardware));
        }

        void input_thread_main(messaging::channel<ui::raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui::ui_tree>>& tree_slot,
            messaging::channel<logic_message>& logic_inbox, messaging::latest_slot<ui::interaction_snapshot>& interaction_slot, const HWND wake_window, const UINT command_message)
        {
            // 더블 클릭 임계는 사용자의 시스템 설정을 따른다.
            ui::interaction_config config {};
            config.double_click_time = std::chrono::milliseconds { GetDoubleClickTime() };
            run_ui_input_pump(
                input_inbox, tree_slot, logic_inbox, interaction_slot,
                [wake_window, command_message](const ui::ui_command command) {
                    if (wake_window != nullptr)
                        PostMessageW(wake_window, command_message, static_cast<WPARAM>(command), 0);
                },
                config);
        }
    } // namespace

    // 구성 요소의 선언 순서가 곧 파괴의 역순이다. 스레드는 shutdown()이 먼저 멈추므로
    // 여기서는 소유만 담당한다.
    struct app_runtime::assembly
    {
        workspace_document_file_system file_system {};
        std::unique_ptr<project_path_resolver> resolver { make_project_path_resolver() };
        std::unique_ptr<process_runner> runner { make_process_runner() };
        std::unique_ptr<vcs_file_probe> probe { make_vcs_file_probe() };
        std::unique_ptr<directory_enumerator> enumerator { make_directory_enumerator() };
        json_project_store store { file_system, *resolver };
        vcs_operation_executor executor { store, *runner, *probe, *enumerator, *resolver, current_vcs_tool_environment() };

        messaging::channel<ui::raw_input_event> input_inbox { messaging::channel_options { 4096, messaging::overflow_policy::drop_oldest, {} } };
        messaging::channel<logic_message> logic_inbox { messaging::channel_options { 1024, messaging::overflow_policy::reject_newest, {} } };
        messaging::latest_slot<std::shared_ptr<const view_snapshot>> view_slot {};
        messaging::latest_slot<std::shared_ptr<const ui::ui_tree>> tree_slot {};
        messaging::latest_slot<ui::interaction_snapshot> interaction_slot {};

        std::unique_ptr<task_scheduler> scheduler {};
        std::thread logic_thread {};
        std::thread input_thread {};
    };

    app_runtime::app_runtime(const HWND window, const UINT snapshot_message, const UINT ui_command_message)
        : assembly_ { std::make_unique<assembly>() }
        , window_ { window }
        , snapshot_message_ { snapshot_message }
    {
        // 조립 순서는 "채널 생성 → callback 설정 → 스레드 시작"으로 고정한다 (ADR-005).
        const HWND wake_window { window_ };
        const UINT wake_message { snapshot_message_ };
        if (wake_window != nullptr)
        {
            assembly_->view_slot.set_signal_callback([wake_window, wake_message] { PostMessageW(wake_window, wake_message, 0, 0); });
            // hover·tooltip·drag 표시가 바뀌면 다시 그린다. wake 신호는 view와 같다.
            assembly_->interaction_slot.set_signal_callback([wake_window, wake_message] { PostMessageW(wake_window, wake_message, 0, 0); });
        }

        assembly_->scheduler = std::make_unique<task_scheduler>(assembly_->executor, assembly_->logic_inbox, default_worker_count());
        assembly_->logic_thread = std::thread { &app_runtime::logic_thread_main, this };

        assembly& parts { *assembly_ };
        assembly_->input_thread = std::thread {
            &input_thread_main,
            std::ref(parts.input_inbox),
            std::ref(parts.tree_slot),
            std::ref(parts.logic_inbox),
            std::ref(parts.interaction_slot),
            wake_window,
            ui_command_message,
        };
    }

    app_runtime::~app_runtime()
    {
        shutdown();
    }

    void app_runtime::shutdown() noexcept
    {
        if (shut_down_)
            return;
        shut_down_ = true;

        try
        {
            // 1. logic이 취소를 전파하고 종료 상태를 게시하게 한다.
            static_cast<void>(assembly_->logic_inbox.post(logic_message { close_intent {} }));

            // 2. logic이 close를 처리할 때까지 기다린다. 이 시점 이후에는 종료 저장
            //    (창 배치) 요청이 이미 worker lane에 들어가 있다.
            wait_for_logic_shutdown();

            // 3. worker를 먼저 끝낸다. 취소된 작업의 event가 아직 열린 logic inbox로
            //    들어가거나, 이후 닫힌 inbox에서 조용히 버려진다. 채널은 닫힌 뒤에도
            //    남은 요청을 소비하므로 종료 저장은 join 안에서 끝까지 실행된다.
            assembly_->scheduler->shutdown();

            // 4. logic inbox를 닫아 남은 메시지를 소진시키고 join한다.
            assembly_->logic_inbox.close();
            if (assembly_->logic_thread.joinable())
                assembly_->logic_thread.join();

            // 5. input을 닫고 join한 뒤 slot을 닫는다.
            assembly_->input_inbox.close();
            if (assembly_->input_thread.joinable())
                assembly_->input_thread.join();
            assembly_->view_slot.close();
            assembly_->tree_slot.close();
            assembly_->interaction_slot.close();
        }
        catch (...)
        {}
    }

    void app_runtime::wait_for_logic_shutdown() noexcept
    {
        // logic이 멈춘 비정상 상황에서 종료가 매달리지 않도록 상한을 둔다. 상한을
        // 넘으면 배치 저장을 포기하고 기존 순서대로 정리한다.
        constexpr std::chrono::milliseconds wait_limit { 3000 };
        const std::chrono::steady_clock::time_point deadline { std::chrono::steady_clock::now() + wait_limit };
        while (logic_shutdown_handled_.load(std::memory_order_acquire) == false)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
        }
    }

    void app_runtime::post_raw_input(ui::raw_input_event event) noexcept
    {
        try
        {
            // drop_oldest 채널이라 반환값 확인이 통계로 충분하다.
            static_cast<void>(assembly_->input_inbox.post(std::move(event)));
        }
        catch (...)
        {}
    }

    void app_runtime::post_logic(logic_message message) noexcept
    {
        try
        {
            static_cast<void>(assembly_->logic_inbox.post(std::move(message)));
        }
        catch (...)
        {}
    }

    std::shared_ptr<const view_snapshot> app_runtime::acquire_view()
    {
        if (const auto newer { assembly_->view_slot.take_newer(seen_view_version_) }; newer.has_value())
        {
            seen_view_version_ = newer->version;
            current_view_ = newer->value;
        }
        return current_view_;
    }

    std::shared_ptr<const ui::ui_tree> app_runtime::acquire_ui_tree()
    {
        if (const auto newer { assembly_->tree_slot.take_newer(seen_tree_version_) }; newer.has_value())
        {
            seen_tree_version_ = newer->version;
            current_tree_ = newer->value;
        }
        return current_tree_;
    }

    ui::interaction_snapshot app_runtime::acquire_interaction()
    {
        if (const auto newer { assembly_->interaction_slot.take_newer(seen_interaction_version_) }; newer.has_value())
        {
            seen_interaction_version_ = newer->version;
            current_interaction_ = newer->value;
        }
        return current_interaction_;
    }

    void app_runtime::publish_snapshots(logic_controller& controller)
    {
        const std::shared_ptr<const view_snapshot> view { controller.make_view_snapshot() };
        // wake 신호는 view 게시에 붙어 있고 tree slot에는 신호가 없다. tree를 먼저
        // 게시해야 wake를 받은 UI thread가 이전 tree로 그리는 일이 없다 — view를
        // 먼저 게시하면 tree 빌드가 끝나기 전에 paint가 끼어들어 화면이 다음
        // 이벤트까지 한 박자 늦는다 (텍스트 입력에서 실측된 race).
        static_cast<void>(assembly_->tree_slot.publish(ui::build_ui_tree(*view)));
        static_cast<void>(assembly_->view_slot.publish(view));
    }

    void app_runtime::logic_thread_main()
    {
        logic_controller controller { *assembly_->scheduler };

        publish_snapshots(controller);
        std::vector<messaging::envelope<logic_message>> batch {};
        messaging::envelope<logic_message> received {};
        while (true)
        {
            const messaging::receive_status status { assembly_->logic_inbox.receive_wait(received, std::chrono::milliseconds { 250 }) };
            if (status == messaging::receive_status::closed)
            {
                publish_snapshots(controller);
                return;
            }
            if (status != messaging::receive_status::received)
                continue;

            controller.handle(std::move(received.payload));

            // 몰려온 메시지는 한 번에 처리하고 snapshot은 batch당 한 번만 게시한다.
            // 상한이 렌더링 기회를 보존한다 (ADR-005 6.4).
            batch.clear();
            static_cast<void>(assembly_->logic_inbox.drain(batch, 63));
            for (messaging::envelope<logic_message>& entry : batch)
                controller.handle(std::move(entry.payload));
            publish_snapshots(controller);

            // 종료 저장 요청까지 나간 뒤에 알린다. shutdown()이 이 신호를 보고
            // worker inbox를 닫는다.
            if (controller.shutdown_requested())
                logic_shutdown_handled_.store(true, std::memory_order_release);
        }
    }
} // namespace gitman::win32

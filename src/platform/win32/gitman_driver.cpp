#include "platform/win32/gitman_driver.h"

#include "platform/win32/gitman_delegate.h"
#include "platform/win32/win32_app_settings_path.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/context_menu_popup.h"
#include "presentation/ui/ui_actions.h"

#include <algorithm>
#include <cstddef>
#include <thread>
#include <utility>
#include <variant>

namespace gitman::win32 {
    namespace {
        // 단계 6 계획 4.4의 초기값이다. 원격 조회는 대기 중심이라 소수로 충분하다.
        [[nodiscard]] std::size_t default_worker_count()
        {
            const unsigned int hardware { std::thread::hardware_concurrency() };
            return static_cast<std::size_t>(std::min(4u, hardware == 0 ? 1u : hardware));
        }
    } // namespace

    gitman_driver::gitman_driver()
    {
        initialize(executor_);
    }

    gitman_driver::gitman_driver(operation_executor& executor)
    {
        initialize(executor);
    }

    void gitman_driver::initialize(operation_executor& executor)
    {
        executor_.bind_cmd_target_inspector(*cmd_inspector_);
        // 앱 단위 설정 파일은 실행 파일 옆에 둔다 (app-shell-design A1.1). 경로는
        // 조립 시 한 번 정해 executor가 들고 있고 logic은 알지 못한다.
        executor_.bind_app_settings(app_settings_store_, app_settings_file_path());
        // worker 결과는 relay 큐가 정본이다 (설계 L3). scheduler는 sink만 안다.
        scheduler_ = std::make_unique<task_scheduler>(executor, [this](logic_message message) { relay_.post(std::move(message)); }, default_worker_count());
        function_pool_ = std::make_unique<function_execution_pool>(function_executor_, [this](logic_message message) { relay_.post(std::move(message)); });
        controller_ = std::make_unique<logic_controller>(*scheduler_, log_writer_, registry_);
        // 기능 실행은 전용 풀로 간다 (function-buttons-design 4.4). `${env:}`는 클릭 시점의
        // gitman 프로세스 환경이다 (3.3).
        controller_->bind_function_execution(*function_pool_
            , make_function_run_services(
                [](const std::u8string_view name)
                {
                    return find_environment_variable(name);
                }
            )
        );
    }

    gitman_driver::~gitman_driver() = default;

    void gitman_driver::bind_host(luil::win32::app_host& host)
    {
        host_.store(&host, std::memory_order_release);
        relay_.bind(host);
    }

    void gitman_driver::handle(luil::app_message message)
    {
        // 깨우기든 다른 메시지든 먼저 큐를 비운다. 깨우기가 유실돼도 다음 메시지가
        // 같은 일을 한다.
        drain_relay();

        const logic_message* const payload { message.get<logic_message>() };
        if (payload == nullptr)
            return;
        deliver(*payload);
    }

    std::shared_ptr<const luil::win32::ui_frame> gitman_driver::make_frame()
    {
        if (shutting_down_.load(std::memory_order_acquire))
        {
            // 종료 중에는 tree를 다시 짓지 않는다. logic thread가 종료 예산 안에
            // 빠져나오게 하는 조치다 (설계 8장).
            const std::lock_guard<std::mutex> lock { frame_mutex_ };
            if (last_frame_ == nullptr)
                last_frame_ = std::make_shared<const luil::win32::ui_frame>();
            return last_frame_;
        }

        const std::shared_ptr<const view_snapshot> view { controller_->make_view_snapshot() };
        const std::shared_ptr<luil::win32::ui_frame> frame { std::make_shared<luil::win32::ui_frame>() };
        frame->tree = ui::build_ui_tree(*view);
        // 컨텍스트 메뉴는 popup 창이다 (설계 D3-A). 주 tree 안 overlay가 아니라
        // frame의 popup 목록에 실어 창 경계를 넘게 한다.
        if (view->context_menu.has_value())
            frame->popups.push_back(ui::build_context_menu_popup(*view->context_menu, view->scale));
        frame->appearance = view->appearance;
        frame->fonts = view->fonts;
        if (view->application_exit_requested && exit_command_posted_ == false)
        {
            if (luil::win32::app_host* const host { host_.load(std::memory_order_acquire) }; host != nullptr)
            {
                exit_command_posted_ = true;
                host->post_ui_command(luil::ui_command::application_exit);
            }
        }
        // 출력 수집 기능이 실행 중이거나 저장하지 못한 변경이 있으면 주 창의 닫힘을 받아 logic이 그것을 보이고 묻는다
        // (function-buttons-design 4.10, D28). 확인 버튼이 앱 끝내기 명령을 내면 luil이 이 훅을 비켜 기존 종료 순서
        // (`cancel` → close → `stop_workers`)를 밟는다. 둘 다 없으면 훅을 걸지 않아 지금처럼 곧바로 끝난다.
        if (view->exit_needs_confirmation)
            frame->close = []
            {
                return ui::make_logic_action(request_exit_intent {});
            };
        frame->window_placement_revision = view->window_placement_revision;
        if (view->window_placement_request.has_value())
            frame->window_placement_request = to_luil_placement(*view->window_placement_request);
        // 배치 변경으로 초점을 가진 기능 줄 버튼이 사라졌으면 다음 자리로 초점을 옮긴다 (function-buttons-design 2.2.5).
        // logic은 무엇이 보이는지 모르므로 지은 tree에서 고른다. 갈 곳이 없어도 같은 번호를 빈 대상으로 실어 요청을
        // 끝낸다 — 그러지 않으면 나중에 다른 이유로 후보가 서는 순간 초점이 뜻밖에 옮겨 간다. 번호가 같은 요청은
        // UI thread가 한 번만 이행한다.
        if (view->function_focus.has_value())
        {
            luil::focus_request request {};
            request.id = view->function_focus->id;
            request.target = ui::function_focus_target(*frame->tree, *view->function_focus).value_or(luil::ui_element_id {});
            frame->focus = { std::move(request) };
        }

        // 게시 값만 lock 안에서 바꾼다. tree 짓기는 오래 걸리므로 밖에 둔다.
        const std::lock_guard<std::mutex> lock { frame_mutex_ };
        last_frame_ = frame;
        last_view_ = view;
        return frame;
    }

    void gitman_driver::cancel() noexcept
    {
        // close 메시지가 유실돼도 worker join이 매달리지 않게 한다 (설계 L3). 이
        // 플래그가 뒤따르는 handle·make_frame을 짧게 만든다.
        shutting_down_.store(true, std::memory_order_release);
        try
        {
            registry_.cancel_all();
        }
        catch (...)
        {}
    }

    luil::app_message gitman_driver::make_close_message()
    {
        return luil::app_message { logic_message { close_intent {} } };
    }

    bool gitman_driver::shutdown_completed() const
    {
        return shutdown_completed_.load(std::memory_order_acquire);
    }

    void gitman_driver::stop_workers()
    {
        wait_for_shutdown_storage();
        scheduler_->shutdown();
        function_pool_->shutdown();
        host_.store(nullptr, std::memory_order_release);
        relay_.unbind();
    }

    void gitman_driver::wait_for_shutdown_storage()
    {
        if (close_received_.load(std::memory_order_acquire) == false)
            return;
        while (shutdown_completed_.load(std::memory_order_acquire) == false)
        {
            luil::win32::app_host* const host { host_.load(std::memory_order_acquire) };
            if (host == nullptr)
                drain_relay();
            else if (host->faulted())
                return;
            // 느린 파일 I/O는 기존 worker join도 기다렸다. 그동안 큐를 열어 두어 결과와 다음 저장을 왕복시킨다.
            std::unique_lock<std::mutex> lock { shutdown_storage_mutex_ };
            shutdown_storage_changed_.wait_for(lock, std::chrono::milliseconds { 25 }, [this]
                {
                    return shutdown_completed_.load(std::memory_order_acquire);
                }
            );
        }
    }

    void gitman_driver::update_shutdown_storage()
    {
        if (controller_->shutdown_requested() == false)
            return;
        {
            // 마지막 결과는 테스트와 진단에도 남긴다. 종료 중 UI tree는 다시 짓지 않는다.
            const auto final_view { controller_->make_view_snapshot() };
            const std::lock_guard<std::mutex> lock { frame_mutex_ };
            last_view_ = final_view;
        }
        if (controller_->shutdown_storage_idle() == false)
            return;
        {
            const std::lock_guard<std::mutex> lock { shutdown_storage_mutex_ };
            shutdown_completed_.store(true, std::memory_order_release);
        }
        shutdown_storage_changed_.notify_all();
    }

    std::optional<std::chrono::steady_clock::time_point> gitman_driver::next_tick()
    {
        if (shutting_down_.load(std::memory_order_acquire))
            return std::nullopt;
        return controller_->next_tick();
    }

    void gitman_driver::tick(const std::chrono::steady_clock::time_point now)
    {
        drain_relay();
        if (shutting_down_.load(std::memory_order_acquire) == false)
            controller_->tick(now);
    }

    std::shared_ptr<const view_snapshot> gitman_driver::last_view() const
    {
        const std::lock_guard<std::mutex> lock { frame_mutex_ };
        return last_view_;
    }

    void gitman_driver::drain_relay()
    {
        relay_.drain([this](logic_message message) { deliver(std::move(message)); });
    }

    void gitman_driver::deliver(logic_message message)
    {
        const bool close { std::holds_alternative<close_intent>(message) };
        const bool storage { std::holds_alternative<document_saved_event>(message)
            || std::holds_alternative<app_settings_loaded_event>(message)
            || std::holds_alternative<app_settings_saved_event>(message)
        };
        if (shutting_down_.load(std::memory_order_acquire) && close == false)
        {
            if (storage)
            {
                if (close_received_.load(std::memory_order_acquire) == false)
                {
                    deferred_shutdown_storage_.push_back(std::move(message));
                    return;
                }
            }
            else if (std::holds_alternative<window_placement_intent>(message) == false
                || close_received_.load(std::memory_order_acquire))
                return;
        }

        controller_->handle(std::move(message));
        if (close)
        {
            for (logic_message& pending : deferred_shutdown_storage_)
                controller_->handle(std::move(pending));
            deferred_shutdown_storage_.clear();
            close_received_.store(true, std::memory_order_release);
        }
        if (close || storage)
            update_shutdown_storage();
    }
} // namespace gitman::win32

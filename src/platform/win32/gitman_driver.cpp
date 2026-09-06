#include "platform/win32/gitman_driver.h"

#include "platform/win32/gitman_delegate.h"
#include "platform/win32/win32_app_settings_path.h"
#include "presentation/ui/build_ui_tree.h"

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
        // 앱 단위 설정 파일은 실행 파일 옆에 둔다 (app-shell-design A1.1). 경로는
        // 조립 시 한 번 정해 executor가 들고 있고 logic은 알지 못한다.
        executor_.bind_app_settings(app_settings_store_, app_settings_file_path());
        // worker 결과는 relay 큐가 정본이다 (설계 L3). scheduler는 sink만 안다.
        scheduler_ = std::make_unique<task_scheduler>(executor_, [this](logic_message message) { relay_.post(std::move(message)); }, default_worker_count());
        controller_ = std::make_unique<logic_controller>(*scheduler_, log_writer_, registry_);
    }

    gitman_driver::~gitman_driver() = default;

    void gitman_driver::bind_host(luil::win32::app_host& host)
    {
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
        frame->appearance = view->appearance;
        frame->fonts = view->fonts;
        frame->window_placement_revision = view->window_placement_revision;
        if (view->window_placement_request.has_value())
            frame->window_placement_request = to_luil_placement(*view->window_placement_request);

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
        scheduler_->shutdown();
        relay_.unbind();
    }

    void gitman_driver::tick(const std::chrono::steady_clock::time_point now)
    {
        static_cast<void>(now);
        drain_relay();
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
        // `cancel()`이 close 게시보다 먼저 불리므로 close가 도착할 때 이미 종료
        // 중이다. 그래서 종료 중에도 close만은 통과시킨다.
        const bool close { std::holds_alternative<close_intent>(message) };
        if (close == false && shutting_down_.load(std::memory_order_acquire))
            return;

        controller_->handle(std::move(message));
        if (close)
            shutdown_completed_.store(true, std::memory_order_release);
    }
} // namespace gitman::win32

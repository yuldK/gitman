#pragma once

#include "application/app_messages.h"
#include "messaging/channel.h"
#include "messaging/latest_slot.h"
#include "presentation/ui/ui_events.h"
#include "presentation/ui/ui_interaction.h"
#include "presentation/ui/ui_tree.h"
#include "presentation/view_snapshot.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gitman {
    class logic_controller;
} // namespace gitman

namespace gitman::win32 {
    // 단계 6의 앱 조립체다. Win32 구현체 주입, 채널·slot, logic thread, input thread와
    // worker pool을 한 곳에서 소유한다 (계획 4.6). UI thread(창)는 이 객체를 통해서만
    // 나머지 스레드와 통신한다.
    //
    // `window`가 null이면 wake 게시를 생략한다. 창 없는 조립 test가 이 경로를 쓴다.
    class app_runtime
    {
    public:
        // ui_command_message는 input thread가 요청한 `ui::ui_command`를 UI thread로
        // 나르는 창 메시지다. wparam이 command 값이다.
        app_runtime(HWND window, UINT snapshot_message, UINT ui_command_message);
        app_runtime(const app_runtime&) = delete;
        app_runtime(app_runtime&&) = delete;
        app_runtime& operator=(const app_runtime&) = delete;
        app_runtime& operator=(app_runtime&&) = delete;
        ~app_runtime();

        // ADR-005 7.3의 순서로 스레드를 정리한다: 취소(close intent) → worker join →
        // logic join → input join. 멱등이며 UI thread에서 호출한다.
        void shutdown() noexcept;

        // UI thread 전용 진입점들이다.
        void post_raw_input(ui::raw_input_event event) noexcept;
        void post_logic(logic_message message) noexcept;
        // 마지막으로 게시된 view snapshot을 돌려준다. 새 것이 없으면 이전 값이다.
        [[nodiscard]] std::shared_ptr<const view_snapshot> acquire_view();
        // 마지막으로 게시된 ui tree다. 그리기와 caption 버튼의 동기 실행이 쓴다.
        [[nodiscard]] std::shared_ptr<const ui::ui_tree> acquire_ui_tree();
        // input thread가 게시한 최신 상호작용 상태다. hover·tooltip 그리기가 쓴다.
        [[nodiscard]] ui::interaction_snapshot acquire_interaction();

    private:
        void logic_thread_main();
        void publish_snapshots(logic_controller& controller);

        struct assembly;
        std::unique_ptr<assembly> assembly_ {};

        HWND window_ { nullptr };
        UINT snapshot_message_ { 0 };
        std::shared_ptr<const view_snapshot> current_view_ {};
        std::uint64_t seen_view_version_ { 0 };
        std::shared_ptr<const ui::ui_tree> current_tree_ {};
        std::uint64_t seen_tree_version_ { 0 };
        ui::interaction_snapshot current_interaction_ {};
        std::uint64_t seen_interaction_version_ { 0 };
        bool shut_down_ { false };
    };
} // namespace gitman::win32

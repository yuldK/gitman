#pragma once

#include "domain/project.h"
#include "platform/win32/gitman_driver.h"

#include "luil/win32/win32_window.h"

#include <optional>
#include <string>

namespace gitman::win32 {
    // luil UI thread에 꽂는 gitman 훅이다 (설계 L3). 예전 `win32_application.cpp`의
    // 파일·폴더 dialog, 파일 연결, 셸 실행, 창 배치·크기 보고, 파일 드롭이 여기로
    // 모인다. 창·렌더러·입력·클립보드는 luil이 맡으므로 넘어오지 않는다.
    //
    // 모든 메서드는 UI thread에서 불린다. 결과는 언제나 메시지로 logic에 돌려주며
    // view를 직접 읽는 경로는 두지 않는다.
    class gitman_delegate final : public luil::win32::window_delegate
    {
    public:
        // `startup_document`는 명령줄이 준 문서 경로다 (없으면 시작 페이지).
        gitman_delegate(gitman_driver& driver, std::optional<std::u8string> startup_document);

        void on_started(luil::win32::app_host& host) override;
        void execute_app_ui_command(luil::win32::app_host& host, const luil::app_ui_command& command) override;
        [[nodiscard]] luil::app_message make_window_metrics_message(float width, float height, float scale) override;
        [[nodiscard]] luil::app_message make_window_placement_message(const luil::win32::window_placement& placement) override;
        [[nodiscard]] bool on_file_dropped(luil::win32::app_host& host, const std::u8string& path) override;

    private:
        gitman_driver& driver_;
        std::optional<std::u8string> startup_document_ {};
    };

    // 창 배치 값 변환이다. 두 타입의 필드는 같고 정수 폭만 다르다.
    [[nodiscard]] window_placement to_gitman_placement(const luil::win32::window_placement& placement) noexcept;
    [[nodiscard]] luil::win32::window_placement to_luil_placement(const window_placement& placement) noexcept;
} // namespace gitman::win32

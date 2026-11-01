#include "application/application_options.h"
#include "platform/win32/gitman_delegate.h"
#include "platform/win32/gitman_driver.h"
#include "platform/win32/resources/resource_ids.h"
#include "platform/win32/utf8.h"
#include "platform/win32/win32_file_association.h"
#include "presentation/ui/gitman_caption.h"
#include "presentation/ui/gitman_policy.h"

#include "luil/text/fonts.h"
#include "luil/win32/win32_window.h"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace {
    void show_argument_error(const std::u8string& error)
    {
        const auto wide_error { gitman::win32::utf8_to_utf16(error) };
        if (wide_error.value.has_value())
            MessageBoxW(nullptr, wide_error.value->c_str(), L"Gitman 명령줄 오류", MB_OK | MB_ICONERROR);
    }

    // 명령행 연결 등록·제거다 (REQ-016). 창 없이 수행하고 종료 코드로 성패를
    // 알린다. 스크립트 사용을 위해 성공은 조용히 끝나고 실패만 dialog로 알린다.
    [[nodiscard]] int run_file_association_command(const bool register_association)
    {
        gitman::win32::file_association_outcome outcome {};
        if (register_association)
            outcome = gitman::win32::register_file_association(gitman::win32::current_executable_path());
        else
            outcome = gitman::win32::unregister_file_association();
        if (outcome.succeeded)
            return 0;
        for (const gitman::diagnostic& value : outcome.diagnostics)
            show_argument_error(value.message);
        return 1;
    }
} // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    // DPI 인지는 창을 만들기 전에 한 번 켠다.
    luil::win32::enable_per_monitor_dpi_awareness();
    // COM STA는 TSF(텍스트 입력)·파일 dialog·OS 파일 끌기·접근성이 요구한다.
    const luil::win32::com_sta_scope com {};

    const std::optional<std::vector<std::u8string>> arguments { luil::win32::command_line_arguments() };
    if (arguments.has_value() == false)
        return 1;

    const gitman::application_options_result parsed_options {
        gitman::parse_application_options(*arguments),
    };
    if (parsed_options.options.has_value() == false)
    {
        show_argument_error(parsed_options.error);
        return 2;
    }

    const gitman::application_options& options { *parsed_options.options };
    // 연결 등록·제거 모드는 창을 만들지 않는다.
    if (options.register_file_association || options.unregister_file_association)
        return run_file_association_command(options.register_file_association);

    // 기본 UI 글꼴 우선순위다 (설계 L3 글꼴). 설치되어 있지 않으면 luil registry가
    // 내장 기본(Segoe UI)으로 물러서므로 지금의 순서가 그대로 재현된다.
    luil::set_ui_typeface_families({ u8"Malgun Gothic" });

    // 창의 겉모습과 정책이다. 길이는 전부 논리 96 DPI 기준 px다.
    luil::win32::window_config config {};
    config.class_name = L"Gitman.Window";
    // caption 값은 한곳에서만 만든다. 비클라이언트 hit test와 tree의 caption
    // element가 같은 값을 읽어야 버튼 자리가 어긋나지 않는다 (설계 L3).
    config.caption = gitman::ui::gitman_caption();
    config.initial_width = 1280;
    config.initial_height = 720;
    config.minimum_client_width = 480;
    config.minimum_client_height = 320;
    config.resize_border_thickness = 4;
    config.resize_corner_thickness = 10;
    config.icon_resource = IDI_GITMAN;
    config.renderer = options.renderer;
    config.simulate_direct3d_failure = options.simulate_direct3d_failure;
    config.smoke_test = options.smoke_test;
    config.accept_file_drop = true;
    // 시작 실패(리소스 검증·창 생성) 상자다. 진단 글은 luil이 그대로 뒤에 붙인다.
    config.startup_error.title = u8"Gitman 시작 오류";
    config.startup_error.preface = u8"Gitman을 시작할 수 없습니다.";

    // 앱의 세 조각이다. smoke 모드에서도 만든다 — luil이 host를 조립하지 않아 logic
    // thread는 서지 않지만, driver가 드는 worker pool과 로그 writer thread는 잠깐 섰다가
    // 소멸자에서 정리된다 (한 frame만 그리는 경로라 비용은 무시할 만하다).
    gitman::win32::gitman_driver driver {};
    gitman::ui::gitman_policy policy {};
    gitman::win32::gitman_delegate delegate { driver, options.workspace_document_path };

    luil::win32::window_environment environment {};
    environment.driver = &driver;
    environment.policy = &policy;
    environment.delegate = &delegate;

    // 메시지 루프가 끝날 때까지 여기서 돈다. 반환값이 프로세스 종료 코드다.
    return luil::win32::run_application_window(config, environment);
}

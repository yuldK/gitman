#pragma once

#include <cstdint>
#include <optional>

namespace gitman {
    // UI thread에서만 실행할 수 있는 앱 명령이다 (파일·폴더 dialog, 레지스트리, 셸
    // 실행). luil은 값을 해석하지 않고 `app_ui_command { command, argument }`로
    // delegate에 그대로 넘긴다 (설계 L7). 인자가 필요한 명령은 `argument`에 경로를
    // 싣는다. 창 명령(최소화·최대화·닫기)은 luil `ui_command`라 여기 없다.
    //
    // 0은 "명령 없음"으로 비워 둔다. `parse_app_command`가 모르는 값을 거른다.
    enum class app_command : std::uint32_t
    {
        // `.version-list` 문서 열기 dialog → 절대 경로 → `open_document_intent`.
        show_open_document_dialog = 1,
        // 새 문서 dialog의 폴더 선택 둘 → `set_generate_*_intent`.
        show_generate_scan_folder_picker,
        show_generate_location_picker,
        // 탐색 등록의 스캔 폴더 선택 → `begin_discovery_intent`. `argument`는 시작
        // 폴더(문서 폴더)다.
        show_discovery_folder_picker,
        // 환경설정의 실행 파일 선택 → `set_settings_executable_intent`.
        show_git_executable_picker,
        show_svn_executable_picker,
        // `.version-list` 파일 연결 등록·제거 → 결과를 `show_notice_intent`로.
        register_file_association,
        unregister_file_association,
        // 외부 열기다. `argument`는 절대 경로다 (Windows 원형).
        open_in_vscode,
        // 탐색기로 부모 폴더를 열고 대상을 선택한다 (`explorer /select,`).
        open_in_explorer_select,
        // 탐색기로 폴더 자체를 연다 (컨텍스트 메뉴의 "저장소 열기").
        open_in_explorer_folder,
        // 기능 편집 dialog의 프로그램 찾아보기 → `set_function_editor_field_intent { program }`
        // (function-buttons-design 2.7). 환경설정의 실행 파일 선택과 같은 Win32 dialog다.
        show_function_program_picker,
    };

    inline constexpr std::uint32_t last_app_command { static_cast<std::uint32_t>(app_command::show_function_program_picker) };

    // luil이 나른 값을 되돌린다. 모르는 값(0 포함)은 nullopt다.
    [[nodiscard]] constexpr std::optional<app_command> parse_app_command(const std::uint32_t value) noexcept
    {
        if (value == 0 || value > last_app_command)
            return std::nullopt;
        return static_cast<app_command>(value);
    }
} // namespace gitman

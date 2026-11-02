#include "platform/win32/gitman_delegate.h"

#include "application/app_commands.h"
#include "application/application_options.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/utf8.h"
#include "platform/win32/win32_file_association.h"

#include "luil/text/fonts.h"

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace gitman::win32 {
    namespace {
        // logic으로 보내는 메시지 하나다. 어느 intent든 같은 상자에 담아 나른다.
        template<typename intent_type>
        void post_intent(luil::win32::app_host& host, intent_type intent)
        {
            host.post_app_message(luil::app_message { logic_message { std::move(intent) } });
        }

        // 고른 항목의 파일 시스템 경로를 UTF-8로 꺼낸다. 실패하면 빈 값이다.
        [[nodiscard]] std::optional<std::u8string> selected_path(IFileOpenDialog& dialog)
        {
            std::optional<std::u8string> chosen {};
            IShellItem* item { nullptr };
            if (SUCCEEDED(dialog.GetResult(&item)) && item != nullptr)
            {
                PWSTR path { nullptr };
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr)
                {
                    auto converted { utf16_to_utf8(path) };
                    if (converted.value.has_value())
                        chosen = { std::move(*converted.value) };
                    CoTaskMemFree(path);
                }
                item->Release();
            }
            return chosen;
        }

        [[nodiscard]] std::optional<std::u8string> choose_workspace_document(const HWND owner)
        {
            IFileOpenDialog* dialog { nullptr };
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || dialog == nullptr)
                return std::nullopt;

            std::optional<std::u8string> chosen {};
            const COMDLG_FILTERSPEC filter { L"Gitman 문서 (*.version-list)", L"*.version-list" };
            static_cast<void>(dialog->SetFileTypes(1, &filter));
            static_cast<void>(dialog->SetTitle(L".version-list 문서 열기"));
            if (SUCCEEDED(dialog->Show(owner)))
                chosen = selected_path(*dialog);
            dialog->Release();
            return chosen;
        }

        // 탐색 등록의 스캔 폴더를 고르는 Win32 폴더 dialog다. UI thread 전용이다.
        [[nodiscard]] std::optional<std::u8string> choose_discovery_folder(const HWND owner, const std::u8string& initial)
        {
            IFileOpenDialog* dialog { nullptr };
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || dialog == nullptr)
                return std::nullopt;

            std::optional<std::u8string> chosen {};
            FILEOPENDIALOGOPTIONS options { 0 };
            if (SUCCEEDED(dialog->GetOptions(&options)))
                static_cast<void>(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
            static_cast<void>(dialog->SetTitle(L"저장소를 탐색할 폴더 선택"));
            if (initial.empty() == false)
            {
                // 시작 폴더 지정 실패는 무해하다. 사용자가 dialog에서 직접 이동한다.
                if (auto converted { utf8_to_utf16(initial) }; converted.value.has_value())
                {
                    IShellItem* start { nullptr };
                    if (SUCCEEDED(SHCreateItemFromParsingName(converted.value->c_str(), nullptr, IID_PPV_ARGS(&start))) && start != nullptr)
                    {
                        static_cast<void>(dialog->SetFolder(start));
                        start->Release();
                    }
                }
            }
            if (SUCCEEDED(dialog->Show(owner)))
                chosen = selected_path(*dialog);
            dialog->Release();
            return chosen;
        }

        // 환경설정의 실행 파일을 고르는 Win32 파일 dialog다. UI thread 전용이다.
        // FOS_FILEMUSTEXIST 기본 동작으로 존재하는 파일만 돌아오므로 logic은 형식
        // 검증만 다시 한다 (stage-8-plan 5.1).
        [[nodiscard]] std::optional<std::u8string> choose_executable_file(const HWND owner, const wchar_t* const title)
        {
            IFileOpenDialog* dialog { nullptr };
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || dialog == nullptr)
                return std::nullopt;

            std::optional<std::u8string> chosen {};
            const COMDLG_FILTERSPEC filter { L"실행 파일 (*.exe)", L"*.exe" };
            static_cast<void>(dialog->SetFileTypes(1, &filter));
            static_cast<void>(dialog->SetTitle(title));
            if (SUCCEEDED(dialog->Show(owner)))
                chosen = selected_path(*dialog);
            dialog->Release();
            return chosen;
        }

        // 환경설정 dialog의 연결 등록·해제다 (REQ-016). registry 작업은 짧은
        // 로컬 I/O라 UI thread에서 곧바로 수행하고, 결과는 logic을 거쳐 앱 스타일
        // 알림 dialog로 알린다 (app-shell-design A3.2).
        void execute_file_association_command(luil::win32::app_host& host, const bool register_association)
        {
            const file_association_outcome outcome { register_association ? register_file_association(current_executable_path()) : unregister_file_association() };

            show_notice_intent notice {};
            notice.title = u8"파일 연결";
            notice.error = outcome.succeeded == false;
            if (outcome.succeeded)
                notice.lines.push_back(
                    register_association ? std::u8string { u8".version-list 문서가 이 프로그램에 연결되었습니다." } : std::u8string { u8".version-list 연결이 제거되었습니다." });
            else
            {
                notice.lines.push_back(register_association ? std::u8string { u8"연결 등록에 실패했습니다." } : std::u8string { u8"연결 제거에 실패했습니다." });
                for (const diagnostic& value : outcome.diagnostics)
                    notice.lines.push_back(value.message);
            }

            post_intent(host, std::move(notice));
        }

        // 외부 열기 요청을 shell로 실행한다 (field-feedback-design 2.3). VSCode는
        // PATH의 `code.cmd`를 사용하며 없으면 shell 오류를 무시하고 아무 일도
        // 하지 않는다. 탐색기는 대상을 선택한 채 열고, 폴더 열기(컨텍스트
        // 메뉴의 "저장소 열기")는 폴더 자체를 연다.
        void execute_open_external(const HWND owner, const app_command command, const std::u8string& absolute_path) noexcept
        {
            try
            {
                const auto wide { utf8_to_utf16(absolute_path) };
                if (wide.value.has_value() == false || wide.value->empty())
                    return;

                if (command == app_command::open_in_explorer_folder)
                {
                    static_cast<void>(ShellExecuteW(owner, L"open", wide.value->c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                    return;
                }

                if (command == app_command::open_in_explorer_select)
                {
                    std::wstring arguments { L"/select,\"" };
                    arguments += *wide.value;
                    arguments += L"\"";
                    static_cast<void>(ShellExecuteW(owner, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
                    return;
                }

                std::wstring arguments { L"\"" };
                arguments += *wide.value;
                arguments += L"\"";
                // `code.cmd`는 cmd 창을 만들므로 숨겨서 실행한다. VSCode 자체는
                // 분리된 GUI 프로세스로 뜬다.
                static_cast<void>(ShellExecuteW(owner, L"open", L"code.cmd", arguments.c_str(), nullptr, SW_HIDE));
            }
            catch (...)
            {}
        }
    } // namespace

    window_placement to_gitman_placement(const luil::win32::window_placement& placement) noexcept
    {
        window_placement converted {};
        converted.x = static_cast<std::int32_t>(placement.x);
        converted.y = static_cast<std::int32_t>(placement.y);
        converted.width = static_cast<std::int32_t>(placement.width);
        converted.height = static_cast<std::int32_t>(placement.height);
        converted.maximized = placement.maximized;
        return converted;
    }

    luil::win32::window_placement to_luil_placement(const window_placement& placement) noexcept
    {
        luil::win32::window_placement converted {};
        converted.x = static_cast<int>(placement.x);
        converted.y = static_cast<int>(placement.y);
        converted.width = static_cast<int>(placement.width);
        converted.height = static_cast<int>(placement.height);
        converted.maximized = placement.maximized;
        return converted;
    }

    gitman_delegate::gitman_delegate(gitman_driver& driver, std::optional<std::u8string> startup_document)
        : driver_ { driver }
        , startup_document_ { std::move(startup_document) }
    {}

    void gitman_delegate::on_started(luil::win32::app_host& host)
    {
        // 첫 줄이 bind다. 이 뒤에야 worker 결과의 깨우기가 host에 닿는다 (설계 L3).
        driver_.bind_host(host);
        // 앱 단위 설정 읽기 제출이다. logic thread의 `start()`가 아니라 이 메시지가
        // 시작을 연다 — relay가 host를 알기 전에 결과가 오는 창을 없앤다.
        post_intent(host, startup_intent {});
        // 글꼴 열거는 OS 호출이라 logic이 할 수 없다. luil이 DirectWrite로 한 번
        // 열거한 목록을 그대로 나른다 (설계 L3 글꼴).
        post_intent(host, font_families_event { luil::installed_font_families() });
        if (startup_document_.has_value())
            post_intent(host, open_document_intent { absolute_workspace_document_path(*startup_document_) });
    }

    void gitman_delegate::execute_app_ui_command(luil::win32::app_host& host, const luil::app_ui_command& command)
    {
        const std::optional<app_command> parsed { parse_app_command(command.command) };
        if (parsed.has_value() == false)
            return;

        // 소유 창은 활성 창이다 (luil 예제와 같다). dialog는 UI thread에서 곧바로
        // 돌고 고른 값만 메시지로 돌아간다.
        const HWND owner { GetActiveWindow() };
        switch (*parsed)
        {
        case app_command::show_open_document_dialog:
            if (std::optional<std::u8string> path { choose_workspace_document(owner) }; path.has_value())
                post_intent(host, open_document_intent { absolute_workspace_document_path(*path) });
            return;
        case app_command::show_generate_scan_folder_picker:
            // 새 문서 dialog의 폴더 선택이다 (F3). 탐색기는 Win32가 맡고 값은
            // intent로 logic에 돌아간다.
            if (std::optional<std::u8string> folder { choose_discovery_folder(owner, {}) }; folder.has_value())
                post_intent(host, set_generate_scan_root_intent { std::move(*folder) });
            return;
        case app_command::show_generate_location_picker:
            if (std::optional<std::u8string> folder { choose_discovery_folder(owner, {}) }; folder.has_value())
                post_intent(host, set_generate_location_intent { std::move(*folder) });
            return;
        case app_command::show_discovery_folder_picker:
            // 시작 폴더는 tree를 지을 때 view에서 실어 보낸 문서 폴더다.
            if (std::optional<std::u8string> folder { choose_discovery_folder(owner, command.argument) }; folder.has_value())
                post_intent(host, begin_discovery_intent { std::move(*folder) });
            return;
        case app_command::show_git_executable_picker:
            if (std::optional<std::u8string> path { choose_executable_file(owner, L"Git 실행 파일 선택") }; path.has_value())
                post_intent(host, set_settings_executable_intent { repository_kind::git, std::move(*path) });
            return;
        case app_command::show_svn_executable_picker:
            if (std::optional<std::u8string> path { choose_executable_file(owner, L"SVN 실행 파일 선택") }; path.has_value())
                post_intent(host, set_settings_executable_intent { repository_kind::subversion, std::move(*path) });
            return;
        case app_command::show_function_program_picker:
            // 고른 경로만 logic으로 돌아간다. 파일을 읽거나 실행하지 않는다 — 검증과 치환은 logic이 Apply에서 한다.
            if (std::optional<std::u8string> path { choose_executable_file(owner, L"기능 프로그램 선택") }; path.has_value())
                post_intent(host, set_function_editor_field_intent { function_editor_field::program, std::move(*path) });
            return;
        case app_command::register_file_association:
        case app_command::unregister_file_association:
            execute_file_association_command(host, *parsed == app_command::register_file_association);
            return;
        case app_command::open_in_vscode:
        case app_command::open_in_explorer_select:
        case app_command::open_in_explorer_folder:
            execute_open_external(owner, *parsed, command.argument);
            // 메뉴 항목의 외부 열기는 예전부터 "닫고 실행"이었다 (설계 D3-A).
            // popup의 select는 앱 명령 하나만 내므로 닫기를 여기서 잇는다. 메뉴가
            // 없을 때(로컬 변경 dialog의 아이콘)는 logic이 무시하므로 무해하다.
            post_intent(host, close_context_menu_intent {});
            return;
        }
    }

    luil::app_message gitman_delegate::make_window_metrics_message(const float width, const float height, const float scale)
    {
        return luil::app_message { logic_message { window_metrics_intent { width, height, scale } } };
    }

    luil::app_message gitman_delegate::make_window_placement_message(const luil::win32::window_placement& placement)
    {
        // 크기가 양수가 아니면 복원에 쓸 수 없다. 빈 메시지는 게시되지 않는다.
        const window_placement converted { to_gitman_placement(placement) };
        if (converted.valid() == false)
            return {};
        return luil::app_message { logic_message { window_placement_intent { converted } } };
    }

    bool gitman_delegate::on_file_dropped(luil::win32::app_host& host, const std::u8string& path)
    {
        // 받는 element가 없는 드롭의 물러섬이다. 문서만 받고 나머지는 그대로 흘린다.
        if (has_workspace_document_extension(path) == false)
            return false;
        post_intent(host, open_document_intent { absolute_workspace_document_path(path) });
        return true;
    }
} // namespace gitman::win32

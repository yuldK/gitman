#include "platform/win32/win32_application.h"

#include "platform/win32/caption_layout.h"
#include "platform/win32/embedded_assets.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/resources/resource_ids.h"
#include "platform/win32/skia_renderer.h"
#include "platform/win32/utf8.h"
#include "platform/win32/version_list_generation_dialog.h"
#include "platform/win32/win32_app_runtime.h"
#include "platform/win32/win32_clipboard.h"
#include "platform/win32/win32_file_association.h"

#include "domain/path_syntax.h"
#include "presentation/log_presentation.h"
#include "presentation/ui/caption_element.h"
#include "presentation/ui/ui_events.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace gitman::win32 {
    namespace {
        constexpr wchar_t window_class_name[] = L"Gitman.Stage1.Window";
        constexpr wchar_t window_title[] = L"Gitman";
        constexpr int direct3d_unavailable_exit_code { 77 };
        // ADR-005의 wake 신호다. view slot과 interaction slot의 signal callback이
        // 게시한다.
        constexpr UINT snapshot_wake_message { WM_APP + 1 };
        // input thread가 요청한 `ui::ui_command`를 UI thread에서 실행하는 신호다.
        // wparam이 command 값이다.
        constexpr UINT ui_command_request_message { WM_APP + 2 };
        // input thread가 큐에 넣은 외부 열기(VSCode·탐색기) 요청의 신호다 (2.3).
        constexpr UINT open_external_request_message { WM_APP + 3 };
        // tooltip 지연이 끝나는 시점에 한 번 다시 그리기 위한 timer다.
        constexpr UINT_PTR tooltip_timer_id { 1 };
        // 초점을 받은 텍스트 박스의 caret이 다음에 뒤집히는 시점에 다시 그리기 위한
        // timer다. 매 render가 남은 시간을 다시 계산해 걸므로 반복 timer가 아니어도
        // 깜빡임이 이어진다.
        constexpr UINT_PTR caret_timer_id { 2 };

        // `.version-list` 문서를 고르는 Win32 파일 dialog다. UI thread 전용이다.
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
            {
                IShellItem* item { nullptr };
                if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr)
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
            }
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
            {
                IShellItem* item { nullptr };
                if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr)
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
            }
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
            {
                IShellItem* item { nullptr };
                if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr)
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
            }
            dialog->Release();
            return chosen;
        }
        // ─────────────────────────────────────────────────────────────────────
        // 창 크기 정책. 값만 바꾸면 되도록 한곳에 모아 둔다 (논리 96 DPI 기준 px).
        //
        //  - minimum_window_width/height: 사용자가 이보다 작게 줄일 수 없는 client
        //    영역 크기다. WM_GETMINMAXINFO가 강제하고, 문서에서 복원한 배치도 이
        //    값보다 작으면 적용하지 않는다.
        //  - resize_border_thickness: 창 가장자리에서 크기 조절로 잡히는 두께다.
        //    좁을수록 가장자리에 붙은 UI(스크롤 막대)를 잡기 쉽다.
        //  - resize_corner_thickness: 대각선 조절이 걸리는 모서리 두께다. 가장자리
        //    보다 넓게 두어야 모서리를 잡기 쉽다.
        // ─────────────────────────────────────────────────────────────────────
        constexpr int minimum_window_width { 480 };
        constexpr int minimum_window_height { 320 };
        constexpr int resize_border_thickness { 4 };
        constexpr int resize_corner_thickness { 10 };

        constexpr DWORD initial_window_style { WS_OVERLAPPEDWINDOW };
        constexpr DWORD retained_window_styles {
            WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        };
        constexpr DWORD custom_window_style {
            initial_window_style & ~static_cast<DWORD>(WS_CAPTION),
        };

        static_assert((custom_window_style & WS_CAPTION) == 0);
        static_assert((custom_window_style & retained_window_styles) == retained_window_styles);

        class application_window
        {
        public:
            application_window(const HINSTANCE instance, application_options options) noexcept
                : instance_(instance)
                , options_(options)
            {}

            [[nodiscard]] bool create(std::u8string& error)
            {
                WNDCLASSEXW window_class {};
                window_class.cbSize = sizeof(window_class);
                window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
                window_class.lpfnWndProc = &application_window::static_window_procedure;
                window_class.hInstance = instance_;
                window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_GITMAN));
                window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                window_class.lpszClassName = window_class_name;
                window_class.hIconSm = window_class.hIcon;
                if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                {
                    error = u8"Failed to register the Win32 window class.";
                    return false;
                }

                window_ = CreateWindowExW(
                    WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, window_class_name, window_title, initial_window_style, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, instance_, this);

                if (window_ == nullptr)
                {
                    error = u8"Failed to create the Win32 window.";
                    return false;
                }

                if (remove_system_caption(error) == false)
                {
                    DestroyWindow(window_);
                    window_ = nullptr;
                    return false;
                }
                apply_dwm_frame();
                dpi_ = GetDpiForWindow(window_);
                renderer_ = renderer_host::create(window_, options_.renderer, options_.simulate_direct3d_failure, error);
                if (renderer_ == nullptr)
                    return false;

                // smoke test는 스레드 없이 한 frame만 그린다. 실제 앱 모드에서만
                // runtime(스레드 4종과 채널)을 조립한다.
                if (options_.smoke_test == false)
                    runtime_ = std::make_unique<app_runtime>(window_, snapshot_wake_message, ui_command_request_message, open_external_request_message);
                return true;
            }

            [[nodiscard]] int run()
            {
                if (options_.smoke_test)
                {
                    std::u8string error {};
                    return render_one_frame(error) ? 0 : 1;
                }

                post_window_metrics();
                // 인자 경로는 절대 경로로 펴서 보낸다. 상대 경로 그대로 열면 문서
                // 기준 상대 저장소 경로가 어긋난다 (app-shell-design A2.2).
                if (options_.workspace_document_path.has_value())
                    runtime_->post_logic(logic_message { open_document_intent { absolute_workspace_document_path(*options_.workspace_document_path) } });

                ShowWindow(window_, SW_SHOWDEFAULT);
                UpdateWindow(window_);
                MSG message {};
                while (GetMessageW(&message, nullptr, 0, 0) > 0)
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                return static_cast<int>(message.wParam);
            }

        private:
            static LRESULT CALLBACK static_window_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
            {
                application_window* self {
                    reinterpret_cast<application_window*>(GetWindowLongPtrW(window, GWLP_USERDATA)),
                };

                if (message == WM_NCCREATE)
                {
                    const auto* creation { reinterpret_cast<const CREATESTRUCTW*>(long_parameter) };
                    self = static_cast<application_window*>(creation->lpCreateParams);
                    self->window_ = window;
                    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }

                if (self != nullptr)
                    return self->window_procedure(message, word_parameter, long_parameter);
                return DefWindowProcW(window, message, word_parameter, long_parameter);
            }

            LRESULT window_procedure(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
            {
                switch (message)
                {
                case snapshot_wake_message:
                    apply_requested_window_placement();
                    InvalidateRect(window_, nullptr, FALSE);
                    return 0;
                case ui_command_request_message:
                    execute_ui_command(static_cast<ui::ui_command>(word_parameter));
                    return 0;
                case open_external_request_message:
                    if (runtime_ != nullptr)
                        for (const ui::open_external_request& request : runtime_->take_open_external_requests())
                            execute_open_external(request);
                    return 0;
                case WM_DROPFILES:
                    open_dropped_workspace_document(reinterpret_cast<HDROP>(word_parameter));
                    return 0;
                case WM_TIMER:
                    if (word_parameter == tooltip_timer_id || word_parameter == caret_timer_id)
                    {
                        KillTimer(window_, word_parameter);
                        InvalidateRect(window_, nullptr, FALSE);
                        return 0;
                    }
                    break;
                case WM_CLOSE:
                    // ADR-005 7.3: 스레드를 모두 정리한 뒤에 창을 파괴한다. 창 배치는
                    // 종료 신호보다 먼저 게시해야 같은 채널의 순서로 반영된다.
                    if (runtime_ != nullptr)
                    {
                        post_window_placement();
                        runtime_->shutdown();
                        runtime_.reset();
                    }
                    break;
                case WM_LBUTTONDOWN:
                case WM_LBUTTONDBLCLK:
                    // CS_DBLCLKS가 두 번째 누름을 DBLCLK로 바꾸므로 같은 누름으로
                    // 되돌린다. 더블 클릭 판정은 interaction controller가 한다.
                    if (runtime_ != nullptr)
                    {
                        SetCapture(window_);
                        post_pointer_pressed(long_parameter, ui::pointer_button::left);
                        return 0;
                    }
                    break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONDBLCLK:
                    if (runtime_ != nullptr)
                    {
                        SetCapture(window_);
                        post_pointer_pressed(long_parameter, ui::pointer_button::right);
                        return 0;
                    }
                    break;
                case WM_LBUTTONUP:
                    if (runtime_ != nullptr)
                    {
                        ReleaseCapture();
                        post_pointer_released(long_parameter, ui::pointer_button::left);
                        return 0;
                    }
                    break;
                case WM_RBUTTONUP:
                    if (runtime_ != nullptr)
                    {
                        ReleaseCapture();
                        post_pointer_released(long_parameter, ui::pointer_button::right);
                        return 0;
                    }
                    break;
                case WM_MOUSEWHEEL:
                    if (runtime_ != nullptr)
                    {
                        POINT client_point { GET_X_LPARAM(long_parameter), GET_Y_LPARAM(long_parameter) };
                        ScreenToClient(window_, &client_point);
                        runtime_->post_raw_input(ui::mouse_wheel_event {
                            static_cast<float>(client_point.x), static_cast<float>(client_point.y), static_cast<float>(GET_WHEEL_DELTA_WPARAM(word_parameter)), std::chrono::steady_clock::now() });
                        return 0;
                    }
                    break;
                case WM_KEYDOWN:
                    if (runtime_ != nullptr)
                    {
                        const ui::key_code key { key_from_virtual(word_parameter) };
                        if (key != ui::key_code::none)
                        {
                            runtime_->post_raw_input(ui::key_pressed_event { key, (GetKeyState(VK_CONTROL) & 0x8000) != 0 });
                            return 0;
                        }
                    }
                    break;
                case WM_CHAR:
                    // 텍스트 박스 입력이다 (field-feedback-design 1.3). surrogate 쌍은
                    // 현재 입력 대상(숫자)에 없으므로 BMP 문자만 넘긴다.
                    if (runtime_ != nullptr && (word_parameter < 0xD800u || word_parameter > 0xDFFFu))
                    {
                        runtime_->post_raw_input(ui::character_typed_event { static_cast<char32_t>(word_parameter) });
                        return 0;
                    }
                    break;
                case WM_NCCALCSIZE:
                    if (word_parameter != 0)
                        return 0;
                    break;
                case WM_NCHITTEST:
                    return non_client_hit_test(long_parameter);
                case WM_NCMOUSEMOVE:
                    track_non_client_mouse_leave();
                    update_caption_hover(caption_hover_from_hit(word_parameter));
                    break;
                case WM_NCMOUSELEAVE:
                    tracking_non_client_mouse_ = false;
                    update_caption_hover(ui::caption_button_hover::none);
                    break;
                case WM_NCLBUTTONDOWN:
                    if (is_caption_button(word_parameter))
                        return 0;
                    break;
                case WM_NCLBUTTONUP:
                    if (execute_caption_button(word_parameter))
                        return 0;
                    break;
                case WM_MOUSEMOVE:
                    update_caption_hover(ui::caption_button_hover::none);
                    if (runtime_ != nullptr)
                    {
                        track_client_mouse_leave();
                        runtime_->post_raw_input(
                            ui::pointer_moved_event { static_cast<float>(GET_X_LPARAM(long_parameter)), static_cast<float>(GET_Y_LPARAM(long_parameter)), std::chrono::steady_clock::now() });
                    }
                    break;
                case WM_MOUSELEAVE:
                    // hover 강조가 창 밖에서 남지 않게 한다.
                    tracking_client_mouse_ = false;
                    if (runtime_ != nullptr)
                        runtime_->post_raw_input(ui::pointer_left_event {});
                    break;
                case WM_ACTIVATE:
                    if (LOWORD(word_parameter) == WA_INACTIVE)
                        update_caption_hover(ui::caption_button_hover::none);
                    break;
                case WM_GETMINMAXINFO:
                    update_size_limits(reinterpret_cast<MINMAXINFO*>(long_parameter));
                    return 0;
                case WM_NCRBUTTONUP:
                    if (word_parameter == HTCAPTION || word_parameter == HTSYSMENU)
                    {
                        show_system_menu({ GET_X_LPARAM(long_parameter), GET_Y_LPARAM(long_parameter) });
                        return 0;
                    }
                    break;
                case WM_SYSKEYDOWN:
                    if (word_parameter == VK_SPACE && (long_parameter & (1LL << 29)) != 0)
                    {
                        RECT window_rectangle {};
                        GetWindowRect(window_, &window_rectangle);
                        show_system_menu({ window_rectangle.left, window_rectangle.top + scale_for_dpi(ui::default_caption_ui_metrics.height) });
                        return 0;
                    }
                    break;
                case WM_SIZE:
                    update_caption_hover(ui::caption_button_hover::none);
                    if (renderer_ != nullptr && word_parameter != SIZE_MINIMIZED)
                    {
                        std::u8string error {};
                        if (renderer_->resize(LOWORD(long_parameter), HIWORD(long_parameter), error) == false)
                        {
                            report_runtime_error(error);
                            PostQuitMessage(1);
                        }
                        post_window_metrics();
                        InvalidateRect(window_, nullptr, FALSE);
                    }
                    return 0;
                case WM_DPICHANGED: {
                    dpi_ = HIWORD(word_parameter);
                    const auto* suggested_rectangle { reinterpret_cast<const RECT*>(long_parameter) };
                    SetWindowPos(window_, nullptr, suggested_rectangle->left, suggested_rectangle->top, suggested_rectangle->right - suggested_rectangle->left,
                        suggested_rectangle->bottom - suggested_rectangle->top, SWP_NOACTIVATE | SWP_NOZORDER);
                    post_window_metrics();
                    InvalidateRect(window_, nullptr, FALSE);
                    return 0;
                }
                case WM_SETTINGCHANGE:
                case WM_THEMECHANGED:
                    InvalidateRect(window_, nullptr, FALSE);
                    return 0;

                case WM_DWMCOMPOSITIONCHANGED:
                    apply_dwm_frame();
                    return 0;

                case WM_ERASEBKGND:
                    return 1;

                case WM_PAINT: {
                    PAINTSTRUCT paint {};
                    BeginPaint(window_, &paint);
                    std::u8string error {};
                    const bool rendered { renderer_ != nullptr && render_one_frame(error) };
                    EndPaint(window_, &paint);
                    if (rendered == false)
                    {
                        report_runtime_error(error);
                        PostQuitMessage(1);
                    }
                    return 0;
                }
                case WM_DESTROY:
                    // WM_CLOSE를 거치지 않은 파괴 경로에서도 스레드를 먼저 정리한다.
                    if (runtime_ != nullptr)
                    {
                        runtime_->shutdown();
                        runtime_.reset();
                    }
                    renderer_.reset();
                    PostQuitMessage(0);
                    return 0;
                default:
                    break;
                }
                return DefWindowProcW(window_, message, word_parameter, long_parameter);
            }

            [[nodiscard]] LRESULT non_client_hit_test(const LPARAM long_parameter) const
            {
                LRESULT dwm_result { 0 };
                if (DwmDefWindowProc(window_, WM_NCHITTEST, 0, long_parameter, &dwm_result))
                    return dwm_result;

                RECT window_rectangle {};
                GetWindowRect(window_, &window_rectangle);
                const int x { GET_X_LPARAM(long_parameter) - window_rectangle.left };
                const int y { GET_Y_LPARAM(long_parameter) - window_rectangle.top };
                const int width { window_rectangle.right - window_rectangle.left };
                const int height { window_rectangle.bottom - window_rectangle.top };

                if (IsZoomed(window_) == FALSE)
                {
                    // 시스템 기본 테두리(보통 8px)는 가장자리에 붙은 스크롤 막대를
                    // 잡기 어렵게 한다. 조절 두께를 좁히고 모서리만 넉넉히 둔다.
                    const int border { std::max(1, scale_for_dpi(resize_border_thickness)) };
                    const int corner { std::max(border, scale_for_dpi(resize_corner_thickness)) };
                    const bool corner_left { x < corner };
                    const bool corner_right { x >= width - corner };
                    const bool corner_top { y < corner };
                    const bool corner_bottom { y >= height - corner };
                    if (corner_top && corner_left)
                        return HTTOPLEFT;
                    if (corner_top && corner_right)
                        return HTTOPRIGHT;
                    if (corner_bottom && corner_left)
                        return HTBOTTOMLEFT;
                    if (corner_bottom && corner_right)
                        return HTBOTTOMRIGHT;
                    if (x < border)
                        return HTLEFT;
                    if (x >= width - border)
                        return HTRIGHT;
                    if (y < border)
                        return HTTOP;
                    if (y >= height - border)
                        return HTBOTTOM;
                }

                if (y >= 0 && y < scale_for_dpi(ui::default_caption_ui_metrics.height) && x >= 0 && x < scale_for_dpi(ui::default_caption_ui_metrics.application_icon_slot_width))
                    return HTSYSMENU;
                const caption_layout layout { make_caption_layout(width, dpi_) };
                switch (hit_test_caption(layout, x, y))
                {
                case caption_hit::drag:
                    return HTCAPTION;
                case caption_hit::minimize:
                    return HTMINBUTTON;
                case caption_hit::maximize:
                    return HTMAXBUTTON;
                case caption_hit::close:
                    return HTCLOSE;
                case caption_hit::client:
                    return HTCLIENT;
                }
                return HTCLIENT;
            }

            [[nodiscard]] static bool is_caption_button(const WPARAM hit) noexcept
            {
                return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE;
            }

            [[nodiscard]] static ui::caption_button_hover caption_hover_from_hit(const WPARAM hit) noexcept
            {
                switch (hit)
                {
                case HTMINBUTTON:
                    return ui::caption_button_hover::minimize;
                case HTMAXBUTTON:
                    return ui::caption_button_hover::maximize;
                case HTCLOSE:
                    return ui::caption_button_hover::close;
                default:
                    return ui::caption_button_hover::none;
                }
            }

            void track_non_client_mouse_leave() noexcept
            {
                if (tracking_non_client_mouse_)
                    return;
                TRACKMOUSEEVENT tracking {
                    static_cast<DWORD>(sizeof(TRACKMOUSEEVENT)),
                    TME_LEAVE | TME_NONCLIENT,
                    window_,
                    HOVER_DEFAULT,
                };
                tracking_non_client_mouse_ = TrackMouseEvent(&tracking) != FALSE;
            }

            void update_caption_hover(const ui::caption_button_hover hover) noexcept
            {
                if (hovered_caption_button_ == hover)
                    return;
                hovered_caption_button_ = hover;
                // caption tooltip 지연 판정의 기준 시각이다.
                nc_hover_since_ = std::chrono::steady_clock::now();
                InvalidateRect(window_, nullptr, FALSE);
            }

            void post_pointer_pressed(const LPARAM long_parameter, const ui::pointer_button button) const noexcept
            {
                runtime_->post_raw_input(
                    ui::pointer_pressed_event { static_cast<float>(GET_X_LPARAM(long_parameter)), static_cast<float>(GET_Y_LPARAM(long_parameter)), button, std::chrono::steady_clock::now() });
            }

            void post_pointer_released(const LPARAM long_parameter, const ui::pointer_button button) const noexcept
            {
                runtime_->post_raw_input(
                    ui::pointer_released_event { static_cast<float>(GET_X_LPARAM(long_parameter)), static_cast<float>(GET_Y_LPARAM(long_parameter)), button, std::chrono::steady_clock::now() });
            }

            void track_client_mouse_leave() noexcept
            {
                if (tracking_client_mouse_)
                    return;
                TRACKMOUSEEVENT tracking {
                    static_cast<DWORD>(sizeof(TRACKMOUSEEVENT)),
                    TME_LEAVE,
                    window_,
                    HOVER_DEFAULT,
                };
                tracking_client_mouse_ = TrackMouseEvent(&tracking) != FALSE;
            }

            void open_dropped_workspace_document(const HDROP dropped_files) noexcept
            {
                struct drop_finish_guard
                {
                    HDROP value { nullptr };

                    ~drop_finish_guard()
                    {
                        DragFinish(value);
                    }
                };

                const drop_finish_guard finish { dropped_files };
                if (runtime_ == nullptr)
                    return;

                try
                {
                    const UINT file_count { DragQueryFileW(dropped_files, 0xFFFFFFFFU, nullptr, 0) };
                    for (UINT index = 0; index < file_count; ++index)
                    {
                        const UINT path_length { DragQueryFileW(dropped_files, index, nullptr, 0) };
                        if (path_length == 0)
                            continue;

                        std::wstring path(static_cast<std::size_t>(path_length) + 1, L'\0');
                        if (DragQueryFileW(dropped_files, index, path.data(), path_length + 1) == 0)
                            continue;
                        path.resize(path_length);

                        auto converted { utf16_to_utf8(path) };
                        if (converted.value.has_value() == false || has_workspace_document_extension(*converted.value) == false)
                            continue;

                        runtime_->post_logic(logic_message { open_document_intent { absolute_workspace_document_path(*converted.value) } });
                        return;
                    }
                }
                catch (...)
                {
                    // WndProc 경계를 예외가 넘어가지 않게 한다. 메모리가 부족하면 drop을
                    // 무시하고 현재 문서를 그대로 유지한다.
                }
            }

            void execute_ui_command(const ui::ui_command command)
            {
                switch (command)
                {
                case ui::ui_command::show_open_document_dialog:
                    if (runtime_ != nullptr)
                    {
                        if (const std::optional<std::u8string> path { choose_workspace_document(window_) }; path.has_value())
                            runtime_->post_logic(logic_message { open_document_intent { absolute_workspace_document_path(*path) } });
                    }
                    return;
                case ui::ui_command::show_generate_document_dialog:
                    if (runtime_ != nullptr)
                    {
                        if (const std::optional<generate_document_intent> intent { show_version_list_generation_dialog(window_) }; intent.has_value())
                            runtime_->post_logic(logic_message { *intent });
                    }
                    return;
                case ui::ui_command::register_file_association:
                case ui::ui_command::unregister_file_association:
                    execute_file_association_command(command == ui::ui_command::register_file_association);
                    return;
                case ui::ui_command::show_discovery_folder_picker:
                    if (runtime_ != nullptr)
                    {
                        // 시작 폴더는 현재 문서가 있는 폴더다. 없으면 기본 위치다.
                        std::u8string initial {};
                        if (const std::shared_ptr<const view_snapshot> view { runtime_->acquire_view() }; view != nullptr)
                            initial = std::u8string { windows_parent_directory(view->document_path) };
                        if (std::optional<std::u8string> folder { choose_discovery_folder(window_, initial) }; folder.has_value())
                            runtime_->post_logic(logic_message { begin_discovery_intent { std::move(*folder) } });
                    }
                    return;
                case ui::ui_command::show_git_executable_picker:
                    if (runtime_ != nullptr)
                    {
                        if (std::optional<std::u8string> path { choose_executable_file(window_, L"Git 실행 파일 선택") }; path.has_value())
                            runtime_->post_logic(logic_message { set_settings_executable_intent { repository_kind::git, std::move(*path) } });
                    }
                    return;
                case ui::ui_command::show_svn_executable_picker:
                    if (runtime_ != nullptr)
                    {
                        if (std::optional<std::u8string> path { choose_executable_file(window_, L"SVN 실행 파일 선택") }; path.has_value())
                            runtime_->post_logic(logic_message { set_settings_executable_intent { repository_kind::subversion, std::move(*path) } });
                    }
                    return;
                case ui::ui_command::copy_selected_log:
                    if (runtime_ != nullptr)
                    {
                        // 표시 중인(필터 적용 후) 로그만 복사한다 (stage-7-plan 4.3).
                        const std::shared_ptr<const view_snapshot> view { runtime_->acquire_view() };
                        if (view != nullptr && view->log.has_value())
                            static_cast<void>(copy_text_to_clipboard(window_, format_log_copy_text(*view->log)));
                    }
                    return;
                case ui::ui_command::window_minimize:
                    PostMessageW(window_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                    return;
                case ui::ui_command::window_toggle_maximize:
                    PostMessageW(window_, WM_SYSCOMMAND, IsZoomed(window_) ? SC_RESTORE : SC_MAXIMIZE, 0);
                    return;
                case ui::ui_command::window_close:
                    PostMessageW(window_, WM_CLOSE, 0, 0);
                    return;
                }
            }

            // 환경설정 dialog의 연결 등록·해제다 (REQ-016). registry 작업은 짧은
            // 로컬 I/O라 UI thread에서 곧바로 수행하고, 결과는 logic을 거쳐 앱 스타일
            // 알림 dialog로 알린다 (app-shell-design A3.2).
            void execute_file_association_command(const bool register_association)
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

                if (runtime_ != nullptr)
                    runtime_->post_logic(logic_message { std::move(notice) });
            }

            // 비클라이언트 클릭을 caption 버튼 element에 등록된 액션으로 실행한다.
            // tree가 아직 없으면(runtime 없음, 첫 snapshot 이전) 같은 의미의 창
            // 명령을 직접 실행한다.
            [[nodiscard]] bool execute_caption_button(const WPARAM hit)
            {
                const ui::ui_element_id id { ui::caption_button_element_id(caption_hover_from_hit(hit)) };
                if (id == ui::ui_element_id {})
                    return false;

                if (runtime_ != nullptr)
                {
                    if (const std::shared_ptr<const ui::ui_tree> tree { runtime_->acquire_ui_tree() }; tree != nullptr)
                    {
                        if (const ui::ui_element* const button { tree->find(id) }; button != nullptr)
                        {
                            if (const ui::ui_action* const action { button->action(ui::ui_trigger::left_click) }; action != nullptr)
                            {
                                const ui::rect_f bounds { button->bounds() };
                                for (const ui::input_action& result : (*action)(ui::ui_action_context { id, bounds.x, bounds.y, false }))
                                    if (const auto* const command { std::get_if<ui::ui_command>(&result) }; command != nullptr)
                                        execute_ui_command(*command);
                                return true;
                            }
                        }
                    }
                }

                switch (hit)
                {
                case HTMINBUTTON:
                    execute_ui_command(ui::ui_command::window_minimize);
                    return true;
                case HTMAXBUTTON:
                    execute_ui_command(ui::ui_command::window_toggle_maximize);
                    return true;
                case HTCLOSE:
                    execute_ui_command(ui::ui_command::window_close);
                    return true;
                default:
                    return false;
                }
            }

            // 최소 크기와 최대화 크기를 함께 정한다. 최소 크기는 client 기준 값을
            // 창 크기로 바꿔 넣는다 (frame 두께 포함).
            void update_size_limits(MINMAXINFO* information) const noexcept
            {
                RECT minimum {
                    0,
                    0,
                    scale_for_dpi(minimum_window_width),
                    scale_for_dpi(minimum_window_height),
                };
                if (AdjustWindowRectExForDpi(&minimum, custom_window_style, FALSE, WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, dpi_) != FALSE)
                {
                    information->ptMinTrackSize.x = minimum.right - minimum.left;
                    information->ptMinTrackSize.y = minimum.bottom - minimum.top;
                }
                else
                {
                    information->ptMinTrackSize.x = scale_for_dpi(minimum_window_width);
                    information->ptMinTrackSize.y = scale_for_dpi(minimum_window_height);
                }

                const HMONITOR monitor { MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST) };
                MONITORINFO monitor_information {};
                monitor_information.cbSize = sizeof(monitor_information);
                if (GetMonitorInfoW(monitor, &monitor_information) == FALSE)
                    return;
                const RECT work { monitor_information.rcWork };
                const RECT monitor_bounds { monitor_information.rcMonitor };
                information->ptMaxPosition.x = work.left - monitor_bounds.left;
                information->ptMaxPosition.y = work.top - monitor_bounds.top;
                information->ptMaxSize.x = work.right - work.left;
                information->ptMaxSize.y = work.bottom - work.top;
            }

            void show_system_menu(const POINT position) const
            {
                const HMENU menu { GetSystemMenu(window_, FALSE) };
                if (menu == nullptr)
                    return;

                const auto command { static_cast<WPARAM>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0, window_, nullptr)) };

                if (command != 0)
                    PostMessageW(window_, WM_SYSCOMMAND, command, 0);
            }

            void apply_dwm_frame() const noexcept
            {
                const MARGINS margins { 0, 0, 1, 0 };
                DwmExtendFrameIntoClientArea(window_, &margins);
            }

            [[nodiscard]] bool remove_system_caption(std::u8string& error) const
            {
                const LONG_PTR current_style { GetWindowLongPtrW(window_, GWL_STYLE) };
                SetLastError(ERROR_SUCCESS);
                const LONG_PTR previous_style {
                    SetWindowLongPtrW(window_, GWL_STYLE, current_style & ~static_cast<LONG_PTR>(WS_CAPTION)),
                };

                if (previous_style == 0 && GetLastError() != ERROR_SUCCESS)
                {
                    error = u8"Failed to remove the Win32 system caption.";
                    return false;
                }

                if (SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
                {
                    error = u8"Failed to recalculate the Win32 custom frame.";
                    return false;
                }
                return true;
            }

            [[nodiscard]] static ui::key_code key_from_virtual(const WPARAM virtual_key) noexcept
            {
                switch (virtual_key)
                {
                case VK_UP:
                    return ui::key_code::arrow_up;
                case VK_DOWN:
                    return ui::key_code::arrow_down;
                case VK_RETURN:
                    return ui::key_code::enter;
                case VK_F5:
                    return ui::key_code::f5;
                case VK_ESCAPE:
                    return ui::key_code::escape;
                default:
                    return ui::key_code::none;
                }
            }

            // 종료 직전의 창 배치를 logic에 알린다. 최대화·최소화 상태에서도
            // `rcNormalPosition`이 복원 크기를 담으므로 그대로 저장한다.
            void post_window_placement() const noexcept
            {
                WINDOWPLACEMENT placement {};
                placement.length = sizeof(placement);
                if (GetWindowPlacement(window_, &placement) == FALSE)
                    return;

                window_placement_intent intent {};
                intent.placement.x = placement.rcNormalPosition.left;
                intent.placement.y = placement.rcNormalPosition.top;
                intent.placement.width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
                intent.placement.height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
                intent.placement.maximized = IsZoomed(window_) != FALSE;
                if (intent.placement.valid() == false)
                    return;
                runtime_->post_logic(logic_message { intent });
            }

            // 문서가 담고 있던 배치를 한 번만 적용한다. snapshot마다 창을 옮기지
            // 않도록 게시 번호가 바뀐 경우에만 움직인다.
            void apply_requested_window_placement()
            {
                if (runtime_ == nullptr)
                    return;
                const std::shared_ptr<const view_snapshot> view { runtime_->acquire_view() };
                if (view == nullptr || view->window_placement_revision == applied_window_placement_revision_)
                    return;

                applied_window_placement_revision_ = view->window_placement_revision;
                if (view->window_placement_request.has_value() == false)
                    return;

                const window_placement& requested { *view->window_placement_request };
                // 너무 작은 값은 창을 사실상 못 쓰게 만든다. 저장이 깨진 경우의 방어다.
                if (requested.valid() == false || requested.width < scale_for_dpi(minimum_window_width) || requested.height < scale_for_dpi(minimum_window_height))
                    return;

                RECT bounds {
                    requested.x,
                    requested.y,
                    requested.x + requested.width,
                    requested.y + requested.height,
                };
                // 모니터 구성이 바뀌어 저장된 위치가 화면 밖이면 크기만 적용한다.
                if (MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL) == nullptr)
                {
                    RECT current {};
                    if (GetWindowRect(window_, &current) == FALSE)
                        return;
                    bounds = { current.left, current.top, current.left + requested.width, current.top + requested.height };
                }

                WINDOWPLACEMENT placement {};
                placement.length = sizeof(placement);
                if (GetWindowPlacement(window_, &placement) == FALSE)
                    return;
                placement.rcNormalPosition = bounds;
                placement.showCmd = requested.maximized ? static_cast<UINT>(SW_SHOWMAXIMIZED) : static_cast<UINT>(SW_SHOWNORMAL);
                static_cast<void>(SetWindowPlacement(window_, &placement));
            }

            void post_window_metrics() const noexcept
            {
                if (runtime_ == nullptr)
                    return;
                RECT client_rectangle {};
                if (GetClientRect(window_, &client_rectangle) == FALSE)
                    return;
                window_metrics_intent metrics {};
                metrics.width = static_cast<float>(client_rectangle.right - client_rectangle.left);
                metrics.height = static_cast<float>(client_rectangle.bottom - client_rectangle.top);
                metrics.scale = static_cast<float>(dpi_) / 96.0f;
                runtime_->post_logic(logic_message { metrics });
            }

            [[nodiscard]] bool render_one_frame(std::u8string& error)
            {
                RECT client_rectangle {};
                if (GetClientRect(window_, &client_rectangle) == FALSE)
                {
                    error = u8"Failed to read the client area before rendering.";
                    return false;
                }

                HIGHCONTRASTW high_contrast {};
                high_contrast.cbSize = sizeof(high_contrast);
                const bool high_contrast_enabled = FALSE != SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0) && (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;

                smoke_view_state state {};
                state.width = std::max(1L, client_rectangle.right - client_rectangle.left);
                state.height = std::max(1L, client_rectangle.bottom - client_rectangle.top);
                state.dpi_scale = static_cast<float>(dpi_) / 96.0F;
                state.theme = high_contrast_enabled ? color_theme::high_contrast : color_theme::dark;
                state.maximized = IsZoomed(window_) != FALSE;

                // 앱 모드에서는 logic이 게시한 tree와 input thread의 상호작용 상태로
                // 그린다. tree는 shared_ptr이 이 호출 동안 수명을 보장한다.
                std::shared_ptr<const ui::ui_tree> tree {};
                if (runtime_ != nullptr)
                {
                    tree = runtime_->acquire_ui_tree();
                    state.interaction = runtime_->acquire_interaction();
                    state.application_tree = tree.get();
                }

                // caption 버튼의 hover는 비클라이언트 메시지로만 도착하므로 UI thread
                // 추적 값을 상호작용 상태에 합친다.
                if (hovered_caption_button_ != ui::caption_button_hover::none)
                {
                    state.interaction.hovered = ui::caption_button_element_id(hovered_caption_button_);
                    state.interaction.hover_started_at = nc_hover_since_;
                }

                schedule_tooltip_repaint(tree.get(), state.interaction);
                schedule_caret_repaint(state.interaction);
                return renderer_->render(state, error);
            }

            // 외부 열기 요청을 shell로 실행한다 (field-feedback-design 2.3). VSCode는
            // PATH의 `code.cmd`를 사용하며 없으면 shell 오류를 무시하고 아무 일도
            // 하지 않는다. 탐색기는 대상을 선택한 채 열고, 폴더 열기(컨텍스트
            // 메뉴의 "저장소 열기")는 폴더 자체를 연다.
            void execute_open_external(const ui::open_external_request& request) const noexcept
            {
                try
                {
                    const auto wide { utf8_to_utf16(request.absolute_path) };
                    if (wide.value.has_value() == false || wide.value->empty())
                        return;

                    if (request.target == ui::external_open_target::explorer_folder)
                    {
                        static_cast<void>(ShellExecuteW(window_, L"open", wide.value->c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                        return;
                    }

                    if (request.target == ui::external_open_target::explorer)
                    {
                        std::wstring arguments { L"/select,\"" };
                        arguments += *wide.value;
                        arguments += L"\"";
                        static_cast<void>(ShellExecuteW(window_, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
                        return;
                    }

                    std::wstring arguments { L"\"" };
                    arguments += *wide.value;
                    arguments += L"\"";
                    // `code.cmd`는 cmd 창을 만들므로 숨겨서 실행한다. VSCode 자체는
                    // 분리된 GUI 프로세스로 뜬다.
                    static_cast<void>(ShellExecuteW(window_, L"open", L"code.cmd", arguments.c_str(), nullptr, SW_HIDE));
                }
                catch (...)
                {}
            }

            // 초점을 받은 텍스트 박스가 있으면 caret이 다음에 뒤집히는 시점에 다시
            // 그리도록 timer를 건다. 위상은 초점 시각 기준이라 render가 몇 번
            // 겹쳐도 뒤집힘 시점은 같다.
            void schedule_caret_repaint(const ui::interaction_snapshot& interaction) noexcept
            {
                if (interaction.focused_input == ui::ui_element_id {} || interaction.focus_started_at.has_value() == false)
                {
                    KillTimer(window_, caret_timer_id);
                    return;
                }

                const auto elapsed { std::chrono::steady_clock::now() - *interaction.focus_started_at };
                const auto phase { elapsed % ui::caret_blink_interval };
                const auto remaining { std::chrono::duration_cast<std::chrono::milliseconds>(ui::caret_blink_interval - phase) };
                SetTimer(window_, caret_timer_id, static_cast<UINT>(remaining.count() + 15), nullptr);
            }

            // hover가 tooltip 지연에 아직 도달하지 않았으면 지연이 끝나는 시점에 한 번
            // 다시 그리도록 timer를 건다. tooltip은 렌더러가 tree로 그린다.
            void schedule_tooltip_repaint(const ui::ui_tree* const tree, const ui::interaction_snapshot& interaction) noexcept
            {
                if (tree == nullptr || interaction.hover_started_at.has_value() == false)
                {
                    KillTimer(window_, tooltip_timer_id);
                    return;
                }
                const ui::ui_element* const hovered { tree->find(interaction.hovered) };
                if (hovered == nullptr || hovered->tooltip().empty())
                {
                    KillTimer(window_, tooltip_timer_id);
                    return;
                }

                const auto elapsed { std::chrono::steady_clock::now() - *interaction.hover_started_at };
                if (elapsed >= ui::tooltip_delay)
                {
                    KillTimer(window_, tooltip_timer_id);
                    return;
                }
                const auto remaining { std::chrono::duration_cast<std::chrono::milliseconds>(ui::tooltip_delay - elapsed) };
                SetTimer(window_, tooltip_timer_id, static_cast<UINT>(remaining.count() + 15), nullptr);
            }

            int scale_for_dpi(const int value) const noexcept
            {
                return MulDiv(value, static_cast<int>(dpi_), 96);
            }

            void report_runtime_error(const std::u8string& error) const noexcept
            {
                const auto wide_error { utf8_to_utf16(error) };
                if (wide_error.value.has_value())
                {
                    OutputDebugStringW(wide_error.value->c_str());
                    OutputDebugStringW(L"\n");
                }
            }

            HINSTANCE instance_ { nullptr };
            application_options options_ {};
            HWND window_ { nullptr };
            std::uint32_t dpi_ { 96 };
            // 마지막으로 적용한 창 배치 게시 번호다. 0은 아직 적용한 적이 없다는 뜻이다.
            std::uint64_t applied_window_placement_revision_ { 0 };
            ui::caption_button_hover hovered_caption_button_ { ui::caption_button_hover::none };
            std::chrono::steady_clock::time_point nc_hover_since_ {};
            bool tracking_non_client_mouse_ { false };
            bool tracking_client_mouse_ { false };
            std::unique_ptr<renderer_host> renderer_ {};
            std::unique_ptr<app_runtime> runtime_ {};
        };

        void show_startup_error(const std::u8string& error, const bool smoke_test)
        {
            const auto wide_error { utf8_to_utf16(error) };
            if (wide_error.value.has_value() == false)
                return;
            OutputDebugStringW(wide_error.value->c_str());
            OutputDebugStringW(L"\n");
            if (smoke_test == false)
                MessageBoxW(nullptr, L"Gitman을 시작하지 못했습니다. 자세한 내용은 디버그 로그를 확인하세요.", L"Gitman 시작 오류", MB_OK | MB_ICONERROR);
        }
    } // namespace

    int run_application(const HINSTANCE instance, const application_options& options)
    {
        std::u8string error {};
        if (verify_embedded_resources(error) == false)
        {
            show_startup_error(error, options.smoke_test);
            return 1;
        }

        application_window window { instance, options };
        if (window.create(error) == false)
        {
            show_startup_error(error, options.smoke_test);
            if (options.smoke_test && options.renderer == renderer_mode::direct3d)
                return direct3d_unavailable_exit_code;
            return 1;
        }
        return window.run();
    }
} // namespace gitman::win32

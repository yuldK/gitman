#include "platform/win32/win32_application.h"

#include "platform/win32/caption_layout.h"
#include "platform/win32/embedded_assets.h"
#include "platform/win32/resources/resource_ids.h"
#include "platform/win32/skia_renderer.h"
#include "platform/win32/utf8.h"
#include "platform/win32/version_list_generation_dialog.h"
#include "platform/win32/win32_app_runtime.h"
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
        // tooltip 지연이 끝나는 시점에 한 번 다시 그리기 위한 timer다.
        constexpr UINT_PTR tooltip_timer_id { 1 };

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
                    runtime_ = std::make_unique<app_runtime>(window_, snapshot_wake_message, ui_command_request_message);
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
                if (options_.workspace_document_path.has_value())
                    runtime_->post_logic(logic_message { open_document_intent { *options_.workspace_document_path } });

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
                    InvalidateRect(window_, nullptr, FALSE);
                    return 0;
                case ui_command_request_message:
                    execute_ui_command(static_cast<ui::ui_command>(word_parameter));
                    return 0;
                case WM_DROPFILES:
                    open_dropped_workspace_document(reinterpret_cast<HDROP>(word_parameter));
                    return 0;
                case WM_TIMER:
                    if (word_parameter == tooltip_timer_id)
                    {
                        KillTimer(window_, tooltip_timer_id);
                        InvalidateRect(window_, nullptr, FALSE);
                        return 0;
                    }
                    break;
                case WM_CLOSE:
                    // ADR-005 7.3: 스레드를 모두 정리한 뒤에 창을 파괴한다.
                    if (runtime_ != nullptr)
                    {
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
                        runtime_->post_raw_input(
                            ui::mouse_wheel_event { static_cast<float>(client_point.x), static_cast<float>(client_point.y), static_cast<float>(GET_WHEEL_DELTA_WPARAM(word_parameter)) });
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
                    update_maximized_bounds(reinterpret_cast<MINMAXINFO*>(long_parameter));
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
                    const int horizontal_border { GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_) };
                    const int vertical_border { GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_) };
                    const bool left { x < horizontal_border };
                    const bool right { x >= width - horizontal_border };
                    const bool top { y < vertical_border };
                    const bool bottom { y >= height - vertical_border };
                    if (top && left)
                        return HTTOPLEFT;
                    if (top && right)
                        return HTTOPRIGHT;
                    if (bottom && left)
                        return HTBOTTOMLEFT;
                    if (bottom && right)
                        return HTBOTTOMRIGHT;
                    if (left)
                        return HTLEFT;
                    if (right)
                        return HTRIGHT;
                    if (top)
                        return HTTOP;
                    if (bottom)
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

                        runtime_->post_logic(logic_message { open_document_intent { std::move(*converted.value) } });
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
                            runtime_->post_logic(logic_message { open_document_intent { *path } });
                    }
                    return;
                case ui::ui_command::show_generate_document_dialog:
                    if (runtime_ != nullptr)
                    {
                        if (const std::optional<generate_document_intent> intent { show_version_list_generation_dialog(window_) }; intent.has_value())
                            runtime_->post_logic(logic_message { *intent });
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

            void update_maximized_bounds(MINMAXINFO* information) const noexcept
            {
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
                return renderer_->render(state, error);
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

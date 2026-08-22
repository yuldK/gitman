#include "platform/win32/version_list_generation_dialog.h"

#include "domain/path_syntax.h"
#include "domain/project.h"
#include "platform/win32/utf8.h"
#include "presentation/ui_theme.h"

#include <dwmapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gitman::win32 {
    namespace {
        constexpr wchar_t dialog_class_name[] { L"Gitman.VersionListGenerationDialog" };
        constexpr int name_edit_id { 1001 };
        constexpr int folder_edit_id { 1002 };
        constexpr int browse_button_id { 1003 };
        constexpr int create_button_id { 1004 };
        constexpr int cancel_button_id { 1005 };
        constexpr int error_label_id { 1006 };
        constexpr int location_edit_id { 1007 };
        constexpr int location_browse_button_id { 1008 };
        constexpr int same_folder_check_id { 1009 };

        // 논리 96 DPI 기준 치수다. 표시 시점에 창 DPI로 배율한다.
        constexpr int dialog_width { 460 };
        constexpr int dialog_height { 236 };
        constexpr int dialog_margin { 16 };
        constexpr int label_width { 44 };
        constexpr int row_height { 26 };
        constexpr int field_gap { 10 };
        constexpr int browse_width { 34 };
        constexpr int action_width { 88 };
        // 체크 상자 행의 높이와 상자 한 변의 길이다.
        constexpr int check_row_height { 22 };
        constexpr int check_box_size { 14 };

        [[nodiscard]] int scaled(const int value, const UINT dpi) noexcept
        {
            return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
        }

        [[nodiscard]] COLORREF to_colorref(const ui_color value) noexcept
        {
            return RGB((value >> 16U) & 0xffU, (value >> 8U) & 0xffU, value & 0xffU);
        }

        // 알파가 있는 overlay 색(palette의 hover·pressed)을 base 위에 섞는다.
        [[nodiscard]] COLORREF blend_over(const COLORREF base, const ui_color overlay) noexcept
        {
            const int alpha { static_cast<int>((overlay >> 24U) & 0xffU) };
            const int inverse { 255 - alpha };
            const int red { (GetRValue(base) * inverse + static_cast<int>((overlay >> 16U) & 0xffU) * alpha + 127) / 255 };
            const int green { (GetGValue(base) * inverse + static_cast<int>((overlay >> 8U) & 0xffU) * alpha + 127) / 255 };
            const int blue { (GetBValue(base) * inverse + static_cast<int>(overlay & 0xffU) * alpha + 127) / 255 };
            return RGB(red, green, blue);
        }

        struct dialog_state;

        // owner-draw 버튼의 hover 추적이다. 클래식 컨트롤에는 hover 개념이 없어
        // subclass로 마우스 진입·이탈만 기록하고 그리기는 부모의 WM_DRAWITEM이 한다.
        struct button_state
        {
            dialog_state* dialog { nullptr };
            WNDPROC original_procedure { nullptr };
            bool hovered { false };
        };

        struct dialog_state
        {
            HWND window { nullptr };
            HWND name_edit { nullptr };
            HWND folder_edit { nullptr };
            HWND location_edit { nullptr };
            HWND location_browse { nullptr };
            HWND same_folder_check { nullptr };
            HWND error_label { nullptr };
            HFONT font { nullptr };
            HBRUSH window_brush { nullptr };
            HBRUSH input_brush { nullptr };
            // 팔레트는 테마 해석 결과를 담은 값이다 (키 컬러가 섞이므로 상수
            // 하나를 가리키던 포인터를 값으로 바꿨다).
            ui_color_palette palette {};
            UINT dpi { 96 };
            // EDIT 뒤에 부모가 그리는 입력 상자 테두리 영역이다.
            RECT name_field {};
            RECT folder_field {};
            RECT location_field {};
            std::array<button_state, 5> buttons {};
            std::optional<generate_document_intent> result {};
            // 기본값은 기존 동작(스캔 폴더에 생성)이다.
            bool same_folder { true };
            bool finished { false };
        };

        [[nodiscard]] std::wstring window_text(const HWND window)
        {
            const int length { GetWindowTextLengthW(window) };
            if (length <= 0)
                return {};
            std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
            const int copied { GetWindowTextW(window, text.data(), length + 1) };
            text.resize(static_cast<std::size_t>(copied < 0 ? 0 : copied));
            return text;
        }

        [[nodiscard]] std::wstring trimmed(const std::wstring& value)
        {
            std::size_t begin { 0 };
            std::size_t end { value.size() };
            while (begin < end && (value[begin] == L' ' || value[begin] == L'\t'))
                ++begin;
            while (end > begin && (value[end - 1] == L' ' || value[end - 1] == L'\t'))
                --end;
            return value.substr(begin, end - begin);
        }

        [[nodiscard]] bool valid_file_name(const std::wstring& name) noexcept
        {
            if (name.empty() || name == L"." || name == L"..")
                return false;
            for (const wchar_t value : name)
            {
                if (value < 0x20)
                    return false;
                if (value == L'<' || value == L'>' || value == L':' || value == L'"' || value == L'/' || value == L'\\' || value == L'|' || value == L'?' || value == L'*')
                    return false;
            }
            return name.back() != L'.' && name.back() != L' ';
        }

        [[nodiscard]] bool ends_with_extension(const std::wstring& name)
        {
            const std::wstring_view extension { L".version-list" };
            if (name.size() < extension.size())
                return false;
            const std::size_t offset { name.size() - extension.size() };
            for (std::size_t index = 0; index < extension.size(); ++index)
            {
                const wchar_t value { name[offset + index] };
                const wchar_t lowered { value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value - L'A' + L'a') : value };
                if (lowered != extension[index])
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::wstring folder_leaf(const std::wstring& folder)
        {
            std::size_t end { folder.size() };
            while (end > 0 && (folder[end - 1] == L'\\' || folder[end - 1] == L'/'))
                --end;
            std::size_t begin { end };
            while (begin > 0 && folder[begin - 1] != L'\\' && folder[begin - 1] != L'/')
                --begin;
            return folder.substr(begin, end - begin);
        }

        void show_error(dialog_state& state, const wchar_t* const message)
        {
            SetWindowTextW(state.error_label, message);
        }

        // 폴더 선택은 문서 열기 dialog와 같은 IFileOpenDialog를 폴더 모드로 쓴다.
        // 스캔 폴더와 저장 위치가 같은 함수를 제목만 바꿔 쓴다.
        [[nodiscard]] std::optional<std::wstring> choose_folder(const HWND owner, const std::wstring& initial, const wchar_t* const title)
        {
            IFileOpenDialog* dialog { nullptr };
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || dialog == nullptr)
                return std::nullopt;

            std::optional<std::wstring> chosen {};
            FILEOPENDIALOGOPTIONS options { 0 };
            if (SUCCEEDED(dialog->GetOptions(&options)))
                static_cast<void>(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
            static_cast<void>(dialog->SetTitle(title));
            if (initial.empty() == false)
            {
                IShellItem* start { nullptr };
                if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&start))) && start != nullptr)
                {
                    static_cast<void>(dialog->SetFolder(start));
                    start->Release();
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
                        chosen = { std::wstring { path } };
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dialog->Release();
            return chosen;
        }

        // 체크 상태에서는 위치가 스캔 폴더를 그대로 따라간다. 입력과 찾아보기는
        // 체크를 풀었을 때만 쓸 수 있다.
        void update_location_controls(dialog_state& state)
        {
            EnableWindow(state.location_edit, state.same_folder ? FALSE : TRUE);
            EnableWindow(state.location_browse, state.same_folder ? FALSE : TRUE);
            if (state.same_folder)
                SetWindowTextW(state.location_edit, window_text(state.folder_edit).c_str());
            InvalidateRect(state.window, nullptr, FALSE);
        }

        void apply_browse_result(dialog_state& state)
        {
            const std::optional<std::wstring> folder { choose_folder(state.window, trimmed(window_text(state.folder_edit)), L"저장소들을 담은 폴더 선택") };
            if (folder.has_value() == false)
                return;
            SetWindowTextW(state.folder_edit, folder->c_str());
            if (trimmed(window_text(state.name_edit)).empty())
                SetWindowTextW(state.name_edit, folder_leaf(*folder).c_str());
            update_location_controls(state);
            show_error(state, L"");
        }

        void apply_location_browse_result(dialog_state& state)
        {
            std::wstring initial { trimmed(window_text(state.location_edit)) };
            if (initial.empty())
                initial = trimmed(window_text(state.folder_edit));

            const std::optional<std::wstring> folder { choose_folder(state.window, initial, L".version-list을 저장할 폴더 선택") };
            if (folder.has_value() == false)
                return;
            SetWindowTextW(state.location_edit, folder->c_str());
            show_error(state, L"");
        }

        void attempt_create(dialog_state& state)
        {
            const std::wstring name { trimmed(window_text(state.name_edit)) };
            const std::wstring folder { trimmed(window_text(state.folder_edit)) };
            if (name.empty())
            {
                show_error(state, L"생성할 문서 이름을 입력하세요.");
                SetFocus(state.name_edit);
                return;
            }
            if (valid_file_name(name) == false)
            {
                show_error(state, L"이름에 파일 이름으로 쓸 수 없는 문자가 있습니다.");
                SetFocus(state.name_edit);
                return;
            }
            if (folder.empty())
            {
                show_error(state, L"저장소들을 담은 폴더를 지정하세요.");
                SetFocus(state.folder_edit);
                return;
            }

            const utf_conversion_result<std::u8string> folder_utf8 { utf16_to_utf8(folder) };
            if (folder_utf8.value.has_value() == false || is_absolute_windows_path(*folder_utf8.value) == false)
            {
                show_error(state, L"폴더는 절대 경로여야 합니다.");
                SetFocus(state.folder_edit);
                return;
            }
            const DWORD folder_attributes { GetFileAttributesW(folder.c_str()) };
            if (folder_attributes == INVALID_FILE_ATTRIBUTES || (folder_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                show_error(state, L"지정한 폴더를 찾을 수 없습니다.");
                SetFocus(state.folder_edit);
                return;
            }

            // 저장 위치는 체크 상태면 스캔 폴더와 같고, 아니면 따로 검증한다.
            std::wstring location { folder };
            if (state.same_folder == false)
            {
                location = trimmed(window_text(state.location_edit));
                if (location.empty())
                {
                    show_error(state, L"문서를 저장할 위치를 지정하세요.");
                    SetFocus(state.location_edit);
                    return;
                }

                const utf_conversion_result<std::u8string> location_utf8 { utf16_to_utf8(location) };
                if (location_utf8.value.has_value() == false || is_absolute_windows_path(*location_utf8.value) == false)
                {
                    show_error(state, L"저장 위치는 절대 경로여야 합니다.");
                    SetFocus(state.location_edit);
                    return;
                }

                const DWORD location_attributes { GetFileAttributesW(location.c_str()) };
                if (location_attributes == INVALID_FILE_ATTRIBUTES || (location_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    show_error(state, L"지정한 저장 위치를 찾을 수 없습니다.");
                    SetFocus(state.location_edit);
                    return;
                }
            }

            std::wstring document_path { location };
            if (document_path.back() != L'\\' && document_path.back() != L'/')
                document_path.push_back(L'\\');
            document_path.append(name);
            if (ends_with_extension(name) == false)
                document_path.append(L".version-list");
            if (GetFileAttributesW(document_path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                show_error(state, L"같은 이름의 .version-list 문서가 이미 있습니다.");
                SetFocus(state.name_edit);
                return;
            }

            const utf_conversion_result<std::u8string> document_utf8 { utf16_to_utf8(document_path) };
            if (document_utf8.value.has_value() == false)
            {
                show_error(state, L"이름을 문서 경로로 변환하지 못했습니다.");
                return;
            }

            state.result = { generate_document_intent { *folder_utf8.value, *document_utf8.value } };
            state.finished = true;
        }

        LRESULT CALLBACK button_subclass_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
        {
            auto* const state { reinterpret_cast<button_state*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
            if (state == nullptr)
                return DefWindowProcW(window, message, word_parameter, long_parameter);

            switch (message)
            {
            case WM_MOUSEMOVE:
                if (state->hovered == false)
                {
                    state->hovered = true;
                    TRACKMOUSEEVENT track { sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
                    static_cast<void>(TrackMouseEvent(&track));
                    InvalidateRect(window, nullptr, FALSE);
                }
                break;
            case WM_MOUSELEAVE:
                state->hovered = false;
                InvalidateRect(window, nullptr, FALSE);
                break;
            default:
                break;
            }
            return CallWindowProcW(state->original_procedure, window, message, word_parameter, long_parameter);
        }

        // 표준 체크 상자는 dark theme에서 밝게 그려진다. 상자와 글자를 직접 그린다.
        void draw_dialog_check(const dialog_state& state, const DRAWITEMSTRUCT& item)
        {
            const ui_color_palette& palette { state.palette };
            const auto* const hover_state { reinterpret_cast<const button_state*>(GetWindowLongPtrW(item.hwndItem, GWLP_USERDATA)) };

            COLORREF background { to_colorref(palette.window_background) };
            if ((item.itemState & ODS_SELECTED) != 0)
                background = blend_over(background, palette.button_pressed_background);
            else if (hover_state != nullptr && hover_state->hovered)
                background = blend_over(background, palette.button_hover_background);

            const HBRUSH fill { CreateSolidBrush(background) };
            FillRect(item.hDC, &item.rcItem, fill);
            DeleteObject(fill);

            const int size { scaled(check_box_size, state.dpi) };
            const int top { item.rcItem.top + ((item.rcItem.bottom - item.rcItem.top) - size) / 2 };
            RECT box { item.rcItem.left, top, item.rcItem.left + size, top + size };
            const HBRUSH box_fill { CreateSolidBrush(to_colorref(state.same_folder ? palette.accent : palette.surface_background)) };
            FillRect(item.hDC, &box, box_fill);
            DeleteObject(box_fill);
            const HBRUSH border { CreateSolidBrush(to_colorref(palette.tooltip_border)) };
            FrameRect(item.hDC, &box, border);
            DeleteObject(border);

            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, to_colorref(palette.primary_foreground));
            const HGDIOBJ previous_font { SelectObject(item.hDC, state.font) };
            RECT text { item.rcItem.left + size + scaled(8, state.dpi), item.rcItem.top, item.rcItem.right, item.rcItem.bottom };
            const std::wstring label { window_text(item.hwndItem) };
            DrawTextW(item.hDC, label.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item.hDC, previous_font);

            if ((item.itemState & ODS_FOCUS) != 0)
            {
                RECT focus { item.rcItem };
                InflateRect(&focus, -1, -1);
                DrawFocusRect(item.hDC, &focus);
            }
        }

        void draw_dialog_button(const dialog_state& state, const DRAWITEMSTRUCT& item)
        {
            if (item.CtlID == static_cast<UINT>(same_folder_check_id))
            {
                draw_dialog_check(state, item);
                return;
            }

            const ui_color_palette& palette { state.palette };
            const bool accent { item.CtlID == static_cast<UINT>(create_button_id) };
            const auto* const hover_state { reinterpret_cast<const button_state*>(GetWindowLongPtrW(item.hwndItem, GWLP_USERDATA)) };

            const COLORREF base { accent ? to_colorref(palette.accent) : to_colorref(palette.surface_background) };
            COLORREF fill { base };
            if ((item.itemState & ODS_SELECTED) != 0)
                fill = blend_over(base, palette.button_pressed_background);
            else if (hover_state != nullptr && hover_state->hovered)
                fill = blend_over(base, palette.button_hover_background);

            const HBRUSH brush { CreateSolidBrush(fill) };
            FillRect(item.hDC, &item.rcItem, brush);
            DeleteObject(brush);
            if (accent == false)
            {
                const HBRUSH border { CreateSolidBrush(to_colorref(palette.tooltip_border)) };
                FrameRect(item.hDC, &item.rcItem, border);
                DeleteObject(border);
            }

            const std::wstring text { window_text(item.hwndItem) };
            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, accent ? RGB(20, 20, 20) : to_colorref(palette.primary_foreground));
            const HGDIOBJ previous_font { SelectObject(item.hDC, state.font) };
            RECT bounds { item.rcItem };
            DrawTextW(item.hDC, text.c_str(), -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item.hDC, previous_font);

            if ((item.itemState & ODS_FOCUS) != 0)
            {
                RECT focus { item.rcItem };
                InflateRect(&focus, -3, -3);
                DrawFocusRect(item.hDC, &focus);
            }
        }

        [[nodiscard]] HWND create_owner_drawn_button(
            const dialog_state& state, const HINSTANCE instance, const wchar_t* const text, const int control_id, const int x, const int y, const int width, const int height)
        {
            return CreateWindowExW(
                0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, width, height, state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), instance, nullptr);
        }

        void register_button(dialog_state& state, const std::size_t slot, const HWND button)
        {
            state.buttons[slot].dialog = &state;
            SetWindowLongPtrW(button, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state.buttons[slot]));
            state.buttons[slot].original_procedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(button, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&button_subclass_procedure)));
        }

        void create_dialog_controls(dialog_state& state)
        {
            const UINT dpi { state.dpi };
            const HINSTANCE instance { reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(state.window, GWLP_HINSTANCE)) };
            const int margin { scaled(dialog_margin, dpi) };
            const int label { scaled(label_width, dpi) };
            const int row { scaled(row_height, dpi) };
            const int gap { scaled(field_gap, dpi) };
            RECT client {};
            GetClientRect(state.window, &client);
            const int content_width { client.right - client.left - margin * 2 };

            const int check_row { scaled(check_row_height, dpi) };
            const int name_row_top { margin };
            const int folder_row_top { name_row_top + row + gap };
            const int check_row_top { folder_row_top + row + gap };
            const int location_row_top { check_row_top + check_row + scaled(4, dpi) };
            const int field_left { margin + label + gap };
            const int browse { scaled(browse_width, dpi) };
            const int name_field_width { content_width - label - gap };
            const int folder_field_width { name_field_width - browse - gap };

            CreateWindowExW(0, L"STATIC", L"이름", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, margin, name_row_top, label, row, state.window, nullptr, instance, nullptr);
            CreateWindowExW(0, L"STATIC", L"폴더", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, margin, folder_row_top, label, row, state.window, nullptr, instance, nullptr);
            CreateWindowExW(0, L"STATIC", L"위치", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, margin, check_row_top, label, check_row, state.window, nullptr, instance, nullptr);

            state.name_field = { field_left, name_row_top, field_left + name_field_width, name_row_top + row };
            state.folder_field = { field_left, folder_row_top, field_left + folder_field_width, folder_row_top + row };
            state.location_field = { field_left, location_row_top, field_left + folder_field_width, location_row_top + row };

            // EDIT는 테두리 없이 만들고 상자는 부모가 그린다. 클래식 컨트롤의 밝은
            // 3D 테두리를 피하면서 IME를 포함한 표준 문자 입력을 그대로 얻는다.
            const int inset { scaled(6, dpi) };
            const int edit_height { row - scaled(8, dpi) };
            state.name_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, field_left + inset, name_row_top + scaled(4, dpi), name_field_width - inset * 2,
                edit_height, state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(name_edit_id)), instance, nullptr);
            state.folder_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, field_left + inset, folder_row_top + scaled(4, dpi),
                folder_field_width - inset * 2, edit_height, state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(folder_edit_id)), instance, nullptr);
            SendMessageW(state.name_edit, EM_SETLIMITTEXT, 128, 0);

            const HWND browse_button { create_owner_drawn_button(state, instance, L"...", browse_button_id, field_left + folder_field_width + gap, folder_row_top, browse, row) };

            state.same_folder_check = create_owner_drawn_button(state, instance, L"스캔 폴더에 만들기", same_folder_check_id, field_left, check_row_top, content_width - label - gap, check_row);
            state.location_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, field_left + inset, location_row_top + scaled(4, dpi),
                folder_field_width - inset * 2, edit_height, state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(location_edit_id)), instance, nullptr);
            state.location_browse = create_owner_drawn_button(state, instance, L"...", location_browse_button_id, field_left + folder_field_width + gap, location_row_top, browse, row);

            const int error_top { location_row_top + row + gap };
            state.error_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, margin, error_top, content_width, row, state.window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(error_label_id)), instance, nullptr);

            const int action_top { client.bottom - margin - row };
            const int action { scaled(action_width, dpi) };
            const HWND create_button { create_owner_drawn_button(state, instance, L"생성", create_button_id, client.right - margin - action * 2 - gap, action_top, action, row) };
            const HWND cancel_button { create_owner_drawn_button(state, instance, L"취소", cancel_button_id, client.right - margin - action, action_top, action, row) };

            register_button(state, 0, browse_button);
            register_button(state, 1, state.location_browse);
            register_button(state, 2, state.same_folder_check);
            register_button(state, 3, create_button);
            register_button(state, 4, cancel_button);
            update_location_controls(state);

            state.font = CreateFontW(
                -scaled(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            EnumChildWindows(
                state.window,
                [](const HWND child, const LPARAM font) -> BOOL {
                    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(state.font));
        }

        void paint_dialog(dialog_state& state)
        {
            PAINTSTRUCT paint {};
            const HDC device { BeginPaint(state.window, &paint) };
            RECT client {};
            GetClientRect(state.window, &client);
            FillRect(device, &client, state.window_brush);

            const HBRUSH border { CreateSolidBrush(to_colorref(state.palette.tooltip_border)) };
            for (const RECT& field : { state.name_field, state.folder_field, state.location_field })
            {
                RECT box { field };
                FillRect(device, &box, state.input_brush);
                FrameRect(device, &box, border);
            }
            DeleteObject(border);
            EndPaint(state.window, &paint);
        }

        LRESULT CALLBACK dialog_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
        {
            auto* state { reinterpret_cast<dialog_state*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
            switch (message)
            {
            case WM_NCCREATE:
                state = static_cast<dialog_state*>(reinterpret_cast<CREATESTRUCTW*>(long_parameter)->lpCreateParams);
                state->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                return DefWindowProcW(window, message, word_parameter, long_parameter);
            case WM_CREATE:
                create_dialog_controls(*state);
                return 0;
            case WM_PAINT:
                paint_dialog(*state);
                return 0;
            case WM_CTLCOLOREDIT: {
                const HDC device { reinterpret_cast<HDC>(word_parameter) };
                SetTextColor(device, to_colorref(state->palette.primary_foreground));
                SetBkColor(device, to_colorref(state->palette.surface_background));
                return reinterpret_cast<LRESULT>(state->input_brush);
            }
            case WM_CTLCOLORSTATIC: {
                const HDC device { reinterpret_cast<HDC>(word_parameter) };
                const HWND control { reinterpret_cast<HWND>(long_parameter) };
                // 비활성 EDIT은 STATIC 색 통지로 온다. 부모가 그린 입력 상자 안에
                // 있으므로 배경만은 입력 상자 색을 유지한다.
                if (control == state->location_edit)
                {
                    SetTextColor(device, RGB(150, 150, 150));
                    SetBkColor(device, to_colorref(state->palette.surface_background));
                    return reinterpret_cast<LRESULT>(state->input_brush);
                }

                const bool error { control == state->error_label };
                SetTextColor(device, error ? to_colorref(state->palette.error_accent) : RGB(190, 190, 190));
                SetBkColor(device, to_colorref(state->palette.window_background));
                return reinterpret_cast<LRESULT>(state->window_brush);
            }
            case WM_DRAWITEM:
                if (state != nullptr)
                {
                    draw_dialog_button(*state, *reinterpret_cast<const DRAWITEMSTRUCT*>(long_parameter));
                    return TRUE;
                }
                break;
            case WM_COMMAND:
                switch (LOWORD(word_parameter))
                {
                case browse_button_id:
                    if (HIWORD(word_parameter) == BN_CLICKED)
                        apply_browse_result(*state);
                    return 0;
                case location_browse_button_id:
                    if (HIWORD(word_parameter) == BN_CLICKED)
                        apply_location_browse_result(*state);
                    return 0;
                case same_folder_check_id:
                    if (HIWORD(word_parameter) == BN_CLICKED)
                    {
                        state->same_folder = state->same_folder == false;
                        update_location_controls(*state);
                        show_error(*state, L"");
                    }
                    return 0;
                case create_button_id:
                    if (HIWORD(word_parameter) == BN_CLICKED)
                        attempt_create(*state);
                    return 0;
                case cancel_button_id:
                    if (HIWORD(word_parameter) == BN_CLICKED)
                        state->finished = true;
                    return 0;
                case folder_edit_id:
                    if (HIWORD(word_parameter) == EN_CHANGE)
                    {
                        // 체크 상태에서는 위치가 스캔 폴더를 그대로 따라간다.
                        if (state->same_folder)
                            SetWindowTextW(state->location_edit, window_text(state->folder_edit).c_str());
                        show_error(*state, L"");
                    }
                    return 0;
                case name_edit_id:
                case location_edit_id:
                    if (HIWORD(word_parameter) == EN_CHANGE)
                        show_error(*state, L"");
                    return 0;
                default:
                    break;
                }
                break;
            case WM_CLOSE:
                state->finished = true;
                return 0;
            default:
                break;
            }
            return DefWindowProcW(window, message, word_parameter, long_parameter);
        }

        void register_dialog_class(const HINSTANCE instance)
        {
            WNDCLASSEXW window_class {};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = &dialog_procedure;
            window_class.hInstance = instance;
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            window_class.lpszClassName = dialog_class_name;
            // 등록 실패는 이미 등록된 경우다. 생성 실패가 진짜 실패를 보고한다.
            static_cast<void>(RegisterClassExW(&window_class));
        }
    } // namespace

    std::optional<generate_document_intent> show_version_list_generation_dialog(const HWND owner)
    {
        const HINSTANCE instance { reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE)) };
        register_dialog_class(instance);

        dialog_state state {};
        state.palette = color_palette_for(color_theme::dark);
        state.dpi = owner != nullptr ? GetDpiForWindow(owner) : 96;
        state.window_brush = CreateSolidBrush(to_colorref(state.palette.window_background));
        state.input_brush = CreateSolidBrush(to_colorref(state.palette.surface_background));

        const int width { scaled(dialog_width, state.dpi) };
        const int height { scaled(dialog_height, state.dpi) };
        RECT frame { 0, 0, width, height };
        constexpr DWORD style { WS_POPUP | WS_CAPTION | WS_SYSMENU };
        constexpr DWORD extended_style { WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT };
        AdjustWindowRectExForDpi(&frame, style, FALSE, extended_style, state.dpi);

        RECT owner_bounds { 0, 0, 0, 0 };
        if (owner != nullptr)
            GetWindowRect(owner, &owner_bounds);
        const int frame_width { frame.right - frame.left };
        const int frame_height { frame.bottom - frame.top };
        const int left { owner_bounds.left + ((owner_bounds.right - owner_bounds.left) - frame_width) / 2 };
        const int top { owner_bounds.top + ((owner_bounds.bottom - owner_bounds.top) - frame_height) / 2 };

        const HWND window { CreateWindowExW(extended_style, dialog_class_name, L".version-list 만들기", style, left, top, frame_width, frame_height, owner, nullptr, instance, &state) };
        if (window == nullptr)
        {
            DeleteObject(state.window_brush);
            DeleteObject(state.input_brush);
            return std::nullopt;
        }

        // 본 창과 같은 어두운 계열로 시스템 caption을 칠한다. 미지원 OS에서는 무해한
        // 실패다.
        const BOOL dark_mode { TRUE };
        static_cast<void>(DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode)));

        if (owner != nullptr)
            EnableWindow(owner, FALSE);
        ShowWindow(window, SW_SHOW);
        SetFocus(state.name_edit);

        MSG message {};
        while (state.finished == false && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && (message.hwnd == window || IsChild(window, message.hwnd) != FALSE))
            {
                attempt_create(state);
                continue;
            }
            if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE && (message.hwnd == window || IsChild(window, message.hwnd) != FALSE))
            {
                state.finished = true;
                continue;
            }
            if (IsDialogMessageW(window, &message) != FALSE)
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        // 종료 중 도착한 WM_QUIT은 본 메시지 루프가 이어받도록 되돌린다.
        if (message.message == WM_QUIT)
            PostQuitMessage(static_cast<int>(message.wParam));

        // 소유 창을 먼저 되살려야 dialog 파괴 시 초점이 다른 앱으로 넘어가지 않는다.
        if (owner != nullptr)
        {
            EnableWindow(owner, TRUE);
            SetActiveWindow(owner);
        }
        DestroyWindow(window);
        if (state.font != nullptr)
            DeleteObject(state.font);
        DeleteObject(state.window_brush);
        DeleteObject(state.input_brush);
        return state.result;
    }
} // namespace gitman::win32

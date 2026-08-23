#pragma once

#include "application/app_messages.h"

#include <windows.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gitman::win32 {
    // TSF가 동기로 읽고 쓰는 그림자 문서다
    // (text-input-and-font-fallback-design E2.3).
    //
    // TSF는 `GetText`·`GetSelection`·`SetText`를 **그 자리에서** 처리하기를 요구하는데
    // Gitman의 글 상태는 logic thread 소유이고 UI thread에는 snapshot으로만 온다.
    // 그래서 초점을 가진 박스 **하나**의 사본을 UI thread가 갖는다. 확정된 글의
    // 진실은 언제나 logic에 있고, 이것은 `interaction_snapshot`과 같은 "입력 정규화
    // 상태"다 (ADR-004).
    struct shadow_document
    {
        std::u8string text {};
        // UTF-8 byte offset이다. TSF가 쓰는 ACP(UTF-16)와는 단위가 다르다.
        std::size_t caret { 0 };
        std::size_t anchor { 0 };

        [[nodiscard]] bool operator==(const shadow_document&) const = default;
    };

    // ACP는 **UTF-16 코드 단위** 개수다. 그림자는 UTF-8이라 경계를 옮겨 주지 않으면
    // 조합이 엉뚱한 자리에 들어간다. 범위를 벗어난 값은 끝으로 자른다.
    [[nodiscard]] long acp_length(std::u8string_view text) noexcept;
    [[nodiscard]] long acp_from_utf8(std::u8string_view text, std::size_t byte_offset) noexcept;
    [[nodiscard]] std::size_t utf8_from_acp(std::u8string_view text, long acp) noexcept;

    // TSF session이 앱에 묻고 알리는 창구다. `application_window`가 구현한다.
    // 모두 UI thread에서만 불린다.
    struct tsf_host
    {
        tsf_host() = default;
        tsf_host(const tsf_host&) = delete;
        tsf_host(tsf_host&&) = delete;
        tsf_host& operator=(const tsf_host&) = delete;
        tsf_host& operator=(tsf_host&&) = delete;
        virtual ~tsf_host() = default;

        // 지금 초점을 가진 텍스트 박스와 그 확정된 글이다. 초점이 없으면 nullopt다.
        [[nodiscard]] virtual std::optional<text_input_target> focused_text_target() const = 0;
        [[nodiscard]] virtual shadow_document committed_document() const = 0;
        // 글 안의 byte 구간이 화면(스크린 좌표)에서 차지하는 자리다. 후보 창이 조합
        // 글자 밑에 붙는 근거다. 알 수 없으면 nullopt다.
        [[nodiscard]] virtual std::optional<RECT> text_screen_rect(std::size_t begin, std::size_t end) const = 0;
        // logic으로 보낸다.
        virtual void post_composition(set_text_composition_intent intent) = 0;
        virtual void post_edit(edit_text_input_intent intent) = 0;
    };

    // 창 하나가 갖는 TSF 연결이다 (E2.2). `ITfThreadMgr2` → `ITfDocumentMgr` →
    // `ITfContext` → `ITextStoreACP2` 한 벌이며, 초점이 옮겨 가면 문서를 바꾸는 대신
    // 그림자 내용을 갈아 끼운다 — 박스마다 COM 객체를 만들지 않는다.
    //
    // COM STA가 이미 잡혀 있어야 한다 (`main.cpp`의 `CoInitializeEx`).
    class tsf_input_session
    {
    public:
        // 실패하면 nullptr다. 그 경우 앱은 TSF 없이 `WM_CHAR` 경로만으로 동작한다.
        [[nodiscard]] static std::unique_ptr<tsf_input_session> create(HWND window, tsf_host& host);

        tsf_input_session(const tsf_input_session&) = delete;
        tsf_input_session(tsf_input_session&&) = delete;
        tsf_input_session& operator=(const tsf_input_session&) = delete;
        tsf_input_session& operator=(tsf_input_session&&) = delete;
        ~tsf_input_session();

        // 초점과 글이 바뀌었을 수 있다. 조합 중이면 미룬다 — 조합 도중 글이 밑에서
        // 바뀌면 TIP이 혼란에 빠진다. 매 frame 불러도 값이 그대로면 아무 일도 없다.
        void synchronize();

        // `WM_KEYDOWN`·`WM_KEYUP`을 TIP에 먼저 준다. true면 TIP이 먹었으므로 앱은
        // 그 키를 처리하지 않는다.
        [[nodiscard]] bool handle_key(UINT message, WPARAM word_parameter, LPARAM long_parameter);

        // 조합 중이면 `WM_CHAR`를 흘리지 않는다 — 확정 글은 TSF가 이미 보냈다.
        [[nodiscard]] bool composing() const noexcept;

    private:
        class implementation;
        explicit tsf_input_session(std::unique_ptr<implementation> impl) noexcept;

        std::unique_ptr<implementation> impl_ {};
    };
} // namespace gitman::win32

#include "platform/win32/win32_tsf_input.h"

#include <objbase.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// TSF는 문서를 **ACP(UTF-16 코드 단위)**로만 다루고 그림자 문서는 UTF-8이다
// (text-input-and-font-fallback-design E2.3). 단위가 섞이면 조합이 엉뚱한 자리에
// 들어가므로 경계 변환을 못박는다. COM에 기대지 않는 순수 부분이라 test가 가능하다.
TEST_CASE("ACP offsets count UTF-16 code units", "[win32][tsf]")
{
    constexpr std::u8string_view latin { u8"abc" };
    REQUIRE(gitman::win32::acp_length(latin) == 3);
    REQUIRE(gitman::win32::acp_from_utf8(latin, 2) == 2);

    // 한글은 UTF-8 3바이트 · UTF-16 1단위다.
    constexpr std::u8string_view hangul { u8"한글" };
    REQUIRE(hangul.size() == 6u);
    REQUIRE(gitman::win32::acp_length(hangul) == 2);
    REQUIRE(gitman::win32::acp_from_utf8(hangul, 3) == 1);

    // 이모지는 UTF-8 4바이트 · UTF-16 2단위(surrogate 쌍)다.
    constexpr std::u8string_view emoji { u8"a😀b" };
    REQUIRE(emoji.size() == 6u);
    REQUIRE(gitman::win32::acp_length(emoji) == 4);
    REQUIRE(gitman::win32::acp_from_utf8(emoji, 1) == 1);
    REQUIRE(gitman::win32::acp_from_utf8(emoji, 5) == 3);
}

TEST_CASE("ACP offsets convert back to UTF-8 byte offsets", "[win32][tsf]")
{
    constexpr std::u8string_view text { u8"a한😀b" };
    // a(1) 한(3) 😀(4) b(1) = 9바이트, ACP는 1 + 1 + 2 + 1 = 5단위다.
    REQUIRE(text.size() == 9u);
    REQUIRE(gitman::win32::acp_length(text) == 5);

    for (const long acp : { 0, 1, 2, 4, 5 })
        REQUIRE(gitman::win32::acp_from_utf8(text, gitman::win32::utf8_from_acp(text, acp)) == acp);

    REQUIRE(gitman::win32::utf8_from_acp(text, 0) == 0u);
    REQUIRE(gitman::win32::utf8_from_acp(text, 1) == 1u);
    REQUIRE(gitman::win32::utf8_from_acp(text, 2) == 4u);
    REQUIRE(gitman::win32::utf8_from_acp(text, 4) == 8u);
    REQUIRE(gitman::win32::utf8_from_acp(text, 5) == 9u);
}

TEST_CASE("ACP inside a surrogate pair falls back to the character start", "[win32][tsf]")
{
    constexpr std::u8string_view text { u8"a😀b" };
    // ACP 2는 이모지의 뒤쪽 surrogate다. 그 자리를 byte offset으로 옮기면 4바이트
    // 문자의 한가운데가 되어 글이 깨진다. 문자의 시작으로 되돌려야 한다.
    REQUIRE(gitman::win32::utf8_from_acp(text, 2) == 1u);
    REQUIRE(gitman::win32::utf8_from_acp(text, 3) == 5u);
}

TEST_CASE("ACP conversions clamp instead of running off the end", "[win32][tsf]")
{
    constexpr std::u8string_view text { u8"ab" };
    REQUIRE(gitman::win32::acp_from_utf8(text, 99) == 2);
    REQUIRE(gitman::win32::utf8_from_acp(text, 99) == 2u);
    REQUIRE(gitman::win32::utf8_from_acp(text, -1) == 0u);
    REQUIRE(gitman::win32::acp_length({}) == 0);
    REQUIRE(gitman::win32::utf8_from_acp({}, 3) == 0u);
}

namespace {
    // TSF가 묻는 것에 고정된 답을 주는 최소 host다. 실제 배선은
    // `application_window`가 한다.
    class stub_host final : public gitman::win32::tsf_host
    {
    public:
        [[nodiscard]] std::optional<gitman::text_input_target> focused_text_target() const override
        {
            return focused;
        }

        [[nodiscard]] gitman::win32::shadow_document committed_document() const override
        {
            return document;
        }

        [[nodiscard]] std::optional<RECT> text_screen_rect(std::size_t, std::size_t) const override
        {
            return RECT { 0, 0, 100, 20 };
        }

        void post_composition(gitman::set_text_composition_intent intent) override
        {
            compositions.push_back(std::move(intent));
        }

        void post_edit(gitman::edit_text_input_intent intent) override
        {
            edits.push_back(std::move(intent));
        }

        std::optional<gitman::text_input_target> focused {};
        gitman::win32::shadow_document document {};
        std::vector<gitman::set_text_composition_intent> compositions {};
        std::vector<gitman::edit_text_input_intent> edits {};
    };

    // COM STA를 이 test 동안만 잡는다. 앱에서는 `main.cpp`가 잡는다.
    class com_scope
    {
    public:
        com_scope() noexcept
            : result_ { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) }
        {}

        com_scope(const com_scope&) = delete;
        com_scope(com_scope&&) = delete;
        com_scope& operator=(const com_scope&) = delete;
        com_scope& operator=(com_scope&&) = delete;

        ~com_scope()
        {
            if (SUCCEEDED(result_))
                CoUninitialize();
        }

        [[nodiscard]] bool ready() const noexcept
        {
            return SUCCEEDED(result_);
        }

    private:
        HRESULT result_ { S_OK };
    };
} // namespace

// TSF 연결이 실제로 서는지다 (E2.2). ITfThreadMgr2 활성화 · document manager ·
// context · text store 한 벌이 모두 성립해야 조합이 박스 안에서 이루어진다.
TEST_CASE("A TSF session activates and tears down cleanly", "[win32][tsf]")
{
    const com_scope com {};
    if (com.ready() == false)
        SKIP("COM apartment initialization failed.");

    stub_host host {};
    const std::unique_ptr<gitman::win32::tsf_input_session> session {
        gitman::win32::tsf_input_session::create(nullptr, host),
    };
    if (session == nullptr)
        SKIP("Text Services Framework is not available on this machine.");

    REQUIRE(session->composing() == false);

    // 초점이 없으면 아무 일도 하지 않는다.
    session->synchronize();
    REQUIRE(host.compositions.empty());
    REQUIRE(host.edits.empty());

    // 초점이 들어오면 그림자를 채우고 TIP에 문서를 넘긴다. 조합이 없으므로
    // logic으로 나가는 것은 아직 없다.
    host.focused = gitman::text_input_target::switch_query;
    host.document = { u8"main", 4u, 4u };
    session->synchronize();
    REQUIRE(session->composing() == false);
    REQUIRE(host.edits.empty());
}

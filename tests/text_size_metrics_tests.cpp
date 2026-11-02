#include "presentation/list_metrics.h"

#include "luil/ui/font_scale.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

namespace {
    // 글자 크기 표를 넣고 test가 끝나면 비운다. 표는 프로세스 전역이라 다른 test에 새지 않게 한다.
    class text_size_scope
    {
    public:
        explicit text_size_scope(const float factor)
        {
            // n 논리 px 글자가 n × factor로 커지는 선형 표다 (Windows 텍스트 크기의 모양).
            std::vector<float> sizes {};
            for (std::size_t size = 0; size <= 64; ++size)
                sizes.push_back(static_cast<float>(size) * factor);
            applied_ = luil::set_system_font_sizes(std::move(sizes));
        }

        ~text_size_scope()
        {
            static_cast<void>(luil::set_system_font_sizes({}));
        }

        text_size_scope(const text_size_scope&) = delete;
        text_size_scope& operator=(const text_size_scope&) = delete;

        [[nodiscard]] bool applied() const noexcept
        {
            return applied_;
        }

    private:
        bool applied_ { false };
    };
} // namespace

TEST_CASE("Text rows keep their sizes at 100 percent text size", "[ui][metrics][text-size]")
{
    // 100%에서 함수가 된 치수는 상수였던 때의 값과 같다. 화면이 바뀌지 않는다.
    REQUIRE(gitman::text_growth(11.0f) == 0.0f);
    REQUIRE(gitman::layout_notice_height() == 22.0f);
    REQUIRE(gitman::layout_card_height() == 64.0f);
    REQUIRE(gitman::layout_card_status_height() == 16.0f);
    REQUIRE(gitman::layout_log_header_height() == 26.0f);
    REQUIRE(gitman::layout_log_line_height() == 15.0f);
    REQUIRE(gitman::layout_start_page_title_height() == 40.0f);
    REQUIRE(gitman::layout_start_page_subtitle_height() == 22.0f);
    REQUIRE(gitman::layout_start_page_section_height() == 26.0f);
    REQUIRE(gitman::layout_start_page_row_height() == 26.0f);
    REQUIRE(gitman::layout_switch_dialog_row_height() == 22.0f);
    REQUIRE(gitman::layout_local_changes_row_height() == 22.0f);
    REQUIRE(gitman::layout_local_changes_diff_line_height() == 15.0f);
    REQUIRE(gitman::layout_discovery_dialog_row_height() == 22.0f);
    REQUIRE(gitman::layout_font_dialog_row_height() == 24.0f);
    REQUIRE(gitman::layout_generate_dialog_row_height() == 30.0f);
}

TEST_CASE("Text rows grow with the system text size and the scroll range follows", "[ui][metrics][text-size]")
{
    const float content_at_100 { gitman::card_list_content_height(3, 1.0f) };
    const text_size_scope scope { 1.5f };
    REQUIRE(scope.applied());

    // 글이 든 칸은 그 칸의 글이 자란 만큼 자란다. 여백은 그대로다.
    REQUIRE(gitman::text_growth(11.0f) == 5.5f);
    REQUIRE(gitman::layout_card_height() == 64.0f + (14.0f + 11.0f + 10.5f) * 0.5f);
    REQUIRE(gitman::layout_log_line_height() == 15.0f + 10.5f * 0.5f);
    REQUIRE(gitman::layout_switch_dialog_row_height() == 22.0f + 11.5f * 0.5f);

    // 목록 내용 높이와 카드 위치가 같은 높이를 쓰므로 스크롤 한계가 카드와 함께 자란다.
    const float grown { (14.0f + 11.0f + 10.5f) * 0.5f };
    REQUIRE(gitman::card_list_content_height(3, 1.0f) == content_at_100 + grown * 3.0f);
    REQUIRE(gitman::card_content_top(2, 1.0f) == gitman::layout_margin + 2.0f * (gitman::layout_card_height() + gitman::layout_card_gap));
}

TEST_CASE("The function bar grows with the system text size and the log body gives way", "[ui][metrics][text-size][function_buttons][t10]")
{
    // 100%에서 줄은 36이고 로그 pane 160 = 줄 36 + 헤더 26 + 본문 98이다 (function-buttons-design 2.2.4).
    REQUIRE(gitman::layout_function_bar_height() == 36.0f);
    REQUIRE(gitman::layout_log_pane_height - gitman::layout_function_bar_height() - gitman::layout_log_header_height() == 98.0f);

    const text_size_scope scope { 1.5f };
    REQUIRE(scope.applied());
    // 줄은 버튼 라벨(12)이 자란 만큼 자라고, pane 높이는 상수라 그만큼 본문이 준다. 헤더도 자라 본문이 더 준다.
    REQUIRE(gitman::layout_function_bar_height() == 36.0f + gitman::text_growth(gitman::layout_function_bar_label_size));
    REQUIRE(gitman::layout_function_bar_height() == 42.0f);
    const float body { gitman::layout_log_pane_height - gitman::layout_function_bar_height() - gitman::layout_log_header_height() };
    REQUIRE(body == 98.0f - 6.0f - 11.5f * 0.5f);
}

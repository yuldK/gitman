#include "presentation/function_editor_layout.h"
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

TEST_CASE("The function editor holds four preview lines and grows with the system text size", "[ui][metrics][text-size][function_buttons][t12]")
{
    // 미리보기 칸은 머리·명령줄·작업 폴더·경고 네 줄을 담는다. 셋이 모두 나와도 경고 줄이 칸(바깥 위아래 4, 안쪽 위 3)
    // 안에 그려진다 (function-buttons-design 2.7 "허용 목록 위반을 보인다").
    const auto fits = []()
    {
        return 3.0f + 4.0f * gitman::layout_function_editor_preview_line_height() <= gitman::layout_function_editor_preview_height() - 8.0f;
    };
    REQUIRE(gitman::layout_function_editor_preview_height() == 86.0f);
    REQUIRE(fits());
    REQUIRE(gitman::layout_function_editor_row_height() == 28.0f);
    REQUIRE(gitman::layout_function_editor_field_height() == 26.0f);
    REQUIRE(gitman::layout_function_editor_note_height() == 16.0f);
    REQUIRE(gitman::layout_function_editor_check_height() == 24.0f);
    REQUIRE(gitman::layout_function_editor_list_header_height() == 22.0f);
    REQUIRE(gitman::layout_function_editor_variable_row_height() == 22.0f);
    const float form_at_100 { gitman::layout_function_editor_form(500.0f, 2).content_height };
    const float body_at_100 { gitman::function_editor_body_height(520.0f) };

    const text_size_scope scope { 1.5f };
    REQUIRE(scope.applied());
    // 글이 든 줄은 그 글이 자란 만큼 자란다. 폼 내용 높이(logic의 스크롤 한계)도 같은 함수라 함께 자란다.
    REQUIRE(gitman::layout_function_editor_row_height() == 28.0f + 6.0f);
    REQUIRE(gitman::layout_function_editor_field_height() == 26.0f + 5.5f);
    REQUIRE(gitman::layout_function_editor_preview_line_height() == 18.0f + 5.5f);
    REQUIRE(gitman::layout_function_editor_preview_height() == 14.0f + 4.0f * 23.5f);
    REQUIRE(fits());
    REQUIRE(gitman::layout_function_editor_form(500.0f, 2).content_height > form_at_100);
    REQUIRE(gitman::function_editor_body_height(520.0f) == body_at_100 - 4.0f * 5.5f);
}

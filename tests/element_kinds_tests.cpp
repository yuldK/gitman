#include "presentation/ui/element_kinds.h"

#include "luil/ui/ui_element_id.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

TEST_CASE("Application kinds are unique", "[presentation][kinds]")
{
    // index를 매기는 enum이 곧 값이라 겹칠 수 없지만, 표에 같은 상수를 두 번 적는
    // 실수는 여기서만 드러난다.
    std::vector<std::uint32_t> values {};
    for (const luil::ui_element_kind kind : gitman::ui::application_kinds)
        values.push_back(static_cast<std::uint32_t>(kind));
    std::sort(values.begin(), values.end());
    REQUIRE(std::adjacent_find(values.begin(), values.end()) == values.end());
    REQUIRE(values.size() == gitman::ui::application_kind_count);
}

TEST_CASE("Application kinds never fall into the luil reserved band", "[presentation][kinds]")
{
    // luil이 0~63을 예약한다. 앱 kind가 그 대역을 침범하면 창 root·caption과
    // 정체성이 겹친다.
    constexpr std::uint32_t first { static_cast<std::uint32_t>(luil::ui_element_kind::first_application_kind) };
    for (const luil::ui_element_kind kind : gitman::ui::application_kinds)
        REQUIRE(static_cast<std::uint32_t>(kind) >= first);

    // 값은 first부터 빈틈없이 이어진다 (표의 순서 = index).
    for (std::size_t index = 0; index < gitman::ui::application_kinds.size(); ++index)
        REQUIRE(static_cast<std::uint32_t>(gitman::ui::application_kinds[index]) == first + static_cast<std::uint32_t>(index));
}

TEST_CASE("The shared scrollbar kind is one of the application kinds", "[presentation][kinds]")
{
    // 예전 막대 8종은 kind 하나 + owner 문자열로 합쳐졌다. kind가 표에 있어야
    // 중복 검사와 예약 대역 검사가 함께 걸린다.
    const auto found { std::find(gitman::ui::application_kinds.begin(), gitman::ui::application_kinds.end(), gitman::ui::kind_scrollbar) };
    REQUIRE(found != gitman::ui::application_kinds.end());

    // 창 root·caption은 luil의 예약 kind라 앱 표에 없다.
    REQUIRE(std::find(gitman::ui::application_kinds.begin(), gitman::ui::application_kinds.end(), luil::ui_element_kind::root) == gitman::ui::application_kinds.end());
    REQUIRE(std::find(gitman::ui::application_kinds.begin(), gitman::ui::application_kinds.end(), luil::ui_element_kind::caption) == gitman::ui::application_kinds.end());
}

TEST_CASE("Scrollbar owners are distinct non empty strings", "[presentation][kinds]")
{
    // 막대를 가르는 것은 owner뿐이라 하나라도 겹치면 두 목록이 같은 element가 된다.
    const std::vector<std::u8string_view> owners {
        gitman::ui::scrollbar_owner_cards,
        gitman::ui::scrollbar_owner_log,
        gitman::ui::scrollbar_owner_switch_dialog,
        gitman::ui::scrollbar_owner_settings_dialog,
        gitman::ui::scrollbar_owner_font_dialog,
        gitman::ui::scrollbar_owner_local_changes_list,
        gitman::ui::scrollbar_owner_local_changes_diff,
        gitman::ui::scrollbar_owner_discovery_dialog,
    };

    for (const std::u8string_view owner : owners)
        REQUIRE(owner.empty() == false);

    std::vector<std::u8string_view> sorted { owners };
    std::sort(sorted.begin(), sorted.end());
    REQUIRE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

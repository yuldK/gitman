#pragma once

#include "presentation/view_snapshot.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 기능 버튼의 표시 문구다 (function-buttons-design 2.3.2, 2.3.4, 2.3.5). logic(메뉴 라벨)과 카드 element
    // (접근 이름·tooltip)가 같은 글을 쓰도록 한곳에 둔다. 상태 구분자는 앞뒤 공백을 둔 em dash(`—`)다.

    // 종료 코드다. runner가 `0xC0000005` 같은 bit 값을 부호 있는 수로 보존하므로 음수는 10진수 대신
    // Windows가 쓰는 16진수로 적는다 (T6의 로그 끝 줄과 같은 규칙).
    [[nodiscard]] std::u8string function_exit_code_text(std::int32_t code);

    // `Alt+N`이다. N이 0이면(`card` 배치가 아니면) 비어 있다.
    [[nodiscard]] std::u8string function_shortcut_text(std::size_t number);

    // 기능 메뉴·카드 메뉴의 항목 라벨이다. 메뉴 항목의 접근 이름은 라벨뿐이라 상태를 라벨에 넣는다.
    //  - 실행 중: `빌드 — 실행 중 00:12` (`elapsed`는 started부터의 경과, started 전이면 0)
    //  - 비활성: `린트 — <사유>`
    //  - 실패: `테스트 — 실패 (종료 코드 1)`, 종료 코드가 없는 실패(시간 초과, 시작 실패, 거부)는 `테스트 — 실패`
    //  - 그 밖: 라벨 그대로
    // logic은 메뉴를 연 시각까지의 경과를 넘겨 메뉴가 떠 있는 동안 라벨이 바뀌지 않게 한다.
    [[nodiscard]] std::u8string function_menu_label(const card_function_view& function, std::chrono::steady_clock::duration elapsed);

    // 카드 `card` 버튼의 접근 이름이다: `라벨, 카드 이름[, 실행 중 | 마지막 실패: 종료 코드 N]`. 카드마다 같은
    // 라벨이 되풀이되므로 카드 이름을 넣는다. 초마다 바뀌면 낭독이 반복되므로 경과 시간은 넣지 않는다.
    [[nodiscard]] std::u8string function_button_access_name(const card_function_view& function, std::u8string_view card_name);

    // 카드 `card` 버튼의 tooltip이다. 비활성이면 사유, 실행 중이면 중지 안내, 실패면 종료 코드다.
    [[nodiscard]] std::u8string function_button_tooltip(const card_function_view& function);

    // 기능 줄 버튼의 보이는 글이다 (2.2.2 규칙 6). 글자 버튼의 접근 이름은 글뿐이라 상태를 글에 넣는다: 실행 중이면
    // `빌드 · 실행 중`, 실패했으면 `빌드 · 실패`, 그 밖은 라벨이다. 경과 시간은 넣지 않는다 — 초마다 바뀌는 이름은
    // 낭독을 되풀이시킨다.
    [[nodiscard]] std::u8string function_bar_text(const card_function_view& function);
    // 기능 줄 버튼 폭을 정하는 가장 긴 상태 글이다: `라벨 · 실행 중` (2.2.4).
    [[nodiscard]] std::u8string function_bar_widest_text(std::u8string_view label);

    // `⋯`의 접근 이름이다: `기능 더 보기, 카드 이름, N개`.
    [[nodiscard]] std::u8string function_more_access_name(std::u8string_view card_name, std::size_t count);
    // 기능 줄 `⋯`의 접근 이름이다: `기능 더 보기 (기능 줄), 카드 이름, N개`. 선택 카드의 `⋯`와 같은 메뉴를 열지만
    // 한 tree에 둘이 함께 서므로 이름으로 가른다 — 같은 이름이면 낭독으로도 UIA 이름 찾기로도 구별되지 않는다.
    [[nodiscard]] std::u8string function_bar_more_access_name(std::u8string_view card_name, std::size_t count);

    // `⋯` 점의 상태다 (2.3.5).
    enum class function_more_marker
    {
        none,
        running,
        failed,
    };

    // `⋯` 점이 맡는 기능(카드 열에 버튼이 없는 것: `bar`·`menu`, 접힌 `card`)의 상태다. `on_card`는 `functions`와
    // 같은 순서로 카드 열에 보이는 버튼인지다. 실패가 있으면 실패다 — 실행 중임은 상태 아이콘 회전과 3번째 줄
    // 칩으로도 보이지만 실패는 이 점뿐이다.
    [[nodiscard]] function_more_marker function_more_marker_state(const std::vector<card_function_view>& functions, const std::vector<bool>& on_card);

    // 출력 수집 기능이 실행 중이라 update·switch가 막힌 사유다: `기능 "빌드"가 실행 중입니다` (4.7).
    [[nodiscard]] std::u8string function_running_reason(std::u8string_view label);
}

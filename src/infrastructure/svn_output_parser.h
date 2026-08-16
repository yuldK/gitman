#pragma once

#include "domain/repository_snapshot.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 비verbose `svn status` 한 줄이다. 앞의 상태 칸은 고정 폭이고 그 뒤가 전부 경로다.
    struct svn_status_entry
    {
        // 1번 칸. `M`, `A`, `D`, `C`, `?`, `!`, `~`, `I`, `X` 등이다.
        char8_t item_state { u8' ' };
        // 2번 칸의 속성 상태다. 속성만 바뀐 항목을 놓치지 않으려고 함께 본다.
        char8_t property_state { u8' ' };
        // 5번 칸. 이 항목이 다른 URL로 switch되어 있다.
        bool switched { false };
        // 7번 칸. tree conflict다.
        bool tree_conflict { false };
        std::u8string path {};
    };

    struct svn_status_summary
    {
        std::vector<svn_status_entry> entries {};
        // 형식을 알 수 없어 버린 줄 수다. 0이 아니면 작업 트리 상태를 신뢰하지 않는다.
        std::uint64_t unparsable_records { 0 };
    };

    // `svnversion` 출력이다. 문법이 `[저리비전:]고리비전[M][S][P]` 한 줄로 좁아 파서
    // 위험이 가장 작다. 작업 복사본이 아니면 해석하지 않는다.
    struct svn_version_info
    {
        std::uint64_t low_revision { 0 };
        std::uint64_t high_revision { 0 };
        bool modified { false };
        bool switched { false };
        // 일부만 받은 작업 복사본이다(`P`).
        bool partial { false };
        bool parsed { false };

        [[nodiscard]] bool mixed_revision() const noexcept;
    };

    // 값 하나만 내는 `--show-item` 출력에서 값을 꺼낸다. 빈 줄과 앞뒤 공백을 버린다.
    [[nodiscard]] std::u8string parse_svn_info_item(const std::vector<std::u8string>& lines);
    [[nodiscard]] svn_status_summary parse_svn_status(const std::vector<std::u8string>& lines);
    [[nodiscard]] svn_version_info parse_svnversion(std::u8string_view line);

    // 항목을 세어 카드가 쓰는 요약으로 옮긴다. Git과 같이 해석하지 못한 줄이 있으면
    // 개수는 채우되 상태는 `unknown`으로 둔다.
    [[nodiscard]] working_tree_summary summarize_svn_working_tree(const svn_status_summary& status);
    // `status`가 보고한 switched 항목이 하나라도 있는지다. `svnversion`이 없을 때의
    // 보조 판정이다.
    [[nodiscard]] bool has_svn_switched_entry(const svn_status_summary& status) noexcept;
} // namespace gitman

#pragma once

#include "domain/local_changes.h"
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

    // svn info --xml이 낸 작업 복사본 항목이다. --show-item을 항목마다 다시 실행하지
    // 않고 프로세스 하나로 같은 값을 얻는다. XML 요소 이름은 로캘과 무관하다.
    struct svn_info_fields
    {
        std::u8string url {};
        std::u8string relative_url {};
        std::u8string repository_root {};
        std::u8string repository_uuid {};
        std::u8string revision {};
        // 첫 entry의 `<commit revision>`이다. WC 리비전(entry attribute)과 달리 이
        // 노드의 마지막 커밋(last-changed) 리비전이라 브랜치의 최신 커밋을 뜻한다.
        // commit 요소가 없으면 빈 문자열이다.
        std::u8string last_changed_revision {};
        std::u8string working_copy_root {};
        // 필수 값(URL과 리비전)을 읽었을 때만 참이다.
        bool parsed { false };
    };

    // 값 하나만 내는 `--show-item` 출력에서 값을 꺼낸다. 빈 줄과 앞뒤 공백을 버린다.
    [[nodiscard]] std::u8string parse_svn_info_item(const std::vector<std::u8string>& lines);
    // svn info --xml 출력을 해석한다. 우리가 만든 요청의 출력만 들어오므로
    // 첫 번째 entry의 값만 읽는 좁은 해석이면 충분하다.
    [[nodiscard]] svn_info_fields parse_svn_info_xml(const std::vector<std::u8string>& lines);
    // 비recursive `svn ls` 기본 출력에서 `/`로 끝나는 디렉터리 이름만 남긴다.
    // 파일과 빈 줄은 버리고 서버가 준 순서를 보존한다.
    [[nodiscard]] std::vector<std::u8string> parse_svn_directory_list(const std::vector<std::u8string>& lines);
    [[nodiscard]] svn_status_summary parse_svn_status(const std::vector<std::u8string>& lines);
    [[nodiscard]] svn_version_info parse_svnversion(std::u8string_view line);

    // 항목을 세어 카드가 쓰는 요약으로 옮긴다. Git과 같이 해석하지 못한 줄이 있으면
    // 개수는 채우되 상태는 `unknown`으로 둔다.
    [[nodiscard]] working_tree_summary summarize_svn_working_tree(const svn_status_summary& status);
    // `status`가 보고한 switched 항목이 하나라도 있는지다. `svnversion`이 없을 때의
    // 보조 판정이다.
    [[nodiscard]] bool has_svn_switched_entry(const svn_status_summary& status) noexcept;

    // `svn update`·`svn switch` 출력에서 충돌 항목을 찾는다. 출력 행은 앞 네 칸이
    // 항목·속성·잠금·트리 충돌 문자이고 다섯 번째 칸이 공백이며, 그 자리의 'C'가
    // 충돌을 뜻한다. 상태 문자는 로캘과 무관하므로 사람이 읽는 문장을 파싱하지
    // 않는다는 원칙은 지켜진다. 한계: 이미 충돌 상태였던 항목은 update가 'C' 행 없이
    // Skipped 문장(로캘 의존)으로만 알리므로 여기서 잡히지 않는다.
    [[nodiscard]] bool svn_change_output_reports_conflict(const std::vector<std::u8string>& lines) noexcept;

    // status 항목을 로컬 변경 확인 dialog의 목록으로 옮긴다 (field-feedback-design
    // 2.3). 무시(`I`)와 외부(`X`) 항목은 뺀다.
    [[nodiscard]] std::vector<local_change_entry> collect_svn_local_changes(const svn_status_summary& status);
} // namespace gitman

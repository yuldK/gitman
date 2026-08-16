#pragma once

#include "domain/repository_snapshot.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // `rev-parse`가 보고하는 저장소 배치다. linked worktree는 git dir이
    // `<main>/.git/worktrees/<name>`이고 진행 중 작업 표식도 그 디렉터리에 있으므로
    // 추가 처리 없이 그대로 동작한다.
    struct git_repository_layout
    {
        std::u8string git_directory {};
        // bare 저장소와 git dir 안에서는 비어 있다. Git이 해당 인자에서 실패한다.
        std::u8string work_tree_root {};
        bool bare { false };
        bool inside_work_tree { false };
        // git dir과 두 판정 값을 읽었다. 등록 경로가 Git 저장소가 아니면 출력 자체가
        // 없으므로 거짓이다.
        bool parsed { false };
    };

    enum class git_status_entry_kind
    {
        // porcelain v2의 `1` 레코드다.
        ordinary,
        // `2` 레코드다. 원래 경로를 함께 담는다.
        renamed_or_copied,
        // `u` 레코드다.
        unmerged,
        // `?` 레코드다.
        untracked,
        // `!` 레코드다. 요약에서 세지 않는다.
        ignored,
    };

    struct git_status_entry
    {
        git_status_entry_kind kind { git_status_entry_kind::ordinary };
        // XY 상태 문자다. 변경이 없는 자리는 `.`이며 `?`와 `!` 레코드에는 없다.
        char8_t index_state { u8'.' };
        char8_t work_tree_state { u8'.' };
        std::u8string path {};
        // rename과 copy 항목의 원래 경로다.
        std::u8string original_path {};
    };

    struct git_status_summary
    {
        // `# branch.head` 값이다. detached HEAD에서는 `(detached)`다.
        std::u8string head {};
        // `# branch.oid` 값이다. 커밋이 없는 저장소(`(initial)`)에서는 비어 있다.
        std::u8string oid {};
        // `# branch.upstream` 값이다. upstream이 없으면 비어 있다.
        std::u8string upstream {};
        bool detached { false };
        bool unborn { false };
        // `# branch.ab`는 upstream이 있고 remote tracking ref를 읽을 수 있을 때만 나온다.
        bool has_ahead_behind { false };
        std::uint64_t ahead { 0 };
        std::uint64_t behind { 0 };
        // 하나라도 있으면 `--branch` 헤더를 읽었다는 뜻이다.
        bool has_branch_header { false };
        std::vector<git_status_entry> entries {};
        // 종류를 모르거나 필드 수가 모자라 해석하지 못한 레코드 수다. 0이 아니면
        // 작업 트리 상태를 신뢰하지 않는다.
        std::uint64_t unparsable_records { 0 };
    };

    // `for-each-ref`가 낸 한 줄이다. 값이 없는 칸은 비어 있다.
    struct git_reference_entry
    {
        // `refs/heads/<name>` 또는 `refs/remotes/<remote>/<branch>` 형태의 완전한 ref다.
        std::u8string name {};
        std::u8string object_id {};
        // `refs/remotes/<remote>/<branch>` 형태의 upstream이다. 없으면 비어 있다.
        std::u8string upstream {};
        // `%(HEAD)`가 `*`다. 현재 checkout된 branch를 나타낸다.
        bool head { false };
        // 심볼릭 ref가 가리키는 대상이다. `refs/remotes/<remote>/HEAD`가 대표적이며
        // 전환 후보가 아니다.
        std::u8string symbolic_target {};

        [[nodiscard]] bool symbolic() const noexcept;
    };

    struct git_ahead_behind
    {
        std::uint64_t ahead { 0 };
        std::uint64_t behind { 0 };
        bool parsed { false };
    };

    [[nodiscard]] git_repository_layout parse_git_repository_layout(const std::vector<std::u8string>& lines);
    [[nodiscard]] git_status_summary parse_git_status_porcelain_v2(const std::vector<std::u8string>& lines);

    // `git remote`는 이름을 한 줄에 하나씩 낸다. 빈 줄은 버린다.
    [[nodiscard]] std::vector<std::u8string> parse_git_remote_names(const std::vector<std::u8string>& lines);

    // `git submodule status --recursive` 출력이다. 한 줄이 `<표시><커밋 ID> <경로>
    // (<describe>)` 형태이며 표시가 상태를 나타낸다.
    //
    //     ' ' 등록된 커밋과 같다   '+' 다른 커밋이 checkout되어 있다
    //     '-' 초기화되지 않았다    'U' 충돌이 남아 있다
    [[nodiscard]] std::vector<submodule_status> parse_git_submodule_status(const std::vector<std::u8string>& lines);

    // `rev-list --left-right --count`의 `<ahead>\t<behind>` 한 줄을 읽는다. 왼쪽이 로컬,
    // 오른쪽이 원격이다.
    [[nodiscard]] git_ahead_behind parse_git_ahead_behind(std::u8string_view line);

    // TAB으로 나눈 `for-each-ref` 출력을 읽는다. ref 이름에는 TAB과 공백이 들어갈 수
    // 없으므로 경계가 흔들리지 않는다. `refs/`로 시작하지 않는 줄은 버린다.
    [[nodiscard]] std::vector<git_reference_entry> parse_git_reference_list(const std::vector<std::u8string>& lines);

    // `worktree list --porcelain`에서 checkout된 local branch 이름을 모은다. 항목은 빈
    // 줄로 나뉘며 detached worktree와 bare 항목에는 `branch` 줄이 없다. 현재 worktree의
    // branch도 함께 나온다.
    [[nodiscard]] std::vector<std::u8string> parse_git_worktree_branches(const std::vector<std::u8string>& lines);

    // 항목을 세어 카드가 쓰는 요약으로 옮긴다. 진행 중 작업과 `index.lock`은 표식
    // 파일로만 알 수 있으므로 호출자가 따로 채운다. 해석하지 못한 레코드가 있으면
    // 개수는 채우되 상태는 `unknown`으로 둔다. 모르는 상태를 깨끗하다고 보고하면
    // 보호 정책이 위험한 저장소에서 변경 명령을 허용하게 된다.
    [[nodiscard]] working_tree_summary summarize_git_working_tree(const git_status_summary& status);

    // `core.quotepath=false`에서도 Git은 제어 문자, `"`와 `\`가 들어간 경로를 C 인용
    // 규칙으로 감싼다. 인용된 값만 해제하고 나머지는 그대로 돌려준다.
    [[nodiscard]] std::u8string unquote_git_path(std::u8string_view value);
} // namespace gitman

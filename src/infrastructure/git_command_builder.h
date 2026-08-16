#pragma once

#include "application/process_request.h"

#include <cstddef>
#include <string_view>

namespace gitman {
    // `status --porcelain=v2`의 rename 항목은 한 줄에 경로 두 개를 담는다. 기본 레코드
    // 상한 8 KiB는 아주 긴 경로에서 줄을 강제로 끊을 수 있고, 그렇게 끊긴 줄은 파서가
    // 다른 레코드로 오해한다. 파서가 보는 줄은 항상 완결된 줄이어야 하므로 이 명령에만
    // 넉넉한 상한을 준다.
    inline constexpr std::size_t git_status_record_byte_limit { 64u * 1024u };

    // 저장소 배치를 한 번에 조회한다. 인자 순서가 곧 출력 순서다.
    //
    //     <absolute git dir>
    //     <is bare>
    //     <is inside work tree>
    //     <work tree root>
    //
    // `--show-toplevel`은 bare 저장소와 git dir 안에서 실패하므로 반드시 마지막에 둔다.
    // Git은 인자를 순서대로 처리하며 실패 전까지의 값은 이미 출력하기 때문에, 실패한
    // 경우에도 앞의 세 값으로 배치를 판정할 수 있다.
    [[nodiscard]] process_request make_git_layout_request(std::u8string_view executable, std::u8string_view working_directory);

    // 로컬 작업 트리 상태를 조회한다. 네트워크에 접근하지 않으며 `branch.ab`는 이미
    // 받아 둔 remote tracking ref와의 비교라 fetch를 유발하지 않는다.
    //
    // `-z`를 쓰지 않는다. 단계 3의 출력 파이프라인이 줄 단위 레코드를 만들고 줄 끝
    // 문자를 남기지 않으므로, NUL로만 구분된 출력은 파이프라인을 통과하면서 경계 정보를
    // 잃는다. 반대로 줄 단위 출력에서는 Git이 제어 문자와 `"`, `\`가 들어간 경로를 C
    // 인용으로 감싸므로 줄 경계가 흔들리지 않고, 로그 뷰에도 사람이 읽을 수 있는 줄로
    // 남는다. 인용 해제는 `unquote_git_path`가 담당한다.
    [[nodiscard]] process_request make_git_status_request(std::u8string_view executable, std::u8string_view working_directory);

    // 설정된 remote 이름을 한 줄에 하나씩 얻는다. `-v`를 쓰지 않는 이유는 URL이 필요하지
    // 않고, URL에 자격 증명이 들어 있으면 로그로 흘러나갈 수 있기 때문이다.
    [[nodiscard]] process_request make_git_remote_list_request(std::u8string_view executable, std::u8string_view working_directory);

    // 원격을 실제로 확인하는 유일한 조회 명령이다. `--prune`으로 지워진 remote branch의
    // 낡은 tracking ref를 함께 정리한다.
    [[nodiscard]] process_request make_git_fetch_request(std::u8string_view executable, std::u8string_view working_directory, std::u8string_view remote);

    // ref가 있는지 확인한다. 없으면 출력 없이 종료 코드만 실패이므로 메시지 언어와
    // 무관하게 판정할 수 있다.
    [[nodiscard]] process_request make_git_verify_reference_request(std::u8string_view executable, std::u8string_view working_directory, std::u8string_view reference);

    // `<ahead>\t<behind>` 한 줄을 얻는다. `HEAD`를 쓰면 branch 이름을 인자로 넘기지 않아도
    // 되고 이름에 특수 문자가 있어도 안전하다.
    [[nodiscard]] process_request make_git_ahead_behind_request(std::u8string_view executable, std::u8string_view working_directory, std::u8string_view target_reference);

    enum class git_submodule_recursion
    {
        // `update_submodules`가 꺼진 기본값이다. pull이 submodule을 건드리지 않는다.
        none,
        // 켜진 경우다. parent가 가리키는 커밋이 바뀐 submodule만 갱신한다.
        on_demand,
    };

    // ADR-003이 정한 update 명령이다. merge와 rebase를 만들지 않는 `--ff-only`만 쓰고
    // `--force`, `--rebase`, `--autostash`는 쓰지 않는다. remote와 branch를 명시해
    // 설정에 따라 다른 대상이 당겨지는 일이 없게 한다.
    [[nodiscard]] process_request make_git_pull_request(
        std::u8string_view executable, std::u8string_view working_directory, std::u8string_view remote, std::u8string_view branch, git_submodule_recursion recursion);

    // 등록된 submodule과 그 상태를 얻는다. 네트워크를 쓰지 않는다.
    [[nodiscard]] process_request make_git_submodule_status_request(std::u8string_view executable, std::u8string_view working_directory);

    // parent pull이 성공한 뒤에만 실행한다.
    [[nodiscard]] process_request make_git_submodule_update_request(std::u8string_view executable, std::u8string_view working_directory);
} // namespace gitman

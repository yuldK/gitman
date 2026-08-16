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
} // namespace gitman

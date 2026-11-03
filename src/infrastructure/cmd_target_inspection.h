#pragma once

#include "application/vcs_file_probe.h"
#include "domain/cmd_command_line.h"

namespace gitman {
    // cmd 기능이 부를 명령 대상을 디스크에서 확인한다 (function-buttons-design 4.3, S3). worker에서 부른다. 공백이 든 상대
    // 경로가 작업 폴더에 있는지 보고, 부를 배치 파일을 크기 상한(`cmd_batch_inspection_limit`)까지 읽어
    // 지연 확장·`call`을 찾는다(`scan_cmd_batch_text`). 경로는 `cmd_target_absolute_path`로 정규화해 묻는다. 배치가 없거나
    // 폴더면 `batch`가 비고, 상한을 넘었거나 있는데 열지 못했거나 이을 수 없는 경로면 `unchecked`다. 예외를 던지지 않는
    // probe만 쓴다.
    [[nodiscard]] cmd_target_facts inspect_cmd_target(const vcs_file_probe& probe, const cmd_target_query& query);
}

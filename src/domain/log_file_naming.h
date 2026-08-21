#pragma once

#include "domain/operation_log.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 저장소 폴더 이름의 길이 상한이다. 넘으면 앞부분 + 경로 해시로 자른다
    // (app-shell-design A4.1).
    inline constexpr std::size_t log_folder_name_limit { 80 };

    // 로그 루트다. 문서가 있는 폴더에 `.<문서 이름>.log` 폴더를 둔다
    // (`team.version-list` → `.team.version-list.log`). 앞의 `.`은 탐색기에서
    // 저장소 폴더들 사이에 섞이지 않고 맨 앞에 모이게 한다 (2026-08-22 지시).
    [[nodiscard]] std::u8string log_root_path(std::u8string_view document_path);

    // 저장소 작업 복사본 경로 목록에서 서로 구분되는 폴더 이름을 만든다 (입력 순서
    // 그대로). 규칙은 app-shell-design A4.1이다: 마지막 폴더 이름 → 겹치면 상위
    // 세그먼트를 하나씩 앞에 붙임(`a-b-c`) → 그래도 겹치면 드라이브·share를
    // 앞에 붙임(`c-drive_a-b-c`). 마지막으로 Windows 파일 이름 규칙에 맞춘다.
    [[nodiscard]] std::vector<std::u8string> log_folder_names(std::span<const std::u8string> repository_paths);

    // 로그 파일 이름이다. 로컬 시각 `YYYYMMDD-HHmmss.log`이며, 같은 이름이 이미
    // 있으면 `attempt`를 올려 `-2`, `-3`을 붙인다.
    [[nodiscard]] std::u8string log_file_name(std::chrono::system_clock::time_point time, std::size_t attempt = 0);

    // 파일에 적는 한 줄이다: `2026-08-21 18:40:12.128 [stdout/warning] 내용`.
    // 줄 끝(CRLF)은 붙이지 않는다.
    [[nodiscard]] std::u8string format_log_file_line(const operation_log_entry& entry);
} // namespace gitman

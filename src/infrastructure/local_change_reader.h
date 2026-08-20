#pragma once

#include "application/repository_provider.h"
#include "application/vcs_file_probe.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 미추적 파일의 내용을 diff 표시 형식(전체 "추가" 취급)으로 읽는다
    // (field-feedback-design 2.3). VCS 명령이 없는 경로라 Git과 SVN provider가
    // 함께 쓴다. 읽기 실패·디렉터리·이진 파일은 결과 플래그와 진단으로 알린다.
    [[nodiscard]] file_diff_result read_untracked_file_diff(const vcs_file_probe& probe, std::u8string_view working_directory, std::u8string_view relative_path);

    // 프로세스 출력 줄을 표시 상한(local_change_diff_display_limit)까지 결과에
    // 담는다. 넘치면 `truncated`를 켜고 나머지를 버린다.
    void append_diff_lines_limited(file_diff_result& result, std::vector<std::u8string> lines);
} // namespace gitman

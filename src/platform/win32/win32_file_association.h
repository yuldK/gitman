#pragma once

#include "domain/diagnostic.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman::win32 {
    struct file_association_outcome
    {
        bool succeeded { false };
        std::vector<diagnostic> diagnostics {};
    };

    // 실제 연결이 사는 HKCU 하위 경로다. test는 임시 root를 넣어 실제 연결을
    // 오염시키지 않는다 (stage-8-plan 5.4).
    inline constexpr std::u8string_view file_association_default_root { u8"Software\\Classes" };

    // `HKCU\{root_subkey}` 아래에 `.version-list` 연결을 등록한다. 멱등이며 기존
    // 값은 덮어쓴다. root가 기본값일 때만 shell에 연결 변경을 알린다.
    [[nodiscard]] file_association_outcome register_file_association(std::u8string_view executable_path, std::u8string_view root_subkey = file_association_default_root);

    // 이 앱이 만든 ProgID와 확장자 연결만 지운다. 확장자 기본값이 다른 앱의
    // ProgID면 그대로 둔다. 지울 것이 없어도 성공이다 (멱등).
    [[nodiscard]] file_association_outcome unregister_file_association(std::u8string_view root_subkey = file_association_default_root);

    // 확장자 연결과 ProgID open command가 이 실행 파일로 등록되어 있는지 조회한다.
    [[nodiscard]] bool file_association_registered(std::u8string_view executable_path, std::u8string_view root_subkey = file_association_default_root);

    // 현재 프로세스 실행 파일의 절대 경로다. 실패하면 빈 값이다.
    [[nodiscard]] std::u8string current_executable_path();
} // namespace gitman::win32

#pragma once

#include <string>
#include <string_view>

namespace gitman {
    // Win32 API 없이 어휘적으로만 절대 경로를 판정한다. `C:\`와 `C:/`, `\\server\share`,
    // `\\?\C:\`를 절대 경로로 보고 `C:relative`, `\rooted`, 빈 값은 거부한다.
    //
    // 프로세스 요청 검증과 문서 `settings`의 실행 파일 경로 검증이 같은 규칙을 써야
    // 하므로 도메인에 둔다. filesystem을 조회하지 않으므로 존재 여부는 알 수 없다.
    [[nodiscard]] bool is_absolute_windows_path(std::u8string_view path) noexcept;

    // 경로의 마지막 구성 요소를 뺀 디렉터리다. 문서 경로에서 문서가 있는 폴더를
    // 얻는 데 쓴다. 구분자가 없으면 빈 값이다.
    [[nodiscard]] std::u8string_view windows_parent_directory(std::u8string_view path) noexcept;

    // `base_directory` 기준의 상대 경로다. filesystem을 조회하지 않는 어휘 계산이며
    // 대소문자를 구분하지 않는다(Windows 규칙). 뿌리(drive·share)가 다르거나 둘 중
    // 하나가 절대 경로가 아니면 원본을 그대로 돌려준다. 같은 위치면 `.`이다.
    [[nodiscard]] std::u8string relative_windows_path(std::u8string_view path, std::u8string_view base_directory);
} // namespace gitman

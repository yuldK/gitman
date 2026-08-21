#pragma once

#include <string>

namespace gitman::win32 {
    // 앱 단위 설정 파일의 절대 경로다 (app-shell-design A1.1). 실행 파일과 같은
    // 폴더의 `gitman.app-settings.json`이며, 실행 파일 경로를 알 수 없으면 빈 값이다.
    [[nodiscard]] std::u8string app_settings_file_path();
} // namespace gitman::win32

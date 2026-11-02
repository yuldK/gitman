#pragma once

#include "application/process_launcher.h"

#include <memory>

namespace gitman::win32 {
    // Win32 분리 실행 구현을 만든다. 실행별 상태가 없어 여러 스레드에서 함께 쓸 수 있다.
    [[nodiscard]] std::unique_ptr<process_launcher> make_process_launcher();
} // namespace gitman::win32

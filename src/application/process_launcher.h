#pragma once

#include "application/process_request.h"
#include "domain/diagnostic.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gitman {
    enum class process_launch_status
    {
        started,
        // 요청 값이 계약을 어겨 프로세스를 만들지 않았다.
        invalid_request,
        start_failed,
    };

    struct process_launch_result
    {
        process_launch_status status { process_launch_status::invalid_request };
        std::optional<std::uint32_t> native_error {};
        // `invalid_request`면 `invalid_process_request`, `start_failed`면 `process_start_failed` 진단이다.
        // 메모리가 모자라 진단 문자열마저 만들지 못한 `start_failed`만 비어 있을 수 있다.
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool started() const noexcept
        {
            return status == process_launch_status::started;
        }
    };

    // 기능 버튼의 분리 실행이다 (function-buttons-design 4.2). 자식을 job에 넣지 않고
    // 기다리지 않는다. 시작 성공 여부만 돌려주고, 자식은 앱이 끝난 뒤에도 남는다.
    // 하나의 구현 instance는 여러 스레드에서 동시에 `launch`를 불러도 안전해야 한다.
    // 실행 풀과 logic test는 이 인터페이스를 대역으로 바꾼다.
    class process_launcher
    {
    public:
        process_launcher() = default;
        process_launcher(const process_launcher&) = delete;
        process_launcher(process_launcher&&) = delete;
        process_launcher& operator=(const process_launcher&) = delete;
        process_launcher& operator=(process_launcher&&) = delete;
        virtual ~process_launcher() = default;

        [[nodiscard]] virtual process_launch_result launch(const process_launch_request& request) noexcept = 0;
    };
} // namespace gitman

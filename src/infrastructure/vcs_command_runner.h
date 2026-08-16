#pragma once

#include "application/process_cancellation.h"
#include "application/process_request.h"
#include "application/process_runner.h"
#include "domain/process_execution.h"

#include <string>
#include <vector>

namespace gitman {
    struct vcs_command_result
    {
        process_result process {};
        std::vector<std::u8string> standard_output_lines {};
        std::vector<std::u8string> standard_error_lines {};

        // 명령이 정상 종료하고 종료 코드가 0인 경우에만 참이다.
        [[nodiscard]] bool succeeded() const noexcept;
        [[nodiscard]] std::u8string standard_output_text() const;
        [[nodiscard]] std::u8string standard_error_text() const;
        // 값 하나만 출력하는 명령(`svn info --show-item` 등)의 결과를 꺼낸다.
        [[nodiscard]] std::u8string first_output_line() const;
    };

    // 두 스트림을 분리해 모으면서 호출자가 준 sink에도 그대로 전달한다. 카드 로그는
    // 단계 7에서 이 sink 자리에 들어간다.
    class vcs_output_collector final : public process_output_sink
    {
    public:
        vcs_output_collector() = default;
        explicit vcs_output_collector(process_output_sink* forward) noexcept;

        void on_record(const process_output_record& record) override;

        [[nodiscard]] std::vector<std::u8string> take_standard_output() noexcept;
        [[nodiscard]] std::vector<std::u8string> take_standard_error() noexcept;

    private:
        process_output_sink* forward_ { nullptr };
        std::vector<std::u8string> standard_output_ {};
        std::vector<std::u8string> standard_error_ {};
    };

    // 요청을 실행하고 출력을 스트림별로 모은다. 예외를 던지지 않는다.
    [[nodiscard]] vcs_command_result run_vcs_command(process_runner& runner, const process_request& request, const process_cancellation_token& token, process_output_sink* forward = nullptr) noexcept;
} // namespace gitman

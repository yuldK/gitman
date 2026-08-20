#pragma once

#include "application/process_runner.h"
#include "application/vcs_file_probe.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gitman::testing {
    // provider와 도구 조사가 만든 요청을 그대로 기록하고 미리 등록한 출력을 돌려준다.
    // "이 조건에서 어떤 명령을 만들었는가"와 "이 출력에서 어떤 값으로 변환했는가"를
    // 실제 Git/SVN 없이 한 자리에서 확인할 수 있다.
    class fake_process_runner final : public process_runner
    {
    public:
        struct response
        {
            process_completion completion { process_completion::exited };
            std::int32_t exit_code { 0 };
            std::u8string standard_output {};
            std::u8string standard_error {};
        };

        // 등록 순서대로 하나씩 소비한다. 남은 응답이 없으면 기본 응답을 쓴다.
        void push_response(response value);
        void set_default_response(response value);

        [[nodiscard]] const std::vector<process_request>& requests() const noexcept;
        [[nodiscard]] std::size_t request_count() const noexcept;
        [[nodiscard]] const process_request& request(std::size_t index) const;
        // 실행 파일 경로 끝이 주어진 이름과 일치하는 요청 수다.
        [[nodiscard]] std::size_t requests_for_executable_suffix(std::u8string_view suffix) const;

        [[nodiscard]] process_result run(const process_request& request, process_output_sink* sink, const process_cancellation_token& token) noexcept override;

    private:
        std::vector<process_request> requests_ {};
        std::vector<response> responses_ {};
        std::size_t next_response_ { 0 };
        response default_response_ { process_completion::exited, 1, {}, u8"fake runner has no scripted response" };
    };

    // 등록한 경로만 존재하는 filesystem 대역이다. Windows 경로 비교와 같게 ASCII
    // 대소문자를 무시하고 구분자도 동일하게 취급한다.
    class fake_vcs_file_probe final : public vcs_file_probe
    {
    public:
        void add_file(std::u8string_view path);
        // 내용을 함께 등록하면 `read_prefix`가 그 바이트를 돌려준다.
        void add_file(std::u8string_view path, std::u8string_view content);
        void add_directory(std::u8string_view path);

        [[nodiscard]] vcs_path_kind probe(std::u8string_view absolute_path) const noexcept override;
        [[nodiscard]] vcs_file_content read_prefix(std::u8string_view absolute_path, std::size_t maximum_bytes) const noexcept override;

    private:
        struct entry
        {
            std::u8string path {};
            vcs_path_kind kind { vcs_path_kind::file };
            std::u8string content {};
        };

        std::vector<entry> entries_ {};
    };
} // namespace gitman::testing

#pragma once

#include "application/process_runner.h"
#include "application/vcs_file_probe.h"
#include "domain/vcs_tool.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gitman::testing {
    // 실제 `git.exe`로 임시 저장소를 만든다. 호스트에 Git이 없으면 `available()`이
    // 거짓이고 통합 test는 스스로 skip한다. 임시 디렉터리는 실패해도 남지 않도록
    // 소멸자에서 지운다.
    //
    // 모든 명령은 사용자 설정과 분리해 실행한다. `HOME`과 `GIT_CONFIG_GLOBAL`을 임시
    // 디렉터리로 돌리고 시스템 설정을 끄지 않으면 호스트의 `core.autocrlf`나 서명 설정에
    // 따라 결과가 달라진다.
    class git_repository_fixture
    {
    public:
        git_repository_fixture();
        git_repository_fixture(const git_repository_fixture&) = delete;
        git_repository_fixture(git_repository_fixture&&) = delete;
        git_repository_fixture& operator=(const git_repository_fixture&) = delete;
        git_repository_fixture& operator=(git_repository_fixture&&) = delete;
        ~git_repository_fixture();

        [[nodiscard]] bool available() const noexcept;
        [[nodiscard]] const vcs_tool_info& tool() const noexcept;
        [[nodiscard]] process_runner& runner() const noexcept;
        [[nodiscard]] const vcs_file_probe& probe() const noexcept;

        [[nodiscard]] std::u8string path_of(std::u8string_view relative) const;
        [[nodiscard]] std::u8string make_directory(std::u8string_view relative);
        [[nodiscard]] std::u8string make_repository(std::u8string_view relative);
        [[nodiscard]] std::u8string make_bare_repository(std::u8string_view relative);

        void write_file(std::u8string_view directory, std::u8string_view relative, std::string_view content);

        // 성공을 기대하는 명령이다. 실패는 `failures()`에 쌓이며 호출자는 준비가 끝난 뒤
        // 한 번만 확인하면 된다.
        void git(std::u8string_view working_directory, std::vector<std::u8string> arguments);
        // 실패를 기대하는 명령이다(충돌하는 merge와 rebase). 종료 코드를 돌려주고
        // 실패로 기록하지 않는다.
        [[nodiscard]] int git_allowing_failure(std::u8string_view working_directory, std::vector<std::u8string> arguments);

        [[nodiscard]] const std::vector<std::u8string>& failures() const noexcept;

    private:
        [[nodiscard]] int execute(std::u8string_view working_directory, std::vector<std::u8string> arguments, bool record_failure);

        std::filesystem::path root_ {};
        std::unique_ptr<process_runner> runner_ {};
        std::unique_ptr<vcs_file_probe> probe_ {};
        vcs_tool_info tool_ {};
        std::vector<std::u8string> failures_ {};
    };
} // namespace gitman::testing

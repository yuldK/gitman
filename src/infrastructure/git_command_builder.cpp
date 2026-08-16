#include "infrastructure/git_command_builder.h"

#include "domain/repository_snapshot.h"
#include "infrastructure/vcs_execution_policy.h"

#include <string>
#include <utility>
#include <vector>

namespace gitman {
    process_request make_git_layout_request(const std::u8string_view executable, const std::u8string_view working_directory)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"rev-parse" });
        arguments.push_back(std::u8string { u8"--absolute-git-dir" });
        arguments.push_back(std::u8string { u8"--is-bare-repository" });
        arguments.push_back(std::u8string { u8"--is-inside-work-tree" });
        // bare 저장소에서 실패하는 인자다. 앞의 값을 잃지 않도록 마지막에 둔다.
        arguments.push_back(std::u8string { u8"--show-toplevel" });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::local_query);
    }

    process_request make_git_status_request(const std::u8string_view executable, const std::u8string_view working_directory)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"status" });
        arguments.push_back(std::u8string { u8"--porcelain=v2" });
        arguments.push_back(std::u8string { u8"--branch" });
        // 무시된 파일은 세지 않는다. 기본값이지만 Git 설정으로 바뀌지 않도록 명시한다.
        arguments.push_back(std::u8string { u8"--untracked-files=normal" });
        arguments.push_back(std::u8string { u8"--ignored=no" });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::local_query, git_status_record_byte_limit);
    }
} // namespace gitman

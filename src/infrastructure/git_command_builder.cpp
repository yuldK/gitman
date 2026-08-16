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

    process_request make_git_remote_list_request(const std::u8string_view executable, const std::u8string_view working_directory)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"remote" });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::local_query);
    }

    process_request make_git_fetch_request(const std::u8string_view executable, const std::u8string_view working_directory, const std::u8string_view remote)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"fetch" });
        arguments.push_back(std::u8string { u8"--prune" });
        // remote 이름은 저장소 설정에서 오므로 `-`로 시작하는 값이 인자로 해석되지 않게
        // 끊어 준다.
        arguments.push_back(std::u8string { u8"--" });
        arguments.push_back(std::u8string { remote });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::remote_query);
    }

    process_request make_git_verify_reference_request(const std::u8string_view executable, const std::u8string_view working_directory, const std::u8string_view reference)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"rev-parse" });
        arguments.push_back(std::u8string { u8"--verify" });
        // 없는 ref를 오류 메시지 없이 종료 코드로만 알린다. `rev-parse`의 `--`는 뒤의
        // 값을 경로로 만들기 때문에 여기서는 쓸 수 없다. 대신 호출자가 `refs/`로 시작하는
        // 완전한 ref만 넘긴다.
        arguments.push_back(std::u8string { u8"--quiet" });
        arguments.push_back(std::u8string { reference });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::local_query);
    }

    process_request make_git_pull_request(
        const std::u8string_view executable, const std::u8string_view working_directory, const std::u8string_view remote, const std::u8string_view branch, const git_submodule_recursion recursion)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"pull" });
        // fast-forward가 불가능하면 merge를 만들지 않고 실패한다. 보호 정책의 핵심이다.
        arguments.push_back(std::u8string { u8"--ff-only" });
        arguments.push_back(std::u8string { recursion == git_submodule_recursion::on_demand ? u8"--recurse-submodules=on-demand" : u8"--recurse-submodules=no" });
        // remote와 branch 이름은 저장소 설정에서 오므로 인자로 해석되지 않게 끊어 준다.
        arguments.push_back(std::u8string { u8"--" });
        arguments.push_back(std::u8string { remote });
        arguments.push_back(std::u8string { branch });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::update);
    }

    process_request make_git_submodule_status_request(const std::u8string_view executable, const std::u8string_view working_directory)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"submodule" });
        arguments.push_back(std::u8string { u8"status" });
        arguments.push_back(std::u8string { u8"--recursive" });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::local_query);
    }

    process_request make_git_submodule_update_request(const std::u8string_view executable, const std::u8string_view working_directory)
    {
        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"submodule" });
        arguments.push_back(std::u8string { u8"update" });
        arguments.push_back(std::u8string { u8"--init" });
        arguments.push_back(std::u8string { u8"--recursive" });
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::update);
    }

    process_request make_git_ahead_behind_request(const std::u8string_view executable, const std::u8string_view working_directory, const std::u8string_view target_reference)
    {
        std::u8string range { u8"HEAD..." };
        range.append(target_reference);

        std::vector<std::u8string> arguments {};
        arguments.push_back(std::u8string { u8"rev-list" });
        arguments.push_back(std::u8string { u8"--left-right" });
        arguments.push_back(std::u8string { u8"--count" });
        arguments.push_back(std::move(range));
        return make_vcs_process_request(repository_kind::git, executable, working_directory, std::move(arguments), vcs_command_class::local_query);
    }
} // namespace gitman

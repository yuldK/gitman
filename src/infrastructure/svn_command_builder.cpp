#include "infrastructure/svn_command_builder.h"

#include "domain/repository_snapshot.h"
#include "infrastructure/vcs_execution_policy.h"

#include <string>
#include <utility>
#include <vector>

namespace gitman {
    namespace {
        std::vector<std::u8string> make_arguments(const std::u8string_view command)
        {
            std::vector<std::u8string> arguments {};
            arguments.push_back(std::u8string { command });
            return arguments;
        }
    } // namespace

    std::u8string_view svn_info_item_name(const svn_info_item item) noexcept
    {
        switch (item)
        {
        case svn_info_item::url:
            return u8"url";
        case svn_info_item::relative_url:
            return u8"relative-url";
        case svn_info_item::repository_root:
            return u8"repos-root-url";
        case svn_info_item::repository_uuid:
            return u8"repos-uuid";
        case svn_info_item::revision:
            return u8"revision";
        case svn_info_item::working_copy_root:
            return u8"wc-root";
        }
        return u8"url";
    }

    process_request make_svn_info_item_request(const std::u8string_view executable, const std::u8string_view working_directory, const svn_info_item item, const std::u8string_view target)
    {
        std::vector<std::u8string> arguments { make_arguments(u8"info") };
        arguments.push_back(std::u8string { u8"--show-item" });
        arguments.push_back(std::u8string { svn_info_item_name(item) });
        if (target.empty() == false)
            arguments.push_back(std::u8string { target });
        return make_vcs_process_request(repository_kind::subversion, executable, working_directory, std::move(arguments), vcs_command_class::local_query);
    }

    process_request make_svn_status_request(const std::u8string_view executable, const std::u8string_view working_directory)
    {
        // 대상을 주지 않으면 작업 디렉터리를 기준으로 상대 경로를 낸다. 카드가 그대로
        // 보여 줄 수 있는 형태다.
        return make_vcs_process_request(repository_kind::subversion, executable, working_directory, make_arguments(u8"status"), vcs_command_class::local_query);
    }

    process_request make_svnversion_request(const std::u8string_view executable, const std::u8string_view working_directory)
    {
        const vcs_command_limits limits { vcs_limits_for(vcs_command_class::local_query) };

        // 공통 인자를 붙이지 않는 유일한 명령이다. 나머지 실행 정책은 같게 맞춘다.
        process_request request {};
        request.executable = executable;
        request.working_directory = working_directory;
        request.environment_overrides = vcs_environment_overrides(repository_kind::subversion);
        request.timeout = limits.timeout;
        request.maximum_captured_bytes_per_stream = limits.maximum_captured_bytes_per_stream;
        request.text_encoding = process_text_encoding::active_code_page_fallback;
        return request;
    }

    process_request make_svn_remote_revision_request(const std::u8string_view executable, const std::u8string_view working_directory, const std::u8string_view url)
    {
        std::vector<std::u8string> arguments { make_arguments(u8"info") };
        arguments.push_back(std::u8string { u8"--show-item" });
        arguments.push_back(std::u8string { svn_info_item_name(svn_info_item::revision) });
        arguments.push_back(std::u8string { url });
        return make_vcs_process_request(repository_kind::subversion, executable, working_directory, std::move(arguments), vcs_command_class::remote_query);
    }
} // namespace gitman

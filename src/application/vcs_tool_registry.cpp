#include "application/vcs_tool_registry.h"

#include <utility>

namespace gitman {
    namespace {
        void append_tool_diagnostics(std::vector<diagnostic>& target, const vcs_tool_info& tool)
        {
            if (tool.usable())
                return;
            target.insert(target.end(), tool.diagnostics.begin(), tool.diagnostics.end());
        }
    } // namespace

    vcs_tool_registry::vcs_tool_registry(vcs_tool_set tools) noexcept
        : tools_ { std::move(tools) }
    {}

    void vcs_tool_registry::set_tools(vcs_tool_set tools) noexcept
    {
        tools_ = std::move(tools);
    }

    const vcs_tool_set& vcs_tool_registry::tools() const noexcept
    {
        return tools_;
    }

    const vcs_tool_info& vcs_tool_registry::tool(const repository_kind kind) const noexcept
    {
        return tools_.tool(kind);
    }

    bool vcs_tool_registry::available(const repository_kind kind) const noexcept
    {
        return tools_.available(kind);
    }

    bool vcs_tool_registry::any_available() const noexcept
    {
        return tools_.any_available();
    }

    bool vcs_tool_registry::none_available() const noexcept
    {
        return tools_.none_available();
    }

    std::vector<diagnostic> vcs_tool_registry::unavailable_diagnostics() const
    {
        std::vector<diagnostic> result {};
        append_tool_diagnostics(result, tools_.git);
        append_tool_diagnostics(result, tools_.subversion);
        return result;
    }

    std::u8string_view vcs_tool_unavailable_message(const repository_kind kind, const vcs_tool_availability availability) noexcept
    {
        const bool is_subversion { kind == repository_kind::subversion };
        switch (availability)
        {
        case vcs_tool_availability::unknown:
            return is_subversion ? u8"SVN 명령줄 도구를 아직 조사하지 않았습니다." : u8"Git 명령줄 도구를 아직 조사하지 않았습니다.";
        case vcs_tool_availability::not_found:
            return is_subversion ? u8"SVN 명령줄 도구를 찾을 수 없습니다. 설치하거나 문서 settings에 경로를 지정하세요."
                                 : u8"Git 명령줄 도구를 찾을 수 없습니다. 설치하거나 문서 settings에 경로를 지정하세요.";
        case vcs_tool_availability::path_invalid:
            return is_subversion ? u8"문서 settings에 지정한 SVN 실행 파일 경로를 사용할 수 없습니다." : u8"문서 settings에 지정한 Git 실행 파일 경로를 사용할 수 없습니다.";
        case vcs_tool_availability::version_unreadable:
            return is_subversion ? u8"SVN 버전을 확인하지 못했습니다." : u8"Git 버전을 확인하지 못했습니다.";
        case vcs_tool_availability::too_old:
            return is_subversion ? u8"설치된 SVN이 지원 최소 버전보다 오래되었습니다." : u8"설치된 Git이 지원 최소 버전보다 오래되었습니다.";
        case vcs_tool_availability::available:
            return is_subversion ? u8"SVN 명령줄 도구를 사용할 수 있습니다." : u8"Git 명령줄 도구를 사용할 수 있습니다.";
        }
        return u8"명령줄 도구를 사용할 수 없습니다.";
    }
} // namespace gitman

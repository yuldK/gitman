#include "infrastructure/cmd_target_inspection.h"

#include <string>
#include <string_view>

namespace gitman {
    cmd_target_facts inspect_cmd_target(const vcs_file_probe& probe, const cmd_target_query& query)
    {
        cmd_target_facts facts {};
        facts.query = query;
        if (query.relative_path.has_value())
        {
            const std::u8string path { cmd_target_absolute_path(query.working_directory, *query.relative_path) };
            facts.path_exists = path.empty() == false && probe.probe(path) == vcs_path_kind::file;
        }

        const std::u8string batch { cmd_target_batch_path(query, facts.path_exists) };
        if (batch.empty())
            return facts;
        // 작업 폴더로 이을 수 없는 경로(다른 드라이브의 `D:x.bat`)는 cmd가 어디서 찾을지 모른다. 없다고 하지 않는다.
        const std::u8string path { cmd_target_absolute_path(query.working_directory, batch) };
        if (path.empty())
        {
            facts.batch = cmd_batch_findings { .unchecked = true };
            return facts;
        }
        // 상한을 넘으면(`truncated`) 앞부분만 보고 "없다"고 하지 않는다 — 뒤에 있을 수 있다.
        const vcs_file_content content { probe.read_prefix(path, cmd_batch_inspection_limit) };
        if (content.kind != vcs_path_kind::file)
        {
            // 있는데 열지 못했으면(다른 프로세스의 잠금, 권한) 없는 것과 다르다. cmd는 나중에 열 수 있으므로 확인하지 못한
            // 것으로 경고한다 (S3 검토). 정말 없으면 PATH에서 찾거나 cmd가 스스로 실패한다.
            if (probe.probe(path) == vcs_path_kind::file)
                facts.batch = cmd_batch_findings { .unchecked = true };
            return facts;
        }
        if (content.truncated)
        {
            facts.batch = cmd_batch_findings { .unchecked = true };
            return facts;
        }
        const std::string_view bytes { reinterpret_cast<const char*>(content.bytes.data()), content.bytes.size() };
        facts.batch = scan_cmd_batch_text(bytes);
        return facts;
    }
}

#pragma once

#include "application/project_path_resolver.h"
#include "infrastructure/json_workspace_document.h"

namespace gitman {
    void resolve_workspace_document_paths(workspace_document_parse_result& result, project_path_resolver& path_resolver);
} // namespace gitman

#include "application/repository_provider.h"

#include <algorithm>

namespace gitman {
    bool repository_query_result::has_errors() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::error; });
    }
} // namespace gitman

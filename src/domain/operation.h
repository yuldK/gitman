#pragma once

#include "domain/project.h"

#include <string>
#include <string_view>

namespace gitman {
    struct operation_id
    {
        std::u8string value {};

        [[nodiscard]] bool operator==(const operation_id&) const noexcept = default;
    };

    enum class operation_kind
    {
        refresh,
        update,
        switch_target,
    };

    enum class operation_state
    {
        queued,
        running,
        succeeded,
        failed,
        cancelled,
    };

    struct operation_descriptor
    {
        operation_id id {};
        project_id project {};
        operation_kind kind { operation_kind::refresh };
        operation_state state { operation_state::queued };
    };

    [[nodiscard]] std::u8string_view operation_kind_name(operation_kind kind) noexcept;
    [[nodiscard]] std::u8string_view operation_state_name(operation_state state) noexcept;
} // namespace gitman

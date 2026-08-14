#include "domain/operation.h"

namespace gitman {
    std::u8string_view operation_kind_name(const operation_kind kind) noexcept
    {
        switch (kind)
        {
        case operation_kind::refresh:
            return u8"refresh";
        case operation_kind::update:
            return u8"update";
        case operation_kind::switch_target:
            return u8"switch";
        }
        return u8"unknown";
    }

    std::u8string_view operation_state_name(const operation_state state) noexcept
    {
        switch (state)
        {
        case operation_state::queued:
            return u8"queued";
        case operation_state::running:
            return u8"running";
        case operation_state::succeeded:
            return u8"succeeded";
        case operation_state::failed:
            return u8"failed";
        case operation_state::cancelled:
            return u8"cancelled";
        }
        return u8"unknown";
    }
} // namespace gitman

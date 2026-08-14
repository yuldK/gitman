#pragma once

#include "include/core/SkRefCnt.h"

#include <cstddef>
#include <string>

class SkTypeface;

namespace gitman::win32 {
    struct embedded_resource_view
    {
        const void* data { nullptr };
        std::size_t size { 0 };
    };

    [[nodiscard]] embedded_resource_view find_embedded_resource(int resource_id) noexcept;
    [[nodiscard]] sk_sp<SkTypeface> load_codicon_typeface();
    [[nodiscard]] sk_sp<SkTypeface> load_ui_typeface();
    [[nodiscard]] bool verify_embedded_resources(std::u8string& error);
} // namespace gitman::win32

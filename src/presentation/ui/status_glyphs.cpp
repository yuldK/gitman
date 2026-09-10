#include "presentation/ui/status_glyphs.h"

#include "generated/codicon_names.h"

#include "luil/generated/codicons.h"

#include <algorithm>

namespace gitman::ui {
    char32_t codicon_for_name(const std::u8string_view name) noexcept
    {
        if (name == u8"pass")
            return luil::codicons::icon_pass;
        if (name == u8"arrow-down")
            return luil::codicons::icon_arrow_down;
        if (name == u8"arrow-up")
            return luil::codicons::icon_arrow_up;
        if (name == u8"git-compare")
            return luil::codicons::icon_git_compare;
        if (name == u8"key")
            return luil::codicons::icon_key;
        if (name == u8"home")
            return luil::codicons::icon_home;
        if (name == u8"warning")
            return luil::codicons::icon_warning;
        if (name == u8"debug-disconnect")
            return luil::codicons::icon_debug_disconnect;
        if (name == u8"error")
            return luil::codicons::icon_error;
        if (name == u8"circle-slash")
            return luil::codicons::icon_circle_slash;
        return luil::codicons::icon_question;
    }

    char32_t codicon_for_configured_name(const std::u8string_view name, const char32_t fallback) noexcept
    {
        if (name.empty())
            return fallback;

        // 표는 만들 때 이름 순으로 정렬된다 (cmake/generate_codicon_names.cmake).
        // 그 순서가 `std::u8string_view`의 사전식 비교와 같아야 이분 탐색이 성립하며,
        // 실제로 그런지는 test가 확인한다.
        const auto entry { std::ranges::lower_bound(generated::codicon_names, name, {}, &generated::codicon_name_entry::name) };
        if (entry == generated::codicon_names.end() || entry->name != name)
            return fallback;
        return entry->codepoint;
    }

    char32_t default_command_codicon() noexcept
    {
        // 사용자 명령은 "무언가를 실행한다"가 전부라 재생 글리프가 가장 덜 틀린다.
        return luil::codicons::icon_play;
    }
} // namespace gitman::ui

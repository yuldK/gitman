#include "presentation/ui/status_glyphs.h"

#include "luil/generated/codicons.h"

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
} // namespace gitman::ui

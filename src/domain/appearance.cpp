#include "domain/appearance.h"

namespace gitman {
    std::u8string_view theme_preference_name(const theme_preference preference) noexcept
    {
        switch (preference)
        {
        case theme_preference::light:
            return u8"light";
        case theme_preference::dark:
            return u8"dark";
        case theme_preference::system:
        default:
            return u8"system";
        }
    }

    bool parse_theme_preference(const std::u8string_view name, theme_preference& target) noexcept
    {
        if (name == u8"system")
        {
            target = theme_preference::system;
            return true;
        }
        if (name == u8"light")
        {
            target = theme_preference::light;
            return true;
        }
        if (name == u8"dark")
        {
            target = theme_preference::dark;
            return true;
        }
        return false;
    }
} // namespace gitman

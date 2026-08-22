#include "domain/appearance.h"

namespace gitman {
    bool appearance_overrides::empty() const noexcept
    {
        return theme.has_value() == false && accent_id.has_value() == false;
    }

    appearance_settings apply_overrides(const appearance_settings& base, const appearance_overrides& overrides)
    {
        appearance_settings effective { base };
        if (overrides.theme.has_value())
            effective.theme = *overrides.theme;
        if (overrides.accent_id.has_value())
            effective.accent_id = *overrides.accent_id;
        return effective;
    }

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

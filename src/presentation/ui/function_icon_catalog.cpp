#include "presentation/ui/function_icon_catalog.h"

#include "luil/generated/codicons.h"

#include <array>
#include <cstddef>
#include <span>

namespace gitman::ui {
    namespace {
        struct icon_name_glyph
        {
            std::u8string_view name {};
            char32_t glyph { 0 };
        };

        // 앞 20개가 D16의 기능 아이콘 이름이고, 뒤는 메뉴 항목이 쓰는 이름이다. 이름은 codicon 이름 그대로다.
        constexpr std::size_t function_icon_count { 20 };
        constexpr std::array<icon_name_glyph, 27> icons {
            icon_name_glyph { u8"tools", luil::codicons::icon_tools },
            icon_name_glyph { u8"play", luil::codicons::icon_play },
            icon_name_glyph { u8"debug-start", luil::codicons::icon_debug_start },
            icon_name_glyph { u8"terminal", luil::codicons::icon_terminal },
            icon_name_glyph { u8"beaker", luil::codicons::icon_beaker },
            icon_name_glyph { u8"rocket", luil::codicons::icon_rocket },
            icon_name_glyph { u8"package", luil::codicons::icon_package },
            icon_name_glyph { u8"bug", luil::codicons::icon_bug },
            icon_name_glyph { u8"build", luil::codicons::icon_build },
            icon_name_glyph { u8"code", luil::codicons::icon_code },
            icon_name_glyph { u8"window", luil::codicons::icon_window },
            icon_name_glyph { u8"folder-opened", luil::codicons::icon_folder_opened },
            icon_name_glyph { u8"file-code", luil::codicons::icon_file_code },
            icon_name_glyph { u8"sync", luil::codicons::icon_sync },
            icon_name_glyph { u8"cloud-upload", luil::codicons::icon_cloud_upload },
            icon_name_glyph { u8"cloud-download", luil::codicons::icon_cloud_download },
            icon_name_glyph { u8"trash", luil::codicons::icon_trash },
            icon_name_glyph { u8"gear", luil::codicons::icon_gear },
            icon_name_glyph { u8"link-external", luil::codicons::icon_link_external },
            icon_name_glyph { u8"run-all", luil::codicons::icon_run_all },
            // 카드 메뉴·배너 메뉴의 항목이다 (T8).
            icon_name_glyph { u8"vscode", luil::codicons::icon_vscode },
            icon_name_glyph { u8"diff", luil::codicons::icon_diff },
            icon_name_glyph { u8"refresh", luil::codicons::icon_refresh },
            icon_name_glyph { u8"repo-pull", luil::codicons::icon_repo_pull },
            icon_name_glyph { u8"source-control", luil::codicons::icon_source_control },
            // 기능 메뉴의 실행 중 항목(중지)과 `⋯`다 (2.3.2).
            icon_name_glyph { u8"stop-circle", luil::codicons::icon_stop_circle },
            icon_name_glyph { u8"ellipsis", luil::codicons::icon_ellipsis },
        };

        char32_t find_glyph(const std::span<const icon_name_glyph> table, const std::u8string_view name) noexcept
        {
            if (name.empty())
                return 0;
            for (const icon_name_glyph& icon : table)
            {
                if (icon.name == name)
                    return icon.glyph;
            }
            return 0;
        }
    }

    char32_t icon_glyph_by_name(const std::u8string_view name) noexcept
    {
        return find_glyph(icons, name);
    }

    char32_t function_icon_glyph(const std::u8string_view name) noexcept
    {
        // D16의 20개만 찾는다. 메뉴 전용 이름(`stop-circle`, `ellipsis` 등)을 `icon`에 쓰면 쉬는 기능이 중지 버튼이나
        // `⋯`처럼 보이므로 모르는 이름처럼 `play`다.
        const char32_t glyph { find_glyph(std::span { icons }.first(function_icon_count), name) };
        return glyph != 0 ? glyph : luil::codicons::icon_play;
    }
}

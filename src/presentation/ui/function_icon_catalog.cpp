#include "presentation/ui/function_icon_catalog.h"

#include "presentation/function_editor_layout.h"

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

        // 앞 20개가 D16의 기능 아이콘 이름이고, 뒤는 메뉴 항목이 쓰는 이름이다. 이름은 codicon 이름 그대로이고 글리프도
        // 그 이름의 것이다. 하나만 다르다: `debug-start`는 codicon `debug-start`가 `play`와 거의 같은 삼각형이라 아이콘
        // 줄에서 둘을 가를 수 없어 재생 + 벌레 모양인 `debug-alt`로 그린다 (S2). 이름은 그대로 두어 이미 그 이름을 쓴
        // 문서가 그대로 읽히고 `play`로 떨어지지 않는다.
        constexpr std::size_t function_icon_count { 20 };
        constexpr std::array<icon_name_glyph, 28> icons {
            icon_name_glyph { u8"tools", luil::codicons::icon_tools },
            icon_name_glyph { u8"play", luil::codicons::icon_play },
            icon_name_glyph { u8"debug-start", luil::codicons::icon_debug_alt },
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
            // 배너 메뉴의 `문서 실행 파일 신뢰…`다 (function-buttons-design 3.10, S1).
            icon_name_glyph { u8"workspace-trusted", luil::codicons::icon_workspace_trusted },
        };

        // 아이콘 목록이 쓰는 이름 표다. 위 표의 앞 20개와 같은 순서다.
        constexpr std::array<std::u8string_view, function_icon_count> function_names {
            u8"tools",
            u8"play",
            u8"debug-start",
            u8"terminal",
            u8"beaker",
            u8"rocket",
            u8"package",
            u8"bug",
            u8"build",
            u8"code",
            u8"window",
            u8"folder-opened",
            u8"file-code",
            u8"sync",
            u8"cloud-upload",
            u8"cloud-download",
            u8"trash",
            u8"gear",
            u8"link-external",
            u8"run-all",
        };

        // 아이콘 목록의 화면 읽기 이름이다. `function_names`와 같은 순서다 — 영문 codicon 이름 대신 뜻을 읽는다.
        constexpr std::array<std::u8string_view, function_icon_count> function_labels {
            u8"도구",
            u8"재생",
            u8"디버그 시작",
            u8"터미널",
            u8"비커",
            u8"로켓",
            u8"꾸러미",
            u8"벌레",
            u8"빌드",
            u8"코드",
            u8"창",
            u8"열린 폴더",
            u8"코드 파일",
            u8"동기화",
            u8"클라우드 올리기",
            u8"클라우드 내려받기",
            u8"휴지통",
            u8"톱니바퀴",
            u8"외부 링크",
            u8"모두 실행",
        };

        // 이름 표가 글리프 표의 앞 20개와 같은지다. 두 표가 갈리면 목록의 아이콘이 기본 `play`로 떨어진다.
        consteval bool function_names_lead_icons()
        {
            for (std::size_t index = 0; index < function_icon_count; ++index)
            {
                if (function_names[index] != icons[index].name)
                    return false;
            }
            return true;
        }
        static_assert(function_names_lead_icons());
        // 편집 dialog의 배치(아이콘 줄 수)가 쓰는 개수와 같아야 한다.
        static_assert(function_icon_count == function_editor_icon_count);

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

    std::span<const std::u8string_view> function_icon_names() noexcept
    {
        return function_names;
    }

    std::u8string_view function_icon_label(const std::u8string_view name) noexcept
    {
        for (std::size_t index = 0; index < function_icon_count; ++index)
        {
            if (function_names[index] == name)
                return function_labels[index];
        }
        return name;
    }

    char32_t function_icon_glyph(const std::u8string_view name) noexcept
    {
        // D16의 20개만 찾는다. 메뉴 전용 이름(`stop-circle`, `ellipsis` 등)을 `icon`에 쓰면 쉬는 기능이 중지 버튼이나
        // `⋯`처럼 보이므로 모르는 이름처럼 `play`다.
        const char32_t glyph { find_glyph(std::span { icons }.first(function_icon_count), name) };
        return glyph != 0 ? glyph : luil::codicons::icon_play;
    }
}

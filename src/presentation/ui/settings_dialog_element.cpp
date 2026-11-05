#include "presentation/ui/settings_dialog_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/dialog_host.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/label_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/scroll_area_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/theme_picker_element.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gitman::ui {
    namespace {
        constexpr float panel_width { 800.0f };
        constexpr float panel_height { 680.0f };
        constexpr float padding { 20.0f };
        constexpr float gap { 12.0f };
        constexpr float card_padding { 14.0f };
        constexpr float card_gap { 8.0f };
        constexpr float control_height { 32.0f };

        float line_height()
        {
            return 24.0f + text_growth(13.0f);
        }

        float header_height()
        {
            return 34.0f + text_growth(20.0f);
        }

        struct settings_chrome
        {
            float inset { 20.0f };
            float spacing { 12.0f };
            float title { 34.0f };
            float scope { 32.0f };
            float description { 48.0f };
            float tabs { 36.0f };
            float footer { 32.0f };

            [[nodiscard]] float height() const noexcept
            {
                return inset * 2.0f + title + scope + description + tabs + footer + spacing * (description > 0.0f ? 5.0f : 4.0f);
            }
        };

        settings_chrome chrome_for(const float window_height)
        {
            if (window_height < 300.0f)
                return { 8.0f, 6.0f, 26.0f + text_growth(18.0f), 28.0f, 0.0f, 28.0f, 32.0f };
            if (window_height < 560.0f)
                return { 12.0f, 8.0f, 30.0f + text_growth(18.0f), 30.0f, 0.0f, 32.0f, 32.0f };
            return { padding, gap, header_height(), 32.0f, line_height() * 2.0f, 36.0f, control_height };
        }

        std::unique_ptr<luil::stack_element> stack(const std::u8string_view owner, const bool row = false, const float spacing = card_gap, const float inset = 0.0f)
        {
            return std::make_unique<luil::stack_element>(ui_element_id { kind_settings_dialog_panel, std::u8string { owner } }
                , luil::stack_config { .direction = row ? luil::stack_direction::row : luil::stack_direction::column, .spacing = spacing, .padding = luil::edge_insets::all(inset) }
            );
        }

        std::unique_ptr<luil::label_element> label(const std::u8string_view owner, std::u8string text, const float size = 12.0f, const bool dim = true, const bool wrap = false)
        {
            return std::make_unique<luil::label_element>(ui_element_id { kind_settings_dialog_panel, std::u8string { owner } }
                , luil::label_config { .text = std::move(text), .font_size = size, .color = dim ? luil::label_color_role::dim : luil::label_color_role::primary, .wrap = wrap, .max_lines = 2 }
            );
        }

        std::unique_ptr<luil::text_button_element> button(const ui_element_kind kind, const std::u8string_view text)
        {
            return std::make_unique<luil::text_button_element>(ui_element_id { kind }, luil::text_button_config { .text = std::u8string { text } });
        }

        struct field_card
        {
            std::u8string owner {};
            std::unique_ptr<luil::stack_element> content {};
            bool overridden { false };
            float height { card_padding * 2.0f };

            void add(std::unique_ptr<ui_element> element, const float length)
            {
                if (content->children().empty() == false)
                    height += card_gap;
                height += length;
                content->add(std::move(element), length);
            }
        };

        field_card card(const settings_dialog_view& view, const std::u8string_view owner, const std::u8string_view title, const std::optional<settings_override_field> field, const bool follows_app)
        {
            field_card result { std::u8string { owner }, stack(owner, false, card_gap, card_padding), view.document_mode && field.has_value() && follows_app == false };
            result.add(label(std::u8string { owner } + u8"-title", std::u8string { title }, 14.0f, false), line_height());
            if (view.document_mode && field.has_value())
            {
                auto scope { stack(std::u8string { owner } + u8"-scope", true) };
                scope->add_flexible(label(std::u8string { owner } + u8"-source", follows_app ? u8"앱 기본값 사용 중" : u8"이 작업공간에서 변경함"));
                if (follows_app == false)
                {
                    const std::u8string_view short_name {
                        owner == u8"git" ? u8"Git" : owner == u8"svn" ? u8"SVN" : owner == u8"timeout" ? u8"제한 시간"
                            : owner == u8"submodules" ? u8"하위 모듈" : owner == u8"ignore-local" ? u8"SVN 조회" : owner == u8"log-files" ? u8"파일 로그"
                            : owner == u8"theme" ? u8"테마" : owner == u8"accent" ? u8"강조색" : owner == u8"ui-font" ? u8"화면 글꼴" : u8"로그 글꼴"
                    };
                    auto reset { std::make_unique<luil::text_button_element>(ui_element_id { kind_settings_override_badge, std::u8string { owner } }, luil::text_button_config { .text = std::u8string { short_name } + u8" 기본값 사용" }) };
                    reset->set_tooltip(std::u8string { title } + u8" 항목을 앱 기본값으로 되돌립니다");
                    reset->set_action(ui_trigger::left_click, make_logic_click(clear_settings_override_intent { *field }));
                    scope->add_fit(std::move(reset));
                }
                result.add(std::move(scope), control_height);
            }
            return result;
        }

        luil::theme_picker_config theme_config(const settings_dialog_view& view)
        {
            luil::theme_picker_config config {};
            config.owner = u8"settings-theme";
            config.selected = view.theme;
            config.name = u8"화면 테마";
            config.labels = { u8"밝게", u8"시스템", u8"어둡게" };
            config.accent_id = view.accent_id;
            config.select = [](const theme_preference value)
            {
                return make_logic_action(set_theme_preference_intent { value });
            };
            return config;
        }

        luil::accent_picker_config accent_config(const settings_dialog_view& view)
        {
            luil::accent_picker_config config {};
            config.owner = u8"settings-accent";
            config.selected = view.accent_id;
            config.name = u8"강조색";
            config.select = [](const std::u8string& value)
            {
                return make_logic_action(set_accent_intent { value });
            };
            return config;
        }

        struct settings_fields
        {
            std::unique_ptr<luil::stack_element> content { stack(u8"settings-fields", false, gap) };
            float height { 0.0f };

            void add(field_card field)
            {
                auto panel { std::make_unique<luil::panel_element>(ui_element_id { kind_settings_dialog_panel, field.owner + u8"-card" }
                    , luil::panel_config {
                        .background = [accent = field.overridden](const ui_color_palette& palette)
                        {
                            return luil::with_alpha(accent ? palette.accent : palette.primary_foreground, accent ? 0.07f : 0.035f);
                        },
                        .corner_radius = 8.0f,
                        .border = [accent = field.overridden](const ui_color_palette& palette)
                        {
                            return accent ? luil::with_alpha(palette.accent, 0.5f) : palette.divider;
                        },
                    }
                )
                };
                panel->set_content(std::move(field.content));
                if (height > 0.0f)
                    height += gap;
                height += field.height;
                content->add(std::move(panel), field.height);
            }
        };

        settings_fields build_fields(const settings_dialog_view& view, const float width)
        {
            settings_fields fields {};
            const float inner_width { std::max(100.0f, width - card_padding * 2.0f) };
            const auto note = [](field_card& field, const std::u8string_view text)
            {
                field.add(label(field.owner + u8"-description", std::u8string { text }, 12.0f, true, true), line_height() * 2.0f);
            };
            if (view.active_tab == settings_tab::tools)
            {
                const auto executable = [&](const bool git)
                {
                    const std::u8string_view owner { git ? u8"git" : u8"svn" };
                    const auto tool { git ? repository_kind::git : repository_kind::subversion };
                    auto field { card(view, owner, git ? u8"Git 실행 파일" : u8"SVN 실행 파일", git ? settings_override_field::git_executable : settings_override_field::svn_executable, git ? view.git_follows_app : view.svn_follows_app) };
                    const std::u8string& path { git ? view.git_path : view.svn_path };
                    auto path_label { label(std::u8string { owner } + u8"-path", path.empty() ? u8"PATH에서 자동으로 찾기" : path, 12.0f, path.empty()) };
                    path_label->set_tooltip(path);
                    field.add(std::move(path_label), line_height());
                    auto actions { stack(std::u8string { owner } + u8"-actions", true) };
                    auto browse { button(git ? kind_settings_git_browse : kind_settings_svn_browse, git ? u8"Git 찾아보기…" : u8"SVN 찾아보기…") };
                    browse->set_action(ui_trigger::left_click, make_app_command_click(git ? app_command::show_git_executable_picker : app_command::show_svn_executable_picker));
                    actions->add_fit(std::move(browse));
                    auto clear { button(git ? kind_settings_git_clear : kind_settings_svn_clear, git ? u8"Git 자동 탐색" : u8"SVN 자동 탐색") };
                    clear->set_enabled(path.empty() == false);
                    clear->set_action(ui_trigger::left_click, make_logic_click(clear_settings_executable_intent { tool }));
                    actions->add_fit(std::move(clear));
                    actions->add_flexible_gap();
                    field.add(std::move(actions), control_height);
                    note(field, (git ? view.git_unused : view.svn_unused) ? u8"작업공간이 지정한 경로를 아직 신뢰하지 않아 사용하지 않습니다. 직접 고르면 저장 후 사용합니다." : u8"설치된 도구를 자동으로 찾습니다. 특정 버전이 필요할 때 실행 파일을 지정하세요.");
                    fields.add(std::move(field));
                };
                executable(true);
                executable(false);
                auto field { card(view, u8"timeout", u8"상태 확인 제한 시간", settings_override_field::query_timeout, view.timeout_follows_app) };
                auto input { std::make_unique<luil::text_input_element>(ui_element_id { kind_settings_timeout_input }, view.timeout, luil::text_input_config { .placeholder = u8"600", .error = view.timeout_error }) };
                input->set_access_name(u8"상태 확인 제한 시간 (초)");
                input->set_tooltip(u8"10~3600초. 비우면 기본값 600초입니다.");
                field.add(std::move(input), control_height + luil::text_input_error_height);
                note(field, u8"초 단위, 10~3600. 비우면 600초입니다. 느린 저장소의 상태 조회가 시간 초과될 때 늘리세요.");
                fields.add(std::move(field));
            }
            else if (view.active_tab == settings_tab::operations)
            {
                const auto toggle = [&](const std::u8string_view owner, const std::u8string_view title, const settings_override_field key, const bool follows, const bool on, logic_message message, const std::u8string_view description)
                {
                    const std::u8string_view heading { owner == u8"submodules" ? u8"Git 업데이트" : owner == u8"ignore-local" ? u8"SVN 상태 조회" : u8"로그 보관" };
                    auto field { card(view, owner, heading, key, follows) };
                    luil::check_config config {};
                    config.owner = u8"settings-" + std::u8string { owner };
                    config.label = title;
                    config.checked = on;
                    config.toggle = make_logic_click(std::move(message));
                    auto check { std::make_unique<luil::check_element>(std::move(config)) };
                    check->set_access_name(std::u8string { title });
                    field.add(std::move(check), luil::check_row_height);
                    note(field, description);
                    fields.add(std::move(field));
                };
                toggle(u8"submodules", u8"Git 하위 모듈 함께 업데이트", settings_override_field::update_submodules, view.submodules_follows_app, view.update_submodules, toggle_settings_submodules_intent {}, u8"Git 업데이트 시 하위 모듈도 재귀적으로 갱신합니다. 끄면 저장소 본체만 업데이트합니다.");
                toggle(u8"ignore-local", u8"SVN 로컬 변경 조회 생략", settings_override_field::ignore_local_changes, view.ignore_local_follows_app, view.ignore_local_changes, toggle_settings_ignore_local_intent {}, u8"SVN의 로컬 수정·충돌 조회를 생략합니다. 변경 상태를 확인하지 않고 진행하므로 주의하세요. Git에는 적용하지 않습니다.");
                toggle(u8"log-files", u8"작업 로그를 파일로 보관", settings_override_field::write_log_files, view.log_files_follows_app, view.write_log_files, toggle_settings_log_files_intent {}, u8"작업공간 폴더에 저장소별 로그를 남깁니다. 끄더라도 화면 로그는 계속 볼 수 있습니다.");
            }
            else if (view.active_tab == settings_tab::appearance)
            {
                auto theme { card(view, u8"theme", u8"화면 테마", settings_override_field::theme, view.theme_follows_app) };
                const auto themes { theme_config(view) };
                theme.add(std::make_unique<luil::theme_picker_element>(themes), luil::theme_picker_element::height_for(themes, inner_width));
                fields.add(std::move(theme));
                auto accent { card(view, u8"accent", u8"강조색", settings_override_field::accent, view.accent_follows_app) };
                const auto accents { accent_config(view) };
                accent.add(std::make_unique<luil::accent_picker_element>(accents), luil::accent_picker_element::height_for(accents, inner_width));
                fields.add(std::move(accent));
                for (const bool code : { false, true })
                {
                    auto font { card(view, code ? u8"code-font" : u8"ui-font", code ? u8"로그·차이 보기 글꼴" : u8"화면 글꼴", code ? settings_override_field::code_font : settings_override_field::ui_font, code ? view.code_font_follows_app : view.ui_font_follows_app) };
                    auto row { stack(code ? u8"code-font-row" : u8"ui-font-row", true) };
                    const std::u8string& family { code ? view.code_font_family : view.ui_font_family };
                    row->add_flexible(label(code ? u8"code-font-value" : u8"ui-font-value", family.empty() ? u8"기본 글꼴" : family, 13.0f, false));
                    auto pick { button(code ? kind_settings_code_font_pick : kind_settings_ui_font_pick, code ? u8"로그 글꼴 선택…" : u8"화면 글꼴 선택…") };
                    pick->set_action(ui_trigger::left_click, make_logic_click(open_font_dialog_intent { code }));
                    row->add_fit(std::move(pick));
                    font.add(std::move(row), control_height);
                    fields.add(std::move(font));
                }
            }
            else
            {
                auto field { card(view, u8"association", u8"작업공간 파일 연결", std::nullopt, false) };
                note(field, u8".version-list 파일을 더블클릭하면 Gitman으로 엽니다. 현재 Windows 사용자에게 즉시 적용됩니다.");
                auto actions { stack(u8"association-actions", true) };
                auto associate { button(kind_settings_associate, u8"연결 등록") };
                associate->set_action(ui_trigger::left_click, make_app_command_click(app_command::register_file_association));
                actions->add_fit(std::move(associate));
                auto dissociate { button(kind_settings_dissociate, u8"연결 해제") };
                dissociate->set_action(ui_trigger::left_click, make_app_command_click(app_command::unregister_file_association));
                actions->add_fit(std::move(dissociate));
                actions->add_flexible_gap();
                field.add(std::move(actions), control_height);
                fields.add(std::move(field));
            }
            return fields;
        }

        float available_width(const float window_width)
        {
            return std::min(panel_width, std::max(240.0f, window_width - 40.0f));
        }

        float available_height(const float window_height)
        {
            return std::min(panel_height, dialog_available_height(window_height));
        }
    }

    settings_dialog_scroll_metrics settings_dialog_metrics(const settings_dialog_view& dialog, const float window_height, const float window_width)
    {
        settings_dialog_scroll_metrics metrics {};
        metrics.viewport_height = std::max(0.0f, available_height(window_height) - chrome_for(window_height).height());
        metrics.content_height = build_fields(dialog, available_width(window_width) - chrome_for(window_height).inset * 2.0f - luil::scroll_area_bar_width).height;
        metrics.panel_content_height = metrics.content_height;
        metrics.maximum_scroll = std::max(0.0f, metrics.content_height - metrics.viewport_height);
        return metrics;
    }

    settings_dialog_element::settings_dialog_element(settings_dialog_view view, const float window_height, const float window_width)
        : ui_element { ui_element_id { kind_settings_dialog } }
    {
        const settings_chrome chrome { chrome_for(window_height) };
        const bool compact { chrome.description == 0.0f };
        auto root { stack(u8"settings-root", false, chrome.spacing, chrome.inset) };
        root->add(label(u8"settings-title", u8"환경설정", compact ? 18.0f : 20.0f, false), chrome.title);
        luil::choice_group_config scopes {};
        scopes.owner = u8"settings-scope";
        scopes.style = luil::choice_style::toggle;
        scopes.items = { { u8"app", u8"앱 기본값" } };
        if (view.document_available)
            scopes.items.push_back({ u8"workspace", u8"이 작업공간" });
        scopes.selected = view.document_mode ? u8"workspace" : u8"app";
        scopes.select = [](const std::u8string& value)
        {
            return make_logic_action(select_settings_scope_intent { value == u8"workspace" });
        };
        scopes.name = u8"설정 적용 범위";
        root->add(std::make_unique<luil::choice_group_element>(std::move(scopes)), chrome.scope);
        if (compact == false)
            root->add(label(u8"settings-scope-description", view.document_mode ? view.workspace_name + u8"에 적용합니다. 변경하지 않은 항목은 앱 기본값을 따릅니다." : u8"모든 작업공간의 기본값입니다. 작업공간에서 따로 정한 값은 유지됩니다.", 12.0f, true, true), chrome.description);
        luil::tab_bar_config tabs {};
        tabs.owner = u8"settings-categories";
        tabs.items = { { u8"tools", u8"도구" }, { u8"operations", u8"작업·로그" }, { u8"appearance", u8"화면·글꼴" }, { u8"system", u8"시스템" } };
        tabs.selected = settings_tab_name(view.active_tab);
        tabs.tab_width = 120.0f;
        tabs.min_tab_width = 76.0f;
        tabs.select = [](const std::u8string& value)
        {
            settings_tab tab { settings_tab::tools };
            for (const auto candidate : { settings_tab::operations, settings_tab::appearance, settings_tab::system })
            {
                if (settings_tab_name(candidate) == value)
                    tab = candidate;
            }
            return make_logic_action(select_settings_tab_intent { tab });
        };
        root->add(std::make_unique<luil::tab_bar_element>(std::move(tabs)), chrome.tabs);
        const auto metrics { settings_dialog_metrics(view, window_height, window_width) };
        auto fields { build_fields(view, available_width(window_width) - chrome_for(window_height).inset * 2.0f - luil::scroll_area_bar_width) };
        luil::scroll_area_config scroll {};
        scroll.owner = u8"settings";
        scroll.content_height = metrics.content_height;
        scroll.scroll_offset = view.scroll_offset;
        scroll.scroll = [](const float delta)
        {
            return make_logic_action(settings_scroll_intent { delta });
        };
        scroll.scroll_to = [](const float offset)
        {
            return make_logic_action(settings_scroll_intent { 0.0f, offset });
        };
        auto area { std::make_unique<luil::scroll_area_element>(std::move(scroll)) };
        area->set_content(std::move(fields.content));
        area->set_access_name(u8"환경설정 항목");
        root->add_flexible(std::move(area));
        auto footer { stack(u8"settings-footer", true, gap) };
        auto status { label(u8"settings-message", view.message.empty() ? (view.document_available ? u8"저장하면 두 범위의 변경을 함께 적용합니다." : u8"저장하면 앱 기본값에 적용합니다.") : view.message, 12.0f, true, true) };
        if (view.message.empty() == false)
            status->set_live_region(luil::access_live::assertive);
        footer->add_flexible(std::move(status));
        auto cancel { button(kind_settings_dialog_cancel, u8"취소") };
        cancel->set_action(ui_trigger::left_click, make_logic_click(cancel_settings_dialog_intent {}));
        footer->add_fit(std::move(cancel));
        auto save { std::make_unique<luil::text_button_element>(ui_element_id { kind_settings_dialog_confirm }, luil::text_button_config { .text = u8"저장", .visual = luil::text_button_visual::accent, .default_button = true }) };
        save->set_enabled(view.can_confirm);
        save->set_action(ui_trigger::left_click, make_logic_click(confirm_settings_intent {}));
        footer->add_fit(std::move(save));
        root->add(std::move(footer), chrome.footer);
        auto surface { std::make_unique<luil::panel_element>(ui_element_id { kind_settings_dialog_panel }, luil::panel_config { .corner_radius = 12.0f }) };
        surface->set_content(std::move(root));
        content_ = surface.get();
        add_child(std::move(surface));
    }

    void settings_dialog_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        content_->arrange(context.for_child(context.slot));
    }

    void settings_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    std::unique_ptr<luil::modal_host_element> make_settings_dialog(const settings_dialog_view& view, const float window_width, const float window_height)
    {
        dialog_host_spec spec {};
        spec.owner = dialog_owner_settings;
        spec.name = view.document_mode ? u8"환경설정 · 이 작업공간" : u8"환경설정 · 앱 기본값";
        spec.content_width = available_width(window_width);
        spec.content_height = available_height(window_height);
        // 두 범위의 초안은 명시적인 취소 또는 Esc에서만 버린다.
        spec.dismiss = make_logic_click(cancel_settings_dialog_intent {});
        return make_dialog_host(std::move(spec), std::make_unique<settings_dialog_element>(view, window_height, window_width));
    }
}

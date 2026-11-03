#include "application/logic_controller.h"

#include "domain/app_settings.h"
#include "domain/function_editor.h"
#include "domain/function_trust.h"
#include "presentation/function_editor_layout.h"
#include "presentation/list_metrics.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 기능 편집 dialog(function-buttons-design 2.7, 4.11, 6.1 T12)의 logic이다. 상태는 `logic_controller`가 들고, 이 파일은
// 그 멤버 함수만 둔다 — `logic_controller.cpp`가 이미 커서 dialog 하나를 따로 읽을 수 있게 나눴다.
namespace gitman {
    namespace {
        constexpr std::u8string_view scope_all { u8"all" };
        constexpr std::u8string_view scope_git { u8"git" };
        constexpr std::u8string_view scope_svn { u8"svn" };
        constexpr std::u8string_view scope_projects { u8"projects" };
        constexpr std::u8string_view new_function_label { u8"새 기능" };
        constexpr std::u8string_view copy_suffix { u8" (복사)" };
        constexpr std::u8string_view card_full_message { u8"카드에는 3개까지 둡니다. 다른 기능을 카드에서 내린 뒤 고르십시오." };
        constexpr std::u8string_view copy_lowered_message { u8"카드에는 3개까지 둡니다. 복사본은 메뉴에만 둡니다." };
        constexpr std::u8string_view arguments_hint { u8"공백으로 나눕니다. 공백이 든 값은 큰따옴표로 감쌉니다. 변수는 나눈 뒤에 풉니다." };
        constexpr std::u8string_view unterminated_message { u8"인자에 닫히지 않은 큰따옴표가 있습니다." };

        [[nodiscard]] text_edit_state text_state(const std::u8string_view text)
        {
            text_edit_state state {};
            text_edit_set_text(state, text, true);
            return state;
        }

        [[nodiscard]] std::u8string quoted_label(const std::u8string_view text)
        {
            std::u8string value { u8"\"" };
            value.append(text);
            value.push_back(u8'"');
            return value;
        }

        // 적용 범위 선택 묶음의 값이다. `project_ids`가 있으면 `vcs`가 함께 있어도 `저장소 목록`이다 — `vcs`는 사용자가 묶음을
        // 바꾸기 전까지 그대로 남는다.
        [[nodiscard]] std::u8string_view scope_value(const function_button& button) noexcept
        {
            if (button.project_ids.has_value())
                return scope_projects;
            switch (button.vcs)
            {
            case function_vcs_scope::git:
                return scope_git;
            case function_vcs_scope::subversion:
                return scope_svn;
            case function_vcs_scope::any:
                break;
            }
            return scope_all;
        }

        [[nodiscard]] std::u8string_view card_name(const project_definition& project) noexcept
        {
            return project.display_name.empty() ? std::u8string_view { project.id.value } : std::u8string_view { project.display_name };
        }

        // 경로의 마지막 구성 요소다. 제목에 문서 파일 이름을 보인다.
        [[nodiscard]] std::u8string_view file_name_of(const std::u8string_view path) noexcept
        {
            const std::size_t separator { path.find_last_of(u8"\\/") };
            return separator == std::u8string_view::npos ? path : path.substr(separator + 1);
        }

        // 경고 행의 이름이다. 진단 글은 `위치: 사유`이고 위치(`function_buttons.buttons[N] ("빌드")`)에 label이 있으면 그것이고,
        // 없으면 순번이다. label 안에 `: `나 `")`가 있어도 되게 위치 머리 뒤 첫 ` ("`부터 마지막 `"): `까지를 label로 본다 — 사유
        // 문구에는 `"): `가 없다.
        [[nodiscard]] std::u8string warning_row_label(const std::u8string_view problem, const std::size_t index)
        {
            constexpr std::u8string_view open { u8" (\"" };
            constexpr std::u8string_view close { u8"\"): " };
            const std::size_t start { problem.starts_with(u8"function_buttons.buttons[") ? problem.find(open) : std::u8string_view::npos };
            const std::size_t end { problem.rfind(close) };
            if (start != std::u8string_view::npos && end != std::u8string_view::npos && end >= start + open.size() && problem.find(u8": ") > start)
            {
                const std::size_t begin { start + open.size() };
                return std::u8string { problem.substr(begin, end - begin) };
            }
            std::u8string label { u8"항목 " };
            label.append(function_decimal_text(index + 1));
            return label;
        }

        // 미리보기의 경고 줄이다. 미리보기는 `${env:}`를 늘 정의된 것으로 보므로 env 미정의는 오지 않는다.
        [[nodiscard]] std::u8string expansion_warning(const std::u8string_view field, const expansion_error& error, const bool queried)
        {
            std::u8string text { field };
            text.append(u8": ");
            switch (error.kind)
            {
            case expansion_error_kind::unterminated:
                text.append(u8"${가 닫히지 않았습니다");
                return text;
            case expansion_error_kind::empty_name:
                text.append(u8"변수 이름이 비었습니다");
                return text;
            case expansion_error_kind::undefined:
                text.append(u8"이 저장소에 정의되지 않은 변수입니다: ");
                break;
            case expansion_error_kind::unresolved:
                text.append(queried ? u8"지금 값을 풀 수 없는 변수입니다: " : u8"저장소를 조회한 뒤에 풀리는 변수입니다: ");
                break;
            case expansion_error_kind::environment_undefined:
                text.append(u8"정의되지 않은 환경 변수입니다: ${env:");
                text.append(error.name);
                text.push_back(u8'}');
                return text;
            }
            text.append(u8"${");
            text.append(error.name);
            text.push_back(u8'}');
            return text;
        }

        // 실행 중인 카드가 그 기능의 적용 범위 안인지다 (4.11). `project_ids`와, 종류를 아는 카드의 `vcs`를 본다. `vcs_hint:
        // auto`이고 조회 전이면 범위 밖이라고 단정할 수 없어 안으로 본다(그 카드에서 버튼이 보이되 비활성이다, B4).
        [[nodiscard]] bool scope_includes(const project_definition& project, const bool queried, const repository_kind queried_kind, const function_button& button) noexcept
        {
            if (button.project_ids.has_value() && std::ranges::find(*button.project_ids, project.id.value) == button.project_ids->end())
                return false;
            if (button.vcs == function_vcs_scope::any)
                return true;
            repository_kind kind { repository_kind::unknown };
            if (project.hint == vcs_hint::git)
                kind = repository_kind::git;
            else if (project.hint == vcs_hint::subversion)
                kind = repository_kind::subversion;
            else if (queried)
                kind = queried_kind;
            if (kind == repository_kind::unknown)
                return true;
            return kind == (button.vcs == function_vcs_scope::git ? repository_kind::git : repository_kind::subversion);
        }
    }

    bool logic_controller::is_function_editor_target(const text_input_target target) noexcept
    {
        switch (target)
        {
        case text_input_target::function_editor_label:
        case text_input_target::function_editor_program:
        case text_input_target::function_editor_arguments:
        case text_input_target::function_editor_working_directory:
        case text_input_target::function_editor_timeout:
            return true;
        default:
            break;
        }
        return false;
    }

    text_edit_state* logic_controller::function_editor_text(const text_input_target target) noexcept
    {
        return const_cast<text_edit_state*>(std::as_const(*this).function_editor_text(target));
    }

    const text_edit_state* logic_controller::function_editor_text(const text_input_target target) const noexcept
    {
        if (function_editor_.has_value() == false || function_editor_->tab != function_editor_tab::functions || function_editor_->selected.has_value() == false)
            return nullptr;
        if (*function_editor_->selected >= function_editor_->entries.size())
            return nullptr;
        const function_editor_draft& draft { function_editor_->entries[*function_editor_->selected] };
        if (draft.source.has_value())
            return nullptr;
        switch (target)
        {
        case text_input_target::function_editor_label:
            return &draft.label;
        case text_input_target::function_editor_program:
            return &draft.program;
        case text_input_target::function_editor_arguments:
            return &draft.arguments;
        case text_input_target::function_editor_working_directory:
            return &draft.working_directory;
        case text_input_target::function_editor_timeout:
            return &draft.timeout;
        default:
            break;
        }
        return nullptr;
    }

    std::optional<function_button> logic_controller::function_editor_button(const function_editor_draft& draft, std::u8string* const error)
    {
        if (draft.source.has_value())
            return std::nullopt;

        function_button button { draft.edited };
        button.label = draft.label.text;
        button.program = draft.program.text;
        // 탐색기의 "경로로 복사"는 경로를 큰따옴표로 감싼다. program은 인자 한 줄이 아니라 경로 하나라 감싼 따옴표만 벗긴다.
        // 처음 글 그대로면 원래 값이다 (원래 program은 파서를 통과한 값이다).
        if (draft.program.text != draft.original.program)
        {
            const std::u8string_view program { button.program };
            if (program.size() >= 2 && program.front() == u8'"' && program.back() == u8'"' && program.substr(1, program.size() - 2).find(u8'"') == std::u8string_view::npos)
                button.program = std::u8string { program.substr(1, program.size() - 2) };
        }

        // 칸의 글이 처음과 같으면 원래 배열을 쓴다 — 다시 나눈 결과가 같더라도 원래 표기를 지킨다.
        if (draft.arguments.text != draft.initial_arguments)
        {
            function_argument_split split { split_function_arguments(draft.arguments.text) };
            if (split.unterminated_quote)
            {
                if (error != nullptr)
                    *error = std::u8string { unterminated_message };
                return std::nullopt;
            }
            button.args = std::move(split.arguments);
        }
        else
            button.args = draft.original.args;

        // 작업 폴더를 비우면 생략(기본 `${projectRoot}`)이다. 처음 글 그대로면 원래 값(생략 포함)이다.
        if (draft.working_directory.text != draft.initial_working_directory)
            button.cwd = draft.working_directory.text.empty() ? std::nullopt : std::optional<std::u8string> { draft.working_directory.text };
        else
            button.cwd = draft.original.cwd;

        // 제한 시간 칸은 숫자만 받는다. 비우면 기본값이다. 범위는 규칙 함수가 본다.
        if (draft.timeout.text != draft.initial_timeout)
        {
            std::int64_t seconds { 0 };
            for (const char8_t value : draft.timeout.text)
                seconds = seconds * 10 + (value - u8'0');
            button.timeout_seconds = draft.timeout.text.empty() ? default_function_timeout_seconds : static_cast<std::int32_t>(std::min<std::int64_t>(seconds, maximum_function_timeout_seconds + 1));
        }
        else
            button.timeout_seconds = draft.original.timeout_seconds;
        return button;
    }

    std::vector<function_button_entry> logic_controller::function_editor_entries() const
    {
        std::vector<function_button_entry> entries {};
        if (function_editor_.has_value() == false)
            return entries;
        for (const function_editor_draft& draft : function_editor_->entries)
        {
            if (draft.source.has_value())
                entries.emplace_back(*draft.source);
            else
                entries.emplace_back(draft.edited);
        }
        return entries;
    }

    void logic_controller::handle_open_function_editor(const open_function_editor_intent& intent)
    {
        // 다시 읽기 결과를 기다리는 동안은 열지 않는다 (메뉴 항목도 비활성이다). 다시 읽기가 성공하면 초안을 버린다.
        if (shutting_down_ || document_.has_value() == false || pending_reload_operation_id_ != 0 || reload_after_save_)
            return;
        context_menu_.reset();

        function_editor_state state {};
        // 실행 중인 기능의 id는 새 id가 피한다. 실행 중에 지우면 Apply가 거절하지만 같은 id의 새 버튼이 그 검사를 비켜 간다.
        for (const card_state& card : cards_)
        {
            for (const function_run_state& run : card.function_runs)
                state.reserved_ids.push_back(run.button_id);
        }
        if (document_->function_buttons.has_value())
        {
            const std::vector<function_button_entry>& buttons { document_->function_buttons->buttons };
            for (std::size_t index = 0; index < buttons.size(); ++index)
            {
                function_editor_draft draft {};
                draft.source_key = index;
                if (const function_button* const button { std::get_if<function_button>(&buttons[index]) }; button != nullptr)
                {
                    state.reserved_ids.push_back(button->id);
                    draft.original = *button;
                    draft.edited = *button;
                    draft.label = text_state(button->label);
                    draft.program = text_state(button->program);
                    draft.initial_arguments = join_function_arguments(button->args);
                    draft.arguments = text_state(draft.initial_arguments);
                    draft.initial_working_directory = button->cwd.value_or(std::u8string {});
                    draft.working_directory = text_state(draft.initial_working_directory);
                    draft.initial_timeout = function_decimal_text(static_cast<std::size_t>(button->timeout_seconds < 0 ? 0 : button->timeout_seconds));
                    draft.timeout = text_state(draft.initial_timeout);
                    if (intent.button_id.empty() == false && button->id == intent.button_id)
                        state.selected = index;
                }
                else
                {
                    draft.source = std::get<function_source_text>(buttons[index]);
                    draft.problem = index < function_button_problems_.size() && function_button_problems_[index].empty() == false ? function_button_problems_[index] : std::u8string { u8"검증에 실패한 항목입니다. 원문을 그대로 둡니다." };
                }
                state.entries.push_back(std::move(draft));
            }
        }
        if (state.selected.has_value() == false && state.entries.empty() == false)
            state.selected = std::size_t { 0 };

        // 미리보기는 선택 카드에서 시작한다. 없으면 첫 카드다.
        for (std::size_t index = 0; index < cards_.size(); ++index)
        {
            if (selected_.has_value() && cards_[index].project.id == *selected_)
                state.preview_card = index;
        }
        function_editor_ = std::move(state);
        composition_.reset();

        // 배치 메뉴에서 연 기능이 목록 아래쪽이면 보이게 둔다.
        if (function_editor_->selected.has_value())
            handle_select_function_editor_entry(*function_editor_->selected);
    }

    void logic_controller::handle_select_function_editor_tab(const select_function_editor_tab_intent& intent)
    {
        if (function_editor_.has_value() == false || function_editor_->tab == intent.tab)
            return;
        function_editor_->tab = intent.tab;
        function_editor_->list_scroll = 0.0f;
        function_editor_->form_scroll = 0.0f;
        composition_.reset();
    }

    void logic_controller::handle_select_function_editor_entry(const std::size_t index)
    {
        if (function_editor_.has_value() == false || index >= function_editor_->entries.size())
            return;
        if (function_editor_->selected != index)
        {
            function_editor_->form_scroll = 0.0f;
            function_editor_->placement_message.clear();
            // 조합 중인 글은 앞 항목의 칸에 속한다.
            if (composition_.has_value() && is_function_editor_target(composition_->target))
                composition_.reset();
        }
        function_editor_->selected = index;

        // 고른 행이 목록 밖이면 보이게 스크롤한다.
        const function_editor_size size { function_editor_dialog_size(window_width_ / scale_, window_height_ / scale_) };
        const float viewport { function_editor_list_viewport_height(size.height) };
        const float top { static_cast<float>(index) * layout_function_editor_row_height() };
        if (top < function_editor_->list_scroll)
            function_editor_->list_scroll = top;
        else if (top + layout_function_editor_row_height() > function_editor_->list_scroll + viewport)
            function_editor_->list_scroll = top + layout_function_editor_row_height() - viewport;
        if (function_editor_->list_scroll < 0.0f)
            function_editor_->list_scroll = 0.0f;
    }

    void logic_controller::handle_edit_function_editor_list(const function_editor_list_edit edit)
    {
        if (function_editor_.has_value() == false || function_editor_->tab != function_editor_tab::functions)
            return;
        function_editor_state& editor { *function_editor_ };
        editor.message.clear();
        editor.placement_message.clear();

        // 새 id는 초안의 모든 id, 열 때 문서에 있던 id와 실행 중인 기능의 id, 원문 항목에 보이는 id 글을 피한다 (3.9 template, 4.11).
        std::vector<std::u8string> taken { editor.reserved_ids };
        std::vector<function_source_text> sources {};
        for (const function_editor_draft& draft : editor.entries)
        {
            if (draft.source.has_value())
                sources.push_back(*draft.source);
            else
                taken.push_back(draft.edited.id);
        }

        const std::optional<std::size_t> selected { editor.selected };
        switch (edit)
        {
        case function_editor_list_edit::add: {
            // `[+]`로 더한 기능은 배치를 적지 않아 기본 배치 `menu`다 (D22-B). 배치 선택 묶음에서 바로 올린다.
            function_editor_draft draft {};
            draft.original.id = make_function_button_id(u8"function", taken, sources);
            draft.original.label = std::u8string { new_function_label };
            draft.edited = draft.original;
            draft.label = text_state(draft.original.label);
            draft.initial_timeout = function_decimal_text(static_cast<std::size_t>(default_function_timeout_seconds));
            draft.timeout = text_state(draft.initial_timeout);
            editor.entries.push_back(std::move(draft));
            handle_select_function_editor_entry(editor.entries.size() - 1);
            return;
        }
        case function_editor_list_edit::duplicate: {
            if (selected.has_value() == false || editor.entries[*selected].source.has_value())
                return;
            function_editor_draft copy { editor.entries[*selected] };
            const std::u8string id { make_function_button_id(copy.edited.id, taken, sources) };
            copy.original.id = id;
            copy.edited.id = id;
            std::u8string label { copy.label.text };
            label.append(copy_suffix);
            copy.label = text_state(label);
            // 카드가 이미 3개면 `card` 기능의 복사본은 메뉴에 둔다. 그대로 두면 복사본이 원본 바로 뒤에 끼어 뒤쪽 카드
            // 기능을 기능 줄로 밀어내고 `Alt+N`이 바뀐다 — 배치 묶음의 `카드 — 3개까지` 거절과 같은 규칙이다 (2.7).
            const std::vector<std::optional<function_placement>> visible { resolve_visible_placements(function_editor_entries()) };
            const std::size_t cards { static_cast<std::size_t>(std::ranges::count(visible, std::optional<function_placement> { function_placement::card })) };
            const bool card_copy { copy.edited.placement.has_value() && parse_function_placement(*copy.edited.placement) == std::optional<function_placement> { function_placement::card } };
            const bool lowered { card_copy && cards >= maximum_card_function_buttons };
            if (lowered)
                copy.edited.placement.reset();
            editor.entries.insert(editor.entries.begin() + static_cast<std::ptrdiff_t>(*selected + 1), std::move(copy));
            handle_select_function_editor_entry(*selected + 1);
            if (lowered)
                editor.placement_message = std::u8string { copy_lowered_message };
            return;
        }
        case function_editor_list_edit::remove: {
            if (selected.has_value() == false)
                return;
            editor.entries.erase(editor.entries.begin() + static_cast<std::ptrdiff_t>(*selected));
            if (composition_.has_value() && is_function_editor_target(composition_->target))
                composition_.reset();
            editor.form_scroll = 0.0f;
            if (editor.entries.empty())
            {
                editor.selected.reset();
                return;
            }
            editor.selected.reset();
            handle_select_function_editor_entry(*selected < editor.entries.size() ? *selected : editor.entries.size() - 1);
            return;
        }
        case function_editor_list_edit::move_up:
            if (selected.has_value() == false || *selected == 0)
                return;
            std::swap(editor.entries[*selected], editor.entries[*selected - 1]);
            handle_select_function_editor_entry(*selected - 1);
            return;
        case function_editor_list_edit::move_down:
            if (selected.has_value() == false || *selected + 1 >= editor.entries.size())
                return;
            std::swap(editor.entries[*selected], editor.entries[*selected + 1]);
            handle_select_function_editor_entry(*selected + 1);
            return;
        }
    }

    void logic_controller::handle_set_function_editor_field(const set_function_editor_field_intent& intent)
    {
        if (function_editor_.has_value() == false || function_editor_->selected.has_value() == false)
            return;
        function_editor_state& editor { *function_editor_ };
        if (*editor.selected >= editor.entries.size() || editor.entries[*editor.selected].source.has_value())
            return;
        function_editor_draft& draft { editor.entries[*editor.selected] };
        editor.message.clear();

        switch (intent.field)
        {
        case function_editor_field::icon:
            if (intent.value.empty() == false)
                draft.edited.icon = intent.value;
            return;
        case function_editor_field::mode:
            if (const std::optional<function_run_mode> mode { parse_function_run_mode(intent.value) }; mode.has_value())
                draft.edited.mode = *mode;
            return;
        case function_editor_field::scope:
            // 묶음 하나가 `vcs`와 `project_ids` 두 필드를 고른다. 고른 묶음 밖의 필드는 지운다.
            if (intent.value == scope_all)
            {
                draft.edited.vcs = function_vcs_scope::any;
                draft.edited.project_ids.reset();
            }
            else if (intent.value == scope_git || intent.value == scope_svn)
            {
                draft.edited.vcs = intent.value == scope_git ? function_vcs_scope::git : function_vcs_scope::subversion;
                draft.edited.project_ids.reset();
            }
            else if (intent.value == scope_projects)
            {
                draft.edited.vcs = function_vcs_scope::any;
                if (draft.edited.project_ids.has_value() == false)
                    draft.edited.project_ids = std::vector<std::u8string> {};
            }
            return;
        case function_editor_field::project: {
            if (draft.edited.project_ids.has_value() == false)
                draft.edited.project_ids = std::vector<std::u8string> {};
            std::vector<std::u8string>& ids { *draft.edited.project_ids };
            if (const auto found { std::ranges::find(ids, intent.value) }; found != ids.end())
                ids.erase(found);
            else
                ids.push_back(intent.value);
            return;
        }
        case function_editor_field::confirm:
            draft.edited.confirm = draft.edited.confirm == false;
            return;
        case function_editor_field::program:
            // 찾아보기 dialog가 고른 경로다. 파일을 읽지 않고 칸에 넣기만 한다 — 검증은 Apply의 규칙 함수가 한다.
            text_edit_set_text(draft.program, intent.value, true);
            return;
        case function_editor_field::placement: {
            const std::optional<function_placement> placement { parse_function_placement(intent.value) };
            if (placement.has_value() == false)
                return;
            // luil 선택 묶음에는 항목별 비활성이 없다 (`choice_group_element.h`). 문서 전체에 보이는 `card`가 3개이고 이
            // 기능이 그중 하나가 아니면 받지 않고 묶음 아래에 알린다 (2.7).
            if (*placement == function_placement::card)
            {
                const std::vector<std::optional<function_placement>> visible { resolve_visible_placements(function_editor_entries()) };
                const std::size_t cards { static_cast<std::size_t>(std::ranges::count(visible, std::optional<function_placement> { function_placement::card })) };
                if (visible[*editor.selected] != function_placement::card && cards >= maximum_card_function_buttons)
                {
                    editor.placement_message = std::u8string { card_full_message };
                    return;
                }
            }
            editor.placement_message.clear();
            draft.edited.placement = std::u8string { function_placement_name(*placement) };
            return;
        }
        }
    }

    void logic_controller::handle_step_function_editor_preview(const bool forward)
    {
        if (function_editor_.has_value() == false || cards_.empty())
            return;
        const std::size_t count { cards_.size() };
        const std::size_t current { function_editor_->preview_card % count };
        function_editor_->preview_card = forward ? (current + 1) % count : (current + count - 1) % count;
    }

    void logic_controller::handle_function_editor_scroll(const function_editor_scroll_intent& intent)
    {
        if (function_editor_.has_value() == false)
            return;
        // 한계는 표시 모델과 같은 계산이다.
        const function_editor_view view { build_function_editor_view() };
        const function_editor_size size { function_editor_dialog_size(window_width_ / scale_, window_height_ / scale_) };
        float content { 0.0f };
        float viewport { 0.0f };
        if (intent.region == function_editor_scroll_region::list)
        {
            if (view.tab == function_editor_tab::variables)
            {
                content = function_editor_variables_content_height(view.variables.size());
                viewport = function_editor_body_height(size.height);
            }
            else
            {
                content = function_editor_list_content_height(view.rows.size());
                viewport = function_editor_list_viewport_height(size.height);
            }
        }
        else if (view.tab == function_editor_tab::functions)
        {
            content = function_editor_form_content_height(view.editable, view.projects.size(), view.warning_text, view.warning_source, size.width);
            viewport = function_editor_form_viewport_height(size.height, function_editor_collapsed(size.width));
        }
        const float maximum { function_editor_maximum_scroll(content, viewport) };
        float& offset { intent.region == function_editor_scroll_region::list ? function_editor_->list_scroll : function_editor_->form_scroll };
        // 막대 값으로 정한 절대 자리(`intent.offset`)는 지금 자리를 보지 않는다 (S4).
        offset = std::clamp(scroll_target(offset, intent.delta, intent.offset), 0.0f, maximum);
    }

    void logic_controller::handle_apply_function_editor()
    {
        if (function_editor_.has_value() == false || shutting_down_ || document_.has_value() == false)
            return;
        function_editor_state& editor { *function_editor_ };
        if (function_editor_lock_reason_.empty() == false)
        {
            editor.message = function_editor_lock_reason_;
            return;
        }

        // 모델을 바꾸기 **전에** 모든 항목을 파서와 같은 규칙 함수로 검증한다 (2.7). 모델에 잘못된 값이 들어가면 저장
        // 재검증이 이후 모든 저장을 막는다 (`json_project_store.cpp`). 실패하면 그 항목을 고르고 사유를 보인다.
        std::vector<std::u8string> project_ids {};
        for (const project_definition& project : document_->projects)
            project_ids.push_back(project.id.value);
        const function_buttons_definition empty_definition {};
        const function_buttons_definition& current { document_->function_buttons.has_value() ? *document_->function_buttons : empty_definition };
        const std::vector<std::u8string> user_variable_names { function_user_variable_names(current, project_ids) };

        std::vector<function_button_entry> buttons {};
        std::vector<std::u8string> problems {};
        std::vector<std::u8string> ids {};
        for (std::size_t index = 0; index < editor.entries.size(); ++index)
        {
            const function_editor_draft& draft { editor.entries[index] };
            if (draft.source.has_value())
            {
                // 검증에 실패한 항목은 원문째 같은 자리에 남는다 (3.9).
                buttons.emplace_back(*draft.source);
                problems.push_back(draft.problem);
                continue;
            }

            std::u8string error {};
            std::optional<function_button> button { function_editor_button(draft, &error) };
            if (button.has_value() && error.empty())
                error = function_button_problem(*button, user_variable_names);
            if (error.empty() && std::ranges::find(ids, button->id) != ids.end())
                error = u8"같은 id의 기능 버튼이 앞에 있습니다.";
            if (error.empty() == false)
            {
                handle_select_function_editor_entry(index);
                editor.message = quoted_label(draft.label.text.empty() ? draft.edited.id : draft.label.text);
                editor.message.append(u8": ");
                editor.message.append(error);
                return;
            }
            ids.push_back(button->id);
            buttons.emplace_back(std::move(*button));
            problems.emplace_back();
        }

        // 실행 중인 출력 수집 기능을 지우거나 실행 중인 카드를 그 기능의 범위 밖으로 만드는 고침은 거절한다 (4.11). 그러면
        // 그 기능이 카드 열·기능 줄·기능 메뉴 어디에도 없어 중지할 길이 문서 닫기와 종료뿐이다.
        for (const card_state& card : cards_)
        {
            for (const function_run_state& run : card.function_runs)
            {
                if (run.mode != function_run_mode::capture)
                    continue;
                const function_button* edited { nullptr };
                for (const function_button_entry& entry : buttons)
                {
                    const function_button* const button { std::get_if<function_button>(&entry) };
                    if (button != nullptr && button->id == run.button_id)
                        edited = button;
                }
                if (edited != nullptr && scope_includes(card.project, card.has_local_result, card.snapshot.kind, *edited))
                    continue;
                editor.message = quoted_label(run.label);
                editor.message.append(u8"가 ");
                editor.message.append(card_name(card.project));
                editor.message.append(u8"에서 실행 중입니다. 중지한 뒤 고치십시오.");
                return;
            }
        }

        function_buttons_definition definition { current };
        definition.buttons = std::move(buttons);
        function_editor_.reset();
        composition_.reset();
        // 바뀐 것이 없으면 쓰지 않는다. 절이 없던 문서에 빈 절을 만들지 않는다.
        if (document_->function_buttons.has_value() ? *document_->function_buttons == definition : definition.buttons.empty())
            return;

        // 편집 전 정의를 신뢰한 상태였는지 기억한다 (D24-A). 저장이 끝나면 저장한 정의의 해시로 신뢰를 갱신한다 — 편집기를
        // 열었다는 것만으로 남이 만든 다른 버튼까지 신뢰하지 않게, 신뢰하지 않았던 정의는 그대로 둔다.
        remember_function_trust_for_save();
        // 알림 띠의 기능 버튼 경고도 쓴 글을 다시 읽은 진단으로 다시 만든다 (지운 항목·새로 생긴 경고).
        function_notice_after_save_ = true;
        // 저장이 끝나기 전에는 해시 입력이 편집 전 원문이다. 그 해시로 신뢰를 판정·기록하지 않게 실행을 막는다. 문서 변경
        // 표시(`unsaved_.document`)는 아래 `request_save`가 켠다.
        unsaved_.function_definition = true;
        document_->function_buttons = std::move(definition);
        function_button_problems_ = std::move(problems);
        // 모델이 바뀌어 카드 열·기능 줄·`Alt+N` 번호가 바로 다시 매겨진다 (view는 snapshot마다 다시 짓는다).
        request_save();
    }

    void logic_controller::remember_function_trust_for_save()
    {
        const std::u8string digest { current_function_digest() };
        if (digest.empty() == false && is_function_definition_trusted(app_settings_, document_path_, digest))
            function_trust_after_save_ = true;
    }

    void logic_controller::finish_function_editor_save(const document_saved_event& event)
    {
        if (event.revision.has_value() == false)
        {
            // 편집이 실은 저장이 실패했으면 신뢰를 바꾸지 않는다 (4.11). 뒤에 이어 낼 저장이 있으면 그것이 편집을 싣는다.
            if (save_queued_ == false)
            {
                function_trust_after_save_ = false;
                function_notice_after_save_ = false;
            }
            return;
        }

        // 쓴 글을 다시 읽은 해시 입력으로 바꾼다 (3.10, T3·T7에서 넘어온 일). 읽을 때의 값은 모델을 고쳐 저장한 뒤 낡는다.
        if (event.function_digest_input.has_value() && document_.has_value() && document_->function_buttons.has_value())
            document_->function_buttons->source_digest_input = *event.function_digest_input;

        // 이어 낼 저장이 있으면 이 저장은 편집 전 모델일 수 있다. 마지막 저장이 끝난 뒤에 갱신한다.
        if (save_queued_)
            return;
        // 모델이 파일과 같아져 해시 입력도 맞다(`handle_document_saved`가 저장하지 못한 변경 표시를 이미 껐다). 아래 신뢰
        // 갱신이 이 해시를 쓴다.
        // store는 쓴 글을 다시 읽어 그 진단을 저장 결과에 싣는다. 편집으로 고치거나 지운 항목, 새로 생긴 경고(4번째
        // `card` 등)가 띠에 바로 맞춰진다.
        if (function_notice_after_save_)
        {
            function_notice_after_save_ = false;
            refresh_function_button_notice(event.diagnostics);
        }
        if (function_trust_after_save_ == false)
            return;
        function_trust_after_save_ = false;
        const std::u8string digest { current_function_digest() };
        if (document_path_.empty() == false && digest.empty() == false && trust_function_definition(app_settings_, document_path_, digest))
            request_app_settings_save();
    }

    function_editor_view logic_controller::build_function_editor_view() const
    {
        function_editor_view view {};
        if (function_editor_.has_value() == false)
            return view;
        const function_editor_state& editor { *function_editor_ };
        view.title = u8"기능 편집 · ";
        view.title.append(file_name_of(document_path_));
        view.tab = editor.tab;
        view.message = editor.message;
        view.locked = function_editor_lock_reason_.empty() == false;
        if (view.locked && view.message.empty())
            view.message = function_editor_lock_reason_;

        // 목록이다. 배치 표식과 `카드 k/3`은 지금 초안의 보이는 배치다 (2.2.2 규칙 4).
        const std::vector<function_button_entry> entries { function_editor_entries() };
        const std::vector<std::optional<function_placement>> visible { resolve_visible_placements(entries) };
        std::size_t cards { 0 };
        for (std::size_t index = 0; index < editor.entries.size(); ++index)
        {
            const function_editor_draft& draft { editor.entries[index] };
            function_editor_row_view row {};
            if (draft.source.has_value())
            {
                row.label = warning_row_label(draft.problem, index);
                row.warning = true;
                // 경고 행의 행 id는 열 때의 순번이다. 통과 항목은 버튼 id다 — 순서를 바꿔도 초점이 같은 항목에 남는다.
                row.key = u8"source-";
                row.key.append(function_decimal_text(draft.source_key));
            }
            else
            {
                row.label = draft.label.text.empty() ? draft.edited.id : draft.label.text;
                row.key = u8"button-";
                row.key.append(draft.edited.id);
                row.icon = std::u8string { draft.edited.icon_name() };
                row.placement = visible[index];
                if (visible[index] == function_placement::card)
                    row.shortcut = ++cards;
            }
            view.rows.push_back(std::move(row));
        }
        view.list_header = u8"기능 ";
        view.list_header.append(function_decimal_text(view.rows.size()));
        view.list_header.append(u8"개 · 카드 ");
        view.list_header.append(function_decimal_text(cards));
        view.list_header.push_back(u8'/');
        view.list_header.append(function_decimal_text(maximum_card_function_buttons));
        view.selected = editor.selected;

        // 변수 탭은 읽기 전용 나열이다. 사용자 변수는 JSON으로 고친다 (2.7).
        if (document_.has_value() && document_->function_buttons.has_value())
        {
            const auto value_text = [](const std::variant<std::u8string, function_source_text>& value) -> std::u8string
            {
                if (const std::u8string* const text { std::get_if<std::u8string>(&value) }; text != nullptr)
                    return *text;
                return std::get<function_source_text>(value).json;
            };
            for (const function_variable& variable : document_->function_buttons->variables)
                view.variables.push_back({ {}, variable.name, value_text(variable.value), is_usable_function_variable(variable) == false });
            for (const function_project_variables& entry : document_->function_buttons->project_variables)
            {
                if (const auto* const variables { std::get_if<std::vector<function_variable>>(&entry.variables) }; variables != nullptr)
                {
                    for (const function_variable& variable : *variables)
                        view.variables.push_back({ entry.project, variable.name, value_text(variable.value), is_usable_function_variable(variable) == false });
                }
                else
                    view.variables.push_back({ entry.project, {}, std::get<function_source_text>(entry.variables).json, true });
            }
        }

        const function_editor_size size { function_editor_dialog_size(window_width_ / scale_, window_height_ / scale_) };
        const float list_content { view.tab == function_editor_tab::variables ? function_editor_variables_content_height(view.variables.size()) : function_editor_list_content_height(view.rows.size()) };
        const float list_viewport { view.tab == function_editor_tab::variables ? function_editor_body_height(size.height) : function_editor_list_viewport_height(size.height) };
        view.list_scroll = std::min(editor.list_scroll, function_editor_maximum_scroll(list_content, list_viewport));

        if (editor.selected.has_value() == false || *editor.selected >= editor.entries.size())
            return view;
        const std::size_t selected { *editor.selected };
        const function_editor_draft& draft { editor.entries[selected] };
        if (draft.source.has_value())
        {
            view.warning_text = draft.problem;
            view.warning_source = draft.source->json;
            // 경고 행의 안내·사유·원문은 작은 창에서 폼 자리를 넘으므로 스크롤한다.
            const float notice_content { function_editor_form_content_height(false, 0, view.warning_text, view.warning_source, size.width) };
            view.form_scroll = std::min(editor.form_scroll, function_editor_maximum_scroll(notice_content, function_editor_form_viewport_height(size.height, function_editor_collapsed(size.width))));
            return view;
        }

        // 고른 항목의 폼이다.
        view.editable = true;
        const auto input = [this](const text_edit_state& state, const text_input_target target)
        {
            text_input_view value { state.text, state.caret, state.anchor };
            apply_composition(target, value);
            return value;
        };
        view.label = input(draft.label, text_input_target::function_editor_label);
        view.program = input(draft.program, text_input_target::function_editor_program);
        view.arguments = input(draft.arguments, text_input_target::function_editor_arguments);
        view.working_directory = input(draft.working_directory, text_input_target::function_editor_working_directory);
        view.timeout = input(draft.timeout, text_input_target::function_editor_timeout);
        std::u8string arguments_error {};
        const std::optional<function_button> button { function_editor_button(draft, &arguments_error) };
        view.arguments_error = arguments_error.empty() == false;
        view.arguments_message = view.arguments_error ? arguments_error : std::u8string { arguments_hint };
        view.icon = std::u8string { draft.edited.icon_name() };
        view.mode = draft.edited.mode;
        view.scope = std::u8string { scope_value(draft.edited) };
        if (view.scope == scope_projects)
        {
            for (const card_state& card : cards_)
            {
                const bool checked { std::ranges::find(*draft.edited.project_ids, card.project.id.value) != draft.edited.project_ids->end() };
                view.projects.push_back({ card.project.id.value, std::u8string { card_name(card.project) }, checked });
            }
        }

        // 배치 선택 묶음은 보이는 배치를 고른다. 카드에 두면 갖는 `Alt+N`은 이 기능 앞의 보이는 `card` 수 + 1이다.
        view.placement = visible[selected].value_or(function_placement::menu);
        std::size_t cards_before { 0 };
        for (std::size_t index = 0; index < selected; ++index)
        {
            if (visible[index] == function_placement::card)
                ++cards_before;
        }
        view.card_full = view.placement != function_placement::card && cards >= maximum_card_function_buttons;
        view.card_shortcut = view.card_full ? 0 : cards_before + 1;
        view.placement_message = editor.placement_message;
        view.confirm = draft.edited.confirm;

        const float form_content { layout_function_editor_form(function_editor_form_width(size.width), view.projects.size()).content_height };
        view.form_scroll = std::min(editor.form_scroll, function_editor_maximum_scroll(form_content, function_editor_form_viewport_height(size.height, function_editor_collapsed(size.width))));

        // 미리보기다. 대상 카드의 값으로 같은 순수 함수가 다시 푼다 (2.7). `${env:}`는 이름으로 남는다 (3.6).
        if (cards_.empty())
        {
            view.preview_warnings.push_back(u8"미리 볼 저장소가 없습니다.");
            return view;
        }
        const card_state& card { cards_[editor.preview_card % cards_.size()] };
        view.preview_card = std::u8string { card_name(card.project) };
        view.preview_can_step = cards_.size() > 1;
        if (button.has_value() == false)
        {
            view.preview_warnings.push_back(arguments_error);
            return view;
        }
        if (scope_includes(card.project, card.has_local_result, card.snapshot.kind, *button) == false)
            view.preview_warnings.push_back(u8"이 저장소에는 적용되지 않습니다.");

        const variable_lookup lookup { make_function_preview_lookup(card) };
        const variable_expansion program { expand_variables(button->program, lookup) };
        std::vector<variable_expansion> arguments {};
        for (const std::u8string& argument : button->args)
            arguments.push_back(expand_variables(argument, lookup));
        const variable_expansion working_directory { expand_variables(button->working_directory(), lookup) };

        bool expanded { true };
        if (program.error.has_value())
        {
            expanded = false;
            view.preview_warnings.push_back(expansion_warning(u8"프로그램", *program.error, card.has_local_result));
        }
        for (const variable_expansion& argument : arguments)
        {
            if (argument.error.has_value())
            {
                expanded = false;
                view.preview_warnings.push_back(expansion_warning(u8"인자", *argument.error, card.has_local_result));
                break;
            }
        }
        if (working_directory.error.has_value())
            view.preview_warnings.push_back(expansion_warning(u8"작업 폴더", *working_directory.error, card.has_local_result));
        else
            view.preview_working_directory = display_text(working_directory);
        if (expanded == false)
            return view;

        if (const std::optional<function_program_rejection> rejection { check_function_program(program.text) }; rejection.has_value())
            view.preview_warnings.push_back(function_program_rejection_message(*rejection));
        // 실행 경계가 묶였으면 실행과 같은 인용·비밀 가리기로 잇는다. 묶지 않은 조립(일부 test)은 칸과 같은 인용으로 잇는다.
        if (function_submitter_ != nullptr)
            view.preview_command = build_display_command_line(program, arguments, function_services_.join, function_services_.mask);
        else
        {
            std::vector<std::u8string> parts { display_text(program) };
            for (const variable_expansion& argument : arguments)
                parts.push_back(display_text(argument));
            view.preview_command = join_function_arguments(parts);
        }
        return view;
    }
}

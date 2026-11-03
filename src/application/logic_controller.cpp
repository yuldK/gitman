#include "application/logic_controller.h"

#include "domain/text_edit.h"

#include "application/function_execution_pool.h"
#include "application/svn_repository_browser.h"
#include "domain/function_trust.h"
#include "domain/log_file_naming.h"
#include "domain/path_syntax.h"
#include "presentation/function_presentation.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"
#include "presentation/ui/settings_dialog_element.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace gitman {
    namespace {
        // 문서 이름의 어휘 검증이다 (F3). 파일 이름으로 쓸 수 없는 문자를 막는다.
        // 존재·중복 확인은 worker의 생성 service가 원자적으로 한다.
        std::u8string_view generate_document_name_error(const std::u8string_view name) noexcept
        {
            if (name.empty())
                return u8"";
            if (name == u8"." || name == u8"..")
                return u8"그 이름은 파일 이름으로 쓸 수 없습니다.";
            for (const char8_t value : name)
                if (value < u8' ' || value == u8'\\' || value == u8'/' || value == u8':' || value == u8'*' || value == u8'?' || value == u8'"' || value == u8'<' || value == u8'>' || value == u8'|')
                    return u8"이름에 파일 이름으로 쓸 수 없는 문자가 있습니다.";
            return u8"";
        }

        // 문서의 `function_buttons` 절에서 나온 진단이다. 파서가 위치를 JSON pointer로 단다.
        bool is_function_button_diagnostic(const diagnostic& value) noexcept
        {
            const std::u8string_view pointer { value.source.json_pointer };
            return pointer == u8"/function_buttons" || pointer.starts_with(u8"/function_buttons/");
        }

        // 저장 충돌(`concurrent_modification`)의 알림이다 (function-buttons-design 4.9).
        constexpr std::u8string_view save_conflict_notice { u8"문서가 밖에서 바뀌어 저장하지 않았습니다. 문서 다시 읽기로 불러오십시오." };
        // 다시 읽기에 실패한 알림이다. 사유는 띠 tooltip의 상세 줄에 둔다.
        constexpr std::u8string_view reload_failed_notice { u8"문서를 다시 읽지 못해 지금 문서를 그대로 둡니다." };

        std::u8string decimal_text(const long long value)
        {
            const std::string digits { std::to_string(value) };
            return std::u8string { digits.begin(), digits.end() };
        }

        // 기능 실행의 로그 줄이다. 카드 로그에 VCS 작업과 섞이므로 `[라벨]` 접두로 가른다
        // (function-buttons-design 2.3.6). 출력 줄에도 붙인다.
        std::u8string function_log_text(const std::u8string_view label, const std::u8string_view text)
        {
            std::u8string result { u8"[" };
            result += label;
            result += u8"] ";
            result += text;
            return result;
        }

        // 끝 줄의 소요 시간이다. 0.1초 단위로 반올림한다 (`12.3초`).
        std::u8string function_duration_text(const std::chrono::milliseconds duration)
        {
            const long long milliseconds { duration.count() < 0 ? 0 : static_cast<long long>(duration.count()) };
            const long long tenths { (milliseconds + 50) / 100 };
            return decimal_text(tenths / 10) + u8"." + decimal_text(tenths % 10) + u8"초";
        }

        std::u8string function_capacity_message()
        {
            return u8"동시에 실행할 수 있는 기능은 " + decimal_text(static_cast<long long>(function_execution_capacity)) + u8"개까지입니다";
        }

        // 로컬 조회 결과가 아직 없어 판정할 수 없는 사유다. 제외된 카드는 조회하지 않아 기다려도
        // 풀리지 않으므로 그렇게 알린다 (D17).
        std::u8string unqueried_card_reason(const bool excluded)
        {
            if (excluded)
                return u8"제외된 카드는 저장소를 조회하지 않습니다";
            return u8"저장소를 조회한 뒤에 쓸 수 있습니다";
        }

        // 치환이 실패한 까닭이다. 카드별 비활성 사유(tooltip)와 클릭 시점 거부 줄이 같이 쓴다.
        // 조사(이/가)가 이름에 따라 달라지지 않게 이름을 문장 뒤에 둔다. `excluded`는 제외된
        // 카드(`enabled == false`)다.
        std::u8string function_expansion_reason(const expansion_error& error, const bool has_local_result, const bool excluded)
        {
            switch (error.kind)
            {
            case expansion_error_kind::unterminated:
                return u8"닫히지 않은 ${가 있습니다";
            case expansion_error_kind::empty_name:
                return u8"이름이 빈 변수가 있습니다";
            case expansion_error_kind::undefined:
                return u8"이 저장소에 정의되지 않은 변수입니다: ${" + error.name + u8"}";
            case expansion_error_kind::unresolved:
                // 런타임 변수는 로컬 조회가 끝나야 풀린다 (3.3). 조회 전이면 그 사실을 알린다.
                if (is_runtime_variable_name(error.name) && has_local_result == false)
                    return unqueried_card_reason(excluded);
                return u8"지금 값을 풀 수 없는 변수입니다: ${" + error.name + u8"}";
            case expansion_error_kind::environment_undefined:
                return u8"정의되지 않은 환경 변수입니다: ${env:" + error.name + u8"}";
            }
            return u8"변수를 풀 수 없습니다";
        }

        // 실패 줄의 사유다. runner·launcher·executor가 단 첫 진단이다.
        const diagnostic* first_problem(const std::vector<diagnostic>& diagnostics) noexcept
        {
            for (const diagnostic& value : diagnostics)
            {
                if (value.severity != diagnostic_severity::information)
                    return &value;
            }
            return nullptr;
        }

        // 끝 줄(`시간 초과(N초)`, `취소됨`)이 이미 같은 뜻을 말하는 진단이다. 다시 남기지 않는다.
        bool restates_function_end(const diagnostic& value) noexcept
        {
            return value.code == diagnostic_code::process_timed_out || value.code == diagnostic_code::process_cancelled;
        }

        // 확인 dialog의 실행 방식 배지다 (시안 `Trust.dc.html`).
        std::u8string_view function_mode_text(const function_run_mode mode) noexcept
        {
            return mode == function_run_mode::detached ? std::u8string_view { u8"따로 실행" } : std::u8string_view { u8"출력을 로그로 받기" };
        }

        // 신뢰 dialog의 인자 줄이다. 치환 전 원문이라 실제 명령줄 인용(`command_line_joiner`)이 아니라
        // 읽기 위한 인용이다. 비었거나 공백·따옴표가 든 인자만 따옴표로 감싸 인자 경계를 보인다.
        std::u8string function_template_arguments(const std::vector<std::u8string>& arguments)
        {
            std::u8string text {};
            for (const std::u8string& argument : arguments)
            {
                if (text.empty() == false)
                    text.push_back(u8' ');
                const bool quote { argument.empty() || argument.find_first_of(u8" \t\"") != std::u8string::npos };
                if (quote == false)
                {
                    text += argument;
                    continue;
                }
                text.push_back(u8'"');
                for (const char8_t character : argument)
                {
                    if (character == u8'"')
                        text.push_back(u8'\\');
                    text.push_back(character);
                }
                text.push_back(u8'"');
            }
            return text;
        }

        // 원문이 참조하는 사용자 변수 이름이다 (내장 변수와 `${env:}`는 뺀다). 처음 나온 순서다.
        std::vector<std::u8string> referenced_user_variables(const function_button& button)
        {
            std::vector<std::u8string_view> templates { button.program };
            for (const std::u8string& argument : button.args)
                templates.push_back(argument);
            templates.push_back(button.working_directory());

            std::vector<std::u8string> names {};
            for (const std::u8string_view text : templates)
            {
                for (const variable_reference& reference : scan_variable_references(text).references)
                {
                    if (reference.environment || is_builtin_variable_name(reference.name) || std::ranges::find(names, reference.name) != names.end())
                        continue;
                    names.push_back(reference.name);
                }
            }
            return names;
        }

        // 신뢰 dialog의 변수 줄이다. 문서 변수 값과 저장소별 덮어쓰기를 함께 보인다
        // (`build_config = Debug (frontend: Release)`). 값은 치환 전 원문이다.
        std::optional<std::u8string> function_variable_line(const function_buttons_definition& definition, const std::u8string_view name)
        {
            std::optional<std::u8string> value {};
            for (const function_variable& variable : definition.variables)
            {
                const std::u8string* const text { std::get_if<std::u8string>(&variable.value) };
                if (text != nullptr && variable.name == name && is_usable_function_variable(variable))
                    value = *text;
            }

            std::u8string overrides {};
            for (const function_project_variables& project : definition.project_variables)
            {
                const std::vector<function_variable>* const variables { std::get_if<std::vector<function_variable>>(&project.variables) };
                if (variables == nullptr)
                    continue;
                for (const function_variable& variable : *variables)
                {
                    const std::u8string* const text { std::get_if<std::u8string>(&variable.value) };
                    if (text == nullptr || variable.name != name || is_usable_function_variable(variable) == false)
                        continue;
                    if (overrides.empty() == false)
                        overrides += u8", ";
                    overrides += project.project + u8": " + *text;
                }
            }

            if (value.has_value() == false && overrides.empty())
                return std::nullopt;
            std::u8string line { name };
            line += u8" =";
            if (value.has_value())
                line += u8" " + *value;
            if (overrides.empty() == false)
                line += u8" (" + overrides + u8")";
            return line;
        }

        // UTF-8 글자 하나를 읽는다. 깨진 byte는 그 byte 하나를 글자로 넘겨 멈추지 않는다.
        struct utf8_character
        {
            std::uint32_t code_point { 0 };
            std::size_t length { 1 };
        };

        utf8_character read_utf8_character(const std::u8string_view text, const std::size_t offset) noexcept
        {
            const auto lead { static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset])) };
            std::size_t length { 1 };
            std::uint32_t code_point { lead };
            if ((lead & 0xE0u) == 0xC0u)
            {
                length = 2;
                code_point = lead & 0x1Fu;
            }
            else if ((lead & 0xF0u) == 0xE0u)
            {
                length = 3;
                code_point = lead & 0x0Fu;
            }
            else if ((lead & 0xF8u) == 0xF0u)
            {
                length = 4;
                code_point = lead & 0x07u;
            }
            if (length == 1 || offset + length > text.size())
                return { lead, 1 };
            for (std::size_t index = 1; index < length; ++index)
            {
                const auto next { static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + index])) };
                if ((next & 0xC0u) != 0x80u)
                    return { lead, 1 };
                code_point = (code_point << 6) | (next & 0x3Fu);
            }
            return { code_point, length };
        }

        // 화면에 보이지 않거나 줄·방향을 바꾸는 글자다. 확인 dialog에서 원문 일부를 숨길 수 있다.
        bool is_invisible_code_point(const std::uint32_t code_point) noexcept
        {
            return code_point < 0x20u
                || (code_point >= 0x7Fu && code_point <= 0x9Fu)
                || code_point == 0xADu
                || code_point == 0x61Cu
                || code_point == 0x180Eu
                || (code_point >= 0x200Bu && code_point <= 0x200Fu)
                || (code_point >= 0x2028u && code_point <= 0x202Eu)
                || (code_point >= 0x2060u && code_point <= 0x206Fu)
                || code_point == 0xFEFFu
                || (code_point >= 0xFFF9u && code_point <= 0xFFFBu);
        }

        // 확인 dialog의 글에서 제어 문자, 줄 바꿈, 방향 바꿈, 폭 없는 글자를 `<U+XXXX>`로 드러낸다.
        // 이 dialog는 보이는 글이 곧 확인할 내용이라 원문의 `\n`이나 U+202E로 뒷부분을 숨기지 못하게 한다.
        std::u8string visible_confirm_text(const std::u8string_view text)
        {
            constexpr std::u8string_view digits { u8"0123456789ABCDEF" };
            std::u8string result {};
            result.reserve(text.size());
            for (std::size_t offset = 0; offset < text.size();)
            {
                const utf8_character character { read_utf8_character(text, offset) };
                if (is_invisible_code_point(character.code_point))
                {
                    result += u8"<U+";
                    const int top_shift { character.code_point > 0xFFFFu ? 20 : 12 };
                    for (int shift = top_shift; shift >= 0; shift -= 4)
                        result.push_back(digits[(character.code_point >> shift) & 0xFu]);
                    result.push_back(u8'>');
                }
                else
                {
                    result += text.substr(offset, character.length);
                }
                offset += character.length;
            }
            return result;
        }

        // 확인 dialog의 코드 줄을 `columns` 칸씩 나눈다 (`function_confirm_code_columns`). ASCII 밖의
        // 글자는 두 칸이다. 칸이 넘치는 자리 앞쪽 절반 뒤에 공백이 있으면 그 공백 뒤에서 나눠 인자 경계를
        // 살린다. 글자 중간에서는 나누지 않는다.
        std::vector<std::u8string> wrap_confirm_text(const std::u8string_view text, const std::size_t columns)
        {
            std::vector<std::u8string> chunks {};
            std::size_t start { 0 };
            while (start < text.size())
            {
                std::size_t offset { start };
                std::size_t used { 0 };
                std::size_t space_break { 0 };
                while (offset < text.size())
                {
                    const utf8_character character { read_utf8_character(text, offset) };
                    const std::size_t width { character.code_point < 0x80u ? std::size_t { 1 } : std::size_t { 2 } };
                    if (used + width > columns && offset > start)
                        break;
                    used += width;
                    offset += character.length;
                    if (character.code_point == u8' ' && used * 2 > columns)
                        space_break = offset;
                }
                if (offset < text.size() && space_break > start)
                    offset = space_break;
                chunks.emplace_back(text.substr(start, offset - start));
                start = offset;
            }
            if (chunks.empty())
                chunks.emplace_back();
            return chunks;
        }

        // 묶은 기능 버튼 알림의 상세 상한이다. 버튼이 많은 문서에서 tooltip이 화면을 덮지
        // 않게 하고, 나머지는 개수로만 알린다. 항목별 확인은 편집 dialog의 경고 행(T12)이 맡는다.
        constexpr std::size_t maximum_function_notice_details { 20 };

        // 기능 버튼 진단을 묶은 알림이다 (3.8). 없으면 값이 없다. 문서를 열 때와, 편집 dialog로 고친 저장이 쓴 글을 다시 읽은
        // 진단으로 다시 만들 때 같은 규칙을 쓴다.
        std::optional<notice_view_model> make_function_button_notice(const std::vector<diagnostic>& diagnostics)
        {
            notice_view_model notice {};
            std::size_t warnings { 0 };
            for (const diagnostic& value : diagnostics)
            {
                if (value.severity == diagnostic_severity::information || is_function_button_diagnostic(value) == false)
                    continue;
                if (++warnings <= maximum_function_notice_details)
                    notice.details.push_back(value.message);
            }
            if (warnings == 0)
                return std::nullopt;
            const std::string count { std::to_string(warnings) };
            notice.text = u8"기능 버튼 경고 ";
            notice.text.append(count.begin(), count.end());
            notice.text += u8"건";
            if (warnings > maximum_function_notice_details)
            {
                const std::string rest { std::to_string(warnings - maximum_function_notice_details) };
                std::u8string& more { notice.details.emplace_back(u8"(외 ") };
                more.append(rest.begin(), rest.end());
                more += u8"건)";
            }
            return notice;
        }

        // 배너에 싣는 문서 진단 한 줄이다. project 하나에 걸린 진단은 메시지만으로는
        // 어느 카드의 일인지 알 수 없어 그 project의 표시 이름을 앞에 붙인다.
        std::u8string document_notice_text(const diagnostic& value, const workspace_document& document)
        {
            if (value.source.project_id.has_value() == false)
                return value.message;
            std::u8string name { *value.source.project_id };
            for (const project_definition& project : document.projects)
                if (project.id.value == *value.source.project_id && project.display_name.empty() == false)
                    name = project.display_name;
            return name + u8": " + value.message;
        }

        // 글꼴 목록 첫 항목의 표시 이름이다. 고르면 빈 이름 = 내장 글꼴이다.
        constexpr std::u8string_view default_font_display_name { u8"(기본 글꼴)" };
        // 글꼴 미리 보기 문구다. 한글·영문·숫자·기호를 한 줄에 담아 코드 글꼴 판단에 쓴다.
        constexpr std::u8string_view font_preview_text { u8"다람쥐 헌 쳇바퀴에 타고파 Illegal1O0 {}(); 😀" };

        char8_t ascii_lowercase(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        bool contains_ignoring_ascii_case(const std::u8string_view text, const std::u8string_view needle) noexcept
        {
            if (needle.empty())
                return true;
            if (text.size() < needle.size())
                return false;
            for (std::size_t start = 0; start + needle.size() <= text.size(); ++start)
            {
                bool matched { true };
                for (std::size_t index = 0; index < needle.size(); ++index)
                {
                    if (ascii_lowercase(text[start + index]) != ascii_lowercase(needle[index]))
                    {
                        matched = false;
                        break;
                    }
                }
                if (matched)
                    return true;
            }
            return false;
        }

        // card_state에서 표시 상태를 정한다. 우선순위: 비활성 → 실행 중 → 초기 조회
        // 전 → 조회 불가 → 오류 → 주의 → 정상.
        struct card_view_inputs
        {
            bool enabled { true };
            bool busy { false };
            bool has_local_result { false };
            repository_availability availability { repository_availability::unknown };
            remote_sync_state sync_state { remote_sync_state::unknown };
            working_tree_state tree_state { working_tree_state::unknown };
        };

        // dialog 수준에서 곧바로 판정할 수 있는 검증이다. 존재·저장소 일치 같은
        // 나머지 검증은 provider가 실행 직전에 다시 수행한다 (REQ-007).
        //
        // 원격을 추적하는 local branch는 local 후보로 따로 서지 않고 그 remote 후보
        // (`origin/main`)로만 선다 (`build_git_switch_candidates`). 그래서 remote 후보도
        // 대응하는 local branch가 체크아웃돼 있으면 현재다. 같은 이름의 local 후보가
        // 따로 있으면 그 remote로는 닿지 않는 branch라 local 후보만 현재다.
        bool candidate_is_current(const repository_snapshot& snapshot, const switch_candidate& candidate, const std::vector<switch_candidate>& candidates) noexcept
        {
            if (candidate.kind == switch_candidate_kind::git_local_branch)
                return candidate.display_name == snapshot.current_reference;
            if (candidate.kind == switch_candidate_kind::subversion_url)
                return candidate.target == snapshot.current_reference;
            if (candidate.local_branch.empty() || candidate.local_branch != snapshot.current_reference)
                return false;
            for (const switch_candidate& other : candidates)
                if (other.kind == switch_candidate_kind::git_local_branch && other.display_name == candidate.local_branch)
                    return false;
            return true;
        }

        // 환경설정 경로 초안의 형식 검증이다 (stage-8-plan 5.1). 파일 선택 dialog가
        // 존재하는 파일만 돌려주므로 logic은 blocking I/O 없이 형식만 본다. 빈 값은
        // 자동 탐색이라 항상 유효하다.
        std::u8string_view settings_executable_error(const std::u8string_view path) noexcept
        {
            if (path.empty() || is_absolute_windows_path(path))
                return u8"";
            return u8"실행 파일 경로는 절대 경로여야 합니다.";
        }

        // 상태 확인 제한 시간 텍스트 박스의 초안을 초 값으로 바꾼다. 초안에는
        // 숫자만 담기므로(handle_edit_settings_timeout) 자리올림만 하면 된다. 빈
        // 초안은 기본값이라 값이 없다.
        std::optional<std::int32_t> parse_settings_timeout(const std::u8string_view text) noexcept
        {
            if (text.empty())
                return std::nullopt;
            std::int32_t value { 0 };
            for (const char8_t digit : text)
                value = value * 10 + static_cast<std::int32_t>(digit - u8'0');
            return value;
        }

        std::u8string_view settings_timeout_error(const std::u8string_view text) noexcept
        {
            const std::optional<std::int32_t> value { parse_settings_timeout(text) };
            if (value.has_value() == false || (*value >= minimum_query_timeout_seconds && *value <= maximum_query_timeout_seconds))
                return u8"";
            return u8"제한 시간은 10~3600초 사이여야 합니다.";
        }

        card_view_state derive_card_state(const card_view_inputs& inputs) noexcept
        {
            if (inputs.enabled == false)
                return card_view_state::disabled;
            if (inputs.busy)
                return card_view_state::running;
            if (inputs.has_local_result == false)
                return card_view_state::loading;
            if (inputs.availability != repository_availability::ready)
                return card_view_state::failed;
            if (inputs.sync_state == remote_sync_state::error)
                return card_view_state::failed;
            if (inputs.tree_state == working_tree_state::conflicted || inputs.sync_state == remote_sync_state::authentication_required || inputs.sync_state == remote_sync_state::offline
                || inputs.sync_state == remote_sync_state::remote_target_missing)
                return card_view_state::warning;
            return card_view_state::ready;
        }
    } // namespace

    logic_controller::logic_controller(operation_submitter& submitter, cancellation_registry& registry) noexcept
        : submitter_ { &submitter }
        , registry_ { &registry }
    {
        // 종료 시 UI thread가 전역 token까지 함께 취소한다 (설계 L3).
        registry_->add(cancellation_source_);
    }

    logic_controller::logic_controller(operation_submitter& submitter, log_file_sink& log_sink, cancellation_registry& registry) noexcept
        : submitter_ { &submitter }
        , log_sink_ { &log_sink }
        , registry_ { &registry }
    {
        registry_->add(cancellation_source_);
    }

    logic_controller::logic_controller(operation_submitter& submitter)
        : submitter_ { &submitter }
        , owned_registry_ { std::make_unique<cancellation_registry>() }
    {
        registry_ = owned_registry_.get();
        registry_->add(cancellation_source_);
    }

    logic_controller::logic_controller(operation_submitter& submitter, log_file_sink& log_sink)
        : submitter_ { &submitter }
        , log_sink_ { &log_sink }
        , owned_registry_ { std::make_unique<cancellation_registry>() }
    {
        registry_ = owned_registry_.get();
        registry_->add(cancellation_source_);
    }

    void logic_controller::bind_function_execution(function_run_submitter& submitter, function_run_services services)
    {
        // 하나라도 비면 클릭 시점에 logic thread에서 `bad_function_call`이 난다. 묶지 않은
        // 것으로 두어 실행 intent가 로그 한 줄로 거절되게 한다.
        if (services.join == nullptr || services.mask == nullptr || services.environment == nullptr || services.make_command == nullptr)
        {
            function_submitter_ = nullptr;
            function_services_ = {};
            return;
        }
        function_submitter_ = &submitter;
        function_services_ = std::move(services);
    }

    std::optional<logic_controller::function_button_state> logic_controller::function_state(const project_id& id, const std::u8string_view button_id) const
    {
        const function_button* const button { find_function_button(button_id) };
        if (button == nullptr)
            return std::nullopt;
        for (const card_state& card : cards_)
        {
            if (card.project.id == id)
                return evaluate_function(card, *button, make_function_preview_lookup(card));
        }
        return std::nullopt;
    }

    void logic_controller::handle(logic_message message)
    {
        std::visit(
            [this](auto&& value) {
                using value_type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<value_type, open_document_intent>)
                    handle_open_document(value);
                else if constexpr (std::is_same_v<value_type, generate_document_intent>)
                    handle_generate_document(value);
                else if constexpr (std::is_same_v<value_type, refresh_all_intent>)
                {
                    for (card_state& card : cards_)
                        if (card.project.enabled)
                            request_refresh(card);
                }
                else if constexpr (std::is_same_v<value_type, refresh_card_intent>)
                {
                    card_state* const card { find_card(value.id) };
                    if (card != nullptr && card->project.enabled)
                        request_refresh(*card);
                }
                else if constexpr (std::is_same_v<value_type, select_card_intent>)
                    handle_select_card(value);
                else if constexpr (std::is_same_v<value_type, set_filter_intent>)
                {
                    filter_ = std::move(value.text);
                    clamp_scroll();
                }
                else if constexpr (std::is_same_v<value_type, toggle_path_display_intent>)
                    handle_toggle_path_display();
                else if constexpr (std::is_same_v<value_type, reorder_card_intent>)
                    handle_reorder_card(value);
                else if constexpr (std::is_same_v<value_type, request_update_intent>)
                    handle_request_update(value);
                else if constexpr (std::is_same_v<value_type, request_switch_intent>)
                    handle_request_switch(value);
                else if constexpr (std::is_same_v<value_type, cancel_operation_intent>)
                    handle_cancel_operation(value);
                else if constexpr (std::is_same_v<value_type, run_function_intent>)
                    handle_run_function(value);
                else if constexpr (std::is_same_v<value_type, stop_function_intent>)
                    handle_stop_function(value);
                else if constexpr (std::is_same_v<value_type, confirm_function_run_intent>)
                    handle_confirm_function_run();
                else if constexpr (std::is_same_v<value_type, cancel_function_run_intent>)
                    handle_cancel_function_run();
                else if constexpr (std::is_same_v<value_type, function_confirm_scroll_intent>)
                    handle_function_confirm_scroll(value);
                else if constexpr (std::is_same_v<value_type, clear_log_intent>)
                {
                    card_state* const card { find_card(value.id) };
                    if (card != nullptr)
                        card->log.clear();
                }
                else if constexpr (std::is_same_v<value_type, set_log_filter_intent>)
                {
                    log_filter_ = value.filter;
                    // 필터가 바뀌면 내용 높이가 달라진다. 저장된 위치를 다시 고정한다.
                    handle_log_scroll(0.0f);
                }
                else if constexpr (std::is_same_v<value_type, set_log_auto_scroll_intent>)
                    log_auto_scroll_ = value.enabled;
                else if constexpr (std::is_same_v<value_type, log_scroll_intent>)
                    handle_log_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, begin_switch_intent>)
                    handle_begin_switch(value);
                else if constexpr (std::is_same_v<value_type, select_switch_candidate_intent>)
                    handle_select_switch_candidate(value.index);
                else if constexpr (std::is_same_v<value_type, select_svn_browser_node_intent>)
                    handle_select_svn_browser_node(value.url);
                else if constexpr (std::is_same_v<value_type, toggle_svn_browser_node_intent>)
                    handle_toggle_svn_browser_node(value.url);
                else if constexpr (std::is_same_v<value_type, confirm_switch_intent>)
                    handle_confirm_switch();
                else if constexpr (std::is_same_v<value_type, cancel_switch_dialog_intent>)
                    switch_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, switch_dialog_scroll_intent>)
                    handle_switch_dialog_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, set_text_composition_intent>)
                    handle_set_text_composition(value);
                else if constexpr (std::is_same_v<value_type, edit_text_input_intent>)
                    handle_edit_text_input(value);
                else if constexpr (std::is_same_v<value_type, submit_switch_query_intent>)
                    handle_submit_switch_query();
                else if constexpr (std::is_same_v<value_type, clear_switch_query_intent>)
                    handle_clear_switch_query();
                else if constexpr (std::is_same_v<value_type, refresh_switch_dialog_intent>)
                    handle_refresh_switch_dialog();
                else if constexpr (std::is_same_v<value_type, open_generate_document_intent>)
                    handle_open_generate_document();
                else if constexpr (std::is_same_v<value_type, set_generate_scan_root_intent>)
                {
                    if (generate_dialog_.has_value())
                    {
                        generate_dialog_->scan_root = std::move(value.path);
                        generate_dialog_->message.clear();
                    }
                }
                else if constexpr (std::is_same_v<value_type, set_generate_location_intent>)
                {
                    if (generate_dialog_.has_value())
                    {
                        generate_dialog_->location = std::move(value.path);
                        generate_dialog_->same_folder = false;
                        generate_dialog_->message.clear();
                    }
                }
                else if constexpr (std::is_same_v<value_type, toggle_generate_same_folder_intent>)
                {
                    if (generate_dialog_.has_value())
                    {
                        generate_dialog_->same_folder = generate_dialog_->same_folder == false;
                        generate_dialog_->message.clear();
                    }
                }
                else if constexpr (std::is_same_v<value_type, confirm_generate_document_intent>)
                    handle_confirm_generate_document();
                else if constexpr (std::is_same_v<value_type, cancel_generate_document_intent>)
                    generate_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, open_font_dialog_intent>)
                    handle_open_font_dialog(value);
                else if constexpr (std::is_same_v<value_type, select_font_family_intent>)
                    handle_select_font_family(value.index);
                else if constexpr (std::is_same_v<value_type, confirm_font_dialog_intent>)
                    handle_confirm_font_dialog();
                else if constexpr (std::is_same_v<value_type, cancel_font_dialog_intent>)
                    font_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, font_dialog_scroll_intent>)
                    handle_font_dialog_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, font_families_event>)
                    font_families_ = std::move(value.families);
                else if constexpr (std::is_same_v<value_type, begin_discovery_intent>)
                    handle_begin_discovery(value);
                else if constexpr (std::is_same_v<value_type, toggle_discovery_candidate_intent>)
                    handle_toggle_discovery_candidate(value.index);
                else if constexpr (std::is_same_v<value_type, confirm_discovery_intent>)
                    handle_confirm_discovery();
                else if constexpr (std::is_same_v<value_type, cancel_discovery_dialog_intent>)
                    handle_cancel_discovery_dialog();
                else if constexpr (std::is_same_v<value_type, discovery_dialog_scroll_intent>)
                    handle_discovery_dialog_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, open_context_menu_intent>)
                    handle_open_context_menu(value);
                else if constexpr (std::is_same_v<value_type, open_function_menu_intent>)
                    handle_open_function_menu(value);
                else if constexpr (std::is_same_v<value_type, run_function_shortcut_intent>)
                    handle_run_function_shortcut(value);
                else if constexpr (std::is_same_v<value_type, open_function_placement_menu_intent>)
                    handle_open_function_placement_menu(value);
                else if constexpr (std::is_same_v<value_type, set_function_placement_intent>)
                    handle_set_function_placement(value, std::nullopt);
                else if constexpr (std::is_same_v<value_type, open_function_editor_intent>)
                    handle_open_function_editor(value);
                else if constexpr (std::is_same_v<value_type, select_function_editor_tab_intent>)
                    handle_select_function_editor_tab(value);
                else if constexpr (std::is_same_v<value_type, select_function_editor_entry_intent>)
                    handle_select_function_editor_entry(value.index);
                else if constexpr (std::is_same_v<value_type, edit_function_editor_list_intent>)
                    handle_edit_function_editor_list(value.edit);
                else if constexpr (std::is_same_v<value_type, set_function_editor_field_intent>)
                    handle_set_function_editor_field(value);
                else if constexpr (std::is_same_v<value_type, step_function_editor_preview_intent>)
                    handle_step_function_editor_preview(value.forward);
                else if constexpr (std::is_same_v<value_type, function_editor_scroll_intent>)
                    handle_function_editor_scroll(value);
                else if constexpr (std::is_same_v<value_type, apply_function_editor_intent>)
                    handle_apply_function_editor();
                else if constexpr (std::is_same_v<value_type, cancel_function_editor_intent>)
                {
                    function_editor_.reset();
                    if (composition_.has_value() && is_function_editor_target(composition_->target))
                        composition_.reset();
                }
                else if constexpr (std::is_same_v<value_type, open_document_context_menu_intent>)
                    handle_open_document_context_menu(value);
                else if constexpr (std::is_same_v<value_type, set_theme_preference_intent>)
                    handle_set_theme_preference(value);
                else if constexpr (std::is_same_v<value_type, set_accent_intent>)
                    handle_set_accent(value);
                else if constexpr (std::is_same_v<value_type, close_context_menu_intent>)
                    context_menu_.reset();
                else if constexpr (std::is_same_v<value_type, select_context_menu_item_intent>)
                    handle_select_context_menu_item(value);
                else if constexpr (std::is_same_v<value_type, remove_recent_document_intent>)
                    handle_remove_recent_document(value);
                else if constexpr (std::is_same_v<value_type, toggle_recent_document_pin_intent>)
                    handle_toggle_recent_document_pin(value);
                else if constexpr (std::is_same_v<value_type, close_document_intent>)
                    handle_close_document();
                else if constexpr (std::is_same_v<value_type, reload_document_intent>)
                    handle_reload_document();
                else if constexpr (std::is_same_v<value_type, request_exit_intent>)
                    handle_request_exit();
                else if constexpr (std::is_same_v<value_type, show_notice_intent>)
                    handle_show_notice(std::move(value));
                else if constexpr (std::is_same_v<value_type, dismiss_notice_intent>)
                    notice_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, open_local_changes_intent>)
                    handle_open_local_changes(value);
                else if constexpr (std::is_same_v<value_type, select_local_change_intent>)
                    handle_select_local_change(value.index);
                else if constexpr (std::is_same_v<value_type, cancel_local_changes_dialog_intent>)
                    local_changes_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, local_changes_scroll_intent>)
                    handle_local_changes_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, local_changes_diff_scroll_intent>)
                    handle_local_changes_diff_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, local_changes_event>)
                    handle_local_changes(std::move(value));
                else if constexpr (std::is_same_v<value_type, file_diff_event>)
                    handle_file_diff(std::move(value));
                else if constexpr (std::is_same_v<value_type, open_settings_intent>)
                    handle_open_settings();
                else if constexpr (std::is_same_v<value_type, set_settings_executable_intent>)
                    handle_set_settings_executable(std::move(value));
                else if constexpr (std::is_same_v<value_type, clear_settings_executable_intent>)
                    handle_clear_settings_executable(value);
                else if constexpr (std::is_same_v<value_type, clear_settings_override_intent>)
                    handle_clear_settings_override(value);
                else if constexpr (std::is_same_v<value_type, select_settings_tab_intent>)
                    handle_select_settings_tab(value);
                else if constexpr (std::is_same_v<value_type, settings_scroll_intent>)
                    handle_settings_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, toggle_settings_submodules_intent>)
                {
                    // 값을 건드리면 문서 모드에서는 그 행이 "문서에 정의됨"이 된다
                    // (G3.2 암묵 덮어쓰기).
                    if (settings_dialog_.has_value())
                    {
                        settings_dialog_->update_submodules = settings_dialog_->update_submodules == false;
                        settings_dialog_->submodules_defined = true;
                    }
                }
                else if constexpr (std::is_same_v<value_type, toggle_settings_ignore_local_intent>)
                {
                    if (settings_dialog_.has_value())
                    {
                        settings_dialog_->ignore_local_changes = settings_dialog_->ignore_local_changes == false;
                        settings_dialog_->ignore_local_defined = true;
                    }
                }
                else if constexpr (std::is_same_v<value_type, toggle_settings_log_files_intent>)
                {
                    if (settings_dialog_.has_value())
                    {
                        settings_dialog_->write_log_files = settings_dialog_->write_log_files == false;
                        settings_dialog_->log_files_defined = true;
                    }
                }
                else if constexpr (std::is_same_v<value_type, confirm_settings_intent>)
                    handle_confirm_settings();
                else if constexpr (std::is_same_v<value_type, cancel_settings_dialog_intent>)
                    settings_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, window_metrics_intent>)
                {
                    window_width_ = value.width;
                    window_height_ = value.height;
                    scale_ = value.scale > 0.0f ? value.scale : 1.0f;
                    // 창이 커지면 남은 스크롤 여유가 줄어든다. 다시 고정하지 않으면
                    // 화면은 맞게 그려지지만 다음 휠 입력이 한참 동안 헛돈다.
                    clamp_scroll();
                }
                else if constexpr (std::is_same_v<value_type, scroll_intent>)
                {
                    scroll_offset_ += value.delta;
                    clamp_scroll();
                }
                else if constexpr (std::is_same_v<value_type, window_placement_intent>)
                    handle_window_placement(value);
                else if constexpr (std::is_same_v<value_type, close_intent> || std::is_same_v<value_type, shutdown_message>)
                    begin_shutdown();
                else if constexpr (std::is_same_v<value_type, startup_intent>)
                    start();
                else if constexpr (std::is_same_v<value_type, relay_drain_intent>)
                {
                    // 깨우기 전용 메시지다 (설계 L3). 내용은 driver가 relay 큐에서
                    // 이미 꺼내 따로 넘겼다.
                }
                else if constexpr (std::is_same_v<value_type, document_loaded_event>)
                    handle_document_loaded(std::move(value));
                else if constexpr (std::is_same_v<value_type, document_generated_event>)
                    handle_document_generated(std::move(value));
                else if constexpr (std::is_same_v<value_type, query_completed_event>)
                    handle_query_completed(std::move(value));
                else if constexpr (std::is_same_v<value_type, document_saved_event>)
                    handle_document_saved(std::move(value));
                else if constexpr (std::is_same_v<value_type, operation_log_event>)
                    handle_operation_log(std::move(value));
                else if constexpr (std::is_same_v<value_type, change_completed_event>)
                    handle_change_completed(std::move(value));
                else if constexpr (std::is_same_v<value_type, switch_candidates_event>)
                    handle_switch_candidates(std::move(value));
                else if constexpr (std::is_same_v<value_type, svn_directory_event>)
                    handle_svn_directory(std::move(value));
                else if constexpr (std::is_same_v<value_type, discovery_completed_event>)
                    handle_discovery_completed(std::move(value));
                else if constexpr (std::is_same_v<value_type, projects_registered_event>)
                    handle_projects_registered(std::move(value));
                else if constexpr (std::is_same_v<value_type, app_settings_loaded_event>)
                    handle_app_settings_loaded(std::move(value));
                else if constexpr (std::is_same_v<value_type, app_settings_saved_event>)
                    handle_app_settings_saved(std::move(value));
                else if constexpr (std::is_same_v<value_type, function_started_event>)
                    handle_function_started(value);
                else if constexpr (std::is_same_v<value_type, function_log_event>)
                    handle_function_log(std::move(value));
                else if constexpr (std::is_same_v<value_type, function_completed_event>)
                    handle_function_completed(std::move(value));
            },
            std::move(message));
    }

    void logic_controller::start()
    {
        if (shutting_down_ || app_settings_loaded_ || pending_app_settings_load_id_ != 0)
            return;

        operation_request request { make_request(operation_kind::load_app_settings, nullptr, 0) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;
        pending_app_settings_load_id_ = operation_id;
    }

    bool logic_controller::shutdown_requested() const noexcept
    {
        return shutting_down_;
    }

    process_cancellation_token logic_controller::cancellation() const noexcept
    {
        return cancellation_source_.token();
    }

    void logic_controller::handle_open_document(const open_document_intent& intent)
    {
        if (shutting_down_)
            return;
        // 실행 중인 출력 수집 기능이나 저장하지 못한 변경이 있으면 먼저 묻는다 (4.10, D28). 확인하면 `open_document_now`가
        // 이어 연다.
        if (confirm_document_change(running_function_action::open_document, intent.path) == false)
            return;
        open_document_now(intent.path);
    }

    void logic_controller::open_document_now(std::u8string path)
    {
        if (shutting_down_)
            return;

        // 문서를 열기 전의 배치를 앱 설정에 남긴다 (S3.2). 아래에서 document_를
        // 비우므로 열려 있었는지 판정이 가능한 지금 호출한다.
        record_app_window_placement();

        // 이전 문서에서 진행 중인 변경 작업은 결과가 버려질 것이므로 프로세스도
        // 계속 둘 이유가 없다. 카드가 사라지기 전에 취소를 전파한다.
        cancel_running_changes();
        // 기능 실행 확인은 이전 문서의 정의를 가리킨다. 남겨 두면 확인할 때 새 문서 경로에 이전
        // 정의의 해시를 기록하게 된다.
        function_confirm_.reset();
        // cmd 보정 승인은 그 문서의 정의와 값에 대한 것이다. 다시 읽거나 다른 문서를 열면 다시 묻는다 (D30).
        cmd_approvals_.clear();
        // 이전 문서의 다시 읽기 결과는 도착해도 쓰지 않는다.
        abandon_reload();
        // 이전 문서를 가리키던 dialog와 메뉴도 지금 닫는다. 읽기에 실패하면 `install_document`가 닫아 주지 않아, 편집
        // dialog가 이전 문서의 초안을 든 채 남고 그 Apply는 문서가 없어 아무 말 없이 버려진다 (`close_document_now`와 같다).
        switch_dialog_.reset();
        settings_dialog_.reset();
        local_changes_dialog_.reset();
        context_menu_.reset();
        function_focus_.reset();
        function_editor_.reset();
        if (composition_.has_value() && is_function_editor_target(composition_->target))
            composition_.reset();
        function_trust_after_save_ = false;
        run_waiting_for_app_settings_.reset();
        function_notice_after_save_ = false;
        unsaved_ = {};
        function_button_problems_.clear();
        function_editor_lock_reason_.clear();
        if (discovery_dialog_.has_value() && discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();
        discovery_dialog_.reset();

        document_path_ = std::move(path);
        document_.reset();
        revision_ = {};
        cards_.clear();
        notices_.clear();
        function_button_notice_.reset();
        save_notice_.clear();
        selected_.reset();
        scroll_offset_ = 0.0f;
        document_loading_ = true;
        // 이전 문서의 진행 중 저장·생성 결과는 도착해도 버린다.
        pending_save_operation_id_ = 0;
        save_queued_ = false;
        pending_generation_operation_id_ = 0;

        operation_request request { make_request(operation_kind::load_document, nullptr, 0) };
        request.document_path = document_path_;
        static_cast<void>(submitter_->submit(std::move(request)));
    }

    void logic_controller::handle_generate_document(const generate_document_intent& intent)
    {
        // 생성은 현재 문서를 건드리지 않고 worker에서 진행되므로 화면은 그대로 둔다.
        if (shutting_down_ || pending_generation_operation_id_ != 0)
            return;

        operation_request request { make_request(operation_kind::generate_document, nullptr, 0) };
        request.document_path = intent.document_path;
        request.scan_root = intent.scan_root;
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;
        pending_generation_operation_id_ = operation_id;
    }

    void logic_controller::handle_document_loaded(document_loaded_event event)
    {
        // 다시 읽기의 결과는 따로 처리한다 (4.9). 버린 다시 읽기의 결과는 지금 문서에도, 새로 여는 문서에도 쓰지
        // 않는다. 열기 결과는 지금처럼 id를 보지 않는다.
        if (event.operation_id != 0)
        {
            if (event.operation_id == pending_reload_operation_id_)
            {
                handle_document_reloaded(std::move(event));
                return;
            }
            const auto abandoned { std::ranges::find(abandoned_reload_operation_ids_, event.operation_id) };
            if (abandoned != abandoned_reload_operation_ids_.end())
            {
                abandoned_reload_operation_ids_.erase(abandoned);
                return;
            }
        }

        document_loading_ = false;
        if (event.document.has_value() == false)
        {
            notices_.clear();
            function_button_notice_.reset();
            for (const diagnostic& value : event.diagnostics)
                if (value.severity != diagnostic_severity::information)
                    notices_.push_back({ value.message });
            return;
        }

        install_document(std::move(*event.document), std::move(event.revision), std::move(event.diagnostics));
    }

    void logic_controller::handle_document_generated(document_generated_event event)
    {
        // 다른 문서를 연 뒤 도착한 이전 생성 결과는 현재 상태에 적용하지 않는다.
        if (event.operation_id != pending_generation_operation_id_)
            return;
        pending_generation_operation_id_ = 0;

        if (event.document.has_value() == false || event.revision.has_value() == false)
        {
            // 실패는 현재 문서를 유지한 채 진단만 알린다. dialog는 열어 둔 채 사유를
            // 보여 주어 이름이나 위치를 고쳐 다시 시도할 수 있게 한다 (F3).
            notices_.clear();
            function_button_notice_.reset();
            for (const diagnostic& value : event.diagnostics)
                if (value.severity != diagnostic_severity::information)
                    notices_.push_back({ value.message });
            if (generate_dialog_.has_value())
            {
                generate_dialog_->executing = false;
                generate_dialog_->message = notices_.empty() ? std::u8string { u8"문서를 만들지 못했습니다." } : notices_.front().text;
            }
            return;
        }
        // 성공하면 만든 문서를 곧바로 열므로 dialog는 닫는다.
        generate_dialog_.reset();

        // 다른 문서를 여는 것이라 실행 중인 출력 수집 기능이나 저장하지 못한 변경이 있으면 먼저 묻는다 (4.10). 만든 문서는 이미 파일에
        // 있으므로, 확인하면 그 경로를 여는 길(`open_document_now`)로 다시 읽어 연다. 취소하면 지금 문서가 남고
        // 만든 파일은 나중에 열 수 있다.
        if (confirm_document_change(running_function_action::open_document, event.document_path) == false)
        {
            function_confirm_->generated_document = true;
            return;
        }

        // 생성된 문서를 곧바로 연다. 이전 문서의 진행 중 결과가 새 문서에 적용되지
        // 않도록 열기와 같은 규칙으로 대기 상태를 정리한다. 문서를 여는 것이므로
        // 열기와 같은 배치 기록 규칙을 따른다 (S3.2).
        record_app_window_placement();
        abandon_reload();
        document_path_ = std::move(event.document_path);
        document_loading_ = false;
        pending_save_operation_id_ = 0;
        save_queued_ = false;
        install_document(std::move(*event.document), std::move(*event.revision), std::move(event.diagnostics));
    }

    void logic_controller::install_document(workspace_document document, workspace_revision_token revision, std::vector<diagnostic> diagnostics)
    {
        // 생성된 문서 채택처럼 open을 거치지 않는 교체 경로도 이전 카드의 변경
        // 작업을 취소해야 한다.
        cancel_running_changes();

        // 기능 버튼 진단은 버튼마다 하나씩 생겨 따로 늘어놓으면 문서·저장 진단이 띠의
        // `(외 N건)` 뒤로 밀린다. 다른 진단 뒤에 알림 하나로 묶고 상세는 tooltip에 둔다
        // (function-buttons-design 3.8).
        notices_.clear();
        // 편집 dialog(T12)의 경고 행 사유와, 절을 편집기로 쓸 수 없는 사유다. 진단의 JSON pointer로 항목을 가른다.
        function_button_problems_.assign(document.function_buttons.has_value() ? document.function_buttons->buttons.size() : 0, std::u8string {});
        function_editor_lock_reason_.clear();
        for (const diagnostic& value : diagnostics)
        {
            constexpr std::u8string_view button_pointer { u8"/function_buttons/buttons/" };
            const std::u8string_view pointer { value.source.json_pointer };
            if (value.code == diagnostic_code::invalid_function_buttons && (pointer == u8"/function_buttons" || pointer == u8"/function_buttons/buttons"))
                function_editor_lock_reason_ = u8"문서의 function_buttons를 읽지 못해 편집기로 쓰지 않습니다. JSON을 고친 뒤 문서 다시 읽기로 불러오십시오.";
            if (value.code == diagnostic_code::invalid_function_button && pointer.starts_with(button_pointer))
            {
                std::size_t index { 0 };
                bool digits { pointer.size() > button_pointer.size() };
                for (const char8_t digit : pointer.substr(button_pointer.size()))
                {
                    if (digit < u8'0' || digit > u8'9')
                        digits = false;
                    else
                        index = index * 10 + static_cast<std::size_t>(digit - u8'0');
                }
                if (digits && index < function_button_problems_.size())
                    function_button_problems_[index] = value.message;
            }
        }
        for (const diagnostic& value : diagnostics)
        {
            if (value.severity == diagnostic_severity::information)
                continue;
            if (is_function_button_diagnostic(value) == false)
                notices_.push_back({ document_notice_text(value, document) });
        }
        // 묶은 알림은 다른 진단 뒤에 선다. 편집 dialog로 고친 저장 뒤 다시 만들 수 있게 따로 둔다.
        refresh_function_button_notice(diagnostics);

        save_notice_.clear();
        reload_notice_.reset();
        selected_.reset();
        scroll_offset_ = 0.0f;
        document_ = std::move(document);
        revision_ = std::move(revision);
        // 이전 문서의 카드를 가리키던 overlay와 dialog는 의미가 없다. 환경설정
        // 초안도 이전 문서의 settings 기준이라 함께 버린다. 탐색 dialog는 진행 중
        // 탐색을 취소하고 닫는다.
        switch_dialog_.reset();
        settings_dialog_.reset();
        local_changes_dialog_.reset();
        context_menu_.reset();
        // 기능 실행 확인은 이전 문서의 카드와 정의를 가리킨다. 초점 옮기기 요청도 이전 문서의 카드 id다 — 새 문서에
        // 같은 id가 있으면 그 element로 초점이 갈 수 있다. 번호는 되돌리지 않는다 (luil이 같은 번호를 한 번만 이행한다).
        function_confirm_.reset();
        // cmd 보정 승인은 그 문서의 정의와 값에 대한 것이다. 다시 읽거나 다른 문서를 열면 다시 묻는다 (D30).
        cmd_approvals_.clear();
        function_focus_.reset();
        // 편집 dialog의 초안과 저장 뒤 신뢰 갱신은 이전 문서의 정의다.
        function_editor_.reset();
        function_trust_after_save_ = false;
        run_waiting_for_app_settings_.reset();
        function_notice_after_save_ = false;
        unsaved_ = {};
        if (discovery_dialog_.has_value() && discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();
        discovery_dialog_.reset();

        // 문서가 배치를 담고 있으면 UI thread가 한 번 적용하도록 게시 번호를 올린다.
        window_placement_ = document_->window;
        window_placement_dirty_ = false;
        if (window_placement_.has_value())
            ++window_placement_revision_;
        cards_.clear();
        cards_.reserve(document_->projects.size());
        for (const project_definition& project : document_->projects)
        {
            card_state card {};
            card.project = project;
            card.snapshot.project = project.id;
            card.generation = 1;
            cards_.push_back(std::move(card));
        }

        // 문서를 실제로 연 시점에만 최근 목록에 남긴다 (app-shell-design A1.2).
        record_recent_document();
        // 로그 파일 적재 대상도 문서 단위로 다시 정해진다 (A4.1).
        publish_log_targets();

        // 시작 시 로컬 상태를 먼저 표시한다 (plan 5.1). 원격 판정은 명시적 refresh다.
        for (card_state& card : cards_)
        {
            if (card.project.enabled == false)
                continue;
            card.busy = true;
            static_cast<void>(submitter_->submit(make_request(operation_kind::query_local, &card, card.generation)));
        }
    }

    void logic_controller::handle_query_completed(query_completed_event event)
    {
        card_state* const card { find_card(event.id) };

        // 카드가 삭제됐거나 그 사이 refresh로 세대가 넘어간 결과는 폐기한다
        // (ADR-004 검증 항목, ADR-005 7.2).
        if (card == nullptr || event.generation != card->generation)
            return;

        card->snapshot = std::move(event.result.snapshot);
        card->diagnostics = std::move(event.result.diagnostics);
        card->has_local_result = true;

        // 조회 실패·경고는 카드 로그에도 남긴다 (2026-08-21 검수: 파일 로그에 조회
        // 결과까지 포함). 성공은 카드 표시로 충분하므로 남기지 않는다.
        for (const diagnostic& value : card->diagnostics)
            if (value.severity != diagnostic_severity::information)
                append_lifecycle_log(*card, value.severity, value.message);

        if (event.final_event)
        {
            card->busy = false;
            if (card->refresh_queued)
            {
                // 실행 중 들어온 중복 refresh는 한 번으로 병합해 뒤이어 실행한다
                // (plan 5.1).
                card->refresh_queued = false;
                request_refresh(*card);
            }
        }
    }

    void logic_controller::handle_reorder_card(const reorder_card_intent& intent)
    {
        if (shutting_down_ || document_.has_value() == false || intent.id == intent.target)
            return;

        std::size_t from { cards_.size() };
        std::size_t target { cards_.size() };
        for (std::size_t index = 0; index < cards_.size(); ++index)
        {
            if (cards_[index].project.id == intent.id)
                from = index;
            if (cards_[index].project.id == intent.target)
                target = index;
        }
        if (from == cards_.size() || target == cards_.size())
            return;

        // 꺼낸 뒤의 삽입 위치다. 앞에서 빼면 뒤 index가 하나 당겨진다.
        std::size_t to { target + (intent.place_after ? 1u : 0u) };
        if (from < to)
            --to;
        // 위치가 그대로면 문서도 그대로다. 저장할 것이 없다.
        if (from == to)
            return;

        card_state moved { std::move(cards_[from]) };
        cards_.erase(cards_.begin() + static_cast<std::ptrdiff_t>(from));
        cards_.insert(cards_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));

        // 문서의 프로젝트 순서가 진실이다. 카드 순서에서 다시 만들어 저장한다.
        document_->projects.clear();
        document_->projects.reserve(cards_.size());
        for (const card_state& card : cards_)
            document_->projects.push_back(card.project);

        request_save();
    }

    void logic_controller::handle_select_card(const select_card_intent& intent)
    {
        const std::optional<project_id> previous { selected_ };
        if (intent.id.has_value() && find_card(*intent.id) == nullptr)
            selected_.reset();
        else
            selected_ = intent.id;

        // 다른 카드의 로그를 이전 카드의 필터·스크롤로 보지 않도록 뷰 상태를
        // 기본값으로 되돌린다. 진행 중 작업은 건드리지 않는다 (plan 3.9).
        if ((selected_ == previous) == false)
        {
            reset_log_view_state();
            // 실패 점은 그 카드를 선택하면 지운다. 실패는 로그의 끝 줄(error)로 남는다
            // (function-buttons-design 2.3.5).
            card_state* const card { selected_.has_value() ? find_card(*selected_) : nullptr };
            if (card != nullptr)
                card->function_results.clear();
        }

        // `Esc`로 선택을 풀 때 Tab 초점이 기능 줄에 있었으면 그 초점은 사라진다(줄에 버튼이 없고 줄의 `⋯`는 비활성).
        // 해제 직전 카드의 `⋯`로 옮긴다 (function-buttons-design 2.2.5, D33). 배치 변경 때와 같은 요청이라 줄의 다음 버튼 →
        // 줄의 `⋯` → 카드의 `⋯` 순서로 보이는 첫 자리를 UI가 고르고, 앞 둘은 서지 않으므로 카드의 `⋯`다. 그 `⋯`도 없으면
        // (목록 밖 등) 빈 대상으로 요청을 끝낸다.
        if (intent.focus_left_function_bar && selected_.has_value() == false && previous.has_value())
        {
            function_focus_request request {};
            request.id = ++function_focus_revision_;
            request.card = *previous;
            function_focus_ = std::move(request);
        }

        // 키보드 순회는 화면 밖 카드도 고를 수 있다. 선택이 보이도록 따라간다.
        scroll_selected_into_view();
    }

    void logic_controller::reset_log_view_state() noexcept
    {
        log_filter_ = log_stream_filter::all;
        log_auto_scroll_ = true;
        log_scroll_offset_ = 0.0f;
    }

    bool logic_controller::has_log_pane() const noexcept
    {
        // 로그 pane은 문서가 열려 있는 동안 항상 열려 있다 (2026-08-20 검수:
        // 클릭해야 열리는 UX 제거). 선택 카드가 없으면 안내 제목의 빈 pane이다.
        // 시작 페이지에는 로그가 없으므로 pane도 두지 않는다 (app-shell-design A1.3).
        return document_.has_value();
    }

    float logic_controller::log_content_height() const noexcept
    {
        if (selected_.has_value() == false)
            return 0.0f;
        const operation_log_buffer* const buffer { card_log(*selected_) };
        if (buffer == nullptr)
            return 0.0f;

        // 스크롤 한계는 progress 접기 후의 표시 줄 수 기준이다 (stage-8-plan 5.3).
        const std::size_t count { log_display_line_count(buffer->records(), log_filter_) };
        return static_cast<float>(count) * layout_log_line_height() + layout_log_text_inset * 2.0f;
    }

    float logic_controller::log_viewport_height(const bool function_bar) const noexcept
    {
        // pane 높이는 창이 작으면 줄어든다. layout과 같은 함수로 계산해야 스크롤
        // 한계가 그리기와 일치한다.
        // 기능 줄이 있으면 pane 맨 위를 줄이 가져간다 (function-buttons-design 2.4). tree의 배치와 같은 함수다.
        const list_layout layout { compute_list_layout(window_height_ / scale_, 1.0f, has_notice(), has_log_pane()) };
        const float bar { function_bar ? layout_function_bar_height() : 0.0f };
        const float body { layout.log_height - bar - layout_log_header_height() };
        return body > 0.0f ? body : 0.0f;
    }

    void logic_controller::handle_log_scroll(const float delta)
    {
        if (has_log_pane() == false)
            return;

        float maximum { log_content_height() - log_viewport_height(has_function_bar()) };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { log_auto_scroll_ ? maximum : log_scroll_offset_ };
        offset += delta;
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        log_scroll_offset_ = offset;
        // 맨 아래에 닿으면 자동 스크롤로 돌아간다. 위로 올리면 꺼진다.
        log_auto_scroll_ = offset >= maximum - 0.5f;
    }

    void logic_controller::handle_toggle_path_display()
    {
        if (shutting_down_ || document_.has_value() == false)
            return;

        // 표시 방식은 문서에 남는 설정이라 순서 변경과 같은 저장 경로를 탄다.
        // 토글은 유효 값을 뒤집은 결과를 문서 override로 정의한다 (G3.1).
        document_->settings.show_relative_paths = { relative_paths() == false };
        request_save();
    }

    void logic_controller::handle_window_placement(const window_placement_intent& intent)
    {
        if (intent.placement.valid() == false)
            return;

        // 문서를 여는 순간 앱 설정에 남길 값이라 문서 유무와 무관하게 기억한다
        // (settings-tabs-and-appearance-scope-design S3.2).
        current_placement_ = intent.placement;

        if (document_.has_value())
        {
            // 문서가 열려 있는 동안의 배치는 문서에만 남는다. 앱 단위 배치는 문서
            // 없이 시작할 때의 값이라 문서의 크기·위치로 덮이면 안 된다 (S3.2).
            if (document_->window.has_value() && *document_->window == intent.placement)
                return;

            document_->window = intent.placement;
            window_placement_ = intent.placement;
            window_placement_dirty_ = true;
            return;
        }

        // 문서가 없을 때만 앱 설정에 남긴다. 저장은 begin_shutdown이 한 번 내보낸다.
        if (app_settings_.window.has_value() == false || *app_settings_.window != intent.placement)
        {
            app_settings_.window = { intent.placement };
            app_settings_window_dirty_ = true;
        }
    }

    void logic_controller::record_app_window_placement()
    {
        // 문서를 여는 순간의 배치가 "문서 없이 쓰던" 마지막 배치다 (S3.2). 문서가
        // 이미 열려 있었다면 그 배치는 문서의 것이라 앱 설정으로 올리지 않는다.
        if (document_.has_value() || current_placement_.has_value() == false)
            return;
        if (app_settings_.window.has_value() && *app_settings_.window == *current_placement_)
            return;

        app_settings_.window = current_placement_;
        app_settings_window_dirty_ = true;
    }

    void logic_controller::handle_document_saved(document_saved_event event)
    {
        // 다른 문서를 연 뒤 도착한 이전 저장 결과는 버린다.
        if (event.operation_id != pending_save_operation_id_)
            return;
        pending_save_operation_id_ = 0;

        save_notice_.clear();
        if (event.revision.has_value())
            revision_ = *event.revision;
        else
            for (const diagnostic& value : event.diagnostics)
                if (value.severity == diagnostic_severity::error)
                {
                    // 손으로 고친 문서는 다시 읽기 전까지 모든 저장이 이 사유로 실패한다. 다시 읽기로 회복하는 길을
                    // 알린다 (function-buttons-design 4.9). store가 revision이 없을 때와 파일이 바뀌었을 때 모두
                    // 이 코드를 내고, 두 경우 모두 다시 읽으면 새 revision으로 저장할 수 있다.
                    save_notice_ = value.code == diagnostic_code::concurrent_modification ? std::u8string { save_conflict_notice } : value.message;
                    break;
                }
        // 저장은 모델 전체를 쓰므로, 이어 낼 저장이 없는 성공이면 앞서 저장하지 못한 변경까지 파일에 있다 (D28). 실패하면
        // 표시가 남아 다시 읽기·닫기·다른 문서 열기·종료가 먼저 묻는다.
        if (event.revision.has_value() && save_queued_ == false)
            unsaved_ = {};
        // 편집 dialog가 고친 정의의 해시 입력과 신뢰 갱신이다 (4.11). 이어 낼 저장이 있는지 보므로 그 앞이다.
        finish_function_editor_save(event);

        if (save_queued_)
        {
            save_queued_ = false;
            request_save();
        }
        // 떠 있는 실행 중인 기능 확인은 열 때의 상태로 지었다. 저장이 진행 중일 때 열었으면 저장하지 못한 변경을 싣지
        // 않았으므로, 그 저장이 실패했으면 다시 지어 확인 전에 그 변경도 사라진다는 것을 보인다 (D28, R2 검토). 저장
        // 하나가 오가는 사이에 연 dialog에서만 생긴다. 반대로 꺼지는 쪽은 없다 — 변경을 실은 dialog는 진행 중인 저장이
        // 없을 때 열리고, 떠 있는 동안 새 저장을 낼 길이 없다.
        if (function_confirm_.has_value() && function_confirm_->mode == function_confirm_mode::running && function_confirm_->view.unsaved_changes == false && has_unsaved_changes())
        {
            const float scroll_offset { function_confirm_->view.scroll_offset };
            function_confirm_->view = build_running_function_view(function_confirm_->action);
            function_confirm_->view.scroll_offset = scroll_offset;
        }
        // 저장이 모두 끝났으면 미뤄 둔 다시 읽기를 낸다 (4.9의 2). 이어 낸 저장이 있으면 그 결과를 기다린다.
        if (reload_after_save_ && pending_save_operation_id_ == 0)
        {
            reload_after_save_ = false;
            // 기다린 저장이 실패했으면 읽으면 그 변경이 사라진다. 누를 때는 저장이 진행 중이라 묻지 않았으므로 지금
            // 묻는다 (D28). 기다림은 이미 풀어 두어 취소해도 메뉴가 `읽는 중`에 묶이지 않는다. 그사이 다른 기능 확인
            // (신뢰·미리보기·실행 중인 기능)이 떠 있으면 그것을 말없이 바꾸지 않고 다시 읽기를 거둔다. 저장 실패 알림이
            // 남아 다시 고를 수 있다 (4.9의 2).
            if (has_unsaved_changes())
            {
                if (function_confirm_.has_value() == false)
                    open_document_change_confirm(running_function_action::reload_document);
            }
            else
                begin_reload_document();
        }
    }

    void logic_controller::request_save()
    {
        if (shutting_down_ || document_.has_value() == false)
            return;
        // 문서를 쓰는 변경(편집 dialog Apply, 배치, 카드 순서, 경로 표시, 환경설정)은 모두 여기를 거친다. 저장이 성공할
        // 때까지 모델에만 있는 변경이다 (D28).
        unsaved_.document = true;
        // 다시 읽기의 결과를 기다리는 동안은 내지 않는다. 지금 모델을 쓰면 읽은 파일과 어긋나고, 다시 읽기가
        // 성공하면 모델이 바뀌므로 버리며, 실패하면 이어 낸다.
        if (pending_save_operation_id_ != 0 || pending_reload_operation_id_ != 0)
        {
            save_queued_ = true;
            return;
        }

        operation_request request { make_request(operation_kind::save_document, nullptr, 0) };
        request.document = document_;
        request.revision = revision_;
        pending_save_operation_id_ = request.operation_id;
        static_cast<void>(submitter_->submit(std::move(request)));
    }

    void logic_controller::handle_request_update(const request_update_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->project.enabled == false)
            return;

        // submodule 갱신 여부는 매번 묻지 않고 유효 설정이 정한다 (2026-08-20
        // 검수: 확인 overlay 제거).
        update_options options { intent.options };
        options.update_submodules = effective_settings().update_submodules;
        begin_change(*card, operation_kind::update, options, nullptr);
    }

    void logic_controller::handle_request_switch(const request_switch_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->project.enabled == false)
            return;

        begin_change(*card, operation_kind::switch_to, {}, &intent.target);
    }

    void logic_controller::begin_change(card_state& card, const operation_kind kind, const update_options& options, const switch_candidate* const target)
    {
        const bool is_update { kind == operation_kind::update };
        if (card.busy)
        {
            // lane 직렬화와 별개로 대기열 폭주를 막는다 (stage-7-plan 4.4). 사유는
            // 로그로 남겨 사용자가 클릭이 무시된 이유를 알 수 있다.
            append_lifecycle_log(card, diagnostic_severity::warning,
                is_update ? std::u8string { u8"이미 작업이 진행 중이라 update를 시작하지 않습니다." } : std::u8string { u8"이미 작업이 진행 중이라 switch를 시작하지 않습니다." });
            return;
        }
        // 같은 카드의 출력 수집 기능과 서로 막는다 (function-buttons-design 4.7, D6-A). 빌드
        // 스크립트와 `git pull`이 같은 작업 트리를 동시에 건드리지 않게 한다. 버튼과 메뉴는
        // 이미 비활성이고 여기가 마지막 거부다.
        for (const function_run_state& run : card.function_runs)
        {
            if (run.mode != function_run_mode::capture)
                continue;
            std::u8string text { is_update ? u8"기능이 실행 중이라 update를 시작하지 않습니다: " : u8"기능이 실행 중이라 switch를 시작하지 않습니다: " };
            text += run.label;
            append_lifecycle_log(card, diagnostic_severity::warning, std::move(text));
            return;
        }

        process_cancellation_source cancellation {};
        operation_request request { make_request(kind, &card, card.generation) };
        request.options = options;
        if (target != nullptr)
            request.switch_target = *target;
        // 변경 작업은 카드 단위 취소가 가능해야 하므로 전역 token 대신 작업별
        // token을 싣는다. 종료 시에는 cancel_running_changes가 함께 취소한다.
        request.token = cancellation.token();

        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
        {
            append_lifecycle_log(card, diagnostic_severity::error, u8"작업을 제출하지 못했습니다.");
            return;
        }

        card.busy = true;
        card.change_operation_id = operation_id;
        card.change_kind = kind;
        // 경과 시간 표시의 기준 시각이다 (로그 헤더의 MM:SS). 새 명령이 시작되면
        // 직전 명령이 남긴 경과 시간을 지운다.
        card.change_started_at = std::chrono::steady_clock::now();
        card.change_elapsed.reset();
        card.change_cancellation = std::move(cancellation);
        // 종료가 close 메시지 없이 일어나도 실행 중 프로세스가 취소되도록 등록한다.
        registry_->add(*card.change_cancellation);
        if (is_update)
            append_lifecycle_log(
                card, diagnostic_severity::information, options.update_submodules ? std::u8string { u8"update를 시작합니다 (submodule 함께 갱신)." } : std::u8string { u8"update를 시작합니다." });
        else
            append_lifecycle_log(card, diagnostic_severity::information, std::u8string { u8"switch를 시작합니다: " } + (target != nullptr ? target->display_name : std::u8string {}));
    }

    void logic_controller::handle_cancel_operation(const cancel_operation_intent& intent)
    {
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->change_operation_id == 0 || card->change_cancellation.has_value() == false)
            return;

        card->change_cancellation->request_cancellation();
        append_lifecycle_log(*card, diagnostic_severity::warning, u8"취소를 요청했습니다.");
    }

    const function_button* logic_controller::find_function_button(const std::u8string_view button_id) const noexcept
    {
        if (document_.has_value() == false || document_->function_buttons.has_value() == false)
            return nullptr;
        // 검증에 실패한 항목은 원문만 있어 실행과 화면에서 빠진다 (3.8).
        for (const function_button_entry& entry : document_->function_buttons->buttons)
        {
            const function_button* const button { std::get_if<function_button>(&entry) };
            if (button != nullptr && button->id == button_id)
                return button;
        }
        return nullptr;
    }

    variable_lookup logic_controller::make_function_lookup(const card_state& card, function_environment_lookup environment) const
    {
        variable_lookup lookup {};
        lookup.builtins = make_builtin_variables(document_path_, card.project, card.has_local_result ? &card.snapshot : nullptr);
        if (document_.has_value() && document_->function_buttons.has_value())
            lookup.user = make_user_variables(*document_->function_buttons, card.project.id.value);
        lookup.environment = std::move(environment);
        return lookup;
    }

    variable_lookup logic_controller::make_function_preview_lookup(const card_state& card) const
    {
        return make_function_lookup(card
            , [](const std::u8string_view)
            {
                return std::optional<std::u8string> { std::u8string {} };
            }
        );
    }

    bool logic_controller::function_running(const card_state& card) noexcept
    {
        return std::ranges::any_of(card.function_runs
            , [](const function_run_state& run)
            {
                return run.mode == function_run_mode::capture;
            }
        );
    }

    logic_controller::function_run_state* logic_controller::find_function_run(card_state& card, const std::uint64_t run_id) noexcept
    {
        for (function_run_state& run : card.function_runs)
        {
            if (run.run_id == run_id)
                return &run;
        }
        return nullptr;
    }

    bool logic_controller::change_allowed(const card_state& card) noexcept
    {
        return card.project.enabled && card.busy == false && card.has_local_result && card.snapshot.availability == repository_availability::ready && function_running(card) == false;
    }

    std::size_t logic_controller::function_runs_in_flight() const noexcept
    {
        std::size_t count { 0 };
        for (const card_state& card : cards_)
            count += card.function_runs.size();
        return count;
    }

    std::optional<logic_controller::function_button_state> logic_controller::evaluate_function(const card_state& card, const function_button& button, const variable_lookup& preview) const
    {
        // 적용 범위다. 범위 밖 카드에는 그 기능이 버튼으로도 메뉴로도 없다 (B4).
        if (button.project_ids.has_value() && std::ranges::find(*button.project_ids, card.project.id.value) == button.project_ids->end())
            return std::nullopt;

        function_button_state state {};
        for (const function_result_state& result : card.function_results)
        {
            if (result.button_id == button.id)
            {
                state.failed = result.failed;
                state.exit_code = result.exit_code;
            }
        }

        // 자기 자신이 실행 중이면 중지 버튼으로 활성이다. 그 사이 조회 결과(`vcs_hint: auto`의
        // 종류, 런타임 값)가 바뀌어도 중지할 수 있어야 하므로 `vcs` 범위와 변수 판정보다 앞에 둔다.
        for (const function_run_state& run : card.function_runs)
        {
            if (run.button_id == button.id && run.mode == function_run_mode::capture)
                state.running = true;
        }
        if (state.running)
        {
            state.enabled = true;
            return state;
        }

        if (button.vcs != function_vcs_scope::any)
        {
            const repository_kind wanted { button.vcs == function_vcs_scope::git ? repository_kind::git : repository_kind::subversion };
            repository_kind kind { repository_kind::unknown };
            if (card.project.hint == vcs_hint::git)
                kind = repository_kind::git;
            else if (card.project.hint == vcs_hint::subversion)
                kind = repository_kind::subversion;
            else if (card.has_local_result)
                kind = card.snapshot.kind;
            // `vcs_hint: auto`이고 조회 전이면 범위 안인지 아직 모른다. 버튼은 보이되 비활성이다 (B4).
            // 제외된 카드는 조회하지 않으므로 늘 이 상태다 (D17).
            if (kind == repository_kind::unknown)
            {
                if (card.has_local_result)
                    state.disabled_reason = u8"저장소 종류를 알 수 없습니다";
                else if (card.project.enabled)
                    state.disabled_reason = u8"저장소 종류를 아직 모릅니다";
                else
                    state.disabled_reason = unqueried_card_reason(true);
                return state;
            }
            if (kind != wanted)
                return std::nullopt;
        }

        // 사용자 변수와 런타임 값이 이 카드에서 풀리는지 본다. `${env:}`는 클릭 시점에만 보므로
        // 여기서는 어떤 이름이든 정의된 것으로 친다 (3.7, `make_function_preview_lookup`).
        std::vector<std::u8string_view> templates { button.program };
        for (const std::u8string& argument : button.args)
            templates.push_back(argument);
        templates.push_back(button.working_directory());
        bool uses_project_root { false };
        for (const std::u8string_view text : templates)
        {
            const variable_expansion expansion { expand_variables(text, preview) };
            if (expansion.error.has_value())
            {
                state.disabled_reason = function_expansion_reason(*expansion.error, card.has_local_result, card.project.enabled == false);
                return state;
            }
            // 사용자 변수 값 안의 `${projectRoot}`도 그 이름의 구간으로 남는다.
            for (const expansion_segment& segment : expansion.segments)
            {
                if (segment.variable == u8"projectRoot")
                    uses_project_root = true;
            }
        }

        // 경로가 없으면 클릭하고 나서야 프로세스 시작이 실패하므로 미리 막는다 (4.8의 5).
        if (uses_project_root && card.project.path.state != configured_path_state::available)
        {
            state.disabled_reason = u8"저장소 경로를 쓸 수 없습니다";
            return state;
        }

        // 출력 수집은 같은 카드에서 하나만 돌고 update·switch와 서로 막는다 (4.7). refresh(`busy`)는
        // 보지 않는다. 보면 초기 조회와 F5 동안 모든 출력 수집 버튼이 비활성이 된다.
        if (button.mode == function_run_mode::capture)
        {
            if (card.change_operation_id != 0)
            {
                state.disabled_reason = card.change_kind == operation_kind::switch_to ? u8"전환 중입니다" : u8"업데이트 중입니다";
                return state;
            }
            if (function_running(card))
            {
                state.disabled_reason = u8"이 카드에서 다른 기능이 실행 중입니다";
                return state;
            }
        }

        state.enabled = true;
        return state;
    }

    std::vector<card_function_view> logic_controller::card_function_views(const card_state& card, std::span<const std::optional<function_placement>> placements) const
    {
        std::vector<card_function_view> views {};
        if (document_.has_value() == false || document_->function_buttons.has_value() == false)
            return views;
        const std::span<const function_button_entry> buttons { document_->function_buttons->buttons };
        std::vector<std::optional<function_placement>> own_placements {};
        if (placements.size() != buttons.size())
        {
            own_placements = resolve_visible_placements(buttons);
            placements = own_placements;
        }

        // 치환 값 묶음은 카드마다 한 번 만든다 (T6에서 넘어온 일). 적용 범위 밖 기능에는 쓰이지 않는다.
        const variable_lookup preview { make_function_preview_lookup(card) };
        std::size_t card_number { 0 };
        std::size_t counted { 0 };
        for (std::size_t index = 0; index < buttons.size(); ++index)
        {
            const function_button* const button { std::get_if<function_button>(&buttons[index]) };
            // 검증에 실패한 항목은 원문만 있어 화면에 나오지 않는다 (3.8).
            if (button == nullptr || placements[index].has_value() == false)
                continue;
            const function_placement placement { *placements[index] };
            // `Alt+N`의 N은 문서 전체의 보이는 `card` 순서다. 이 카드에 적용되지 않는 `card`도 번호를 차지한다 (2.3.4).
            if (placement == function_placement::card)
                ++card_number;
            // 카드당 적용 기능은 20개까지다. 넘친 것은 그 카드에서 쓰지 않고 파서가 경고했다 (2.3.2, 3.8). 파서와 같은
            // 정적 기준으로 센다 — 조회 뒤 범위 밖이 된 `vcs` 한정 기능이 자리를 비워도 21번째가 올라오지 않는다.
            if (function_counts_toward_card(*button, card.project) == false || ++counted > maximum_functions_per_card)
                continue;
            const std::optional<function_button_state> state { evaluate_function(card, *button, preview) };
            if (state.has_value() == false)
                continue;

            card_function_view view {};
            view.button_id = button->id;
            view.label = button->label;
            view.icon = std::u8string { button->icon_name() };
            view.placement = placement;
            view.shortcut = placement == function_placement::card ? card_number : 0;
            view.enabled = state->enabled;
            view.disabled_reason = state->disabled_reason;
            view.running = state->running;
            view.failed = state->failed;
            view.exit_code = state->exit_code;
            for (const function_run_state& run : card.function_runs)
            {
                if (run.button_id == button->id && run.mode == function_run_mode::capture && run.phase == function_run_phase::running)
                    view.started_at = run.started_at;
            }
            views.push_back(std::move(view));
        }
        return views;
    }

    void logic_controller::handle_run_function(const run_function_intent& intent)
    {
        if (shutting_down_)
            return;
        // 확인 dialog가 떠 있는 동안에는 다른 실행을 받지 않는다. dialog가 클릭을 막지만 `Alt+N`처럼
        // 키로 오는 intent도 있다.
        if (function_confirm_.has_value())
            return;
        // 앱 설정을 읽는 중이면 신뢰 목록이 아직 비어 있다. 지금 판정하면 신뢰한 정의도 묻는다. 읽기가 끝나면
        // `handle_app_settings_loaded`가 이 실행을 다시 들인다 (5차 검수). 읽기는 실패해도 결과 event가 와서 기다림이 끝난다.
        if (app_settings_loaded_ == false && pending_app_settings_load_id_ != 0)
        {
            if (run_waiting_for_app_settings_.has_value() == false)
                run_waiting_for_app_settings_ = intent;
            return;
        }
        card_state* const card { find_card(intent.id) };
        const function_button* const button { find_function_button(intent.button_id) };
        if (card == nullptr || button == nullptr)
            return;

        const std::optional<prepared_function_run> prepared { prepare_function_run(*card, *button) };
        if (prepared.has_value() == false)
            return;
        // 편집 dialog가 바꾼 정의가 아직 파일에 없으면 해시 입력이 편집 전 원문이다. 그 해시로 판정하면 신뢰 dialog가 보일
        // 정의와 기록할 해시가 다르다 (4.11). 저장이 끝나거나 다시 읽을 때까지 실행하지 않는다.
        if (unsaved_.function_definition)
        {
            append_lifecycle_log(*card, diagnostic_severity::warning, function_log_text(button->label, u8"기능 정의가 아직 저장되지 않아 실행하지 않습니다"));
            return;
        }

        // 신뢰를 기록하지 않은 정의는 확인 없이 실행하지 않는다 (B9, 3.10). 해시는 누른 이 순간의
        // 문서 모델로 만든다 (`current_function_digest`). 신뢰한 정의라도 `confirm` 버튼은 미리보기를 거친다.
        // 신뢰하지 않은 `confirm` 버튼은 신뢰 dialog 하나가 미리보기 줄까지 함께 보인다. 두 dialog를
        // 잇따라 열면 같은 자리의 확인 버튼을 두 번 누른 것(더블클릭, Enter 반복)이 미리보기를 지나간다.
        std::u8string digest { current_function_digest() };
        if (is_function_definition_trusted(app_settings_, document_path_, digest) == false)
        {
            open_function_confirm(function_confirm_mode::trust, *card, *button, *prepared, std::move(digest));
            return;
        }
        if (button->confirm)
        {
            open_function_confirm(function_confirm_mode::preview, *card, *button, *prepared, std::move(digest));
            return;
        }
        // cmd 명령을 gitman이 보정했거나 고칠 수 없는 위험이 있으면 보정한 명령을 보이고 최종 승인을 받는다 (D30). 신뢰
        // 확인이나 `confirm` 미리보기가 함께 필요하면 위 dialog 하나가 같은 줄을 싣는다 — 잇따라 열면 같은 자리의 확인을
        // 두 번 누른 것이 두 번째 dialog를 지나간다.
        if (cmd_approval_needed(*card, *button, *prepared))
        {
            open_function_confirm(function_confirm_mode::correction, *card, *button, *prepared, std::move(digest));
            return;
        }
        submit_function_run(*card, *button, *prepared);
    }

    bool logic_controller::cmd_approval_needed(const card_state& card, const function_button& button, const prepared_function_run& prepared) const
    {
        if (prepared.cmd.has_value() == false || prepared.cmd->needs_approval() == false)
            return false;
        // 기억은 사용자가 본 그대로의 실행에만 맞는다. 값이 바뀌어 최종 명령줄, 경고(배치가 읽을 값), 작업 폴더가 달라지면
        // 다시 묻는다.
        const auto approved = [&](const cmd_approval& approval)
        {
            return approval.card == card.project.id
                && approval.button_id == button.id
                && approval.command_line == prepared.cmd->text
                && approval.warnings == prepared.cmd->warnings
                && approval.working_directory == prepared.working_directory.text;
        };
        return std::ranges::none_of(cmd_approvals_, approved);
    }

    std::optional<logic_controller::prepared_function_run> logic_controller::prepare_function_run(card_state& card, const function_button& button)
    {
        // 버튼을 그릴 때와 같은 판정을 다시 한다. 클릭과 상태 변화가 엇갈린 늦은 클릭이다 (4.8).
        // 범위 밖이면 그 카드에 버튼이 없다. 이미 실행 중이면 `Alt+N`이 중지하지 않도록 아무것도
        // 하지 않는다 (2.3.4).
        const std::optional<function_button_state> state { evaluate_function(card, button, make_function_preview_lookup(card)) };
        if (state.has_value() == false || state->running)
            return std::nullopt;
        if (state->enabled == false)
        {
            append_lifecycle_log(card, diagnostic_severity::warning, function_log_text(button.label, state->disabled_reason));
            return std::nullopt;
        }
        if (function_submitter_ == nullptr)
        {
            append_lifecycle_log(card, diagnostic_severity::error, function_log_text(button.label, u8"기능 실행 경로가 없어 실행하지 않습니다"));
            return std::nullopt;
        }

        // 클릭 시점의 값으로 치환하고 검사한다 (3.7). 실제 argv에는 env 값이 들어가고, 로그와
        // 확인 dialog에는 표시용 명령줄만 남는다 (3.6).
        variable_lookup lookup { make_function_lookup(card, function_services_.environment) };
        prepared_function_run prepared {};
        prepared.program = expand_variables(button.program, lookup);
        prepared.arguments.reserve(button.args.size());
        for (const std::u8string& argument : button.args)
            prepared.arguments.push_back(expand_variables(argument, lookup));
        prepared.working_directory = expand_variables(button.working_directory(), lookup);

        std::optional<expansion_error> error { prepared.program.error };
        for (const variable_expansion& argument : prepared.arguments)
        {
            if (error.has_value() == false)
                error = argument.error;
        }
        if (error.has_value() == false)
            error = prepared.working_directory.error;
        if (error.has_value())
        {
            reject_function_run(card, button, function_expansion_reason(*error, card.has_local_result, card.project.enabled == false));
            return std::nullopt;
        }

        const std::optional<function_program_rejection> rejection { check_function_program(prepared.program.text) };
        if (rejection.has_value())
        {
            reject_function_run(card, button, function_program_rejection_message(*rejection));
            return std::nullopt;
        }
        // stage-3 시작 계약이다 (stage-3-plan.md:59). 드라이브 루트 문서의 `${workspaceFolder}\x`처럼
        // 구분자가 겹친 경로도 Windows 경로로는 같은 곳이라 그대로 받는다.
        if (is_absolute_windows_path(prepared.working_directory.text) == false)
        {
            reject_function_run(card, button, u8"작업 폴더(cwd)가 절대 경로가 아닙니다");
            return std::nullopt;
        }
        // cmd는 cmd 명령줄로 아예 넘길 수 없는 줄바꿈만 거부한다. 메타 문자·`%`·`!`·`"`는 cmd 전용 조립이 보정하고, 배치
        // 안에서 읽힐 `GITMAN_BRANCH` 같은 런타임 값의 메타 문자는 경고로 남긴다. 보정이나 경고가 있으면 실행 전에 사용자의
        // 최종 승인을 받는다 (D30, 4.3).
        const std::optional<cmd_injection_finding> injection { find_cmd_injection(prepared.program, prepared.arguments) };
        if (injection.has_value())
        {
            reject_function_run(card, button, cmd_injection_message(*injection));
            return std::nullopt;
        }

        if (is_cmd_program(prepared.program.text))
        {
            prepared.cmd = assemble_function_cmd_command_line(prepared.program, prepared.arguments, lookup.builtins);
            // 위 검사가 같은 글자를 먼저 거르므로 오지 않는다. 조립이 거부한 명령줄을 실행 요청에 싣지 않으려는 마지막 자리다.
            if (prepared.cmd->rejection.has_value())
            {
                reject_function_run(card, button, u8"cmd.exe 명령줄로 넘길 수 없는 문자(줄바꿈)가 있습니다");
                return std::nullopt;
            }
            // 표시용 명령줄은 실제 명령줄과 같은 보정 결정으로 지은 글이다. env 값은 이름으로 남는다 (3.6).
            prepared.display_command_line = function_services_.mask(prepared.cmd->display_text);
        }
        else
        {
            prepared.display_command_line = build_display_command_line(prepared.program, prepared.arguments, function_services_.join, function_services_.mask);
        }
        prepared.builtins = std::move(lookup.builtins);
        return prepared;
    }

    void logic_controller::submit_function_run(card_state& card, const function_button& button, const prepared_function_run& prepared)
    {
        // 풀이 꽉 찼으면 대기열에 넣지 않고 거절한다. 보이지 않는 대기열을 두지 않는다 (4.4).
        if (function_runs_in_flight() >= function_execution_capacity)
        {
            append_lifecycle_log(card, diagnostic_severity::warning, function_log_text(button.label, function_capacity_message()));
            return;
        }

        function_command command {};
        command.program = prepared.program.text;
        command.arguments.reserve(prepared.arguments.size());
        for (const variable_expansion& argument : prepared.arguments)
            command.arguments.push_back(argument.text);
        command.working_directory = prepared.working_directory.text;
        // 사용자가 본(또는 보정이 없어 묻지 않은) 최종 명령줄을 그대로 싣는다. runner가 다시 짓지 않는다 (D30).
        if (prepared.cmd.has_value())
            command.command_line = prepared.cmd->text;

        function_run_state run {};
        run.run_id = next_operation_id_++;
        run.button_id = button.id;
        run.label = button.label;
        run.mode = button.mode;
        run.timeout_seconds = button.timeout_seconds;
        run.phase = button.mode == function_run_mode::capture ? function_run_phase::queued : function_run_phase::launching;
        run.display_command_line = prepared.display_command_line;

        function_run_request request {};
        request.id = card.project.id;
        request.run_id = run.run_id;
        request.command = function_services_.make_command(std::move(command), button.mode, button.timeout_seconds, prepared.builtins);
        if (button.mode == function_run_mode::capture)
        {
            // 카드 단위로 중지할 수 있고, 종료 취소가 logic thread를 거치지 않게 등록한다 (4.10).
            run.cancellation.emplace();
            request.token = run.cancellation->token();
            registry_->add(*run.cancellation);
        }
        else
        {
            // 분리 실행은 취소가 없다. 대기 중에 앱이 끝나면 띄우지 않게 종료 token만 싣는다.
            request.token = cancellation_source_.token();
        }

        card.function_runs.push_back(std::move(run));
        if (function_submitter_->submit(std::move(request)) == false)
        {
            // 남는 run이 없게 되돌린다. source가 사라지면 registry의 손잡이도 정리된다.
            card.function_runs.pop_back();
            append_lifecycle_log(card, diagnostic_severity::warning, function_log_text(button.label, function_capacity_message()));
            return;
        }

        // 실패 점은 그 기능을 다시 실행하면 지운다 (2.3.5).
        std::erase_if(card.function_results
            , [&button](const function_result_state& result)
            {
                return result.button_id == button.id;
            }
        );

        // 분리 실행의 `시작:` 줄은 결과를 받은 뒤에 남긴다 (2.3.6).
        if (button.mode == function_run_mode::capture)
            append_lifecycle_log(card, diagnostic_severity::information, function_log_text(button.label, u8"실행: " + card.function_runs.back().display_command_line));
    }

    std::u8string logic_controller::current_function_digest() const
    {
        if (document_.has_value() == false || document_->function_buttons.has_value() == false)
            return {};
        // 해시 입력의 출처와 낡는 시점은 `function_trust.h`에 적었다. 여기서는 그때그때 지금 모델로
        // 만든다 — 누를 때와 확인할 때 두 번 부르고, 두 값이 다르면 확인한 정의가 아니다.
        //  - `source_digest_input`: 문서를 읽을 때(열기, T11의 다시 읽기) 파일 원문에서 만든 값이다.
        //    `bar`·`menu` 사이 배치 변경(T10)은 해시 입력이 아니라 그대로 맞다. 저장이 성공하면 store가 쓴 글을 다시
        //    읽은 값으로 바뀐다 (`finish_function_editor_save`, T12). 편집 dialog가 고친 정의와 `card`에 넣거나 뺀 배치(D31)는
        //    그 저장이 끝나기 전까지 앞 값이다.
        //  - project `{id, path.original, ${projectName} 값}`: 지금 `document_->projects`다. 등록으로 project가 늘면
        //    해시가 바뀌어 다시 묻는다.
        return function_trust_digest(*document_->function_buttons, document_->projects);
    }

    void logic_controller::open_function_confirm(const function_confirm_mode mode, const card_state& card, const function_button& button, const prepared_function_run& prepared, std::u8string digest)
    {
        function_confirm_state state {};
        state.mode = mode;
        state.card = card.project.id;
        state.button_id = button.id;
        state.digest = std::move(digest);
        state.prepared = prepared;
        switch (mode)
        {
        case function_confirm_mode::trust:
            state.view = build_function_trust_view(button, prepared);
            break;
        case function_confirm_mode::preview:
            state.view = build_function_preview_view(card, button, prepared);
            break;
        case function_confirm_mode::correction:
            state.view = build_function_correction_view(card, button, prepared);
            break;
        case function_confirm_mode::running:
            break;
        }
        // 열린 메뉴가 dialog 뒤에 남지 않게 한다 (카드 메뉴의 기능 절에서 연 경우, T9).
        context_menu_.reset();
        function_confirm_ = { std::move(state) };
    }

    void logic_controller::open_vcs_executables_confirm()
    {
        // 다른 확인이 떠 있으면 열지 않는다. 메뉴가 떠 있는 사이 신뢰했거나 문서가 바뀌었으면 할 것이 없다.
        if (shutting_down_ || function_confirm_.has_value() || vcs_executables_notice().has_value() == false)
            return;

        // 기능 신뢰 dialog와 같은 틀이다. 목록에는 실행 파일마다 한 항목을 두고 경로를 그대로 보인다.
        function_confirm_dialog_view view {};
        view.mode = function_confirm_mode::trust;
        view.title = u8"실행 파일 신뢰 확인";
        view.subject = document_path_;
        view.description = {
            u8"이 문서가 지정한 실행 파일을 아직 신뢰하지 않았습니다.",
            u8"신뢰하면 저장소를 조회·업데이트·전환할 때 실행합니다.",
        };
        view.note = {
            u8"신뢰는 실행 파일의 내용을 확인하지 않습니다.",
            u8"문서의 실행 파일 경로가 바뀌면 다시 묻습니다.",
        };
        view.confirm_label = u8"신뢰";
        const auto add_item = [&view](const std::u8string_view label, const std::optional<std::u8string>& path)
        {
            if (path.has_value() == false || path->empty())
                return;
            function_confirm_item_view item {};
            item.label = label;
            item.mode_text = u8"문서 설정";
            item.lines.push_back({ *path, false });
            view.items.push_back(std::move(item));
        };
        add_item(u8"Git 실행 파일", document_->settings.git_executable);
        add_item(u8"SVN 실행 파일", document_->settings.svn_executable);

        function_confirm_state state {};
        state.mode = function_confirm_mode::trust;
        state.vcs_executables = true;
        state.digest = document_vcs_executables_digest();
        state.view = std::move(view);
        context_menu_.reset();
        function_confirm_ = { std::move(state) };
    }

    function_confirm_dialog_view logic_controller::build_function_trust_view(const function_button& clicked, const prepared_function_run& prepared) const
    {
        function_confirm_dialog_view view {};
        view.mode = function_confirm_mode::trust;
        view.subject = document_path_;
        // 안내·경고 줄은 480 폭 창과 글 크기 150%에서도 말줄임되지 않게 짧게 나눈다. 경고는 가장 중요한
        // 것(스크립트 내용은 덮지 않음)을 앞에 둔다.
        view.description = {
            u8"이 문서의 기능 정의를 아직 신뢰하지 않았습니다.",
            u8"신뢰하면 아래 프로그램이 이 PC에서 실행됩니다.",
        };
        view.note = {
            u8"신뢰는 스크립트 파일의 내용을 확인하지 않습니다.",
            u8"기능 정의나 저장소 경로·이름이 바뀌면 다시 묻습니다.",
        };
        view.confirm_label = u8"신뢰하고 실행";
        if (document_.has_value() == false || document_->function_buttons.has_value() == false)
            return view;

        // 신뢰는 문서의 정의 전체에 걸리므로 검증을 통과한 기능을 모두 보인다. 누른 기능이 맨 앞이고
        // 나머지는 문서 순서다. 값은 치환 전 원문이다. 원문에 적은 비밀도 표시용 명령줄과 같은
        // 가리기를 거친다 (1.3의 NFR-008).
        const function_buttons_definition& definition { *document_->function_buttons };
        const auto mask = [this](const std::u8string_view text)
        {
            return function_services_.mask ? function_services_.mask(text) : std::u8string { text };
        };
        const auto add_item = [&](const function_button& button)
        {
            function_confirm_item_view item {};
            item.label = button.label;
            item.mode_text = function_mode_text(button.mode);
            item.lines.push_back({ mask(button.program), false });
            if (button.args.empty() == false)
                item.lines.push_back({ mask(function_template_arguments(button.args)), false });
            item.lines.push_back({ mask(u8"작업 폴더 " + std::u8string { button.working_directory() }), false });
            for (const std::u8string& name : referenced_user_variables(button))
            {
                const std::optional<std::u8string> line { function_variable_line(definition, name) };
                if (line.has_value())
                    item.lines.push_back({ mask(*line), true });
            }
            view.items.push_back(std::move(item));
        };

        add_item(clicked);
        // 누른 기능이 `confirm`이면 미리보기 줄을 그 항목에 잇는다. 확인 한 번으로 신뢰와 미리보기를 함께
        // 끝내므로 두 번째 dialog가 없다 — 이어 뜬 dialog의 같은 자리 버튼을 더블클릭이나 Enter 반복이
        // 지나가는 일을 없앤다. 다음 실행부터는 신뢰한 정의라 미리보기 dialog만 뜬다 (3.10의 "별개").
        // cmd 보정이나 경고가 있어도 같다. 원래 명령, 실행할 명령, 바뀐 자리와 이유, 경고를 이 항목에 싣고 확인 한 번이
        // 보정 승인까지 끝낸다 (D30).
        if (clicked.confirm || (prepared.cmd.has_value() && prepared.cmd->needs_approval()))
        {
            for (function_confirm_line& line : function_preview_lines(prepared, u8"실행할 명령 ", u8"실행할 작업 폴더 "))
                view.items.front().lines.push_back(std::move(line));
        }
        for (const function_button_entry& entry : definition.buttons)
        {
            const function_button* const button { std::get_if<function_button>(&entry) };
            if (button != nullptr && button->id != clicked.id)
                add_item(*button);
        }
        return view;
    }

    function_confirm_dialog_view logic_controller::build_function_preview_view(const card_state& card, const function_button& button, const prepared_function_run& prepared) const
    {
        function_confirm_dialog_view view {};
        view.mode = function_confirm_mode::preview;
        view.subject = function_project_name(card.project);
        view.description = {
            u8"실행 전 확인을 켠 기능입니다.",
            u8"아래 명령을 이 PC에서 실행합니다.",
        };
        view.confirm_label = u8"실행";

        function_confirm_item_view item {};
        item.label = button.label;
        item.mode_text = function_mode_text(button.mode);
        item.lines = function_preview_lines(prepared, {}, u8"작업 폴더 ");
        view.items.push_back(std::move(item));
        return view;
    }

    std::vector<function_confirm_line> logic_controller::function_preview_lines(const prepared_function_run& prepared, const std::u8string_view command_prefix, const std::u8string_view folder_prefix) const
    {
        const auto mask = [this](const std::u8string_view text)
        {
            return function_services_.mask ? function_services_.mask(text) : std::u8string { text };
        };
        const bool corrected { prepared.cmd.has_value() && prepared.cmd->needs_approval() };
        std::vector<function_confirm_line> lines {};

        // 보정 전의 명령이다. 문서에 적은 인자 배열을 보통의 argv 규칙으로 이은 글이고, env 값은 이름으로 남는다 (3.6).
        if (corrected)
        {
            std::vector<std::u8string> displayed {};
            displayed.reserve(prepared.arguments.size());
            for (const variable_expansion& argument : prepared.arguments)
                displayed.push_back(display_text(argument));
            std::u8string original { u8"원래 명령 " };
            if (function_services_.join)
                original += function_services_.join(display_text(prepared.program), std::span<const std::u8string> { displayed }, command_line_syntax::argv);
            lines.push_back({ mask(original), false });
        }

        // 로그 시작 줄과 같은 표시용 명령줄이다. env 값은 이름으로 남고 비밀은 가린다 (3.6). 작업
        // 폴더도 같은 가리기를 거친다.
        std::u8string command { corrected && command_prefix.empty() ? std::u8string_view { u8"실행할 명령 " } : command_prefix };
        command += prepared.display_command_line;
        lines.push_back({ std::move(command), false });
        std::u8string folder { folder_prefix };
        folder += display_text(prepared.working_directory);
        lines.push_back({ mask(folder), false });
        if (corrected == false)
            return lines;

        // 바뀐 자리와 이유다. 원래 글과 보정한 글은 표시용 글(env는 이름)이고 비밀 가리기를 거친다.
        for (const cmd_correction& correction : prepared.cmd->corrections)
        {
            std::u8string changed { u8"보정 " };
            if (correction.argument.has_value())
            {
                changed += u8"args[";
                changed += function_decimal_text(*correction.argument);
                changed += u8"]: ";
                changed += correction.original;
                // 명령 문장으로만 넘긴 인자는 글이 그대로라 `X → X`가 되므로 무엇을 했는지를 쓴다.
                if (correction.corrected == correction.original)
                    changed += u8" (명령 문장으로 넘김)";
                else
                    changed += u8" → " + correction.corrected;
            }
            else
            {
                changed += u8"스위치: " + correction.corrected;
            }
            lines.push_back({ mask(changed), false });
            lines.push_back({ u8"이유: " + cmd_correction_reason(correction.kinds), true });
        }
        // gitman이 고칠 수 없는 위험이다. 경고색으로 그린다.
        for (const cmd_warning& warning : prepared.cmd->warnings)
            lines.push_back({ mask(u8"경고 " + cmd_warning_message(warning)), false, false, true });
        return lines;
    }

    function_confirm_dialog_view logic_controller::build_function_correction_view(const card_state& card, const function_button& button, const prepared_function_run& prepared) const
    {
        function_confirm_dialog_view view {};
        view.mode = function_confirm_mode::correction;
        view.subject = function_project_name(card.project);
        // 480 폭 창과 글 크기 150%에서도 말줄임되지 않게 짧게 나눈다. 보정 없이 경고만 있으면(저장소 루트의 괄호) 보정했다고
        // 말하지 않는다.
        if (prepared.cmd.has_value() && prepared.cmd->corrections.empty())
        {
            view.description = {
                u8"cmd.exe 명령에 확인할 경고가 있습니다.",
                u8"실행할 명령과 경고를 확인하고 실행합니다.",
                u8"같은 명령은 다시 묻지 않습니다.",
            };
        }
        else
        {
            view.description = {
                u8"gitman이 cmd.exe 명령을 보정했습니다.",
                u8"보정한 명령과 경고를 확인하고 실행합니다.",
                u8"같은 명령은 다시 묻지 않습니다.",
            };
        }
        view.confirm_label = u8"실행";

        function_confirm_item_view item {};
        item.label = button.label;
        item.mode_text = function_mode_text(button.mode);
        item.lines = function_preview_lines(prepared, u8"실행할 명령 ", u8"작업 폴더 ");
        view.items.push_back(std::move(item));
        return view;
    }

    function_confirm_dialog_view logic_controller::wrapped_function_confirm_view() const
    {
        function_confirm_dialog_view view { function_confirm_->view };
        // 창 폭이 오기 전이면 dialog 기본 폭으로 센다.
        const float scale { scale_ > 0.0f ? scale_ : 1.0f };
        const float width { window_width_ > 0.0f ? window_width_ / scale : layout_function_confirm_dialog_width };
        const std::size_t columns { function_confirm_code_columns(width) };
        for (function_confirm_item_view& item : view.items)
        {
            item.label = visible_confirm_text(item.label);
            std::vector<function_confirm_line> lines {};
            lines.reserve(item.lines.size());
            for (const function_confirm_line& line : item.lines)
            {
                const std::vector<std::u8string> chunks { wrap_confirm_text(visible_confirm_text(line.text), columns) };
                for (std::size_t index = 0; index < chunks.size(); ++index)
                    lines.push_back({ chunks[index], line.dim, index > 0, line.warning });
            }
            item.lines = std::move(lines);
        }
        return view;
    }

    void logic_controller::handle_confirm_function_run()
    {
        if (function_confirm_.has_value() == false)
            return;
        const function_confirm_state pending { std::move(*function_confirm_) };
        function_confirm_.reset();

        // 실행 중인 기능 확인이다 (4.10). 미뤄 둔 일을 이어 한다. 그 길이 출력 수집 기능을 취소한다 — 닫기·열기는 곧바로,
        // 다시 읽기는 읽기에 성공해 문서를 바꿀 때다. 앱 종료는 확인 버튼이 UI thread 명령을 내므로 여기 오지 않는다.
        if (pending.mode == function_confirm_mode::running)
        {
            switch (pending.action)
            {
            case running_function_action::close_document:
                close_document_now();
                break;
            case running_function_action::open_document:
                open_document_now(pending.path);
                break;
            case running_function_action::reload_document:
                begin_reload_document();
                break;
            case running_function_action::exit_application:
                break;
            }
            return;
        }

        // 문서 실행 파일의 신뢰다 (S1). 사용자가 본 경로의 해시를 기록하고, 요청 설정이 바뀌므로 켜진 카드를 다시 조회해
        // 문서의 실행 파일로 바꾼다. dialog가 떠 있는 동안 경로가 바뀌었으면(환경설정은 이 dialog 뒤에서 열 수 없지만 다시
        // 읽기 성공은 dialog를 닫는다) 본 값이 아니므로 기록하지 않는다.
        if (pending.vcs_executables)
        {
            if (shutting_down_ || document_path_.empty() || pending.digest.empty() || document_vcs_executables_digest() != pending.digest)
                return;
            const workspace_settings before { request_settings() };
            if (trust_vcs_executables(app_settings_, document_path_, pending.digest))
                request_app_settings_save();
            refresh_cards_after_request_settings_change(before);
            return;
        }

        // 사용자가 본 정의의 해시를 기록한다. 저장에 실패해도 `app_settings_`에 남아 이 실행 동안은
        // 다시 묻지 않는다 (3.10, `docs/user-manual.md` 3장 끝의 앱 설정 규칙).
        if (pending.mode == function_confirm_mode::trust && document_path_.empty() == false && pending.digest.empty() == false)
        {
            if (trust_function_definition(app_settings_, document_path_, pending.digest))
                request_app_settings_save();
        }

        card_state* const card { find_card(pending.card) };
        const function_button* const button { find_function_button(pending.button_id) };
        if (shutting_down_ || card == nullptr || button == nullptr)
            return;

        // 확인한 것과 같은 것만 실행한다 (4.5). dialog가 떠 있는 동안 조회 결과(`${branch}`)나 정의가
        // 바뀌었을 수 있으므로 처음부터 다시 판정하고 치환한다.
        const std::optional<prepared_function_run> prepared { prepare_function_run(*card, *button) };
        if (prepared.has_value() == false)
            return;
        // dialog가 떠 있는 동안 정의가 바뀌는 길은 편집 dialog의 Apply다 (문서 열기·교체·닫기·다시 읽기는 dialog를 닫는다).
        // Apply 뒤 저장이 끝나기 전에는 해시 입력이 아직 편집 전 원문이라 해시만 보면 같으므로 저장 전 표시도 본다. 저장이
        // 끝났으면 해시가 바뀌어 잡힌다. dialog는 저장 전 표시가 꺼져 있을 때만 열린다 (`handle_run_function`).
        if (unsaved_.function_definition || current_function_digest() != pending.digest)
        {
            append_lifecycle_log(*card, diagnostic_severity::warning, function_log_text(button->label, u8"확인한 뒤 기능 정의가 바뀌어 실행하지 않습니다"));
            return;
        }
        const auto texts_of = [](const std::vector<variable_expansion>& values)
        {
            std::vector<std::u8string> texts {};
            texts.reserve(values.size());
            for (const variable_expansion& value : values)
                texts.push_back(value.text);
            return texts;
        };
        // 실제 argv와 작업 폴더까지 비교한다. 표시용 명령줄만 보면 env 값이 바뀐 것을 놓친다.
        // 내보내는 `GITMAN_*` 환경(`prepared.builtins`)은 비교하지 않는다. dialog에 보이지 않는 값이고,
        // 조회로 바뀌는 런타임 값(`${branch}` 등)은 인자에 쓰였으면 위 비교가 잡는다.
        // cmd는 사용자가 본 최종 명령줄과 경고까지 같아야 한다. 경고는 인자에 쓰지 않은 값(배치가 읽을 `GITMAN_BRANCH`)도
        // 담으므로 argv 비교만으로는 놓친다 (D30).
        const auto same_cmd = [](const std::optional<cmd_command_line>& left, const std::optional<cmd_command_line>& right)
        {
            if (left.has_value() != right.has_value())
                return false;
            return left.has_value() == false || (left->text == right->text && left->warnings == right->warnings);
        };
        const bool same {
            prepared->program.text == pending.prepared.program.text
            && texts_of(prepared->arguments) == texts_of(pending.prepared.arguments)
            && prepared->working_directory.text == pending.prepared.working_directory.text
            && prepared->display_command_line == pending.prepared.display_command_line
            && same_cmd(prepared->cmd, pending.prepared.cmd)
        };
        if (same == false)
        {
            append_lifecycle_log(*card, diagnostic_severity::warning, function_log_text(button->label, u8"확인한 뒤 실행 값이 바뀌어 실행하지 않습니다"));
            return;
        }
        // dialog가 보정과 경고를 보였으면 승인을 기억한다. 신뢰·미리보기 dialog에 실은 경우도 같다 (D30).
        if (cmd_approval_needed(*card, *button, *prepared))
            cmd_approvals_.push_back({ card->project.id, button->id, prepared->cmd->text, prepared->cmd->warnings, prepared->working_directory.text });
        submit_function_run(*card, *button, *prepared);
    }

    void logic_controller::handle_function_confirm_scroll(const function_confirm_scroll_intent& intent)
    {
        if (function_confirm_.has_value() == false)
            return;

        // 스크롤 한계는 element와 같은 줄 나누기로 센다.
        const function_confirm_dialog_view view { wrapped_function_confirm_view() };
        float content { 0.0f };
        for (std::size_t index = 0; index < view.items.size(); ++index)
        {
            if (index > 0)
                content += layout_function_confirm_item_gap;
            content += layout_function_confirm_item_height(view.items[index].lines.size());
        }
        const float scale { scale_ > 0.0f ? scale_ : 1.0f };
        const float viewport { function_confirm_list_height(window_height_ / scale, view.description.size() + 1, view.note.size()) };

        // 키 스크롤이다 (B6, 키보드만으로 모든 항목에 닿는다). 방향키는 코드 세 줄, 쪽 키는 목록 높이에서
        // 한 줄을 남긴 만큼, Home·End는 끝까지다.
        const float line { layout_function_confirm_code_line_height() * 3.0f };
        const float page_rest { viewport - layout_function_confirm_code_line_height() };
        const float page { page_rest > line ? page_rest : line };
        float delta { intent.delta };
        switch (intent.step)
        {
        case function_confirm_scroll_step::delta:
            break;
        case function_confirm_scroll_step::line_up:
            delta = -line;
            break;
        case function_confirm_scroll_step::line_down:
            delta = line;
            break;
        case function_confirm_scroll_step::page_up:
            delta = -page;
            break;
        case function_confirm_scroll_step::page_down:
            delta = page;
            break;
        case function_confirm_scroll_step::start:
            delta = -content;
            break;
        case function_confirm_scroll_step::end:
            delta = content;
            break;
        }
        function_confirm_->view.scroll_offset = clamp_scroll_offset(function_confirm_->view.scroll_offset + delta, content, viewport);
    }

    void logic_controller::handle_stop_function(const stop_function_intent& intent)
    {
        card_state* const card { find_card(intent.id) };
        if (card == nullptr)
            return;

        // 끝 줄 `취소됨`은 completed가 남긴다. 대기 중이던 실행도 같은 줄로 끝난다.
        for (function_run_state& run : card->function_runs)
        {
            if (run.button_id == intent.button_id && run.cancellation.has_value())
                run.cancellation->request_cancellation();
        }
    }

    void logic_controller::reject_function_run(card_state& card, const function_button& button, const std::u8string_view reason)
    {
        std::u8string text { u8"실행하지 않습니다: " };
        text += reason;
        append_lifecycle_log(card, diagnostic_severity::error, function_log_text(button.label, text));
        set_function_result(card, button.id, true, std::nullopt);
    }

    void logic_controller::set_function_result(card_state& card, const std::u8string_view button_id, const bool failed, const std::optional<std::int32_t> exit_code)
    {
        for (function_result_state& result : card.function_results)
        {
            if (result.button_id == button_id)
            {
                result.failed = failed;
                result.exit_code = exit_code;
                return;
            }
        }
        card.function_results.push_back({ std::u8string { button_id }, failed, exit_code });
    }

    void logic_controller::handle_function_started(const function_started_event& event)
    {
        card_state* const card { find_card(event.id) };
        function_run_state* const run { card == nullptr ? nullptr : find_function_run(*card, event.run_id) };
        // 끝났거나 문서를 닫은 뒤의 늦은 event다.
        if (run == nullptr || run->mode != function_run_mode::capture)
            return;

        run->phase = function_run_phase::running;
        run->started_at = event.started_at;
        // 로그 헤더는 가장 나중에 시작한 작업의 경과 시간을 보인다. 직전 명령이 남긴 값을 지운다.
        card->change_elapsed.reset();
    }

    void logic_controller::handle_function_log(function_log_event event)
    {
        card_state* const card { find_card(event.id) };
        const function_run_state* const run { card == nullptr ? nullptr : find_function_run(*card, event.run_id) };
        if (run == nullptr)
            return;

        for (operation_log_entry& entry : event.entries)
        {
            entry.text = function_log_text(run->label, entry.text);
            append_card_log(*card, std::move(entry));
        }
    }

    void logic_controller::handle_function_completed(function_completed_event event)
    {
        card_state* const card { find_card(event.id) };
        function_run_state* const found { card == nullptr ? nullptr : find_function_run(*card, event.run_id) };
        // executor가 예외 경로에서 completed를 한 번 더 보낼 수 있다. 이미 끝난 run은 버린다 (T5).
        if (found == nullptr)
            return;

        // 끝 줄은 실행을 시작할 때의 라벨과 timeout으로 남긴다. 항목을 빼면 source도 사라진다.
        const function_run_state run { std::move(*found) };
        std::erase_if(card->function_runs
            , [&run](const function_run_state& value)
            {
                return value.run_id == run.run_id;
            }
        );

        // 시작하지 못한 결과는 첫 진단을 끝 줄의 사유로 쓴다. 그 진단은 아래에서 다시 남기지 않는다.
        const diagnostic* const problem { first_problem(event.diagnostics) };
        const bool reason_in_end_line { event.outcome == function_run_outcome::start_failed || event.outcome == function_run_outcome::failed };
        const std::u8string reason { problem == nullptr ? std::u8string {} : problem->message };
        std::u8string text {};
        bool failed { true };
        switch (event.outcome)
        {
        case function_run_outcome::exited:
            if (event.exit_code.has_value())
            {
                text = u8"종료 코드 " + function_exit_code_text(*event.exit_code) + u8" · " + function_duration_text(event.duration);
                failed = *event.exit_code != 0;
            }
            else
                text = u8"종료 코드를 알 수 없습니다 · " + function_duration_text(event.duration);
            break;
        case function_run_outcome::timed_out:
            text = u8"시간 초과(" + decimal_text(run.timeout_seconds) + u8"초)";
            break;
        case function_run_outcome::cancelled:
            // 사용자 중지는 실패 표식을 남기지 않는다 (2.3.5).
            text = u8"취소됨";
            failed = false;
            break;
        case function_run_outcome::start_failed:
            text = reason.empty() ? std::u8string { u8"시작하지 못했습니다" } : u8"시작하지 못했습니다: " + reason;
            break;
        case function_run_outcome::failed:
            text = reason.empty() ? std::u8string { u8"실행하지 못했습니다" } : u8"실행하지 못했습니다: " + reason;
            break;
        case function_run_outcome::launched:
            text = u8"시작: " + run.display_command_line;
            failed = false;
            break;
        }
        diagnostic_severity severity { failed ? diagnostic_severity::error : diagnostic_severity::information };
        if (event.outcome == function_run_outcome::cancelled)
            severity = diagnostic_severity::warning;

        append_lifecycle_log(*card, severity, function_log_text(run.label, text));
        set_function_result(*card, run.button_id, failed, event.exit_code);

        // 끝 줄 뒤에 runner가 단 경고와 오류를 남긴다 (`handle_change_completed`와 같다). 출력
        // 잘림, pipe 읽기 실패, sink 예외, 출력 pipe를 잡은 손자 정리처럼 정상 종료에도 붙는
        // 진단이다 (4.1, 6.3). 잘림 문구도 원인별로 이 진단이 맡는다.
        for (const diagnostic& value : event.diagnostics)
        {
            if (value.severity == diagnostic_severity::information || restates_function_end(value) || (reason_in_end_line && &value == problem))
                continue;
            append_lifecycle_log(*card, value.severity, function_log_text(run.label, value.message));
        }

        // 끝나도 걸린 시간을 헤더에 남긴다. 소요 시간은 started부터 잰 값이다 (4.5).
        if (run.phase == function_run_phase::running)
            card->change_elapsed = { std::chrono::duration_cast<std::chrono::steady_clock::duration>(event.duration) };
        // 결과를 VCS 상태로 해석하지 않으므로 refresh를 걸지 않는다 (B8).
    }

    void logic_controller::handle_operation_log(operation_log_event event)
    {
        card_state* const card { find_card(event.id) };
        // 끝난 작업이나 다른 문서의 늦은 로그는 버린다. 같은 작업의 로그는 executor가
        // 완료 event보다 먼저 보내므로 (같은 producer, FIFO) 유실되지 않는다.
        if (card == nullptr || event.operation_id != card->change_operation_id)
            return;

        for (operation_log_entry& entry : event.entries)
            append_card_log(*card, std::move(entry));
    }

    void logic_controller::handle_change_completed(change_completed_event event)
    {
        card_state* const card { find_card(event.id) };
        if (card == nullptr || event.operation_id != card->change_operation_id)
            return;

        // 명령이 끝나도 걸린 시간은 남긴다. 작업이 끝난 뒤에 확인할 수 있어야 한다
        // (2026-08-22 지시). 다음 명령이 시작될 때 지워진다.
        card->change_elapsed = { std::chrono::steady_clock::now() - card->change_started_at };
        card->change_operation_id = 0;
        card->change_cancellation.reset();
        card->busy = false;

        // 실행 직후 재조회한 상태만 반영한다. 도구 부재처럼 조회 없이 차단된 결과의
        // 빈 snapshot으로 카드 상태를 지우지 않는다.
        card->diagnostics = std::move(event.result.diagnostics);
        if (event.result.snapshot.availability != repository_availability::unknown)
        {
            card->snapshot = std::move(event.result.snapshot);
            card->has_local_result = true;
        }

        const bool is_update { event.kind == operation_kind::update };
        if (event.result.executed && event.result.succeeded)
            append_lifecycle_log(*card, diagnostic_severity::information, is_update ? std::u8string { u8"update가 완료되었습니다." } : std::u8string { u8"switch가 완료되었습니다." });
        else if (event.result.executed)
            append_lifecycle_log(*card, diagnostic_severity::error, is_update ? std::u8string { u8"update가 실패했습니다." } : std::u8string { u8"switch가 실패했습니다." });
        else if (event.result.blocked_by != update_block_reason::none)
            append_lifecycle_log(*card, diagnostic_severity::warning, std::u8string { update_block_reason_message(event.result.blocked_by) });
        else if (event.result.rejected_by != switch_rejection::none)
            append_lifecycle_log(*card, diagnostic_severity::warning, std::u8string { switch_rejection_message(event.result.rejected_by) });
        else
            append_lifecycle_log(*card, diagnostic_severity::error, is_update ? std::u8string { u8"update가 실행되지 않았습니다." } : std::u8string { u8"switch가 실행되지 않았습니다." });

        // 실패 원인 진단도 로그에서 추적할 수 있어야 한다 (REQ-008).
        for (const diagnostic& value : card->diagnostics)
            if (value.severity != diagnostic_severity::information)
                append_lifecycle_log(*card, value.severity, value.message);

        // 실행을 기다리던 switch dialog에 결과를 반영한다. 재검증 거부는 dialog가
        // 사유를 표시한 채 남고 (REQ-007), 실행된 전환은 성패와 관계없이 dialog를
        // 닫는다. 결과는 카드 로그와 상태로 확인한다.
        if (switch_dialog_.has_value() && switch_dialog_->card == event.id && switch_dialog_->executing && event.kind == operation_kind::switch_to)
        {
            switch_dialog_->executing = false;
            if (event.result.executed)
                switch_dialog_.reset();
            else
            {
                switch_dialog_->tracking_confirm_pending = false;
                if (event.result.rejected_by != switch_rejection::none)
                    switch_dialog_->message = std::u8string { switch_rejection_message(event.result.rejected_by) };
                else
                    switch_dialog_->message = u8"전환이 실행되지 않았습니다. 카드 로그를 확인하세요.";
            }
        }

        // 성공 여부와 관계없이 상태를 다시 조회한다 (plan 5.2의 8, 5.3의 9).
        // provider의 재조회는 로컬뿐이므로 remote-first 판정까지 이어 실행한다.
        if (shutting_down_ == false && card->project.enabled)
        {
            card->refresh_queued = false;
            request_refresh(*card);
        }
    }

    void logic_controller::handle_begin_discovery(const begin_discovery_intent& intent)
    {
        // 탐색 등록은 열린 문서에 추가하는 경로다 (stage-8-plan 5.2). 이미 열린
        // dialog가 있으면 무시한다 (dialog가 화면을 덮어 정상 경로에서는 오지 않는다).
        if (shutting_down_ || document_.has_value() == false || discovery_dialog_.has_value())
            return;

        process_cancellation_source cancellation {};
        operation_request request { make_request(operation_kind::discover_projects, nullptr, 0) };
        request.scan_root = intent.scan_root;
        // 탐색은 기존 등록 항목과의 중복 판정을 위해 문서 사본이 필요하다 (단계 5).
        request.document = document_;
        // dialog 취소가 탐색을 함께 취소할 수 있게 작업별 token을 싣는다.
        request.token = cancellation.token();
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        discovery_dialog_state dialog {};
        dialog.scan_root = intent.scan_root;
        dialog.scan_operation_id = operation_id;
        dialog.scan_cancellation = std::move(cancellation);
        discovery_dialog_ = { std::move(dialog) };
        // 변경 작업과 같은 이유로 등록한다 (설계 L3).
        registry_->add(*discovery_dialog_->scan_cancellation);
    }

    void logic_controller::handle_discovery_completed(discovery_completed_event event)
    {
        // 닫힌 dialog나 다른 탐색의 늦은 결과는 버린다.
        if (discovery_dialog_.has_value() == false || event.operation_id != discovery_dialog_->scan_operation_id)
            return;

        discovery_dialog_->scan_operation_id = 0;
        discovery_dialog_->scan_cancellation.reset();
        discovery_dialog_->loading = false;
        discovery_dialog_->result = std::move(event.result);
        discovery_dialog_->scroll_offset = 0.0f;

        // 등록 가능한 후보는 기본 체크다 (stage-8-plan 5.2).
        discovery_dialog_->checked.assign(discovery_dialog_->result.candidates.size(), false);
        for (std::size_t index = 0; index < discovery_dialog_->result.candidates.size(); ++index)
            if (discovery_dialog_->result.candidates[index].selectable())
                discovery_dialog_->checked[index] = true;

        if (discovery_dialog_->result.completed == false)
        {
            discovery_dialog_->message = u8"탐색이 끝까지 완료되지 않았습니다.";
            for (const diagnostic& value : discovery_dialog_->result.diagnostics)
                if (value.severity == diagnostic_severity::error)
                {
                    discovery_dialog_->message = value.message;
                    break;
                }
        }
    }

    void logic_controller::handle_toggle_discovery_candidate(const std::size_t index)
    {
        if (discovery_dialog_.has_value() == false || discovery_dialog_->loading || discovery_dialog_->register_operation_id != 0)
            return;
        if (index >= discovery_dialog_->result.candidates.size() || discovery_dialog_->result.candidates[index].selectable() == false)
            return;

        discovery_dialog_->checked[index] = discovery_dialog_->checked[index] == false;
        discovery_dialog_->message.clear();
    }

    void logic_controller::handle_confirm_discovery()
    {
        if (discovery_dialog_.has_value() == false || discovery_dialog_->loading || discovery_dialog_->register_operation_id != 0 || shutting_down_ || document_.has_value() == false)
            return;

        std::vector<discovery_candidate> selected {};
        for (std::size_t index = 0; index < discovery_dialog_->result.candidates.size(); ++index)
            if (discovery_dialog_->checked[index])
                selected.push_back(discovery_dialog_->result.candidates[index]);
        if (selected.empty())
            return;

        // 진행 중인 일반 저장과 등록이 겹치면 store의 revision 충돌로 실패한다.
        // 자초한 충돌 대신 안내를 표시하고 저장이 끝난 뒤 다시 시도하게 한다.
        if (pending_save_operation_id_ != 0)
        {
            discovery_dialog_->message = u8"문서 저장이 진행 중입니다. 잠시 후 다시 시도하세요.";
            return;
        }

        operation_request request { make_request(operation_kind::register_projects, nullptr, 0) };
        request.document = document_;
        request.revision = revision_;
        request.discovery_selection = std::move(selected);
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
        {
            discovery_dialog_->message = u8"등록 작업을 제출하지 못했습니다.";
            return;
        }

        discovery_dialog_->register_operation_id = operation_id;
        discovery_dialog_->message.clear();
    }

    void logic_controller::handle_projects_registered(projects_registered_event event)
    {
        if (discovery_dialog_.has_value() == false || event.operation_id != discovery_dialog_->register_operation_id)
            return;
        discovery_dialog_->register_operation_id = 0;

        if (event.document.has_value() == false || event.revision.has_value() == false)
        {
            // 실패(저장 충돌 포함)는 dialog를 유지한 채 사유를 표시한다. 사용자는
            // 그대로 다시 시도할 수 있다 (stage-8-plan 5.2).
            discovery_dialog_->message = u8"등록에 실패했습니다.";
            for (const diagnostic& value : event.diagnostics)
                if (value.severity == diagnostic_severity::error)
                {
                    discovery_dialog_->message = value.message;
                    break;
                }
            return;
        }

        // 등록된 문서를 활성 문서로 바꾼다 (단계 5 계약). 기존 카드의 상태·로그를
        // 보존하기 위해 install_document 대신 새 프로젝트만 카드로 추가한다.
        document_ = std::move(*event.document);
        revision_ = std::move(*event.revision);
        // 등록은 `request_save`를 거치지 않고 모델 전체(`request.document = document_`)를 쓴다. 그래서 앞서 쓰기 실패로
        // 저장하지 못한 변경도 이제 파일에 있다 (D28, R2 검토). 충돌로 실패한 변경이면 등록도 같은 revision으로 충돌해
        // 여기 오지 않는다. 등록이 나간 뒤 낸 저장이 있으면 그 결과가 정한다. 기능 정의 표시(`function_definition`)는
        // 그대로 둔다 — 등록 결과는 쓴 글의 해시 입력을 싣지 않아 `source_digest_input`이 아직 낡았다 (4.11).
        if (pending_save_operation_id_ == 0 && save_queued_ == false)
            unsaved_.document = false;
        for (const project_definition& project : document_->projects)
        {
            if (find_card(project.id) != nullptr)
                continue;

            card_state card {};
            card.project = project;
            card.snapshot.project = project.id;
            card.generation = 1;
            cards_.push_back(std::move(card));

            // 새 카드는 로컬 상태를 곧바로 조회한다 (plan 5.1).
            card_state& added { cards_.back() };
            if (added.project.enabled)
            {
                added.busy = true;
                static_cast<void>(submitter_->submit(make_request(operation_kind::query_local, &added, added.generation)));
            }
        }
        discovery_dialog_.reset();
        // 등록으로 카드가 늘었으면 로그 폴더 이름도 다시 계산한다 (A4.1).
        publish_log_targets();
    }

    void logic_controller::handle_close_document()
    {
        if (shutting_down_ || document_.has_value() == false)
            return;
        // 실행 중인 출력 수집 기능이나 저장하지 못한 변경이 있으면 먼저 묻는다 (4.10, D28). 확인하면 `close_document_now`가
        // 이어 닫는다.
        if (confirm_document_change(running_function_action::close_document) == false)
            return;
        close_document_now();
    }

    void logic_controller::close_document_now()
    {
        if (shutting_down_ || document_.has_value() == false)
            return;

        // 카드가 사라지기 전에 진행 중인 변경 작업을 취소한다 (문서 열기와 같다). 출력 수집 기능도 함께 취소하고
        // join하지 않는다. 늦게 온 event는 run id로 버린다 (4.10).
        cancel_running_changes();
        abandon_reload();
        // 아직 문서에 반영되지 않은 창 배치는 닫기 전에 한 번 내보낸다. 이미 제출된
        // 저장은 worker가 끝까지 수행하고 늦은 결과는 id 비교로 버려진다.
        if (window_placement_dirty_)
        {
            window_placement_dirty_ = false;
            request_save();
        }

        document_path_.clear();
        document_.reset();
        revision_ = {};
        cards_.clear();
        notices_.clear();
        function_button_notice_.reset();
        save_notice_.clear();
        selected_.reset();
        filter_.clear();
        scroll_offset_ = 0.0f;
        document_loading_ = false;
        reset_log_view_state();
        // 이전 문서를 가리키던 dialog와 메뉴는 의미가 없다.
        switch_dialog_.reset();
        settings_dialog_.reset();
        local_changes_dialog_.reset();
        context_menu_.reset();
        // 기능 실행 확인과 초점 옮기기 요청은 이전 문서의 카드와 정의를 가리킨다. 편집 dialog도 같다.
        function_confirm_.reset();
        // cmd 보정 승인은 그 문서의 정의와 값에 대한 것이다. 다시 읽거나 다른 문서를 열면 다시 묻는다 (D30).
        cmd_approvals_.clear();
        function_focus_.reset();
        function_editor_.reset();
        function_trust_after_save_ = false;
        run_waiting_for_app_settings_.reset();
        function_notice_after_save_ = false;
        unsaved_ = {};
        function_button_problems_.clear();
        function_editor_lock_reason_.clear();
        if (discovery_dialog_.has_value() && discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();
        discovery_dialog_.reset();
        // 문서를 닫으면 다시 "문서 없이 쓰는" 상태다. 문서가 남긴 크기·위치를 그대로
        // 두지 않고 앱 단위 배치로 되돌린다 (D1). 게시 번호를 올려 UI thread가 한 번
        // 적용한다 — 문서를 열 때 문서 배치를 적용하는 것과 같은 경로다.
        window_placement_ = app_settings_.window;
        current_placement_ = app_settings_.window;
        if (window_placement_.has_value())
            ++window_placement_revision_;
        // 진행 중이던 저장·생성 결과는 도착해도 버린다.
        pending_save_operation_id_ = 0;
        save_queued_ = false;
        pending_generation_operation_id_ = 0;
        // 문서를 닫으면 더 쌓을 로그가 없다. 파일은 그대로 두고 적재만 멈춘다.
        publish_log_targets();
    }

    void logic_controller::refresh_function_button_notice(const std::vector<diagnostic>& diagnostics)
    {
        function_button_notice_ = make_function_button_notice(diagnostics);
    }

    bool logic_controller::reload_available() const noexcept
    {
        return shutting_down_ == false
            && document_.has_value()
            && document_path_.empty() == false
            && document_loading_ == false
            && pending_reload_operation_id_ == 0
            && reload_after_save_ == false;
    }

    context_menu_item_view logic_controller::reload_menu_item() const
    {
        // 비활성 사유는 라벨 접미다 (2.3.2, `카드에 두기 — 3개까지`). 문서가 없거나 여는 중이면 메뉴 자체가 그 상태를
        // 말하므로 다시 읽는 중일 때만 붙인다.
        const bool reading { pending_reload_operation_id_ != 0 || reload_after_save_ };
        context_menu_item_view item { .entry = context_menu_entry::reload_document, .label = u8"문서 다시 읽기", .enabled = reload_available(), .icon = u8"sync" };
        if (reading)
            item.label = u8"문서 다시 읽기 — 읽는 중";
        return item;
    }

    void logic_controller::handle_reload_document()
    {
        // 다시 읽기가 이미 진행 중이면 한 번으로 족하다. 메뉴 항목도 그동안 비활성이다.
        if (reload_available() == false)
            return;
        // 실행 중인 출력 수집 기능이나 저장하지 못한 변경이 있으면 먼저 묻는다 (4.9의 1, D28). 하나의 dialog에 함께 싣는다.
        // 확인해도 취소는 읽기가 성공해 문서를 바꿀 때 한다 — 읽기에 실패하면 지금 문서와 함께 실행과 변경도 그대로
        // 남는다. 저장이 진행 중이면 아직 실패하지 않았으므로 묻지 않고, 그 저장이 실패하면 그때 묻는다.
        if (confirm_document_change(running_function_action::reload_document) == false)
            return;
        begin_reload_document();
    }

    void logic_controller::begin_reload_document()
    {
        if (reload_available() == false)
            return;
        reload_notice_.reset();
        // 저장 충돌 알림은 다시 읽기를 안내한다. 그 안내를 따랐으니 걷는다 — 남기면 띠 첫 줄을 차지해 다시 읽기의 실패를
        // 가린다. 실패한 뒤 저장이 다시 충돌하면 그때 다시 선다.
        if (save_notice_ == save_conflict_notice)
            save_notice_.clear();
        // 진행 중인 저장이 있으면 끝난 뒤 읽는다 (4.9의 2). 저장이 쓴 파일을 읽어야 그 revision이 맞는다.
        if (pending_save_operation_id_ != 0)
        {
            reload_after_save_ = true;
            return;
        }

        // 지금 문서·카드·로그·revision은 건드리지 않는다 (4.9의 3). 열기 경로와 달리 load 전에 비우지 않는다.
        operation_request request { make_request(operation_kind::load_document, nullptr, 0) };
        request.document_path = document_path_;
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;
        pending_reload_operation_id_ = operation_id;
    }

    void logic_controller::handle_document_reloaded(document_loaded_event event)
    {
        pending_reload_operation_id_ = 0;
        // 종료가 시작된 뒤의 결과로 문서를 바꾸거나 조회를 내지 않는다.
        if (shutting_down_)
            return;

        // 읽지 못했거나(파일이 없음, JSON 오류, schema 오류) error 진단이 하나라도 있으면 지금 문서·카드·로그·실행을
        // 그대로 두고 알림 띠에 알린다 (4.9의 4, B10). 열기는 project 하나의 오류처럼 문서가 오는 error를 받지만, 다시
        // 읽기는 손 편집의 실수로 그 project를 뺀 문서로 바꾸고 실행을 취소하지 않으려고 받지 않는다. 고쳐서 다시
        // 읽으면 된다.
        const bool has_error { std::ranges::any_of(event.diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::error; }) };
        if (event.document.has_value() == false || has_error)
        {
            // 사유는 상세 줄이다. 문서가 왔으면 막은 error만 project 이름을 붙여 싣는다 — 기능 버튼 경고 같은 warning은
            // 다시 읽기를 막지 않았다.
            notice_view_model notice { std::u8string { reload_failed_notice }, {} };
            for (const diagnostic& value : event.diagnostics)
            {
                if (event.document.has_value() == false)
                {
                    if (value.severity != diagnostic_severity::information)
                        notice.details.push_back(value.message);
                }
                else if (value.severity == diagnostic_severity::error)
                    notice.details.push_back(document_notice_text(value, *event.document));
            }
            reload_notice_ = { std::move(notice) };
            // 기다리게 한 저장을 이어 낸다. 손 편집으로 파일이 바뀌었으면 저장 충돌로 알린다.
            if (save_queued_)
            {
                save_queued_ = false;
                request_save();
            }
            return;
        }

        // 다시 읽은 문서가 모델을 바꾸므로 기다리게 한 저장은 버린다.
        save_queued_ = false;
        // 창 배치는 닫기와 종료에서만 문서에 저장한다. 아직 저장하지 않은 배치는 새 모델에 덮어 두고 dirty를 남겨
        // 닫기·종료 때 저장한다 (4.9의 6). 화면의 배치를 그대로 두려고 UI thread에 다시 알리는 값도 이 배치다.
        const bool placement_dirty { window_placement_dirty_ && document_->window.has_value() };
        const std::optional<window_placement> placement { placement_dirty ? document_->window : std::nullopt };

        // 교체다 (4.9의 5). `install_document`가 출력 수집 기능과 변경 작업을 취소하고, 카드 상태와 로그를 새로 만들고,
        // 모든 카드를 다시 조회한다. revision도 새 파일의 것이라 손 편집 뒤의 저장이 회복된다.
        install_document(std::move(*event.document), std::move(event.revision), std::move(event.diagnostics));
        if (placement.has_value())
        {
            document_->window = placement;
            window_placement_ = placement;
            window_placement_dirty_ = true;
        }
    }

    void logic_controller::abandon_reload()
    {
        if (pending_reload_operation_id_ != 0)
            abandoned_reload_operation_ids_.push_back(pending_reload_operation_id_);
        pending_reload_operation_id_ = 0;
        reload_after_save_ = false;
        reload_notice_.reset();
    }

    void logic_controller::handle_request_exit()
    {
        if (shutting_down_)
            return;
        // driver는 출력 수집 기능이 실행 중이거나 저장하지 못한 변경이 있을 때만 닫힘 훅을 건다. 그 frame을 게시한 뒤
        // 기능이 끝났으면 실행 중인 것이 없다는 dialog가 뜬다 — logic은 UI thread의 끝내기 명령을 낼 수 없어 확인 버튼에 맡긴다.
        function_confirm_state state {};
        state.mode = function_confirm_mode::running;
        state.action = running_function_action::exit_application;
        state.view = build_running_function_view(running_function_action::exit_application);
        context_menu_.reset();
        function_confirm_ = { std::move(state) };
    }

    std::size_t logic_controller::running_capture_count() const noexcept
    {
        std::size_t count { 0 };
        for (const card_state& card : cards_)
        {
            for (const function_run_state& run : card.function_runs)
            {
                if (run.mode == function_run_mode::capture)
                    ++count;
            }
        }
        return count;
    }

    bool logic_controller::has_unsaved_changes() const noexcept
    {
        // 진행 중인 저장은 아직 실패하지 않았다. 그 결과를 기다리는 다시 읽기는 실패하면 그때 묻는다 (`handle_document_saved`).
        return unsaved_.document && pending_save_operation_id_ == 0;
    }

    bool logic_controller::confirm_document_change(const running_function_action action, const std::u8string_view path)
    {
        // 확인이 없는 것은 출력 수집 기능이 0개이고 저장하지 못한 변경이 없을 때뿐이다. 분리 실행은 job 밖이라 남으므로
        // 묻지 않는다 (4.10). 저장하지 못한 변경은 모델에만 있어 문서를 바꾸거나 닫으면 사라진다 (D28).
        if (running_capture_count() == 0 && has_unsaved_changes() == false)
            return true;
        open_document_change_confirm(action, path);
        return false;
    }

    void logic_controller::open_document_change_confirm(const running_function_action action, const std::u8string_view path)
    {
        function_confirm_state state {};
        state.mode = function_confirm_mode::running;
        state.action = action;
        state.path = path;
        state.view = build_running_function_view(action);
        // 열린 메뉴가 dialog 뒤에 남지 않게 한다. 떠 있던 신뢰 확인·미리보기는 이 dialog가 대신한다 — 그 실행은
        // 닫기·열기·다시 읽기와 함께 사라질 일이다.
        context_menu_.reset();
        function_confirm_ = { std::move(state) };
    }

    void logic_controller::handle_cancel_function_run()
    {
        if (function_confirm_.has_value() == false)
            return;
        const function_confirm_state pending { std::move(*function_confirm_) };
        function_confirm_.reset();
        // 새 문서 만들기의 결과를 열지 않았다. 생성 dialog는 이미 닫혔고 파일은 디스크에 있으므로 그 경로를 알린다 —
        // 알리지 않으면 만든 문서가 어디 있는지 다시 찾아야 한다.
        if (pending.mode == function_confirm_mode::running && pending.generated_document)
            notices_.push_back({ u8"새 문서를 만들었지만 열지 않았습니다.", { pending.path } });
    }

    function_confirm_dialog_view logic_controller::build_running_function_view(const running_function_action action) const
    {
        function_confirm_dialog_view view {};
        view.mode = function_confirm_mode::running;
        view.exit_application = action == running_function_action::exit_application;

        // 개수와 이름을 보인다 (4.10, `빌드 (frontend) 외 1개`). 항목은 카드 순서, 카드 안에서는 시작 순서다.
        std::vector<function_running_entry> entries {};
        for (const card_state& card : cards_)
        {
            for (const function_run_state& run : card.function_runs)
            {
                if (run.mode != function_run_mode::capture)
                    continue;
                entries.push_back({ run.label, std::u8string { function_project_name(card.project) } });

                function_confirm_item_view item {};
                item.label = function_running_entry_text(entries.back());
                item.mode_text = run.phase == function_run_phase::running ? u8"실행 중" : u8"시작 대기";
                item.lines.push_back({ run.display_command_line, false });
                view.items.push_back(std::move(item));
            }
        }
        view.subject = function_running_summary(entries);
        view.unsaved_changes = has_unsaved_changes();

        // 실행 중인 것은 없고 저장하지 못한 변경만 있다 (D28). 같은 틀에 목록 없이 무엇이 사라지는지만 말한다. 확인
        // 버튼은 `중지하고 …`와 같은 꼴로 그 일을 하면 변경을 버린다는 것을 밝힌다. 사라진다는 말은 경고 줄(경고색과
        // 아이콘)이다 — 함께 뜨는 dialog의 `저장하지 못한 변경도 사라집니다.`와 같은 자리여야 단독일 때 덜 눈에 띄지
        // 않는다 (R2 검토). 안내 줄에는 다시 읽기의 실패 때 일만 남는다.
        if (entries.empty() && view.unsaved_changes)
        {
            view.subject = u8"저장하지 못한 변경이 있습니다";
            switch (action)
            {
            case running_function_action::close_document:
                view.note = { u8"문서를 닫으면 저장하지 못한 변경이 사라집니다." };
                view.confirm_label = u8"버리고 닫기";
                break;
            case running_function_action::open_document:
                view.note = { u8"다른 문서를 열면 저장하지 못한 변경이 사라집니다." };
                view.confirm_label = u8"버리고 열기";
                break;
            case running_function_action::reload_document:
                view.description = { u8"읽지 못하면 지금 문서와 변경을 그대로 둡니다." };
                view.note = { u8"다시 읽은 문서로 바꾸면 저장하지 못한 변경이 사라집니다." };
                view.confirm_label = u8"버리고 읽기";
                break;
            case running_function_action::exit_application:
                view.note = { u8"종료하면 저장하지 못한 변경이 사라집니다." };
                view.confirm_label = u8"버리고 종료";
                break;
            }
            return view;
        }

        // 안내 두 줄과 경고 한 줄이다. 신뢰 dialog처럼 480 폭과 글 크기 150%에서 말줄임되지 않게 짧게 나눈다.
        switch (action)
        {
        case running_function_action::close_document:
            view.description = { u8"문서를 닫으면 실행 중인 기능을 중지합니다.", u8"따로 실행한 기능은 그대로 둡니다." };
            view.confirm_label = u8"중지하고 닫기";
            break;
        case running_function_action::open_document:
            view.description = { u8"다른 문서를 열면 실행 중인 기능을 중지합니다.", u8"따로 실행한 기능은 그대로 둡니다." };
            view.confirm_label = u8"중지하고 열기";
            break;
        case running_function_action::reload_document:
            // 저장하지 못한 변경도 함께 보이면 읽지 못했을 때 그 변경도 남는다는 것을 단독 dialog처럼 밝힌다 (R2 검토).
            view.description = {
                u8"다시 읽은 문서로 바꿀 때 실행 중인 기능을 중지합니다."
                , view.unsaved_changes ? u8"읽지 못하면 중지하지 않고 지금 문서와 변경을 둡니다." : u8"읽지 못하면 중지하지 않고 지금 문서를 둡니다."
            };
            view.confirm_label = u8"중지하고 읽기";
            break;
        case running_function_action::exit_application:
            view.description = { u8"종료하면 실행 중인 기능을 중지합니다.", u8"따로 실행한 기능은 그대로 둡니다." };
            view.confirm_label = u8"중지하고 종료";
            break;
        }
        if (entries.empty())
        {
            // 닫힘 훅을 건 frame 뒤에 기능이 끝났거나, 훅을 건 저장하지 못한 변경의 다시 저장이 진행 중이다 (앱 종료에서만
            // 생긴다). 대상 줄이 `실행 중인 기능이 없습니다`라 안내는 같은 말을 되풀이하지 않고 누르면 무엇이 되는지만
            // 말한다. 원인이 둘이라 그 까닭은 말하지 않는다 (R2 검토).
            view.description = { u8"지금은 멈출 기능이 없어 바로 종료할 수 있습니다." };
            view.confirm_label = view.exit_application ? u8"종료" : u8"확인";
            return view;
        }
        // 중지는 job 종료라 유예가 없다 (4.10). 그래서 확인을 받는다.
        view.note = { u8"중지하면 끝나기를 기다리지 않고 바로 끝냅니다." };
        // 저장하지 못한 변경도 같은 확인으로 버린다. dialog를 둘 띄우지 않고 경고 줄 하나를 더한다 (D28).
        if (view.unsaved_changes)
            view.note.push_back(u8"저장하지 못한 변경도 사라집니다.");
        return view;
    }

    void logic_controller::handle_show_notice(show_notice_intent intent)
    {
        notice_dialog_view dialog {};
        dialog.title = std::move(intent.title);
        dialog.lines = std::move(intent.lines);
        dialog.error = intent.error;
        notice_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_app_settings_loaded(app_settings_loaded_event event)
    {
        if (event.operation_id != pending_app_settings_load_id_)
            return;
        pending_app_settings_load_id_ = 0;

        // 읽기가 끝나기 전에 문서를 열었으면 그 항목이 파일의 어떤 항목보다 최신이다.
        // 읽은 목록을 바탕으로 깔고 그 위에 오래된 것부터 다시 올린다.
        const std::vector<recent_document> pending { std::move(app_settings_.recent_documents) };
        // 읽기 전에 이미 받은 창 배치(시작 직후 종료의 드문 경로)도 파일 값보다
        // 최신이라 보존한다.
        const std::optional<window_placement> pending_window { app_settings_window_dirty_ ? app_settings_.window : std::nullopt };
        // 읽기 전에 신뢰한 기능 정의도 파일 값보다 최신이다 (function-buttons-design 3.10). 덮어쓰면
        // 방금 신뢰한 문서를 다시 묻고, 뒤따르는 저장이 그 항목 없이 파일을 쓴다.
        const std::vector<trusted_function_definition> pending_trust { std::move(app_settings_.trusted_function_definitions) };
        // 읽기 전에 환경설정에서 고른 문서 실행 파일의 신뢰도 같다 (S1).
        const std::vector<trusted_function_definition> pending_executable_trust { std::move(app_settings_.trusted_vcs_executables) };
        app_settings_ = std::move(event.settings);
        app_settings_shadow_ = std::move(event.shadow_source_json);
        app_settings_loaded_ = true;
        if (pending_window.has_value())
            app_settings_.window = pending_window;
        for (std::size_t index = pending.size(); index > 0; --index)
        {
            const recent_document& value { pending[index - 1] };
            touch_recent_document(app_settings_, value.path, value.opened_at);
        }
        bool trust_changed { false };
        for (const trusted_function_definition& value : pending_trust)
        {
            if (trust_function_definition(app_settings_, value.document_path, value.digest))
                trust_changed = true;
        }
        for (const trusted_function_definition& value : pending_executable_trust)
        {
            if (trust_vcs_executables(app_settings_, value.document_path, value.digest))
                trust_changed = true;
        }

        for (const diagnostic& value : event.diagnostics)
            if (value.severity != diagnostic_severity::information)
                notices_.push_back({ value.message });

        // 아직 어떤 배치도 적용되지 않았으면 앱 설정의 마지막 배치를 복원한다 (G1).
        // 문서가 자기 배치를 이미 적용했으면(=revision이 올라갔으면) 문서가 우선이다.
        if (window_placement_revision_ == 0 && app_settings_.window.has_value())
        {
            window_placement_ = app_settings_.window;
            ++window_placement_revision_;
        }

        if (pending.empty() == false || trust_changed || app_settings_save_queued_)
        {
            app_settings_save_queued_ = false;
            request_app_settings_save();
        }

        // 읽기 전에 연 문서의 실행 파일은 신뢰 목록을 몰라 쓰지 않았다 (S1). 읽은 목록에서 신뢰했으면 문서의 실행 파일로
        // 다시 조회한다. 앱 설정의 다른 값이 바뀐 것만으로는 다시 조회하지 않는다(기존 동작).
        if (document_.has_value() && document_vcs_executables_digest().empty() == false && document_vcs_executables_trusted())
        {
            for (card_state& card : cards_)
                if (card.project.enabled)
                    request_refresh(card);
        }

        // 읽는 동안 누른 기능을 이제 판정한다. 같은 길이라 늦은 클릭 판정(카드·기능이 아직 있는지, 활성인지)도 다시 한다.
        if (run_waiting_for_app_settings_.has_value())
        {
            const run_function_intent waiting { std::move(*run_waiting_for_app_settings_) };
            run_waiting_for_app_settings_.reset();
            handle_run_function(waiting);
        }
    }

    void logic_controller::handle_app_settings_saved(app_settings_saved_event event)
    {
        if (event.operation_id != pending_app_settings_save_id_)
            return;
        pending_app_settings_save_id_ = 0;

        if (event.succeeded)
            app_settings_shadow_ = std::move(event.shadow_source_json);
        else if (app_settings_save_notified_ == false)
        {
            // 실행 파일 폴더가 보호된 위치면 매번 실패한다. 사유는 한 번만 알리고
            // 최근 목록은 이 세션의 메모리에만 남긴다 (app-shell-design A1.1).
            app_settings_save_notified_ = true;
            for (const diagnostic& value : event.diagnostics)
                if (value.severity != diagnostic_severity::information)
                {
                    notices_.push_back({ value.message });
                    break;
                }
        }

        if (app_settings_save_queued_)
        {
            app_settings_save_queued_ = false;
            request_app_settings_save();
        }
    }

    void logic_controller::handle_remove_recent_document(const remove_recent_document_intent& intent)
    {
        if (remove_recent_document(app_settings_, intent.path) == false)
            return;
        request_app_settings_save();
    }

    void logic_controller::handle_toggle_recent_document_pin(const toggle_recent_document_pin_intent& intent)
    {
        // 지금 상태의 반대로 바꾼다. 목록에 없는 경로는 아무 일도 하지 않는다.
        const std::u8string key { recent_document_key(intent.path) };
        const auto found { std::ranges::find_if(app_settings_.recent_documents, [&key](const recent_document& value) { return recent_document_key(value.path) == key; }) };
        if (found == app_settings_.recent_documents.end())
            return;
        if (set_recent_document_pinned(app_settings_, intent.path, found->pinned == false) == false)
            return;
        request_app_settings_save();
    }

    void logic_controller::record_recent_document()
    {
        if (document_path_.empty())
            return;

        touch_recent_document(app_settings_, document_path_, format_utc_timestamp(std::chrono::system_clock::now()));
        request_app_settings_save();
    }

    void logic_controller::request_app_settings_save()
    {
        if (shutting_down_)
            return;
        // 읽기가 끝나기 전에 저장하면 파일에 있던 항목을 지운다. 결과가 도착한 뒤
        // 합쳐서 한 번만 쓴다.
        if (app_settings_loaded_ == false || pending_app_settings_save_id_ != 0)
        {
            app_settings_save_queued_ = true;
            return;
        }

        operation_request request { make_request(operation_kind::save_app_settings, nullptr, 0) };
        request.app_settings_payload = app_settings_;
        request.app_settings_shadow = app_settings_shadow_;
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;
        pending_app_settings_save_id_ = operation_id;
    }

    void logic_controller::handle_cancel_discovery_dialog()
    {
        if (discovery_dialog_.has_value() == false)
            return;
        // 등록 실행 중에는 닫지 않는다. 결과를 dialog가 보고해야 하고 store 저장은
        // 취소할 수 없다.
        if (discovery_dialog_->register_operation_id != 0)
            return;

        if (discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();
        discovery_dialog_.reset();
    }

    void logic_controller::handle_discovery_dialog_scroll(const float delta)
    {
        if (discovery_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(discovery_dialog_->result.candidates.size()) * layout_discovery_dialog_row_height() };
        float maximum { content - layout_discovery_dialog_list_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { discovery_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        discovery_dialog_->scroll_offset = offset;
    }

    std::vector<std::u8string> logic_controller::filtered_font_families() const
    {
        std::vector<std::u8string> families {};
        if (font_dialog_.has_value() == false)
            return families;

        // 조합 중이면 조합 글이 섞인 표시 글로 먼저 거르고, 그 글로 하나도 맞지
        // 않으면 확정 글로 물러선다 (search_query_for).
        const auto any_match = [this](const std::u8string_view probe) {
            if (contains_ignoring_ascii_case(default_font_display_name, probe))
                return true;
            for (const std::u8string& family : font_families_)
                if (contains_ignoring_ascii_case(family, probe))
                    return true;
            return false;
        };
        const std::u8string query { search_query_for(text_input_target::font_filter, font_dialog_->query.text, any_match) };

        // 첫 항목은 내장 글꼴로 되돌리는 선택지다. 표시 이름으로 걸러진다.
        families.reserve(font_families_.size() + 1u);
        if (contains_ignoring_ascii_case(default_font_display_name, query))
            families.emplace_back();
        for (const std::u8string& family : font_families_)
            if (contains_ignoring_ascii_case(family, query))
                families.push_back(family);
        return families;
    }

    void logic_controller::handle_open_generate_document()
    {
        if (shutting_down_)
            return;
        generate_dialog_ = { generate_dialog_state {} };
    }

    void logic_controller::handle_confirm_generate_document()
    {
        if (generate_dialog_.has_value() == false || generate_dialog_->executing)
            return;
        const std::u8string path { generate_document_path() };
        if (path.empty())
            return;

        generate_document_intent intent {};
        intent.scan_root = generate_dialog_->scan_root;
        intent.document_path = path;
        handle_generate_document(intent);
        generate_dialog_->executing = pending_generation_operation_id_ != 0;
        if (generate_dialog_->executing == false)
            generate_dialog_->message = u8"문서 생성을 시작하지 못했습니다.";
    }

    std::u8string logic_controller::generate_document_path() const
    {
        if (generate_dialog_.has_value() == false)
            return {};
        const std::u8string_view name { generate_dialog_->name.text };
        if (name.empty() || generate_dialog_->scan_root.empty())
            return {};
        if (generate_dialog_->same_folder == false && generate_dialog_->location.empty())
            return {};
        if (generate_document_name_error(name).empty() == false)
            return {};

        std::u8string folder { generate_dialog_->same_folder ? generate_dialog_->scan_root : generate_dialog_->location };
        while (folder.empty() == false && (folder.back() == u8'\\' || folder.back() == u8'/'))
            folder.pop_back();
        if (folder.empty())
            return {};
        folder.push_back(u8'\\');
        folder.append(name);
        folder.append(u8".version-list");
        return folder;
    }

    void logic_controller::handle_open_font_dialog(const open_font_dialog_intent& intent)
    {
        if (shutting_down_)
            return;
        font_dialog_state dialog {};
        dialog.code = intent.code;
        font_dialog_ = { std::move(dialog) };

        // 지금 쓰는 글꼴을 미리 골라 둔다. 목록에 없으면 아무것도 고르지 않는다.
        // 환경설정 dialog의 **초안**을 기준으로 한다 — 문서 override를 편집 중일 수
        // 있다 (2026-08-23 지시).
        const font_settings current_fonts { settings_dialog_.has_value() ? font_settings { settings_dialog_->ui_font_family, settings_dialog_->code_font_family } : effective_fonts() };
        const std::u8string& current { intent.code ? current_fonts.code_family : current_fonts.ui_family };
        const std::vector<std::u8string> families { filtered_font_families() };
        for (std::size_t index = 0; index < families.size(); ++index)
            if (families[index] == current)
            {
                font_dialog_->selected = index;
                break;
            }
        handle_font_dialog_scroll(0.0f);
    }

    void logic_controller::handle_select_font_family(const std::size_t index)
    {
        if (font_dialog_.has_value() == false || index >= filtered_font_families().size())
            return;
        font_dialog_->selected = { index };
    }

    void logic_controller::handle_confirm_font_dialog()
    {
        if (font_dialog_.has_value() == false || font_dialog_->selected.has_value() == false)
            return;
        const std::vector<std::u8string> families { filtered_font_families() };
        if (*font_dialog_->selected >= families.size())
            return;

        // 글꼴도 테마·키 컬러와 같은 초안이다 (2026-08-23 지시). 고른 값은 환경설정
        // dialog의 초안으로 가고 `저장`에서 앱 설정 또는 문서 override가 된다.
        // 건드린 행은 문서 모드에서 "문서에 정의됨"이 된다 (G3.2 암묵 덮어쓰기).
        if (settings_dialog_.has_value())
        {
            if (font_dialog_->code)
            {
                settings_dialog_->code_font_family = families[*font_dialog_->selected];
                settings_dialog_->code_font_defined = true;
            }
            else
            {
                settings_dialog_->ui_font_family = families[*font_dialog_->selected];
                settings_dialog_->ui_font_defined = true;
            }
        }
        font_dialog_.reset();
    }

    void logic_controller::handle_font_dialog_scroll(const float delta)
    {
        if (font_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(filtered_font_families().size()) * layout_font_dialog_row_height() };
        float maximum { content - layout_font_dialog_list_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { font_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        font_dialog_->scroll_offset = offset;
    }

    void logic_controller::handle_open_settings()
    {
        // 문서가 열려 있으면 문서 override를, 없으면 전역 설정을 편집한다 (G3.2).
        if (shutting_down_)
            return;

        settings_dialog_state dialog {};
        dialog.document_mode = document_.has_value();
        // 초안은 유효 값에서 시작한다. 문서 모드에서 건드리지 않은 행은 정의되지
        // 않은 채 남아 앱 설정을 따른다.
        const workspace_settings effective { effective_settings() };
        dialog.git_path = effective.git_executable;
        dialog.svn_path = effective.svn_executable;
        if (effective.query_timeout_seconds.has_value())
        {
            const std::string digits { std::to_string(*effective.query_timeout_seconds) };
            dialog.timeout.text.append(digits.begin(), digits.end());
        }
        dialog.update_submodules = effective.update_submodules;
        dialog.ignore_local_changes = effective.ignore_local_changes;
        dialog.write_log_files = effective.write_log_files;
        // 외양도 같은 초안 규칙이다 (D4). 유효 값에서 시작한다.
        const appearance_settings appearance { effective_appearance() };
        dialog.theme = appearance.theme;
        dialog.accent_id = appearance.accent_id;
        const font_settings fonts { effective_fonts() };
        dialog.ui_font_family = fonts.ui_family;
        dialog.code_font_family = fonts.code_family;
        if (dialog.document_mode)
        {
            dialog.git_defined = document_->settings.git_executable.has_value();
            dialog.svn_defined = document_->settings.svn_executable.has_value();
            dialog.timeout_defined = document_->settings.query_timeout_seconds.has_value();
            dialog.submodules_defined = document_->settings.update_submodules.has_value();
            dialog.ignore_local_defined = document_->settings.ignore_local_changes.has_value();
            dialog.log_files_defined = document_->settings.write_log_files.has_value();
            dialog.theme_defined = document_->appearance.theme.has_value();
            dialog.accent_defined = document_->appearance.accent_id.has_value();
            dialog.ui_font_defined = document_->fonts.ui_family.has_value();
            dialog.code_font_defined = document_->fonts.code_family.has_value();
        }
        settings_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_set_settings_executable(set_settings_executable_intent intent)
    {
        // 닫힌 뒤 도착한 파일 선택 결과는 버린다.
        if (settings_dialog_.has_value() == false)
            return;

        if (intent.tool == repository_kind::git)
        {
            settings_dialog_->git_path = std::move(intent.path);
            settings_dialog_->git_defined = true;
            settings_dialog_->git_chosen = true;
        }
        else if (intent.tool == repository_kind::subversion)
        {
            settings_dialog_->svn_path = std::move(intent.path);
            settings_dialog_->svn_defined = true;
            settings_dialog_->svn_chosen = true;
        }
    }

    void logic_controller::handle_clear_settings_executable(const clear_settings_executable_intent& intent)
    {
        if (settings_dialog_.has_value() == false)
            return;

        // 지우기는 두 모드 모두 빈 값(자동 탐색)이다. 문서 모드에서는 값을 건드린
        // 것이므로 그 행이 정의된다 — 문서 정의를 삭제하려면 `덮어씀` 배지를 쓴다
        // (2026-08-22 지시).
        if (intent.tool == repository_kind::git)
        {
            settings_dialog_->git_path.clear();
            settings_dialog_->git_defined = true;
            settings_dialog_->git_chosen = true;
        }
        else if (intent.tool == repository_kind::subversion)
        {
            settings_dialog_->svn_path.clear();
            settings_dialog_->svn_defined = true;
            settings_dialog_->svn_chosen = true;
        }
    }

    void logic_controller::handle_select_settings_tab(const select_settings_tab_intent& intent)
    {
        // 탭은 초안이 아니라 보기 상태다 (S1.2). 다른 탭에서 고친 초안은 남는다.
        if (settings_dialog_.has_value() == false)
            return;
        settings_dialog_->active_tab = intent.tab;
        // 탭을 옮기면 항목 열의 맨 위부터 본다. 앞 탭의 스크롤을 물려받으면 짧은
        // 탭에서 빈 화면이 보인다 (E1.4). 스크롤 **한계**도 이 시점부터 새 탭의
        // 내용으로 다시 계산된다 — `settings_dialog_metrics`가 활성 탭을 본다
        // (2026-08-23 지시).
        settings_dialog_->scroll_offset = 0.0f;
    }

    // 환경설정 dialog의 표시 모델이다. snapshot 게시와 스크롤 한계 계산이 같은
    // 값을 봐야 하므로 한곳에서 만든다 (E1.3). `scroll_offset`은 호출자가 채운다 —
    // 한계를 구하는 쪽이 아직 그 값을 모른다.
    settings_dialog_view logic_controller::build_settings_dialog_view() const
    {
        settings_dialog_view dialog {};
        dialog.active_tab = settings_dialog_->active_tab;
        dialog.document_mode = settings_dialog_->document_mode;
        dialog.git_path = settings_dialog_->git_path;
        dialog.svn_path = settings_dialog_->svn_path;
        dialog.timeout = { settings_dialog_->timeout.text, settings_dialog_->timeout.caret, settings_dialog_->timeout.anchor };
        apply_composition(text_input_target::settings_timeout, dialog.timeout);
        // 글꼴도 테마·키 컬러와 같은 초안이다 (2026-08-23 지시).
        dialog.ui_font_family = settings_dialog_->ui_font_family;
        dialog.code_font_family = settings_dialog_->code_font_family;
        dialog.update_submodules = settings_dialog_->update_submodules;
        dialog.ignore_local_changes = settings_dialog_->ignore_local_changes;
        dialog.write_log_files = settings_dialog_->write_log_files;
        // 외양도 초안이다 (D4). 화면 색은 `저장` 전까지 바뀌지 않는다.
        dialog.theme = settings_dialog_->theme;
        dialog.accent_id = settings_dialog_->accent_id;
        // 문서 모드에서 정의되지 않은 행은 "앱 설정 따름"으로 표시된다 (G3.2).
        if (settings_dialog_->document_mode)
        {
            dialog.git_follows_app = settings_dialog_->git_defined == false;
            dialog.svn_follows_app = settings_dialog_->svn_defined == false;
            dialog.timeout_follows_app = settings_dialog_->timeout_defined == false;
            dialog.submodules_follows_app = settings_dialog_->submodules_defined == false;
            dialog.ignore_local_follows_app = settings_dialog_->ignore_local_defined == false;
            dialog.log_files_follows_app = settings_dialog_->log_files_defined == false;
            dialog.theme_follows_app = settings_dialog_->theme_defined == false;
            dialog.accent_follows_app = settings_dialog_->accent_defined == false;
            dialog.ui_font_follows_app = settings_dialog_->ui_font_defined == false;
            dialog.code_font_follows_app = settings_dialog_->code_font_defined == false;
            // 초안은 유효 값에서 시작하므로 신뢰하지 않아 쓰지 않는 남의 경로도 그 행에 보인다(3.10, S1). 쓰는 값처럼
            // 보이지 않게 표시한다. 사용자가 이 dialog에서 고르거나 지운 값은 저장하면 신뢰하므로 표시하지 않는다.
            if (document_vcs_executables_trusted() == false)
            {
                dialog.git_unused = settings_dialog_->git_defined && settings_dialog_->git_chosen == false && settings_dialog_->git_path.empty() == false;
                dialog.svn_unused = settings_dialog_->svn_defined && settings_dialog_->svn_chosen == false && settings_dialog_->svn_path.empty() == false;
            }
        }
        // 검증 메시지와 확인 가능 여부는 logic이 한곳에서 정한다. 첫 오류만
        // 표시해도 확인이 막혀 있어 사용자는 고칠 것을 하나씩 안내받는다.
        const std::u8string_view git_error { settings_executable_error(settings_dialog_->git_path) };
        const std::u8string_view svn_error { settings_executable_error(settings_dialog_->svn_path) };
        const std::u8string_view timeout_error { settings_timeout_error(settings_dialog_->timeout.text) };
        if (git_error.empty() == false)
            dialog.message = std::u8string { u8"Git: " } + std::u8string { git_error };
        else if (svn_error.empty() == false)
            dialog.message = std::u8string { u8"SVN: " } + std::u8string { svn_error };
        else if (timeout_error.empty() == false)
            dialog.message = std::u8string { timeout_error };
        dialog.can_confirm = git_error.empty() && svn_error.empty() && timeout_error.empty();
        return dialog;
    }
    void logic_controller::handle_settings_scroll(const float delta)
    {
        if (settings_dialog_.has_value() == false)
            return;

        // element와 같은 함수로 한계를 구한다. 다르면 휠이 헛돌거나 마지막 항목이
        // 잘린다 (E1.3).
        const float maximum { ui::settings_dialog_metrics(build_settings_dialog_view(), window_height_ / scale_).maximum_scroll };
        float offset { settings_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        settings_dialog_->scroll_offset = offset;
    }

    void logic_controller::handle_clear_settings_override(const clear_settings_override_intent& intent)
    {
        // 문서 모드에서만 의미가 있다. 그 행의 문서 정의를 지우고 초안에는 앱의
        // 값이 다시 보인다 (G3.2 `덮어씀` 배지). 외양 항목은 문서를 곧바로 고치므로
        // 문서가 사라진 뒤 도착한 늦은 배지 클릭도 막는다.
        if (settings_dialog_.has_value() == false || settings_dialog_->document_mode == false || document_.has_value() == false)
            return;

        switch (intent.field)
        {
        case settings_override_field::git_executable:
            settings_dialog_->git_path = app_settings_.settings.git_executable;
            settings_dialog_->git_defined = false;
            break;
        case settings_override_field::svn_executable:
            settings_dialog_->svn_path = app_settings_.settings.svn_executable;
            settings_dialog_->svn_defined = false;
            break;
        case settings_override_field::query_timeout:
            settings_dialog_->timeout.text.clear();
            if (app_settings_.settings.query_timeout_seconds.has_value())
            {
                const std::string digits { std::to_string(*app_settings_.settings.query_timeout_seconds) };
                settings_dialog_->timeout.text.append(digits.begin(), digits.end());
            }
            settings_dialog_->timeout_defined = false;
            break;
        case settings_override_field::update_submodules:
            settings_dialog_->update_submodules = app_settings_.settings.update_submodules;
            settings_dialog_->submodules_defined = false;
            break;
        case settings_override_field::ignore_local_changes:
            settings_dialog_->ignore_local_changes = app_settings_.settings.ignore_local_changes;
            settings_dialog_->ignore_local_defined = false;
            break;
        case settings_override_field::write_log_files:
            settings_dialog_->write_log_files = app_settings_.settings.write_log_files;
            settings_dialog_->log_files_defined = false;
            break;
        case settings_override_field::theme:
            settings_dialog_->theme = app_settings_.appearance.theme;
            settings_dialog_->theme_defined = false;
            break;
        case settings_override_field::accent:
            settings_dialog_->accent_id = app_settings_.appearance.accent_id;
            settings_dialog_->accent_defined = false;
            break;
        case settings_override_field::ui_font:
            settings_dialog_->ui_font_family = app_settings_.fonts.ui_family;
            settings_dialog_->ui_font_defined = false;
            break;
        case settings_override_field::code_font:
            settings_dialog_->code_font_family = app_settings_.fonts.code_family;
            settings_dialog_->code_font_defined = false;
            break;
        }
    }

    void logic_controller::handle_confirm_settings()
    {
        if (settings_dialog_.has_value() == false || shutting_down_)
            return;
        // 문서 모드 dialog가 열린 채 문서가 사라진 경우(닫기 등)의 늦은 확인은
        // 버린다.
        if (settings_dialog_->document_mode && document_.has_value() == false)
            return;
        // 버튼 비활성과 별개로 늦게 도착한 확인도 막는다 (view의 can_confirm과 같은
        // 판정이다).
        if (settings_executable_error(settings_dialog_->git_path).empty() == false || settings_executable_error(settings_dialog_->svn_path).empty() == false
            || settings_timeout_error(settings_dialog_->timeout.text).empty() == false)
            return;

        const std::optional<std::int32_t> timeout { parse_settings_timeout(settings_dialog_->timeout.text) };
        const workspace_settings previous_effective { effective_settings() };
        const workspace_settings previous_request { request_settings() };
        const bool executables_trusted_before { document_vcs_executables_trusted() };

        if (settings_dialog_->document_mode)
        {
            // 건드린(정의된) 행만 문서 override로 남는다 (G3.2 암묵 덮어쓰기).
            // show_relative_paths는 도구 막대 토글의 몫이라 그대로 둔다. 제한 시간은
            // 빈 칸이면 "따로 정하지 않음"이라 정의를 거둔다.
            workspace_settings_overrides overrides { document_->settings };
            overrides.git_executable = settings_dialog_->git_defined ? std::optional<std::u8string> { settings_dialog_->git_path } : std::nullopt;
            overrides.svn_executable = settings_dialog_->svn_defined ? std::optional<std::u8string> { settings_dialog_->svn_path } : std::nullopt;
            overrides.query_timeout_seconds = settings_dialog_->timeout_defined && timeout.has_value() ? timeout : std::nullopt;
            overrides.update_submodules = settings_dialog_->submodules_defined ? std::optional<bool> { settings_dialog_->update_submodules } : std::nullopt;
            overrides.ignore_local_changes = settings_dialog_->ignore_local_defined ? std::optional<bool> { settings_dialog_->ignore_local_changes } : std::nullopt;
            overrides.write_log_files = settings_dialog_->log_files_defined ? std::optional<bool> { settings_dialog_->write_log_files } : std::nullopt;

            appearance_overrides appearance { document_->appearance };
            appearance.theme = settings_dialog_->theme_defined ? std::optional<theme_preference> { settings_dialog_->theme } : std::nullopt;
            appearance.accent_id = settings_dialog_->accent_defined ? std::optional<std::u8string> { settings_dialog_->accent_id } : std::nullopt;

            // 글꼴도 같은 규칙이다 (2026-08-23 지시). 빈 이름도 정의다 — "이 문서는
            // 내장 글꼴을 쓴다"는 뜻이라 앱 설정을 따르는 것과 구별된다.
            font_overrides fonts { document_->fonts };
            fonts.ui_family = settings_dialog_->ui_font_defined ? std::optional<std::u8string> { settings_dialog_->ui_font_family } : std::nullopt;
            fonts.code_family = settings_dialog_->code_font_defined ? std::optional<std::u8string> { settings_dialog_->code_font_family } : std::nullopt;

            // 문서 실행 파일의 신뢰다 (3.10, S1). 이 dialog에서 고른 경로는 사용자의 값이라 신뢰한다. 고르지 않은 경로가
            // 신뢰하지 않은 남의 값이면 함께 신뢰하지 않는다 — 다른 항목을 저장한 것이 그 경로를 받아들인 것은 아니다.
            const auto accepted = [&](const std::optional<std::u8string>& value, const bool chosen)
            {
                return value.has_value() == false || value->empty() || chosen || executables_trusted_before;
            };
            const bool trust_executables {
                accepted(overrides.git_executable, settings_dialog_->git_chosen)
                && accepted(overrides.svn_executable, settings_dialog_->svn_chosen)
            };

            const bool settings_changed { (overrides == document_->settings) == false };
            const bool appearance_changed { (appearance == document_->appearance) == false };
            const bool fonts_changed { (fonts == document_->fonts) == false };
            if (settings_changed)
                document_->settings = std::move(overrides);
            if (appearance_changed)
                document_->appearance = std::move(appearance);
            if (fonts_changed)
                document_->fonts = std::move(fonts);
            if (settings_changed || appearance_changed || fonts_changed)
                request_save();
            // 저장 결과를 기다리지 않는다. 사용자가 고른 값이고, 저장에 실패해도 모델에 남아 쓰이는 값이다.
            if (const std::u8string digest { document_vcs_executables_digest() }; trust_executables && digest.empty() == false
                && trust_vcs_executables(app_settings_, document_path_, digest))
                request_app_settings_save();
        }
        else
        {
            // 전역 모드는 모든 행이 구체 값이다. show_relative_paths는 dialog에
            // 행이 없어 유지된다.
            workspace_settings global { app_settings_.settings };
            global.git_executable = settings_dialog_->git_path;
            global.svn_executable = settings_dialog_->svn_path;
            global.query_timeout_seconds = timeout;
            global.update_submodules = settings_dialog_->update_submodules;
            global.ignore_local_changes = settings_dialog_->ignore_local_changes;
            global.write_log_files = settings_dialog_->write_log_files;
            // 전역 모드는 외양도 구체 값이다 (D4).
            appearance_settings appearance { app_settings_.appearance };
            appearance.theme = settings_dialog_->theme;
            if (settings_dialog_->accent_id.empty() == false)
                appearance.accent_id = settings_dialog_->accent_id;

            // 전역 모드는 글꼴도 구체 값이다.
            font_settings fonts { app_settings_.fonts };
            fonts.ui_family = settings_dialog_->ui_font_family;
            fonts.code_family = settings_dialog_->code_font_family;

            const bool settings_changed { (global == app_settings_.settings) == false };
            const bool appearance_changed { (appearance == app_settings_.appearance) == false };
            const bool fonts_changed { (fonts == app_settings_.fonts) == false };
            if (settings_changed)
                app_settings_.settings = std::move(global);
            if (appearance_changed)
                app_settings_.appearance = std::move(appearance);
            if (fonts_changed)
                app_settings_.fonts = std::move(fonts);
            if (settings_changed || appearance_changed || fonts_changed)
                request_app_settings_save();
        }

        // 유효 설정이 실제로 바뀐 경우에만 적재 대상을 갱신하고 활성 카드를
        // 재조회한다 (G3.2 — 문서가 전부 덮어쓰고 있으면 전역 변경은 재조회하지
        // 않는다). 요청마다 유효 설정 사본이 실리므로 저장 완료를 기다릴 필요가
        // 없다.
        // 요청 설정은 문서 실행 파일 신뢰로도 바뀐다 (S1). 유효 설정이 같아도 다시 조회한다.
        if ((effective_settings() == previous_effective) == false)
        {
            publish_log_targets();
            for (card_state& card : cards_)
                if (card.project.enabled)
                    request_refresh(card);
        }
        else
            refresh_cards_after_request_settings_change(previous_request);
        settings_dialog_.reset();
    }

    void logic_controller::handle_begin_switch(const begin_switch_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->project.enabled == false)
            return;

        // Git은 remote-first 후보를, SVN은 browser root/current URL을 조회한다. 조회는
        // 카드를 busy로 만들지 않아 dialog가 열린 동안에도 UI가 멈추지 않는다.
        operation_request request { make_request(operation_kind::query_switch_candidates, card, card->generation) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        switch_dialog_state dialog {};
        dialog.card = intent.id;
        dialog.candidates_operation_id = operation_id;
        dialog.subversion = card->snapshot.kind == repository_kind::subversion;
        switch_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_switch_candidates(switch_candidates_event event)
    {
        // 닫힌 dialog나 다른 조회의 늦은 결과는 버린다.
        if (switch_dialog_.has_value() == false || event.operation_id != switch_dialog_->candidates_operation_id)
            return;

        switch_dialog_->loading = false;
        switch_dialog_->candidates = std::move(event.result);
        switch_dialog_->selected.reset();
        switch_dialog_->svn_browser.reset();
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->scroll_offset = 0.0f;

        if (switch_dialog_->subversion == false)
            return;

        if (switch_dialog_->candidates.svn_browser.has_value() == false)
        {
            if (switch_dialog_->candidates.diagnostics.empty() == false)
                switch_dialog_->message = switch_dialog_->candidates.diagnostics.front().message;
            else
                switch_dialog_->message = u8"SVN 저장소 정보를 조회하지 못했습니다.";
            return;
        }

        switch_dialog_->svn_browser = { make_svn_repository_browser(*switch_dialog_->candidates.svn_browser) };
        svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
        // 주소 막대는 현재 작업 복사본의 경로로 시작한다.
        text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(browser.root_url, browser.current_url), true);

        // 창을 열 때는 현재 위치까지 펼쳐 둔다 (2026-08-22 검수 정정: 자기 branch
        // 까지는 재귀 조회가 필요하다). 그 아래와 다른 가지는 여전히 사용자가 열
        // 때만 조회한다 — 저장소 전체를 미리 읽지 않는다.
        const std::u8string root_url { browser.root_url };
        const svn_browser_reveal_progress progress { begin_svn_browser_reveal(browser, browser.current_url) };
        if (progress.query_url.has_value())
            submit_svn_directory_query(*progress.query_url);
        else if (begin_svn_browser_query(browser, root_url))
            submit_svn_directory_query(root_url);
    }

    void logic_controller::handle_select_switch_candidate(const std::size_t index)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading)
            return;
        // index는 거른 목록 기준이다 (view가 그린 것과 같은 순서).
        if (index >= filtered_switch_candidates().size())
            return;

        switch_dialog_->selected = { index };
        // 다른 후보를 고르면 이전 검증·거부 메시지와 확인 단계는 의미가 없다.
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->message.clear();
    }

    void logic_controller::handle_select_svn_browser_node(const std::u8string_view url)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading || switch_dialog_->svn_browser.has_value() == false)
            return;
        if (select_svn_browser_node(*switch_dialog_->svn_browser, url) == false)
            return;
        switch_dialog_->message.clear();
        // 주소 막대는 고른 노드의 실제 경로를 따라간다 (repo-browser 원형 동작).
        text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(switch_dialog_->svn_browser->root_url, url), true);
    }

    void logic_controller::handle_toggle_svn_browser_node(const std::u8string_view url)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading || switch_dialog_->svn_browser.has_value() == false)
            return;
        const std::optional<std::u8string> query_url { toggle_svn_browser_node(*switch_dialog_->svn_browser, url) };
        switch_dialog_->message.clear();
        handle_switch_dialog_scroll(0.0f);
        if (query_url.has_value())
            submit_svn_directory_query(*query_url);
    }

    void logic_controller::handle_confirm_switch()
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->loading || switch_dialog_->executing)
            return;

        card_state* const card { find_card(switch_dialog_->card) };
        if (card == nullptr)
        {
            switch_dialog_.reset();
            return;
        }

        switch_candidate target {};
        if (switch_dialog_->svn_browser.has_value())
        {
            const svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
            if (browser.selected_url.empty())
                return;
            target.kind = switch_candidate_kind::subversion_url;
            target.display_name = browser.selected_url;
            target.target = browser.selected_url;
            if (target.target == browser.current_url)
            {
                switch_dialog_->message = u8"이미 현재 위치입니다. 다른 디렉터리를 선택하세요.";
                return;
            }
        }
        else
        {
            const std::vector<std::size_t> visible { filtered_switch_candidates() };
            if (switch_dialog_->selected.has_value() == false || *switch_dialog_->selected >= visible.size())
                return;
            target = switch_dialog_->candidates.candidates[visible[*switch_dialog_->selected]];
        }

        if (switch_dialog_->svn_browser.has_value() == false && candidate_is_current(card->snapshot, target, switch_dialog_->candidates.candidates))
        {
            switch_dialog_->message = u8"이미 현재 참조입니다. 다른 후보를 선택하세요.";
            return;
        }

        // tracking branch 생성은 명시적인 두 단계 확인을 거친다 (plan 3.3).
        if (target.requires_tracking_branch && switch_dialog_->tracking_confirm_pending == false)
        {
            switch_dialog_->tracking_confirm_pending = true;
            switch_dialog_->message = std::u8string { u8"local tracking branch를 만들고 전환합니다. 확인하려면 한 번 더 누르세요." };
            return;
        }
        if (target.requires_tracking_branch)
            target.tracking_branch_confirmed = true;

        if (card->busy)
        {
            switch_dialog_->message = u8"카드가 다른 작업을 실행 중입니다. 끝난 뒤 다시 시도하세요.";
            return;
        }
        // dialog가 열린 뒤 시작한 출력 수집 기능과도 서로 막는다 (function-buttons-design 4.7).
        // begin_change도 거부하지만 사유는 dialog에 보여야 한다.
        if (function_running(*card))
        {
            switch_dialog_->message = u8"이 카드에서 기능이 실행 중입니다. 끝난 뒤 다시 시도하세요.";
            return;
        }

        begin_change(*card, operation_kind::switch_to, {}, &target);
        if (card->change_operation_id != 0)
        {
            switch_dialog_->executing = true;
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->message.clear();
        }
    }

    void logic_controller::handle_svn_directory(svn_directory_event event)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false || event.id != switch_dialog_->card)
            return;

        std::size_t query_index { switch_dialog_->directory_queries.size() };
        for (std::size_t index = 0; index < switch_dialog_->directory_queries.size(); ++index)
            if (switch_dialog_->directory_queries[index].operation_id == event.operation_id)
            {
                query_index = index;
                break;
            }
        if (query_index == switch_dialog_->directory_queries.size())
            return;

        const std::u8string expected_url { switch_dialog_->directory_queries[query_index].url };
        if (event.url != expected_url)
            return;
        switch_dialog_->directory_queries.erase(switch_dialog_->directory_queries.begin() + static_cast<std::ptrdiff_t>(query_index));

        const svn_browser_reveal_progress progress { complete_svn_browser_query(*switch_dialog_->svn_browser, expected_url, event.result) };
        handle_switch_dialog_scroll(0.0f);
        apply_svn_browser_reveal(progress);
    }

    void logic_controller::apply_svn_browser_reveal(const svn_browser_reveal_progress& progress)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false)
            return;
        if (progress.reached)
        {
            text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(switch_dialog_->svn_browser->root_url, switch_dialog_->svn_browser->selected_url), true);
            switch_dialog_->message.clear();
            // 도달한 곳을 눈으로 확인할 수 있어야 한다 — 창을 열 때와 주소 막대
            // 이동은 사용자가 그 행을 보려고 한 것이다 (2026-08-23 지시).
            scroll_svn_browser_selection_into_view();
            return;
        }
        if (progress.not_found)
        {
            switch_dialog_->message = std::u8string { u8"그 경로를 찾을 수 없습니다: " } + switch_dialog_->query.text;
            return;
        }
        if (progress.query_url.has_value())
        {
            // 한 걸음 내려갈 때마다 그 깊이를 따라간다 (2026-08-23 지시). 조회 하나가
            // 초 단위로 걸려서, 목표에 닿을 때까지 아무 데도 붙지 않으면 한참 동안
            // 어디까지 갔는지 알 수 없다. 주소 막대는 건드리지 않는다 — 사용자가 친
            // 목표가 도착할 때까지 보여야 한다.
            static_cast<void>(select_svn_browser_node(*switch_dialog_->svn_browser, *progress.query_url));
            scroll_svn_browser_selection_into_view();
            submit_svn_directory_query(*progress.query_url);
        }
    }

    void logic_controller::scroll_svn_browser_selection_into_view()
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false)
            return;
        // 행 index는 화면에 그려질 목록 그대로여야 한다 — 접힌 가지는 행이 없다.
        const std::u8string& selected { switch_dialog_->svn_browser->selected_url };
        const std::vector<svn_repository_browser_row> rows { build_svn_repository_browser_rows(*switch_dialog_->svn_browser) };
        std::optional<std::size_t> index {};
        if (selected.empty() == false)
            for (std::size_t row = 0; row < rows.size(); ++row)
                if (rows[row].kind == svn_browser_row_kind::directory && rows[row].url == selected)
                {
                    index = row;
                    break;
                }

        // 도달한 행을 목록 **가운데**에 놓는다 (2026-08-23 지시). 끝에 붙이면 그
        // 위나 아래가 보이지 않아 어디에 있는 가지인지 읽히지 않는다. 목록의 처음·
        // 끝 근처는 아래의 한계 고정이 자연스럽게 위아래로 붙인다.
        if (index.has_value())
        {
            const float viewport { switch_dialog_list_height(switch_dialog_->subversion, window_height_ / scale_) };
            const float top { static_cast<float>(*index) * layout_switch_dialog_row_height() };
            switch_dialog_->scroll_offset = top - (viewport - layout_switch_dialog_row_height()) / 2.0f;
        }
        // 찾지 못했어도 한계 고정은 한다 — 목록이 줄어든 뒤일 수 있다. 계산은 휠과
        // 같은 handler를 그대로 쓴다.
        handle_switch_dialog_scroll(0.0f);
    }

    void logic_controller::handle_set_text_composition(const set_text_composition_intent& intent)
    {
        const std::u8string before { displayed_text_of(intent.target) };
        if (intent.composing == false)
        {
            // 조합이 끝났으면 상태를 버린다. 확정된 글은 별도의 insert intent로 온다.
            if (composition_.has_value() && composition_->target == intent.target)
                composition_.reset();
        }
        else
        {
            // 조합 글은 초안에 넣지 않는다 (E2.3). 화면에 보일 글만 따로 들고 있다가
            // 조합이 끝나면 버린다.
            text_composition_state state {};
            state.target = intent.target;
            state.text = intent.text;
            state.caret = text_edit_clamp_offset(state.text, intent.caret);
            state.composing_begin = text_edit_clamp_offset(state.text, intent.composing_begin);
            state.composing_end = text_edit_clamp_offset(state.text, intent.composing_end);
            if (state.composing_end < state.composing_begin)
                state.composing_end = state.composing_begin;
            composition_ = std::move(state);
        }

        // 화면 글이 달라지면 점진 검색 목록도 달라진다 (D5). 편집과 같은 규칙으로
        // 사라질 수 있는 선택과 tracking 확인 단계를 남겨 두지 않는다 — 확인한 적
        // 없는 후보로 branch를 만들면 안 된다.
        if (displayed_text_of(intent.target) != before)
            reset_search_selection(intent.target);
    }

    std::u8string logic_controller::displayed_text_of(const text_input_target target) const
    {
        if (composition_.has_value() && composition_->target == target)
            return composition_->text;
        if (target == text_input_target::switch_query && switch_dialog_.has_value())
            return switch_dialog_->query.text;
        if (target == text_input_target::font_filter && font_dialog_.has_value())
            return font_dialog_->query.text;
        return {};
    }

    void logic_controller::reset_search_selection(const text_input_target target)
    {
        // SVN 질의 막대는 주소 막대라 목록을 거르지 않는다 (트리거를 눌러야 움직인다).
        if (target == text_input_target::switch_query && switch_dialog_.has_value() && switch_dialog_->subversion == false)
        {
            switch_dialog_->selected.reset();
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->scroll_offset = 0.0f;
        }
        else if (target == text_input_target::font_filter && font_dialog_.has_value())
        {
            font_dialog_->selected.reset();
            font_dialog_->scroll_offset = 0.0f;
        }
    }

    void logic_controller::apply_composition(const text_input_target target, text_input_view& view) const
    {
        if (composition_.has_value() == false || composition_->target != target)
            return;
        view.composing = true;
        view.composition_text = composition_->text;
        view.composition_caret = composition_->caret;
        view.composing_begin = composition_->composing_begin;
        view.composing_end = composition_->composing_end;
    }

    std::u8string logic_controller::search_query_for(const text_input_target target, const std::u8string& committed, const std::function<bool(std::u8string_view)>& any_match) const
    {
        // 조합이 없거나 다른 박스의 것이면 고를 것이 확정 글뿐이다.
        if (composition_.has_value() == false || composition_->target != target)
            return committed;

        // 조합 글이 섞인 표시 글이 먼저다.
        const std::u8string& displayed { composition_->text };
        // 확정 글이 비어 있으면 물러설 자리가 없다 — 빈 질의는 좁히기가 아니라
        // "전부"라서 첫 글자를 조합하는 동안 목록 전체가 되살아난다.
        if (committed.empty() || any_match == nullptr)
            return displayed;
        if (any_match(displayed))
            return displayed;
        // 하나도 맞지 않으면 조합 중인 글자를 뺀 자리로 물러선다.
        return committed;
    }

    void logic_controller::handle_edit_text_input(const edit_text_input_intent& intent)
    {
        // 닫힌 뒤 도착한 키 입력은 버린다. 초안을 가진 dialog가 대상이다.
        text_edit_state* state { nullptr };
        if (intent.target == text_input_target::settings_timeout && settings_dialog_.has_value())
            state = &settings_dialog_->timeout;
        else if (intent.target == text_input_target::switch_query && switch_dialog_.has_value() && switch_dialog_->executing == false)
            state = &switch_dialog_->query;
        else if (intent.target == text_input_target::font_filter && font_dialog_.has_value())
            state = &font_dialog_->query;
        else if (intent.target == text_input_target::generate_document_name && generate_dialog_.has_value() && generate_dialog_->executing == false)
            state = &generate_dialog_->name;
        else if (is_function_editor_target(intent.target))
            state = function_editor_text(intent.target);
        if (state == nullptr)
            return;

        const std::u8string before { state->text };
        switch (intent.command)
        {
        case text_edit_command::insert:
            text_edit_insert(*state, intent.text);
            break;
        case text_edit_command::backspace:
            text_edit_backspace(*state);
            break;
        case text_edit_command::delete_forward:
            text_edit_delete_forward(*state);
            break;
        case text_edit_command::delete_word_left:
            text_edit_delete_word_left(*state);
            break;
        case text_edit_command::delete_word_right:
            text_edit_delete_word_right(*state);
            break;
        case text_edit_command::undo:
            static_cast<void>(text_edit_undo(*state));
            break;
        case text_edit_command::redo:
            static_cast<void>(text_edit_redo(*state));
            break;
        case text_edit_command::cut:
            static_cast<void>(text_edit_erase_selection(*state));
            break;
        case text_edit_command::clear:
            text_edit_select_all(*state);
            static_cast<void>(text_edit_erase_selection(*state));
            break;
        case text_edit_command::replace_all:
            // IME 조합이 확정한 글이다 (E2.3). 조합 중의 중간 단계는 초안에 들어오지
            // 않았으므로 조합 한 번이 실행 취소 한 덩어리가 된다.
            text_edit_replace_all(*state, intent.text, intent.offset);
            break;
        case text_edit_command::move_left:
            text_edit_move(*state, text_edit_motion::left, intent.extend);
            break;
        case text_edit_command::move_right:
            text_edit_move(*state, text_edit_motion::right, intent.extend);
            break;
        case text_edit_command::move_word_left:
            text_edit_move(*state, text_edit_motion::word_left, intent.extend);
            break;
        case text_edit_command::move_word_right:
            text_edit_move(*state, text_edit_motion::word_right, intent.extend);
            break;
        case text_edit_command::move_line_start:
            text_edit_move(*state, text_edit_motion::line_start, intent.extend);
            break;
        case text_edit_command::move_line_end:
            text_edit_move(*state, text_edit_motion::line_end, intent.extend);
            break;
        case text_edit_command::select_all:
            text_edit_select_all(*state);
            break;
        case text_edit_command::select_word:
            text_edit_select_word(*state, intent.offset);
            break;
        case text_edit_command::place_caret:
            text_edit_place_caret(*state, intent.offset, intent.extend);
            break;
        }

        if (intent.target == text_input_target::font_filter)
        {
            // 목록이 달라지므로 사라질 수 있는 선택을 남겨 두지 않는다.
            if (state->text != before)
            {
                font_dialog_->selected.reset();
                font_dialog_->scroll_offset = 0.0f;
            }
            return;
        }
        if (intent.target == text_input_target::generate_document_name)
        {
            if (state->text != before)
                generate_dialog_->message.clear();
            return;
        }
        if (is_function_editor_target(intent.target))
        {
            // 제한 시간은 숫자만 받는다. 최대값(86400)이 5자리라 그 이상은 버린다. 범위는 Apply의 규칙 함수가 본다.
            if (intent.target == text_input_target::function_editor_timeout)
            {
                std::u8string digits {};
                for (const char8_t value : state->text)
                {
                    if (value >= u8'0' && value <= u8'9' && digits.size() < 5)
                        digits.push_back(value);
                }
                if (digits != state->text)
                    text_edit_set_text(*state, digits, true);
            }
            if (state->text != before)
                function_editor_->message.clear();
            return;
        }
        if (intent.target == text_input_target::settings_timeout)
        {
            // 숫자만 받는 칸이다. 최대값(3600)이 4자리라 그 이상은 버린다. 붙여넣기가
            // 섞인 글을 넣어도 여기서 걸러진다 (field-feedback-design 1.3).
            std::u8string digits {};
            for (const char8_t value : state->text)
                if (value >= u8'0' && value <= u8'9' && digits.size() < 4)
                    digits.push_back(value);
            if (digits != state->text)
                text_edit_set_text(*state, digits, true);
            if (state->text != before)
                settings_dialog_->timeout_defined = true;
            return;
        }

        if (state->text == before)
            return;
        switch_dialog_->message.clear();
        // Git 필터는 즉시 적용되어 목록이 달라진다. 사라질 수 있는 선택과 확인
        // 단계를 남겨 두지 않는다. SVN은 트리거를 눌러야 움직인다.
        if (switch_dialog_->subversion == false)
        {
            switch_dialog_->selected.reset();
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->scroll_offset = 0.0f;
        }
    }

    void logic_controller::handle_submit_switch_query()
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading)
            return;

        if (switch_dialog_->subversion == false)
        {
            // Git은 이미 걸러져 있다. Enter는 첫 후보를 잡는 몫이다.
            const std::vector<std::size_t> visible { filtered_switch_candidates() };
            if (visible.empty())
                return;
            switch_dialog_->selected = { std::size_t { 0 } };
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->message.clear();
            return;
        }

        if (switch_dialog_->svn_browser.has_value() == false)
            return;
        const std::u8string target { resolve_svn_browser_path(*switch_dialog_->svn_browser, switch_dialog_->query.text) };
        if (target.empty())
        {
            switch_dialog_->message = std::u8string { u8"그 경로를 찾을 수 없습니다: " } + switch_dialog_->query.text;
            return;
        }
        switch_dialog_->message.clear();
        apply_svn_browser_reveal(begin_svn_browser_reveal(*switch_dialog_->svn_browser, target));
    }

    void logic_controller::handle_clear_switch_query()
    {
        // 지우기 버튼은 Git 필터에만 있다. SVN 주소는 비우면 뜻이 없다.
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->subversion)
            return;
        text_edit_set_text(switch_dialog_->query, {}, true);
        switch_dialog_->selected.reset();
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->scroll_offset = 0.0f;
        switch_dialog_->message.clear();
    }

    void logic_controller::handle_refresh_switch_dialog()
    {
        if (shutting_down_ || switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading)
            return;
        card_state* const card { find_card(switch_dialog_->card) };
        if (card == nullptr)
            return;

        if (switch_dialog_->svn_browser.has_value())
        {
            // 선택 노드(없으면 root)의 cache를 버리고 다시 조회한다. 진행 중이던
            // 조회의 결과는 bookkeeping을 비워 늦은 결과로 떨어뜨린다.
            svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
            std::u8string target { browser.selected_url.empty() ? browser.root_url : browser.selected_url };
            if (find_svn_browser_node(browser, target) == nullptr)
                target = browser.root_url;
            if (reset_svn_browser_node(browser, target) == false)
                return;
            switch_dialog_->directory_queries.clear();
            switch_dialog_->message.clear();
            text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(browser.root_url, browser.selected_url), true);
            if (begin_svn_browser_query(browser, target))
                submit_svn_directory_query(target);
            handle_switch_dialog_scroll(0.0f);
            return;
        }

        // Git은 후보 목록 전체를 다시 조회한다. 필터 글은 그대로 둔다.
        operation_request request { make_request(operation_kind::query_switch_candidates, card, card->generation) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
        {
            switch_dialog_->message = u8"후보를 다시 조회하지 못했습니다.";
            return;
        }
        switch_dialog_->candidates_operation_id = operation_id;
        switch_dialog_->loading = true;
        switch_dialog_->selected.reset();
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->message.clear();
        switch_dialog_->scroll_offset = 0.0f;
    }

    void logic_controller::submit_svn_directory_query(std::u8string url)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false)
            return;
        card_state* const card { find_card(switch_dialog_->card) };
        if (card == nullptr)
            return;

        operation_request request { make_request(operation_kind::query_svn_directory, card, card->generation) };
        request.svn_repository_root_url = switch_dialog_->svn_browser->root_url;
        request.svn_directory_url = url;
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)))
        {
            switch_dialog_->directory_queries.push_back({ operation_id, std::move(url) });
            return;
        }

        svn_directory_query_result failure {};
        failure.error = svn_browser_query_error::failed;
        static_cast<void>(complete_svn_browser_query(*switch_dialog_->svn_browser, url, failure));
    }

    std::size_t logic_controller::switch_dialog_row_count() const
    {
        if (switch_dialog_.has_value() == false)
            return 0;
        if (switch_dialog_->svn_browser.has_value())
            return build_svn_repository_browser_rows(*switch_dialog_->svn_browser).size();
        return filtered_switch_candidates().size();
    }

    std::vector<std::size_t> logic_controller::filtered_switch_candidates() const
    {
        std::vector<std::size_t> visible {};
        if (switch_dialog_.has_value() == false)
            return visible;
        const std::vector<switch_candidate>& candidates { switch_dialog_->candidates.candidates };
        visible.reserve(candidates.size());

        // SVN의 질의 막대는 필터가 아니라 주소 막대라 목록을 거르지 않는다.
        if (switch_dialog_->subversion)
        {
            for (std::size_t index = 0; index < candidates.size(); ++index)
                visible.push_back(index);
            return visible;
        }

        // 조합 중이면 조합 글이 섞인 표시 글로 먼저 거르고, 그 글로 하나도 맞지
        // 않으면 확정 글로 물러선다 (search_query_for).
        const auto any_match = [&candidates](const std::u8string_view probe) {
            for (const switch_candidate& candidate : candidates)
                if (contains_ignoring_ascii_case(candidate.display_name, probe))
                    return true;
            return false;
        };
        const std::u8string query { search_query_for(text_input_target::switch_query, switch_dialog_->query.text, any_match) };

        for (std::size_t index = 0; index < candidates.size(); ++index)
            if (contains_ignoring_ascii_case(candidates[index].display_name, query))
                visible.push_back(index);
        return visible;
    }

    void logic_controller::handle_switch_dialog_scroll(const float delta)
    {
        if (switch_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(switch_dialog_row_count()) * layout_switch_dialog_row_height() };
        const float viewport { switch_dialog_list_height(switch_dialog_->subversion, window_height_ / scale_) };
        float maximum { content - viewport };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { switch_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        switch_dialog_->scroll_offset = offset;
    }

    void logic_controller::handle_open_context_menu(const open_context_menu_intent& intent)
    {
        if (shutting_down_)
            return;
        if (find_card(intent.id) == nullptr)
        {
            context_menu_.reset();
            return;
        }

        // 우클릭한 카드를 선택 카드로 만든 뒤 연다 (로그 pane 연동 유지, 3장).
        handle_select_card(select_card_intent { { intent.id } });
        context_menu_ = { context_menu_state { context_menu_kind::card, intent.id, intent.anchor_x, intent.anchor_y, std::chrono::steady_clock::now() } };
    }

    void logic_controller::handle_open_function_menu(const open_function_menu_intent& intent)
    {
        if (shutting_down_)
            return;
        // 확인 dialog가 떠 있는 동안에는 열지 않는다. `Alt+↓`은 키라 modal을 지나 올 수 있다.
        if (find_card(intent.id) == nullptr || function_confirm_.has_value())
        {
            context_menu_.reset();
            return;
        }
        // 카드 안 버튼이라 선택과 로그 pane은 그대로다 (2.2.6). 카드 메뉴와 달리 카드를 선택하지 않는다.
        context_menu_ = { context_menu_state { context_menu_kind::functions, intent.id, intent.anchor_x, intent.anchor_y, std::chrono::steady_clock::now() } };
    }

    void logic_controller::handle_run_function_shortcut(const run_function_shortcut_intent& intent)
    {
        if (shutting_down_ || intent.number == 0 || document_.has_value() == false || document_->function_buttons.has_value() == false)
            return;
        // 필터로 목록에서 빠진 카드는 보이지 않으므로 그 카드에서 기능을 시작하지 않는다. 초점 카드(`focused_card_`)는
        // 필터가 바뀌어도 policy에 남아 있을 수 있다.
        const card_state* const card { find_card(intent.id) };
        if (card == nullptr || matches_filter(*card) == false)
            return;

        // N은 문서 전체의 보이는 `card` 순서다 — 창 폭과 그 카드의 적용 범위와 상관없다 (2.3.4). 카드 view와 같은
        // 목록에서 찾으므로 그 기능이 이 카드의 범위 밖이면(목록에 없으면) 아무것도 하지 않는다. 실행 중이면 실행
        // intent의 늦은 클릭 규칙대로 아무것도 하지 않는다 — `Alt+N`은 중지하지 않는다.
        for (const card_function_view& function : card_function_views(*card))
        {
            if (function.shortcut == intent.number)
            {
                handle_run_function(run_function_intent { intent.id, function.button_id });
                return;
            }
        }
    }

    void logic_controller::handle_open_function_placement_menu(const open_function_placement_menu_intent& intent)
    {
        if (shutting_down_)
            return;
        // 확인 dialog가 떠 있으면 열지 않는다 (기능 메뉴와 같다). 버튼이 이미 비활성이 됐거나 다른 자리로 옮겨 갔으면
        // 늦게 온 우클릭이다 — 항목 builder가 같은 판정으로 빈 목록을 돌려준다.
        const card_state* const card { find_card(intent.id) };
        if (card == nullptr || function_confirm_.has_value() || placement_context_menu_items(*card, intent.button_id, intent.from_bar).empty())
        {
            context_menu_.reset();
            return;
        }
        // 카드 안 버튼이라 선택과 로그 pane은 그대로다 (기능 메뉴와 같다, 2.2.6).
        context_menu_ = { context_menu_state { context_menu_kind::placement, intent.id, intent.anchor_x, intent.anchor_y, std::chrono::steady_clock::now(), intent.button_id, intent.from_bar } };
    }

    void logic_controller::handle_set_function_placement(const set_function_placement_intent& intent, const std::optional<project_id>& focus_card)
    {
        if (shutting_down_ || document_.has_value() == false || document_->function_buttons.has_value() == false)
            return;
        std::vector<function_button_entry>& buttons { document_->function_buttons->buttons };
        std::size_t index { buttons.size() };
        for (std::size_t candidate = 0; candidate < buttons.size(); ++candidate)
        {
            const function_button* const button { std::get_if<function_button>(&buttons[candidate]) };
            if (button != nullptr && button->id == intent.button_id)
            {
                index = candidate;
                break;
            }
        }
        if (index == buttons.size())
            return;

        // 모델 값과 같으면 쓸 것이 없다. 생략한 배치는 `menu`다 (D22-B). 4번째 `card`와 알 수 없는 값은 보이는 배치가
        // `bar`여도 모델 값이 다르므로 `기능 줄에 두기`를 고르면 `bar`로 쓴다 — 사용자가 고른 그때 새 값으로 쓴다 (3.2).
        function_button& button { std::get<function_button>(buttons[index]) };
        const std::u8string_view name { function_placement_name(intent.placement) };
        const bool unchanged { button.placement.has_value() ? *button.placement == name : intent.placement == function_placement::menu };
        if (unchanged)
            return;

        // `card`는 문서 전체에 보이는 `card`가 3개 미만일 때만이다. 그 기능이 이미 보이는 `card`면 통과한다. 메뉴에서
        // 이미 비활성이라 여기 오는 것은 메뉴가 떠 있는 사이 다른 길로 바뀐 경우뿐이다.
        const std::vector<std::optional<function_placement>> before { resolve_visible_placements(buttons) };
        if (intent.placement == function_placement::card && before[index] != function_placement::card)
        {
            const auto visible_cards { std::ranges::count(before, std::optional<function_placement> { function_placement::card }) };
            if (static_cast<std::size_t>(visible_cards) >= maximum_card_function_buttons)
                return;
        }

        // 줄 버튼에서 연 메뉴로 그 버튼이 줄을 떠나면 초점을 옮긴다 (2.2.5). 후보는 바꾸기 전 줄에서 그 버튼 뒤에 있던
        // 버튼들이다. 줄 버튼은 Tab 자리라 우클릭의 누름도, `Shift+F10`도 그 버튼에 초점이 있다.
        std::optional<function_focus_request> focus {};
        if (focus_card.has_value() && before[index] == function_placement::bar && intent.placement != function_placement::bar)
        {
            for (const card_state& card : cards_)
            {
                if ((card.project.id == *focus_card) == false)
                    continue;
                function_focus_request request {};
                request.card = card.project.id;
                bool after { false };
                for (const card_function_view& function : card_function_views(card, before))
                {
                    if (function.placement != function_placement::bar)
                        continue;
                    if (after)
                        request.next_buttons.push_back(function.button_id);
                    after = after || function.button_id == intent.button_id;
                }
                focus = std::move(request);
                break;
            }
        }

        // 모델 값을 바꾸면 카드 열, 기능 줄, `Alt+N` 번호, 기능 줄 유무가 다음 view에서 바로 바뀐다. 실행 중인 그 기능은
        // 계속 돌고 실행 중 표시가 새 자리로 옮겨 간다 (4.11). 신뢰 해시 입력에는 보이는 `card`와 그 순서만 들어간다 (3.10,
        // D31). `card`에 넣거나 빼면 해시가 바뀌므로 편집 dialog의 Apply처럼 편집 전에 신뢰했을 때만 저장이 끝난 뒤 저장한
        // 글의 해시 입력으로 신뢰를 갱신한다(D24-A와 같은 규칙). 실행은 막지 않는다 — 저장 전 해시 입력은 바꾸기 전 원문이고,
        // 그 사이에 신뢰를 물어 기록하면 저장 뒤 한 번 더 물을 뿐 덜 묻지는 않는다. `bar`·`menu` 사이 이동은 해시가 같아
        // 갱신해도 바뀌는 것이 없다.
        if (before[index] == function_placement::card || intent.placement == function_placement::card)
            remember_function_trust_for_save();
        button.placement = std::u8string { name };
        if (focus.has_value())
        {
            focus->id = ++function_focus_revision_;
            function_focus_ = std::move(focus);
        }
        request_save();
    }

    bool logic_controller::has_function_bar() const
    {
        if (document_.has_value() == false || document_->function_buttons.has_value() == false)
            return false;
        const std::vector<std::optional<function_placement>> placements { resolve_visible_placements(document_->function_buttons->buttons) };
        // `bar` 배치가 없는 문서(배치를 적지 않은 문서 포함, D22-B)는 카드를 보지 않고 끝낸다.
        if (std::ranges::find(placements, std::optional<function_placement> { function_placement::bar }) == placements.end())
            return false;
        // 카드 view와 같은 판정이다. `vcs` 한정 + `vcs_hint: auto`이고 조회 전인 카드는 비활성 버튼으로 적용되므로
        // 센다. 어느 카드에도 적용되지 않는 `bar` 기능만으로는 줄을 두지 않는다 (D23-A).
        for (const card_state& card : cards_)
        {
            for (const card_function_view& function : card_function_views(card, placements))
            {
                if (function.placement == function_placement::bar)
                    return true;
            }
        }
        return false;
    }

    function_bar_view logic_controller::build_function_bar_view() const
    {
        function_bar_view bar {};
        const card_state* card { nullptr };
        if (selected_.has_value())
        {
            for (const card_state& candidate : cards_)
            {
                if (candidate.project.id == *selected_)
                    card = &candidate;
            }
        }
        // 줄은 늘 선택 카드를 보인다 (2.2.6). 선택이 없으면 힌트만 있고 줄의 `⋯`는 비활성이다.
        if (card == nullptr)
        {
            bar.hint = u8"카드를 선택하면 기능이 나옵니다";
            return bar;
        }

        bar.card = card->project.id;
        bar.card_name = card->project.display_name.empty() ? card->project.id.value : card->project.display_name;
        const std::vector<card_function_view> functions { card_function_views(*card) };
        bar.total_functions = functions.size();
        for (const card_function_view& function : functions)
        {
            if (function.placement == function_placement::bar)
                bar.functions.push_back(function);
        }
        if (bar.functions.empty())
            bar.hint = functions.empty() ? u8"이 카드에 적용되는 기능이 없습니다" : u8"이 카드에는 기능 줄 항목이 없습니다 — ⋯에서 모든 기능을 봅니다";
        return bar;
    }

    void logic_controller::handle_open_document_context_menu(const open_document_context_menu_intent& intent)
    {
        if (shutting_down_)
            return;
        // 열린 문서가 없으면 열 메뉴가 없다 (T1). 배너도 같은 조건에서 intent를
        // 내지 않지만 늦게 도착한 요청을 여기서 한 번 더 막는다.
        if (document_.has_value() == false || document_path_.empty())
        {
            context_menu_.reset();
            return;
        }

        // 문서 메뉴는 선택 카드를 바꾸지 않는다 — 카드와 무관한 대상이다.
        context_menu_ = { context_menu_state { context_menu_kind::document, {}, intent.anchor_x, intent.anchor_y, std::chrono::steady_clock::now() } };
    }

    void logic_controller::handle_select_context_menu_item(const select_context_menu_item_intent& intent)
    {
        // 메뉴가 이미 닫혔으면 늦게 도착한 클릭이다. 버린다.
        if (context_menu_.has_value() == false)
            return;

        // 항목을 고르면 메뉴는 어느 경우에나 닫힌다 (예전 element가 닫기 액션을 함께
        // 내던 것과 같다). 대상과 지금의 항목 목록을 먼저 잡아 둔다.
        const context_menu_state menu { *context_menu_ };
        const std::vector<context_menu_item_view> items { context_menu_items(menu) };
        context_menu_.reset();

        // 비활성 항목은 view에서 이미 걸렀지만, 메뉴가 떠 있는 동안 카드 상태가
        // 바뀌었을 수 있다. view를 만들 때와 같은 목록에서 고른 항목을 다시 찾아 지금
        // 활성인지 본다 (늦은 클릭 방어). 이 메뉴에 없는 항목(다른 메뉴의 늦은 클릭)도
        // 닫기만 한다.
        for (const context_menu_item_view& item : items)
        {
            if (item.entry != intent.entry || item.argument != intent.argument)
                continue;
            if (item.enabled)
                run_context_menu_item(menu, item);
            return;
        }
    }

    void logic_controller::run_context_menu_item(const context_menu_state& menu, const context_menu_item_view& item)
    {
        card_state* const state { find_card(menu.card) };
        switch (item.entry)
        {
        case context_menu_entry::show_local_changes:
            if (state != nullptr)
                handle_open_local_changes(open_local_changes_intent { menu.card });
            break;
        case context_menu_entry::refresh:
            // `refresh_card_intent`의 처리와 같다.
            if (state != nullptr && state->project.enabled)
                request_refresh(*state);
            break;
        case context_menu_entry::update:
            // submodule 여부는 logic이 문서 settings로 채운다 (카드 버튼과 동일).
            if (state != nullptr)
                handle_request_update(request_update_intent { menu.card, {} });
            break;
        case context_menu_entry::switch_to:
            if (state != nullptr)
                handle_begin_switch(begin_switch_intent { menu.card });
            break;
        case context_menu_entry::run_function:
            // 버튼 클릭과 같은 길이다. 신뢰 확인·`confirm` 미리보기도 그대로 거친다 (T7).
            if (state != nullptr)
                handle_run_function(run_function_intent { menu.card, item.argument });
            break;
        case context_menu_entry::stop_function:
            if (state != nullptr)
                handle_stop_function(stop_function_intent { menu.card, item.argument });
            break;
        case context_menu_entry::more_functions:
            // 닫힌 메뉴 자리에 같은 카드의 기능 메뉴 다음 쪽을 연다 (2.3.2, 2.3.3, D34). 카드 메뉴는 앞 8개를 이미 실었으므로
            // 9번째부터, 기능 메뉴는 지금 쪽의 다음이다. 하위 메뉴는 쓰지 않는다.
            if (state != nullptr)
            {
                context_menu_state next { context_menu_kind::functions, menu.card, menu.anchor_x, menu.anchor_y, std::chrono::steady_clock::now() };
                next.function_first = menu.kind == context_menu_kind::functions ? menu.function_first + function_menu_page_size : card_menu_function_count;
                context_menu_ = { std::move(next) };
            }
            break;
        case context_menu_entry::reload_document:
            // 안전한 다시 읽기다 (4.9). 실행 중인 출력 수집 기능이 있으면 확인 dialog가 먼저 뜬다.
            handle_reload_document();
            break;
        case context_menu_entry::place_function_on_card:
        case context_menu_entry::place_function_on_bar:
        case context_menu_entry::place_function_in_menu: {
            // 배치 메뉴의 radio다 (2.2.5, 4.11). 줄 버튼에서 연 메뉴면 그 버튼이 줄을 떠날 때 초점을 옮긴다.
            function_placement placement { function_placement::menu };
            if (item.entry == context_menu_entry::place_function_on_card)
                placement = function_placement::card;
            else if (item.entry == context_menu_entry::place_function_on_bar)
                placement = function_placement::bar;
            const std::optional<project_id> focus_card { menu.from_bar ? std::optional<project_id> { menu.card } : std::nullopt };
            handle_set_function_placement(set_function_placement_intent { item.argument, placement }, focus_card);
            break;
        }
        case context_menu_entry::edit_functions:
            // 편집 dialog다 (2.7). 배치 메뉴에서 고르면 argument가 그 기능이다.
            handle_open_function_editor(open_function_editor_intent { item.argument });
            break;
        case context_menu_entry::trust_document_executables:
            open_vcs_executables_confirm();
            break;
        case context_menu_entry::open_repository:
        case context_menu_entry::open_in_vscode:
        case context_menu_entry::open_document_folder:
        case context_menu_entry::open_document_in_vscode:
            // 외부 열기는 UI thread의 앱 명령이라 logic으로 오지 않는다. 닫기만 한다.
            break;
        }
    }

    std::vector<context_menu_item_view> logic_controller::context_menu_items(const context_menu_state& menu) const
    {
        if (menu.kind == context_menu_kind::document)
            return document_context_menu_items();
        for (const card_state& card : cards_)
        {
            if ((card.project.id == menu.card) == false)
                continue;
            if (menu.kind == context_menu_kind::functions)
                return function_context_menu_items(card, menu.opened_at, menu.function_first);
            if (menu.kind == context_menu_kind::placement)
                return placement_context_menu_items(card, menu.button, menu.from_bar);
            return card_context_menu_items(card, menu.opened_at);
        }
        return {};
    }

    std::vector<context_menu_item_view> logic_controller::card_context_menu_items(const card_state& card, const std::chrono::steady_clock::time_point opened_at) const
    {
        const std::u8string repository_path { card.project.path.normalized.empty() ? card.project.path.original : card.project.path.normalized };

        // 항목 활성 여부는 카드 버튼과 같은 판정이다 (3장: 버튼이 비활성이면 메뉴 항목도
        // 비활성). 실행 중에는 update 버튼이 취소 버튼으로 바뀌므로 메뉴의 업데이트는
        // 비활성이다.
        const bool can_change { change_allowed(card) };
        const bool change_running { card.change_operation_id != 0 };
        std::vector<context_menu_item_view> items {};
        items.push_back({ .entry = context_menu_entry::open_repository, .label = u8"저장소 열기", .target_path = repository_path, .icon = u8"folder-opened" });
        // 미추적·변경 정리는 편집기에서 하는 편이 낫다 (2026-08-22 지시).
        items.push_back({ .entry = context_menu_entry::open_in_vscode, .label = u8"VSCode로 열기", .target_path = repository_path, .icon = u8"vscode" });
        items.push_back({ .entry = context_menu_entry::show_local_changes, .label = u8"로컬 변경 확인", .icon = u8"diff" });
        items.push_back({ .entry = context_menu_entry::refresh, .label = u8"상태 갱신", .icon = u8"refresh" });
        items.push_back({ .entry = context_menu_entry::update, .label = u8"업데이트", .enabled = can_change && change_running == false, .icon = u8"repo-pull" });
        items.push_back({ .entry = context_menu_entry::switch_to, .label = u8"전환…", .enabled = can_change, .icon = u8"source-control" });

        // 기능 절이다 (function-buttons-design 2.3.3). 적용 기능이 없으면 지금 메뉴 그대로다. 앞
        // `card_menu_function_count`(8)개를 문서 순서로 싣고 넘치면 `기능 더 보기…`가 9번째부터 그 카드의 기능 메뉴로
        // 연다 (D34). 하위 메뉴는 쓰지 않는다. 기능 메뉴 쪽 크기(12)보다 작은 것은 이 메뉴가 가장 긴 메뉴라 작은 작업
        // 영역에서도 통째로 들게 하려는 것이다 (`list_metrics.h`).
        const std::vector<card_function_view> functions { card_function_views(card) };
        if (functions.empty())
            return items;
        for (std::size_t index = 0; index < functions.size() && index < card_menu_function_count; ++index)
        {
            context_menu_item_view item { function_menu_item(functions[index], opened_at) };
            item.separator_above = index == 0;
            items.push_back(std::move(item));
        }
        if (functions.size() > card_menu_function_count)
            items.push_back({ .entry = context_menu_entry::more_functions, .label = u8"기능 더 보기…", .icon = u8"ellipsis" });
        if (std::optional<context_menu_item_view> edit { function_edit_menu_item() }; edit.has_value())
            items.push_back(std::move(*edit));
        return items;
    }

    std::vector<context_menu_item_view> logic_controller::function_context_menu_items(const card_state& card, const std::chrono::steady_clock::time_point opened_at, const std::size_t first_function) const
    {
        // 그 카드의 적용 기능이다 — 배치(`card`·`bar`·`menu`)와 상관없이 문서 순서다 (2.3.2). 카드 열이나 기능 줄에
        // 이미 보이는 것도 다시 나온다. 메뉴가 카드 폭에 의존하면 logic이 UI 배치를 알아야 한다.
        //
        // luil 메뉴는 스크롤이 없어 작업 영역보다 긴 메뉴는 아래가 잘린다. 한 쪽에 `function_menu_page_size`개만 싣고
        // 남으면 `기능 더 보기…`가 다음 쪽을 연다 (D34). 쪽마다 아래 구역이 같아 어느 쪽도 같은 높이 한계 안이다. 쪽은
        // 시작 기능으로 센다 — 카드 메뉴의 `기능 더 보기…`는 기능 메뉴 쪽 경계가 아닌 9번째에서 연다. 메뉴가 떠 있는 사이
        // 기능이 줄어 그 쪽이 비면 첫 쪽을 보인다 — 빈 쪽은 고를 것이 없다.
        const std::vector<card_function_view> functions { card_function_views(card) };
        const std::size_t first { first_function < functions.size() ? first_function : 0 };
        const std::size_t end { first + function_menu_page_size < functions.size() ? first + function_menu_page_size : functions.size() };
        std::vector<context_menu_item_view> items {};
        for (std::size_t index = first; index < end; ++index)
            items.push_back(function_menu_item(functions[index], opened_at));
        if (end < functions.size())
            items.push_back({ .entry = context_menu_entry::more_functions, .label = u8"기능 더 보기…", .icon = u8"ellipsis" });

        // 아래 구역은 기능 목록과 구분선으로 가른다. 기능이 없으면 구분선도 없다.
        const std::size_t tail { items.size() };
        if (std::optional<context_menu_item_view> edit { function_edit_menu_item() }; edit.has_value())
            items.push_back(std::move(*edit));
        // 안전한 다시 읽기다 (4.9). 다시 읽기가 진행 중이면 비활성이다.
        items.push_back(reload_menu_item());
        items[tail].separator_above = tail > 0;
        return items;
    }

    context_menu_item_view logic_controller::function_menu_item(const card_function_view& function, const std::chrono::steady_clock::time_point opened_at)
    {
        // 상태는 라벨 안에 넣는다 (2.3.2). 메뉴 항목의 접근 이름은 라벨뿐이라 단축키 열에 두면 읽히지 않는다.
        // 경과 시간은 메뉴를 연 시각에서 멈춘다 — 떠 있는 동안 snapshot이 다시 지어져도 라벨(접근 이름)과 메뉴 폭이
        // 바뀌지 않는다. 메뉴가 떠 있는 사이 시작된 기능은 00:00이다. 흐르는 시간은 카드 3번째 줄 칩이 보인다.
        const std::chrono::steady_clock::duration elapsed {
            function.started_at.has_value() && opened_at > *function.started_at ? opened_at - *function.started_at : std::chrono::steady_clock::duration::zero()
        };
        context_menu_item_view item {};
        // 실행 중인 항목은 고르면 중지다. 아이콘 자리에 중지 글리프를 둔다.
        item.entry = function.running ? context_menu_entry::stop_function : context_menu_entry::run_function;
        item.label = function_menu_label(function, elapsed);
        item.enabled = function.enabled;
        item.argument = function.button_id;
        item.icon = function.running ? std::u8string { u8"stop-circle" } : function.icon;
        // `card` 항목은 단축키 열에 `Alt+N`을 붙여 고정 표시와 단축키 안내를 겸한다.
        item.shortcut_text = function_shortcut_text(function.shortcut);
        return item;
    }

    std::optional<context_menu_item_view> logic_controller::function_edit_menu_item(const std::u8string_view button_id) const
    {
        // `기능 편집…`은 편집 dialog를 연다 (2.7, T12). 배치 메뉴의 항목만 그 기능의 id를 실어 그 기능을 고른 상태로 연다.
        if (document_.has_value() == false || document_path_.empty())
            return std::nullopt;
        context_menu_item_view item { .entry = context_menu_entry::edit_functions, .label = u8"기능 편집…", .argument = std::u8string { button_id }, .icon = u8"gear" };
        // 다시 읽기 결과를 기다리는 동안은 열지 않는다. 다시 읽기가 성공하면 문서를 바꾸며 초안을 버리고, 그 사이 낸 Apply의
        // 저장도 버려진다 (4.9, T11).
        if (pending_reload_operation_id_ != 0 || reload_after_save_)
        {
            item.label = u8"기능 편집… — 읽는 중";
            item.enabled = false;
        }
        return item;
    }

    std::vector<context_menu_item_view> logic_controller::placement_context_menu_items(const card_state& card, const std::u8string_view button_id, const bool from_bar) const
    {
        // 메뉴는 활성 버튼에만 있다 — 비활성 버튼은 우클릭도 `Shift+F10`도 받지 않는다 (2.2.5). 카드 열의 `card` 버튼과
        // 기능 줄의 `bar` 버튼이 연 자리 그대로인지 카드 view와 같은 판정으로 다시 본다.
        if (document_.has_value() == false || document_->function_buttons.has_value() == false)
            return {};
        const std::vector<std::optional<function_placement>> placements { resolve_visible_placements(document_->function_buttons->buttons) };
        const function_placement expected { from_bar ? function_placement::bar : function_placement::card };
        std::optional<card_function_view> function {};
        for (card_function_view& candidate : card_function_views(card, placements))
        {
            if (candidate.button_id == button_id)
                function = std::move(candidate);
        }
        if (function.has_value() == false || function->enabled == false || function->placement != expected)
            return {};

        // `카드에 두기`는 문서 전체에 보이는 `card`가 이미 3개이고 이 기능이 그중 하나가 아니면 비활성이고 사유를 붙인다.
        // 단축키 열은 이 기능이 카드에 두면 갖는 `Alt+N`이다 — 보이는 `card`는 문서 순서의 앞 3개라, 3개 미만이면 이 기능
        // 앞의 보이는 `card` 수 + 1이다.
        const std::span<const function_button_entry> buttons { document_->function_buttons->buttons };
        std::size_t visible_cards { 0 };
        std::size_t cards_before { 0 };
        bool reached { false };
        for (std::size_t index = 0; index < buttons.size(); ++index)
        {
            const function_button* const button { std::get_if<function_button>(&buttons[index]) };
            if (button != nullptr && button->id == button_id)
                reached = true;
            if (placements[index] != function_placement::card)
                continue;
            ++visible_cards;
            if (reached == false)
                ++cards_before;
        }
        const bool on_card { function->placement == function_placement::card };
        const bool card_allowed { on_card || visible_cards < maximum_card_function_buttons };
        const std::size_t number { on_card ? function->shortcut : cards_before + 1 };

        context_menu_item_view card_item {
            .entry = context_menu_entry::place_function_on_card,
            .label = card_allowed ? u8"카드에 두기" : u8"카드에 두기 — 3개까지",
            .enabled = card_allowed,
            .argument = std::u8string { button_id },
            .shortcut_text = card_allowed ? function_shortcut_text(number) : std::u8string {},
            .kind = context_menu_item_kind::radio,
            .checked = function->placement == function_placement::card,
        };
        context_menu_item_view bar_item {
            .entry = context_menu_entry::place_function_on_bar,
            .label = u8"기능 줄에 두기",
            .argument = std::u8string { button_id },
            .kind = context_menu_item_kind::radio,
            .checked = function->placement == function_placement::bar,
        };
        // 버튼이 없는 `menu` 기능에는 이 메뉴가 없으므로 이 항목은 늘 꺼져 있다. 고르면 버튼이 사라진다.
        context_menu_item_view menu_item {
            .entry = context_menu_entry::place_function_in_menu,
            .label = u8"메뉴에만 두기",
            .argument = std::u8string { button_id },
            .kind = context_menu_item_kind::radio,
            .checked = function->placement == function_placement::menu,
        };
        std::vector<context_menu_item_view> items {};
        items.push_back(std::move(card_item));
        items.push_back(std::move(bar_item));
        items.push_back(std::move(menu_item));
        // `기능 편집…`은 그 기능을 고른 상태로 편집 dialog를 연다 (2.2.5).
        if (std::optional<context_menu_item_view> edit { function_edit_menu_item(button_id) }; edit.has_value())
        {
            edit->separator_above = true;
            items.push_back(std::move(*edit));
        }
        return items;
    }

    std::vector<context_menu_item_view> logic_controller::document_context_menu_items() const
    {
        // 배너 우클릭 메뉴다 (T1). 탐색기는 문서 파일을 선택 상태로 열고, VSCode는 문서가
        // 있는 폴더를 workspace로 연다. 두 경로 모두 Windows 원형이라 셸이 그대로 받는다.
        const std::u8string folder { windows_parent_directory(document_path_) };
        if (document_path_.empty() || folder.empty())
            return {};

        std::vector<context_menu_item_view> items {};
        items.push_back({ .entry = context_menu_entry::open_document_folder, .label = u8"경로를 탐색기로 열기", .target_path = document_path_, .icon = u8"folder-opened" });
        items.push_back({ .entry = context_menu_entry::open_document_in_vscode, .label = u8"VSCode로 열기", .target_path = folder, .icon = u8"vscode" });
        // 손으로 고친 문서를 불러오는 길이다 (function-buttons-design 4.9, 2.3.3). 기능 메뉴의 항목과 같은 동작이다.
        items.push_back(reload_menu_item());
        // 편집 dialog다 (2.3.3, T12). 적용 기능이 0개인 카드에는 기능 절이 없으므로 정의가 0개인 문서의 진입은 여기뿐이다.
        if (std::optional<context_menu_item_view> edit { function_edit_menu_item() }; edit.has_value())
            items.push_back(std::move(*edit));
        // 문서가 지정한 실행 파일을 신뢰하지 않아 쓰지 않을 때만이다 (3.10, S1). 기능 버튼이 없는 문서도 이 길로 신뢰한다.
        if (vcs_executables_notice().has_value())
            items.push_back({ .entry = context_menu_entry::trust_document_executables, .label = u8"문서 실행 파일 신뢰…", .icon = u8"workspace-trusted", .separator_above = true });
        return items;
    }

    void logic_controller::handle_set_theme_preference(const set_theme_preference_intent& intent)
    {
        // 외양도 다른 항목과 같은 초안이다 (D4). 화면 색은 `저장` 전까지 그대로이고,
        // 닫힌 뒤 도착한 클릭은 버린다.
        if (settings_dialog_.has_value() == false)
            return;

        settings_dialog_->theme = intent.theme;
        settings_dialog_->theme_defined = true;
    }

    void logic_controller::handle_set_accent(const set_accent_intent& intent)
    {
        if (settings_dialog_.has_value() == false || intent.accent_id.empty())
            return;

        settings_dialog_->accent_id = intent.accent_id;
        settings_dialog_->accent_defined = true;
    }

    void logic_controller::handle_open_local_changes(const open_local_changes_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr)
            return;

        operation_request request { make_request(operation_kind::query_local_changes, card, card->generation) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        local_changes_dialog_state dialog {};
        dialog.card = intent.id;
        dialog.title = card->project.display_name;
        dialog.list_operation_id = operation_id;
        local_changes_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_local_changes(local_changes_event event)
    {
        // 닫힌 dialog나 다른 조회의 늦은 결과는 버린다.
        if (local_changes_dialog_.has_value() == false || event.operation_id != local_changes_dialog_->list_operation_id)
            return;

        local_changes_dialog_->loading = false;
        local_changes_dialog_->entries = std::move(event.result.entries);
        local_changes_dialog_->list_scroll = 0.0f;
        for (const diagnostic& value : event.result.diagnostics)
            if (value.severity != diagnostic_severity::information)
            {
                local_changes_dialog_->message = value.message;
                break;
            }
        if (local_changes_dialog_->entries.empty())
        {
            if (local_changes_dialog_->message.empty())
                local_changes_dialog_->message = u8"표시할 로컬 변경이 없습니다.";
            return;
        }

        // 첫 항목을 바로 선택해 diff까지 이어서 보여 준다.
        local_changes_dialog_->selected = { 0 };
        request_selected_file_diff();
    }

    void logic_controller::handle_select_local_change(const std::size_t index)
    {
        if (local_changes_dialog_.has_value() == false || local_changes_dialog_->loading)
            return;
        if (index >= local_changes_dialog_->entries.size() || local_changes_dialog_->selected == index)
            return;

        local_changes_dialog_->selected = { index };
        request_selected_file_diff();
    }

    void logic_controller::request_selected_file_diff()
    {
        if (shutting_down_ || local_changes_dialog_.has_value() == false || local_changes_dialog_->selected.has_value() == false)
            return;
        card_state* const card { find_card(local_changes_dialog_->card) };
        if (card == nullptr)
            return;

        operation_request request { make_request(operation_kind::query_file_diff, card, card->generation) };
        request.diff_target = { local_changes_dialog_->entries[*local_changes_dialog_->selected] };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        local_changes_dialog_->diff_operation_id = operation_id;
        local_changes_dialog_->diff_loading = true;
        local_changes_dialog_->diff_rows.clear();
        local_changes_dialog_->diff_notice.clear();
        local_changes_dialog_->diff_scroll = 0.0f;
    }

    void logic_controller::handle_file_diff(file_diff_event event)
    {
        if (local_changes_dialog_.has_value() == false || event.operation_id != local_changes_dialog_->diff_operation_id)
            return;

        local_changes_dialog_->diff_loading = false;
        local_changes_dialog_->diff_rows = build_two_way_diff(event.result.lines);
        local_changes_dialog_->diff_scroll = 0.0f;

        // 안내는 하나만 보여 준다. 오류 > 이진/디렉터리 > 생략 > 변화 없음 순서다.
        std::u8string notice {};
        for (const diagnostic& value : event.result.diagnostics)
            if (value.severity != diagnostic_severity::information)
            {
                notice = value.message;
                break;
            }
        if (notice.empty() && event.result.binary)
            notice = u8"이진 파일 - 내용을 표시하지 않습니다.";
        if (notice.empty() && event.result.directory)
            notice = u8"미추적 디렉터리 - 내부 파일은 표시하지 않습니다.";
        if (notice.empty() && event.result.truncated)
            notice = u8"표시 상한(256 KiB)을 넘어 이후 내용을 생략했습니다.";
        if (notice.empty() && local_changes_dialog_->diff_rows.empty())
            notice = u8"표시할 차이가 없습니다.";
        local_changes_dialog_->diff_notice = std::move(notice);
    }

    void logic_controller::handle_local_changes_scroll(const float delta)
    {
        if (local_changes_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(local_changes_dialog_->entries.size()) * layout_local_changes_row_height() };
        float maximum { content - layout_local_changes_list_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { local_changes_dialog_->list_scroll + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        local_changes_dialog_->list_scroll = offset;
    }

    void logic_controller::handle_local_changes_diff_scroll(const float delta)
    {
        if (local_changes_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(local_changes_dialog_->diff_rows.size()) * layout_local_changes_diff_line_height() };
        float maximum { content - layout_local_changes_diff_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { local_changes_dialog_->diff_scroll + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        local_changes_dialog_->diff_scroll = offset;
    }

    void logic_controller::append_lifecycle_log(card_state& card, const diagnostic_severity severity, std::u8string text)
    {
        operation_log_entry entry {};
        entry.kind = log_entry_kind::lifecycle;
        entry.severity = severity;
        entry.text = std::move(text);
        entry.time = std::chrono::system_clock::now();
        append_card_log(card, std::move(entry));
    }

    void logic_controller::append_card_log(card_state& card, operation_log_entry entry)
    {
        if (log_sink_ != nullptr)
            log_sink_->append(card.project.id, entry);
        card.log.append(std::move(entry));
        if (log_sink_ == nullptr)
            return;

        // 파일 로그가 꺼진 사유는 화면 로그에만 남긴다. 파일로 다시 보내면 실패가
        // 반복되므로 buffer에 직접 넣는다.
        std::optional<std::u8string> failure { log_sink_->take_failure() };
        if (failure.has_value() == false)
            return;

        operation_log_entry notice {};
        notice.kind = log_entry_kind::lifecycle;
        notice.severity = diagnostic_severity::warning;
        notice.text = std::move(*failure);
        notice.time = std::chrono::system_clock::now();
        card.log.append(std::move(notice));
    }

    void logic_controller::publish_log_targets()
    {
        if (log_sink_ == nullptr)
            return;

        std::vector<log_file_target> targets {};
        targets.reserve(cards_.size());
        for (const card_state& card : cards_)
        {
            log_file_target target {};
            target.id = card.project.id;
            target.display_name = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
            target.repository_path = card.project.path.normalized.empty() ? card.project.path.original : card.project.path.normalized;
            targets.push_back(std::move(target));
        }
        // 유효 설정이 꺼져 있으면 폴더를 만들지 않도록 빈 문서를 알린다 (A4.5).
        const bool enabled { document_.has_value() && effective_settings().write_log_files };
        log_sink_->set_document(enabled ? document_path_ : std::u8string {}, targets);
    }

    void logic_controller::cancel_running_changes() noexcept
    {
        for (card_state& card : cards_)
        {
            if (card.change_cancellation.has_value())
                card.change_cancellation->request_cancellation();
            // 출력 수집 기능도 문서를 열고 닫을 때와 종료할 때 함께 취소한다 (4.10). join하지
            // 않으며 늦게 온 event는 run id로 버린다. 분리 실행은 취소가 없어 남는다.
            for (function_run_state& run : card.function_runs)
            {
                if (run.cancellation.has_value())
                    run.cancellation->request_cancellation();
            }
        }
    }

    void logic_controller::request_refresh(card_state& card)
    {
        if (shutting_down_)
            return;
        if (card.busy)
        {
            card.refresh_queued = true;
            return;
        }

        ++card.generation;
        card.busy = true;
        static_cast<void>(submitter_->submit(make_request(operation_kind::refresh, &card, card.generation)));
    }

    void logic_controller::begin_shutdown()
    {
        if (shutting_down_)
            return;
        shutting_down_ = true;
        cancellation_source_.request_cancellation();
        // 변경 작업은 작업별 token을 쓰므로 전역 취소와 별도로 전파한다. 탐색도
        // 작업별 token이라 함께 취소한다.
        cancel_running_changes();
        if (discovery_dialog_.has_value() && discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();

        // 종료 저장은 취소 전파 뒤에도 한 번 나간다. runtime의 종료 순서가 이 요청이
        // worker inbox에 들어간 뒤에 inbox를 닫으므로 join 안에서 끝까지 실행된다.
        if (window_placement_dirty_ && document_.has_value())
        {
            window_placement_dirty_ = false;
            operation_request request { make_request(operation_kind::save_document, nullptr, 0) };
            request.document = document_;
            request.revision = revision_;
            // 저장은 취소 token을 보지 않지만, 종료 저장이 취소 대상이 아니라는
            // 의도를 요청에 남긴다.
            request.token = {};
            pending_save_operation_id_ = request.operation_id;
            static_cast<void>(submitter_->submit(std::move(request)));
        }

        // 앱 설정의 마지막 창 배치도 같은 구간에서 한 번 저장한다 (G1). worker
        // lane이 FIFO라 진행 중이던 일반 저장 뒤에 실행되어 마지막 상태가 남는다.
        // 읽기가 끝나기 전이면 파일의 다른 항목을 지울 수 있어 포기한다 (시작
        // 직후 종료의 드문 경로).
        if (app_settings_window_dirty_ && app_settings_loaded_)
        {
            app_settings_window_dirty_ = false;
            operation_request request { make_request(operation_kind::save_app_settings, nullptr, 0) };
            request.app_settings_payload = app_settings_;
            request.app_settings_shadow = app_settings_shadow_;
            request.token = {};
            pending_app_settings_save_id_ = request.operation_id;
            static_cast<void>(submitter_->submit(std::move(request)));
        }
    }

    bool logic_controller::has_notice() const noexcept
    {
        if (notices_.empty() == false || function_button_notice_.has_value() || save_notice_.empty() == false || reload_notice_.has_value())
            return true;
        // 문서 실행 파일 알림은 해시를 만들어 판정한다. 메모리가 모자라 실패하면 띠 자리를 두지 않는다 — 다음 snapshot이 다시 본다.
        try
        {
            return vcs_executables_notice().has_value();
        }
        catch (...)
        {
            return false;
        }
    }

    float logic_controller::list_viewport_height() const noexcept
    {
        // logic은 논리 픽셀로 계산한다. 창 크기만 물리 픽셀이라 배율로 되돌린다.
        return compute_list_layout(window_height_ / scale_, 1.0f, has_notice(), has_log_pane()).viewport_height;
    }

    void logic_controller::clamp_scroll()
    {
        const float content { card_list_content_height(visible_card_count(), 1.0f) };
        scroll_offset_ = clamp_scroll_offset(scroll_offset_, content, list_viewport_height());
    }

    void logic_controller::scroll_selected_into_view()
    {
        if (selected_.has_value() == false)
        {
            clamp_scroll();
            return;
        }

        const std::vector<card_view_model> ordered { build_ordered_cards() };
        std::size_t index { ordered.size() };
        for (std::size_t position = 0; position < ordered.size(); ++position)
            if (ordered[position].id == *selected_)
                index = position;

        scroll_offset_ = scroll_offset_showing_card(scroll_offset_, index, ordered.size(), list_viewport_height(), 1.0f);
    }

    logic_controller::card_state* logic_controller::find_card(const project_id& id) noexcept
    {
        for (card_state& card : cards_)
            if (card.project.id == id)
                return &card;
        return nullptr;
    }

    const operation_log_buffer* logic_controller::card_log(const project_id& id) const noexcept
    {
        for (const card_state& card : cards_)
            if (card.project.id == id)
                return &card.log;
        return nullptr;
    }

    operation_request logic_controller::make_request(const operation_kind kind, const card_state* const card, const std::uint64_t generation)
    {
        operation_request request {};
        request.operation_id = next_operation_id_++;
        request.generation = generation;
        request.kind = kind;
        request.document_path = document_path_;
        if (card != nullptr)
            request.project = card->project;
        // worker는 유효 설정(전역 + 문서 override)의 사본을 받는다. 층 구분은
        // logic 안에서 끝난다. 신뢰하지 않은 문서의 실행 파일은 빠진다 (S1).
        request.settings = request_settings();
        request.token = cancellation_source_.token();
        return request;
    }

    bool logic_controller::matches_filter(const card_state& card) const noexcept
    {
        return contains_ignoring_ascii_case(card.project.display_name, filter_) || contains_ignoring_ascii_case(card.project.path.original, filter_);
    }

    bool logic_controller::relative_paths() const noexcept
    {
        return document_.has_value() && document_->settings.show_relative_paths.value_or(app_settings_.settings.show_relative_paths);
    }

    workspace_settings logic_controller::effective_settings() const
    {
        if (document_.has_value())
            return apply_overrides(app_settings_.settings, document_->settings);
        return app_settings_.settings;
    }

    workspace_settings logic_controller::request_settings() const
    {
        if (document_.has_value() == false)
            return app_settings_.settings;
        // 문서의 실행 파일은 그 문서를 열고 처음 조회할 때 확인 없이 실행됐다(3.10). 남이 준 문서가 저장소 안의 프로그램을
        // 가리키면 열기만 해도 돈다. 신뢰하지 않았으면 그 값을 정의하지 않은 것처럼 본다 — 빈 값으로 덮지 않는 것은 앱
        // 설정에 사용자가 직접 고른 경로가 있으면 그것을 써야 해서다. 빈 값(자동 탐색)은 실행 파일을 지정하지 않으므로 둔다.
        workspace_settings_overrides overrides { document_->settings };
        if (document_vcs_executables_trusted() == false)
        {
            if (overrides.git_executable.has_value() && overrides.git_executable->empty() == false)
                overrides.git_executable.reset();
            if (overrides.svn_executable.has_value() && overrides.svn_executable->empty() == false)
                overrides.svn_executable.reset();
        }
        return apply_overrides(app_settings_.settings, overrides);
    }

    std::u8string logic_controller::document_vcs_executables_digest() const
    {
        if (document_.has_value() == false)
            return {};
        const std::u8string_view git { document_->settings.git_executable.has_value() ? std::u8string_view { *document_->settings.git_executable } : std::u8string_view {} };
        const std::u8string_view svn { document_->settings.svn_executable.has_value() ? std::u8string_view { *document_->settings.svn_executable } : std::u8string_view {} };
        if (git.empty() && svn.empty())
            return {};
        return vcs_executables_trust_digest(git, svn);
    }

    bool logic_controller::document_vcs_executables_trusted() const
    {
        const std::u8string digest { document_vcs_executables_digest() };
        if (digest.empty())
            return true;
        // 앱 설정을 읽는 중이면 신뢰 목록을 아직 모른다. 읽기를 내지 못했으면(제출 실패) 메모리의 목록이 전부다.
        const bool known { app_settings_loaded_ || pending_app_settings_load_id_ == 0 };
        return known && document_path_.empty() == false && is_vcs_executables_trusted(app_settings_, document_path_, digest);
    }

    std::optional<notice_view_model> logic_controller::vcs_executables_notice() const
    {
        // 앱 설정을 읽기 전에는 신뢰 목록을 몰라 알리지 않는다. 읽은 뒤 신뢰했으면 바로 문서의 값으로 다시 조회한다.
        if ((app_settings_loaded_ == false && pending_app_settings_load_id_ != 0) || document_vcs_executables_trusted())
            return std::nullopt;
        const std::optional<std::u8string>& git { document_->settings.git_executable };
        const std::optional<std::u8string>& svn { document_->settings.svn_executable };
        const bool has_git { git.has_value() && git->empty() == false };
        const bool has_svn { svn.has_value() && svn->empty() == false };
        notice_view_model notice {};
        if (has_git && has_svn)
            notice.text = u8"문서가 지정한 Git·SVN 실행 파일을 쓰지 않았습니다";
        else
            notice.text = has_git ? u8"문서가 지정한 Git 실행 파일을 쓰지 않았습니다" : u8"문서가 지정한 SVN 실행 파일을 쓰지 않았습니다";
        if (has_git)
            notice.details.push_back(u8"Git: " + *git);
        if (has_svn)
            notice.details.push_back(u8"SVN: " + *svn);
        notice.details.push_back(u8"이 문서의 실행 파일을 신뢰하지 않아 앱 설정의 실행 파일(비어 있으면 자동 탐색)을 씁니다.");
        notice.details.push_back(u8"쓰려면 문서 경로를 우클릭해 `문서 실행 파일 신뢰…`를 고르거나 환경설정에서 경로를 다시 고르십시오.");
        return notice;
    }

    void logic_controller::refresh_cards_after_request_settings_change(const workspace_settings& before)
    {
        if (request_settings() == before)
            return;
        for (card_state& card : cards_)
            if (card.project.enabled)
                request_refresh(card);
    }

    appearance_settings logic_controller::effective_appearance() const
    {
        // 외양도 다른 설정과 같은 계층이다 (settings-tabs-and-appearance-scope-
        // design S2.2). 문서가 덮어쓴 항목만 앱 값 위에 얹힌다.
        if (document_.has_value())
            return apply_overrides(app_settings_.appearance, document_->appearance);
        return app_settings_.appearance;
    }

    font_settings logic_controller::effective_fonts() const
    {
        // 글꼴도 테마·키 컬러와 같은 계층이다 (2026-08-23 지시). 문서가 덮어쓴
        // 항목만 앱 값 위에 얹힌다.
        if (document_.has_value())
            return apply_overrides(app_settings_.fonts, document_->fonts);
        return app_settings_.fonts;
    }

    std::u8string logic_controller::display_path(const project_definition& project) const
    {
        // 표시 전용이라 구분자를 `/`로 통일한다 (T2). 원형 경로는 project가 그대로
        // 들고 있어 실행·저장 경로에는 영향이 없다.
        if (relative_paths() == false)
            return to_display_path(project.path.original);
        // 문서가 있는 폴더가 기준이다. 문서 경로를 모르면 전체 경로를 그대로 쓴다.
        const std::u8string_view base { windows_parent_directory(document_path_) };
        if (base.empty())
            return to_display_path(project.path.original);
        return to_display_path(relative_windows_path(project.path.original, base));
    }

    std::size_t logic_controller::visible_card_count() const noexcept
    {
        std::size_t count { 0 };
        for (const card_state& card : cards_)
            if (matches_filter(card))
                ++count;
        return count;
    }

    std::vector<card_view_model> logic_controller::build_ordered_cards() const
    {
        std::vector<card_view_model> ordered {};
        // 보이는 배치는 문서 전체의 `card` 수에 달려 snapshot마다 한 번 만든다 (2.2.2 규칙 4).
        std::vector<std::optional<function_placement>> placements {};
        if (document_.has_value() && document_->function_buttons.has_value())
            placements = resolve_visible_placements(document_->function_buttons->buttons);
        for (const card_state& card : cards_)
        {
            if (matches_filter(card) == false)
                continue;

            card_view_model model {};
            model.id = card.project.id;
            model.display_name = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
            model.path = display_path(card.project);
            model.kind = card.snapshot.kind;
            model.reference = card.snapshot.current_reference;
            model.revision = card.snapshot.local_revision;
            // status 순회가 원격 조회와 병렬로 도는 동안에는 요약 대신 진행 표시를
            // 그린다 (대형 작업 복사본에서 분 단위로 걸린다).
            model.working_tree_text = card.snapshot.working_tree_scan_pending ? std::u8string { u8"로컬 변경 확인 중" } : working_tree_summary_text(card.snapshot.working_tree);
            model.comparison_target = card.snapshot.comparison_target;
            model.local_checked_at = card.snapshot.local_checked_at;
            model.remote_checked_at = card.snapshot.remote_checked_at;
            model.busy = card.busy;
            model.selected = selected_.has_value() && *selected_ == card.project.id;
            model.enabled = card.project.enabled;
            model.can_change = change_allowed(card);
            model.change_running = card.change_operation_id != 0;
            if (placements.empty() == false)
                model.functions = card_function_views(card, placements);
            for (const function_run_state& run : card.function_runs)
            {
                if (run.mode != function_run_mode::capture)
                    continue;
                model.function_running = true;
                model.running_function_label = run.label;
                if (run.phase == function_run_phase::running)
                    model.running_function_started_at = run.started_at;
            }

            card_view_inputs inputs {};
            inputs.enabled = card.project.enabled;
            inputs.busy = card.busy;
            inputs.has_local_result = card.has_local_result;
            inputs.availability = card.snapshot.availability;
            inputs.sync_state = card.snapshot.sync_state;
            inputs.tree_state = card.snapshot.working_tree.state;
            model.state = derive_card_state(inputs);

            const bool availability_problem { card.snapshot.availability != repository_availability::ready && card.snapshot.availability != repository_availability::unknown };
            model.status = availability_problem ? availability_glyph(card.snapshot.availability) : sync_state_glyph(card.snapshot.sync_state, card.snapshot.ahead_count, card.snapshot.behind_count);
            ordered.push_back(std::move(model));
        }

        // 카드는 항상 문서의 프로젝트 순서 그대로다. drag & drop이 문서를 바꾼다.
        return ordered;
    }

    std::shared_ptr<const view_snapshot> logic_controller::make_view_snapshot() const
    {
        auto snapshot { std::make_shared<view_snapshot>() };
        snapshot->document_path = document_path_;
        snapshot->document_display_path = to_display_path(document_path_);
        snapshot->selected = selected_;
        snapshot->filter_text = filter_;
        snapshot->notices = notices_;
        if (function_button_notice_.has_value())
            snapshot->notices.push_back(*function_button_notice_);
        // 신뢰하지 않아 쓰지 않는 문서 실행 파일은 문서 진단보다 먼저 보인다. 조회가 문서와 다른 git으로 돈다는 것을 띠의
        // 첫 알림으로 알린다 (S1).
        if (std::optional<notice_view_model> executables { vcs_executables_notice() }; executables.has_value())
            snapshot->notices.insert(snapshot->notices.begin(), std::move(*executables));
        // 다시 읽기 실패는 문서 진단보다 먼저 보인다. 띠가 첫 알림만 보이므로 묻히면 실패를 모른다.
        if (reload_notice_.has_value())
            snapshot->notices.insert(snapshot->notices.begin(), *reload_notice_);
        // 저장 실패는 그보다도 앞이다. UI는 첫 notice만 표시한다. 다시 읽기에 실패한 뒤의 저장은 대개 저장 충돌이고, 그
        // 알림이 다시 읽기를 다시 하라고 안내한다. 다시 읽기의 사유는 띠 tooltip에 남는다.
        if (save_notice_.empty() == false)
            snapshot->notices.insert(snapshot->notices.begin(), notice_view_model { save_notice_ });
        snapshot->window_width = window_width_;
        snapshot->window_height = window_height_;
        snapshot->scale = scale_;
        snapshot->scroll_offset = scroll_offset_;
        snapshot->relative_paths = relative_paths();
        snapshot->appearance = effective_appearance();
        // 글꼴은 UI thread가 이 값으로 registry를 갱신한다 (F1). 문서가 덮어쓴 값이
        // 있으면 그것이 유효 값이다 (2026-08-23 지시).
        snapshot->fonts = effective_fonts();
        // 환경설정이 열려 있는 동안에는 초안을 그대로 미리 보여 준다 (E2). `저장`이
        // 초안을 문서·앱 설정에 남기고, `취소`는 초안을 버려 원래 값으로 돌아간다.
        if (settings_dialog_.has_value())
        {
            snapshot->appearance.theme = settings_dialog_->theme;
            if (settings_dialog_->accent_id.empty() == false)
                snapshot->appearance.accent_id = settings_dialog_->accent_id;
            // 글꼴도 같은 규칙이다 (2026-08-23 지시). 고른 글꼴이 곧바로 화면에
            // 보이고 `취소`면 되돌아간다.
            snapshot->fonts.ui_family = settings_dialog_->ui_font_family;
            snapshot->fonts.code_family = settings_dialog_->code_font_family;
        }
        if (generate_dialog_.has_value())
        {
            generate_document_dialog_view dialog {};
            dialog.name = { generate_dialog_->name.text, generate_dialog_->name.caret, generate_dialog_->name.anchor };
            apply_composition(text_input_target::generate_document_name, dialog.name);
            dialog.name_placeholder = u8"확장자 없이 문서 이름";
            dialog.scan_root = generate_dialog_->scan_root;
            dialog.location = generate_dialog_->location;
            dialog.same_folder = generate_dialog_->same_folder;
            dialog.executing = generate_dialog_->executing;
            dialog.document_path = generate_document_path();
            dialog.can_confirm = dialog.document_path.empty() == false && dialog.executing == false;

            // 안내는 실패 사유가 있으면 그것이고, 없으면 아직 덜 채운 것을 알린다.
            dialog.message = generate_dialog_->message;
            if (dialog.message.empty() && dialog.can_confirm == false && dialog.executing == false)
            {
                if (const std::u8string_view error { generate_document_name_error(generate_dialog_->name.text) }; error.empty() == false)
                    dialog.message = error;
                else if (generate_dialog_->name.text.empty())
                    dialog.message = u8"만들 문서의 이름을 입력하세요.";
                else if (generate_dialog_->scan_root.empty())
                    dialog.message = u8"저장소들을 담은 폴더를 지정하세요.";
                else
                    dialog.message = u8"문서를 저장할 폴더를 지정하세요.";
            }
            snapshot->generate_dialog = { std::move(dialog) };
        }

        if (font_dialog_.has_value())
        {
            font_dialog_view dialog {};
            dialog.title = font_dialog_->code ? u8"코드 글꼴 선택" : u8"UI 글꼴 선택";
            dialog.query = { font_dialog_->query.text, font_dialog_->query.caret, font_dialog_->query.anchor };
            apply_composition(text_input_target::font_filter, dialog.query);
            dialog.query_placeholder = u8"글꼴 이름으로 거르기";
            dialog.query_action_enabled = dialog.query.text.empty() == false;
            dialog.preview = std::u8string { font_preview_text };

            const std::vector<std::u8string> families { filtered_font_families() };
            dialog.names.reserve(families.size());
            for (const std::u8string& family : families)
                dialog.names.push_back(family.empty() ? std::u8string { default_font_display_name } : family);
            dialog.families = families;
            if (font_dialog_->selected.has_value() && *font_dialog_->selected < families.size())
            {
                dialog.selected = font_dialog_->selected;
                dialog.preview_family = families[*font_dialog_->selected];
                dialog.can_confirm = true;
            }
            if (families.empty())
                dialog.list_note = font_families_.empty() ? u8"시스템 글꼴 목록을 읽지 못했습니다." : u8"이름이 맞는 글꼴이 없습니다.";

            const float content { static_cast<float>(families.size()) * layout_font_dialog_row_height() };
            float maximum { content - layout_font_dialog_list_height };
            if (maximum < 0.0f)
                maximum = 0.0f;
            dialog.scroll_offset = font_dialog_->scroll_offset > maximum ? maximum : font_dialog_->scroll_offset;
            snapshot->font_dialog = { std::move(dialog) };
        }

        snapshot->window_placement_request = window_placement_;
        snapshot->window_placement_revision = window_placement_revision_;
        snapshot->document_generating = pending_generation_operation_id_ != 0;
        snapshot->shutting_down = shutting_down_;
        // 출력 수집 기능이 실행 중이거나 저장하지 못한 변경이 있으면 창을 닫기 전에 묻는다 (function-buttons-design 4.10, D28).
        snapshot->exit_needs_confirmation = shutting_down_ == false && (running_capture_count() > 0 || has_unsaved_changes());
        snapshot->cards = build_ordered_cards();

        // 선택 카드의 로그 뷰다. 필터를 통과한 record만 담고 스크롤은 이미 고정된
        // 값이라 렌더러는 그대로 그린다 (REQ-008).
        if (has_log_pane())
        {
            // 기능 줄 유무는 문서의 카드를 훑는 판정이라 snapshot마다 한 번만 구해 스크롤 한계와 줄 view가 함께 쓴다.
            const bool function_bar { has_function_bar() };
            // pane은 항상 열려 있다. 선택 카드가 없으면 제목만 있는 빈 모델을 게시하고,
            // 고르라는 안내는 본문이 한 번만 한다 (제목과 본문이 같은 말을 둘로 했다).
            log_view_model log {};
            log.title = u8"로그";
            log.filter = log_filter_;
            log.auto_scroll = log_auto_scroll_;
            if (selected_.has_value())
                for (const card_state& card : cards_)
                {
                    if ((card.project.id == *selected_) == false)
                        continue;

                    log.card = card.project.id;
                    log.title = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
                    for (const operation_log_record& record : card.log.records())
                        if (log_entry_matches_filter(record.entry, log_filter_))
                            log.records.push_back(record);
                    // 렌더링·스크롤은 접힌 표시 목록을, 복사는 records를 쓴다.
                    log.lines = build_log_display_lines(card.log.records(), log_filter_);
                    log.truncated = card.log.dropped_count() > 0;
                    // 실행 중이면 헤더의 경과 시간이 흐르고, 끝난 뒤에는 마지막
                    // 명령이 걸린 시간을 그대로 세워 둔다. 둘 다 없으면 표시하지
                    // 않는다 (아직 아무 명령도 실행하지 않았다).
                    // 기능 실행도 같은 표시다 (function-buttons-design 2.3.6). 기준점은
                    // started이라 풀 대기 시간이 섞이지 않는다. 출력 수집 기능과 update·switch는
                    // 서로 막으므로 둘이 함께 흐르지 않는다.
                    const function_run_state* started_function { nullptr };
                    for (const function_run_state& run : card.function_runs)
                    {
                        if (run.phase == function_run_phase::running)
                            started_function = &run;
                    }
                    if (card.change_operation_id != 0)
                        log.change_started_at = { card.change_started_at };
                    else if (started_function != nullptr)
                        log.change_started_at = { started_function->started_at };
                    else
                        log.change_elapsed = card.change_elapsed;

                    float maximum { log_content_height() - log_viewport_height(function_bar) };
                    if (maximum < 0.0f)
                        maximum = 0.0f;
                    log.scroll_offset = log_auto_scroll_ ? maximum : (log_scroll_offset_ > maximum ? maximum : log_scroll_offset_);
                    break;
                }
            snapshot->log = { std::move(log) };
            // 기능 줄은 로그 pane 안 맨 위다 (function-buttons-design 2.4). 줄 유무는 선택과 상관없다 (D23-A).
            if (function_bar)
                snapshot->function_bar = { build_function_bar_view() };
            snapshot->function_focus = function_focus_;
        }

        if (switch_dialog_.has_value())
        {
            for (const card_state& card : cards_)
            {
                if ((card.project.id == switch_dialog_->card) == false)
                    continue;

                switch_dialog_view dialog {};
                dialog.card = card.project.id;
                dialog.title = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
                dialog.loading = switch_dialog_->loading;
                dialog.stale = switch_dialog_->candidates.stale;
                dialog.svn_browser = switch_dialog_->subversion;
                const std::vector<std::size_t> visible { filtered_switch_candidates() };
                dialog.candidates.reserve(visible.size());
                for (const std::size_t index : visible)
                {
                    const switch_candidate& candidate { switch_dialog_->candidates.candidates[index] };
                    if (candidate_is_current(card.snapshot, candidate, switch_dialog_->candidates.candidates))
                        dialog.current = dialog.candidates.size();
                    dialog.candidates.push_back(candidate);
                }
                if (switch_dialog_->svn_browser.has_value())
                    dialog.svn_rows = build_svn_repository_browser_rows(*switch_dialog_->svn_browser);
                dialog.selected = switch_dialog_->selected;
                dialog.executing = switch_dialog_->executing;
                dialog.message = switch_dialog_->message;

                // 질의 막대다 (switch-dialog-query-bar-design Q1). Git은 필터,
                // SVN은 실제 경로 주소 막대라 문구와 우측 버튼의 몫이 다르다.
                dialog.query = { switch_dialog_->query.text, switch_dialog_->query.caret, switch_dialog_->query.anchor };
                apply_composition(text_input_target::switch_query, dialog.query);
                dialog.query_enabled = switch_dialog_->loading == false && switch_dialog_->executing == false;
                if (dialog.svn_browser)
                {
                    dialog.query_placeholder = u8"이동할 경로 (예: /branches/2026-08)";
                    dialog.query_action = switch_query_action::navigate;
                    dialog.query_action_enabled = dialog.query.text.empty() == false && switch_dialog_->svn_browser.has_value();
                }
                else
                {
                    dialog.query_placeholder = u8"브랜치 이름으로 필터링";
                    dialog.query_action = dialog.query.text.empty() ? switch_query_action::search : switch_query_action::clear;
                    dialog.query_action_enabled = dialog.query.text.empty() == false;
                }

                // 목록이 비었을 때의 안내다. 필터가 비운 것과 후보가 없는 것은 다른
                // 말이라 logic이 구분해 정한다.
                const bool list_empty { dialog.svn_browser ? dialog.svn_rows.empty() : dialog.candidates.empty() };
                if (switch_dialog_->loading)
                    dialog.list_note = dialog.svn_browser ? u8"SVN 저장소 정보를 조회하는 중입니다..." : u8"전환 후보를 조회하는 중입니다...";
                else if (list_empty && dialog.svn_browser)
                    dialog.list_note = u8"SVN 저장소를 탐색할 수 없습니다.";
                else if (list_empty && dialog.query.text.empty() == false)
                    dialog.list_note = u8"필터에 맞는 후보가 없습니다.";
                else if (list_empty)
                    dialog.list_note = u8"전환할 수 있는 후보가 없습니다.";

                // 확인 버튼 상태와 label은 logic이 한곳에서 정한다 (plan 5.3의 4~5).
                if (switch_dialog_->svn_browser.has_value())
                {
                    const svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
                    dialog.confirm_label = u8"전환";
                    // 이동이 걸어 내려가는 동안의 선택은 지나가는 자리다 (2026-08-23
                    // 지시). 그 사이 `전환`을 받으면 목표가 아니라 중간 폴더로 간다.
                    if (switch_dialog_->loading == false && switch_dialog_->executing == false && browser.reveal_url.empty() && browser.selected_url.empty() == false
                        && browser.selected_url != browser.current_url)
                        dialog.can_confirm = true;
                    else if (browser.selected_url == browser.current_url && dialog.message.empty())
                        dialog.message = u8"현재 위치입니다.";
                }
                else if (switch_dialog_->loading == false && switch_dialog_->selected.has_value() && *switch_dialog_->selected < dialog.candidates.size() && switch_dialog_->executing == false)
                {
                    const switch_candidate& candidate { dialog.candidates[*switch_dialog_->selected] };
                    if (candidate_is_current(card.snapshot, candidate, switch_dialog_->candidates.candidates))
                    {
                        dialog.can_confirm = false;
                        if (dialog.message.empty())
                            dialog.message = u8"이미 현재 참조입니다.";
                    }
                    else
                    {
                        dialog.can_confirm = true;
                        if (switch_dialog_->tracking_confirm_pending)
                            dialog.confirm_label = u8"생성 확인";
                        else if (candidate.requires_tracking_branch)
                            dialog.confirm_label = u8"브랜치 만들고 전환";
                        else
                            dialog.confirm_label = u8"전환 실행";
                    }
                }
                if (dialog.confirm_label.empty())
                    dialog.confirm_label = u8"전환 실행";

                const std::size_t row_count { dialog.svn_browser ? dialog.svn_rows.size() : dialog.candidates.size() };
                const float content { static_cast<float>(row_count) * layout_switch_dialog_row_height() };
                const float viewport { switch_dialog_list_height(dialog.svn_browser, window_height_ / scale_) };
                float maximum { content - viewport };
                if (maximum < 0.0f)
                    maximum = 0.0f;
                dialog.scroll_offset = switch_dialog_->scroll_offset > maximum ? maximum : switch_dialog_->scroll_offset;
                snapshot->switch_dialog = { std::move(dialog) };
                break;
            }
        }

        if (discovery_dialog_.has_value())
        {
            discovery_dialog_view dialog {};
            dialog.scan_root = discovery_dialog_->scan_root;
            dialog.loading = discovery_dialog_->loading;
            dialog.executing = discovery_dialog_->register_operation_id != 0;
            dialog.message = discovery_dialog_->message;
            dialog.rows.reserve(discovery_dialog_->result.candidates.size());
            bool any_checked { false };
            for (std::size_t index = 0; index < discovery_dialog_->result.candidates.size(); ++index)
            {
                discovery_row_view row {};
                row.candidate = discovery_dialog_->result.candidates[index];
                row.checked = discovery_dialog_->checked[index];
                any_checked = any_checked || row.checked;
                dialog.rows.push_back(std::move(row));
            }
            dialog.can_confirm = dialog.loading == false && dialog.executing == false && any_checked;

            const float content { static_cast<float>(dialog.rows.size()) * layout_discovery_dialog_row_height() };
            float maximum { content - layout_discovery_dialog_list_height };
            if (maximum < 0.0f)
                maximum = 0.0f;
            dialog.scroll_offset = discovery_dialog_->scroll_offset > maximum ? maximum : discovery_dialog_->scroll_offset;
            snapshot->discovery_dialog = { std::move(dialog) };
        }

        if (settings_dialog_.has_value())
        {
            settings_dialog_view dialog { build_settings_dialog_view() };
            const float maximum { ui::settings_dialog_metrics(dialog, window_height_ / scale_).maximum_scroll };
            dialog.scroll_offset = settings_dialog_->scroll_offset > maximum ? maximum : settings_dialog_->scroll_offset;
            snapshot->settings_dialog = { std::move(dialog) };
        }

        if (local_changes_dialog_.has_value())
        {
            local_changes_dialog_view dialog {};
            dialog.card = local_changes_dialog_->card;
            dialog.title = local_changes_dialog_->title;
            dialog.loading = local_changes_dialog_->loading;
            dialog.message = local_changes_dialog_->message;
            dialog.list_scroll = local_changes_dialog_->list_scroll;
            dialog.diff_loading = local_changes_dialog_->diff_loading;
            dialog.diff_rows = local_changes_dialog_->diff_rows;
            dialog.diff_notice = local_changes_dialog_->diff_notice;
            dialog.diff_scroll = local_changes_dialog_->diff_scroll;
            std::u8string_view card_root {};
            for (const card_state& card : cards_)
                if (card.project.id == local_changes_dialog_->card)
                {
                    card_root = card.project.path.normalized.empty() ? std::u8string_view { card.project.path.original } : std::u8string_view { card.project.path.normalized };
                    break;
                }
            dialog.rows.reserve(local_changes_dialog_->entries.size());
            for (std::size_t index = 0; index < local_changes_dialog_->entries.size(); ++index)
            {
                const local_change_entry& entry { local_changes_dialog_->entries[index] };
                local_change_row_view row {};
                row.badge = std::u8string { local_change_kind_badge(entry.kind) };
                row.untracked = entry.kind == local_change_kind::untracked;
                // Git status는 미추적 디렉터리를 `/`로 끝나는 항목 하나로 접는다.
                row.directory = entry.path.empty() == false && entry.path.back() == u8'/';
                row.path = entry.path;
                row.absolute_path = join_local_change_path(card_root, entry.path);
                row.selected = local_changes_dialog_->selected == index;
                dialog.rows.push_back(std::move(row));
            }
            snapshot->local_changes_dialog = { std::move(dialog) };
        }

        if (function_editor_.has_value())
            snapshot->function_editor = { build_function_editor_view() };

        // 긴 줄은 지금 창 폭으로 나눈다. 창 크기가 바뀌면 다음 snapshot이 다시 나눈다.
        if (function_confirm_.has_value())
            snapshot->function_confirm_dialog = { wrapped_function_confirm_view() };

        if (notice_dialog_.has_value())
            snapshot->notice_dialog = notice_dialog_;

        if (context_menu_.has_value())
        {
            // 항목은 메뉴 종류별 builder가 짓는다 (function-buttons-design T8). 대상이
            // 사라져 항목이 없으면 메뉴를 그리지 않는다.
            std::vector<context_menu_item_view> items { context_menu_items(*context_menu_) };
            if (items.empty() == false)
            {
                context_menu_view menu {};
                // 카드 메뉴와 기능 메뉴만 대상 카드를 갖는다. 배너(문서) 메뉴는 owner가 비어 있다.
                if (context_menu_->kind != context_menu_kind::document)
                    menu.owner = context_menu_->card;
                menu.anchor_x = context_menu_->anchor_x;
                menu.anchor_y = context_menu_->anchor_y;
                // 기능 메뉴만 기본 항목이 있다 (2.3.2). 메뉴를 열고 Enter 한 번이면 첫 기능이 실행된다. 비활성 항목은
                // Enter가 고르지 못하므로 첫 **활성** 기능이다. 그 카드에서 출력 수집이 돌고 있으면 비운다 — 습관적으로
                // 친 Enter가 실행 중인 작업을 끊거나 다른 기능을 시작하지 않게 한다. 첫 쪽만이다 (D34) — `기능 더 보기…`를
                // 고른 손이 이어 친 Enter가 다음 쪽의 기능을 시작하지 않게 한다.
                if (context_menu_->kind == context_menu_kind::functions && context_menu_->function_first == 0)
                {
                    const card_state* card { nullptr };
                    for (const card_state& candidate : cards_)
                    {
                        if (candidate.project.id == context_menu_->card)
                            card = &candidate;
                    }
                    if (card != nullptr && function_running(*card) == false)
                    {
                        for (std::size_t index = 0; index < items.size(); ++index)
                        {
                            if (items[index].entry == context_menu_entry::run_function && items[index].enabled)
                            {
                                menu.default_item = index;
                                break;
                            }
                        }
                    }
                }
                menu.items = std::move(items);
                snapshot->context_menu = { std::move(menu) };
            }
        }

        if (document_loading_)
            snapshot->empty_state = view_empty_state::document_loading;
        else if (document_.has_value() == false)
        {
            snapshot->empty_state = view_empty_state::no_document;
            // 열린 문서가 없으면 빈 문구 대신 시작 페이지를 그린다 (A1.3).
            start_page_view page {};
            page.loading = app_settings_loaded_ == false;
            page.recents.reserve(app_settings_.recent_documents.size());
            for (const recent_document& value : app_settings_.recent_documents)
            {
                recent_document_view row {};
                row.display_name = value.display_name.empty() ? recent_document_display_name(value.path) : value.display_name;
                row.folder = to_display_path(windows_parent_directory(value.path));
                row.path = value.path;
                row.pinned = value.pinned;
                page.recents.push_back(std::move(row));
            }
            snapshot->start_page = { std::move(page) };
        }
        else if (cards_.empty())
            snapshot->empty_state = view_empty_state::no_projects;
        else if (snapshot->cards.empty())
            snapshot->empty_state = view_empty_state::no_filter_match;
        else
            snapshot->empty_state = view_empty_state::none;
        return snapshot;
    }
} // namespace gitman

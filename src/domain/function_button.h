#pragma once

#include "domain/cmd_command_line.h"
#include "domain/process_execution.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gitman {
    // project.h가 문서 모델에 기능 버튼 절을 넣으려고 이 헤더를 include하므로
    // 여기서는 project.h를 include하지 않고 이름만 알린다.
    struct project_definition;
    struct repository_snapshot;

    // 기능 버튼의 배치다 (function-buttons-design 2.2.2). 문서에 적힌 원문과 화면에
    // 보이는 배치는 다를 수 있다. 판정은 늘 보이는 배치(resolve_visible_placements)로 한다.
    enum class function_placement
    {
        card,
        bar,
        menu,
    };

    // `card` 배치는 문서 전체에서 이 수까지만 카드에 보인다 (D19). 넘친 것은 `bar`로 보인다.
    inline constexpr std::size_t maximum_card_function_buttons { 3 };
    // 카드당 적용 기능의 상한이다 (B6). 넘으면 파서가 진단 경고를 낸다.
    inline constexpr std::size_t maximum_functions_per_card { 20 };

    enum class function_run_mode
    {
        // 출력을 카드 로그로 모은다. stdin NUL, timeout, 취소를 보장한다.
        capture,
        // 창을 보이며 따로 띄우고 시작 성공 여부만 기록한다.
        detached,
    };

    enum class function_vcs_scope
    {
        any,
        git,
        subversion,
    };

    inline constexpr std::int32_t default_function_timeout_seconds { 3600 };
    inline constexpr std::int32_t maximum_function_timeout_seconds { 86400 };
    inline constexpr std::size_t maximum_function_button_id_length { 64 };
    // label은 이 코드 포인트 수를 넘으면 경고만 하고 그대로 쓴다. 넘치는 글은 말줄임된다.
    inline constexpr std::size_t maximum_function_label_length { 40 };
    inline constexpr std::u8string_view default_function_icon { u8"play" };
    inline constexpr std::u8string_view default_function_working_directory { u8"${projectRoot}" };

    // 검증에 실패한 항목의 원문이다. domain은 JSON 라이브러리를 모르므로 파서가
    // 원문 JSON을 다시 직렬화한 문자열로 든다. 쓰기 함수가 같은 자리에 되살린다 (3.9).
    struct function_source_text
    {
        std::u8string json {};

        [[nodiscard]] bool operator==(const function_source_text&) const = default;
    };

    // 검증을 통과한 기능 버튼 하나다 (3.2). 변수가 든 필드는 치환 전 원문이다.
    struct function_button
    {
        std::u8string id {};
        std::u8string label {};
        // 값이 없거나 비면 기본 아이콘(`play`)이다. 생략과 `""`를 구분해 다시 쓸 때
        // 파일의 키가 바뀌지 않게 하고, 생략한 문서에 기본값 키를 만들지 않는다.
        std::optional<std::u8string> icon {};
        std::u8string program {};
        std::vector<std::u8string> args {};
        // 값이 없으면 `${projectRoot}`다.
        std::optional<std::u8string> cwd {};
        function_run_mode mode { function_run_mode::capture };
        function_vcs_scope vcs { function_vcs_scope::any };
        // 값이 없으면 모든 project에 적용된다. 빈 배열은 어디에도 적용되지 않는다.
        // project.h와 서로 include하지 않으려고 project id 값을 문자열로 든다.
        std::optional<std::vector<std::u8string>> project_ids {};
        bool confirm { false };
        // 0은 무제한이다 (D18). `capture`에서만 쓴다.
        std::int32_t timeout_seconds { default_function_timeout_seconds };
        // 문서에 적힌 배치 원문이다. 생략하면 값이 없다. 알 수 없는 값이나 4번째 `card`도
        // 고치지 않고 들고 있어 다른 일로 저장해도 파일의 값이 바뀌지 않는다 (3.2).
        std::optional<std::u8string> placement {};

        [[nodiscard]] bool operator==(const function_button&) const = default;
        [[nodiscard]] std::u8string_view working_directory() const noexcept;
        [[nodiscard]] std::u8string_view icon_name() const noexcept;
    };

    // `buttons` 배열의 한 자리다. 실패한 항목도 문서 순서를 지키려고 같은 배열에 둔다.
    using function_button_entry = std::variant<function_button, function_source_text>;

    // 사용자 변수 하나다. 값이 문자열이 아니면 원문을 든다. 이름 규칙을 어긴 항목도
    // 원문 보존을 위해 그대로 두고, 쓸 수 있는지는 is_usable_function_variable로 본다.
    struct function_variable
    {
        std::u8string name {};
        std::variant<std::u8string, function_source_text> value {};

        [[nodiscard]] bool operator==(const function_variable&) const = default;
    };

    // `project_variables.<project id>` 하나다. object가 아니면 원문을 든다.
    struct function_project_variables
    {
        std::u8string project {};
        std::variant<std::vector<function_variable>, function_source_text> variables {};

        [[nodiscard]] bool operator==(const function_project_variables&) const = default;
    };

    // 문서의 `function_buttons` 절이다 (3.1). 모든 배열은 문서 순서를 지킨다.
    struct function_buttons_definition
    {
        std::vector<function_variable> variables {};
        std::vector<function_project_variables> project_variables {};
        std::vector<function_button_entry> buttons {};
        // 신뢰 해시 입력이다 (3.10). shadow는 project_store만 읽으므로 파서가 문서를 읽을 때
        // `function_buttons` 원문에서 버튼마다 `placement`를 빼고, 화면에 보이는 `card`만 `"card"`로 다시 적어
        // 직렬화해 싣는다 (`label`·`icon`은 넣는다, D29, D31). logic이 project `{id, path}`와 합쳐 해시를 만든다.
        std::u8string source_digest_input {};

        [[nodiscard]] bool operator==(const function_buttons_definition&) const = default;
    };

    [[nodiscard]] std::optional<function_placement> parse_function_placement(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view function_placement_name(function_placement placement) noexcept;
    [[nodiscard]] std::optional<function_run_mode> parse_function_run_mode(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view function_run_mode_name(function_run_mode mode) noexcept;
    [[nodiscard]] std::optional<function_vcs_scope> parse_function_vcs_scope(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view function_vcs_scope_name(function_vcs_scope scope) noexcept;

    // 원문이 뜻하는 배치다. 생략하면 `menu`이고 (D22-B), 알 수 없는 값이면 값이 없다.
    // 문서 전체의 `card` 상한은 보지 않는다.
    [[nodiscard]] std::optional<function_placement> declared_function_placement(const function_button& button) noexcept;
    // `buttons`와 같은 순서로 각 항목의 보이는 배치를 돌려준다 (2.2.2 규칙 4).
    // 알 수 없는 값은 `bar`, 문서 순서로 4번째부터의 `card`도 `bar`다. 검증에 실패한
    // 항목은 화면에 나오지 않으므로 값이 없고 `card` 수에도 들지 않는다.
    [[nodiscard]] std::vector<std::optional<function_placement>> resolve_visible_placements(std::span<const function_button_entry> buttons);

    // 이름 규칙이다 (3.2, 3.4). 파서와 편집 dialog가 같은 판정을 쓴다.
    [[nodiscard]] bool is_function_button_id(std::u8string_view value) noexcept;
    [[nodiscard]] bool is_function_variable_name(std::u8string_view value) noexcept;
    // 내장 변수 이름과 `env`다. 사용자 변수가 이 이름을 쓰면 무시한다.
    [[nodiscard]] bool is_reserved_variable_name(std::u8string_view value) noexcept;
    [[nodiscard]] bool is_builtin_variable_name(std::u8string_view value) noexcept;
    // 카드의 로컬 조회가 끝나야 풀리는 내장 변수다 (3.3).
    [[nodiscard]] bool is_runtime_variable_name(std::u8string_view value) noexcept;
    [[nodiscard]] bool is_usable_function_variable(const function_variable& variable) noexcept;

    // 치환 결과의 각 구간이 어디서 왔는지다 (3.5). 표시용 명령줄은 `environment`
    // 구간을 이름으로 되돌리고, cmd 방어의 거부 사유는 이 출처로 변수를 가리킨다.
    enum class variable_origin
    {
        literal,
        document,
        runtime,
        environment,
    };

    // 내장 변수 하나의 값이다. 값이 없으면 이름은 알지만 지금 풀리지 않는다
    // (로컬 조회 전, detached 등).
    struct variable_binding
    {
        std::u8string name {};
        std::optional<std::u8string> value {};
        variable_origin origin { variable_origin::document };

        [[nodiscard]] bool operator==(const variable_binding&) const = default;
    };

    // 문서 변수 위에 저장소 덮어쓰기를 얹은 사용자 변수 하나다. 값은 치환 전 원문이며
    // 쓸 때 내장 변수와 `${env:}`만 한 단계 푼다 (3.4).
    struct user_variable
    {
        std::u8string name {};
        std::u8string value {};

        [[nodiscard]] bool operator==(const user_variable&) const = default;
    };

    // 치환에 쓰는 값 묶음이다. logic thread가 클릭 시점에 만든다. `environment`는
    // `${env:NAME}`의 값을 돌려주고 정의되지 않았으면 값이 없다. 비어 있으면 어떤
    // env 이름도 정의되지 않은 것으로 본다.
    struct variable_lookup
    {
        std::vector<variable_binding> builtins {};
        std::vector<user_variable> user {};
        std::function<std::optional<std::u8string>(std::u8string_view)> environment {};
    };

    // 내장 변수 표를 만든다 (3.3). 경로 출처 값은 구분자를 `\`로 통일한다.
    // `snapshot`이 null이면 로컬 조회 전이라 런타임 변수는 모두 풀리지 않는다.
    [[nodiscard]] std::vector<variable_binding> make_builtin_variables(std::u8string_view document_path, const project_definition& project, const repository_snapshot* snapshot);
    // `${projectName}`(`GITMAN_PROJECT_NAME`)의 값이다. 표시 이름이 비면 id다. 신뢰 해시(T7)도 이 값을
    // 넣어 실행 입력과 같은 규칙을 본다.
    [[nodiscard]] std::u8string_view function_project_name(const project_definition& project) noexcept;
    // 카드당 적용 수(20개, B6)를 셀 때의 정적 적용 판정이다. `project_ids`와 `vcs`를 project의 `vcs_hint`로만 보고
    // `vcs_hint: auto`는 적용되는 것으로 센다. 파서의 21번째 경고와 카드 view의 상한이 같은 기준을 써야 경고한
    // 기능이 조회 뒤 메뉴에 나타나지 않는다 (function-buttons-design T9).
    [[nodiscard]] bool function_counts_toward_card(const function_button& button, const project_definition& project) noexcept;
    // 문서 변수에 그 project의 덮어쓰기를 얹는다. 같은 이름이면 저장소 값이 이긴다.
    // 쓸 수 없는 항목(원문 항목, 이름 규칙 위반, 예약 이름)은 뺀다.
    [[nodiscard]] std::vector<user_variable> make_user_variables(const function_buttons_definition& definition, std::u8string_view project);

    enum class expansion_error_kind
    {
        // `${` 뒤에 `}`가 없다.
        unterminated,
        // `${}` 또는 `${env:}`다.
        empty_name,
        // 내장 변수에도 사용자 변수에도 없는 이름이다.
        undefined,
        // 내장 변수지만 지금 값이 없다 (D13).
        unresolved,
        // `${env:NAME}`의 NAME이 환경에 없다.
        environment_undefined,
    };

    struct expansion_error
    {
        expansion_error_kind kind { expansion_error_kind::undefined };
        // 문제가 된 변수 이름이다. env면 `env:`를 뺀 NAME이다. 사용자 변수 값 안에서
        // 생긴 문제면 그 안쪽 이름이다.
        std::u8string name {};
        // 원문 안에서 문제가 된 `${`의 위치(byte)다.
        std::size_t offset { 0 };

        [[nodiscard]] bool operator==(const expansion_error&) const = default;
    };

    // 치환 결과의 한 구간이다. 구간들은 결과 글을 빈틈없이 순서대로 덮는다. 값이
    // 빈 변수도 길이 0인 구간을 남겨 표시용 명령줄이 이름을 되살릴 수 있게 한다.
    struct expansion_segment
    {
        std::size_t offset { 0 };
        std::size_t length { 0 };
        variable_origin origin { variable_origin::literal };
        // 그 구간을 만든 변수 이름이다. 글자 그대로면 비고, env면 NAME이다. 사용자 변수
        // 값의 글자 부분은 그 사용자 변수 이름이다.
        std::u8string variable {};

        [[nodiscard]] bool operator==(const expansion_segment&) const = default;
    };

    struct variable_expansion
    {
        // 실패하면 비어 있다. 일부만 치환한 글을 쓰지 않게 한다.
        std::u8string text {};
        std::vector<expansion_segment> segments {};
        std::optional<expansion_error> error {};

        [[nodiscard]] bool succeeded() const noexcept;
    };

    // 문법은 `${name}`과 `${env:NAME}`이고 `$${`는 글자 그대로의 `${`다 (3.5). 치환한
    // 값은 다시 훑지 않는다. 사용자 변수 값만 내장 변수와 env를 한 단계 푼다.
    // 프로세스·파일 I/O를 하지 않는 순수 함수다.
    [[nodiscard]] variable_expansion expand_variables(std::u8string_view template_text, const variable_lookup& lookup);

    struct variable_reference
    {
        std::u8string name {};
        bool environment { false };
        std::size_t offset { 0 };

        [[nodiscard]] bool operator==(const variable_reference&) const = default;
    };

    struct variable_scan
    {
        std::vector<variable_reference> references {};
        // 문법 오류(`unterminated`, `empty_name`)만 담는다. 이름이 있는지는 보지 않는다.
        std::optional<expansion_error> error {};
    };

    // 값 없이 원문의 참조만 훑는다. 정적 검사(3.7)와 런타임 의존 판정이 쓴다.
    [[nodiscard]] variable_scan scan_variable_references(std::u8string_view template_text);

    // 표시용 글이다 (3.6). `environment` 구간은 값 대신 `${env:NAME}`으로 되돌리고
    // 나머지는 치환한 값을 쓴다.
    [[nodiscard]] std::u8string display_text(const variable_expansion& expansion);

    // 명령줄 인용(`build_process_command_line`)과 비밀 가리기(`mask_secrets`)는
    // infrastructure에 있어 domain이 부를 수 없다. 상위 계층이 넘긴다. 조립 규칙은 실제 요청과 같게
    // `program`이 `cmd.exe`이면 `cmd`다 (4.3, R1).
    using command_line_joiner = std::function<std::u8string(std::u8string_view, std::span<const std::u8string>, command_line_syntax)>;
    using secret_text_masker = std::function<std::u8string(std::u8string_view)>;

    // 로그 시작 줄, `confirm` 미리보기, 신뢰·편집 dialog에 보이는 명령줄이다 (3.6).
    // 인자마다 display_text를 만들어 `join`으로 잇고 결과 전체에 `mask`를 한 번 적용한다.
    // 치환한 실제 `program`이 `cmd.exe`이면(`${env:}`로 가린 표시 글로 정하면 cmd를 놓친다) `join` 대신 cmd 조립
    // (`assemble_function_cmd_command_line`)의 표시용 명령줄을 쓴다. 보정 결정이 실제 명령줄과 같다 (D30). 실제
    // argv에는 쓰지 않는다.
    // 부르는 쪽이 지킬 것: `program`과 `arguments`는 모두 성공한 치환이다. 실패한 치환을
    // 넘기면 빈 인자로 조용히 이어지므로 편집 dialog 미리보기도 오류가 있으면 이 함수를
    // 부르지 않고 오류를 보인다. `join`과 `mask`는 비어 있지 않다. 비밀 가리기를 빼고
    // 부르는 길을 두지 않으려고 빈 함수를 받아 주지 않는다(비어 있으면 std::bad_function_call).
    [[nodiscard]] std::u8string build_display_command_line(const variable_expansion& program, std::span<const variable_expansion> arguments, const command_line_joiner& join, const secret_text_masker& mask);

    // 치환한 `program`이 허용 목록(4.3)에 걸린 까닭이다. 두 실행 모드에 같게 적용한다.
    enum class function_program_rejection
    {
        not_absolute,
        // 마지막 구성 요소가 `.`이나 공백으로 끝난다. Windows가 이름을 정규화하며 지워
        // `run.bat.`이 `run.bat`으로 실행되는 우회를 막는다.
        trailing_dot_or_space,
        // 확장자가 `.exe`·`.com`이 아니다. 스크립트는 인터프리터를 `program`에 적는다.
        extension_not_allowed,
    };

    // 치환이 끝난 `program`을 본다. 절대 경로 → 끝 글자 → 확장자 순서이고 처음 걸린
    // 까닭 하나를 돌려준다. 통과하면 값이 없다. filesystem은 보지 않는다.
    [[nodiscard]] std::optional<function_program_rejection> check_function_program(std::u8string_view program) noexcept;
    // 진단과 로그에 쓰는 문구다. `program`으로 시작하고 확장자 거부에는 인터프리터 예를 담는다.
    [[nodiscard]] std::u8string function_program_rejection_message(function_program_rejection rejection);

    // 치환한 `program`의 파일 이름이 `cmd.exe`인지다(대소문자 무시, 구분자는 `\`와 `/`). 폴더는 보지 않으므로
    // System32 밖의 사본도 cmd로 본다. 이 판정이 참이면 실행 요청과 표시용 명령줄을 cmd 전용으로 조립한다 (4.3).
    [[nodiscard]] command_line_syntax function_command_line_syntax(std::u8string_view program) noexcept;
    [[nodiscard]] bool is_cmd_program(std::u8string_view program) noexcept;

    // cmd 방어(4.3)에 걸린 자리다.
    struct cmd_injection_finding
    {
        // 값이 없으면 `program`이고, 있으면 그 번째 인자다.
        std::optional<std::size_t> argument {};
        variable_origin origin { variable_origin::runtime };
        // 그 구간을 만든 변수 이름이다. env면 `env:`를 뺀 NAME이다. 글자 그대로의 구간이면 비어 있다.
        std::u8string variable {};
        char8_t character { u8'%' };

        [[nodiscard]] bool operator==(const cmd_injection_finding&) const = default;
    };

    // `program`이 `cmd.exe`일 때 cmd 명령줄로 아예 전달할 수 없는 문자(`cmd_uncorrectable_characters`: 줄바꿈)를
    // 찾는다. 출처를 가리지 않는다. `% ! "`와 `& | < > ^ ( )`는 cmd 전용 조립이 보정하고 사용자가 최종 승인한다
    // (D30, R4). `program`이 cmd가 아니면 늘 값이 없다.
    [[nodiscard]] std::optional<cmd_injection_finding> find_cmd_injection(const variable_expansion& program, std::span<const variable_expansion> arguments);
    // 거부 사유 문구다. `실행하지 않습니다:` 접두는 거부 줄을 남기는 logic이 붙인다.
    [[nodiscard]] std::u8string cmd_injection_message(const cmd_injection_finding& finding);

    // 내장 변수를 내보내는 고정 이름 표다 (3.3, D14). `svnUrl`은 내보내지 않는다.
    struct function_environment_export
    {
        std::u8string_view variable {};
        std::u8string_view environment {};
    };

    [[nodiscard]] std::span<const function_environment_export> function_environment_exports() noexcept;

    // cmd 조립(`assemble_cmd_command_line`)의 인자 하나다. 글자 그대로의 구간은 `literal` 조각이고, 나머지는 값
    // 조각이다. `${env:}` 구간은 표시용 글이 `${env:NAME}`이라 표시용 명령줄과 보정 목록에 값이 남지 않는다 (3.6).
    [[nodiscard]] cmd_argument make_cmd_argument(const variable_expansion& expansion);
    // 배치 파일 안에서 읽힐 **런타임** 값(`GITMAN_BRANCH`, `GITMAN_REPOSITORY_ROOT`, `GITMAN_VCS`, `GITMAN_REVISION`)이다
    // (D26). 배치 줄의 `%GITMAN_BRANCH%`는 `&` 해석보다 먼저 펼쳐지고 gitman이 고칠 수 없는 글이라 메타 문자가 있으면
    // 경고한다(R4에서 거부를 경고로 바꿈). 문서 출처 값(`GITMAN_WORKSPACE_FOLDER` 등)은 문서 작성자의 것이라 넣지 않는다.
    [[nodiscard]] std::vector<cmd_exported_value> function_cmd_exported_values(std::span<const variable_binding> builtins);
    // `program`이 `cmd.exe`인 기능의 명령줄, 보정, 경고다 (4.3, D30). `builtins`가 비면 환경 변수 경고를 보지 않는다.
    // `facts`는 worker가 확인한 명령 대상(배치 파일, 공백 상대 경로)이다 (S3). 없으면 글자만으로 짓는다.
    [[nodiscard]] cmd_command_line assemble_function_cmd_command_line(const variable_expansion& program, std::span<const variable_expansion> arguments, std::span<const variable_binding> builtins, const cmd_target_facts* facts = nullptr);
    // cmd 기능에서 worker가 실행 전에 확인할 명령 대상이다 (S3, `find_cmd_target_query`). 작업 폴더는 부르는 쪽이 채운다.
    [[nodiscard]] std::optional<cmd_target_query> function_cmd_target_query(std::span<const variable_expansion> arguments);

    // 정적 검사(3.7)가 받는 사용자 변수 이름이다. 문서 변수와, `project_ids`(카드로 읽은 project)의 덮어쓰기 중 쓸 수
    // 있는 것이다. 카드 목록에 없는 project의 덮어쓰기는 어느 카드에서도 풀리지 않으므로 세지 않는다. 파서와 편집
    // dialog(T12)가 같은 이름 집합으로 검증한다.
    [[nodiscard]] std::vector<std::u8string> function_user_variable_names(const function_buttons_definition& definition, std::span<const std::u8string> project_ids);

    // 음이 아닌 정수의 십진 글이다. 편집 dialog의 목록 머리·`Alt+N`·새 id 번호와 규칙 사유의 `args[N]`이 같이 쓴다.
    [[nodiscard]] std::u8string function_decimal_text(std::size_t value);

    // 버튼 하나를 파서와 같은 규칙으로 본다 (3.2, 3.7, 4.3). JSON 형식 검사는 파서의 몫이고, 여기서는 모델 값의 규칙(id
    // 이름 규칙, 비지 않은 label·program, timeout 범위, 변수 문법과 이름 존재, 변수 없는 program의 허용 목록)을 본다.
    // 걸리면 첫 사유를 돌려주고 통과하면 빈 글이다. 편집 dialog의 Apply가 모델을 바꾸기 **전에** 같은 함수로 검증한다
    // (2.7) — 모델에 잘못된 값이 들어가면 저장 재검증이 이후 모든 저장을 막는다.
    [[nodiscard]] std::u8string function_button_problem(const function_button& button, std::span<const std::u8string> user_variable_names);
} // namespace gitman

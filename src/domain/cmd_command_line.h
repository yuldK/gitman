#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // `cmd.exe`에 넘기는 글의 한 조각이다 (function-buttons-design 4.3, D30). 조각들이 인자 글을 순서대로 덮는다.
    // `literal`은 문서 작성자가 `args`에 글자 그대로 쓴 구간이고, 아니면 변수로 치환한 값이다. `display`는 표시용
    // 명령줄에 대신 보일 글이다(`${env:NAME}`). 값이 없으면 `text`를 보인다.
    struct cmd_text_piece
    {
        std::u8string text {};
        bool literal { false };
        std::optional<std::u8string> display {};

        [[nodiscard]] bool operator==(const cmd_text_piece&) const = default;
    };

    struct cmd_argument
    {
        std::vector<cmd_text_piece> pieces {};

        // 조각의 `text`를 이은 실제 값이다.
        [[nodiscard]] std::u8string text() const;
        // 조각의 표시용 글을 이은 값이다.
        [[nodiscard]] std::u8string display_text() const;
    };

    // 배치 파일 안에서 읽힐 환경 변수 하나다 (`GITMAN_BRANCH` 등, D26).
    struct cmd_exported_value
    {
        std::u8string name {};
        std::u8string value {};
    };

    // cmd가 원래 값과 다르게 읽을 자리를 gitman이 고친 방법이다 (D30의 보정). 공백 경로를 따옴표로 감싸는 보통
    // 조립(공백·`, ; =` 때문에 어차피 감싸는 토큰의 괄호·`&` 포함), `/s`·`/c`를 넣는 것, argv[0]의 `/`를 `\`로 쓰는
    // 것은 뜻을 바꾸지 않아 보정으로 치지 않는다. 새 종류는 `sentence` 앞에 더한다(구현이 `sentence`로 개수를 센다).
    enum class cmd_correction_kind
    {
        // `& | < > ^ ( )`를 따옴표 안에 넣었다.
        metacharacter,
        // `%`를 따옴표 밖의 `^%`로 썼다. `%NAME%`이 환경 변수로 펼쳐지지 않는다.
        percent,
        // `"`를 따옴표 안에서 `""`로 겹쳤다. 따옴표 짝이 맞아 뒤의 메타 문자가 따옴표 안에 남는다.
        quote,
        // 지연 확장을 끄는 `/v:off`를 넣었다. `!NAME!`이 펼쳐지지 않는다.
        delayed_expansion,
        // 공백이 든 글자 그대로의 인자를 한 토큰이 아닌 명령 문장으로 넘겼다.
        sentence,
    };

    struct cmd_correction
    {
        // 바뀐 인자의 순번(`args`)이다. 값이 없으면 gitman이 넣은 스위치(`/v:off`)다.
        std::optional<std::size_t> argument {};
        // 표시용 글이다. 원래 인자와 명령줄에 쓴 글이고, 스위치면 원래 글이 비어 있다.
        std::u8string original {};
        std::u8string corrected {};
        std::vector<cmd_correction_kind> kinds {};

        [[nodiscard]] bool operator==(const cmd_correction&) const = default;
    };

    // gitman이 고칠 수 없는 위험이다 (D30의 경고). 거부하지 않고 확인 dialog에 보인다.
    enum class cmd_warning_kind
    {
        // 배치가 읽을 환경 변수 값에 cmd가 다시 해석하는 문자가 있다. 배치가 따옴표 없이 쓰면 명령이 된다 (D26).
        exported_environment,
        // 작성자가 `/v:on`을 적어 `!`를 무력화하지 못했다.
        delayed_expansion_enabled,
        // 명령 문장에 글자 그대로 쓴 `%`·`!`는 작성자의 cmd 문법이라 cmd가 그대로 펼친다(`!`는 지연 확장이 켜졌을 때).
        sentence_expansion,
        // 명령 문장의 글자 그대로 부분이 값 바로 앞에서 `^`로 끝나거나 따옴표를 연 채로 끝나, gitman이 감싼 따옴표가
        // 거꾸로 읽혀 값이 따옴표 밖에 남는다. 그 값은 보정으로 세지 않는다 (R4 검토).
        unprotected_value,
    };

    struct cmd_warning
    {
        cmd_warning_kind kind { cmd_warning_kind::exported_environment };
        // `exported_environment`의 환경 변수 이름과 값이다. 값(브랜치 이름, 저장소 루트)은 비밀이 아니고, 사용자가 본 값과
        // 다른 값으로는 승인 기억(D30)이 맞지 않게 하려고 싣는다 — `main&whoami`를 승인한 뒤 `main&calc`로 바뀌면 다시 묻는다.
        std::u8string environment {};
        std::u8string value {};
        // 그 밖의 경고가 가리키는 인자 순번이다.
        std::optional<std::size_t> argument {};
        char8_t character { u8'&' };

        [[nodiscard]] bool operator==(const cmd_warning&) const = default;
    };

    // cmd 명령줄로 아예 전달할 수 없는 글자가 든 인자다. 줄바꿈은 cmd가 명령을 거기서 끝내고 `^`로도 이을 수 없다
    // (2026-11-03 실측, R4).
    struct cmd_rejection
    {
        std::size_t argument { 0 };
        char8_t character { u8'\n' };

        [[nodiscard]] bool operator==(const cmd_rejection&) const = default;
    };

    struct cmd_command_line
    {
        // 실제 명령줄이다. runner·launcher가 그대로 쓴다.
        std::u8string text {};
        // 같은 결정으로 지은 표시용 명령줄이다. env 구간은 이름이다. 비밀 가리기는 부르는 쪽이 한다.
        std::u8string display_text {};
        std::vector<cmd_correction> corrections {};
        std::vector<cmd_warning> warnings {};
        std::optional<cmd_rejection> rejection {};

        // 보정이나 경고가 있어 실행 전에 사용자의 최종 승인을 받아야 한다 (D30).
        [[nodiscard]] bool needs_approval() const noexcept;
    };

    // `cmd.exe` 전용 명령줄을 짓는다 (4.3, D27, D30). 실제 cmd.exe로 정한 규칙이다(2026-11-03, Windows 11 26300).
    // - argv[0]은 `/`를 `\`로 바꿔 감싼다. cmd는 argv[0] 안의 `/c`도 스위치로 읽는다. argv[0]의 `% ! & ( )`는
    //   cmd가 해석하지 않는다.
    // - 앞쪽의 스위치(`/d`, `/v:on` 꼴)는 그대로 둔다. `/c`·`/k`·`/r`이 스위치 줄을 끝내고, 스위치 꼴이 아닌 첫
    //   인자에서 `/c` 없이 명령이 시작되면 `/c`를 넣는다. 그 앞에 `/s`가 없으면 넣는다. `/c:x`·`/k:1`·`/r:x`는
    //   스위치가 아니라 명령의 시작이다.
    // - 명령 인자는 비었거나 공백·탭, `, ; =`, `& | < > ^ ( )`, `"`가 있으면 따옴표로 감싼다. 감싼 토큰 안의 `"`는
    //   `""`로 겹치고, `"` 앞과 끝의 `\`는 두 배로 늘린다. `%`가 있으면 `%`를 기준으로 나눈 조각을 따로 감싸고 그
    //   사이에 따옴표 밖의 `^%`를 둔다(`"a"^%"PATH"^%"b"`). 펼칠 이름이 언제나 `"`로 시작하거나 `^`로 끝나
    //   정의된 변수와 맞지 않고, `^`는 cmd가 지운다. 배치가 `%*`·`"%~1"`로 다시 넘겨도 따옴표 짝이 맞는다.
    // - 명령의 첫 인자가 글자 그대로 쓴 부분에 공백·탭을 담으면 명령 문장이다(`["/c", "echo hi"]`). 단 절대 경로로
    //   시작하면 경로이고, 글자 그대로 쓴 부분에서 실행 확장자(`.exe .com .bat .cmd`) 뒤에 공백이 오면 다시 문장이다.
    //   변수 값이 절대 경로이고 바로 뒤 글자 그대로의 부분이 `\`·`/`로 시작하면 경로를 이어 쓴 것으로 본다.
    //   문장의 글자 그대로의 부분은 그대로 쓰고, 변수 구간만 위 규칙으로 감싼다. 작성자의 따옴표 안이면 감싸지 않고
    //   `"`·`%`만 고치며, 곧바로 `"`가 오면 끝의 `\`를 두 배로 늘린다.
    // - 따옴표 상태는 명령 전체에 걸쳐 센다. 문장이 값 바로 앞에서 `^`로 끝나거나 따옴표를 연 채로 끝나 값을 보호할 수
    //   없으면 그 값은 보정으로 세지 않고 `unprotected_value` 경고다.
    // - 값(명령 문장의 글자 그대로 부분은 빼고)에 `!`가 있고 `/v:` 스위치가 없으면 `/v:off`를 넣는다. `/v:on`이면 경고다.
    // - `exported`는 배치가 읽을 환경 변수다. 값에 `& | < > ^ ( ) % ! "`, `, ; =`, 줄바꿈이 있으면 경고다.
    // - 줄바꿈이 든 인자는 거부다(`rejection`). 그래도 명령줄은 짓는다.
    [[nodiscard]] cmd_command_line assemble_cmd_command_line(const cmd_text_piece& executable, std::span<const cmd_argument> arguments, std::span<const cmd_exported_value> exported = {});

    // 확인 dialog와 로그에 쓰는 이유 문구다. 보정 종류가 여럿이면 쉼표로 잇는다.
    [[nodiscard]] std::u8string cmd_correction_reason(std::span<const cmd_correction_kind> kinds);
    [[nodiscard]] std::u8string cmd_warning_message(const cmd_warning& warning);
}

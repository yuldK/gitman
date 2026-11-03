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
        // 명령이 부르는 배치 파일이 지연 확장을 켠다(`setlocal EnableDelayedExpansion`, 자식 `cmd /v:on`). 넣은 `/v:off`는
        // 배치 안까지 막지 못해 넘긴 값의 `!NAME!`이 펼쳐진다 (S3, R4 보안 검토 실측).
        batch_delayed_expansion,
        // 명령이 부르는 배치 파일이 `call`을 쓴다. `call`로 다시 넘긴 값은 따옴표 안 `^`가 두 배가 되고 `%`가 한 번 더
        // 펼쳐진다 (S3, 4.3의 "값이 그대로 닿지 않는 경우").
        batch_call,
        // 배치 파일을 확인하지 못했다 (S3). 크기 상한(`cmd_batch_inspection_limit`)을 넘었거나, 있는데 열지 못했거나, 작업
        // 폴더로 이을 수 없는 경로(다른 드라이브의 `D:x.bat`)이거나, 신뢰하기 전이라 로컬 드라이브 밖을 읽지 않았다.
        batch_unchecked,
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

    // 명령의 첫 인자가 가리키는 파일 중 글자만으로 정할 수 없어 worker가 디스크에서 확인할 것이다 (function-buttons-design
    // 4.3, S3). logic은 파일을 읽지 않으므로(ADR-004) 실행 전에 worker에 묻고, 답(`cmd_target_facts`)으로 명령줄을 짓는다.
    struct cmd_target_query
    {
        // 상대 경로를 잇는 작업 폴더다. 글로 판정하는 `find_cmd_target_query`는 비워 두고 logic이 채운다.
        std::u8string working_directory {};
        // 명령의 첫 인자 순번(`args`)이다.
        std::size_t argument { 0 };
        // 글자만으로 본 판정이 명령 문장이다.
        bool sentence { false };
        // 공백이 든 상대 경로다. 글자만으로는 문장이지만 작업 폴더에 이 파일이 있으면 경로 하나로 본다.
        std::optional<std::u8string> relative_path {};
        // 경로로 볼 때 부르는 배치 파일(`.bat`·`.cmd`)이다. 비면 배치가 아니다.
        std::u8string batch_if_path {};
        // 문장으로 볼 때 첫 토큰이 가리키는 배치 파일이다. 비면 배치가 아니다.
        std::u8string batch_if_sentence {};

        [[nodiscard]] bool operator==(const cmd_target_query&) const = default;
    };

    // 배치 파일 글에서 찾은 것이다 (S3).
    struct cmd_batch_findings
    {
        // 지연 확장을 켜는 글(`EnableDelayedExpansion`, `cmd /v:on`·`cmd /v`)이 있다.
        bool delayed_expansion { false };
        // `call`이 있다.
        bool call { false };
        // 확인하지 못했다(크기 상한을 넘음, 읽지 못함, 경로를 이을 수 없음, 신뢰 전이라 읽지 않음).
        bool unchecked { false };

        [[nodiscard]] bool operator==(const cmd_batch_findings&) const = default;
    };

    // worker가 확인한 답이다. `query`는 물은 것 그대로라 logic이 지금 값과 같은지 본다.
    struct cmd_target_facts
    {
        cmd_target_query query {};
        // 작업 폴더 기준으로 `query.relative_path` 파일이 있다.
        bool path_exists { false };
        // 부르는 배치 파일을 읽었다. 배치가 아니거나 없거나 읽을 수 없으면 값이 없다.
        std::optional<cmd_batch_findings> batch {};

        [[nodiscard]] bool operator==(const cmd_target_facts&) const = default;
    };

    // 배치 파일을 읽는 상한이다. 손으로 쓴 배치는 이보다 훨씬 작다. 넘으면 읽지 않고 확인하지 못했다고 경고한다.
    inline constexpr std::size_t cmd_batch_inspection_limit { 256 * 1024 };

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
    // - `facts`는 worker가 확인한 답이다(S3). 공백이 든 상대 경로가 작업 폴더에 있으면 문장이 아니라 경로 하나로 넘기고,
    //   부르는 배치 파일의 `setlocal EnableDelayedExpansion`·`call`·크기 상한을 경고로 더한다. 답의 `query`가 이 인자로
    //   지은 것인지는 부르는 쪽이 본다.
    [[nodiscard]] cmd_command_line assemble_cmd_command_line(const cmd_text_piece& executable, std::span<const cmd_argument> arguments, std::span<const cmd_exported_value> exported = {}, const cmd_target_facts* facts = nullptr);

    // 명령의 첫 인자에서 worker가 확인할 것이다 (S3). 확인할 것(공백이 든 경로 꼴 상대 경로, `.bat`·`.cmd`를 부르는 경로나
    // 문장의 첫 토큰)이 없으면 값이 없고 글자만의 판정으로 짓는다. 상대 경로 후보는 문장 판정이 문장이고, 절대 경로가
    // 아니며, `"`가 없고, `\`·`/`가 있거나 실행 확장자로 끝나는 글이다. `echo hi`처럼 경로 꼴이 아닌 문장은 보지 않는다.
    [[nodiscard]] std::optional<cmd_target_query> find_cmd_target_query(std::span<const cmd_argument> arguments);
    // worker가 읽을 배치 파일이다. 상대 경로 후보가 있으면 그 파일이 있을 때 경로로, 없을 때 문장으로 본다. 비면 읽지 않는다.
    [[nodiscard]] std::u8string cmd_target_batch_path(const cmd_target_query& query, bool path_exists);
    // cmd가 작업 폴더에서 찾을 절대 경로다. `/`는 `\`로 바꾸고 `.`·`..`는 글자로 접는다(뿌리 위로 올라가지 않음) — probe가
    // 붙이는 `\\?\`가 Win32의 정규화를 끄기 때문이다 (S3 검토). 절대 경로(`X:\`, `\\server\share`)도 접는다. 루트 상대
    // 경로(`\x.bat`)는 작업 폴더의 드라이브나 share 뿌리에, 같은 드라이브의 드라이브 상대 경로(`C:x.bat`)는 작업 폴더에
    // 잇는다. 장치 경로(`\\?\`, `\\.\`)는 그대로다. 이을 수 없으면(다른 드라이브의 드라이브 상대 경로, 작업 폴더가 절대
    // 경로가 아님) 비어 있다.
    [[nodiscard]] std::u8string cmd_target_absolute_path(std::u8string_view working_directory, std::u8string_view path);
    // 물을 경로(공백 상대 경로, 두 배치 후보)가 모두 드라이브 문자로 시작하는 로컬 경로로 이어진다. 신뢰하기 전에는 이때만
    // 읽는다 — UNC·장치 경로는 읽는 것만으로 네트워크에 접속하고 worker를 오래 붙잡을 수 있다 (S3 검토).
    [[nodiscard]] bool cmd_target_is_local(const cmd_target_query& query);
    // 읽지 않았거나 확인이 실패한 답이다. 공백 상대 경로는 없는 것으로 보고, 배치를 부를 수 있으면 `unchecked`다.
    [[nodiscard]] cmd_target_facts cmd_target_unread_facts(const cmd_target_query& query);
    // 배치 파일 글에서 지연 확장(`EnableDelayedExpansion`, 같은 줄에 `cmd`가 있는 `/v:on`·`/v`)과 낱말 `call`을 찾는다.
    // 대소문자를 가리지 않는다. 낱말 경계는 cmd의 구분자(공백, 탭, `, ; =`, VT, FF, 0xFF)와 `& | < > ( ) " @`이고,
    // `EnableDelayedExpansion`은 경계 없이 찾는다. 각 줄을 그대로, `^`를 지운 사본, 그 사본에서 `%…%` 구간을 지운
    // 사본으로 세 번 본다(`set^local`, `%x%call`). 맨 앞의 UTF-8 BOM은 건너뛰고 `rem`·`::` 줄은 건너뛴다. 줄 단위 글자
    // 검사라 `echo call x`처럼 명령이 아닌 자리도 센다(경고라 넘치는 쪽을 고름). 변수 값으로 조립한 명령은 보지 못한다.
    // 바이트 그대로 보므로 ASCII와 호환되는 인코딩(UTF-8, 코드 페이지)이면 같다.
    [[nodiscard]] cmd_batch_findings scan_cmd_batch_text(std::string_view text);

    // 확인 dialog와 로그에 쓰는 이유 문구다. 보정 종류가 여럿이면 쉼표로 잇는다.
    [[nodiscard]] std::u8string cmd_correction_reason(std::span<const cmd_correction_kind> kinds);
    [[nodiscard]] std::u8string cmd_warning_message(const cmd_warning& warning);
}

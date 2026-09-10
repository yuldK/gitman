#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 사용자가 정의한 명령과 SVN 훅의 값 타입이다
    // (custom-commands-and-svn-hooks-design C1, H6).
    //
    // 이 헤더는 다른 도메인 헤더를 들이지 않는다. `project.h`가 이것을 include하기
    // 때문이며, 반대로 들이면 순환한다. 카드의 정보가 필요한 판정은
    // `command_match_context`로 값만 받는다.

    // 명령을 어떤 VCS의 카드에 보여 줄지다. 카드의 실제 종류를 담을 때도 같은
    // 타입을 쓰며, 그때 `any`는 "아직 판정되지 않았다"를 뜻한다.
    enum class command_target_vcs
    {
        any,
        git,
        subversion,
    };

    // 스크립트를 어느 폴더에서 실행할지다.
    enum class command_working_directory
    {
        // 카드의 저장소 작업 트리다 (기본). C++ 프로젝트를 빌드하는 명령이 이것을 쓴다.
        repository,
        // 열린 `.version-list`가 있는 폴더다.
        document,
        // 스크립트 파일이 있는 폴더다.
        script,
    };

    // 명령을 보여 줄지 판정할 때 필요한 카드의 사실이다. 도메인이 `project.h`를
    // 들이지 않도록 값으로만 받는다.
    struct command_match_context
    {
        std::u8string_view project_id {};
        // 저장소 작업 트리의 경로다. 패턴 비교는 구분자를 `/`로 통일해서 한다.
        std::u8string_view repository_path {};
        std::u8string_view display_name {};
        // 카드의 실제 VCS다. `any`이면 아직 모른다 — 이때 VCS를 지정한 명령은
        // 보여 주지 않는다. 모르는 채로 git 전용 명령을 띄우면 눌러 봐야 실패를 안다.
        command_target_vcs vcs { command_target_vcs::any };
    };

    // 사용자 정의 명령 하나다. 전역 앱 설정과 문서 어느 쪽에도 같은 모양으로 들어간다.
    struct custom_command
    {
        // 목록 안에서 유일하다. element id와 메뉴 key의 재료라 비어 있을 수 없다.
        std::u8string id {};
        // 메뉴와 tooltip에 뜨는 이름이다.
        std::u8string label {};
        // 절대 경로이거나 문서가 있는 폴더 기준 상대 경로다.
        std::u8string script {};
        // 스크립트 **뒤에** 붙는 인자다.
        std::vector<std::u8string> arguments {};
        command_working_directory working_directory { command_working_directory::repository };
        // codicon 이름이다. 모르는 이름은 표시 계층이 기본 글리프로 물러선다.
        std::u8string icon {};
        // 카드 표면에도 버튼으로 낸다. 컨텍스트 메뉴에는 이 값과 무관하게 늘 나온다.
        bool show_on_card { false };
        command_target_vcs vcs { command_target_vcs::any };
        // 저장소 경로와 표시 이름에 대한 glob이다. 빈 값이면 거르지 않는다.
        std::u8string path_pattern {};
        // 카드 id 목록이다. 비어 있으면 나머지 두 필터만 본다.
        std::vector<std::u8string> projects {};
        // 0이면 제한 없음이다. 취소로만 끝난다.
        std::int32_t timeout_seconds { 0 };
        // 끝난 뒤 카드 상태를 다시 조회한다.
        bool refresh_after { true };

        [[nodiscard]] bool operator==(const custom_command&) const noexcept = default;
    };

    // 훅을 거는 지점이다. TortoiseSVN의 update 훅 세 종류와 이름을 맞췄고, switch도
    // 같은 단계를 쓴다 (TortoiseSVN이 그렇게 한다).
    enum class svn_hook_phase
    {
        // 사전 검사 전이다. 훅이 작업 트리를 건드리면 그 결과가 검사에 반영된다.
        start_update,
        // 사전 검사를 통과한 뒤 `svn update`·`svn switch` 직전이다.
        pre_update,
        // 본 명령 직후, 재조회 전이다. 실패해도 경고만 남긴다.
        post_update,
    };

    struct svn_hook
    {
        svn_hook_phase phase { svn_hook_phase::pre_update };
        std::u8string script {};
        std::vector<std::u8string> arguments {};
        bool enabled { true };
        // 작업 복사본 경로에 대한 glob이다. 빈 값이면 모든 SVN 카드에 건다.
        std::u8string path_pattern {};
        std::int32_t timeout_seconds { 0 };

        [[nodiscard]] bool operator==(const svn_hook&) const noexcept = default;
    };

    struct svn_hook_settings
    {
        // TortoiseSVN이 설치되어 있으면 그 클라이언트 훅도 함께 실행한다. 기본 켬이다.
        bool use_tortoise_svn { true };
        std::vector<svn_hook> hooks {};

        [[nodiscard]] bool operator==(const svn_hook_settings&) const noexcept = default;
        // 기본값 그대로다. 전역 설정을 쓸 때 JSON에 구역을 만들지 판정한다.
        [[nodiscard]] bool is_default() const noexcept;
    };

    // 명령과 훅을 함께 담은 구체 값이다. 전역 앱 설정이 이것을 든다.
    struct command_catalog
    {
        std::vector<custom_command> custom {};
        svn_hook_settings svn_hooks {};

        [[nodiscard]] bool operator==(const command_catalog&) const noexcept = default;
        [[nodiscard]] bool is_default() const noexcept;
    };

    // 문서가 덮어쓴 것만 담는다 (C2). 목록은 스칼라와 달리 항목별 병합 규칙을 둘
    // 방법이 없어 — "전역의 이 항목만 지우기"를 표현할 수 없다 — **통째로 대체**한다.
    // 값이 없으면 전역을 따르고, 빈 배열이라도 있으면 정의다.
    struct command_catalog_overrides
    {
        std::optional<std::vector<custom_command>> custom {};
        std::optional<svn_hook_settings> svn_hooks {};

        [[nodiscard]] bool operator==(const command_catalog_overrides&) const noexcept = default;
        // 모든 항목이 "따름"이다. 저장 시 `commands` object를 만들지 않는 판정이다.
        [[nodiscard]] bool empty() const noexcept;
    };

    [[nodiscard]] command_catalog apply_overrides(const command_catalog& base, const command_catalog_overrides& overrides);

    // 편집 화면이 한 줄로 보여 주는 인자 목록을 서로 옮긴다. 공백과 탭이 구분자이고,
    // 그것이나 따옴표가 든 조각은 따옴표로 감싼다. 따옴표 구간 **안**의 `""`는
    // 따옴표 글자 하나다 (Windows 명령줄 관례) — 여는 따옴표 바로 뒤의 `""`는 빈
    // 인자다. 두 규칙 덕에 `split(join(x)) == x`가 어떤 값에서도 성립한다.
    [[nodiscard]] std::vector<std::u8string> split_command_arguments(std::u8string_view text);
    [[nodiscard]] std::u8string join_command_arguments(const std::vector<std::u8string>& arguments);

    // 카드 id 목록도 한 줄로 편집한다. 이쪽은 쉼표가 구분자다 — 카드 id에는 공백이
    // 들어갈 수 있어서다.
    [[nodiscard]] std::vector<std::u8string> split_command_projects(std::u8string_view text);
    [[nodiscard]] std::u8string join_command_projects(const std::vector<std::u8string>& projects);

    // 이름에서 명령 id를 만든다. 사용자에게 보이지 않는 값이라 화면에서 고치지 않고
    // 만들어 준다. 소문자 영숫자와 `-`만 남기며, 남는 것이 없으면 `command`다.
    // `taken`에 이미 있는 id면 뒤에 번호를 붙인다.
    [[nodiscard]] std::u8string make_command_id(std::u8string_view label, const std::vector<std::u8string>& taken);

    // `*`(0자 이상)와 `?`(1자)만 있는 glob이다. ASCII 대소문자를 무시하고 경로
    // 구분자는 `/`로 통일해서 비교한다. 빈 패턴은 무엇에나 맞는다.
    [[nodiscard]] bool command_pattern_matches(std::u8string_view pattern, std::u8string_view text);

    // 세 필터를 모두 통과해야 그 카드에 보여 준다 (AND).
    [[nodiscard]] bool command_matches(const custom_command& command, const command_match_context& context);
    [[nodiscard]] bool svn_hook_matches(const svn_hook& hook, std::u8string_view repository_path);

    // 값이 성립하지 않는 이유다. 비어 있으면 쓸 수 있는 정의다. 파서와 실행 경로가
    // 같은 판정을 쓰도록 도메인에 둔다.
    [[nodiscard]] std::u8string_view custom_command_error(const custom_command& command) noexcept;
    [[nodiscard]] std::u8string_view svn_hook_error(const svn_hook& hook) noexcept;

    [[nodiscard]] std::optional<command_target_vcs> parse_command_target_vcs(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view command_target_vcs_name(command_target_vcs value) noexcept;
    [[nodiscard]] std::optional<command_working_directory> parse_command_working_directory(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view command_working_directory_name(command_working_directory value) noexcept;
    [[nodiscard]] std::optional<svn_hook_phase> parse_svn_hook_phase(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view svn_hook_phase_name(svn_hook_phase value) noexcept;
} // namespace gitman

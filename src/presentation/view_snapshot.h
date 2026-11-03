#pragma once

#include "application/svn_repository_browser.h"
#include "domain/app_settings.h"
#include "domain/appearance.h"
#include "domain/discovery.h"
#include "domain/operation_log.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "presentation/diff_presentation.h"
#include "presentation/status_presentation.h"

#include "luil/ui/text_input_state.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gitman {
    // 카드 하나에서 본 기능 버튼 하나다 (function-buttons-design 2.3, 4.8). logic이 snapshot을 만들 때 판정을
    // 끝내 싣고, 카드 열·기능 메뉴·카드 메뉴가 같은 값을 쓴다. 활성 판정은 배치와 무관하다.
    struct card_function_view
    {
        std::u8string button_id {};
        std::u8string label {};
        // 아이콘 이름이다 (D16). 이름 → 글리프는 presentation이 정하고 모르는 이름은 `play`다.
        std::u8string icon {};
        // 보이는 배치다 (2.2.2 규칙 4). 4번째 `card`와 알 수 없는 값은 이미 `bar`다.
        function_placement placement { function_placement::menu };
        // `Alt+N`의 N이다 (1~3). 문서 전체의 보이는 `card` 순서이고 `card`가 아니면 0이다.
        std::size_t shortcut { 0 };
        bool enabled { false };
        // 비활성 사유다. tooltip과 메뉴 라벨 접미가 쓴다.
        std::u8string disabled_reason {};
        // 그 기능의 출력 수집 실행이 이 카드에서 돌고 있다. 버튼은 중지 버튼으로 활성이다.
        bool running { false };
        // 마지막 실행이 실패했다 (실패 점). 종료 코드가 없는 실패(시간 초과, 시작 실패, 거부)도 있다.
        bool failed { false };
        std::optional<std::int32_t> exit_code {};
        // 실행 중이고 started를 받았으면 그 시각이다. 메뉴 라벨의 경과 시간 기준이다.
        std::optional<std::chrono::steady_clock::time_point> started_at {};
    };

    // 렌더러가 그대로 그리는 카드 하나의 불변 표시 모델이다 (REQ-005).
    struct card_view_model
    {
        project_id id {};
        std::u8string display_name {};
        std::u8string path {};
        repository_kind kind { repository_kind::unknown };
        card_view_state state { card_view_state::loading };
        status_glyph status {};
        // Git은 현재 branch, SVN은 URL이다. provider가 채운 current_reference를 쓴다.
        std::u8string reference {};
        std::u8string revision {};
        std::u8string working_tree_text {};
        std::u8string comparison_target {};
        std::optional<std::chrono::system_clock::time_point> local_checked_at {};
        std::optional<std::chrono::system_clock::time_point> remote_checked_at {};
        bool busy { false };
        bool selected { false };
        bool enabled { true };
        // 변경 작업(update·switch)을 시작할 수 있는 상태다. 로컬 조회가 끝난 준비
        // 상태의 활성 카드만 참이다. 실제 보호 정책은 provider가 실행 직전에 다시
        // 검사한다 (stage-7-plan 4.4).
        bool can_change { false };
        // 변경 작업이 실행 중이다. update 버튼이 중지 버튼으로 바뀐다.
        bool change_running { false };
        // 이 카드에 적용되는 기능이다 (function-buttons-design 2.3). 문서 순서이고 카드당 20개까지다.
        // 비어 있으면 카드 열은 기능 버튼 이전과 같다(`⋯`도 구분선도 없다).
        std::vector<card_function_view> functions {};
        // 출력 수집 기능이 이 카드에서 돌고 있다. 상태 아이콘이 돌고 3번째 줄 맨 앞에 칩이 선다 (2.3.5).
        // `busy`와 따로 둔다 (B8).
        bool function_running { false };
        // 실행 중인 출력 수집 기능의 라벨과 started 시각이다. 칩과 update·switch 비활성 사유
        // (`기능 "빌드"가 실행 중입니다`)가 쓴다. 칩의 경과 시간은 렌더러가 draw 시각과의 차이로 그린다.
        std::u8string running_function_label {};
        std::optional<std::chrono::steady_clock::time_point> running_function_started_at {};
    };

    // 선택 카드 기능 줄의 표시 모델이다 (function-buttons-design 2.2.2 규칙 5, 2.4). 문서의 카드 중 하나에라도
    // 적용되는 `bar` 기능(보이는 배치 기준)이 있으면 늘 있다 — 선택이 바뀔 때 로그 본문 높이가 튀지 않게 한다.
    struct function_bar_view
    {
        // 선택 카드다. 선택이 없으면 비어 있고 줄의 `⋯`는 비활성이다.
        project_id card {};
        std::u8string card_name {};
        // 선택 카드에 적용되는 `bar` 기능이다 (문서 순서, 판정은 카드 view와 같다).
        std::vector<card_function_view> functions {};
        // 선택 카드에 적용되는 기능 전체의 수다. 줄의 `⋯`가 여는 기능 메뉴의 기능 수라 그 접근 이름이 쓴다.
        std::size_t total_functions { 0 };
        // 버튼이 없을 때 그 자리에 보이는 안내다. 버튼이 있으면 비어 있다.
        std::u8string hint {};
    };

    // 초점을 가진 기능 줄 버튼이 배치 변경으로 줄을 떠났을 때의 초점 옮기기다 (2.2.5). logic은 어떤 element가
    // 그려지고 보이는지 모르므로 후보만 싣고, tree를 지은 쪽이 보이는 첫 후보를 고른다: 줄에서 그 버튼 뒤에 있던
    // 버튼들(문서 순서) → 줄의 `⋯` → 카드의 `⋯`. 없으면 초점이 지워진 채로 둔다.
    struct function_focus_request
    {
        // 요청 번호다. 바뀔 때만 새 요청이다 (luil `focus_request::id`).
        std::uint64_t id { 0 };
        project_id card {};
        std::vector<std::u8string> next_buttons {};
    };

    enum class view_empty_state
    {
        none,
        no_document,
        document_loading,
        no_projects,
        no_filter_match,
    };

    // 하단 로그 뷰의 스트림 필터다 (plan 3.9). `output`은 stderr를 제외하고,
    // `errors`는 stderr와 경고 이상 수명 주기만 남긴다.
    enum class log_stream_filter
    {
        all,
        output,
        errors,
    };

    // 로그 뷰 본문의 표시 줄 하나다 (stage-8-plan 5.3). 같은 작업의 연속된 progress
    // record는 마지막 하나로 접히고, collapsed가 이 줄로 접힌 앞선 record 수다.
    struct log_display_line
    {
        operation_log_record record {};
        std::size_t collapsed { 0 };
    };

    // 선택 카드 전용 하단 로그 뷰의 불변 표시 모델이다 (REQ-008). records는 필터를
    // 통과한 것만 담고 scroll_offset은 이미 범위 안으로 고정된 논리 픽셀 값이다.
    // lines는 records에서 progress 접기를 적용한 표시 목록이다. 렌더링과 스크롤
    // 높이는 lines를, 복사는 records를 기준으로 한다 (buffer 보존, stage-8-plan
    // 5.3).
    struct log_view_model
    {
        project_id card {};
        std::u8string title {};
        std::vector<operation_log_record> records {};
        std::vector<log_display_line> lines {};
        log_stream_filter filter { log_stream_filter::all };
        bool auto_scroll { true };
        float scroll_offset { 0.0f };
        // 상한 초과로 오래된 record가 제거된 적이 있다.
        bool truncated { false };
        // 이 카드에서 변경 작업(update·switch)이 실행 중이면 그 시작 시각이다.
        // 헤더의 경과 시간(MM:SS) 표시 기준이며, 렌더러가 draw 시각과의 차이를
        // 그린다 (실시간 갱신은 UI thread의 timer가 일으킨다).
        std::optional<std::chrono::steady_clock::time_point> change_started_at {};
        // 실행 중이 아닐 때, 마지막으로 끝난 명령이 걸린 시간이다. 값이 있으면 그
        // 시간을 그대로 세워 그린다 — 작업이 끝난 뒤에도 확인할 수 있어야 한다
        // (2026-08-22 지시). 다음 명령이 시작되면 `change_started_at`으로 바뀐다.
        std::optional<std::chrono::steady_clock::duration> change_elapsed {};
    };

    // 한 줄 텍스트 박스의 표시 상태는 luil의 것이다 (`luil/ui/text_input_state.h`).
    // element 어휘가 없는 값 헤더라 application 계층이 include해도 된다 (설계 L4).
    using luil::text_input_view;

    // 질의 막대 우측 버튼이 맡은 몫이다 (switch-dialog-query-bar-design Q1).
    // element가 이 값으로 glyph와 클릭 intent를 정한다.
    enum class switch_query_action
    {
        // Git · 질의가 비었다. 장식이라 누를 수 없다.
        search,
        // Git · 질의가 있다. 누르면 필터를 지운다.
        clear,
        // SVN · 누르면 박스의 경로로 이동한다.
        navigate,
    };

    // switch dialog의 불변 표시 모델이다 (REQ-007, stage-7-plan 4.5). 후보 목록은
    // provider가 만든 순서 그대로다 (remote group 먼저). confirm_label과 message는
    // logic이 검증 상태에서 계산해 UI는 그대로 그린다.
    struct switch_dialog_view
    {
        project_id card {};
        std::u8string title {};
        // 후보 조회가 아직 진행 중이다.
        bool loading { true };
        // fetch 실패로 cache된 tracking ref만으로 만든 목록이다.
        bool stale { false };
        // true이면 candidates 대신 svn_rows를 그리는 저장소 브라우저다. 초기 저장소
        // 정보 조회 중에도 true라 제목과 안내 문구가 SVN용으로 유지된다.
        bool svn_browser { false };
        // Git은 질의로 **거른** 후보다. `selected`와 선택 intent의 index도 이 목록
        // 기준이며 logic이 원본 index로 되돌린다.
        std::vector<switch_candidate> candidates {};
        std::vector<svn_repository_browser_row> svn_rows {};
        std::optional<std::size_t> selected {};
        // 지금 체크아웃된 후보다 (`candidates` 기준 index). 행이 `현재 브랜치`로
        // 표시되어, 고르면 거부될 후보를 미리 알 수 있다.
        std::optional<std::size_t> current {};
        // 질의 막대다 (switch-dialog-query-bar-design Q1). Git은 필터, SVN은 저장소
        // root 기준 실제 경로를 담는 주소 막대다.
        text_input_view query {};
        std::u8string query_placeholder {};
        // 후보 조회 중이거나 전환 실행 중이면 막대 전체가 비활성이다.
        bool query_enabled { false };
        switch_query_action query_action { switch_query_action::search };
        bool query_action_enabled { false };
        // 목록이 비었거나 조회 중일 때 목록 자리에 그릴 흐린 안내다. 비어 있으면
        // 그리지 않는다.
        std::u8string list_note {};
        bool can_confirm { false };
        std::u8string confirm_label {};
        // 검증·거부·안내 메시지다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        // 전환 실행이 제출되어 결과를 기다리는 중이다.
        bool executing { false };
        // 후보 목록의 스크롤 위치다 (논리 픽셀, 이미 고정됨).
        float scroll_offset { 0.0f };
    };

    // 글꼴 선택 dialog의 불변 표시 모델이다 (fonts-and-in-app-dialogs-design F2.5).
    // 목록의 각 행은 **그 글꼴로** 그려 미리 보기가 되고, 아래 미리 보기 칸은 고른
    // 글꼴로 한글·영문·숫자·기호를 한 줄에 보여 준다.
    struct font_dialog_view
    {
        // true이면 코드 글꼴을 고르는 중이다. 제목이 이 값으로 갈린다.
        std::u8string title {};
        // 표시 이름이다. 첫 항목은 `(기본 글꼴)`이고 그 `family`가 비어 있다.
        std::vector<std::u8string> names {};
        // `names`와 같은 길이의 실제 가족 이름이다. 빈 값은 내장 글꼴이다.
        std::vector<std::u8string> families {};
        std::optional<std::size_t> selected {};
        // 이름 필터다. switch dialog의 질의 막대와 같은 편집 규칙이다.
        text_input_view query {};
        std::u8string query_placeholder {};
        // 질의가 있을 때만 지우기 버튼이 활성이다.
        bool query_action_enabled { false };
        // 목록이 비었을 때의 흐린 안내다.
        std::u8string list_note {};
        // 미리 보기 문구다. 고른 글꼴로 그린다.
        std::u8string preview {};
        // 미리 보기에 쓸 가족 이름이다 (빈 값이면 내장 글꼴).
        std::u8string preview_family {};
        bool can_confirm { false };
        float scroll_offset { 0.0f };
    };

    // 새 문서 만들기 dialog의 불변 표시 모델이다 (fonts-and-in-app-dialogs-design
    // F3). 이전의 Win32 모달 팝업을 앱 UI로 옮긴 것이라 검증 문구도 logic이 정한다.
    struct generate_document_dialog_view
    {
        // 문서 이름이다 (확장자 없이). 유일한 편집 칸이다.
        text_input_view name {};
        std::u8string name_placeholder {};
        // 저장소들을 담은 폴더다. Win32 폴더 dialog가 고른다.
        std::u8string scan_root {};
        // 문서를 저장할 폴더다. `same_folder`이면 쓰지 않는다.
        std::u8string location {};
        bool same_folder { true };
        // 지금 값으로 만들어질 최종 경로다. 비어 있으면 아직 정해지지 않았다.
        std::u8string document_path {};
        // 검증 실패 사유다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        bool can_confirm { false };
        // 생성이 제출되어 결과를 기다린다.
        bool executing { false };
    };

    // 로컬 변경 확인 dialog의 목록 한 행이다 (field-feedback-design 2.3).
    struct local_change_row_view
    {
        // "수정"·"미추적" 같은 종류 배지다.
        std::u8string badge {};
        std::u8string path {};
        // 외부 열기(VSCode·탐색기)에 쓰는 절대 경로다.
        std::u8string absolute_path {};
        // 미추적 항목은 흐리게 그린다.
        bool untracked { false };
        bool directory { false };
        bool selected { false };
    };

    // 로컬 변경 확인 dialog의 불변 표시 모델이다 (field-feedback-design 2.3).
    // 상단이 변경 목록, 하단이 선택 항목의 diff viewer다. diff 줄의 색은 element가
    // 줄 첫 문자로 판정한다.
    struct local_changes_dialog_view
    {
        project_id card {};
        std::u8string title {};
        bool loading { true };
        std::vector<local_change_row_view> rows {};
        // 목록 수준 오류·안내다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        float list_scroll { 0.0f };
        bool diff_loading { false };
        // 2-way diff의 행이다 (diff_presentation::build_two_way_diff).
        std::vector<two_way_diff_row> diff_rows {};
        // 이진 파일·디렉터리·생략 같은 diff pane의 안내다.
        std::u8string diff_notice {};
        float diff_scroll { 0.0f };
    };

    // 컨텍스트 메뉴 항목이 고르면 하는 하위 동작의 식별자다 (field-feedback-design 3장,
    // theme-and-banner-menu-design T1). 모양(라벨·아이콘·단축키·구분선·radio)은 항목 데이터가
    // 들고(function-buttons-design T8), 이 값은 고른 뒤의 동작만 가른다. 외부 열기는 popup이
    // UI thread의 앱 명령으로 곧바로 내고 나머지는 logic이 처리한다. 앞의 여섯은 카드 메뉴,
    // 그다음 둘은 배너(문서) 메뉴의 항목이고, 그 뒤는 기능 메뉴·카드 메뉴 기능 절의 항목이다
    // (function-buttons-design T9). 네 메뉴의 `기능 편집…`은 `edit_functions`다 (T12).
    enum class context_menu_entry
    {
        open_repository,
        // 저장소를 VSCode로 연다 (2026-08-22 지시). 미추적·변경 정리는 앱이 아니라
        // 편집기에서 하는 편이 낫다.
        open_in_vscode,
        show_local_changes,
        refresh,
        update,
        switch_to,
        // 배너 우클릭 메뉴다 (T1). 문서가 있는 폴더를 탐색기로 열고 문서를 선택
        // 상태로 두거나, 그 폴더를 VSCode workspace로 연다.
        open_document_folder,
        open_document_in_vscode,
        // 기능 메뉴와 카드 메뉴 기능 절의 기능 항목이다 (function-buttons-design 2.3.2, 2.3.3). argument가 버튼
        // id다. 실행과 중지를 다른 동작으로 둬, 메뉴가 떠 있는 사이 그 기능이 시작됐으면 늦은 "실행" 클릭이
        // 중지가 되지 않고 닫히기만 한다 (항목 목록에서 같은 동작을 다시 찾으므로).
        run_function,
        stop_function,
        // 카드 메뉴의 `기능 더 보기…`다. 같은 자리에 그 카드의 기능 메뉴를 연다.
        more_functions,
        // 기능 메뉴와 배너 메뉴의 `문서 다시 읽기`다 (안전한 다시 읽기, function-buttons-design 4.9).
        reload_document,
        // 배치 메뉴의 세 radio다 (function-buttons-design 2.2.5). argument가 버튼 id다. 항목 하나에 값이 둘(버튼 id,
        // 배치) 필요해 배치마다 동작을 하나씩 둔다 — argument에 둘을 이어 담으면 늦은 클릭 판정이 나눠 읽어야 한다.
        place_function_on_card,
        place_function_on_bar,
        place_function_in_menu,
        // 기능 메뉴·카드 메뉴·배너 메뉴·배치 메뉴의 `기능 편집…`이다 (function-buttons-design 2.7). 배치 메뉴의 항목만
        // argument에 버튼 id를 실어 그 기능을 고른 상태로 연다.
        edit_functions,
    };

    // `context_menu_entry`의 값 수다. 키 표를 되돌리는 배열이 모든 값을 덮는지 묶는다 (popup의
    // `static_assert`). 새 값은 enum 끝에 더하고 이 값을 함께 고친다.
    inline constexpr std::size_t context_menu_entry_count { static_cast<std::size_t>(context_menu_entry::edit_functions) + 1 };

    // 메뉴 항목 하나의 모양이다. radio는 하나만 고르는 무리(배치 메뉴의 세 배치,
    // function-buttons-design 2.2.5)이고 luil `menu_item_kind::radio`로 그린다.
    enum class context_menu_item_kind
    {
        action,
        radio,
    };

    struct context_menu_item_view
    {
        context_menu_entry entry { context_menu_entry::open_repository };
        std::u8string label {};
        // 진행 중 작업으로 카드 버튼이 비활성이면 해당 항목도 비활성이다.
        bool enabled { true };
        // 외부 열기 항목이 셸에 넘길 절대 경로다 (T1). 항목마다 대상이 달라서
        // 메뉴가 아니라 항목이 들고 있다 — 배너 메뉴의 탐색기 항목은 문서 파일을,
        // VSCode 항목은 문서가 있는 폴더를 받는다. Windows 원형 그대로이며 표시용
        // 변환(`/`)을 적용하지 않는다. 외부 열기가 아닌 항목은 비어 있다.
        std::u8string target_path {};

        // 아래는 메뉴를 항목 데이터로 짓기 위한 칸이다 (function-buttons-design T8). 앞의 네 칸
        // 뒤에 붙여 기존 집합 초기화(`{ entry, label, enabled, target_path }`)가 그대로 뜻을 갖는다.

        // 같은 동작을 여러 항목이 나눠 쓸 때 항목을 가르는 값이다 (기능 메뉴의 버튼 id 등).
        // popup 항목 키와 select intent에 함께 실린다. 비어 있으면 동작 하나에 항목 하나다.
        // 같은 메뉴 안에서 (entry, argument)는 겹치지 않는다. 겹치면 popup 항목 id가 겹치고
        // logic은 목록의 첫 항목만 처리한다.
        std::u8string argument {};
        // 아이콘 이름이다 (codicon 이름, `folder-opened` 등). 이름 → 글리프는 presentation이
        // 정하고(`context_menu_icon_glyph`), 비었거나 모르는 이름이면 아이콘을 그리지 않는다.
        // 기능 버튼의 `icon`도 이름이라 같은 칸에 싣는다.
        std::u8string icon {};
        // 오른쪽 단축키 열(`Alt+1` 등)이다. 표시만 하며 실행 판정에는 쓰지 않는다. 메뉴 항목의
        // 접근 이름은 라벨뿐이라, 낭독해야 하는 상태는 이 열이 아니라 라벨에 넣는다.
        std::u8string shortcut_text {};
        // 참이면 이 항목 위에 구분선을 긋는다 (높이 9).
        bool separator_above { false };
        // radio 항목은 아이콘 자리에 `checked` 점을 그린다 (배치 메뉴).
        context_menu_item_kind kind { context_menu_item_kind::action };
        bool checked { false };
    };

    // 카드 body 또는 배너 우클릭이 여는 컨텍스트 메뉴의 불변 표시 모델이다 (3장,
    // T1). anchor는 우클릭 지점의 창 좌표(물리 픽셀)이며, panel이 창 밖으로 나가면
    // element가 안쪽으로 민다. 항목 수는 정해져 있지 않고 popup 높이는 항목과 구분선
    // 수로 정해진다 (function-buttons-design T8).
    struct context_menu_view
    {
        // 카드 메뉴에서만 채운다. 배너(문서) 메뉴는 비어 있다.
        project_id owner {};
        float anchor_x { 0.0f };
        float anchor_y { 0.0f };
        std::vector<context_menu_item_view> items {};
        // 강조가 없을 때 Enter가 고를 항목의 순번(`items` 안)이다. 비어 있으면 강조 없는
        // Enter는 아무것도 하지 않는다 (카드 메뉴·배너 메뉴). 기능 메뉴가 유휴일 때 첫 기능을
        // 가리킨다 (function-buttons-design 2.3.2).
        std::optional<std::size_t> default_item {};
    };

    // 확인 버튼 하나짜리 알림 다이얼로그다 (app-shell-design A3.2). 파일 연결
    // 결과처럼 UI thread가 곧바로 수행한 작업의 결과를 앱 스타일로 알린다.
    struct notice_dialog_view
    {
        std::u8string title {};
        // 본문 줄 목록이다. 실패는 진단 메시지를 줄 단위로 담는다.
        std::vector<std::u8string> lines {};
        // 실패 알림이면 아이콘과 강조 색이 오류 색이다.
        bool error { false };
    };

    // 기능 실행 확인 dialog의 세 모드다 (function-buttons-design 3.10, 4.5, 4.10). 같은 element를 쓴다.
    enum class function_confirm_mode
    {
        // 신뢰를 기록하지 않은 정의에서 처음 누를 때다. 문서의 기능 정의 전체를 치환 전 원문으로 보인다.
        trust,
        // `confirm: true` 버튼의 실행 전 미리보기다. 그 버튼의 표시용 명령줄과 작업 폴더를 보인다.
        preview,
        // 출력 수집 기능이 실행 중이거나 저장하지 못한 변경이 있을 때 문서를 닫거나 다시 읽거나 다른 문서를 열거나 앱을
        // 끝내려 할 때다 (4.10, D28).
        // 실행 중인 기능을 목록으로 보이고, 확인하면 중지하고(저장하지 못한 변경은 버리고) 그 일을 한다.
        running,
        // `program`이 `cmd.exe`인 기능의 명령을 gitman이 보정했거나 경고가 있을 때의 최종 승인이다 (4.3, D30). 원래 명령,
        // 보정한 실행 명령, 바뀐 자리와 이유, 경고를 보인다. 신뢰 확인·`confirm` 미리보기가 함께 필요하면 그 모드의 dialog
        // 하나에 같은 줄을 싣고 이 모드는 쓰지 않는다.
        correction,
    };

    // dialog 목록 항목의 코드 줄 하나다. 비밀 가리기를 거친 글이고, 제어 문자와 보이지 않는 글자는
    // `<U+XXXX>`로 드러나 있다 (3.6, T7).
    struct function_confirm_line
    {
        std::u8string text {};
        // 변수 값 줄처럼 곁들인 정보는 흐리게 그린다.
        bool dim { false };
        // 앞 줄이 폭을 넘겨 이어지는 줄이다. element가 들여 그린다.
        bool continuation { false };
        // gitman이 고칠 수 없는 위험을 알리는 줄이다 (cmd 보정의 경고, D30). element가 경고색으로 그린다.
        bool warning { false };
    };

    // dialog 목록의 기능 하나다. 줄 수만큼 높이가 정해져 logic의 스크롤 한계와 element의 배치가
    // 같은 함수(`layout_function_confirm_item_height`)를 쓴다. 긴 줄은 logic이 창 폭에 맞춰 여러 줄로
    // 나눠 싣는다 (`function_confirm_code_columns`). 말줄임은 그 어림이 빗나갈 때의 마지막 방어다.
    struct function_confirm_item_view
    {
        std::u8string label {};
        // 실행 방식 배지다 (`출력을 로그로 받기`, `따로 실행`).
        std::u8string mode_text {};
        std::vector<function_confirm_line> lines {};
    };

    struct function_confirm_dialog_view
    {
        function_confirm_mode mode { function_confirm_mode::trust };
        // 제목 바로 아래 한 줄이다. 신뢰 모드는 문서 경로, 미리보기는 카드 이름이다. 넘치면 앞을
        // 줄여 끝(파일 이름)을 남긴다.
        std::u8string subject {};
        // 그 아래 흐린 안내 줄이다.
        std::vector<std::u8string> description {};
        std::vector<function_confirm_item_view> items {};
        // 목록 아래 경고 줄이다 (신뢰가 덮지 않는 것). 비면 그리지 않는다.
        std::vector<std::u8string> note {};
        std::u8string confirm_label {};
        // 실행 중인 기능 확인을 앱 종료에서 열었다 (4.10). 확인 버튼이 logic intent 대신 UI thread의 앱 끝내기
        // 명령을 낸다 — 종료는 창의 닫힘 훅을 받아 간 뒤라 logic이 끝낼 길이 없다.
        bool exit_application { false };
        // 실행 중인 기능 확인에 저장하지 못한 변경도 함께 보인다 (D28, 4.9·4.10). 그 일을 하면 모델에만 있는 변경이
        // 사라진다. 실행 중인 것이 없으면 목록이 비고 제목·대상·경고 줄이 저장하지 못한 변경을 말한다(panel도 목록 자리 없이
        // 낮아진다).
        bool unsaved_changes { false };
        // 목록의 스크롤 위치다 (논리 픽셀, 이미 고정됨).
        float scroll_offset { 0.0f };
    };

    // 기능 편집 dialog의 두 탭이다 (function-buttons-design 2.7). 변수 탭은 읽기 전용 나열이다.
    enum class function_editor_tab
    {
        functions,
        variables,
    };

    // 기능 편집 dialog 목록의 행 하나다. 검증에 실패해 원문만 든 항목은 경고 행이다.
    struct function_editor_row_view
    {
        // 행 element id의 값이다. 통과 항목은 `button-<id>`, 경고 행은 `source-<열 때의 순번>`이다. 순서를 바꿔도 같은 항목에
        // 같은 값이라 luil 초점이 그 항목을 따라간다 (intent는 순번을 싣는다).
        std::u8string key {};
        std::u8string label {};
        // 아이콘 이름이다. 경고 행은 비어 있다.
        std::u8string icon {};
        // 지금 초안 목록으로 판정한 보이는 배치다 (2.2.2 규칙 4). 경고 행은 값이 없다.
        std::optional<function_placement> placement {};
        // 보이는 `card`면 `Alt+N`의 N이다. 아니면 0이다.
        std::size_t shortcut { 0 };
        bool warning { false };
    };

    // 적용 범위가 `저장소 목록`일 때의 카드 하나다.
    struct function_editor_project_view
    {
        std::u8string id {};
        std::u8string name {};
        bool checked { false };
    };

    // 변수 탭의 행 하나다. `scope`가 비면 문서 변수, 아니면 그 project의 덮어쓰기다.
    struct function_editor_variable_view
    {
        std::u8string scope {};
        std::u8string name {};
        std::u8string value {};
        // 쓸 수 없는 변수(이름 규칙 위반, 예약 이름, 문자열이 아닌 값)다. 원문 그대로 둔다.
        bool warning { false };
    };

    // 기능 편집 dialog의 불변 표시 모델이다 (function-buttons-design 2.7). 초안은 logic이 들고 있고, Apply 전까지 문서
    // 모델을 바꾸지 않는다.
    struct function_editor_view
    {
        // `기능 편집 · 문서 이름`이다.
        std::u8string title {};
        function_editor_tab tab { function_editor_tab::functions };
        std::vector<function_editor_row_view> rows {};
        std::optional<std::size_t> selected {};
        // `기능 N개 · 카드 k/3`이다. N은 경고 행을 포함한 행 수, k는 보이는 `card` 수다.
        std::u8string list_header {};

        // 고른 항목의 폼이다. 경고 행을 고르면 `editable`이 꺼지고 사유와 원문을 보인다.
        bool editable { false };
        std::u8string warning_text {};
        std::u8string warning_source {};
        text_input_view label {};
        text_input_view program {};
        text_input_view arguments {};
        text_input_view working_directory {};
        text_input_view timeout {};
        // 인자 칸 아래 줄이다. 닫히지 않은 따옴표면 경고(`arguments_error`)이고 Apply가 막힌다.
        std::u8string arguments_message {};
        bool arguments_error { false };
        std::u8string icon {};
        function_run_mode mode { function_run_mode::capture };
        // 적용 범위 선택 묶음의 값이다: `all`·`git`·`svn`·`projects`.
        std::u8string scope {};
        std::vector<function_editor_project_view> projects {};
        // 배치 선택 묶음이 고른 값이다 (보이는 배치).
        function_placement placement { function_placement::menu };
        // 카드에 두면 갖는 `Alt+N`이다. 고를 수 없으면 0이다.
        std::size_t card_shortcut { 0 };
        // 문서 전체의 보이는 `card`가 3개이고 이 기능이 그중 하나가 아니다. `카드` 라벨이 `카드 — 3개까지`가 된다.
        bool card_full { false };
        // 배치 묶음 아래 줄이다 (`카드 — 3개까지` 거절 등).
        std::u8string placement_message {};
        bool confirm { false };

        // 미리보기다. 대상 카드를 `<` `>`로 바꾼다.
        std::u8string preview_card {};
        bool preview_can_step { false };
        std::u8string preview_command {};
        std::u8string preview_working_directory {};
        std::vector<std::u8string> preview_warnings {};

        std::vector<function_editor_variable_view> variables {};
        // Apply 거절·검증 실패 사유다. 비어 있으면 그리지 않는다.
        std::u8string message {};
        // 문서의 기능 버튼 절을 편집기로 쓸 수 없다 (`function_buttons`가 object가 아니거나 `buttons`가 배열이 아님). Apply가
        // 비활성이고 `message`가 사유다 — 쓰면 원문이 사라진다 (3.9).
        bool locked { false };
        float list_scroll { 0.0f };
        float form_scroll { 0.0f };
    };

    // 알림 띠의 알림 하나다. 띠는 한 줄이라 `text`만 보이고, `details`는 띠 tooltip에서
    // 그 알림 아래 줄로 잇는다. 기능 버튼 경고처럼 여러 진단을 한 알림으로 묶을 때 쓴다
    // (function-buttons-design 3.8).
    struct notice_view_model
    {
        std::u8string text {};
        std::vector<std::u8string> details {};
    };

    // 시작 페이지의 최근 항목 한 행이다 (app-shell-design A1.3).
    struct recent_document_view
    {
        // 확장자를 뗀 문서 이름이다.
        std::u8string display_name {};
        // 문서가 있는 폴더다. 이름 뒤에 흐리게 붙는다.
        std::u8string folder {};
        // 클릭했을 때 여는 문서의 절대 경로다.
        std::u8string path {};
        // 고정한 항목이다 (2026-08-23 지시). 목록 앞에 모이고 pin 버튼이 채워진
        // 글리프로 바뀐다.
        bool pinned { false };
    };

    // 열린 문서가 없을 때 카드 목록 자리를 채우는 시작 페이지다 (VSCode 시작
    // 페이지 모방, app-shell-design A1.3). 값이 있으면 빈 상태 문구 대신 그린다.
    struct start_page_view
    {
        std::vector<recent_document_view> recents {};
        // 앱 설정 파일을 아직 읽는 중이다. 목록 자리에 안내 문구를 그린다.
        bool loading { false };
    };

    // 탐색 dialog의 후보 한 행이다. 제외 사유가 있는 후보는 체크할 수 없고 표시만
    // 된다 (stage-8-plan 5.2).
    struct discovery_row_view
    {
        discovery_candidate candidate {};
        bool checked { false };
    };

    // 탐색 후보 선택 등록 dialog의 불변 표시 모델이다 (REQ-004, stage-8-plan 5.2).
    // 행 순서는 탐색 결과의 결정적 정렬 그대로다. message와 can_confirm은 logic이
    // 계산해 UI는 그대로 그린다.
    struct discovery_dialog_view
    {
        std::u8string scan_root {};
        // 탐색이 아직 진행 중이다.
        bool loading { true };
        // 등록 실행이 제출되어 결과를 기다리는 중이다.
        bool executing { false };
        std::vector<discovery_row_view> rows {};
        // 검증·실패·안내 메시지다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        bool can_confirm { false };
        // 후보 목록의 스크롤 위치다 (논리 픽셀, 이미 고정됨).
        float scroll_offset { 0.0f };
    };

    // 환경설정 dialog의 불변 표시 모델이다 (REQ-017, stage-8-plan 5.1). 경로가
    // 비어 있으면 자동 탐색을 뜻하고 element가 안내 문구를 대신 그린다. message와
    // can_confirm은 logic이 초안 검증에서 계산해 UI는 그대로 그린다.
    // 환경설정 dialog의 섹션 묶음이다 (settings-tabs-and-appearance-scope-design
    // S1.2). 탭 하나가 섹션 여러 개를, 섹션 하나가 항목 여러 개를 담는다.
    enum class settings_tab
    {
        tools,
        operations,
        appearance,
        system,
    };

    // element의 정체성과 test가 쓰는 이름이다.
    [[nodiscard]] constexpr std::u8string_view settings_tab_name(const settings_tab tab) noexcept
    {
        switch (tab)
        {
        case settings_tab::operations:
            return u8"operations";
        case settings_tab::appearance:
            return u8"appearance";
        case settings_tab::system:
            return u8"system";
        case settings_tab::tools:
        default:
            return u8"tools";
        }
    }

    struct settings_dialog_view
    {
        // 지금 보고 있는 탭이다 (S1.2). 초안과 무관하므로 탭을 옮겨도 다른 탭에서
        // 고친 값은 그대로 남는다.
        settings_tab active_tab { settings_tab::tools };
        // true면 문서 override 편집(제목 "환경설정 (문서)"), false면 전역 설정
        // 편집이다 (global-settings-and-ui-fixes-design G3.2).
        bool document_mode { false };
        // 문서 모드에서 그 행이 문서에 정의되지 않아 앱 설정을 따른다는 표시다.
        // 전역 모드에서는 항상 false다.
        bool git_follows_app { false };
        bool svn_follows_app { false };
        bool timeout_follows_app { false };
        bool submodules_follows_app { false };
        bool ignore_local_follows_app { false };
        bool log_files_follows_app { false };
        // 외양도 문서가 덮어쓸 수 있다 (settings-tabs-and-appearance-scope-design
        // S2.3). 초안이 없어 문서의 정의 여부를 그대로 싣는다.
        bool theme_follows_app { false };
        bool accent_follows_app { false };
        // 글꼴도 문서가 덮어쓸 수 있다 (2026-08-23 지시).
        bool ui_font_follows_app { false };
        bool code_font_follows_app { false };
        std::u8string git_path {};
        std::u8string svn_path {};
        // 상태 확인 제한 시간 텍스트 박스의 초안이다 (field-feedback-design 1.3).
        // 숫자만 담기며, 비어 있으면 기본값(600초)을 뜻해 element가 안내 문구를
        // 대신 그린다.
        text_input_view timeout {};
        // 글꼴 초안이다 (fonts-and-in-app-dialogs-design F1, 2026-08-23 지시).
        // 비어 있으면 내장 글꼴이라 항목이 `(기본 글꼴)`을 대신 그린다. 테마·키
        // 컬러와 같이 `저장`에서 반영된다.
        std::u8string ui_font_family {};
        std::u8string code_font_family {};
        // 업데이트 시 submodule 갱신 여부의 초안이다 (확인 overlay 대체).
        bool update_submodules { false };
        // 로컬 변경을 상관하지 않음 여부의 초안이다. status 순회를 건너뛴다.
        bool ignore_local_changes { false };
        // 카드 로그를 문서 폴더에 파일로 남길지의 초안이다 (app-shell-design A4.5).
        bool write_log_files { true };
        // 테마와 키 컬러의 현재 유효 값이다 (S2.3). 클릭 즉시 반영·저장되므로
        // `저장`/`취소` 초안에 들어가지 않으며, 문서가 열려 있으면 문서 override를
        // 편집한다. 고를 수 있는 색 목록은 표시 계층이 직접 안다
        // (`accent_catalog`).
        theme_preference theme { theme_preference::system };
        std::u8string accent_id {};
        // 검증 오류다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        bool can_confirm { true };
        // 항목 열의 스크롤 위치다 (논리 픽셀, 이미 고정됨). 창이 낮아 항목이 다
        // 들어가지 않을 때만 0이 아니다 (E1).
        float scroll_offset { 0.0f };
    };

    // logic thread가 게시하고 UI thread가 그대로 그리는 불변 snapshot이다 (ADR-004).
    // 렌더러와 input thread가 같은 layout을 계산할 수 있도록 창 크기와 스크롤 값을
    // 함께 담는다.
    struct view_snapshot
    {
        // 열린 문서의 절대 경로다. Windows 원형(`\` 구분자)이라 셸 실행·파일
        // dialog의 초기 폴더가 그대로 쓸 수 있다.
        std::u8string document_path {};
        // 배너에 그리는 문서 주소다. 구분자를 `/`로 통일한 표시 전용 문자열이며
        // (theme-and-banner-menu-design T2), 카드의 `path`와 같은 규칙이다.
        std::u8string document_display_path {};
        std::vector<card_view_model> cards {};
        std::optional<project_id> selected {};
        std::u8string filter_text {};
        view_empty_state empty_state { view_empty_state::no_document };
        // 문서 수준 진단의 한국어 요약이다.
        std::vector<notice_view_model> notices {};
        float window_width { 0.0f };
        float window_height { 0.0f };
        float scale { 1.0f };
        float scroll_offset { 0.0f };
        // 카드 경로가 문서 기준 상대 경로로 표시되고 있다. toolbar 토글의 표시
        // 상태이며, 카드의 `path`에는 이미 적용된 문자열이 들어 있다.
        bool relative_paths { false };
        // 앱 설정의 테마·키 컬러다 (theme-and-banner-menu-design T3.2). logic은
        // 팔레트를 만들지 않는다 — UI thread가 고대비·OS 설정과 함께 해석한다.
        appearance_settings appearance {};
        // UI·코드 글꼴이다 (fonts-and-in-app-dialogs-design F1). UI thread가 이 값으로
        // 글꼴 registry를 갱신한다. 앱 단위라 문서 override가 없다.
        font_settings fonts {};
        // 문서에서 읽은 창 배치다. UI thread는 revision이 바뀔 때 한 번만 적용한다.
        // snapshot마다 창을 다시 배치하지 않도록 revision 비교가 경계를 만든다.
        std::optional<window_placement> window_placement_request {};
        std::uint64_t window_placement_revision { 0 };
        // `.version-list` 생성이 진행 중이다. toolbar 생성 버튼을 비활성화한다.
        bool document_generating { false };
        // 선택 카드가 있으면 그 카드의 로그 뷰다. 없으면 하단 pane을 그리지 않는다.
        std::optional<log_view_model> log {};
        // 값이 있으면 로그 pane 안 맨 위에 기능 줄이 서고 로그 본문이 그만큼 준다 (function-buttons-design 2.4).
        // 로그 pane이 있을 때만 값이 있다.
        std::optional<function_bar_view> function_bar {};
        // 배치 변경으로 초점을 가진 줄 버튼이 사라졌을 때의 초점 옮기기다 (2.2.5).
        std::optional<function_focus_request> function_focus {};
        // 값이 있으면 switch dialog가 화면을 덮는다.
        std::optional<switch_dialog_view> switch_dialog {};
        // 값이 있으면 글꼴 선택 dialog가 환경설정 위에 뜬다 (F2.5).
        std::optional<font_dialog_view> font_dialog {};
        // 값이 있으면 새 문서 만들기 dialog가 화면을 덮는다 (F3).
        std::optional<generate_document_dialog_view> generate_dialog {};
        // 값이 있으면 환경설정 dialog가 화면을 덮는다.
        std::optional<settings_dialog_view> settings_dialog {};
        // 값이 있으면 로컬 변경 확인 dialog가 화면을 덮는다 (2.3).
        std::optional<local_changes_dialog_view> local_changes_dialog {};
        // 열린 문서가 없을 때의 시작 페이지다 (A1.3). 값이 있으면 카드 목록 자리를
        // 대신 채운다.
        std::optional<start_page_view> start_page {};
        // 값이 있으면 기능 편집 dialog가 화면을 덮는다 (function-buttons-design 2.7).
        std::optional<function_editor_view> function_editor {};
        // 값이 있으면 기능 실행 확인 dialog가 화면을 덮는다 (function-buttons-design 3.10).
        std::optional<function_confirm_dialog_view> function_confirm_dialog {};
        // 값이 있으면 알림 다이얼로그가 다른 dialog 위에 떠 있다 (A3.2).
        std::optional<notice_dialog_view> notice_dialog {};
        // 값이 있으면 카드 컨텍스트 메뉴가 앵커 좌표에 떠 있다 (3장).
        std::optional<context_menu_view> context_menu {};
        // 값이 있으면 탐색 후보 선택 등록 dialog가 화면을 덮는다.
        std::optional<discovery_dialog_view> discovery_dialog {};
        // 출력 수집 기능이 실행 중이거나 저장하지 못한 변경이 있어 창을 닫기 전에 확인을 받아야 한다 (function-buttons-design
        // 4.10, D28). driver가 이 값일 때만 frame의 닫힘 훅을 건다. 분리 실행만 있으면 묻지 않는다.
        bool exit_needs_confirmation { false };
        bool shutting_down { false };
    };
} // namespace gitman

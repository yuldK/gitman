#pragma once

#include "domain/function_button.h"
#include "domain/project.h"

#include <span>
#include <string>
#include <string_view>

namespace gitman {
    // 기능 버튼 신뢰 확인의 해시다 (function-buttons-design 3.10, D4-A). 앱 설정에 문서 경로와
    // 함께 남기고, 기능을 누를 때 지금 정의로 다시 만들어 비교한다.
    //
    // 해시 입력은 두 가지다.
    //  - `function_buttons_definition::source_digest_input`: 파서가 문서를 **읽을 때** 파일 원문의
    //    `function_buttons`에서 버튼마다 `placement`를 빼고(화면에 `card`로 보이는 항목만 `"card"`로 다시 적고) 키를
    //    정렬해 다시 직렬화한 글이다 (T3, D29, D31). 그래서 `bar`·`menu` 사이 재배치는 해시를 바꾸지 않고, `card`에 넣거나
    //    빼서 `Alt+N`이 가리키는 기능이 바뀌는 것과 이름·아이콘 변경, 실패 항목의 고침은 바꾼다.
    //  - 문서의 project마다 `{id, path.original, ${projectName} 값}`: `${projectRoot}`가 가리키는 곳이나
    //    `${projectName}`(`GITMAN_PROJECT_NAME`)이 바뀌면 다시 묻는다. 둘 다 프로그램에 들어가는 실행
    //    입력이다. id 순으로 정렬해 카드 순서 변경과 표시 토글은 해시를 바꾸지 않는다.
    //  - 신뢰 판정은 기능을 누를 때와 확인 dialog에서 확인할 때 두 번, 그때의 문서 모델로 이 해시를
    //    만든다 (`logic_controller::current_function_digest`).
    // `source_digest_input`은 읽을 때의 값이라 gitman이 모델을 고쳐 저장한 뒤에는 낡는다. 그래서 저장이 성공하면
    // store가 쓴 글을 다시 읽은 값(`project_store_save_result::function_digest_input`)으로 바꾼다 (T12).

    // 해시 입력을 하나의 글로 잇는다. 구간마다 byte 길이를 앞에 붙여 경계가 모호하지 않다.
    // test와 해시가 같은 글을 본다.
    [[nodiscard]] std::u8string function_trust_digest_input(const function_buttons_definition& definition, std::span<const project_definition> projects);
    // 위 글의 SHA-256을 소문자 16진수 64자로 돌려준다.
    [[nodiscard]] std::u8string function_trust_digest(const function_buttons_definition& definition, std::span<const project_definition> projects);

    // 문서가 지정한 VCS 실행 파일(`settings.git_executable`·`svn_executable` 덮어쓰기)의 신뢰 해시다 (3.10, 5차 검수
    // S1). 기능 버튼 신뢰 해시와 따로 둔다. 그 해시에는 project와 기능 정의가 들어 있어 저장소 등록이나 기능 손 편집만으로
    // 실행 파일 신뢰가 풀리고, 환경설정에서 사용자가 고른 경로가 기능 신뢰(D24-A)에 묶이기 때문이다. 입력은 두 값이며 비어
    // 있거나 정의하지 않은 값은 빈 글이다. 어느 하나를 바꿔도 다시 묻는다.
    [[nodiscard]] std::u8string vcs_executables_trust_digest(std::u8string_view git_executable, std::u8string_view svn_executable);

    // SHA-256(FIPS 180-4)이다. 바뀜을 알아보는 데만 쓰고 비밀을 다루지 않는다. 결과는 소문자
    // 16진수 64자다. `src`에 해시 도우미가 없어 domain 순수 함수로 둔다 (T7).
    [[nodiscard]] std::u8string sha256_hex(std::u8string_view bytes);
}

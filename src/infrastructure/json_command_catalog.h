#pragma once

#include "domain/command_definition.h"
#include "domain/diagnostic.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // `.version-list` 문서와 전역 앱 설정이 함께 쓰는 `commands` 구역의 codec이다
    // (custom-commands-and-svn-hooks-design C2). 두 저장소가 같은 모양을 읽고 쓰므로
    // 규칙을 한 곳에 둔다.
    //
    // `nlohmann::ordered_json`으로 고정한 것은 쓰는 쪽(project_store·app_settings_store)이
    // 그 타입을 쓰기 때문이다. 문서 파서는 `nlohmann::json`이라 이 구역만 변환해
    // 넘긴다 — 사본 하나가 비싸지 않고, 대신 파싱 규칙이 두 벌이 되지 않는다.
    struct command_catalog_parse_result
    {
        command_catalog_overrides overrides {};
        std::vector<diagnostic> diagnostics {};
    };

    // `commands` object 하나를 읽는다. `source`는 그 object 자체이며 object가 아니면
    // 진단 하나를 남기고 빈 결과를 돌려준다.
    //
    // 진단의 심각도는 부르는 쪽이 정한다. 문서의 명령은 실행에 영향을 주므로
    // `settings`처럼 오류이고, 전역 앱 설정은 어떤 값도 시작을 막지 않는다는 기존
    // 규칙에 따라 경고다.
    [[nodiscard]] command_catalog_parse_result parse_command_catalog(
        const nlohmann::ordered_json& source, std::u8string_view document_path, std::u8string_view pointer_prefix, diagnostic_severity severity);

    // 정의된 항목만 쓰고 정의되지 않은 키는 지운다 (문서 override 규칙). 쓸 것이
    // 하나도 없으면 false를 돌려주어 부르는 쪽이 `commands` object 자체를 지운다.
    bool write_command_catalog_overrides(nlohmann::ordered_json& target, const command_catalog_overrides& overrides);

    // 전역 설정은 구체 값이라 늘 두 항목을 모두 쓴다.
    void write_command_catalog(nlohmann::ordered_json& target, const command_catalog& catalog);
} // namespace gitman

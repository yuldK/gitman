#pragma once

#include "domain/project.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    inline constexpr std::int32_t current_app_settings_schema_version { 1 };
    // 시작 페이지가 보여 주는 최근 항목의 상한이다 (app-shell-design A1.1). 넘으면
    // 오래된 항목부터 버린다.
    inline constexpr std::size_t recent_document_capacity { 10 };
    // 앱 설정 파일의 이름이다. 실행 파일과 같은 폴더에 둔다.
    inline constexpr std::u8string_view app_settings_file_name { u8"gitman.app-settings.json" };

    // 최근에 연 `.version-list` 문서 하나다. 시각은 도메인이 시계를 모르도록 이미
    // 만들어진 ISO 8601 UTC 문자열로 받는다.
    struct recent_document
    {
        // 절대 경로다. 구분자는 `\`로 통일해 저장한다.
        std::u8string path {};
        // 확장자를 뗀 표시 이름이다. 저장 시점에 계산한다.
        std::u8string display_name {};
        // `2026-08-21T18:40:12Z` 형식이다. 읽을 수 없는 값은 파서가 버린다.
        std::u8string opened_at {};

        [[nodiscard]] bool operator==(const recent_document&) const noexcept = default;
    };

    // 문서 밖에 남는 앱 단위 설정이다 (app-shell-design A1). 실행 파일과 같은 폴더의
    // JSON 파일 하나에 저장하며, 문서 `settings`와 달리 열린 문서와 무관하다.
    struct app_settings
    {
        std::int32_t schema_version { current_app_settings_schema_version };
        // 최근 순으로 정렬된 목록이다. 맨 앞이 가장 최근에 연 문서다.
        std::vector<recent_document> recent_documents {};
        // 마지막으로 앱을 닫을 때의 창 배치다 (global-settings-and-ui-fixes-design
        // G1). 문서의 `window`와 같은 좌표 규칙이며, 문서 없이 시작할 때의 복원
        // fallback이다 (적용 우선순위: 여는 문서의 배치 > 이 값 > 기본값).
        std::optional<window_placement> window {};

        [[nodiscard]] bool operator==(const app_settings&) const noexcept = default;
    };

    // 같은 문서인지 판정하는 열쇠다. `/`를 `\`로 바꾸고 끝 구분자를 지운 뒤 ASCII
    // 대문자를 소문자로 내린다. Windows 파일 이름은 대소문자를 구분하지 않는다.
    [[nodiscard]] std::u8string recent_document_key(std::u8string_view path);
    // 표시 이름이다. 경로의 마지막 구성 요소에서 `.version-list` 확장자를 뗀다.
    [[nodiscard]] std::u8string recent_document_display_name(std::u8string_view path);
    // 문서를 목록 맨 앞으로 올린다. 같은 문서가 있으면 경로 표기와 시각을 갱신하고,
    // 상한을 넘으면 뒤에서 버린다.
    void touch_recent_document(app_settings& settings, std::u8string path, std::u8string opened_at);
    // 목록에서 문서를 지운다. 지운 항목이 있으면 true다.
    bool remove_recent_document(app_settings& settings, std::u8string_view path);
    // `2026-08-21T18:40:12Z` 형식의 UTC 문자열이다. 변환에 실패하면 빈 값이다.
    [[nodiscard]] std::u8string format_utc_timestamp(std::chrono::system_clock::time_point time);
} // namespace gitman

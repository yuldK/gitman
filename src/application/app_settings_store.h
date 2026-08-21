#pragma once

#include "domain/app_settings.h"
#include "domain/diagnostic.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    struct app_settings_load_result
    {
        app_settings settings {};
        // 읽은 파일의 원본 JSON이다. 저장할 때 이 위에 값을 덮어써 알 수 없는 키를
        // 보존한다. 파일이 없거나 깨졌으면 비어 있다.
        std::u8string shadow_source_json {};
        std::vector<diagnostic> diagnostics {};
    };

    struct app_settings_save_result
    {
        bool succeeded { false };
        // 저장에 성공하면 다음 저장의 기준이 되는 새 원본 JSON이다.
        std::u8string shadow_source_json {};
        std::vector<diagnostic> diagnostics {};
    };

    // 앱 단위 설정 파일의 읽기·쓰기 경계다 (app-shell-design A1.2). 문서 store와 달리
    // 낙관적 잠금을 두지 않는다. 파일은 사용자 한 명의 앱이 드물게 쓰는 작은 값이라
    // 마지막 저장이 이긴다.
    class app_settings_store
    {
    public:
        app_settings_store() = default;
        app_settings_store(const app_settings_store&) = delete;
        app_settings_store(app_settings_store&&) = delete;
        app_settings_store& operator=(const app_settings_store&) = delete;
        app_settings_store& operator=(app_settings_store&&) = delete;
        virtual ~app_settings_store() = default;

        // 파일이 없으면 기본값과 진단 없이 돌아온다. 앱 시작을 막지 않는다.
        [[nodiscard]] virtual app_settings_load_result load(std::u8string_view path) noexcept = 0;
        [[nodiscard]] virtual app_settings_save_result save(std::u8string_view path, const app_settings& settings, std::u8string_view shadow_source_json) noexcept = 0;
    };
} // namespace gitman

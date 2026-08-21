#pragma once

#include "application/app_settings_store.h"
#include "application/workspace_document_file_system.h"

#include <string>
#include <string_view>

namespace gitman {
    // 원본 JSON 위에 값만 덮어써 알 수 없는 키를 보존한다. shadow가 비어 있으면 새
    // object에서 시작한다. test가 직접 부를 수 있도록 store와 분리해 둔다.
    [[nodiscard]] std::u8string serialize_app_settings_json(const app_settings& settings, std::u8string_view shadow_source_json);
    // 형식 오류는 진단으로 알리고 읽을 수 있는 항목만 남긴다. 파일 전체가 깨졌으면
    // 기본값을 돌려준다.
    [[nodiscard]] app_settings_load_result parse_app_settings_json(std::u8string_view source_json, std::u8string_view path);

    class json_app_settings_store final : public app_settings_store
    {
    public:
        explicit json_app_settings_store(workspace_document_file_system& file_system) noexcept;

        [[nodiscard]] app_settings_load_result load(std::u8string_view path) noexcept override;
        [[nodiscard]] app_settings_save_result save(std::u8string_view path, const app_settings& settings, std::u8string_view shadow_source_json) noexcept override;

    private:
        workspace_document_file_system* file_system_ { nullptr };
    };
} // namespace gitman

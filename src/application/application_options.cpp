#include "application/application_options.h"

#include "domain/project.h"

namespace gitman {
    namespace {
        constexpr std::u8string_view renderer_prefix { u8"--renderer=" };

        constexpr char8_t ascii_lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value + (u8'a' - u8'A'));
            return value;
        }

    } // namespace

    bool has_workspace_document_extension(const std::u8string_view path) noexcept
    {
        if (path.size() < workspace_document_extension.size())
            return false;

        const std::size_t extension_offset { path.size() - workspace_document_extension.size() };
        for (std::size_t index = 0; index < workspace_document_extension.size(); ++index)
            if (ascii_lower(path[extension_offset + index]) != workspace_document_extension[index])
                return false;
        return true;
    }

    application_options_result parse_application_options(const std::span<const std::u8string> arguments)
    {
        application_options options {};
        bool renderer_was_set { false };

        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            const std::u8string_view argument { arguments[index] };
            if (argument.starts_with(renderer_prefix))
            {
                if (renderer_was_set)
                    return { std::nullopt, u8"renderer 설정은 한 번만 지정할 수 있습니다." };
                const auto renderer {
                    parse_renderer_mode(argument.substr(renderer_prefix.size())),
                };

                if (renderer.has_value() == false)
                    return { std::nullopt, u8"renderer는 auto, direct3d, cpu 중 하나여야 합니다." };
                options.renderer = *renderer;
                renderer_was_set = true;
                continue;
            }

            if (argument == u8"--smoke-test")
            {
                options.smoke_test = true;
                continue;
            }

            if (argument == u8"--simulate-direct3d-failure")
            {
                options.simulate_direct3d_failure = true;
                continue;
            }

            if (argument == u8"--register-file-association")
            {
                options.register_file_association = true;
                continue;
            }

            if (argument == u8"--unregister-file-association")
            {
                options.unregister_file_association = true;
                continue;
            }

            if (argument.starts_with(u8"--"))
                return { std::nullopt, u8"알 수 없는 명령줄 인자가 있습니다: " + arguments[index] };
            if (options.workspace_document_path.has_value())
                return { std::nullopt, u8"작업공간 문서 경로는 하나만 지정할 수 있습니다." };
            if (has_workspace_document_extension(argument) == false)
            {
                return {
                    std::nullopt,
                    u8"작업공간 문서는 .version-list 확장자여야 합니다: " + arguments[index],
                };
            }
            options.workspace_document_path = arguments[index];
        }

        if (options.simulate_direct3d_failure && options.smoke_test == false)
        {
            return {
                std::nullopt,
                u8"--simulate-direct3d-failure는 smoke test에서만 사용할 수 있습니다.",
            };
        }
        if (options.register_file_association && options.unregister_file_association)
        {
            return {
                std::nullopt,
                u8"연결 등록과 제거는 함께 지정할 수 없습니다.",
            };
        }
        return { options, {} };
    }
} // namespace gitman

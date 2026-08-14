#include "application/application_options.h"

namespace gitman {
    namespace {
        constexpr std::u8string_view renderer_prefix { u8"--renderer=" };
    } // namespace

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
            return { std::nullopt, u8"알 수 없는 명령줄 인자가 있습니다: " + arguments[index] };
        }

        if (options.simulate_direct3d_failure && options.smoke_test == false)
        {
            return {
                std::nullopt,
                u8"--simulate-direct3d-failure는 smoke test에서만 사용할 수 있습니다.",
            };
        }
        return { options, {} };
    }
} // namespace gitman

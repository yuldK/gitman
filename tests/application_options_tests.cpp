#include "application/application_options.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("Command-line renderer defaults to auto", "[options]")
{
    const std::vector<std::u8string> arguments { u8"gitman.exe" };
    const auto result = gitman::parse_application_options(arguments);
    REQUIRE(result.options.has_value());
    REQUIRE(result.options->renderer == gitman::renderer_mode::automatic);
    REQUIRE_FALSE(result.options->smoke_test);
}

TEST_CASE("CPU smoke-test options are parsed", "[options]")
{
    const std::vector<std::u8string> arguments {
        u8"gitman.exe",
        u8"--renderer=cpu",
        u8"--smoke-test",
    };
    const auto result = gitman::parse_application_options(arguments);
    REQUIRE(result.options.has_value());
    REQUIRE(result.options->renderer == gitman::renderer_mode::cpu);
    REQUIRE(result.options->smoke_test);
}

TEST_CASE("Failure injection is rejected outside smoke tests", "[options]")
{
    const std::vector<std::u8string> arguments {
        u8"gitman.exe",
        u8"--simulate-direct3d-failure",
    };
    REQUIRE_FALSE(gitman::parse_application_options(arguments).options.has_value());
}

TEST_CASE("Duplicate or unknown renderer options are rejected", "[options]")
{
    const std::vector<std::u8string> duplicate {
        u8"gitman.exe",
        u8"--renderer=cpu",
        u8"--renderer=auto",
    };
    REQUIRE_FALSE(gitman::parse_application_options(duplicate).options.has_value());

    const std::vector<std::u8string> unknown { u8"gitman.exe", u8"--renderer=opengl" };
    REQUIRE_FALSE(gitman::parse_application_options(unknown).options.has_value());
}

TEST_CASE("File association flags are parsed and mutually exclusive", "[options][association]")
{
    const std::vector<std::u8string> register_arguments { u8"gitman.exe", u8"--register-file-association" };
    const auto registered { gitman::parse_application_options(register_arguments) };
    REQUIRE(registered.options.has_value());
    REQUIRE(registered.options->register_file_association);
    REQUIRE_FALSE(registered.options->unregister_file_association);

    const std::vector<std::u8string> unregister_arguments { u8"gitman.exe", u8"--unregister-file-association" };
    const auto unregistered { gitman::parse_application_options(unregister_arguments) };
    REQUIRE(unregistered.options.has_value());
    REQUIRE(unregistered.options->unregister_file_association);

    // 등록과 제거를 함께 지정하면 의도가 모호하므로 거부한다.
    const std::vector<std::u8string> both { u8"gitman.exe", u8"--register-file-association", u8"--unregister-file-association" };
    REQUIRE_FALSE(gitman::parse_application_options(both).options.has_value());
}

TEST_CASE("Workspace launch path is optional", "[options][workspace]")
{
    const std::vector<std::u8string> arguments {
        u8"gitman.exe",
        u8"--renderer=cpu",
    };
    const auto result { gitman::parse_application_options(arguments) };
    REQUIRE(result.options.has_value());
    REQUIRE_FALSE(result.options->workspace_document_path.has_value());
}

TEST_CASE("Workspace launch paths preserve Unicode and option ordering", "[options][workspace]")
{
    constexpr std::u8string_view document_path { u8"E:/한글 작업 공간/활성 목록.version-list" };

    const std::vector<std::u8string> path_first {
        u8"gitman.exe",
        std::u8string { document_path },
        u8"--renderer=cpu",
    };
    const auto path_first_result { gitman::parse_application_options(path_first) };
    REQUIRE(path_first_result.options.has_value());
    REQUIRE(path_first_result.options->workspace_document_path.has_value());
    REQUIRE(path_first_result.options->workspace_document_path->compare(document_path) == 0);
    REQUIRE(path_first_result.options->renderer == gitman::renderer_mode::cpu);

    constexpr std::u8string_view uppercase_path { u8"E:/work/ACTIVE.version-list" };
    const std::vector<std::u8string> options_first {
        u8"gitman.exe",
        u8"--renderer=direct3d",
        std::u8string { uppercase_path },
    };
    const auto options_first_result { gitman::parse_application_options(options_first) };
    REQUIRE(options_first_result.options.has_value());
    REQUIRE(options_first_result.options->workspace_document_path.has_value());
    REQUIRE(options_first_result.options->workspace_document_path->compare(uppercase_path) == 0);
    REQUIRE(options_first_result.options->renderer == gitman::renderer_mode::direct3d);
}

TEST_CASE("Duplicate workspace launch paths are rejected", "[options][workspace]")
{
    const std::vector<std::u8string> arguments {
        u8"gitman.exe",
        u8"E:/work/first.version-list",
        u8"--renderer=cpu",
        u8"E:/work/second.version-list",
    };
    REQUIRE_FALSE(gitman::parse_application_options(arguments).options.has_value());
}

TEST_CASE("Invalid workspace launch extensions are rejected", "[options][workspace]")
{
    constexpr std::array invalid_paths {
        std::u8string_view { u8"E:/work/workspace.json" },
        std::u8string_view { u8"E:/work/workspace.version-list.bak" },
        std::u8string_view { u8"E:/work/workspace.version-list " },
    };

    for (const std::u8string_view invalid_path : invalid_paths)
    {
        const std::vector<std::u8string> arguments {
            u8"gitman.exe",
            std::u8string { invalid_path },
        };
        REQUIRE_FALSE(gitman::parse_application_options(arguments).options.has_value());
    }
}

TEST_CASE("Workspace extension matching is shared by launch and file drop", "[options][workspace]")
{
    REQUIRE(gitman::has_workspace_document_extension(u8"E:/work/project.version-list"));
    REQUIRE(gitman::has_workspace_document_extension(u8"E:/work/project.VERSION-LIST"));
    REQUIRE_FALSE(gitman::has_workspace_document_extension(u8"E:/work/project.version-list.bak"));
    REQUIRE_FALSE(gitman::has_workspace_document_extension(u8"version-list"));
}

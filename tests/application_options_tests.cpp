#include "application/application_options.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
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

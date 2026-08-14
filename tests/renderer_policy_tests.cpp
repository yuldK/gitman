#include "presentation/renderer_policy.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Renderer strings map to fixed modes", "[renderer]")
{
    REQUIRE(gitman::parse_renderer_mode(u8"auto") == gitman::renderer_mode::automatic);
    REQUIRE(gitman::parse_renderer_mode(u8"direct3d") == gitman::renderer_mode::direct3d);
    REQUIRE(gitman::parse_renderer_mode(u8"cpu") == gitman::renderer_mode::cpu);
    REQUIRE_FALSE(gitman::parse_renderer_mode(u8"vulkan").has_value());
}

TEST_CASE("Auto renderer tries Direct3D before CPU fallback", "[renderer]")
{
    const auto gpu = gitman::select_renderer_backend(gitman::renderer_mode::automatic, true);
    REQUIRE(gpu.status == gitman::renderer_selection_status::selected);
    REQUIRE(gpu.backend == gitman::renderer_backend::direct3d);
    REQUIRE_FALSE(gpu.used_fallback);

    const auto fallback = gitman::select_renderer_backend(gitman::renderer_mode::automatic, false);
    REQUIRE(fallback.status == gitman::renderer_selection_status::selected);
    REQUIRE(fallback.backend == gitman::renderer_backend::cpu);
    REQUIRE(fallback.used_fallback);
}

TEST_CASE("Explicit renderer selection preserves fallback policy", "[renderer]")
{
    const auto cpu = gitman::select_renderer_backend(gitman::renderer_mode::cpu, true);
    REQUIRE(cpu.status == gitman::renderer_selection_status::selected);
    REQUIRE(cpu.backend == gitman::renderer_backend::cpu);
    REQUIRE_FALSE(cpu.used_fallback);

    const auto unavailable = gitman::select_renderer_backend(gitman::renderer_mode::direct3d, false);
    REQUIRE(unavailable.status == gitman::renderer_selection_status::unavailable);
    REQUIRE(unavailable.backend == gitman::renderer_backend::direct3d);
    REQUIRE_FALSE(unavailable.used_fallback);
}

#include "include/core/SkMilestone.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_version_macros.hpp>
#include <nlohmann/json.hpp>

static_assert(SK_MILESTONE == 148);
static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3);
static_assert(NLOHMANN_JSON_VERSION_MINOR == 12);
static_assert(NLOHMANN_JSON_VERSION_PATCH == 0);
static_assert(CATCH_VERSION_MAJOR == 3);
static_assert(CATCH_VERSION_MINOR == 15);
static_assert(CATCH_VERSION_PATCH == 3);

TEST_CASE("Build uses pinned dependency versions", "[dependencies]")
{
    const nlohmann::json version = {
        { "skia", SK_MILESTONE },
        { "nlohmann_json", NLOHMANN_JSON_VERSION_MAJOR },
        { "catch2", CATCH_VERSION_MAJOR },
    };
    REQUIRE(version.at("skia") == 148);
}

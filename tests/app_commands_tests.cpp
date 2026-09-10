#include "application/app_commands.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace {
    // luil이 나르는 값은 `std::uint32_t` 하나다. 앱 명령 전부가 그 값으로 왕복해야
    // delegate가 명령을 되찾을 수 있다 (계약 3장 app_ui_command).
    constexpr std::array<gitman::app_command, 12> all_commands {
        gitman::app_command::show_open_document_dialog,
        gitman::app_command::show_generate_scan_folder_picker,
        gitman::app_command::show_generate_location_picker,
        gitman::app_command::show_discovery_folder_picker,
        gitman::app_command::show_git_executable_picker,
        gitman::app_command::show_svn_executable_picker,
        gitman::app_command::register_file_association,
        gitman::app_command::unregister_file_association,
        gitman::app_command::open_in_vscode,
        gitman::app_command::open_in_explorer_select,
        gitman::app_command::open_in_explorer_folder,
        gitman::app_command::show_command_script_picker,
    };
} // namespace

TEST_CASE("Every app command survives the round trip through a raw value", "[app][commands]")
{
    for (const gitman::app_command command : all_commands)
    {
        const std::uint32_t raw { static_cast<std::uint32_t>(command) };
        const std::optional<gitman::app_command> parsed { gitman::parse_app_command(raw) };
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == command);
    }

    // 값은 1부터 빈틈없이 이어지고 마지막이 `last_app_command`다.
    REQUIRE(static_cast<std::uint32_t>(all_commands.front()) == 1u);
    REQUIRE(static_cast<std::uint32_t>(all_commands.back()) == gitman::last_app_command);
    REQUIRE(all_commands.size() == static_cast<std::size_t>(gitman::last_app_command));
}

TEST_CASE("Zero and out of range values are not app commands", "[app][commands]")
{
    // 0은 "명령 없음"이라 비워 둔 자리다.
    REQUIRE(gitman::parse_app_command(0) == std::nullopt);
    REQUIRE(gitman::parse_app_command(gitman::last_app_command + 1) == std::nullopt);
    REQUIRE(gitman::parse_app_command(9999) == std::nullopt);

    // 경계 안쪽은 모두 값이 있다.
    for (std::uint32_t value = 1; value <= gitman::last_app_command; ++value)
        REQUIRE(gitman::parse_app_command(value).has_value());
}

TEST_CASE("App command parsing is usable in constant expressions", "[app][commands]")
{
    // delegate의 switch가 constexpr 값을 쓰므로 컴파일 시각에도 풀려야 한다.
    static_assert(gitman::parse_app_command(0).has_value() == false);
    static_assert(gitman::parse_app_command(1) == gitman::app_command::show_open_document_dialog);
    static_assert(gitman::parse_app_command(gitman::last_app_command) == gitman::app_command::show_command_script_picker);
    SUCCEED();
}

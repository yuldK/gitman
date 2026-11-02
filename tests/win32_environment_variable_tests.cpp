#include "platform/win32/win32_vcs_file_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // test가 중간에 실패해도 프로세스 환경을 처음 상태로 되돌린다. 같은 프로세스에서 도는
    // 다른 test가 남은 값을 보지 않게 하려는 것이다.
    class environment_variable_restorer
    {
    public:
        explicit environment_variable_restorer(const wchar_t* name)
            : name_ { name }
        {
            SetLastError(ERROR_SUCCESS);
            const DWORD length { GetEnvironmentVariableW(name_, nullptr, 0) };
            if (length == 0)
                return;
            std::vector<wchar_t> buffer(length);
            const DWORD written { GetEnvironmentVariableW(name_, buffer.data(), length) };
            if (written < length)
                original_ = std::wstring { buffer.data(), written };
        }

        environment_variable_restorer(const environment_variable_restorer&) = delete;
        environment_variable_restorer& operator=(const environment_variable_restorer&) = delete;

        ~environment_variable_restorer()
        {
            static_cast<void>(SetEnvironmentVariableW(name_, original_.has_value() ? original_->c_str() : nullptr));
        }

    private:
        const wchar_t* name_ { nullptr };
        std::optional<std::wstring> original_ {};
    };
} // namespace

TEST_CASE("The process environment lookup separates undefined from empty variables", "[function_buttons][t6][win32]")
{
    // 운영 조립이 `${env:}`에 넘기는 읽기다. 미정의만 거부하므로 빈 값과 갈라야 한다
    // (function-buttons-design 3.3).
    const environment_variable_restorer restorer { L"GITMAN_T6_ENVIRONMENT_PROBE" };
    const std::u8string_view name { u8"GITMAN_T6_ENVIRONMENT_PROBE" };
    // 없는 변수를 지우면 실패를 돌려줄 수 있어 결과를 보지 않는다.
    static_cast<void>(SetEnvironmentVariableW(L"GITMAN_T6_ENVIRONMENT_PROBE", nullptr));
    REQUIRE_FALSE(gitman::win32::find_environment_variable(name).has_value());
    REQUIRE(gitman::win32::read_environment_variable(name).empty());

    REQUIRE(SetEnvironmentVariableW(L"GITMAN_T6_ENVIRONMENT_PROBE", L"") != FALSE);
    REQUIRE(gitman::win32::find_environment_variable(name) == std::optional<std::u8string> { std::u8string {} });

    REQUIRE(SetEnvironmentVariableW(L"GITMAN_T6_ENVIRONMENT_PROBE", L"값") != FALSE);
    REQUIRE(gitman::win32::find_environment_variable(name) == std::optional<std::u8string> { std::u8string { u8"값" } });
    REQUIRE(gitman::win32::read_environment_variable(name) == u8"값");
}

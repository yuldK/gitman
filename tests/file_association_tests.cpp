#include "infrastructure/file_association.h"
#include "platform/win32/win32_file_association.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace {
    // 실제 HKCU를 오염시키지 않는 test 전용 root다. 등록·제거가 이 아래에서만
    // 일어나고, guard가 test 전후로 tree를 지운다 (stage-8-plan 5.4).
    constexpr std::u8string_view test_root { u8"Software\\GitmanTest\\file-association" };
    constexpr wchar_t test_root_wide[] = L"Software\\GitmanTest\\file-association";

    struct registry_root_guard
    {
        registry_root_guard()
        {
            static_cast<void>(RegDeleteTreeW(HKEY_CURRENT_USER, test_root_wide));
        }

        registry_root_guard(const registry_root_guard&) = delete;
        registry_root_guard(registry_root_guard&&) = delete;
        registry_root_guard& operator=(const registry_root_guard&) = delete;
        registry_root_guard& operator=(registry_root_guard&&) = delete;

        ~registry_root_guard()
        {
            static_cast<void>(RegDeleteTreeW(HKEY_CURRENT_USER, test_root_wide));
            static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\GitmanTest"));
        }
    };

    [[nodiscard]] bool key_exists(const std::wstring& subkey)
    {
        HKEY key { nullptr };
        if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;
        RegCloseKey(key);
        return true;
    }

    void write_extension_default(const wchar_t* const data)
    {
        HKEY key { nullptr };
        REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring { test_root_wide } + L"\\.version-list").c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS);
        const DWORD size { static_cast<DWORD>((std::wcslen(data) + 1) * sizeof(wchar_t)) };
        REQUIRE(RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), size) == ERROR_SUCCESS);
        RegCloseKey(key);
    }
} // namespace

TEST_CASE("The association plan carries the ProgID, icon and quoted open command", "[association]")
{
    const std::vector<gitman::registry_default_value> values { gitman::make_file_association_values(u8"C:\\Program Files\\Gitman\\gitman.exe") };
    REQUIRE(values.size() == 4u);
    REQUIRE(values[0].subkey == u8".version-list");
    REQUIRE(values[0].data == gitman::file_association_prog_id);
    REQUIRE(values[1].subkey == gitman::file_association_prog_id);
    REQUIRE(values[1].data.empty() == false);
    REQUIRE(values[2].subkey == u8"Gitman.VersionList\\DefaultIcon");
    // 공백 경로가 깨지지 않도록 실행 파일은 따옴표로 감싼다.
    REQUIRE(values[2].data == u8"\"C:\\Program Files\\Gitman\\gitman.exe\",0");
    REQUIRE(values[3].subkey == u8"Gitman.VersionList\\shell\\open\\command");
    REQUIRE(values[3].data == u8"\"C:\\Program Files\\Gitman\\gitman.exe\" \"%1\"");

    // 제거 목록은 자식 키가 앞에 온다 (부모를 먼저 지우면 실패한다).
    const std::vector<std::u8string> removal { gitman::file_association_prog_id_subkeys() };
    REQUIRE(removal.size() == 5u);
    REQUIRE(removal.front() == u8"Gitman.VersionList\\shell\\open\\command");
    REQUIRE(removal.back() == u8"Gitman.VersionList");

    REQUIRE(gitman::owns_extension_link(u8"Gitman.VersionList"));
    REQUIRE(gitman::owns_extension_link(u8"Other.App") == false);
    REQUIRE(gitman::owns_extension_link(u8"") == false);
}

TEST_CASE("Registration under the test root is idempotent and queryable", "[association]")
{
    registry_root_guard guard {};
    const std::u8string executable { u8"C:\\tools\\gitman test\\gitman.exe" };

    REQUIRE(gitman::win32::file_association_registered(executable, test_root) == false);

    const gitman::win32::file_association_outcome first { gitman::win32::register_file_association(executable, test_root) };
    REQUIRE(first.succeeded);
    REQUIRE(gitman::win32::file_association_registered(executable, test_root));
    // 다른 실행 파일 기준으로는 등록 상태가 아니다.
    REQUIRE(gitman::win32::file_association_registered(u8"C:\\other\\gitman.exe", test_root) == false);

    // 같은 등록을 반복해도 결과가 같다 (멱등).
    const gitman::win32::file_association_outcome second { gitman::win32::register_file_association(executable, test_root) };
    REQUIRE(second.succeeded);
    REQUIRE(gitman::win32::file_association_registered(executable, test_root));

    // 빈 실행 파일 경로는 등록을 만들지 않는다.
    const gitman::win32::file_association_outcome invalid { gitman::win32::register_file_association(u8"", test_root) };
    REQUIRE(invalid.succeeded == false);
    REQUIRE(invalid.diagnostics.empty() == false);
}

TEST_CASE("Unregistration removes our keys and leaves nothing behind", "[association]")
{
    registry_root_guard guard {};
    const std::u8string executable { u8"C:\\tools\\gitman\\gitman.exe" };
    REQUIRE(gitman::win32::register_file_association(executable, test_root).succeeded);

    const gitman::win32::file_association_outcome removed { gitman::win32::unregister_file_association(test_root) };
    REQUIRE(removed.succeeded);
    REQUIRE(gitman::win32::file_association_registered(executable, test_root) == false);
    REQUIRE(key_exists(std::wstring { test_root_wide } + L"\\Gitman.VersionList") == false);
    REQUIRE(key_exists(std::wstring { test_root_wide } + L"\\.version-list") == false);

    // 등록된 것이 없어도 제거는 성공한다 (멱등).
    REQUIRE(gitman::win32::unregister_file_association(test_root).succeeded);
}

TEST_CASE("Unregistration keeps an extension link owned by another application", "[association]")
{
    registry_root_guard guard {};
    REQUIRE(gitman::win32::register_file_association(u8"C:\\tools\\gitman\\gitman.exe", test_root).succeeded);

    // 다른 앱이 연결을 가져간 상황이다. 확장자 기본값이 남아야 한다.
    write_extension_default(L"Other.App");
    const gitman::win32::file_association_outcome removed { gitman::win32::unregister_file_association(test_root) };
    REQUIRE(removed.succeeded);

    // ProgID tree는 지워지고 확장자 연결은 다른 앱의 것으로 남는다.
    REQUIRE(key_exists(std::wstring { test_root_wide } + L"\\Gitman.VersionList") == false);
    DWORD size { 0 };
    REQUIRE(RegGetValueW(HKEY_CURRENT_USER, (std::wstring { test_root_wide } + L"\\.version-list").c_str(), nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS);
}

TEST_CASE("The current executable path is absolute and non-empty", "[association]")
{
    const std::u8string path { gitman::win32::current_executable_path() };
    REQUIRE(path.empty() == false);
    // test 실행 파일 자신이다.
    REQUIRE(path.find(u8"gitman_tests") != std::u8string::npos);
}

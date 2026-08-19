#include "platform/win32/win32_file_association.h"

#include "infrastructure/file_association.h"
#include "platform/win32/utf8.h"

#include <shlobj.h>
#include <windows.h>

#include <optional>
#include <utility>

namespace gitman::win32 {
    namespace {
        diagnostic make_error(std::u8string message)
        {
            diagnostic value {};
            value.code = diagnostic_code::operation_failed;
            value.severity = diagnostic_severity::error;
            value.message = std::move(message);
            return value;
        }

        // root와 상대 subkey를 하나의 HKCU 경로로 합친다. 변환 실패는 값 없음이다.
        [[nodiscard]] std::optional<std::wstring> full_subkey_path(const std::u8string_view root_subkey, const std::u8string_view subkey)
        {
            std::u8string combined { root_subkey };
            if (combined.empty() == false && subkey.empty() == false)
                combined += u8"\\";
            combined += subkey;
            auto converted { utf8_to_utf16(combined) };
            if (converted.value.has_value() == false)
                return std::nullopt;
            return { std::move(*converted.value) };
        }

        // subkey의 기본값을 REG_SZ로 쓴다. 키가 없으면 만든다.
        [[nodiscard]] bool write_default_value(const std::wstring& subkey, const std::u8string_view data)
        {
            auto converted { utf8_to_utf16(data) };
            if (converted.value.has_value() == false)
                return false;

            HKEY key { nullptr };
            if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
                return false;
            const std::wstring& value { *converted.value };
            const DWORD size { static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)) };
            const LSTATUS written { RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), size) };
            RegCloseKey(key);
            return written == ERROR_SUCCESS;
        }

        // subkey의 기본값이다. 키나 값이 없으면 값 없음이다.
        [[nodiscard]] std::optional<std::u8string> read_default_value(const std::wstring& subkey)
        {
            DWORD size { 0 };
            if (RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0)
                return std::nullopt;

            std::wstring value(size / sizeof(wchar_t), L'\0');
            if (RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
                return std::nullopt;
            // RegGetValueW의 size는 종결 NUL을 포함한다. 문자열 길이에서 걷어낸다.
            while (value.empty() == false && value.back() == L'\0')
                value.pop_back();
            auto converted { utf16_to_utf8(value) };
            if (converted.value.has_value() == false)
                return std::nullopt;
            return { std::move(*converted.value) };
        }

        // 실제 연결(root가 기본값)을 바꿨을 때만 shell 캐시에 알린다.
        void notify_shell(const std::u8string_view root_subkey)
        {
            if (root_subkey == file_association_default_root)
                SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }
    } // namespace

    file_association_outcome register_file_association(const std::u8string_view executable_path, const std::u8string_view root_subkey)
    {
        file_association_outcome outcome {};
        if (executable_path.empty())
        {
            outcome.diagnostics.push_back(make_error(u8"실행 파일 경로를 확인하지 못해 연결을 등록할 수 없습니다."));
            return outcome;
        }

        for (const registry_default_value& value : make_file_association_values(executable_path))
        {
            const std::optional<std::wstring> subkey { full_subkey_path(root_subkey, value.subkey) };
            if (subkey.has_value() == false || write_default_value(*subkey, value.data) == false)
            {
                outcome.diagnostics.push_back(make_error(std::u8string { u8"registry 등록에 실패했습니다: " } + value.subkey));
                return outcome;
            }
        }

        notify_shell(root_subkey);
        outcome.succeeded = true;
        return outcome;
    }

    file_association_outcome unregister_file_association(const std::u8string_view root_subkey)
    {
        file_association_outcome outcome {};

        // 확장자 연결은 우리 ProgID일 때만 지운다. 남은 하위 값이 있으면 키 삭제가
        // 실패해도 무해하다 (기본값이 비면 연결은 끊긴다).
        const std::optional<std::wstring> extension_subkey { full_subkey_path(root_subkey, file_association_extension_subkey()) };
        if (extension_subkey.has_value() == false)
        {
            outcome.diagnostics.push_back(make_error(u8"registry 경로 변환에 실패했습니다."));
            return outcome;
        }
        const std::optional<std::u8string> current { read_default_value(*extension_subkey) };
        if (current.has_value() && owns_extension_link(*current))
        {
            static_cast<void>(RegDeleteKeyValueW(HKEY_CURRENT_USER, extension_subkey->c_str(), nullptr));
            static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, extension_subkey->c_str()));
        }

        // ProgID tree는 전부 우리 것이다. 깊은 키부터 지우고, 이미 없는 키는
        // 성공으로 본다 (멱등).
        for (const std::u8string& subkey : file_association_prog_id_subkeys())
        {
            const std::optional<std::wstring> path { full_subkey_path(root_subkey, subkey) };
            if (path.has_value() == false)
            {
                outcome.diagnostics.push_back(make_error(u8"registry 경로 변환에 실패했습니다."));
                return outcome;
            }
            const LSTATUS removed { RegDeleteKeyW(HKEY_CURRENT_USER, path->c_str()) };
            if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND)
            {
                outcome.diagnostics.push_back(make_error(std::u8string { u8"registry 제거에 실패했습니다: " } + subkey));
                return outcome;
            }
        }

        notify_shell(root_subkey);
        outcome.succeeded = true;
        return outcome;
    }

    bool file_association_registered(const std::u8string_view executable_path, const std::u8string_view root_subkey)
    {
        if (executable_path.empty())
            return false;

        const std::optional<std::wstring> extension_subkey { full_subkey_path(root_subkey, file_association_extension_subkey()) };
        if (extension_subkey.has_value() == false)
            return false;
        const std::optional<std::u8string> linked { read_default_value(*extension_subkey) };
        if (linked.has_value() == false || owns_extension_link(*linked) == false)
            return false;

        // open command가 이 실행 파일을 가리켜야 등록 상태다. 계획 함수가 만드는
        // command 값과 문자 그대로 비교한다 (같은 코드가 썼으므로 형식이 같다).
        const std::vector<registry_default_value> values { make_file_association_values(executable_path) };
        const std::optional<std::wstring> command_subkey { full_subkey_path(root_subkey, values.back().subkey) };
        if (command_subkey.has_value() == false)
            return false;
        const std::optional<std::u8string> command { read_default_value(*command_subkey) };
        return command.has_value() && *command == values.back().data;
    }

    std::u8string current_executable_path()
    {
        wchar_t path[MAX_PATH + 1] {};
        const DWORD length { GetModuleFileNameW(nullptr, path, MAX_PATH + 1) };
        if (length == 0 || length > MAX_PATH)
            return {};
        auto converted { utf16_to_utf8(std::wstring_view { path, length }) };
        if (converted.value.has_value() == false)
            return {};
        return std::move(*converted.value);
    }
} // namespace gitman::win32

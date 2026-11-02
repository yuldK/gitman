#include "platform/win32/win32_environment_block.h"

#include "platform/win32/utf8.h"

#include <windows.h>

#include <algorithm>
#include <string_view>
#include <utility>

namespace gitman::win32 {
    namespace {
        class environment_strings
        {
        public:
            environment_strings() noexcept
                : value_ { GetEnvironmentStringsW() }
            {}

            environment_strings(const environment_strings&) = delete;
            environment_strings(environment_strings&&) = delete;
            environment_strings& operator=(const environment_strings&) = delete;
            environment_strings& operator=(environment_strings&&) = delete;

            ~environment_strings()
            {
                if (value_ != nullptr)
                    FreeEnvironmentStringsW(value_);
            }

            [[nodiscard]] const wchar_t* get() const noexcept
            {
                return value_;
            }

        private:
            wchar_t* value_ { nullptr };
        };

        struct environment_entry
        {
            std::wstring name {};
            std::wstring value {};
        };

        int compare_environment_names(const std::wstring_view left, const std::wstring_view right) noexcept
        {
            if (left.empty() || right.empty())
                return left.empty() && right.empty() ? 0 : (left.empty() ? -1 : 1);

            const int result { CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) };
            // API가 실패하면 대소문자를 구분하는 비교로 대체한다. 순서만 안정적이면 된다.
            if (result == 0)
                return left.compare(right);
            return result - CSTR_EQUAL;
        }

        std::vector<environment_entry> parent_environment_entries()
        {
            std::vector<environment_entry> entries {};
            const environment_strings strings {};
            if (strings.get() == nullptr)
                return entries;

            for (const wchar_t* cursor = strings.get(); *cursor != L'\0';)
            {
                const std::wstring_view text { cursor };
                cursor += text.size() + 1;
                // `=C:` 형태의 drive 현재 디렉터리 항목은 이름이 `=`로 시작하므로 1부터 찾는다.
                const std::size_t separator { text.find(L'=', 1) };
                if (separator == std::wstring_view::npos)
                    continue;
                entries.push_back(environment_entry { std::wstring { text.substr(0, separator) }, std::wstring { text.substr(separator + 1) } });
            }
            return entries;
        }
    } // namespace

    environment_block make_environment_block(const std::vector<process_environment_override>& overrides)
    {
        if (overrides.empty())
            return {};

        std::vector<environment_entry> entries { parent_environment_entries() };
        for (const process_environment_override& entry : overrides)
        {
            const auto name { utf8_to_utf16(entry.name) };
            if (name.value.has_value() == false)
                return { std::nullopt, static_cast<std::uint32_t>(name.error->native_error), true };

            const auto existing { std::ranges::find_if(entries, [&name](const environment_entry& value) { return compare_environment_names(value.name, *name.value) == 0; }) };
            if (entry.value.has_value() == false)
            {
                if (existing != entries.end())
                    entries.erase(existing);
                continue;
            }

            const auto value { utf8_to_utf16(*entry.value) };
            if (value.value.has_value() == false)
                return { std::nullopt, static_cast<std::uint32_t>(value.error->native_error), true };
            if (existing != entries.end())
                existing->value = *value.value;
            else
                entries.push_back(environment_entry { *name.value, *value.value });
        }

        // CreateProcess에 넘기는 block은 이름 기준 대소문자 무시 정렬을 요구한다.
        std::ranges::sort(entries, [](const environment_entry& left, const environment_entry& right) { return compare_environment_names(left.name, right.name) < 0; });

        std::wstring block {};
        for (const environment_entry& entry : entries)
        {
            block.append(entry.name);
            block.push_back(L'=');
            block.append(entry.value);
            block.push_back(L'\0');
        }
        // std::wstring이 마지막에 암시적 null을 하나 더 두므로 block은 이중 null로 끝난다.
        return { std::move(block), ERROR_SUCCESS, false };
    }
} // namespace gitman::win32

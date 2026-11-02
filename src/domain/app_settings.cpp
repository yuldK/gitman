#include "domain/app_settings.h"

#include "domain/project.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <optional>
#include <utility>

namespace gitman {
    namespace {
        constexpr bool is_path_separator(const char8_t value) noexcept
        {
            return value == u8'\\' || value == u8'/';
        }

        constexpr char8_t ascii_lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        // 경로의 마지막 구성 요소다. 끝 구분자는 무시한다.
        std::u8string_view last_component(std::u8string_view path) noexcept
        {
            while (path.empty() == false && is_path_separator(path.back()))
                path.remove_suffix(1);
            const std::size_t separator { path.find_last_of(u8"\\/") };
            if (separator == std::u8string_view::npos)
                return path;
            return path.substr(separator + 1);
        }

        bool ends_with_document_extension(const std::u8string_view name) noexcept
        {
            if (name.size() <= workspace_document_extension.size())
                return false;

            const std::size_t offset { name.size() - workspace_document_extension.size() };
            for (std::size_t index = 0; index < workspace_document_extension.size(); ++index)
                if (ascii_lower(name[offset + index]) != workspace_document_extension[index])
                    return false;
            return true;
        }
    } // namespace

    std::u8string recent_document_key(const std::u8string_view path)
    {
        std::u8string key {};
        key.reserve(path.size());
        for (const char8_t value : path)
            key.push_back(is_path_separator(value) ? u8'\\' : ascii_lower(value));

        // 뿌리(`C:\`)까지 지워 버리지 않도록 마지막 구분자 하나만 정리한다.
        while (key.size() > 3 && key.back() == u8'\\')
            key.pop_back();
        return key;
    }

    std::u8string recent_document_display_name(const std::u8string_view path)
    {
        const std::u8string_view name { last_component(path) };
        if (ends_with_document_extension(name))
            return std::u8string { name.substr(0, name.size() - workspace_document_extension.size()) };
        return std::u8string { name };
    }

    namespace {
        // 항목 하나를 자기 무리의 맨 앞에 끼워 넣는다. 고정 항목은 목록 앞쪽에
        // 모여 있으므로 비고정은 고정 무리 **뒤**가 자기 자리의 맨 앞이다.
        void insert_at_group_front(app_settings& settings, recent_document entry)
        {
            std::size_t position { 0 };
            if (entry.pinned == false)
                while (position < settings.recent_documents.size() && settings.recent_documents[position].pinned)
                    ++position;
            settings.recent_documents.insert(settings.recent_documents.begin() + static_cast<std::ptrdiff_t>(position), std::move(entry));
        }

        // 비고정 항목만 상한에 걸린다. 고정은 사용자가 고른 것이라 버리지 않는다
        // (2026-08-23 지시).
        void trim_unpinned_recent_documents(app_settings& settings)
        {
            std::size_t unpinned { 0 };
            for (std::size_t index = 0; index < settings.recent_documents.size();)
            {
                if (settings.recent_documents[index].pinned)
                {
                    ++index;
                    continue;
                }
                ++unpinned;
                if (unpinned <= recent_document_capacity)
                {
                    ++index;
                    continue;
                }
                settings.recent_documents.erase(settings.recent_documents.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

        // 목록에서 문서를 떼어 낸다. 있었으면 그 항목을, 없었으면 값이 없다.
        std::optional<recent_document> detach_recent_document(app_settings& settings, const std::u8string_view path)
        {
            const std::u8string key { recent_document_key(path) };
            for (std::size_t index = 0; index < settings.recent_documents.size(); ++index)
                if (recent_document_key(settings.recent_documents[index].path) == key)
                {
                    recent_document entry { std::move(settings.recent_documents[index]) };
                    settings.recent_documents.erase(settings.recent_documents.begin() + static_cast<std::ptrdiff_t>(index));
                    return entry;
                }
            return std::nullopt;
        }
    } // namespace

    void touch_recent_document(app_settings& settings, std::u8string path, std::u8string opened_at)
    {
        if (path.empty())
            return;

        // 고정 여부는 이전 항목에서 물려받는다. 다시 열었다고 고정이 풀리면 놀랍다.
        const std::optional<recent_document> previous { detach_recent_document(settings, path) };

        recent_document entry {};
        entry.display_name = recent_document_display_name(path);
        entry.path = std::move(path);
        entry.opened_at = std::move(opened_at);
        entry.pinned = previous.has_value() && previous->pinned;
        insert_at_group_front(settings, std::move(entry));
        trim_unpinned_recent_documents(settings);
    }

    bool remove_recent_document(app_settings& settings, const std::u8string_view path)
    {
        const std::u8string key { recent_document_key(path) };
        const std::size_t removed { std::erase_if(settings.recent_documents, [&key](const recent_document& value) { return recent_document_key(value.path) == key; }) };
        return removed > 0;
    }

    bool set_recent_document_pinned(app_settings& settings, const std::u8string_view path, const bool pinned)
    {
        // 이미 그 상태면 자리도 건드리지 않는다 — 저장을 부르지 않게 하는 것이 이
        // 반환값의 몫이다.
        const std::u8string key { recent_document_key(path) };
        const auto found { std::ranges::find_if(settings.recent_documents, [&key](const recent_document& value) { return recent_document_key(value.path) == key; }) };
        if (found == settings.recent_documents.end() || found->pinned == pinned)
            return false;

        std::optional<recent_document> entry { detach_recent_document(settings, path) };
        entry->pinned = pinned;
        insert_at_group_front(settings, std::move(*entry));
        trim_unpinned_recent_documents(settings);
        return true;
    }

    bool is_function_definition_trusted(const app_settings& settings, const std::u8string_view document_path, const std::u8string_view digest)
    {
        if (digest.empty())
            return false;
        const std::u8string key { recent_document_key(document_path) };
        return std::ranges::any_of(settings.trusted_function_definitions
            , [&key, digest](const trusted_function_definition& value)
            {
                return value.digest == digest && recent_document_key(value.document_path) == key;
            }
        );
    }

    bool trust_function_definition(app_settings& settings, std::u8string document_path, std::u8string digest)
    {
        const std::u8string key { recent_document_key(document_path) };
        const auto same_document = [&key](const trusted_function_definition& value)
        {
            return recent_document_key(value.document_path) == key;
        };
        // 손으로 고친 파일에 같은 문서 항목이 여럿이면 모두 지우고 하나로 남긴다. 하나뿐이고 해시가
        // 같으면 바뀐 것이 없다.
        std::vector<trusted_function_definition>& values { settings.trusted_function_definitions };
        const auto found { std::ranges::find_if(values, same_document) };
        if (found != values.end() && found->digest == digest && std::ranges::count_if(values, same_document) == 1)
            return false;
        if (found == values.end())
        {
            values.push_back({ std::move(document_path), std::move(digest) });
            return true;
        }
        found->document_path = std::move(document_path);
        found->digest = std::move(digest);
        // `found`가 첫 항목이라 남은 같은 문서 항목은 모두 그 뒤에 있다.
        values.erase(std::remove_if(found + 1, values.end(), same_document), values.end());
        return true;
    }

    std::u8string format_utc_timestamp(const std::chrono::system_clock::time_point time)
    {
        const std::time_t seconds { std::chrono::system_clock::to_time_t(time) };
        std::tm parts {};
        if (gmtime_s(&parts, &seconds) != 0)
            return {};

        char text[32] {};
        const int written {
            std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec),
        };

        if (written != 20)
            return {};
        return std::u8string { reinterpret_cast<const char8_t*>(text), 20 };
    }
} // namespace gitman

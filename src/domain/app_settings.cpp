#include "domain/app_settings.h"

#include "domain/project.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

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

    void touch_recent_document(app_settings& settings, std::u8string path, std::u8string opened_at)
    {
        if (path.empty())
            return;

        const std::u8string key { recent_document_key(path) };
        std::erase_if(settings.recent_documents, [&key](const recent_document& value) { return recent_document_key(value.path) == key; });

        recent_document entry {};
        entry.display_name = recent_document_display_name(path);
        entry.path = std::move(path);
        entry.opened_at = std::move(opened_at);
        settings.recent_documents.insert(settings.recent_documents.begin(), std::move(entry));
        if (settings.recent_documents.size() > recent_document_capacity)
            settings.recent_documents.resize(recent_document_capacity);
    }

    bool remove_recent_document(app_settings& settings, const std::u8string_view path)
    {
        const std::u8string key { recent_document_key(path) };
        const std::size_t removed { std::erase_if(settings.recent_documents, [&key](const recent_document& value) { return recent_document_key(value.path) == key; }) };
        return removed > 0;
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

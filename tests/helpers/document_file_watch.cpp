#include "helpers/document_file_watch.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace gitman::testing {
    document_file_stamp stamp_document_file(const std::u8string& path)
    {
        // `std::filesystem::file_size`·`last_write_time`도 안에서 문서를 잠깐 열어 교체와 겹친다(실측).
        // 찾기 API는 부모 디렉터리만 열고 그 항목을 읽는다. rename 교체는 새 항목을 만들므로 항목의 크기·시각이 곧 새 파일의 것이다.
        WIN32_FIND_DATAW found {};
        const HANDLE find { FindFirstFileW(std::filesystem::path { path }.c_str(), &found) };
        if (find == INVALID_HANDLE_VALUE)
            return {};
        FindClose(find);

        const std::uint64_t size { (static_cast<std::uint64_t>(found.nFileSizeHigh) << 32u) | found.nFileSizeLow };
        const std::uint64_t written { (static_cast<std::uint64_t>(found.ftLastWriteTime.dwHighDateTime) << 32u) | found.ftLastWriteTime.dwLowDateTime };
        return { true, size, written };
    }

    bool wait_for_document_replacement(const std::u8string& path, const document_file_stamp& before, const std::chrono::milliseconds limit)
    {
        const auto deadline { std::chrono::steady_clock::now() + limit };
        while (std::chrono::steady_clock::now() < deadline)
        {
            const document_file_stamp now { stamp_document_file(path) };
            if (now.exists && now != before)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
        }
        return false;
    }

    std::optional<std::string> wait_for_document_content(
        const std::u8string& path
        , const document_file_stamp& before
        , const std::chrono::milliseconds limit
        , const std::function<bool(const std::string&)>& accept
    )
    {
        const auto deadline { std::chrono::steady_clock::now() + limit };
        document_file_stamp seen { before };
        while (true)
        {
            const auto now { std::chrono::steady_clock::now() };
            if (now >= deadline)
                return std::nullopt;

            const auto remaining { std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) };
            if (wait_for_document_replacement(path, seen, remaining) == false)
                return std::nullopt;

            // 읽기 전에 항목을 찍어 둔다. 읽은 내용이 기대와 다르면 이 항목 뒤의 교체만 기다린다.
            seen = stamp_document_file(path);
            std::string content {};
            {
                std::ifstream stream { std::filesystem::path { path }, std::ios::binary };
                content.assign(std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {});
            }
            if (accept(content))
                return content;
        }
    }
}

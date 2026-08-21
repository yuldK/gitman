#include "infrastructure/json_project_store.h"
#include "infrastructure/json_workspace_document.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/workspace_document_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
    constexpr std::u8string_view fake_document_path { u8"C:/gitman-s2-d4/workspace.version-list" };
    constexpr std::uint32_t injected_native_error { 0x4d2U };

    struct fake_file_entry
    {
        std::u8string path {};
        std::u8string bytes {};
    };

    class fake_workspace_document_file_system final : public gitman::workspace_document_file_system
    {
    public:
        void set_file(const std::u8string_view path, const std::u8string_view bytes)
        {
            if (fake_file_entry* entry = find_entry(path); entry != nullptr)
            {
                entry->bytes = bytes;
                return;
            }
            files_.push_back(fake_file_entry { std::u8string { path }, std::u8string { bytes } });
        }

        [[nodiscard]] const std::u8string* file_bytes(const std::u8string_view path) const noexcept
        {
            const fake_file_entry* entry { find_entry(path) };
            return entry == nullptr ? nullptr : &entry->bytes;
        }

        [[nodiscard]] bool has_file(const std::u8string_view path) const noexcept
        {
            return find_entry(path) != nullptr;
        }

        void inject_commit_failure(const gitman::workspace_file_commit_failure failure) noexcept
        {
            injected_failure_ = failure;
        }

        [[nodiscard]] std::size_t atomic_commit_count() const noexcept
        {
            return atomic_commit_count_;
        }

        [[nodiscard]] bool protocol_error() const noexcept
        {
            return protocol_error_;
        }

        [[nodiscard]] bool last_replace_existing() const noexcept
        {
            return last_replace_existing_;
        }

        [[nodiscard]] std::u8string_view last_document_path() const noexcept
        {
            return last_document_path_;
        }

        [[nodiscard]] std::u8string_view last_candidate_bytes() const noexcept
        {
            return last_candidate_bytes_;
        }

        [[nodiscard]] gitman::workspace_file_read_result read(const std::u8string_view path) noexcept override
        {
            try
            {
                const fake_file_entry* entry { find_entry(path) };
                if (entry == nullptr)
                    return { gitman::workspace_file_read_state::not_found, {}, 2U };
                return { gitman::workspace_file_read_state::available, entry->bytes, std::nullopt };
            }
            catch (...)
            {
                return { gitman::workspace_file_read_state::failed, {}, injected_native_error };
            }
        }

        [[nodiscard]] gitman::workspace_file_commit_result atomic_commit(const std::u8string_view document_path, const std::u8string_view bytes, const bool replace_existing) noexcept override
        {
            try
            {
                ++atomic_commit_count_;
                last_document_path_ = document_path;
                last_candidate_bytes_ = bytes;
                last_replace_existing_ = replace_existing;

                if (injected_failure_ != gitman::workspace_file_commit_failure::none)
                    return { injected_failure_, injected_native_error };

                const fake_file_entry* existing { find_entry(document_path) };
                if (replace_existing)
                {
                    if (existing == nullptr)
                    {
                        protocol_error_ = true;
                        return { gitman::workspace_file_commit_failure::replace, injected_native_error };
                    }
                    set_file(document_path, bytes);
                    return {};
                }

                if (existing != nullptr)
                {
                    protocol_error_ = true;
                    return { gitman::workspace_file_commit_failure::replace, injected_native_error };
                }
                set_file(document_path, bytes);
                return {};
            }
            catch (...)
            {
                return { gitman::workspace_file_commit_failure::write, injected_native_error };
            }
        }

    private:
        [[nodiscard]] fake_file_entry* find_entry(const std::u8string_view path) noexcept
        {
            for (fake_file_entry& entry : files_)
                if (entry.path == path)
                    return &entry;
            return nullptr;
        }

        [[nodiscard]] const fake_file_entry* find_entry(const std::u8string_view path) const noexcept
        {
            for (const fake_file_entry& entry : files_)
                if (entry.path == path)
                    return &entry;
            return nullptr;
        }

        std::vector<fake_file_entry> files_ {};
        gitman::workspace_file_commit_failure injected_failure_ { gitman::workspace_file_commit_failure::none };
        std::size_t atomic_commit_count_ {};
        bool protocol_error_ {};
        bool last_replace_existing_ {};
        std::u8string last_document_path_ {};
        std::u8string last_candidate_bytes_ {};
    };

    // 저장 계약 단위 test가 실제 디스크를 조회하지 않도록 lexical 규칙만 흉내 낸다.
    class fake_project_path_resolver final : public gitman::project_path_resolver
    {
    public:
        [[nodiscard]] gitman::project_path_resolution resolve(const std::u8string_view original_path, const std::u8string_view) noexcept override
        {
            try
            {
                return { std::u8string { original_path }, gitman::configured_path_state::available, std::nullopt };
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] bool normalized_equal(const std::u8string_view left, const std::u8string_view right) noexcept override
        {
            return left == right;
        }
    };

    class temporary_directory_fixture
    {
    public:
        temporary_directory_fixture()
        {
            std::error_code error {};
            const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
            REQUIRE_FALSE(static_cast<bool>(error));

            const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
            for (std::size_t attempt = 0; attempt < 100; ++attempt)
            {
                error.clear();
                const std::filesystem::path candidate { base / (L"gitman-store-tests-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
                if (std::filesystem::create_directory(candidate, error))
                {
                    root_ = candidate;
                    break;
                }
            }
            REQUIRE_FALSE(root_.empty());
        }

        ~temporary_directory_fixture()
        {
            std::error_code error {};
            std::filesystem::remove_all(root_, error);
        }

        temporary_directory_fixture(const temporary_directory_fixture&) = delete;
        temporary_directory_fixture& operator=(const temporary_directory_fixture&) = delete;

        [[nodiscard]] const std::filesystem::path& root() const noexcept
        {
            return root_;
        }

    private:
        std::filesystem::path root_ {};
    };

    class scoped_test_handle
    {
    public:
        explicit scoped_test_handle(const HANDLE value) noexcept
            : value_ { value }
        {}

        ~scoped_test_handle()
        {
            if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr)
                CloseHandle(value_);
        }

        scoped_test_handle(const scoped_test_handle&) = delete;
        scoped_test_handle& operator=(const scoped_test_handle&) = delete;

        [[nodiscard]] bool valid() const noexcept
        {
            return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
        }

    private:
        HANDLE value_ { INVALID_HANDLE_VALUE };
    };

    const gitman::diagnostic* find_diagnostic(const std::span<const gitman::diagnostic> diagnostics, const gitman::diagnostic_code code) noexcept
    {
        for (const gitman::diagnostic& value : diagnostics)
            if (value.code == code)
                return &value;
        return nullptr;
    }

    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    bool has_only_crlf_line_endings(const std::u8string_view bytes) noexcept
    {
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            if (bytes[index] == u8'\r' && (index + 1 >= bytes.size() || bytes[index + 1] != u8'\n'))
                return false;
            if (bytes[index] == u8'\n' && (index == 0 || bytes[index - 1] != u8'\r'))
                return false;
        }
        return true;
    }

    bool has_utf8_bom(const std::u8string_view bytes) noexcept
    {
        return bytes.size() >= 3 && bytes[0] == static_cast<char8_t>(0xef) && bytes[1] == static_cast<char8_t>(0xbb) && bytes[2] == static_cast<char8_t>(0xbf);
    }

    gitman::workspace_document empty_document(const std::u8string_view document_path)
    {
        gitman::workspace_document document {};
        document.document_path = document_path;
        return document;
    }

    std::u8string read_file_bytes(const std::filesystem::path& path)
    {
        std::ifstream stream { path, std::ios::binary };
        REQUIRE(stream.is_open());
        const std::string bytes { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
        REQUIRE_FALSE(stream.bad());
        return {
            reinterpret_cast<const char8_t*>(bytes.data()),
            bytes.size(),
        };
    }

    void write_file_bytes(const std::filesystem::path& path, const std::u8string_view bytes)
    {
        std::ofstream stream { path, std::ios::binary | std::ios::trunc };
        REQUIRE(stream.is_open());
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        REQUIRE(stream.good());
    }

    bool has_temporary_artifact(const std::filesystem::path& directory)
    {
        std::error_code error {};
        for (std::filesystem::directory_iterator current { directory, error }, end; current != end; current.increment(error))
        {
            if (error)
                return true;
            const std::wstring name { current->path().filename().wstring() };
            if (name.starts_with(L".gitman-") && name.ends_with(L".tmp"))
                return true;
        }
        return static_cast<bool>(error);
    }
} // namespace

TEST_CASE("Project store creates new documents without replacing an existing file", "[workspace][store][save]")
{
    fake_workspace_document_file_system file_system {};
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    const gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE_FALSE(loaded.document.has_value());
    REQUIRE(loaded.revision.valid());
    REQUIRE(find_diagnostic(loaded.diagnostics, gitman::diagnostic_code::document_not_found) != nullptr);

    const gitman::workspace_document document { empty_document(fake_document_path) };
    const gitman::project_store_save_result saved { store.save(fake_document_path, document, loaded.revision) };

    REQUIRE(saved.succeeded());
    REQUIRE(saved.revision.has_value());
    REQUIRE(saved.revision->valid());
    REQUIRE(file_system.atomic_commit_count() == 1);
    REQUIRE_FALSE(file_system.protocol_error());
    REQUIRE_FALSE(file_system.last_replace_existing());
    REQUIRE(u8_equal(file_system.last_document_path(), fake_document_path));
    constexpr std::u8string_view expected { u8"{\r\n    \"schema_version\": 1,\r\n    \"projects\": []\r\n}\r\n" };
    REQUIRE(u8_equal(file_system.last_candidate_bytes(), expected));
    REQUIRE_FALSE(file_system.has_file(u8"C:/gitman-s2-d4/workspace.version-list.bak"));
}

TEST_CASE("Project store preserves shadow fields and canonical output", "[workspace][store][save]")
{
    constexpr std::u8string_view source {
        u8R"({
    "schema_version": 1,
    "future_top": { "keep": true },
    "projects": [
        {
            "id": "alpha",
            "path": "C:/gitman-s2-d4/original repo/../repo",
            "svn_switch_targets": ["https://svn.example.com/repo/legacy"],
            "future_project": "keep"
        }
    ]
})",
    };

    fake_workspace_document_file_system file_system {};
    file_system.set_file(fake_document_path, source);
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE_FALSE(loaded.has_errors());
    REQUIRE(loaded.document->projects.size() == 1);
    loaded.document->projects[0].enabled = false;

    const gitman::project_store_save_result saved { store.save(fake_document_path, *loaded.document, loaded.revision) };
    REQUIRE(saved.succeeded());
    REQUIRE(file_system.last_replace_existing());

    const std::u8string* output { file_system.file_bytes(fake_document_path) };
    REQUIRE(output != nullptr);
    REQUIRE_FALSE(has_utf8_bom(*output));
    REQUIRE(has_only_crlf_line_endings(*output));
    REQUIRE(output->ends_with(u8"\r\n"));
    REQUIRE(output->find(u8"\r\n    \"schema_version\": 1") != std::u8string::npos);
    REQUIRE(output->find(u8"\"future_top\": {") != std::u8string::npos);
    REQUIRE(output->find(u8"\"future_project\": \"keep\"") != std::u8string::npos);
    REQUIRE(output->find(u8"\"path\": \"C:/gitman-s2-d4/original repo/../repo\"") != std::u8string::npos);
    REQUIRE(output->find(u8"\"svn_switch_targets\": [") != std::u8string::npos);
    REQUIRE(output->find(u8"https://svn.example.com/repo/legacy") != std::u8string::npos);
    REQUIRE(output->find(u8"\"enabled\": false") != std::u8string::npos);
    REQUIRE(output->find(u8"\"display_name\"") == std::u8string::npos);
    REQUIRE(output->find(u8"\"vcs_hint\"") == std::u8string::npos);

    // 저장은 backup 파일을 남기지 않는다 (2026-08-21 사용자 지시).
    REQUIRE_FALSE(file_system.has_file(u8"C:/gitman-s2-d4/workspace.version-list.bak"));
}

TEST_CASE("Project store round-trips workspace settings and unknown keys", "[workspace][store][save][settings]")
{
    constexpr std::u8string_view source {
        u8R"({
    "schema_version": 1,
    "settings": {
        "git_executable": "C:/tools/git.exe",
        "future_setting": { "keep": true }
    },
    "projects": []
})",
    };

    fake_workspace_document_file_system file_system {};
    file_system.set_file(fake_document_path, source);
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE_FALSE(loaded.has_errors());
    REQUIRE(u8_equal(loaded.document->settings.git_executable, u8"C:/tools/git.exe"));

    // 환경설정 화면이 SVN 경로를 채우는 상황이다.
    loaded.document->settings.svn_executable = u8"D:/tools/svn/bin/svn.exe";

    const gitman::project_store_save_result saved { store.save(fake_document_path, *loaded.document, loaded.revision) };
    REQUIRE(saved.succeeded());

    const std::u8string* output { file_system.file_bytes(fake_document_path) };
    REQUIRE(output != nullptr);
    REQUIRE(output->find(u8"\"git_executable\": \"C:/tools/git.exe\"") != std::u8string::npos);
    REQUIRE(output->find(u8"\"svn_executable\": \"D:/tools/svn/bin/svn.exe\"") != std::u8string::npos);
    // 알 수 없는 키는 project 필드와 같은 정책으로 보존한다.
    REQUIRE(output->find(u8"\"future_setting\": {") != std::u8string::npos);

    const gitman::project_store_load_result reloaded { store.load(fake_document_path) };
    REQUIRE(reloaded.document.has_value());
    REQUIRE(u8_equal(reloaded.document->settings.git_executable, u8"C:/tools/git.exe"));
    REQUIRE(u8_equal(reloaded.document->settings.svn_executable, u8"D:/tools/svn/bin/svn.exe"));
}

TEST_CASE("Project store writes and removes the query timeout setting", "[workspace][store][save][settings]")
{
    constexpr std::u8string_view source {
        u8R"({
    "schema_version": 1,
    "settings": {
        "query_timeout_seconds": 900
    },
    "projects": []
})",
    };

    fake_workspace_document_file_system file_system {};
    file_system.set_file(fake_document_path, source);
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE(loaded.document->settings.query_timeout_seconds == 900);

    // 값을 바꾸면 그대로 저장된다.
    loaded.document->settings.query_timeout_seconds = 1800;
    const gitman::project_store_save_result saved { store.save(fake_document_path, *loaded.document, loaded.revision) };
    REQUIRE(saved.succeeded());
    const std::u8string* output { file_system.file_bytes(fake_document_path) };
    REQUIRE(output != nullptr);
    REQUIRE(output->find(u8"\"query_timeout_seconds\": 1800") != std::u8string::npos);

    // 기본값으로 되돌리면 필드를 지운다. 남기면 다음 열기에서 이전 값이 되살아난다.
    gitman::project_store_load_result changed { store.load(fake_document_path) };
    REQUIRE(changed.document.has_value());
    changed.document->settings.query_timeout_seconds.reset();
    const gitman::project_store_save_result cleared { store.save(fake_document_path, *changed.document, changed.revision) };
    REQUIRE(cleared.succeeded());
    output = file_system.file_bytes(fake_document_path);
    REQUIRE(output != nullptr);
    REQUIRE(output->find(u8"query_timeout_seconds") == std::u8string::npos);

    const gitman::project_store_load_result reloaded { store.load(fake_document_path) };
    REQUIRE(reloaded.document.has_value());
    REQUIRE(reloaded.document->settings.query_timeout_seconds.has_value() == false);
}

TEST_CASE("Project store does not add a settings field to documents that lack one", "[workspace][store][save][settings]")
{
    constexpr std::u8string_view source { u8"{\"schema_version\":1,\"projects\":[]}" };

    fake_workspace_document_file_system file_system {};
    file_system.set_file(fake_document_path, source);
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    const gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE(loaded.document->settings.is_default());

    const gitman::project_store_save_result saved { store.save(fake_document_path, *loaded.document, loaded.revision) };
    REQUIRE(saved.succeeded());

    const std::u8string* output { file_system.file_bytes(fake_document_path) };
    REQUIRE(output != nullptr);
    // 기본값만 있는 문서에 필드를 만들지 않아 기존 문서 형태를 바꾸지 않는다.
    REQUIRE(output->find(u8"\"settings\"") == std::u8string::npos);
    REQUIRE(u8_equal(*output, u8"{\r\n    \"schema_version\": 1,\r\n    \"projects\": []\r\n}\r\n"));
}

TEST_CASE("Project store writes settings once a value is configured", "[workspace][store][save][settings]")
{
    fake_workspace_document_file_system file_system {};
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    const gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE_FALSE(loaded.document.has_value());

    gitman::workspace_document document { empty_document(fake_document_path) };
    document.settings.git_executable = u8"C:/tools/git.exe";

    const gitman::project_store_save_result saved { store.save(fake_document_path, document, loaded.revision) };
    REQUIRE(saved.succeeded());

    constexpr std::u8string_view expected {
        u8"{\r\n    \"schema_version\": 1,\r\n    \"settings\": {\r\n        \"git_executable\": \"C:/tools/git.exe\"\r\n    },\r\n    \"projects\": []\r\n}\r\n",
    };
    REQUIRE(u8_equal(file_system.last_candidate_bytes(), expected));
    // 지정하지 않은 SVN 경로는 빈 항목으로 채우지 않는다.
    REQUIRE(file_system.last_candidate_bytes().find(u8"svn_executable") == std::u8string::npos);
}

TEST_CASE("Project store refuses to save relative executable paths", "[workspace][store][save][settings]")
{
    fake_workspace_document_file_system file_system {};
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    const gitman::project_store_load_result loaded { store.load(fake_document_path) };
    gitman::workspace_document document { empty_document(fake_document_path) };
    document.settings.git_executable = u8"git.exe";

    // 저장 직전 재검증이 상대 경로를 걸러내므로 손상된 문서가 디스크에 남지 않는다.
    const gitman::project_store_save_result saved { store.save(fake_document_path, document, loaded.revision) };
    REQUIRE_FALSE(saved.succeeded());
    REQUIRE(find_diagnostic(saved.diagnostics, gitman::diagnostic_code::vcs_tool_path_invalid) != nullptr);
    REQUIRE(file_system.atomic_commit_count() == 0);
    REQUIRE_FALSE(file_system.has_file(fake_document_path));
}

TEST_CASE("Project store rejects stale exact byte revisions", "[workspace][store][concurrency]")
{
    constexpr std::u8string_view source { u8"{\"schema_version\":1,\"projects\":[]}" };
    constexpr std::u8string_view external_source { u8"{ \"schema_version\": 1, \"projects\": [] }" };
    fake_workspace_document_file_system file_system {};
    file_system.set_file(fake_document_path, source);
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    const gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE(loaded.document.has_value());
    file_system.set_file(fake_document_path, external_source);

    const gitman::project_store_save_result saved { store.save(fake_document_path, *loaded.document, loaded.revision) };
    REQUIRE_FALSE(saved.succeeded());
    REQUIRE(find_diagnostic(saved.diagnostics, gitman::diagnostic_code::concurrent_modification) != nullptr);
    REQUIRE(file_system.atomic_commit_count() == 0);
    REQUIRE(u8_equal(*file_system.file_bytes(fake_document_path), external_source));
    REQUIRE_FALSE(file_system.has_file(u8"C:/gitman-s2-d4/workspace.version-list.bak"));
}

TEST_CASE("Project store revalidates complete candidates before committing", "[workspace][store][validation]")
{
    constexpr std::u8string_view source {
        u8R"({"schema_version":1,"projects":[
        {"id":"first","path":"C:/gitman-s2-d4/first"},
        {"id":"second","path":"C:/gitman-s2-d4/second"}
    ]})",
    };
    fake_workspace_document_file_system file_system {};
    file_system.set_file(fake_document_path, source);
    fake_project_path_resolver path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };

    gitman::project_store_load_result loaded { store.load(fake_document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE(loaded.document->projects.size() == 2);
    loaded.document->projects[1].id = loaded.document->projects[0].id;

    const gitman::project_store_save_result saved { store.save(fake_document_path, *loaded.document, loaded.revision) };
    REQUIRE_FALSE(saved.succeeded());
    REQUIRE(find_diagnostic(saved.diagnostics, gitman::diagnostic_code::duplicate_project_id) != nullptr);
    REQUIRE(file_system.atomic_commit_count() == 0);
    REQUIRE(u8_equal(*file_system.file_bytes(fake_document_path), source));
}

TEST_CASE("Project store maps injected commit failures without changing originals", "[workspace][store][failure]")
{
    struct failure_case
    {
        gitman::workspace_file_commit_failure failure { gitman::workspace_file_commit_failure::none };
        gitman::diagnostic_code diagnostic { gitman::diagnostic_code::unknown };
    };
    constexpr std::array cases {
        failure_case { gitman::workspace_file_commit_failure::write, gitman::diagnostic_code::document_write_failed },
        failure_case { gitman::workspace_file_commit_failure::flush, gitman::diagnostic_code::document_flush_failed },
        failure_case { gitman::workspace_file_commit_failure::replace, gitman::diagnostic_code::document_replace_failed },
    };
    constexpr std::u8string_view source { u8"{\"schema_version\":1,\"projects\":[]}" };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        CAPTURE(index);
        fake_workspace_document_file_system file_system {};
        file_system.set_file(fake_document_path, source);
        fake_project_path_resolver path_resolver {};
        gitman::json_project_store store { file_system, path_resolver };
        const gitman::project_store_load_result loaded { store.load(fake_document_path) };
        REQUIRE(loaded.document.has_value());
        file_system.inject_commit_failure(cases[index].failure);

        const gitman::project_store_save_result saved { store.save(fake_document_path, *loaded.document, loaded.revision) };
        REQUIRE_FALSE(saved.succeeded());
        const gitman::diagnostic* failure { find_diagnostic(saved.diagnostics, cases[index].diagnostic) };
        REQUIRE(failure != nullptr);
        REQUIRE(failure->native_error == injected_native_error);
        REQUIRE(file_system.atomic_commit_count() == 1);
        REQUIRE(u8_equal(*file_system.file_bytes(fake_document_path), source));
        REQUIRE_FALSE(file_system.has_file(u8"C:/gitman-s2-d4/workspace.version-list.bak"));
    }
}

TEST_CASE("Win32 workspace storage creates replaces and detects external changes", "[workspace][store][win32]")
{
    temporary_directory_fixture fixture {};
    const std::filesystem::path document_path { fixture.root() / L"workspace.version-list" };
    const std::u8string document_path_utf8 { document_path.u8string() };
    const std::filesystem::path backup_path { document_path.wstring() + L".bak" };
    gitman::win32::workspace_document_file_system file_system {};
    // Win32 통합 test는 실제 경로 해석 구현을 그대로 주입한다.
    const std::unique_ptr<gitman::project_path_resolver> path_resolver { gitman::win32::make_project_path_resolver() };
    REQUIRE(path_resolver != nullptr);
    gitman::json_project_store store { file_system, *path_resolver };

    const gitman::project_store_load_result missing { store.load(document_path_utf8) };
    REQUIRE_FALSE(missing.document.has_value());
    REQUIRE(missing.revision.valid());

    const gitman::workspace_document initial_document { empty_document(document_path_utf8) };
    const gitman::project_store_save_result created { store.save(document_path_utf8, initial_document, missing.revision) };
    REQUIRE(created.succeeded());
    REQUIRE(std::filesystem::is_regular_file(document_path));
    REQUIRE_FALSE(std::filesystem::exists(backup_path));
    const std::u8string first_bytes { read_file_bytes(document_path) };
    REQUIRE_FALSE(has_utf8_bom(first_bytes));
    REQUIRE(has_only_crlf_line_endings(first_bytes));

    gitman::project_store_load_result loaded { store.load(document_path_utf8) };
    REQUIRE(loaded.document.has_value());
    gitman::project_definition project {};
    project.id.value = u8"repository";
    project.path.original = fixture.root().u8string();
    project.display_name = fixture.root().filename().u8string();
    loaded.document->projects.push_back(std::move(project));
    const gitman::project_store_save_result replaced { store.save(document_path_utf8, *loaded.document, loaded.revision) };
    REQUIRE(replaced.succeeded());
    // 교체 저장도 backup 파일을 남기지 않는다 (2026-08-21 사용자 지시).
    REQUIRE_FALSE(std::filesystem::exists(backup_path));
    REQUIRE_FALSE(has_temporary_artifact(fixture.root()));

    const gitman::project_store_load_result before_external_change { store.load(document_path_utf8) };
    REQUIRE(before_external_change.document.has_value());
    constexpr std::u8string_view external_bytes { u8"{ \"schema_version\": 1, \"projects\": [] }" };
    write_file_bytes(document_path, external_bytes);
    const gitman::project_store_save_result conflicted { store.save(document_path_utf8, *before_external_change.document, before_external_change.revision) };
    REQUIRE_FALSE(conflicted.succeeded());
    REQUIRE(find_diagnostic(conflicted.diagnostics, gitman::diagnostic_code::concurrent_modification) != nullptr);
    REQUIRE(u8_equal(read_file_bytes(document_path), external_bytes));
    REQUIRE_FALSE(std::filesystem::exists(backup_path));
    REQUIRE_FALSE(has_temporary_artifact(fixture.root()));
}

TEST_CASE("Win32 workspace storage cleans temporary files after replace failure", "[workspace][store][win32][failure]")
{
    temporary_directory_fixture fixture {};
    const std::filesystem::path document_path { fixture.root() / L"locked.version-list" };
    const std::filesystem::path backup_path { fixture.root() / L"locked.version-list.bak" };
    constexpr std::u8string_view original_bytes { u8"original" };
    constexpr std::u8string_view candidate_bytes { u8"candidate" };
    write_file_bytes(document_path, original_bytes);

    scoped_test_handle locked { CreateFileW(document_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
    REQUIRE(locked.valid());

    gitman::win32::workspace_document_file_system file_system {};
    const gitman::workspace_file_commit_result committed { file_system.atomic_commit(document_path.u8string(), candidate_bytes, true) };
    REQUIRE_FALSE(committed.succeeded());
    REQUIRE(committed.failure == gitman::workspace_file_commit_failure::replace);
    REQUIRE(u8_equal(read_file_bytes(document_path), original_bytes));
    REQUIRE_FALSE(std::filesystem::exists(backup_path));
    REQUIRE_FALSE(has_temporary_artifact(fixture.root()));
}

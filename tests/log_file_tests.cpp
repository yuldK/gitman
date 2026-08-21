#include "application/logic_controller.h"
#include "domain/log_file_naming.h"
#include "infrastructure/file_log_writer.h"
#include "platform/win32/win32_log_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    std::vector<std::u8string> folder_names(const std::vector<std::u8string>& paths)
    {
        return gitman::log_folder_names(std::span<const std::u8string> { paths });
    }

    // 메모리에만 쓰는 로그 파일 시스템이다. 실패 주입과 기록 확인을 함께 한다.
    class fake_log_file_system final : public gitman::log_file_system
    {
    public:
        [[nodiscard]] bool create_directories(const std::u8string_view path) noexcept override
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            if (directories_fail_)
                return false;
            directories_.emplace_back(path);
            return true;
        }

        [[nodiscard]] bool file_exists(const std::u8string_view path) noexcept override
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            for (const auto& file : files_)
                if (file.first == path)
                    return true;
            return false;
        }

        [[nodiscard]] bool append_file(const std::u8string_view path, const std::u8string_view bytes) noexcept override
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            if (appends_fail_)
                return false;
            for (auto& file : files_)
                if (file.first == path)
                {
                    file.second.append(bytes);
                    return true;
                }
            files_.emplace_back(std::u8string { path }, std::u8string { bytes });
            return true;
        }

        void fail_directories() noexcept
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            directories_fail_ = true;
        }

        void fail_appends() noexcept
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            appends_fail_ = true;
        }

        [[nodiscard]] std::vector<std::u8string> directories() const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return directories_;
        }

        [[nodiscard]] std::size_t file_count() const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return files_.size();
        }

        // 경로에 `fragment`가 들어간 첫 파일의 내용이다.
        [[nodiscard]] std::optional<std::u8string> content_of(const std::u8string_view fragment) const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            for (const auto& file : files_)
                if (file.first.find(fragment) != std::u8string::npos)
                    return file.second;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::u8string> path_of(const std::u8string_view fragment) const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            for (const auto& file : files_)
                if (file.first.find(fragment) != std::u8string::npos)
                    return file.first;
            return std::nullopt;
        }

    private:
        mutable std::mutex mutex_ {};
        std::vector<std::u8string> directories_ {};
        std::vector<std::pair<std::u8string, std::u8string>> files_ {};
        bool directories_fail_ { false };
        bool appends_fail_ { false };
    };

    gitman::operation_log_entry make_entry(const std::u8string_view text, const gitman::log_entry_kind kind = gitman::log_entry_kind::lifecycle)
    {
        gitman::operation_log_entry entry {};
        entry.kind = kind;
        entry.severity = gitman::diagnostic_severity::information;
        entry.text = text;
        entry.time = std::chrono::system_clock::now();
        return entry;
    }

    std::vector<gitman::log_file_target> make_targets(const std::vector<std::pair<std::u8string, std::u8string>>& entries)
    {
        std::vector<gitman::log_file_target> targets {};
        for (const auto& entry : entries)
        {
            gitman::log_file_target target {};
            target.id.value = entry.first;
            target.display_name = entry.first;
            target.repository_path = entry.second;
            targets.push_back(std::move(target));
        }
        return targets;
    }
} // namespace

TEST_CASE("The log root is the document path plus the log suffix", "[log-file][domain]")
{
    // 탐색기에서 맨 앞에 모이도록 이름 앞에 `.`을 붙인다 (2026-08-22 지시).
    REQUIRE(u8_equal(gitman::log_root_path(u8"D:\\workspaces\\team.version-list"), u8"D:\\workspaces\\.team.version-list.log"));
    REQUIRE(u8_equal(gitman::log_root_path(u8"D:/workspaces/team.version-list"), u8"D:/workspaces/.team.version-list.log"));
    // 폴더가 없는 이름도 그대로 앞에 붙인다.
    REQUIRE(u8_equal(gitman::log_root_path(u8"team.version-list"), u8".team.version-list.log"));
}

TEST_CASE("Log folder names use the last folder and grow only where they collide", "[log-file][domain]")
{
    // 겹치지 않으면 마지막 폴더 이름 그대로다.
    const std::vector<std::u8string> distinct { u8"C:\\work\\frontend", u8"C:\\work\\backend" };
    const std::vector<std::u8string> distinct_names { folder_names(distinct) };
    REQUIRE(u8_equal(distinct_names[0], u8"frontend"));
    REQUIRE(u8_equal(distinct_names[1], u8"backend"));

    // 겹치면 겹치는 것들만 구분될 때까지 상위 세그먼트를 붙인다.
    const std::vector<std::u8string> collide { u8"C:\\a\\b\\c", u8"C:\\x\\y\\c", u8"C:\\work\\alone" };
    const std::vector<std::u8string> collide_names { folder_names(collide) };
    REQUIRE(u8_equal(collide_names[0], u8"b-c"));
    REQUIRE(u8_equal(collide_names[1], u8"y-c"));
    REQUIRE(u8_equal(collide_names[2], u8"alone"));

    // 경로 전체가 같고 드라이브만 다르면 드라이브까지 붙는다.
    const std::vector<std::u8string> drives { u8"C:\\a\\b\\c", u8"D:\\a\\b\\c" };
    const std::vector<std::u8string> drive_names { folder_names(drives) };
    REQUIRE(u8_equal(drive_names[0], u8"c-drive_a-b-c"));
    REQUIRE(u8_equal(drive_names[1], u8"d-drive_a-b-c"));

    // UNC는 server-share를 뿌리로 쓴다.
    const std::vector<std::u8string> unc { u8"\\\\server\\share\\a\\b", u8"C:\\a\\b" };
    const std::vector<std::u8string> unc_names { folder_names(unc) };
    REQUIRE(u8_equal(unc_names[0], u8"server-share_a-b"));
    REQUIRE(u8_equal(unc_names[1], u8"c-drive_a-b"));
}

TEST_CASE("Log folder names obey Windows file name rules", "[log-file][domain]")
{
    const std::vector<std::u8string> awkward { u8"C:\\work\\repo?name", u8"C:\\work\\trailing.", u8"C:\\work\\CON", u8"C:\\work\\한글 저장소" };
    const std::vector<std::u8string> names { folder_names(awkward) };
    REQUIRE(u8_equal(names[0], u8"repo_name"));
    REQUIRE(u8_equal(names[1], u8"trailing"));
    REQUIRE(u8_equal(names[2], u8"CON_"));
    REQUIRE(u8_equal(names[3], u8"한글 저장소"));

    // 아주 긴 이름은 앞부분 + 해시로 자른다.
    const std::vector<std::u8string> long_path { std::u8string { u8"C:\\work\\" } + std::u8string(200, u8'a') };
    const std::vector<std::u8string> long_names { folder_names(long_path) };
    REQUIRE(long_names[0].size() == gitman::log_folder_name_limit + 9);

    // 빈 경로도 이름을 만든다.
    const std::vector<std::u8string> empty { u8"" };
    REQUIRE(u8_equal(folder_names(empty)[0], u8"repository"));
}

TEST_CASE("Log file names are local timestamps that avoid collisions", "[log-file][domain]")
{
    const std::u8string first { gitman::log_file_name(std::chrono::system_clock::now(), 0) };
    REQUIRE(first.size() == 19);
    REQUIRE(first.ends_with(u8".log"));
    REQUIRE(first[8] == u8'-');

    const std::u8string second { gitman::log_file_name(std::chrono::system_clock::now(), 1) };
    REQUIRE(second.ends_with(u8"-2.log"));
}

TEST_CASE("Log file lines carry the timestamp kind and severity", "[log-file][domain]")
{
    const std::u8string line { gitman::format_log_file_line(make_entry(u8"pull 완료", gitman::log_entry_kind::standard_output)) };
    REQUIRE(line.find(u8"[stdout/information] pull 완료") != std::u8string::npos);
    REQUIRE(line.find(u8'\n') == std::u8string::npos);
}

TEST_CASE("The writer creates one file per repository and appends the header once", "[log-file][writer]")
{
    fake_log_file_system files {};
    gitman::file_log_writer writer { files };

    const std::vector<gitman::log_file_target> targets { make_targets({ { u8"alpha", u8"C:\\work\\alpha" }, { u8"beta", u8"C:\\work\\beta" } }) };
    writer.set_document(u8"D:\\ws\\team.version-list", targets);
    writer.append(gitman::project_id { u8"alpha" }, make_entry(u8"첫 줄"));
    writer.append(gitman::project_id { u8"alpha" }, make_entry(u8"둘째 줄"));
    writer.flush();

    // 로그가 있는 저장소의 폴더만 만든다.
    REQUIRE(files.file_count() == 1);
    const std::vector<std::u8string> directories { files.directories() };
    REQUIRE(directories.size() == 1);
    REQUIRE(u8_equal(directories.front(), u8"D:\\ws\\.team.version-list.log\\alpha"));

    const std::optional<std::u8string> content { files.content_of(u8"alpha") };
    REQUIRE(content.has_value());
    REQUIRE(content->find(u8"# 문서: D:\\ws\\.team.version-list.log") != std::u8string::npos);
    REQUIRE(content->find(u8"# 저장소: alpha (C:\\work\\alpha)") != std::u8string::npos);
    REQUIRE(content->find(u8"첫 줄") != std::u8string::npos);
    REQUIRE(content->find(u8"둘째 줄") != std::u8string::npos);
    // 머리글은 한 번만 적힌다 (세션당 파일 하나).
    REQUIRE(content->find(u8"# 저장소:") == content->rfind(u8"# 저장소:"));

    // 다른 저장소는 자기 폴더에 쓴다.
    writer.append(gitman::project_id { u8"beta" }, make_entry(u8"beta 줄"));
    writer.flush();
    REQUIRE(files.file_count() == 2);
    const std::optional<std::u8string> beta { files.path_of(u8".team.version-list.log\\beta") };
    REQUIRE(beta.has_value());
}

TEST_CASE("The writer ignores entries without a document or an unknown card", "[log-file][writer]")
{
    fake_log_file_system files {};
    gitman::file_log_writer writer { files };

    // 문서를 알리기 전에는 버린다.
    writer.append(gitman::project_id { u8"alpha" }, make_entry(u8"버려짐"));
    writer.flush();
    REQUIRE(files.file_count() == 0);

    writer.set_document(u8"D:\\ws\\team.version-list", make_targets({ { u8"alpha", u8"C:\\work\\alpha" } }));
    writer.append(gitman::project_id { u8"unknown" }, make_entry(u8"모르는 카드"));
    writer.flush();
    REQUIRE(files.file_count() == 0);

    // 문서를 닫으면 다시 버린다.
    writer.set_document(u8"", {});
    writer.append(gitman::project_id { u8"alpha" }, make_entry(u8"닫힌 뒤"));
    writer.flush();
    REQUIRE(files.file_count() == 0);
}

TEST_CASE("A failing log folder disables the file log with a reason", "[log-file][writer]")
{
    fake_log_file_system files {};
    files.fail_directories();
    gitman::file_log_writer writer { files };

    writer.set_document(u8"D:\\ws\\team.version-list", make_targets({ { u8"alpha", u8"C:\\work\\alpha" } }));
    writer.append(gitman::project_id { u8"alpha" }, make_entry(u8"첫 줄"));
    writer.flush();

    REQUIRE(files.file_count() == 0);
    REQUIRE(writer.disabled());
    const std::optional<std::u8string> failure { writer.take_failure() };
    REQUIRE(failure.has_value());
    REQUIRE(failure->find(u8"로그 폴더를 만들지 못해") != std::u8string::npos);
    // 사유는 한 번만 돌려준다.
    REQUIRE(writer.take_failure().has_value() == false);

    // 꺼진 뒤에는 더 시도하지 않는다.
    writer.append(gitman::project_id { u8"alpha" }, make_entry(u8"둘째 줄"));
    writer.flush();
    REQUIRE(files.file_count() == 0);
}

TEST_CASE("A failing append disables the file log too", "[log-file][writer]")
{
    fake_log_file_system files {};
    files.fail_appends();
    gitman::file_log_writer writer { files };

    writer.set_document(u8"D:\\ws\\team.version-list", make_targets({ { u8"alpha", u8"C:\\work\\alpha" } }));
    writer.append(gitman::project_id { u8"alpha" }, make_entry(u8"첫 줄"));
    writer.flush();

    REQUIRE(writer.disabled());
    const std::optional<std::u8string> failure { writer.take_failure() };
    REQUIRE(failure.has_value());
    REQUIRE(failure->find(u8"로그 파일을 쓰지 못해") != std::u8string::npos);
}

namespace {
    class recording_submitter final : public gitman::operation_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::operation_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::operation_request> requests {};
    };

    // logic이 sink에 무엇을 보내는지 그대로 기록하는 대역이다.
    class recording_sink final : public gitman::log_file_sink
    {
    public:
        void set_document(const std::u8string_view document_path, const std::span<const gitman::log_file_target> published) override
        {
            document = document_path;
            targets.assign(published.begin(), published.end());
            ++document_changes;
        }

        void append(const gitman::project_id& id, const gitman::operation_log_entry& entry) override
        {
            appended.emplace_back(id.value, entry.text);
        }

        void flush() override
        {}

        [[nodiscard]] std::optional<std::u8string> take_failure() override
        {
            std::optional<std::u8string> value { std::move(failure) };
            failure.reset();
            return value;
        }

        std::u8string document {};
        std::vector<gitman::log_file_target> targets {};
        std::vector<std::pair<std::u8string, std::u8string>> appended {};
        std::size_t document_changes { 0 };
        std::optional<std::u8string> failure {};
    };

    gitman::document_loaded_event make_loaded_document()
    {
        gitman::document_loaded_event event {};
        gitman::workspace_document document {};
        document.document_path = u8"D:\\ws\\team.version-list";
        gitman::project_definition project {};
        project.id.value = u8"alpha";
        project.display_name = u8"Alpha";
        project.path.original = u8"C:\\work\\alpha";
        project.path.normalized = project.path.original;
        document.projects.push_back(std::move(project));
        event.document = { std::move(document) };
        return event;
    }
} // namespace

TEST_CASE("Logic mirrors the card log into the sink and reports sink failures once", "[logic][log-file]")
{
    recording_submitter submitter {};
    recording_sink sink {};
    gitman::logic_controller controller { submitter, sink };

    controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\ws\\team.version-list" } });
    controller.handle(gitman::logic_message { make_loaded_document() });

    // 문서를 열면 적재 대상이 sink로 나간다.
    REQUIRE(sink.document == u8"D:\\ws\\team.version-list");
    REQUIRE(sink.targets.size() == 1);
    REQUIRE(sink.targets.front().display_name == u8"Alpha");
    REQUIRE(sink.targets.front().repository_path == u8"C:\\work\\alpha");

    // 조회 실패는 카드 로그와 파일 양쪽에 남는다.
    gitman::query_completed_event query {};
    query.id.value = u8"alpha";
    query.generation = 1;
    query.final_event = true;
    query.result.snapshot.project.value = u8"alpha";
    gitman::diagnostic failure {};
    failure.code = gitman::diagnostic_code::vcs_command_failed;
    failure.severity = gitman::diagnostic_severity::error;
    failure.message = u8"상태 확인에 실패했습니다.";
    query.result.diagnostics.push_back(std::move(failure));
    sink.failure = u8"로그 파일을 쓰지 못해 이 문서의 파일 로그를 끕니다.";
    controller.handle(gitman::logic_message { std::move(query) });

    REQUIRE(sink.appended.size() == 1);
    REQUIRE(sink.appended.front().first == u8"alpha");
    REQUIRE(sink.appended.front().second == u8"상태 확인에 실패했습니다.");

    // sink 실패 문구는 화면 로그에만 남고 파일로 다시 보내지 않는다.
    const gitman::operation_log_buffer* const buffer { controller.card_log(gitman::project_id { u8"alpha" }) };
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->records().size() == 2);
    REQUIRE(buffer->records().back().entry.text.find(u8"파일 로그를 끕니다") != std::u8string::npos);
    REQUIRE(sink.appended.size() == 1);

    // 문서를 닫으면 빈 대상이 나간다.
    controller.handle(gitman::logic_message { gitman::close_document_intent {} });
    REQUIRE(sink.document.empty());
    REQUIRE(sink.targets.empty());
}

TEST_CASE("The document setting can turn the file log off", "[logic][log-file]")
{
    recording_submitter submitter {};
    recording_sink sink {};
    gitman::logic_controller controller { submitter, sink };

    controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\ws\\team.version-list" } });
    gitman::document_loaded_event loaded { make_loaded_document() };
    loaded.document->settings.write_log_files = false;
    controller.handle(gitman::logic_message { std::move(loaded) });

    // 설정이 꺼져 있으면 문서 경로를 알리지 않아 폴더도 만들지 않는다.
    REQUIRE(sink.document.empty());
    REQUIRE(sink.targets.size() == 1);

    // 환경설정에서 켜면 곧바로 적재가 시작된다.
    controller.handle(gitman::logic_message { gitman::open_settings_intent {} });
    controller.handle(gitman::logic_message { gitman::toggle_settings_log_files_intent {} });
    controller.handle(gitman::logic_message { gitman::confirm_settings_intent {} });
    REQUIRE(sink.document == u8"D:\\ws\\team.version-list");
}

TEST_CASE("The Win32 log file system creates nested folders and appends", "[log-file][win32]")
{
    std::error_code error {};
    const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
    REQUIRE_FALSE(static_cast<bool>(error));
    const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
    const std::filesystem::path root { base / (L"gitman-log-tests-" + std::to_wstring(token)) };

    gitman::win32::log_file_system files {};
    const std::filesystem::path nested { root / L".team.version-list.log" / L"c-drive_a-b-c" };
    REQUIRE(files.create_directories(nested.u8string()));
    REQUIRE(std::filesystem::is_directory(nested));
    // 이미 있어도 성공이다.
    REQUIRE(files.create_directories(nested.u8string()));

    const std::filesystem::path file { nested / L"20260821-184012.log" };
    REQUIRE_FALSE(files.file_exists(file.u8string()));
    REQUIRE(files.append_file(file.u8string(), u8"첫 줄\r\n"));
    REQUIRE(files.file_exists(file.u8string()));
    REQUIRE(files.append_file(file.u8string(), u8"둘째 줄\r\n"));

    std::ifstream stream { file, std::ios::binary };
    const std::string content { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    stream.close();
    const std::u8string bytes { reinterpret_cast<const char8_t*>(content.data()), content.size() };
    REQUIRE(bytes == u8"첫 줄\r\n둘째 줄\r\n");

    std::filesystem::remove_all(root, error);
}

#include "application/logic_controller.h"
#include "domain/app_settings.h"
#include "infrastructure/json_app_settings_store.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    constexpr std::u8string_view settings_path { u8"C:\\apps\\gitman\\gitman.app-settings.json" };

    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    // 앱 설정 파일 하나만 담는 대역이다. 읽기 실패와 저장 실패를 주입할 수 있다.
    class fake_app_settings_file_system final : public gitman::workspace_document_file_system
    {
    public:
        [[nodiscard]] gitman::workspace_file_read_result read(const std::u8string_view path) noexcept override
        {
            gitman::workspace_file_read_result result {};
            if (path != settings_path)
            {
                result.state = gitman::workspace_file_read_state::not_found;
                return result;
            }
            if (read_fails_)
            {
                result.state = gitman::workspace_file_read_state::failed;
                return result;
            }
            if (bytes_.has_value() == false)
            {
                result.state = gitman::workspace_file_read_state::not_found;
                return result;
            }

            result.state = gitman::workspace_file_read_state::available;
            result.bytes = *bytes_;
            return result;
        }

        [[nodiscard]] gitman::workspace_file_commit_result atomic_commit(const std::u8string_view document_path, const std::u8string_view bytes, const bool replace_existing) noexcept override
        {
            last_replace_existing_ = replace_existing;
            ++commit_count_;
            gitman::workspace_file_commit_result result {};
            if (commit_fails_)
            {
                result.failure = gitman::workspace_file_commit_failure::write;
                return result;
            }
            if (document_path == settings_path)
                bytes_ = std::u8string { bytes };
            return result;
        }

        void set_bytes(const std::u8string_view value)
        {
            bytes_ = std::u8string { value };
        }

        [[nodiscard]] const std::optional<std::u8string>& bytes() const noexcept
        {
            return bytes_;
        }

        void fail_reads() noexcept
        {
            read_fails_ = true;
        }

        void fail_commits() noexcept
        {
            commit_fails_ = true;
        }

        [[nodiscard]] std::size_t commit_count() const noexcept
        {
            return commit_count_;
        }

        [[nodiscard]] bool last_replace_existing() const noexcept
        {
            return last_replace_existing_;
        }

    private:
        std::optional<std::u8string> bytes_ {};
        bool read_fails_ { false };
        bool commit_fails_ { false };
        bool last_replace_existing_ { false };
        std::size_t commit_count_ { 0 };
    };

    class recording_submitter final : public gitman::operation_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::operation_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        [[nodiscard]] const gitman::operation_request* last_of(const gitman::operation_kind kind) const noexcept
        {
            for (std::size_t index = requests.size(); index > 0; --index)
                if (requests[index - 1].kind == kind)
                    return &requests[index - 1];
            return nullptr;
        }

        [[nodiscard]] std::size_t count_of(const gitman::operation_kind kind) const noexcept
        {
            std::size_t count { 0 };
            for (const gitman::operation_request& request : requests)
                if (request.kind == kind)
                    ++count;
            return count;
        }

        std::vector<gitman::operation_request> requests {};
    };

    gitman::document_loaded_event make_loaded_document()
    {
        gitman::document_loaded_event event {};
        gitman::workspace_document document {};
        document.document_path = u8"D:\\workspaces\\team.version-list";
        event.document = { std::move(document) };
        return event;
    }

    // start()가 제출한 읽기 요청에 대한 응답이다. 파일이 없는 상태를 흉내 낸다.
    gitman::app_settings_loaded_event make_loaded_settings(const std::uint64_t operation_id, gitman::app_settings settings = {})
    {
        gitman::app_settings_loaded_event event {};
        event.operation_id = operation_id;
        event.settings = std::move(settings);
        return event;
    }
} // namespace

TEST_CASE("Recent document keys normalize separators, case and the trailing separator", "[app-settings][domain]")
{
    REQUIRE(u8_equal(gitman::recent_document_key(u8"D:/Workspaces/Team.version-list"), u8"d:\\workspaces\\team.version-list"));
    REQUIRE(u8_equal(gitman::recent_document_key(u8"D:\\workspaces\\"), u8"d:\\workspaces"));
    // 뿌리는 마지막 구분자가 경로의 일부다.
    REQUIRE(u8_equal(gitman::recent_document_key(u8"C:\\"), u8"c:\\"));
}

TEST_CASE("Recent document display names drop the document extension", "[app-settings][domain]")
{
    REQUIRE(u8_equal(gitman::recent_document_display_name(u8"D:\\workspaces\\team.version-list"), u8"team"));
    REQUIRE(u8_equal(gitman::recent_document_display_name(u8"D:/workspaces/TEAM.VERSION-LIST"), u8"TEAM"));
    REQUIRE(u8_equal(gitman::recent_document_display_name(u8"D:\\workspaces\\notes.txt"), u8"notes.txt"));
}

TEST_CASE("Touching a recent document moves it to the front and keeps the capacity", "[app-settings][domain]")
{
    gitman::app_settings settings {};
    for (std::size_t index = 0; index < gitman::recent_document_capacity + 3; ++index)
    {
        const std::u8string path { std::u8string { u8"D:\\ws\\doc" } + static_cast<char8_t>(u8'a' + index) + std::u8string { u8".version-list" } };
        gitman::touch_recent_document(settings, path, u8"2026-08-21T00:00:00Z");
    }

    REQUIRE(settings.recent_documents.size() == gitman::recent_document_capacity);
    // 마지막에 올린 항목이 맨 앞이고 가장 오래된 항목은 버려졌다.
    REQUIRE(u8_equal(settings.recent_documents.front().display_name, u8"docm"));

    gitman::touch_recent_document(settings, u8"d:/ws/DOCM.version-list", u8"2026-08-22T00:00:00Z");
    REQUIRE(settings.recent_documents.size() == gitman::recent_document_capacity);
    REQUIRE(u8_equal(settings.recent_documents.front().path, u8"d:/ws/DOCM.version-list"));
    REQUIRE(u8_equal(settings.recent_documents.front().opened_at, u8"2026-08-22T00:00:00Z"));

    REQUIRE(gitman::remove_recent_document(settings, u8"D:\\ws\\docm.version-list"));
    REQUIRE(settings.recent_documents.size() == gitman::recent_document_capacity - 1);
    REQUIRE(gitman::remove_recent_document(settings, u8"D:\\ws\\missing.version-list") == false);
}

TEST_CASE("UTC timestamps are formatted as ISO 8601 seconds", "[app-settings][domain]")
{
    const std::chrono::system_clock::time_point epoch {};
    const std::u8string text { gitman::format_utc_timestamp(epoch + std::chrono::seconds { 0 }) };
    REQUIRE(text.size() == 20);
    REQUIRE(text.back() == u8'Z');
    REQUIRE(text[4] == u8'-');
    REQUIRE(text[10] == u8'T');
}

TEST_CASE("App settings JSON round trips and preserves unknown keys", "[app-settings][json]")
{
    const std::u8string source {
        u8"{\r\n    \"schema_version\": 1,\r\n    \"future_option\": true,\r\n    \"recent_documents\": [\r\n        {\r\n            \"path\": \"D:\\\\ws\\\\team.version-list\",\r\n"
        u8"            \"display_name\": \"team\",\r\n            \"opened_at\": \"2026-08-21T10:00:00Z\"\r\n        }\r\n    ]\r\n}\r\n",
    };

    const gitman::app_settings_load_result parsed { gitman::parse_app_settings_json(source, settings_path) };
    REQUIRE(parsed.diagnostics.empty());
    REQUIRE(parsed.settings.recent_documents.size() == 1);
    REQUIRE(u8_equal(parsed.settings.recent_documents.front().path, u8"D:\\ws\\team.version-list"));
    REQUIRE(u8_equal(parsed.settings.recent_documents.front().display_name, u8"team"));

    const std::u8string serialized { gitman::serialize_app_settings_json(parsed.settings, parsed.shadow_source_json) };
    REQUIRE(serialized.find(u8"future_option") != std::u8string::npos);

    const gitman::app_settings_load_result reparsed { gitman::parse_app_settings_json(serialized, settings_path) };
    REQUIRE(reparsed.settings.recent_documents == parsed.settings.recent_documents);
}

TEST_CASE("The app settings window placement round trips and ignores broken values", "[app-settings][json]")
{
    // 앱 단위 창 배치다 (global-settings-and-ui-fixes-design G1). 문서의 window와
    // 같은 규칙으로 왕복하고, 깨진 값은 경고만 남기고 무시한다.
    gitman::app_settings settings {};
    gitman::window_placement placement {};
    placement.x = 320;
    placement.y = 180;
    placement.width = 1280;
    placement.height = 720;
    placement.maximized = true;
    settings.window = { placement };

    const std::u8string serialized { gitman::serialize_app_settings_json(settings, {}) };
    const gitman::app_settings_load_result reparsed { gitman::parse_app_settings_json(serialized, settings_path) };
    REQUIRE(reparsed.diagnostics.empty());
    REQUIRE(reparsed.settings.window.has_value());
    REQUIRE(*reparsed.settings.window == placement);

    // 값이 없으면 window 필드를 만들지 않고, shadow에 이미 있던 값은 지우지 않는다.
    const std::u8string without { gitman::serialize_app_settings_json({}, {}) };
    REQUIRE(without.find(u8"\"window\"") == std::u8string::npos);
    const std::u8string preserved { gitman::serialize_app_settings_json({}, serialized) };
    REQUIRE(preserved.find(u8"\"window\"") != std::u8string::npos);

    // 크기가 0이거나 형식이 다르면 배치 없이 열고 경고를 남긴다.
    const gitman::app_settings_load_result zero { gitman::parse_app_settings_json(u8"{ \"window\": { \"x\": 0, \"y\": 0, \"width\": 0, \"height\": 400 } }", settings_path) };
    REQUIRE(zero.settings.window.has_value() == false);
    REQUIRE(zero.diagnostics.size() == 1);
    REQUIRE(zero.diagnostics.front().severity == gitman::diagnostic_severity::warning);

    const gitman::app_settings_load_result wrong { gitman::parse_app_settings_json(u8"{ \"window\": { \"x\": \"left\" } }", settings_path) };
    REQUIRE(wrong.settings.window.has_value() == false);
    REQUIRE(wrong.diagnostics.size() == 1);
}

TEST_CASE("Global settings in the app settings file round trip with validation", "[app-settings][json]")
{
    // 앱 단위 전역 설정이다 (G3.1). 저장은 모든 키를 기록하고, 읽기는 문서 파서와
    // 같은 검증을 경고로만 적용한다.
    gitman::app_settings settings {};
    settings.settings.git_executable = u8"C:\\tools\\git\\git.exe";
    settings.settings.show_relative_paths = true;
    settings.settings.write_log_files = false;
    settings.settings.query_timeout_seconds = { 900 };

    const std::u8string serialized { gitman::serialize_app_settings_json(settings, {}) };
    const gitman::app_settings_load_result reparsed { gitman::parse_app_settings_json(serialized, settings_path) };
    REQUIRE(reparsed.diagnostics.empty());
    REQUIRE(reparsed.settings.settings == settings.settings);

    // 잘못된 값(상대 경로·boolean 아님·범위 밖)은 경고만 남기고 그 필드만
    // 기본값을 쓴다.
    const gitman::app_settings_load_result invalid {
        gitman::parse_app_settings_json(u8"{ \"settings\": { \"git_executable\": \"git.exe\", \"update_submodules\": \"yes\", \"query_timeout_seconds\": 5 } }", settings_path),
    };
    REQUIRE(invalid.settings.settings == gitman::workspace_settings {});
    REQUIRE(invalid.diagnostics.size() == 3);
    for (const gitman::diagnostic& value : invalid.diagnostics)
        REQUIRE(value.severity == gitman::diagnostic_severity::warning);
}

TEST_CASE("A broken app settings file falls back to defaults with a warning", "[app-settings][json]")
{
    const gitman::app_settings_load_result broken { gitman::parse_app_settings_json(u8"not json", settings_path) };
    REQUIRE(broken.settings.recent_documents.empty());
    REQUIRE(broken.diagnostics.size() == 1);
    REQUIRE(broken.diagnostics.front().severity == gitman::diagnostic_severity::warning);
    REQUIRE(broken.shadow_source_json.empty());

    const gitman::app_settings_load_result wrong_type { gitman::parse_app_settings_json(u8"{ \"recent_documents\": 3 }", settings_path) };
    REQUIRE(wrong_type.settings.recent_documents.empty());
    REQUIRE(wrong_type.diagnostics.size() == 1);

    // 경로가 없는 항목은 버리고 나머지는 남긴다.
    const gitman::app_settings_load_result partial {
        gitman::parse_app_settings_json(u8"{ \"recent_documents\": [ { \"display_name\": \"x\" }, { \"path\": \"D:\\\\ws\\\\a.version-list\" } ] }", settings_path),
    };

    REQUIRE(partial.settings.recent_documents.size() == 1);
    REQUIRE(u8_equal(partial.settings.recent_documents.front().display_name, u8"a"));
}

TEST_CASE("The app settings store reads a missing file as defaults and writes atomically", "[app-settings][store]")
{
    fake_app_settings_file_system file_system {};
    gitman::json_app_settings_store store { file_system };

    const gitman::app_settings_load_result missing { store.load(settings_path) };
    REQUIRE(missing.diagnostics.empty());
    REQUIRE(missing.settings.recent_documents.empty());

    gitman::app_settings settings {};
    gitman::touch_recent_document(settings, u8"D:\\ws\\team.version-list", u8"2026-08-21T10:00:00Z");
    const gitman::app_settings_save_result saved { store.save(settings_path, settings, {}) };
    REQUIRE(saved.succeeded);
    REQUIRE(file_system.last_replace_existing() == false);
    REQUIRE(file_system.bytes().has_value());

    const gitman::app_settings_load_result reloaded { store.load(settings_path) };
    REQUIRE(reloaded.settings.recent_documents.size() == 1);
    REQUIRE(u8_equal(reloaded.settings.recent_documents.front().display_name, u8"team"));

    // 이미 파일이 있으면 교체 저장이다.
    REQUIRE(store.save(settings_path, settings, reloaded.shadow_source_json).succeeded);
    REQUIRE(file_system.last_replace_existing());
}

TEST_CASE("Read and write failures become warnings instead of errors", "[app-settings][store]")
{
    fake_app_settings_file_system file_system {};
    file_system.fail_reads();
    gitman::json_app_settings_store store { file_system };

    const gitman::app_settings_load_result failed { store.load(settings_path) };
    REQUIRE(failed.settings.recent_documents.empty());
    REQUIRE(failed.diagnostics.size() == 1);
    REQUIRE(failed.diagnostics.front().code == gitman::diagnostic_code::app_settings_read_failed);

    fake_app_settings_file_system write_only {};
    write_only.fail_commits();
    gitman::json_app_settings_store write_store { write_only };
    const gitman::app_settings_save_result save_failure { write_store.save(settings_path, {}, {}) };
    REQUIRE(save_failure.succeeded == false);
    REQUIRE(save_failure.diagnostics.size() == 1);
    REQUIRE(save_failure.diagnostics.front().code == gitman::diagnostic_code::app_settings_write_failed);
}

TEST_CASE("Starting the controller loads the app settings once", "[logic][app-settings]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };

    controller.start();
    controller.start();
    REQUIRE(submitter.count_of(gitman::operation_kind::load_app_settings) == 1);
}

TEST_CASE("Opening a document records it in the recent list after the settings arrive", "[logic][app-settings]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };

    controller.start();
    const std::uint64_t load_id { submitter.requests.front().operation_id };

    // 읽기가 끝나기 전에 문서를 열어도 저장은 나가지 않는다. 파일 내용을 지우지
    // 않도록 결과를 기다린다.
    controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\workspaces\\team.version-list" } });
    controller.handle(gitman::logic_message { make_loaded_document() });
    REQUIRE(submitter.count_of(gitman::operation_kind::save_app_settings) == 0);

    gitman::app_settings stored {};
    gitman::touch_recent_document(stored, u8"E:\\other\\legacy.version-list", u8"2026-08-01T00:00:00Z");
    controller.handle(gitman::logic_message { make_loaded_settings(load_id, stored) });

    const gitman::operation_request* const save { submitter.last_of(gitman::operation_kind::save_app_settings) };
    REQUIRE(save != nullptr);
    REQUIRE(save->app_settings_payload.has_value());
    REQUIRE(save->app_settings_payload->recent_documents.size() == 2);
    // 방금 연 문서가 파일에 있던 항목보다 앞이다.
    REQUIRE(u8_equal(save->app_settings_payload->recent_documents.front().path, u8"D:\\workspaces\\team.version-list"));
    REQUIRE(u8_equal(save->app_settings_payload->recent_documents.back().path, u8"E:\\other\\legacy.version-list"));
}

TEST_CASE("Recent list changes merge into a single pending save", "[logic][app-settings]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };

    controller.start();
    const std::uint64_t load_id { submitter.requests.front().operation_id };
    controller.handle(gitman::logic_message { make_loaded_settings(load_id) });

    controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\workspaces\\team.version-list" } });
    controller.handle(gitman::logic_message { make_loaded_document() });
    REQUIRE(submitter.count_of(gitman::operation_kind::save_app_settings) == 1);

    // 저장이 끝나기 전의 두 번째 변경은 한 번으로 합쳐진다.
    controller.handle(gitman::logic_message { gitman::remove_recent_document_intent { u8"D:\\workspaces\\team.version-list" } });
    REQUIRE(submitter.count_of(gitman::operation_kind::save_app_settings) == 1);

    gitman::app_settings_saved_event saved {};
    saved.operation_id = submitter.last_of(gitman::operation_kind::save_app_settings)->operation_id;
    saved.succeeded = true;
    saved.shadow_source_json = u8"{}";
    controller.handle(gitman::logic_message { saved });

    REQUIRE(submitter.count_of(gitman::operation_kind::save_app_settings) == 2);
    const gitman::operation_request* const second { submitter.last_of(gitman::operation_kind::save_app_settings) };
    REQUIRE(second->app_settings_payload->recent_documents.empty());
    REQUIRE(u8_equal(second->app_settings_shadow, u8"{}"));
}

TEST_CASE("The closing window placement lands in the app settings save at shutdown", "[logic][app-settings]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };

    controller.start();
    const std::uint64_t load_id { submitter.requests.front().operation_id };
    controller.handle(gitman::logic_message { make_loaded_settings(load_id) });

    // 문서 없이(시작 페이지) 닫아도 배치는 앱 설정에 남는다 (G1).
    gitman::window_placement placement {};
    placement.x = 10;
    placement.y = 20;
    placement.width = 800;
    placement.height = 600;
    controller.handle(gitman::logic_message { gitman::window_placement_intent { placement } });
    // 배치만으로는 즉시 저장하지 않는다. 종료 처리에서 한 번 나간다.
    REQUIRE(submitter.count_of(gitman::operation_kind::save_app_settings) == 0);

    controller.handle(gitman::logic_message { gitman::close_intent {} });
    const gitman::operation_request* const save { submitter.last_of(gitman::operation_kind::save_app_settings) };
    REQUIRE(save != nullptr);
    REQUIRE(save->app_settings_payload.has_value());
    REQUIRE(save->app_settings_payload->window.has_value());
    REQUIRE(*save->app_settings_payload->window == placement);
}

TEST_CASE("The app settings placement restores the window only when nothing was applied", "[logic][app-settings]")
{
    gitman::window_placement stored {};
    stored.x = 320;
    stored.y = 180;
    stored.width = 1280;
    stored.height = 720;

    SECTION("문서가 없으면 앱 설정의 마지막 배치를 1회 요청한다")
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };
        controller.start();
        const std::uint64_t load_id { submitter.requests.front().operation_id };

        gitman::app_settings settings {};
        settings.window = { stored };
        controller.handle(gitman::logic_message { make_loaded_settings(load_id, std::move(settings)) });

        const std::shared_ptr<const gitman::view_snapshot> view { controller.make_view_snapshot() };
        REQUIRE(view->window_placement_request.has_value());
        REQUIRE(*view->window_placement_request == stored);
        REQUIRE(view->window_placement_revision == 1);
    }

    SECTION("문서가 자기 배치를 이미 적용했으면 문서가 우선이다")
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };
        controller.start();
        const std::uint64_t load_id { submitter.requests.front().operation_id };

        gitman::window_placement document_placement {};
        document_placement.x = 1;
        document_placement.y = 2;
        document_placement.width = 640;
        document_placement.height = 480;

        controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\workspaces\\team.version-list" } });
        gitman::document_loaded_event loaded { make_loaded_document() };
        loaded.document->window = { document_placement };
        controller.handle(gitman::logic_message { std::move(loaded) });

        gitman::app_settings settings {};
        settings.window = { stored };
        controller.handle(gitman::logic_message { make_loaded_settings(load_id, std::move(settings)) });

        const std::shared_ptr<const gitman::view_snapshot> view { controller.make_view_snapshot() };
        REQUIRE(view->window_placement_request.has_value());
        REQUIRE(*view->window_placement_request == document_placement);
        REQUIRE(view->window_placement_revision == 1);
    }
}

TEST_CASE("A failing app settings save is reported once", "[logic][app-settings]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };

    controller.start();
    const std::uint64_t load_id { submitter.requests.front().operation_id };
    controller.handle(gitman::logic_message { make_loaded_settings(load_id) });
    controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\workspaces\\team.version-list" } });
    controller.handle(gitman::logic_message { make_loaded_document() });

    const auto fail_save = [&submitter, &controller]() {
        gitman::app_settings_saved_event failure {};
        failure.operation_id = submitter.last_of(gitman::operation_kind::save_app_settings)->operation_id;
        gitman::diagnostic value {};
        value.code = gitman::diagnostic_code::app_settings_write_failed;
        value.severity = gitman::diagnostic_severity::warning;
        value.message = u8"앱 설정을 저장하지 못했습니다.";
        failure.diagnostics.push_back(std::move(value));
        controller.handle(gitman::logic_message { std::move(failure) });
    };

    fail_save();
    std::shared_ptr<const gitman::view_snapshot> view { controller.make_view_snapshot() };
    REQUIRE(view->notices.size() == 1);

    controller.handle(gitman::logic_message { gitman::remove_recent_document_intent { u8"D:\\workspaces\\team.version-list" } });
    fail_save();
    view = controller.make_view_snapshot();
    REQUIRE(view->notices.size() == 1);
}

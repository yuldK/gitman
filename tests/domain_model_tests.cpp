#include "domain/diagnostic.h"
#include "domain/operation.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    struct diagnostic_name_case
    {
        gitman::diagnostic_code code { gitman::diagnostic_code::unknown };
        std::u8string_view name {};
    };
} // namespace

TEST_CASE("Workspace documents expose approved defaults", "[domain][project]")
{
    REQUIRE(gitman::current_workspace_schema_version == 1);
    REQUIRE(u8_equal(gitman::workspace_document_extension, u8".version-list"));

    const gitman::workspace_document document {};
    REQUIRE(document.schema_version == gitman::current_workspace_schema_version);
    REQUIRE(document.document_path.empty());
    REQUIRE(document.projects.empty());

    // `settings`가 없는 문서는 전부 기본값이며 실행 파일은 자동 탐색한다.
    REQUIRE(document.settings.is_default());
    REQUIRE(document.settings.git_executable.empty());
    REQUIRE(document.settings.svn_executable.empty());
    REQUIRE(document.settings == gitman::workspace_settings {});

    gitman::workspace_settings configured {};
    configured.git_executable = u8"C:/Program Files/Git/cmd/git.exe";
    REQUIRE_FALSE(configured.is_default());
    REQUIRE_FALSE(configured == gitman::workspace_settings {});

    const gitman::project_definition project {};
    REQUIRE(project.id.value.empty());
    REQUIRE(project.path.original.empty());
    REQUIRE(project.path.normalized.empty());
    REQUIRE(project.path.state == gitman::configured_path_state::unchecked);
    REQUIRE(project.display_name.empty());
    REQUIRE(project.hint == gitman::vcs_hint::automatic);
    REQUIRE(project.enabled);
    REQUIRE_FALSE(project.preferred_remote.has_value());
    REQUIRE(project.svn_switch_targets.empty());
}

TEST_CASE("Project identifiers and VCS hints have stable contracts", "[domain][project]")
{
    const gitman::project_id first { u8"project-one" };
    const gitman::project_id same { u8"project-one" };
    const gitman::project_id different { u8"project-two" };
    REQUIRE(first == same);
    REQUIRE_FALSE(first == different);

    const auto automatic { gitman::parse_vcs_hint(u8"auto") };
    const auto git { gitman::parse_vcs_hint(u8"git") };
    const auto subversion { gitman::parse_vcs_hint(u8"svn") };
    REQUIRE(automatic.has_value());
    REQUIRE(git.has_value());
    REQUIRE(subversion.has_value());
    REQUIRE(*automatic == gitman::vcs_hint::automatic);
    REQUIRE(*git == gitman::vcs_hint::git);
    REQUIRE(*subversion == gitman::vcs_hint::subversion);
    REQUIRE_FALSE(gitman::parse_vcs_hint(u8"SVN").has_value());
    REQUIRE_FALSE(gitman::parse_vcs_hint(u8"unknown").has_value());

    REQUIRE(u8_equal(gitman::vcs_hint_name(gitman::vcs_hint::automatic), u8"auto"));
    REQUIRE(u8_equal(gitman::vcs_hint_name(gitman::vcs_hint::git), u8"git"));
    REQUIRE(u8_equal(gitman::vcs_hint_name(gitman::vcs_hint::subversion), u8"svn"));
    REQUIRE(u8_equal(gitman::vcs_hint_name(static_cast<gitman::vcs_hint>(-1)), u8"unknown"));
}

TEST_CASE("Configured path states have stable names", "[domain][project]")
{
    REQUIRE(u8_equal(gitman::configured_path_state_name(gitman::configured_path_state::unchecked), u8"unchecked"));
    REQUIRE(u8_equal(gitman::configured_path_state_name(gitman::configured_path_state::available), u8"available"));
    REQUIRE(u8_equal(gitman::configured_path_state_name(gitman::configured_path_state::missing), u8"missing"));
    REQUIRE(u8_equal(gitman::configured_path_state_name(gitman::configured_path_state::inaccessible), u8"inaccessible"));
    REQUIRE(u8_equal(gitman::configured_path_state_name(gitman::configured_path_state::not_directory), u8"not_directory"));
    REQUIRE(u8_equal(gitman::configured_path_state_name(gitman::configured_path_state::invalid), u8"invalid"));
}

TEST_CASE("Repository snapshots expose neutral defaults and stable names", "[domain][repository]")
{
    const gitman::repository_snapshot snapshot {};
    REQUIRE(snapshot.project.value.empty());
    REQUIRE(snapshot.kind == gitman::repository_kind::unknown);
    REQUIRE(snapshot.repository_root.empty());
    REQUIRE(snapshot.current_reference.empty());
    REQUIRE(snapshot.local_revision.empty());
    REQUIRE(snapshot.comparison == gitman::comparison_source::none);
    REQUIRE(snapshot.comparison_target.empty());
    REQUIRE(snapshot.sync_state == gitman::remote_sync_state::unknown);
    REQUIRE(snapshot.ahead_count == 0);
    REQUIRE(snapshot.behind_count == 0);
    REQUIRE(snapshot.working_tree.state == gitman::working_tree_state::unknown);
    REQUIRE(snapshot.working_tree.modified_count == 0);
    REQUIRE(snapshot.working_tree.untracked_count == 0);
    REQUIRE(snapshot.working_tree.conflicted_count == 0);
    REQUIRE_FALSE(snapshot.local_checked_at.has_value());
    REQUIRE_FALSE(snapshot.remote_checked_at.has_value());

    REQUIRE(u8_equal(gitman::repository_kind_name(gitman::repository_kind::unknown), u8"unknown"));
    REQUIRE(u8_equal(gitman::repository_kind_name(gitman::repository_kind::git), u8"git"));
    REQUIRE(u8_equal(gitman::repository_kind_name(gitman::repository_kind::subversion), u8"svn"));
    REQUIRE(u8_equal(gitman::comparison_source_name(gitman::comparison_source::none), u8"none"));
    REQUIRE(u8_equal(gitman::comparison_source_name(gitman::comparison_source::remote), u8"remote"));
    REQUIRE(u8_equal(gitman::comparison_source_name(gitman::comparison_source::local), u8"local"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::unknown), u8"unknown"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::up_to_date), u8"up_to_date"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::behind), u8"behind"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::ahead), u8"ahead"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::diverged), u8"diverged"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::local_only), u8"local_only"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::remote_target_missing), u8"remote_target_missing"));
    // ADR-003이 요구한 대로 인증 실패를 네트워크 단절과 구분한다.
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::authentication_required), u8"authentication_required"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::offline), u8"offline"));
    REQUIRE(u8_equal(gitman::remote_sync_state_name(gitman::remote_sync_state::error), u8"error"));
    REQUIRE(u8_equal(gitman::working_tree_state_name(gitman::working_tree_state::unknown), u8"unknown"));
    REQUIRE(u8_equal(gitman::working_tree_state_name(gitman::working_tree_state::clean), u8"clean"));
    REQUIRE(u8_equal(gitman::working_tree_state_name(gitman::working_tree_state::modified), u8"modified"));
    REQUIRE(u8_equal(gitman::working_tree_state_name(gitman::working_tree_state::conflicted), u8"conflicted"));
}

TEST_CASE("Operations expose stable identifiers states and names", "[domain][operation]")
{
    const gitman::operation_id first { u8"operation-one" };
    const gitman::operation_id same { u8"operation-one" };
    const gitman::operation_id different { u8"operation-two" };
    REQUIRE(first == same);
    REQUIRE_FALSE(first == different);

    const gitman::operation_descriptor operation {};
    REQUIRE(operation.id.value.empty());
    REQUIRE(operation.project.value.empty());
    REQUIRE(operation.kind == gitman::operation_kind::refresh);
    REQUIRE(operation.state == gitman::operation_state::queued);

    REQUIRE(u8_equal(gitman::operation_kind_name(gitman::operation_kind::refresh), u8"refresh"));
    REQUIRE(u8_equal(gitman::operation_kind_name(gitman::operation_kind::update), u8"update"));
    REQUIRE(u8_equal(gitman::operation_kind_name(gitman::operation_kind::switch_target), u8"switch"));
    REQUIRE(u8_equal(gitman::operation_state_name(gitman::operation_state::queued), u8"queued"));
    REQUIRE(u8_equal(gitman::operation_state_name(gitman::operation_state::running), u8"running"));
    REQUIRE(u8_equal(gitman::operation_state_name(gitman::operation_state::succeeded), u8"succeeded"));
    REQUIRE(u8_equal(gitman::operation_state_name(gitman::operation_state::failed), u8"failed"));
    REQUIRE(u8_equal(gitman::operation_state_name(gitman::operation_state::cancelled), u8"cancelled"));
}

TEST_CASE("Diagnostics expose structured defaults and stable names", "[domain][diagnostic]")
{
    const gitman::diagnostic value {};
    REQUIRE(value.code == gitman::diagnostic_code::unknown);
    REQUIRE(value.severity == gitman::diagnostic_severity::error);
    REQUIRE(value.message.empty());
    REQUIRE(value.source.document_path.empty());
    REQUIRE(value.source.json_pointer.empty());
    REQUIRE_FALSE(value.source.project_index.has_value());
    REQUIRE_FALSE(value.source.project_id.has_value());
    REQUIRE_FALSE(value.native_error.has_value());

    REQUIRE(u8_equal(gitman::diagnostic_severity_name(gitman::diagnostic_severity::information), u8"information"));
    REQUIRE(u8_equal(gitman::diagnostic_severity_name(gitman::diagnostic_severity::warning), u8"warning"));
    REQUIRE(u8_equal(gitman::diagnostic_severity_name(gitman::diagnostic_severity::error), u8"error"));

    constexpr std::array diagnostic_names {
        diagnostic_name_case { gitman::diagnostic_code::unknown, u8"unknown" },
        diagnostic_name_case { gitman::diagnostic_code::malformed_document, u8"malformed_document" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_document_root, u8"invalid_document_root" },
        diagnostic_name_case { gitman::diagnostic_code::missing_schema_version, u8"missing_schema_version" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_schema_version, u8"invalid_schema_version" },
        diagnostic_name_case { gitman::diagnostic_code::unsupported_legacy_schema, u8"unsupported_legacy_schema" },
        diagnostic_name_case { gitman::diagnostic_code::unsupported_future_schema, u8"unsupported_future_schema" },
        diagnostic_name_case { gitman::diagnostic_code::missing_projects, u8"missing_projects" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_projects, u8"invalid_projects" },
        diagnostic_name_case { gitman::diagnostic_code::missing_project_field, u8"missing_project_field" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_project_field, u8"invalid_project_field" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_project_id, u8"invalid_project_id" },
        diagnostic_name_case { gitman::diagnostic_code::duplicate_project_id, u8"duplicate_project_id" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_project_path, u8"invalid_project_path" },
        diagnostic_name_case { gitman::diagnostic_code::duplicate_project_path, u8"duplicate_project_path" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_vcs_hint, u8"invalid_vcs_hint" },
        diagnostic_name_case { gitman::diagnostic_code::unknown_field, u8"unknown_field" },
        diagnostic_name_case { gitman::diagnostic_code::path_missing, u8"path_missing" },
        diagnostic_name_case { gitman::diagnostic_code::path_inaccessible, u8"path_inaccessible" },
        diagnostic_name_case { gitman::diagnostic_code::path_not_directory, u8"path_not_directory" },
        diagnostic_name_case { gitman::diagnostic_code::document_not_found, u8"document_not_found" },
        diagnostic_name_case { gitman::diagnostic_code::document_read_failed, u8"document_read_failed" },
        diagnostic_name_case { gitman::diagnostic_code::document_write_failed, u8"document_write_failed" },
        diagnostic_name_case { gitman::diagnostic_code::document_flush_failed, u8"document_flush_failed" },
        diagnostic_name_case { gitman::diagnostic_code::document_replace_failed, u8"document_replace_failed" },
        diagnostic_name_case { gitman::diagnostic_code::concurrent_modification, u8"concurrent_modification" },
        diagnostic_name_case { gitman::diagnostic_code::repository_unavailable, u8"repository_unavailable" },
        diagnostic_name_case { gitman::diagnostic_code::operation_failed, u8"operation_failed" },
        diagnostic_name_case { gitman::diagnostic_code::invalid_process_request, u8"invalid_process_request" },
        diagnostic_name_case { gitman::diagnostic_code::process_start_failed, u8"process_start_failed" },
        diagnostic_name_case { gitman::diagnostic_code::process_pipe_failed, u8"process_pipe_failed" },
        diagnostic_name_case { gitman::diagnostic_code::process_timed_out, u8"process_timed_out" },
        diagnostic_name_case { gitman::diagnostic_code::process_cancelled, u8"process_cancelled" },
        diagnostic_name_case { gitman::diagnostic_code::process_output_truncated, u8"process_output_truncated" },
        diagnostic_name_case { gitman::diagnostic_code::vcs_tool_not_found, u8"vcs_tool_not_found" },
        diagnostic_name_case { gitman::diagnostic_code::vcs_tool_too_old, u8"vcs_tool_too_old" },
        diagnostic_name_case { gitman::diagnostic_code::vcs_tool_version_unreadable, u8"vcs_tool_version_unreadable" },
        diagnostic_name_case { gitman::diagnostic_code::vcs_tool_path_invalid, u8"vcs_tool_path_invalid" },
        diagnostic_name_case { gitman::diagnostic_code::vcs_command_failed, u8"vcs_command_failed" },
        diagnostic_name_case { gitman::diagnostic_code::vcs_output_unparsable, u8"vcs_output_unparsable" },
        diagnostic_name_case { gitman::diagnostic_code::authentication_required, u8"authentication_required" },
        diagnostic_name_case { gitman::diagnostic_code::remote_unreachable, u8"remote_unreachable" },
        diagnostic_name_case { gitman::diagnostic_code::repository_not_found, u8"repository_not_found" },
        diagnostic_name_case { gitman::diagnostic_code::update_blocked, u8"update_blocked" },
        diagnostic_name_case { gitman::diagnostic_code::switch_target_rejected, u8"switch_target_rejected" },
    };
    for (const auto& diagnostic_name : diagnostic_names)
        REQUIRE(u8_equal(gitman::diagnostic_code_name(diagnostic_name.code), diagnostic_name.name));
}

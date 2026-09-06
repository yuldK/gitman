set(gitman_resource_directory "${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/resources")
set(gitman_manifest_file "${gitman_resource_directory}/gitman.manifest")
set(gitman_resource_ids_file "${gitman_resource_directory}/resource_ids.h")
set(gitman_resource_template "${gitman_resource_directory}/gitman.rc.in")
set(gitman_app_icon "${CMAKE_CURRENT_SOURCE_DIR}/../assets/gitman.ico")

file(TO_CMAKE_PATH "${gitman_manifest_file}" GITMAN_MANIFEST_PATH)
file(TO_CMAKE_PATH "${gitman_resource_ids_file}" GITMAN_RESOURCE_IDS_PATH)
file(TO_CMAKE_PATH "${gitman_app_icon}" GITMAN_APP_ICON_PATH)
configure_file(
    "${gitman_resource_template}"
    "${GITMAN_GENERATED_RESOURCE}"
    @ONLY)
set_source_files_properties(
    "${GITMAN_GENERATED_RESOURCE}"
    PROPERTIES
        GENERATED TRUE
        OBJECT_DEPENDS "${gitman_app_icon}")

# 앱 코드 전체가 하나의 static library다. 계층(domain → application →
# infrastructure → presentation → platform)은 디렉터리 구조와 코드 리뷰로 지키며,
# target 경계로는 나누지 않는다. 프로젝트 규모에 비해 target이 지나치게 많아지는
# 것을 피하기 위한 결정이다 (docs/window-state-and-scroll-design.md 10장).
#
# UI 기반 계층(element·tree·상호작용·창·렌더러·TSF·클립보드·글꼴·messaging)은
# luil이 맡는다 (ADR-007). gitman은 앱 element와 driver·policy·delegate만 갖는다.
add_library(gitman_lib STATIC
    # 도메인: 순수 규칙과 값 타입이다. 다른 계층에 의존하지 않는다. 외양·텍스트
    # 편집·UTF-8 헤더는 luil의 값 타입을 `gitman::` 이름으로 전달한다.
    domain/app_settings.cpp
    domain/app_settings.h
    domain/appearance.h
    domain/diagnostic.cpp
    domain/diagnostic.h
    domain/discovery.cpp
    domain/discovery.h
    domain/local_changes.cpp
    domain/local_changes.h
    domain/log_file_naming.cpp
    domain/log_file_naming.h
    domain/operation.cpp
    domain/operation.h
    domain/operation_log.cpp
    domain/operation_log.h
    domain/path_syntax.cpp
    domain/path_syntax.h
    domain/process_execution.cpp
    domain/process_execution.h
    domain/project.cpp
    domain/project.h
    domain/repository_snapshot.cpp
    domain/repository_snapshot.h
    domain/text_edit.h
    domain/utf8_text.h
    domain/vcs_operation.cpp
    domain/vcs_operation.h
    domain/vcs_tool.cpp
    domain/vcs_tool.h

    # application: 상태 소유와 계약(interface)이다. OS API를 직접 부르지 않고 luil
    # UI 헤더를 include하지 않는다.
    application/app_commands.h
    application/app_messages.h
    application/app_settings_store.h
    application/application_options.cpp
    application/application_options.h
    application/cancellation_registry.cpp
    application/cancellation_registry.h
    application/directory_enumerator.h
    application/discovery_service.cpp
    application/discovery_service.h
    application/log_file_sink.h
    application/log_file_system.h
    application/logic_controller.cpp
    application/logic_controller.h
    application/operation_executor.h
    application/process_cancellation.cpp
    application/process_cancellation.h
    application/process_request.cpp
    application/process_request.h
    application/process_runner.h
    application/project_path_resolver.h
    application/project_registration_service.cpp
    application/project_registration_service.h
    application/project_store.cpp
    application/project_store.h
    application/repository_provider.cpp
    application/repository_provider.h
    application/svn_repository_browser.cpp
    application/svn_repository_browser.h
    application/switch_validation_service.cpp
    application/switch_validation_service.h
    application/task_scheduler.cpp
    application/task_scheduler.h
    application/text_transcoder.h
    application/vcs_file_probe.h
    application/vcs_tool_registry.cpp
    application/vcs_tool_registry.h
    application/version_list_generation_service.cpp
    application/version_list_generation_service.h
    application/workspace_document_file_system.h

    # infrastructure: application 계약의 구현이다 (JSON, VCS, 프로세스).
    infrastructure/command_line_builder.cpp
    infrastructure/command_line_builder.h
    infrastructure/file_association.cpp
    infrastructure/file_association.h
    infrastructure/file_log_writer.cpp
    infrastructure/file_log_writer.h
    infrastructure/git_command_builder.cpp
    infrastructure/git_command_builder.h
    infrastructure/git_repository_provider.cpp
    infrastructure/git_repository_provider.h
    infrastructure/git_status_parser.cpp
    infrastructure/git_status_parser.h
    infrastructure/local_change_reader.cpp
    infrastructure/local_change_reader.h
    infrastructure/json_app_settings_store.cpp
    infrastructure/json_app_settings_store.h
    infrastructure/json_project_store.cpp
    infrastructure/json_project_store.h
    infrastructure/json_workspace_document.cpp
    infrastructure/json_workspace_document.h
    infrastructure/process_output_pipeline.cpp
    infrastructure/process_output_pipeline.h
    infrastructure/secret_masking.cpp
    infrastructure/secret_masking.h
    infrastructure/svn_command_builder.cpp
    infrastructure/svn_command_builder.h
    infrastructure/svn_output_parser.cpp
    infrastructure/svn_output_parser.h
    infrastructure/svn_repository_provider.cpp
    infrastructure/svn_repository_provider.h
    infrastructure/vcs_command_runner.cpp
    infrastructure/vcs_command_runner.h
    infrastructure/vcs_error_classifier.cpp
    infrastructure/vcs_error_classifier.h
    infrastructure/vcs_execution_policy.cpp
    infrastructure/vcs_execution_policy.h
    infrastructure/vcs_operation_executor.cpp
    infrastructure/vcs_operation_executor.h
    infrastructure/vcs_tool_discovery.cpp
    infrastructure/vcs_tool_discovery.h
    infrastructure/vcs_version.cpp
    infrastructure/vcs_version.h
    infrastructure/workspace_document_paths.cpp
    infrastructure/workspace_document_paths.h

    # presentation/ui: view snapshot을 luil element tree로 짓는 앱 element 계층과
    # 입력 정책이다.
    presentation/ui/build_ui_tree.cpp
    presentation/ui/build_ui_tree.h
    presentation/ui/card_element.cpp
    presentation/ui/card_element.h
    presentation/ui/card_list_element.cpp
    presentation/ui/card_list_element.h
    presentation/ui/context_menu_element.cpp
    presentation/ui/context_menu_element.h
    presentation/ui/discovery_dialog_element.cpp
    presentation/ui/discovery_dialog_element.h
    presentation/ui/element_kinds.h
    presentation/ui/font_dialog_element.cpp
    presentation/ui/font_dialog_element.h
    presentation/ui/generate_document_dialog_element.cpp
    presentation/ui/generate_document_dialog_element.h
    presentation/ui/gitman_caption.h
    presentation/ui/gitman_policy.cpp
    presentation/ui/gitman_policy.h
    presentation/ui/local_changes_dialog_element.cpp
    presentation/ui/local_changes_dialog_element.h
    presentation/ui/log_view_element.cpp
    presentation/ui/log_view_element.h
    presentation/ui/notice_dialog_element.cpp
    presentation/ui/notice_dialog_element.h
    presentation/ui/settings_dialog_element.cpp
    presentation/ui/settings_dialog_element.h
    presentation/ui/start_page_element.cpp
    presentation/ui/start_page_element.h
    presentation/ui/status_glyphs.cpp
    presentation/ui/status_glyphs.h
    presentation/ui/switch_dialog_element.cpp
    presentation/ui/switch_dialog_element.h
    presentation/ui/toolbar_element.cpp
    presentation/ui/toolbar_element.h
    presentation/ui/ui_actions.h
    presentation/ui/ui_common.h

    # presentation: 표시 모델이다.
    presentation/diff_presentation.cpp
    presentation/diff_presentation.h
    presentation/list_metrics.cpp
    presentation/list_metrics.h
    presentation/log_presentation.cpp
    presentation/log_presentation.h
    presentation/status_presentation.cpp
    presentation/status_presentation.h
    presentation/view_snapshot.h

    # platform/win32: Win32 어댑터와 luil 앱 조립(driver·delegate·relay)이다. 창·
    # 렌더러·입력은 luil 안에 있다. 실행 파일만 이 계층을 쓴다.
    platform/win32/gitman_delegate.cpp
    platform/win32/gitman_delegate.h
    platform/win32/gitman_driver.cpp
    platform/win32/gitman_driver.h
    platform/win32/operation_result_relay.cpp
    platform/win32/operation_result_relay.h
    platform/win32/project_file_system.cpp
    platform/win32/project_file_system.h
    platform/win32/utf8.cpp
    platform/win32/utf8.h
    platform/win32/win32_app_settings_path.cpp
    platform/win32/win32_app_settings_path.h
    platform/win32/win32_directory_enumerator.cpp
    platform/win32/win32_directory_enumerator.h
    platform/win32/win32_file_association.cpp
    platform/win32/win32_file_association.h
    platform/win32/win32_log_file_system.cpp
    platform/win32/win32_log_file_system.h
    platform/win32/win32_process_runner.cpp
    platform/win32/win32_process_runner.h
    platform/win32/win32_text_transcoder.cpp
    platform/win32/win32_text_transcoder.h
    platform/win32/win32_vcs_file_probe.cpp
    platform/win32/win32_vcs_file_probe.h
    platform/win32/workspace_document_file_system.cpp
    platform/win32/workspace_document_file_system.h)
target_include_directories(gitman_lib
    PUBLIC
        "${GITMAN_SOURCE_DIRECTORY}")
# luil이 Skia·nlohmann(single_include)·WebView2 로더·Windows 시스템 라이브러리를
# PUBLIC으로 나른다. `advapi32`는 win32_file_association.cpp의 `Reg*` 때문에
# 명시한다.
target_link_libraries(gitman_lib
    PUBLIC
        gitman_project_options
        nlohmann_json::nlohmann_json
        luil::luil
        advapi32)

# IDE에서 디렉터리 구조를 그대로 보여 준다. target을 합쳐도 계층은 눈에 보인다.
get_target_property(gitman_lib_sources gitman_lib SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "src" FILES ${gitman_lib_sources})

# resource는 두 .rc다: luil 조각(코디콘 폰트·라이선스·고지, ID 101~104)과 gitman
# 조각(manifest·아이콘·VERSIONINFO).
add_executable(gitman WIN32
    "${GITMAN_GENERATED_LUIL_RESOURCE}"
    "${GITMAN_GENERATED_RESOURCE}"
    platform/win32/main.cpp
    "${gitman_resource_ids_file}")
target_link_libraries(gitman PRIVATE gitman_lib)
gitman_disable_automatic_windows_manifest(gitman)

install(TARGETS gitman RUNTIME DESTINATION .)

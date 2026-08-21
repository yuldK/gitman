set(gitman_resource_directory "${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/resources")
set(gitman_manifest_file "${gitman_resource_directory}/gitman.manifest")
set(gitman_resource_ids_file "${gitman_resource_directory}/resource_ids.h")
set(gitman_resource_template "${gitman_resource_directory}/gitman.rc.in")
set(gitman_codicons_font "${GITMAN_ASSET_DIRECTORY}/codicon.ttf")
set(gitman_codicons_license "${GITMAN_ASSET_DIRECTORY}/LICENSE")
set(gitman_codicons_code_license "${GITMAN_ASSET_DIRECTORY}/LICENSE-CODE")
set(gitman_app_icon "${CMAKE_CURRENT_SOURCE_DIR}/../assets/gitman.ico")

# Codicons C++ 매핑은 파일 하나를 만드는 custom command다. 결과 header를 라이브러리
# 소스에 넣어 두면 빌드 순서가 저절로 맞으므로 별도 target을 두지 않는다.
add_custom_command(
    OUTPUT "${GITMAN_GENERATED_CODICONS}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE=${GITMAN_ASSET_DIRECTORY}/mapping.json"
        "-DOUTPUT_FILE=${GITMAN_GENERATED_CODICONS}"
        -P "${GITMAN_CMAKE_DIRECTORY}/generate_codicons.cmake"
    DEPENDS
        "${GITMAN_ASSET_DIRECTORY}/mapping.json"
        "${GITMAN_CMAKE_DIRECTORY}/generate_codicons.cmake"
    COMMENT "Generating the Codicons C++ mapping"
    VERBATIM)

include("${GITMAN_CMAKE_DIRECTORY}/generate_notices.cmake")
gitman_generate_third_party_notices("${GITMAN_THIRD_PARTY_NOTICES}")

file(TO_CMAKE_PATH "${gitman_manifest_file}" GITMAN_MANIFEST_PATH)
file(TO_CMAKE_PATH "${gitman_resource_ids_file}" GITMAN_RESOURCE_IDS_PATH)
file(TO_CMAKE_PATH "${gitman_codicons_font}" GITMAN_CODICONS_FONT_PATH)
file(TO_CMAKE_PATH "${gitman_codicons_license}" GITMAN_CODICONS_LICENSE_PATH)
file(TO_CMAKE_PATH "${gitman_codicons_code_license}" GITMAN_CODICONS_LICENSE_CODE_PATH)
file(TO_CMAKE_PATH "${gitman_app_icon}" GITMAN_APP_ICON_PATH)
file(TO_CMAKE_PATH "${GITMAN_THIRD_PARTY_NOTICES}" GITMAN_THIRD_PARTY_NOTICES_PATH)
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
add_library(gitman_lib STATIC
    "${GITMAN_GENERATED_CODICONS}"
    # 도메인: 순수 규칙과 값 타입이다. 다른 계층에 의존하지 않는다.
    domain/app_settings.cpp
    domain/app_settings.h
    domain/diagnostic.cpp
    domain/diagnostic.h
    domain/discovery.cpp
    domain/discovery.h
    domain/local_changes.cpp
    domain/local_changes.h
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
    domain/vcs_operation.cpp
    domain/vcs_operation.h
    domain/vcs_tool.cpp
    domain/vcs_tool.h

    # application: 상태 소유와 계약(interface)이다. OS API를 직접 부르지 않는다.
    application/app_messages.h
    application/app_settings_store.h
    application/application_options.cpp
    application/application_options.h
    application/directory_enumerator.h
    application/discovery_service.cpp
    application/discovery_service.h
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

    # messaging: 스레드 경계 채널이다 (ADR-005). header-only component다.
    messaging/channel.h
    messaging/envelope.h
    messaging/latest_slot.h

    # presentation/ui: view snapshot을 그리는 UI element 계층이다.
    presentation/ui/build_ui_tree.cpp
    presentation/ui/build_ui_tree.h
    presentation/ui/button_element.cpp
    presentation/ui/button_element.h
    presentation/ui/caption_element.cpp
    presentation/ui/caption_element.h
    presentation/ui/caption_metrics.h
    presentation/ui/card_element.cpp
    presentation/ui/card_element.h
    presentation/ui/card_list_element.cpp
    presentation/ui/card_list_element.h
    presentation/ui/context_menu_element.cpp
    presentation/ui/context_menu_element.h
    presentation/ui/dialog_elements.cpp
    presentation/ui/dialog_elements.h
    presentation/ui/discovery_dialog_element.cpp
    presentation/ui/discovery_dialog_element.h
    presentation/ui/draw_primitives.cpp
    presentation/ui/draw_primitives.h
    presentation/ui/label_element.cpp
    presentation/ui/label_element.h
    presentation/ui/local_changes_dialog_element.cpp
    presentation/ui/local_changes_dialog_element.h
    presentation/ui/log_view_element.cpp
    presentation/ui/log_view_element.h
    presentation/ui/scrollbar_element.cpp
    presentation/ui/scrollbar_element.h
    presentation/ui/settings_dialog_element.cpp
    presentation/ui/settings_dialog_element.h
    presentation/ui/start_page_element.cpp
    presentation/ui/start_page_element.h
    presentation/ui/switch_dialog_element.cpp
    presentation/ui/switch_dialog_element.h
    presentation/ui/toolbar_element.cpp
    presentation/ui/toolbar_element.h
    presentation/ui/ui_element.cpp
    presentation/ui/ui_element.h
    presentation/ui/ui_events.h
    presentation/ui/ui_interaction.cpp
    presentation/ui/ui_interaction.h
    presentation/ui/ui_tree.cpp
    presentation/ui/ui_tree.h

    # presentation: 표시 모델과 theme이다.
    presentation/diff_presentation.cpp
    presentation/diff_presentation.h
    presentation/list_metrics.cpp
    presentation/list_metrics.h
    presentation/log_presentation.cpp
    presentation/log_presentation.h
    presentation/renderer_policy.cpp
    presentation/renderer_policy.h
    presentation/skia_smoke_view.cpp
    presentation/skia_smoke_view.h
    presentation/status_presentation.cpp
    presentation/status_presentation.h
    presentation/ui_theme.cpp
    presentation/ui_theme.h
    presentation/view_snapshot.h

    # platform/win32: Win32와 Skia 조립이다. 실행 파일만 이 계층을 쓴다.
    platform/win32/caption_layout.cpp
    platform/win32/caption_layout.h
    platform/win32/cpu_skia_renderer.cpp
    platform/win32/direct3d_skia_renderer.cpp
    platform/win32/embedded_assets.cpp
    platform/win32/embedded_assets.h
    platform/win32/project_file_system.cpp
    platform/win32/project_file_system.h
    platform/win32/skia_renderer.cpp
    platform/win32/skia_renderer.h
    platform/win32/utf8.cpp
    platform/win32/utf8.h
    platform/win32/version_list_generation_dialog.cpp
    platform/win32/version_list_generation_dialog.h
    platform/win32/win32_app_runtime.cpp
    platform/win32/win32_app_runtime.h
    platform/win32/win32_app_settings_path.cpp
    platform/win32/win32_app_settings_path.h
    platform/win32/win32_application.cpp
    platform/win32/win32_application.h
    platform/win32/win32_clipboard.cpp
    platform/win32/win32_clipboard.h
    platform/win32/win32_directory_enumerator.cpp
    platform/win32/win32_directory_enumerator.h
    platform/win32/win32_file_association.cpp
    platform/win32/win32_file_association.h
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
        "${GITMAN_SOURCE_DIRECTORY}"
        "${GITMAN_GENERATED_INCLUDE_DIRECTORY}")
target_link_libraries(gitman_lib
    PUBLIC
        gitman_project_options
        nlohmann_json::nlohmann_json
        unofficial::skia::skia
        advapi32
        d3d12
        dwrite
        dwmapi
        dxgi
        gdi32
        ole32
        shell32
        user32)

# 자산 checksum 검증은 빌드마다 한 번 실행한다. 별도 target 대신 라이브러리의
# 빌드 전 단계에 붙여 솔루션 구성 요소를 늘리지 않는다.
add_custom_command(TARGET gitman_lib PRE_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DASSET_DIRECTORY=${GITMAN_ASSET_DIRECTORY}"
        -P "${GITMAN_CMAKE_DIRECTORY}/verify_codicons.cmake"
    COMMENT "Verifying Codicons checksums"
    VERBATIM)

# IDE에서 디렉터리 구조를 그대로 보여 준다. target을 합쳐도 계층은 눈에 보인다.
# 생성 파일은 source tree 밖에 있으므로 따로 묶는다.
get_target_property(gitman_lib_sources gitman_lib SOURCES)
list(REMOVE_ITEM gitman_lib_sources "${GITMAN_GENERATED_CODICONS}")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "src" FILES ${gitman_lib_sources})
source_group("generated" FILES "${GITMAN_GENERATED_CODICONS}")

add_executable(gitman WIN32
    "${GITMAN_GENERATED_RESOURCE}"
    platform/win32/main.cpp
    "${gitman_resource_ids_file}")
target_link_libraries(gitman PRIVATE gitman_lib)
gitman_disable_automatic_windows_manifest(gitman)

install(TARGETS gitman RUNTIME DESTINATION .)

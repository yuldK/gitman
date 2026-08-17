set(gitman_resource_directory "${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/resources")
set(gitman_manifest_file "${gitman_resource_directory}/gitman.manifest")
set(gitman_resource_ids_file "${gitman_resource_directory}/resource_ids.h")
set(gitman_resource_template "${gitman_resource_directory}/gitman.rc.in")
set(gitman_codicons_font "${GITMAN_ASSET_DIRECTORY}/codicon.ttf")
set(gitman_codicons_license "${GITMAN_ASSET_DIRECTORY}/LICENSE")
set(gitman_codicons_code_license "${GITMAN_ASSET_DIRECTORY}/LICENSE-CODE")

add_custom_target(gitman_verify_assets ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DASSET_DIRECTORY=${GITMAN_ASSET_DIRECTORY}"
        -P "${GITMAN_CMAKE_DIRECTORY}/verify_codicons.cmake"
    COMMENT "Verifying Codicons checksums"
    VERBATIM)

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
add_custom_target(gitman_generate_codicons DEPENDS "${GITMAN_GENERATED_CODICONS}")

include("${GITMAN_CMAKE_DIRECTORY}/generate_notices.cmake")
gitman_generate_third_party_notices("${GITMAN_THIRD_PARTY_NOTICES}")

file(TO_CMAKE_PATH "${gitman_manifest_file}" GITMAN_MANIFEST_PATH)
file(TO_CMAKE_PATH "${gitman_resource_ids_file}" GITMAN_RESOURCE_IDS_PATH)
file(TO_CMAKE_PATH "${gitman_codicons_font}" GITMAN_CODICONS_FONT_PATH)
file(TO_CMAKE_PATH "${gitman_codicons_license}" GITMAN_CODICONS_LICENSE_PATH)
file(TO_CMAKE_PATH "${gitman_codicons_code_license}" GITMAN_CODICONS_LICENSE_CODE_PATH)
file(TO_CMAKE_PATH "${GITMAN_THIRD_PARTY_NOTICES}" GITMAN_THIRD_PARTY_NOTICES_PATH)
configure_file(
    "${gitman_resource_template}"
    "${GITMAN_GENERATED_RESOURCE}"
    @ONLY)
set_source_files_properties("${GITMAN_GENERATED_RESOURCE}" PROPERTIES GENERATED TRUE)

add_library(gitman_domain STATIC
    domain/diagnostic.cpp
    domain/diagnostic.h
    domain/operation.cpp
    domain/operation.h
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
    domain/vcs_tool.h)
target_include_directories(gitman_domain PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_domain PUBLIC gitman_project_options)

add_library(gitman_win32_platform STATIC
    platform/win32/project_file_system.cpp
    platform/win32/project_file_system.h
    platform/win32/utf8.cpp
    platform/win32/utf8.h
    platform/win32/workspace_document_file_system.cpp
    platform/win32/workspace_document_file_system.h)
target_include_directories(gitman_win32_platform PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_win32_platform PUBLIC gitman_domain)

# 경로 해석은 `project_path_resolver` 계약으로만 사용하므로 workspace 계층은 Win32
# platform에 링크하지 않는다. 구현체 주입은 호출자(테스트와 후속 app 조립)가 한다.
add_library(gitman_workspace STATIC
    application/project_path_resolver.h
    application/project_store.cpp
    application/project_store.h
    application/workspace_document_file_system.h
    infrastructure/json_project_store.cpp
    infrastructure/json_project_store.h
    infrastructure/json_workspace_document.cpp
    infrastructure/json_workspace_document.h
    infrastructure/workspace_document_paths.cpp
    infrastructure/workspace_document_paths.h)
target_include_directories(gitman_workspace PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_workspace
    PUBLIC
        gitman_domain
    PRIVATE
        nlohmann_json::nlohmann_json)

add_library(gitman_process STATIC
    application/process_cancellation.cpp
    application/process_cancellation.h
    application/process_request.cpp
    application/process_request.h
    application/process_runner.h
    application/text_transcoder.h
    infrastructure/command_line_builder.cpp
    infrastructure/command_line_builder.h
    infrastructure/process_output_pipeline.cpp
    infrastructure/process_output_pipeline.h
    infrastructure/secret_masking.cpp
    infrastructure/secret_masking.h
    platform/win32/win32_process_runner.cpp
    platform/win32/win32_process_runner.h
    platform/win32/win32_text_transcoder.cpp
    platform/win32/win32_text_transcoder.h)
target_include_directories(gitman_process PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_process
    PUBLIC
        gitman_domain
    PRIVATE
        gitman_win32_platform)

# Git/SVN 고유 지식 계층이다. provider는 주입받은 `process_runner`와 `vcs_file_probe`만
#사용하므로 OS API를 직접 호출하지 않는다. Win32 probe 구현은 단계 3의 runner와 같은
# 이유로 platform target이 아니라 이 target에 두어 static library 순환을 피한다.
add_library(gitman_vcs STATIC
    application/repository_provider.cpp
    application/repository_provider.h
    application/switch_validation_service.cpp
    application/switch_validation_service.h
    application/vcs_file_probe.h
    application/vcs_tool_registry.cpp
    application/vcs_tool_registry.h
    infrastructure/git_command_builder.cpp
    infrastructure/git_command_builder.h
    infrastructure/git_repository_provider.cpp
    infrastructure/git_repository_provider.h
    infrastructure/git_status_parser.cpp
    infrastructure/git_status_parser.h
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
    infrastructure/vcs_tool_discovery.cpp
    infrastructure/vcs_tool_discovery.h
    infrastructure/vcs_version.cpp
    infrastructure/vcs_version.h
    platform/win32/win32_vcs_file_probe.cpp
    platform/win32/win32_vcs_file_probe.h)
target_include_directories(gitman_vcs PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_vcs
    PUBLIC
        gitman_domain
        gitman_process
    PRIVATE
        gitman_win32_platform)

# ADR-005의 범용 스레드 메시지 component다. 어떤 gitman target에도 의존하지 않고
# 표준 라이브러리만 사용하며, template 중심이라 header-only INTERFACE target이다.
# src/messaging 디렉터리를 다른 프로젝트에 복사해도 그대로 컴파일되는 것이 분리
# 기준이다.
add_library(gitman_messaging INTERFACE
    messaging/channel.h
    messaging/envelope.h
    messaging/latest_slot.h)
target_include_directories(gitman_messaging INTERFACE "${GITMAN_SOURCE_DIRECTORY}")

# 탐색·등록 계층이다. 표식 판정은 순수 함수이고 filesystem 접근은 주입 계약 뒤에
# 있다. Win32 열거 구현은 단계 3의 runner, 단계 4의 probe와 같은 이유로 platform
# target이 아니라 이 target에 두어 계층 방향을 지킨다. 탐색은 프로세스를 만들지
# 않지만 `vcs_file_probe`와 취소 token 계약을 위해 gitman_vcs를 링크한다.
add_library(gitman_discovery STATIC
    application/directory_enumerator.h
    application/discovery_service.cpp
    application/discovery_service.h
    application/project_registration_service.cpp
    application/project_registration_service.h
    domain/discovery.cpp
    domain/discovery.h
    platform/win32/win32_directory_enumerator.cpp
    platform/win32/win32_directory_enumerator.h)
target_include_directories(gitman_discovery PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_discovery
    PUBLIC
        gitman_domain
        gitman_vcs
        gitman_workspace
    PRIVATE
        gitman_win32_platform)

# 단계 6의 앱 상태와 표시 모델 계층이다. logic thread가 소유하는 상태, view/layout
# snapshot과 상태 표시 매핑을 담는다. 파일·프로세스 I/O는 하지 않고 worker에
# 위임하므로 Win32에 링크하지 않는다.
add_library(gitman_app STATIC
    application/app_messages.h
    application/logic_controller.cpp
    application/logic_controller.h
    application/operation_executor.h
    application/task_scheduler.cpp
    application/task_scheduler.h
    infrastructure/vcs_operation_executor.cpp
    infrastructure/vcs_operation_executor.h
    presentation/list_metrics.cpp
    presentation/list_metrics.h
    presentation/status_presentation.cpp
    presentation/status_presentation.h
    presentation/view_snapshot.h)
target_include_directories(gitman_app PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_app
    PUBLIC
        gitman_discovery
        gitman_domain
        gitman_messaging
        gitman_process
        gitman_vcs
        gitman_workspace)

add_library(gitman_core STATIC
    application/application_options.cpp
    application/application_options.h
    platform/win32/caption_layout.cpp
    platform/win32/caption_layout.h
    presentation/renderer_policy.cpp
    presentation/renderer_policy.h
    presentation/ui_theme.cpp
    presentation/ui_theme.h)
target_include_directories(gitman_core PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_core
    PUBLIC
        gitman_domain
        gitman_project_options
        nlohmann_json::nlohmann_json)

# 승인된 UI element 계층이다 (docs/ui-element-design.md). view snapshot에서 불변
# tree를 빌드하고 Skia로 그리며, 상호작용 상태 기계와 input pump를 담는다. Win32에
# 의존하지 않아 어느 platform 조립에서도 재사용할 수 있다.
add_library(gitman_ui STATIC
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
    presentation/ui/draw_primitives.cpp
    presentation/ui/draw_primitives.h
    presentation/ui/label_element.cpp
    presentation/ui/label_element.h
    presentation/ui/toolbar_element.cpp
    presentation/ui/toolbar_element.h
    presentation/ui/ui_element.cpp
    presentation/ui/ui_element.h
    presentation/ui/ui_events.h
    presentation/ui/ui_interaction.cpp
    presentation/ui/ui_interaction.h
    presentation/ui/ui_tree.cpp
    presentation/ui/ui_tree.h)
add_dependencies(gitman_ui gitman_generate_codicons gitman_verify_assets)
target_include_directories(gitman_ui
    PUBLIC
        "${GITMAN_SOURCE_DIRECTORY}"
        "${GITMAN_GENERATED_INCLUDE_DIRECTORY}")
target_link_libraries(gitman_ui
    PUBLIC
        gitman_app
        gitman_core
        gitman_messaging
        unofficial::skia::skia)

add_library(gitman_win32 STATIC
    "${GITMAN_GENERATED_CODICONS}"
    platform/win32/cpu_skia_renderer.cpp
    platform/win32/direct3d_skia_renderer.cpp
    platform/win32/embedded_assets.cpp
    platform/win32/embedded_assets.h
    platform/win32/skia_renderer.cpp
    platform/win32/skia_renderer.h
    platform/win32/win32_app_runtime.cpp
    platform/win32/win32_app_runtime.h
    platform/win32/win32_application.cpp
    platform/win32/win32_application.h
    presentation/skia_smoke_view.cpp
    presentation/skia_smoke_view.h)
add_dependencies(gitman_win32 gitman_generate_codicons gitman_verify_assets)
target_include_directories(gitman_win32
    PUBLIC
        "${GITMAN_SOURCE_DIRECTORY}"
        "${GITMAN_GENERATED_INCLUDE_DIRECTORY}")
target_link_libraries(gitman_win32
    PUBLIC
        gitman_app
        gitman_core
        gitman_ui
        gitman_win32_platform
        unofficial::skia::skia
    PRIVATE
        d3d12
        dwrite
        dwmapi
        dxgi
        gdi32
        user32)

add_executable(gitman WIN32
    "${GITMAN_GENERATED_RESOURCE}"
    platform/win32/main.cpp
    "${gitman_resource_ids_file}")
add_dependencies(gitman gitman_verify_assets)
target_include_directories(gitman PRIVATE "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman
    PRIVATE
        gitman_win32
        ole32
        shell32)
gitman_disable_automatic_windows_manifest(gitman)

install(TARGETS gitman RUNTIME DESTINATION .)

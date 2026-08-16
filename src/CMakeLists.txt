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
    domain/process_execution.cpp
    domain/process_execution.h
    domain/project.cpp
    domain/project.h
    domain/repository_snapshot.cpp
    domain/repository_snapshot.h)
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

add_library(gitman_workspace STATIC
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
        gitman_win32_platform
        nlohmann_json::nlohmann_json)

add_library(gitman_process STATIC
    application/process_cancellation.cpp
    application/process_cancellation.h
    application/process_request.cpp
    application/process_request.h
    application/process_runner.h)
target_include_directories(gitman_process PUBLIC "${GITMAN_SOURCE_DIRECTORY}")
target_link_libraries(gitman_process PUBLIC gitman_domain)

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

add_library(gitman_win32 STATIC
    "${GITMAN_GENERATED_CODICONS}"
    platform/win32/cpu_skia_renderer.cpp
    platform/win32/direct3d_skia_renderer.cpp
    platform/win32/embedded_assets.cpp
    platform/win32/embedded_assets.h
    platform/win32/skia_renderer.cpp
    platform/win32/skia_renderer.h
    platform/win32/win32_application.cpp
    platform/win32/win32_application.h
    presentation/caption_ui.cpp
    presentation/caption_ui.h
    presentation/skia_smoke_view.cpp
    presentation/skia_smoke_view.h)
add_dependencies(gitman_win32 gitman_generate_codicons gitman_verify_assets)
target_include_directories(gitman_win32
    PUBLIC
        "${GITMAN_SOURCE_DIRECTORY}"
        "${GITMAN_GENERATED_INCLUDE_DIRECTORY}")
target_link_libraries(gitman_win32
    PUBLIC
        gitman_core
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

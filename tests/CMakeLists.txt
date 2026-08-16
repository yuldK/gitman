include(Catch)

add_executable(gitman_tests
    "${GITMAN_GENERATED_RESOURCE}"
    application_options_tests.cpp
    caption_layout_tests.cpp
    dependency_versions_tests.cpp
    domain_model_tests.cpp
    embedded_assets_tests.cpp
    json_project_store_tests.cpp
    process_cancellation_tests.cpp
    process_execution_tests.cpp
    process_request_tests.cpp
    project_path_tests.cpp
    project_schema_tests.cpp
    renderer_policy_tests.cpp
    ui_theme_tests.cpp
    utf8_tests.cpp)
add_dependencies(gitman_tests gitman_verify_assets)
target_include_directories(gitman_tests PRIVATE "${GITMAN_SOURCE_DIRECTORY}")
target_compile_definitions(gitman_tests
    PRIVATE
        GITMAN_TEST_FIXTURE_DIRECTORY="${GITMAN_TEST_DIRECTORY}/fixtures/projects")
target_link_libraries(gitman_tests
    PRIVATE
        gitman_process
        gitman_workspace
        gitman_win32
        Catch2::Catch2WithMain)
gitman_disable_automatic_windows_manifest(gitman_tests)
catch_discover_tests(gitman_tests)

add_test(NAME gitman_assets_checksum
    COMMAND "${CMAKE_COMMAND}"
        "-DASSET_DIRECTORY=${GITMAN_ASSET_DIRECTORY}"
        -P "${GITMAN_CMAKE_DIRECTORY}/verify_codicons.cmake")
add_test(NAME gitman_source_style
    COMMAND "${CMAKE_COMMAND}"
        "-DPOWERSHELL_EXECUTABLE=${GITMAN_POWERSHELL_EXECUTABLE}"
        "-DROOT_DIRECTORY=${PROJECT_SOURCE_DIR}"
        -P "${GITMAN_SOURCE_STYLE_CMAKE_SCRIPT}")
add_test(NAME gitman_smoke_cpu
    COMMAND "$<TARGET_FILE:gitman>" --renderer=cpu --smoke-test)
add_test(NAME gitman_smoke_auto
    COMMAND "$<TARGET_FILE:gitman>" --renderer=auto --smoke-test)
add_test(NAME gitman_smoke_auto_fallback
    COMMAND "$<TARGET_FILE:gitman>" --renderer=auto --smoke-test
        --simulate-direct3d-failure)
add_test(NAME gitman_smoke_direct3d
    COMMAND "$<TARGET_FILE:gitman>" --renderer=direct3d --smoke-test)
set_tests_properties(gitman_smoke_direct3d PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(
    gitman_smoke_cpu
    gitman_smoke_auto
    gitman_smoke_auto_fallback
    gitman_smoke_direct3d
    PROPERTIES TIMEOUT 30 LABELS "smoke")

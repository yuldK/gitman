include(Catch)

# 프로세스 실행 test는 표준 argv 규칙을 쓰는 콘솔 자식이 필요하다. `cmd.exe`는 자체
# 따옴표 처리를 하므로 대체할 수 없다. test 전용 target이며 install 대상이 아니다.
add_executable(gitman_process_test_child helpers/process_test_child.cpp)
target_link_libraries(gitman_process_test_child PRIVATE gitman_project_options)
gitman_disable_automatic_windows_manifest(gitman_process_test_child)

add_executable(gitman_tests
    "${GITMAN_GENERATED_RESOURCE}"
    application_options_tests.cpp
    caption_layout_tests.cpp
    command_line_builder_tests.cpp
    dependency_versions_tests.cpp
    domain_model_tests.cpp
    embedded_assets_tests.cpp
    git_command_builder_tests.cpp
    git_integration_tests.cpp
    git_remote_query_tests.cpp
    git_repository_provider_tests.cpp
    git_status_parser_tests.cpp
    helpers/git_repository_fixture.cpp
    helpers/git_repository_fixture.h
    helpers/vcs_test_doubles.cpp
    helpers/vcs_test_doubles.h
    json_project_store_tests.cpp
    process_cancellation_tests.cpp
    process_execution_tests.cpp
    process_output_pipeline_tests.cpp
    process_request_tests.cpp
    project_path_tests.cpp
    project_schema_tests.cpp
    renderer_policy_tests.cpp
    secret_masking_tests.cpp
    ui_theme_tests.cpp
    utf8_tests.cpp
    vcs_domain_tests.cpp
    vcs_error_classifier_tests.cpp
    vcs_execution_policy_tests.cpp
    vcs_tool_discovery_tests.cpp
    vcs_version_tests.cpp
    win32_process_runner_tests.cpp
    win32_text_transcoder_tests.cpp)
add_dependencies(gitman_tests gitman_verify_assets gitman_process_test_child)
target_include_directories(gitman_tests
    PRIVATE
        "${GITMAN_SOURCE_DIRECTORY}"
        "${GITMAN_TEST_DIRECTORY}")
target_compile_definitions(gitman_tests
    PRIVATE
        GITMAN_TEST_FIXTURE_DIRECTORY="${GITMAN_TEST_DIRECTORY}/fixtures/projects"
        # 실제 Git이 낸 출력을 그대로 저장한 fixture다. 파서가 실제 형식에서 벗어나지
        # 않도록 고정한다.
        GITMAN_VCS_FIXTURE_DIRECTORY="${GITMAN_TEST_DIRECTORY}/fixtures/vcs")
target_link_libraries(gitman_tests
    PRIVATE
        gitman_process
        gitman_vcs
        gitman_workspace
        gitman_win32
        Catch2::Catch2WithMain)
gitman_disable_automatic_windows_manifest(gitman_tests)
# hang 유형의 회귀가 CI에서 무기한 대기 대신 실패로 드러나도록 모든 Catch2 test에
# timeout을 부여한다. 가장 긴 통합 test 기준으로 여유를 둔 값이다.
catch_discover_tests(gitman_tests PROPERTIES TIMEOUT 120)

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

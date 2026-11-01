include(Catch)

# 프로세스 실행 test는 표준 argv 규칙을 쓰는 콘솔 자식이 필요하다. `cmd.exe`는 자체
# 따옴표 처리를 하므로 대체할 수 없다. test 전용 target이며 install 대상이 아니다.
add_executable(gitman_process_test_child helpers/process_test_child.cpp)
target_link_libraries(gitman_process_test_child PRIVATE gitman_project_options)
gitman_disable_automatic_windows_manifest(gitman_process_test_child)

# UI 기반 계층의 test(element·tree·상호작용·caption 배치·글꼴·TSF·messaging·
# 테마·리소스)는 luil 저장소가 갖는다 (ADR-007). 여기에는 gitman의 의미(intent
# 매핑·표시 모델·앱 조립)를 검증하는 test만 있다. luil의 .rc 조각을 함께 넣어
# 코디콘 폰트·고지 리소스가 test 실행 파일에도 실린다.
#
# test 파일도 라이브러리와 같은 방식으로 IDE 필터를 짠다 (gitman_add_source_filter) —
# 검증하는 계층과 하는 일 단위다.
set(gitman_test_files)

gitman_add_source_filter(gitman_test_files "resource"
    "${GITMAN_GENERATED_LUIL_RESOURCE}"
    "${GITMAN_GENERATED_RESOURCE}")

# test 대역·fixture·UI 도우미다.
gitman_add_source_filter(gitman_test_files "helpers"
    helpers/discovery_test_doubles.cpp
    helpers/discovery_test_doubles.h
    helpers/git_repository_fixture.cpp
    helpers/git_repository_fixture.h
    helpers/ui_test_support.h
    helpers/vcs_test_doubles.cpp
    helpers/vcs_test_doubles.h)

gitman_add_source_filter(gitman_test_files "domain"
    dependency_versions_tests.cpp
    discovery_domain_tests.cpp
    domain_model_tests.cpp
    local_changes_domain_tests.cpp
    operation_log_tests.cpp
    utf8_tests.cpp
    vcs_domain_tests.cpp)

gitman_add_source_filter(gitman_test_files "application/logic"
    app_commands_tests.cpp
    application_options_tests.cpp
    cancellation_registry_tests.cpp
    log_isolation_stress_tests.cpp
    logic_change_operation_tests.cpp
    logic_controller_tests.cpp
    process_cancellation_tests.cpp
    stabilization_tests.cpp
    task_scheduler_tests.cpp)

gitman_add_source_filter(gitman_test_files "application/documents"
    app_settings_tests.cpp
    json_project_store_tests.cpp
    path_display_tests.cpp
    project_path_tests.cpp
    project_registration_tests.cpp
    project_schema_tests.cpp
    version_list_generation_tests.cpp)

gitman_add_source_filter(gitman_test_files "application/vcs"
    svn_repository_browser_tests.cpp
    switch_validation_tests.cpp)

gitman_add_source_filter(gitman_test_files "application/discovery"
    directory_enumerator_tests.cpp
    discovery_integration_tests.cpp
    discovery_service_tests.cpp)

gitman_add_source_filter(gitman_test_files "infrastructure/git"
    git_command_builder_tests.cpp
    git_integration_tests.cpp
    git_network_tests.cpp
    git_remote_query_tests.cpp
    git_repository_provider_tests.cpp
    git_status_parser_tests.cpp
    git_switch_tests.cpp
    git_update_tests.cpp)

gitman_add_source_filter(gitman_test_files "infrastructure/svn"
    svn_command_builder_tests.cpp
    svn_integration_tests.cpp
    svn_output_parser_tests.cpp
    svn_repository_provider_tests.cpp)

gitman_add_source_filter(gitman_test_files "infrastructure/vcs"
    vcs_error_classifier_tests.cpp
    vcs_execution_policy_tests.cpp
    vcs_operation_executor_tests.cpp
    vcs_tool_discovery_tests.cpp
    vcs_version_tests.cpp)

gitman_add_source_filter(gitman_test_files "infrastructure/process"
    command_line_builder_tests.cpp
    process_execution_tests.cpp
    process_output_pipeline_tests.cpp
    process_request_tests.cpp
    secret_masking_tests.cpp
    win32_process_runner_tests.cpp
    win32_text_transcoder_tests.cpp)

gitman_add_source_filter(gitman_test_files "infrastructure/logging"
    log_file_tests.cpp)

gitman_add_source_filter(gitman_test_files "infrastructure/shell"
    file_association_tests.cpp)

gitman_add_source_filter(gitman_test_files "presentation/ui"
    element_kinds_tests.cpp
    log_view_tests.cpp
    start_page_tests.cpp
    status_presentation_tests.cpp
    text_font_tests.cpp
    ui_interaction_tests.cpp
    ui_tree_build_tests.cpp
    update_ui_tests.cpp)

gitman_add_source_filter(gitman_test_files "presentation/dialogs"
    context_menu_tests.cpp
    discovery_dialog_tests.cpp
    generate_document_dialog_tests.cpp
    local_changes_tests.cpp
    notice_dialog_tests.cpp
    settings_dialog_tests.cpp
    switch_dialog_tests.cpp)

gitman_add_source_filter(gitman_test_files "platform/host"
    gitman_delegate_tests.cpp
    gitman_driver_tests.cpp
    operation_result_relay_tests.cpp)

add_executable(gitman_tests ${gitman_test_files})
add_dependencies(gitman_tests gitman_process_test_child)
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
        gitman_lib
        Catch2::Catch2WithMain)
gitman_disable_automatic_windows_manifest(gitman_tests)

# ADR-005의 ASan 검증은 messaging component가 luil 소유가 되면서 luil 저장소의
# `luil_messaging_asan_tests`(luil CI)가 맡는다 (ADR-007 결정 8). gitman 빌드에서는
# 돌지 않는다.

# hang 유형의 회귀가 CI에서 무기한 대기 대신 실패로 드러나도록 모든 Catch2 test에
# timeout을 부여한다. 가장 긴 통합 test 기준으로 여유를 둔 값이다.
catch_discover_tests(gitman_tests PROPERTIES TIMEOUT 120)

# PowerShell은 script가 돌 때 PATH에서 찾는다 (run_style_check.cmake에 이유가 있다).
add_test(NAME gitman_source_style
    COMMAND "${CMAKE_COMMAND}"
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

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
#
# 파일은 IDE 필터 단위로 적고, 그렇게 모인 것이 곧 라이브러리의 source 목록이다
# (gitman_add_source_filter, cmake/macro.cmake). 필터의 첫 단은 계층, 둘째 단은
# **하는 일**이다 — 디렉터리 그대로 한 필터에 40~50개를 넣으면 찾을 수가 없다.
set(gitman_lib_files)

# 도메인: 순수 규칙과 값 타입이다. 다른 계층에 의존하지 않는다.
# 저장소·프로젝트·도구·경로와 로컬 변경·탐색 후보의 모델이다.
gitman_add_source_filter(gitman_lib_files "domain/repository"
    domain/discovery.cpp
    domain/discovery.h
    domain/local_changes.cpp
    domain/local_changes.h
    domain/path_syntax.cpp
    domain/path_syntax.h
    domain/project.cpp
    domain/project.h
    domain/repository_snapshot.cpp
    domain/repository_snapshot.h
    domain/vcs_operation.cpp
    domain/vcs_operation.h
    domain/vcs_tool.cpp
    domain/vcs_tool.h)

# 작업 하나의 요청·결과·진단·로그 record와 외부 프로세스 실행의 값 타입이다.
gitman_add_source_filter(gitman_lib_files "domain/operation"
    domain/diagnostic.cpp
    domain/diagnostic.h
    domain/log_file_naming.cpp
    domain/log_file_naming.h
    domain/operation.cpp
    domain/operation.h
    domain/operation_log.cpp
    domain/operation_log.h
    domain/process_execution.cpp
    domain/process_execution.h)

# 앱 전역·문서 설정과 외양(테마·키 컬러·글꼴) 값이다. 외양은 luil의 값 타입을
# `gitman::` 이름으로 전달한다.
gitman_add_source_filter(gitman_lib_files "domain/settings"
    domain/app_settings.cpp
    domain/app_settings.h
    domain/appearance.h)

# 문서에 정의한 기능 버튼과 변수의 모델, 배치 판정, 변수 치환과 표시용 명령줄,
# 실행 파일 허용 목록과 cmd 방어 판정이다.
gitman_add_source_filter(gitman_lib_files "domain/function_buttons"
    domain/function_button.cpp
    domain/function_button.h)

# 텍스트 편집 상태와 UTF-8 도우미의 전달 헤더다 (luil의 값 타입).
gitman_add_source_filter(gitman_lib_files "domain/text"
    domain/text_edit.h
    domain/utf8_text.h)

# application: 상태 소유와 계약(interface)이다. OS API를 직접 부르지 않고 luil
# UI 헤더를 include하지 않는다.
# logic thread의 중심이다 — intent·이벤트 메시지, 상태 기계, worker 배정과 취소.
gitman_add_source_filter(gitman_lib_files "application/logic"
    application/app_commands.h
    application/app_messages.h
    application/application_options.cpp
    application/application_options.h
    application/cancellation_registry.cpp
    application/cancellation_registry.h
    application/function_execution_pool.cpp
    application/function_execution_pool.h
    application/function_executor.h
    application/logic_controller.cpp
    application/logic_controller.h
    application/operation_executor.h
    application/process_cancellation.cpp
    application/process_cancellation.h
    application/task_scheduler.cpp
    application/task_scheduler.h)

# `.version-list` 문서와 앱 설정의 저장·생성·등록 계약이다.
gitman_add_source_filter(gitman_lib_files "application/documents"
    application/app_settings_store.h
    application/project_path_resolver.h
    application/project_registration_service.cpp
    application/project_registration_service.h
    application/project_store.cpp
    application/project_store.h
    application/version_list_generation_service.cpp
    application/version_list_generation_service.h
    application/workspace_document_file_system.h)

# Git·SVN provider 계약, 도구 등록, switch 검증, SVN 저장소 브라우저다.
gitman_add_source_filter(gitman_lib_files "application/vcs"
    application/repository_provider.cpp
    application/repository_provider.h
    application/svn_repository_browser.cpp
    application/svn_repository_browser.h
    application/switch_validation_service.cpp
    application/switch_validation_service.h
    application/vcs_file_probe.h
    application/vcs_tool_registry.cpp
    application/vcs_tool_registry.h)

# 깊이 1 하위 저장소 탐색이다.
gitman_add_source_filter(gitman_lib_files "application/discovery"
    application/directory_enumerator.h
    application/discovery_service.cpp
    application/discovery_service.h)

# 외부 프로세스 요청과 실행기·분리 실행기·출력 transcoder 계약이다.
gitman_add_source_filter(gitman_lib_files "application/process"
    application/process_launcher.h
    application/process_request.cpp
    application/process_request.h
    application/process_runner.h
    application/text_transcoder.h)

# 문서 폴더의 파일 로그 계약이다.
gitman_add_source_filter(gitman_lib_files "application/logging"
    application/log_file_sink.h
    application/log_file_system.h)

# infrastructure: application 계약의 구현이다 (JSON, VCS, 프로세스).
# JSON 문서·앱 설정 저장소와 문서 경로 규칙이다.
gitman_add_source_filter(gitman_lib_files "infrastructure/documents"
    infrastructure/json_app_settings_store.cpp
    infrastructure/json_app_settings_store.h
    infrastructure/json_project_store.cpp
    infrastructure/json_project_store.h
    infrastructure/json_workspace_document.cpp
    infrastructure/json_workspace_document.h
    infrastructure/workspace_document_paths.cpp
    infrastructure/workspace_document_paths.h)

# Git 명령 조립·출력 파서·provider다.
gitman_add_source_filter(gitman_lib_files "infrastructure/git"
    infrastructure/git_command_builder.cpp
    infrastructure/git_command_builder.h
    infrastructure/git_repository_provider.cpp
    infrastructure/git_repository_provider.h
    infrastructure/git_status_parser.cpp
    infrastructure/git_status_parser.h)

# SVN 명령 조립·출력 파서·provider다.
gitman_add_source_filter(gitman_lib_files "infrastructure/svn"
    infrastructure/svn_command_builder.cpp
    infrastructure/svn_command_builder.h
    infrastructure/svn_output_parser.cpp
    infrastructure/svn_output_parser.h
    infrastructure/svn_repository_provider.cpp
    infrastructure/svn_repository_provider.h)

# Git·SVN 공통 — 명령 실행기, 오류 분류, 실행 정책, 작업 실행기, 도구 탐색·판번,
# 로컬 변경 읽기다.
gitman_add_source_filter(gitman_lib_files "infrastructure/vcs"
    infrastructure/local_change_reader.cpp
    infrastructure/local_change_reader.h
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
    infrastructure/vcs_version.h)

# 명령줄 조립, 출력 파이프라인, 출력의 로그 묶음 전달, 비밀 마스킹이다.
gitman_add_source_filter(gitman_lib_files "infrastructure/process"
    infrastructure/command_line_builder.cpp
    infrastructure/command_line_builder.h
    infrastructure/operation_log_forwarder.cpp
    infrastructure/operation_log_forwarder.h
    infrastructure/process_output_pipeline.cpp
    infrastructure/process_output_pipeline.h
    infrastructure/secret_masking.cpp
    infrastructure/secret_masking.h)

# 기능 버튼 실행 요청 조립(캡처 상한·timeout·`GITMAN_*` 환경)과 기능 실행 executor다. VCS 정책과 따로 둔다.
gitman_add_source_filter(gitman_lib_files "infrastructure/function_buttons"
    infrastructure/function_execution_policy.cpp
    infrastructure/function_execution_policy.h
    infrastructure/function_process_executor.cpp
    infrastructure/function_process_executor.h)

# 파일 로그 writer다.
gitman_add_source_filter(gitman_lib_files "infrastructure/logging"
    infrastructure/file_log_writer.cpp
    infrastructure/file_log_writer.h)

# `.version-list` 파일 연결의 레지스트리 규칙이다.
gitman_add_source_filter(gitman_lib_files "infrastructure/shell"
    infrastructure/file_association.cpp
    infrastructure/file_association.h)

# presentation: 표시 모델이다. logic이 만든 view snapshot과 그 치수·문구다.
gitman_add_source_filter(gitman_lib_files "presentation/model"
    presentation/diff_presentation.cpp
    presentation/diff_presentation.h
    presentation/list_metrics.cpp
    presentation/list_metrics.h
    presentation/log_presentation.cpp
    presentation/log_presentation.h
    presentation/status_presentation.cpp
    presentation/status_presentation.h
    presentation/view_snapshot.h)

# presentation/ui: view snapshot을 luil element tree로 짓는 앱 element 계층과
# 입력 정책이다.
# 공통 어휘·kind 표·액션 도우미·글리프 표·dialog 뼈대와 tree 조립이다.
gitman_add_source_filter(gitman_lib_files "presentation/ui/foundation"
    presentation/ui/build_ui_tree.cpp
    presentation/ui/build_ui_tree.h
    presentation/ui/dialog_host.h
    presentation/ui/element_kinds.h
    presentation/ui/gitman_caption.h
    presentation/ui/status_glyphs.cpp
    presentation/ui/status_glyphs.h
    presentation/ui/ui_actions.h
    presentation/ui/ui_common.h)

# luil 상호작용 상태 기계가 모르는 앱 정책(텍스트 칸·휠·메뉴 kind·남은 키)이다.
gitman_add_source_filter(gitman_lib_files "presentation/ui/policy"
    presentation/ui/gitman_policy.cpp
    presentation/ui/gitman_policy.h)

# 주 화면 — 도구 막대, 카드 목록과 카드, 하단 로그 pane, 시작 페이지다.
gitman_add_source_filter(gitman_lib_files "presentation/ui/workspace"
    presentation/ui/card_element.cpp
    presentation/ui/card_element.h
    presentation/ui/card_list_element.cpp
    presentation/ui/card_list_element.h
    presentation/ui/log_view_element.cpp
    presentation/ui/log_view_element.h
    presentation/ui/start_page_element.cpp
    presentation/ui/start_page_element.h
    presentation/ui/toolbar_element.cpp
    presentation/ui/toolbar_element.h)

# in-app dialog 본체 7종이다 (luil modal_host의 content).
gitman_add_source_filter(gitman_lib_files "presentation/ui/dialogs"
    presentation/ui/discovery_dialog_element.cpp
    presentation/ui/discovery_dialog_element.h
    presentation/ui/font_dialog_element.cpp
    presentation/ui/font_dialog_element.h
    presentation/ui/generate_document_dialog_element.cpp
    presentation/ui/generate_document_dialog_element.h
    presentation/ui/local_changes_dialog_element.cpp
    presentation/ui/local_changes_dialog_element.h
    presentation/ui/notice_dialog_element.cpp
    presentation/ui/notice_dialog_element.h
    presentation/ui/settings_dialog_element.cpp
    presentation/ui/settings_dialog_element.h
    presentation/ui/switch_dialog_element.cpp
    presentation/ui/switch_dialog_element.h)

# 컨텍스트 메뉴 popup 빌더다.
gitman_add_source_filter(gitman_lib_files "presentation/ui/menu"
    presentation/ui/context_menu_popup.cpp
    presentation/ui/context_menu_popup.h)

# platform/win32: Win32 어댑터와 luil 앱 조립이다. 창·렌더러·입력은 luil 안에
# 있다. 실행 파일만 이 계층을 쓴다.
# luil driver·delegate와 worker 결과 relay다.
gitman_add_source_filter(gitman_lib_files "platform/host"
    platform/win32/gitman_delegate.cpp
    platform/win32/gitman_delegate.h
    platform/win32/gitman_driver.cpp
    platform/win32/gitman_driver.h
    platform/win32/operation_result_relay.cpp
    platform/win32/operation_result_relay.h)

# 파일·디렉터리 접근 — 문서·설정 경로, 디렉터리 열거, 로그 파일, VCS 파일 probe다.
gitman_add_source_filter(gitman_lib_files "platform/file_system"
    platform/win32/project_file_system.cpp
    platform/win32/project_file_system.h
    platform/win32/win32_app_settings_path.cpp
    platform/win32/win32_app_settings_path.h
    platform/win32/win32_directory_enumerator.cpp
    platform/win32/win32_directory_enumerator.h
    platform/win32/win32_log_file_system.cpp
    platform/win32/win32_log_file_system.h
    platform/win32/win32_vcs_file_probe.cpp
    platform/win32/win32_vcs_file_probe.h
    platform/win32/workspace_document_file_system.cpp
    platform/win32/workspace_document_file_system.h)

# 외부 프로세스 실행·분리 실행과 환경 block, 출력 code page 변환, UTF-8/UTF-16 경계다.
gitman_add_source_filter(gitman_lib_files "platform/process"
    platform/win32/utf8.cpp
    platform/win32/utf8.h
    platform/win32/win32_environment_block.cpp
    platform/win32/win32_environment_block.h
    platform/win32/win32_process_launcher.cpp
    platform/win32/win32_process_launcher.h
    platform/win32/win32_process_runner.cpp
    platform/win32/win32_process_runner.h
    platform/win32/win32_text_transcoder.cpp
    platform/win32/win32_text_transcoder.h)

# 파일 연결 레지스트리 쓰기다.
gitman_add_source_filter(gitman_lib_files "platform/shell"
    platform/win32/win32_file_association.cpp
    platform/win32/win32_file_association.h)

add_library(gitman_lib STATIC ${gitman_lib_files})
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

# resource는 두 .rc다: luil 조각(코디콘 폰트·라이선스·고지, ID 101~104)과 gitman
# 조각(manifest·아이콘·VERSIONINFO).
set(gitman_app_files)
gitman_add_source_filter(gitman_app_files "app"
    platform/win32/main.cpp)
gitman_add_source_filter(gitman_app_files "resource"
    "${GITMAN_GENERATED_LUIL_RESOURCE}"
    "${GITMAN_GENERATED_RESOURCE}"
    "${gitman_resource_ids_file}")
add_executable(gitman WIN32 ${gitman_app_files})
target_link_libraries(gitman PRIVATE gitman_lib)
gitman_disable_automatic_windows_manifest(gitman)

install(TARGETS gitman RUNTIME DESTINATION .)

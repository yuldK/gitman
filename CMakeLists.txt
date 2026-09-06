cmake_minimum_required(VERSION 4.2.0 FATAL_ERROR)

cmake_policy(SET CMP0091 NEW)

set(GITMAN_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "${GITMAN_MSVC_RUNTIME_LIBRARY}")

project(gitman VERSION 0.1.0 LANGUAGES CXX RC)

set(GITMAN_CXX_STANDARD 20)
set(GITMAN_MINIMUM_MSVC_VERSION 1940)
set(GITMAN_MINIMUM_WINDOWS_SDK_VERSION "10.0.22621.0")
set(GITMAN_TARGET_ARCHITECTURE "x64")
set(GITMAN_SUPPORTED_GENERATORS
    "Visual Studio 17 2022"
    "Visual Studio 18 2026"
)

set(GITMAN_CMAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/cmake")
set(GITMAN_THIRD_PARTY_DIRECTORY "${PROJECT_SOURCE_DIR}/third_party")
set(GITMAN_SOURCE_DIRECTORY "${PROJECT_SOURCE_DIR}/src")
set(GITMAN_TEST_DIRECTORY "${PROJECT_SOURCE_DIR}/tests")
set(GITMAN_GENERATED_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
set(GITMAN_GENERATED_RESOURCE "${GITMAN_GENERATED_DIRECTORY}/gitman.rc")
# luil이 요구하는 resource(코디콘 폰트·라이선스·고지) 조각이다. gitman.rc와 함께
# 실행 파일과 test 실행 파일에 들어간다. ID 규칙: 101~104 luil, 105 앱 아이콘,
# 이후 gitman resource는 106부터다 (ADR-007 결정 7).
set(GITMAN_GENERATED_LUIL_RESOURCE "${GITMAN_GENERATED_DIRECTORY}/luil_resources.rc")
set(GITMAN_THIRD_PARTY_NOTICES
    "${GITMAN_GENERATED_DIRECTORY}/third_party_notices.txt")
set(GITMAN_SOURCE_STYLE_SCRIPT "${PROJECT_SOURCE_DIR}/scripts/check_source_style.ps1")
set(GITMAN_SOURCE_STYLE_CMAKE_SCRIPT "${GITMAN_CMAKE_DIRECTORY}/run_style_check.cmake")

# 의존성 위치. 전부 submodule이다. Skia·WebView2 SDK는 luil의 패키지 스크립트가
# third_party/luil 안에 준비한다 (ADR-007).
set(GITMAN_NLOHMANN_JSON_ROOT "${GITMAN_THIRD_PARTY_DIRECTORY}/nlohmann-json")
set(GITMAN_CATCH2_ROOT "${GITMAN_THIRD_PARTY_DIRECTORY}/catch2")

set(CMAKE_CXX_STANDARD ${GITMAN_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 기본 솔루션에는 실제 빌드에 필요한 target(gitman, gitman_lib)만 둔다. test와
# 개발 도구 target은 명시적으로 켤 때만 만들어진다.
#   cmake --preset vs2022-tests     → test target과 CTest 등록 포함
#   -DGITMAN_BUILD_TOOLING=ON       → clang-format·style 검사 target 포함
option(GITMAN_BUILD_TESTS "Build the Gitman test targets." OFF)
option(GITMAN_BUILD_TOOLING "Build the clang-format and source style targets." OFF)
option(GITMAN_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze for project sources." OFF)

include("${GITMAN_CMAKE_DIRECTORY}/macro.cmake")
include("${GITMAN_CMAKE_DIRECTORY}/compiler/msvc.cmake")
include("${GITMAN_CMAKE_DIRECTORY}/platform/windows.cmake")

gitman_validate_windows_platform(
    ARCHITECTURE "${GITMAN_TARGET_ARCHITECTURE}"
    MINIMUM_SDK_VERSION "${GITMAN_MINIMUM_WINDOWS_SDK_VERSION}"
    SUPPORTED_GENERATORS ${GITMAN_SUPPORTED_GENERATORS}
)
gitman_validate_msvc(MINIMUM_VERSION "${GITMAN_MINIMUM_MSVC_VERSION}")
gitman_configure_msvc_link_directories()

add_library(gitman_project_options INTERFACE)
target_compile_features(gitman_project_options INTERFACE cxx_std_${GITMAN_CXX_STANDARD})
gitman_apply_windows_options(gitman_project_options)
gitman_apply_msvc_options(
    gitman_project_options
    ENABLE_ANALYZE "${GITMAN_ENABLE_MSVC_ANALYZE}"
)

find_program(GITMAN_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

# 의존성은 submodule과 luil이 준비한 패키지로만 구성한다 (ADR-006·ADR-007). 취득을
# 자동화하지 않으므로 configure는 네트워크를 전혀 쓰지 않는다.
include("${GITMAN_CMAKE_DIRECTORY}/dependencies.cmake")
gitman_find_dependencies(BUILD_TESTS ${GITMAN_BUILD_TESTS})

# UI 프레임워크 luil (ADR-007). submodule 안의 Skia 패키지·WebView2 SDK는 luil의
# 스크립트가 준비하며 configure는 존재만 검사한다. Skia·nlohmann(single_include)·
# WebView2 로더는 luil::luil이 PUBLIC으로 나른다.
#  - LUIL_BUILD_TESTS는 반드시 OFF다. 켜면 luil이 자기 Catch2를 같은 바이너리
#    디렉터리(third_party/catch2)에 add_subdirectory해 gitman의 것과 충돌한다.
#  - SYSTEM: luil 공개 헤더를 gitman의 /W4·/analyze 밖에 둔다.
#  - 정적 CRT 설정은 project() 앞의 것을 luil이 그대로 따른다.
set(LUIL_BUILD_TESTS OFF)
set(LUIL_BUILD_EXAMPLES OFF)
set(LUIL_BUILD_TOOLING OFF)
add_subdirectory(third_party/luil SYSTEM)
# luil의 고지·resource 함수는 호출 scope의 세 변수를 읽는다. 하위 디렉터리의
# 일반 변수는 부모로 올라오지 않으므로 여기서 다시 정한다.
set(LUIL_SOURCE_DIRECTORY "${GITMAN_THIRD_PARTY_DIRECTORY}/luil/src")
set(LUIL_ASSET_DIRECTORY "${GITMAN_THIRD_PARTY_DIRECTORY}/luil/assets/codicons")
set(LUIL_NLOHMANN_JSON_ROOT "${GITMAN_THIRD_PARTY_DIRECTORY}/luil/third_party/nlohmann_json")
# 제3자 고지와 .rc 조각은 src/와 tests/가 함께 쓰므로 여기서 한 번 만든다.
luil_generate_third_party_notices("${GITMAN_THIRD_PARTY_NOTICES}")
luil_configure_resource_script("${GITMAN_GENERATED_LUIL_RESOURCE}" "${GITMAN_THIRD_PARTY_NOTICES}")

# `include(CTest)`는 Continuous·Experimental·Nightly 등 dashboard target을 함께
# 만들어 솔루션을 부풀린다. 필요한 것은 test 등록뿐이라 enable_testing만 쓴다.
if(GITMAN_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src)
if(GITMAN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(GITMAN_BUILD_TOOLING)
    gitman_find_visual_studio_clang_format(GITMAN_CLANG_FORMAT_EXECUTABLE)
    gitman_add_clang_format_targets(
        CLANG_FORMAT_EXECUTABLE "${GITMAN_CLANG_FORMAT_EXECUTABLE}"
        FORMAT_TARGET gitman_format
        CHECK_TARGET gitman_format_check
        SOURCE_DIRECTORIES
            "${GITMAN_SOURCE_DIRECTORY}"
            "${GITMAN_TEST_DIRECTORY}"
        FILE_EXTENSIONS cpp h
        CHECK_COMMAND
            "${GITMAN_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${GITMAN_SOURCE_STYLE_SCRIPT}"
            -root "${PROJECT_SOURCE_DIR}"
    )
    gitman_set_ide_folder("Tooling"
        gitman_format
        gitman_format_check)
endif()

# IDE 필터 구성은 target 정의와 분리해 이 한 곳에서만 관리한다. 실행 파일 target인
# `gitman`만 최상위에 남겨 솔루션을 열었을 때 진입점이 바로 보이게 한다.
gitman_set_ide_folder("Libraries" gitman_lib luil)
if(GITMAN_BUILD_TESTS)
    gitman_set_ide_folder("Tests"
        gitman_process_test_child
        gitman_tests)
endif()

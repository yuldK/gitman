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
set(GITMAN_ASSET_DIRECTORY "${PROJECT_SOURCE_DIR}/assets/codicons")
set(GITMAN_GENERATED_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
set(GITMAN_GENERATED_INCLUDE_DIRECTORY "${GITMAN_GENERATED_DIRECTORY}/include")
set(GITMAN_GENERATED_CODICONS
    "${GITMAN_GENERATED_INCLUDE_DIRECTORY}/gitman/generated/codicons.h")
set(GITMAN_GENERATED_RESOURCE "${GITMAN_GENERATED_DIRECTORY}/gitman.rc")
set(GITMAN_THIRD_PARTY_NOTICES
    "${GITMAN_GENERATED_DIRECTORY}/third_party_notices.txt")
set(GITMAN_SOURCE_STYLE_SCRIPT "${PROJECT_SOURCE_DIR}/scripts/check_source_style.ps1")
set(GITMAN_SOURCE_STYLE_CMAKE_SCRIPT "${GITMAN_CMAKE_DIRECTORY}/run_style_check.cmake")

# 의존성 위치. Skia만 사용자가 손으로 빌드하며 나머지는 submodule이다.
set(GITMAN_SKIA_ROOT "${GITMAN_THIRD_PARTY_DIRECTORY}/skia"
    CACHE PATH "Skia source tree that the user built by hand.")
set(GITMAN_SKIA_BUILD_DEBUG "${GITMAN_SKIA_ROOT}/out/gitman-debug"
    CACHE PATH "Skia Debug build directory.")
set(GITMAN_SKIA_BUILD_RELEASE "${GITMAN_SKIA_ROOT}/out/gitman-release"
    CACHE PATH "Skia Release build directory.")
set(GITMAN_NLOHMANN_JSON_ROOT "${GITMAN_THIRD_PARTY_DIRECTORY}/nlohmann-json")
set(GITMAN_CATCH2_ROOT "${GITMAN_THIRD_PARTY_DIRECTORY}/catch2")

# Skia 빌드가 내는 라이브러리다. vcpkg는 하나로 합쳐 주었지만 직접 빌드에서는
# external별로 나오므로 모두 링크한다.
set(GITMAN_SKIA_COMPONENTS
    skia
    skcms
    spirv_cross
    d3d12allocator)

# Gitman이 요구하는 Skia 기능이다. 이 값으로 빌드되지 않은 Skia는 configure에서
# 거른다. SkShaper를 쓰기 시작하면 skia_use_harfbuzz를 여기에 추가한다.
set(GITMAN_SKIA_REQUIRED_ARGUMENTS
    "skia_use_direct3d=true")

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

# 의존성은 submodule과 사용자가 손으로 빌드한 Skia로만 구성한다 (ADR-006). 취득을
# 자동화하지 않으므로 configure는 네트워크를 전혀 쓰지 않는다.
include("${GITMAN_CMAKE_DIRECTORY}/dependencies.cmake")
gitman_find_dependencies(BUILD_TESTS ${GITMAN_BUILD_TESTS})

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
gitman_set_ide_folder("Libraries" gitman_lib)
if(GITMAN_BUILD_TESTS)
    gitman_set_ide_folder("Tests"
        gitman_messaging_asan_tests
        gitman_process_test_child
        gitman_tests)
endif()

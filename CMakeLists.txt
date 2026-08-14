cmake_minimum_required(VERSION 4.2.0 FATAL_ERROR)

cmake_policy(SET CMP0091 NEW)

set(GITMAN_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "${GITMAN_MSVC_RUNTIME_LIBRARY}")

project(gitman VERSION 0.1.0 LANGUAGES CXX RC)

set(GITMAN_CXX_STANDARD 20)
set(GITMAN_MINIMUM_MSVC_VERSION 1940)
set(GITMAN_MINIMUM_WINDOWS_SDK_VERSION "10.0.22621.0")
set(GITMAN_TARGET_ARCHITECTURE "x64")
set(GITMAN_VCPKG_TARGET_TRIPLET "x64-windows-static")
set(GITMAN_CATCH2_VERSION "3.15.3")
set(GITMAN_SUPPORTED_GENERATORS
    "Visual Studio 17 2022"
    "Visual Studio 18 2026"
)

set(GITMAN_CMAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/cmake")
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

set(CMAKE_CXX_STANDARD ${GITMAN_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GITMAN_BUILD_TESTS "Build the Gitman test targets." ON)
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

if(NOT VCPKG_TARGET_TRIPLET STREQUAL GITMAN_VCPKG_TARGET_TRIPLET)
    message(FATAL_ERROR
        "VCPKG_TARGET_TRIPLET must be ${GITMAN_VCPKG_TARGET_TRIPLET}. "
        "Current value: ${VCPKG_TARGET_TRIPLET}")
endif()

add_library(gitman_project_options INTERFACE)
target_compile_features(gitman_project_options INTERFACE cxx_std_${GITMAN_CXX_STANDARD})
gitman_apply_windows_options(gitman_project_options)
gitman_apply_msvc_options(
    gitman_project_options
    ENABLE_ANALYZE "${GITMAN_ENABLE_MSVC_ANALYZE}"
)

find_package(unofficial-skia CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_program(GITMAN_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

include(CTest)
if(BUILD_TESTING AND GITMAN_BUILD_TESTS)
    find_package(Catch2 ${GITMAN_CATCH2_VERSION} CONFIG REQUIRED)
endif()

add_subdirectory(src)
if(BUILD_TESTING AND GITMAN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

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

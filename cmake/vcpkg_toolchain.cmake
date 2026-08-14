if(DEFINED GITMAN_VCPKG_ROOT AND NOT GITMAN_VCPKG_ROOT STREQUAL "")
    set(_gitman_vcpkg_root "${GITMAN_VCPKG_ROOT}")
elseif(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(_gitman_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
    get_filename_component(
        _gitman_vcpkg_root
        "${CMAKE_CURRENT_LIST_DIR}/../build/vcpkg-baseline"
        ABSOLUTE)
    message(STATUS
        "VCPKG_ROOT is not set; using the project-local checkout: "
        "${_gitman_vcpkg_root}")
endif()

file(TO_CMAKE_PATH "${_gitman_vcpkg_root}" _gitman_vcpkg_root)
set(_gitman_vcpkg_toolchain
    "${_gitman_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
if(NOT EXISTS "${_gitman_vcpkg_toolchain}" OR
    NOT EXISTS "${_gitman_vcpkg_root}/vcpkg.exe")
    message(FATAL_ERROR
        "No usable vcpkg checkout was found: ${_gitman_vcpkg_root}\n"
        "Set VCPKG_ROOT or GITMAN_VCPKG_ROOT to the pinned checkout, or prepare "
        "build/vcpkg-baseline as described in docs/build.md.")
endif()

# vcpkg 내부 스크립트도 선택한 checkout을 일관되게 참조하게 한다.
set(ENV{VCPKG_ROOT} "${_gitman_vcpkg_root}")

# vcpkg가 CMake generator와 동일한 Visual Studio 설치본을 사용하게 한다.
# 첫 toolchain 평가에서는 CMAKE_GENERATOR_INSTANCE가 아직 비어 있으므로
# Visual Studio Installer에서 같은 major version의 설치 경로를 조회한다.
if(CMAKE_GENERATOR MATCHES "^Visual Studio 17 ")
    set(_gitman_vs_version_range "[17.0,18.0)")
elseif(CMAKE_GENERATOR MATCHES "^Visual Studio 18 ")
    set(_gitman_vs_version_range "[18.0,19.0)")
endif()

if(DEFINED _gitman_vs_version_range)
    cmake_host_system_information(
        RESULT _gitman_program_files_x86
        QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows/CurrentVersion"
        VALUE "ProgramFilesDir (x86)"
        VIEW 64)
    set(_gitman_vswhere
        "${_gitman_program_files_x86}/Microsoft Visual Studio/Installer/vswhere.exe")
    if(NOT EXISTS "${_gitman_vswhere}")
        message(FATAL_ERROR "Could not find vswhere.exe from Visual Studio Installer.")
    endif()

    execute_process(
        COMMAND
            "${_gitman_vswhere}"
            -latest
            -products *
            -version "${_gitman_vs_version_range}"
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
            -property installationPath
        RESULT_VARIABLE _gitman_vswhere_result
        OUTPUT_VARIABLE _gitman_vs_instance
        ERROR_VARIABLE _gitman_vswhere_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _gitman_vswhere_result EQUAL 0 OR _gitman_vs_instance STREQUAL "")
        message(FATAL_ERROR
            "Could not find a matching Visual Studio C++ installation: "
            "${_gitman_vswhere_error}")
    endif()

    file(TO_NATIVE_PATH "${_gitman_vs_instance}" _gitman_vs_instance_native)
    set(ENV{VCPKG_VISUAL_STUDIO_PATH} "${_gitman_vs_instance_native}")
    message(STATUS "vcpkg Visual Studio instance: ${_gitman_vs_instance}")
endif()

include("${_gitman_vcpkg_toolchain}")

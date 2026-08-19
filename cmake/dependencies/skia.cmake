include_guard(GLOBAL)

# Skia는 사용자가 손으로 1회 빌드한다 (docs/skia-build.md). CMake는 네트워크도 GN도
# 건드리지 않고 이미 존재하는 산출물만 검사해 imported target으로 노출한다. target
# 이름은 vcpkg 시절과 같게 유지해 `src`와 `tests`가 수정 없이 링크되게 한다.

# 산출물이 실제로 Gitman과 맞는지 configure 시점에 검사한다. 잘못된 옵션으로 빌드한
# Skia를 링크 오류가 아니라 원인이 드러나는 메시지로 잡는다.
function(gitman_check_skia_arguments build_directory)
    set(arguments_file "${build_directory}/args.gn")
    if(NOT EXISTS "${arguments_file}")
        message(FATAL_ERROR
            "Skia args.gn was not found: ${arguments_file}\n"
            "Build Skia first as described in docs/skia-build.md.")
    endif()

    file(READ "${arguments_file}" arguments_text)
    foreach(requirement IN LISTS GITMAN_SKIA_REQUIRED_ARGUMENTS)
        string(REPLACE "=" ";" requirement_parts "${requirement}")
        list(GET requirement_parts 0 requirement_name)
        list(GET requirement_parts 1 requirement_value)
        if(NOT arguments_text MATCHES
            "${requirement_name}[ \t]*=[ \t]*${requirement_value}")
            message(FATAL_ERROR
                "Skia was built without ${requirement_name} = ${requirement_value}.\n"
                "Build directory: ${build_directory}\n"
                "Rebuild Skia with the argument file in third_party/skia-args.")
        endif()
    endforeach()
endfunction()

# Skia 빌드는 skia.lib 하나가 아니라 external별 라이브러리를 함께 낸다. vcpkg는 이를
# 하나로 합쳐 주었지만 직접 빌드에서는 모두 링크해야 한다.
function(gitman_add_skia_component name)
    set(target "gitman_skia_${name}")
    add_library("${target}" STATIC IMPORTED GLOBAL)
    set_target_properties("${target}" PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${GITMAN_SKIA_BUILD_DEBUG}/${name}.lib"
        IMPORTED_LOCATION_RELEASE "${GITMAN_SKIA_BUILD_RELEASE}/${name}.lib")
endfunction()

function(gitman_find_skia)
    if(NOT IS_DIRECTORY "${GITMAN_SKIA_ROOT}")
        message(FATAL_ERROR
            "The Skia source tree was not found: ${GITMAN_SKIA_ROOT}\n"
            "Run: git submodule update --init third_party/skia")
    endif()
    if(NOT EXISTS "${GITMAN_SKIA_ROOT}/include/core/SkCanvas.h")
        message(FATAL_ERROR
            "The Skia source tree is incomplete: ${GITMAN_SKIA_ROOT}\n"
            "Run: git submodule update --init third_party/skia")
    endif()

    foreach(build_directory
        "${GITMAN_SKIA_BUILD_DEBUG}"
        "${GITMAN_SKIA_BUILD_RELEASE}")
        foreach(component IN LISTS GITMAN_SKIA_COMPONENTS)
            if(NOT EXISTS "${build_directory}/${component}.lib")
                message(FATAL_ERROR
                    "A Skia build output is missing: "
                    "${build_directory}/${component}.lib\n"
                    "Build Skia as described in docs/skia-build.md:\n"
                    "  scripts/build_skia.ps1 -Configuration Debug\n"
                    "  scripts/build_skia.ps1 -Configuration Release")
            endif()
        endforeach()
        gitman_check_skia_arguments("${build_directory}")
    endforeach()

    foreach(component IN LISTS GITMAN_SKIA_COMPONENTS)
        gitman_add_skia_component("${component}")
    endforeach()

    add_library(gitman_skia INTERFACE)
    target_include_directories(gitman_skia SYSTEM INTERFACE "${GITMAN_SKIA_ROOT}")
    target_link_libraries(gitman_skia
        INTERFACE
            gitman_skia_skia
            gitman_skia_skcms
            gitman_skia_spirv_cross
            gitman_skia_d3d12allocator
            # Skia가 요구하는 Windows 시스템 라이브러리다. Gitman이 직접 쓰는 것은
            # src/CMakeLists.txt에 따로 있다.
            d3dcompiler
            FontSub
            Usp10)
    add_library(unofficial::skia::skia ALIAS gitman_skia)

    # Debug 외 구성은 Release 산출물을 쓴다. 앱은 Debug와 Release만 쓰지만 IDE가
    # 다른 구성을 만들 수 있어 매핑을 명시한다.
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "Release;" PARENT_SCOPE)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release;" PARENT_SCOPE)

    message(STATUS "Skia: ${GITMAN_SKIA_ROOT}")
endfunction()

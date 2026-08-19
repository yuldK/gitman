include_guard(GLOBAL)

# 실행 파일 resource에 넣을 제3자 고지를 만든다 (ADR-002). vcpkg를 쓰던 시절에는
# installed 트리의 copyright 파일을 긁어 왔으나, 취득을 submodule로 옮기면서
# (ADR-006) 원문을 저장소에서 직접 읽는다. 고지 대상이 빌드 환경에 따라 달라지지
# 않는다는 이점이 있다.
#
# 목록은 실행 파일에 실제로 들어가는 것만 담는다. Catch2는 test 실행 파일에만
# 링크되므로 제외한다.
set(GITMAN_NOTICE_ENTRIES
    "Skia|third_party/skia/LICENSE"
    "SPIRV-Cross|third_party/skia-externals/spirv-cross/LICENSE"
    "SPIRV-Headers|third_party/skia-externals/spirv-headers/LICENSE"
    "D3D12 Memory Allocator|third_party/skia-externals/d3d12allocator/LICENSE.txt"
    "nlohmann/json|third_party/nlohmann-json/LICENSE.MIT"
    "Visual Studio Code Icons (Codicons)|assets/codicons/LICENSE"
    "Visual Studio Code Icons (Codicons) - Code|assets/codicons/LICENSE-CODE")

function(gitman_generate_third_party_notices output_file)
    if(NOT DEFINED PROJECT_SOURCE_DIR)
        message(FATAL_ERROR "PROJECT_SOURCE_DIR is required to generate notices.")
    endif()

    set(notice_text
        "Gitman 제3자 소프트웨어 고지\n"
        "================================\n\n"
        "이 파일은 실행 파일에 포함되는 제3자 구성 요소의 라이선스 원문을 모아\n"
        "생성합니다. 원문은 저장소의 submodule과 자산 디렉터리에서 직접 읽습니다.\n")

    foreach(entry IN LISTS GITMAN_NOTICE_ENTRIES)
        string(FIND "${entry}" "|" separator_index)
        if(separator_index EQUAL -1)
            message(FATAL_ERROR "Malformed notice entry: ${entry}")
        endif()
        string(SUBSTRING "${entry}" 0 ${separator_index} component_name)
        math(EXPR path_index "${separator_index} + 1")
        string(SUBSTRING "${entry}" ${path_index} -1 relative_path)

        set(license_file "${PROJECT_SOURCE_DIR}/${relative_path}")
        if(NOT EXISTS "${license_file}")
            message(FATAL_ERROR
                "A third-party license file is missing: ${relative_path}\n"
                "Component: ${component_name}\n"
                "Initialize the submodules: git submodule update --init --recursive")
        endif()

        file(READ "${license_file}" license_text)
        string(APPEND notice_text
            "\n\n----------------------------------------\n"
            "Component: ${component_name}\n"
            "----------------------------------------\n"
            "${license_text}")
    endforeach()

    get_filename_component(output_directory "${output_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    file(WRITE "${output_file}" "${notice_text}")
endfunction()

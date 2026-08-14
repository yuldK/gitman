function(gitman_generate_third_party_notices output_file)
    if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
        message(FATAL_ERROR "Cannot generate third-party notices without vcpkg manifest data.")
    endif()

    set(share_directory "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
    file(GLOB copyright_files LIST_DIRECTORIES false "${share_directory}/*/copyright")
    if(NOT copyright_files)
        message(FATAL_ERROR "No vcpkg copyright files found: ${share_directory}")
    endif()
    list(SORT copyright_files)

    set(notice_text
        "Gitman 제3자 소프트웨어 고지\n"
        "================================\n\n"
        "이 파일은 빌드에 실제로 포함된 vcpkg package의 copyright 파일을 모아 생성합니다.\n")

    foreach(copyright_file IN LISTS copyright_files)
        get_filename_component(package_directory "${copyright_file}" DIRECTORY)
        get_filename_component(package_name "${package_directory}" NAME)
        file(READ "${copyright_file}" copyright_text)
        string(APPEND notice_text
            "\n\n----------------------------------------\n"
            "Package: ${package_name}\n"
            "----------------------------------------\n"
            "${copyright_text}")
    endforeach()

    get_filename_component(output_directory "${output_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    file(WRITE "${output_file}" "${notice_text}")
endfunction()


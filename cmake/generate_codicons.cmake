if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE are required.")
endif()

file(READ "${INPUT_FILE}" mapping_json)
string(JSON codepoint_count LENGTH "${mapping_json}")
if(codepoint_count EQUAL 0)
    message(FATAL_ERROR "Codicons mapping is empty: ${INPUT_FILE}")
endif()

set(output_text "#pragma once\n\n#include <cstdint>\n\nnamespace gitman::codicons {\n")
math(EXPR last_codepoint_index "${codepoint_count} - 1")
set(generated_identifiers)

foreach(codepoint_index RANGE 0 ${last_codepoint_index})
    string(JSON codepoint MEMBER "${mapping_json}" ${codepoint_index})
    string(JSON alias_count LENGTH "${mapping_json}" "${codepoint}")
    math(EXPR last_alias_index "${alias_count} - 1")
    math(EXPR codepoint_hex "${codepoint}" OUTPUT_FORMAT HEXADECIMAL)

    foreach(alias_index RANGE 0 ${last_alias_index})
        string(JSON alias GET "${mapping_json}" "${codepoint}" ${alias_index})
        string(TOLOWER "${alias}" identifier)
        string(REGEX REPLACE "[^a-z0-9_]" "_" identifier "${identifier}")
        if(identifier MATCHES "^[0-9]")
            string(PREPEND identifier "number_")
        endif()
        string(PREPEND identifier "icon_")

        if(identifier IN_LIST generated_identifiers)
            message(FATAL_ERROR "Duplicate Codicon identifier: ${identifier}")
        endif()
        list(APPEND generated_identifiers "${identifier}")
        string(APPEND output_text
            "inline constexpr char32_t ${identifier} = static_cast<char32_t>(${codepoint_hex});\n")
    endforeach()
endforeach()

string(APPEND output_text "} // namespace gitman::codicons\n")
get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}" "${output_text}")


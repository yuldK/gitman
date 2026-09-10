if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE are required.")
endif()

# luil은 같은 mapping.json에서 `icon_<name>` 상수를 만든다. 그것은 컴파일 시점에
# 이름을 아는 코드용이라, 사용자가 문서에 적은 이름을 실행 중에 찾는 데는 쓸 수
# 없다. 여기서는 이름으로 정렬한 조회 표를 따로 만든다.
#
# 정렬 열쇠에 `!`(0x21)를 구분자로 쓰는 이유는 이름에 쓰이는 어떤 문자보다도
# 작기 때문이다. `-`(0x2D)를 쓰면 "add-small"이 "add"보다 앞서 정렬되어
# std::u8string_view의 사전식 비교(짧은 접두사가 먼저)와 어긋난다.
file(READ "${INPUT_FILE}" mapping_json)
string(JSON codepoint_count LENGTH "${mapping_json}")
if(codepoint_count EQUAL 0)
    message(FATAL_ERROR "Codicons mapping is empty: ${INPUT_FILE}")
endif()

math(EXPR last_codepoint_index "${codepoint_count} - 1")
set(sort_keys)

foreach(codepoint_index RANGE 0 ${last_codepoint_index})
    string(JSON codepoint MEMBER "${mapping_json}" ${codepoint_index})
    string(JSON alias_count LENGTH "${mapping_json}" "${codepoint}")
    math(EXPR last_alias_index "${alias_count} - 1")
    math(EXPR codepoint_hex "${codepoint}" OUTPUT_FORMAT HEXADECIMAL)

    foreach(alias_index RANGE 0 ${last_alias_index})
        string(JSON alias GET "${mapping_json}" "${codepoint}" ${alias_index})
        string(TOLOWER "${alias}" name)
        if(NOT name MATCHES "^[a-z0-9-]+$")
            message(FATAL_ERROR "Unexpected Codicon alias: ${alias}")
        endif()
        list(APPEND sort_keys "${name}!${codepoint_hex}")
    endforeach()
endforeach()

list(SORT sort_keys COMPARE STRING)
list(LENGTH sort_keys entry_count)

set(output_text "#pragma once\n\n")
string(APPEND output_text "// mapping.json에서 만든 이름 → 글리프 표다. 사용자가 설정에 적은 codicon\n")
string(APPEND output_text "// 이름을 실행 중에 찾는 데 쓴다. 이름 순으로 정렬되어 있어 이분 탐색이 가능하다.\n")
string(APPEND output_text "// 별칭도 모두 들어 있다 (`warning`·`sync`는 별칭이라 제외하면 찾지 못한다).\n\n")
string(APPEND output_text "#include <array>\n#include <string_view>\n\n")
string(APPEND output_text "namespace gitman::ui::generated {\n")
string(APPEND output_text "    struct codicon_name_entry\n    {\n")
string(APPEND output_text "        std::u8string_view name {};\n")
string(APPEND output_text "        char32_t codepoint { 0 };\n")
string(APPEND output_text "    };\n\n")
string(APPEND output_text "    inline constexpr std::array<codicon_name_entry, ${entry_count}> codicon_names {\n")
string(APPEND output_text "        {\n")

foreach(sort_key IN LISTS sort_keys)
    string(REGEX REPLACE "^([^!]+)!(.+)$" "\\1" name "${sort_key}")
    string(REGEX REPLACE "^([^!]+)!(.+)$" "\\2" value "${sort_key}")
    string(APPEND output_text "            { u8\"${name}\", static_cast<char32_t>(${value}) },\n")
endforeach()

string(APPEND output_text "        }\n")
string(APPEND output_text "    };\n")
string(APPEND output_text "} // namespace gitman::ui::generated\n")

get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}" "${output_text}")

if(NOT DEFINED ASSET_DIRECTORY)
    message(FATAL_ERROR "ASSET_DIRECTORY is required.")
endif()

set(asset_names codicon.ttf mapping.json LICENSE LICENSE-CODE)
set(expected_hashes
    3819e4ae4b87350e7c37a5d8f24e71ada2f1f2ee58f7ce5ebc1f88e3c8c38c80
    c9c9c568b3d166b22c9b21073cbad3928aefe8fbbef9262b336296985b520092
    af5e030844efddbc7ab00dcfea8b019703753d4d9f5172d727c533a492aec665
    9906940f61b1f0b533fa7d99baf55178b2808fbe113ea51dfbfad8572ccd5f2b)

list(LENGTH asset_names asset_count)
math(EXPR last_asset_index "${asset_count} - 1")
foreach(asset_index RANGE 0 ${last_asset_index})
    list(GET asset_names ${asset_index} asset_name)
    list(GET expected_hashes ${asset_index} expected_hash)
    set(asset_path "${ASSET_DIRECTORY}/${asset_name}")
    if(NOT EXISTS "${asset_path}")
        message(FATAL_ERROR "Codicons asset not found: ${asset_path}")
    endif()
    file(SHA256 "${asset_path}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "Codicons checksum mismatch: ${asset_name}\n"
            "Expected: ${expected_hash}\n"
            "Actual: ${actual_hash}")
    endif()
endforeach()

message(STATUS "Codicons v0.0.46-24 checksum verification passed")


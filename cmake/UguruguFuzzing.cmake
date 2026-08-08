if(NOT UGURUGU_ENABLE_FUZZING)
    return()
endif()

add_custom_target(ugurugu_fuzzers)

# Seeds are copied out of the fixtures the regular tests already use, so the
# starting corpus cannot drift away from the formats under test.
function(ugurugu_add_fuzzer target)
    cmake_parse_arguments(FUZZER "" "SOURCE;LIBRARY" "SEEDS" ${ARGN})
    add_executable(${target} EXCLUDE_FROM_ALL "${FUZZER_SOURCE}")
    target_link_libraries(${target} PRIVATE "${FUZZER_LIBRARY}")
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    ugurugu_target_defaults(${target})

    set(seeds)
    foreach(seed IN LISTS FUZZER_SEEDS)
        list(APPEND seeds "${CMAKE_CURRENT_SOURCE_DIR}/${seed}")
    endforeach()
    set(corpus "${CMAKE_BINARY_DIR}/fuzz-corpus/${target}")
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${corpus}"
        COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different ${seeds} "${corpus}"
        VERBATIM
    )

    add_dependencies(ugurugu_fuzzers ${target})
endfunction()

ugurugu_add_fuzzer(
    ugurugu_fuzz_wawa_v10
    SOURCE tests/fuzz/WawaV10ImporterFuzzer.cpp
    LIBRARY ugurugu_core
    SEEDS
    tests/fixtures/legacy-render/animated-paint-erase.wagle
    tests/fixtures/legacy-render/fill-hierarchy.wagle
    tests/fixtures/legacy-render/ordered-operations.wagle
)

ugurugu_add_fuzzer(
    ugurugu_fuzz_document_json
    SOURCE tests/fuzz/DocumentJsonFuzzer.cpp
    LIBRARY ugurugu_core
    SEEDS examples/Wave.ugu
)

# The clipboard payload is a single-layer document in the project schema, so a
# whole project makes a usable seed even though it is never a valid payload.
ugurugu_add_fuzzer(
    ugurugu_fuzz_selection_clipboard
    SOURCE tests/fuzz/SelectionClipboardFuzzer.cpp
    LIBRARY ugurugu_core
    SEEDS examples/Wave.ugu
)

ugurugu_add_fuzzer(
    ugurugu_fuzz_wwp_preset
    SOURCE tests/fuzz/WwpPresetFuzzer.cpp
    LIBRARY ugurugu_ui
    SEEDS examples/Wave.ugu
)

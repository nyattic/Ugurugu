set(
    WOBBLEPAINT_LLVM_TOOL_HINTS
    "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin"
    "/opt/homebrew/opt/llvm/bin"
    "/usr/local/opt/llvm/bin"
)
if(WIN32 AND CMAKE_GENERATOR_INSTANCE)
    list(
        APPEND
        WOBBLEPAINT_LLVM_TOOL_HINTS
        "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/x64/bin"
        "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/bin"
    )
endif()

find_program(
    WOBBLEPAINT_CLANG_FORMAT
    NAMES clang-format
    HINTS ${WOBBLEPAINT_LLVM_TOOL_HINTS}
)
if(WOBBLEPAINT_CLANG_FORMAT)
    file(
        GLOB_RECURSE
        WOBBLEPAINT_FORMAT_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.hpp"
    )
    add_custom_target(
        wobblepaint_format
        COMMAND
        "${WOBBLEPAINT_CLANG_FORMAT}"
        -i
        ${WOBBLEPAINT_FORMAT_SOURCES}
        VERBATIM
    )
    add_custom_target(
        wobblepaint_format_check
        COMMAND
        "${WOBBLEPAINT_CLANG_FORMAT}"
        --dry-run
        --Werror
        ${WOBBLEPAINT_FORMAT_SOURCES}
        VERBATIM
    )
endif()

find_program(
    WOBBLEPAINT_CLANG_TIDY
    NAMES clang-tidy
    HINTS ${WOBBLEPAINT_LLVM_TOOL_HINTS}
)
find_program(
    WOBBLEPAINT_RUN_CLANG_TIDY
    NAMES run-clang-tidy run-clang-tidy.py
    HINTS ${WOBBLEPAINT_LLVM_TOOL_HINTS}
)
set(
    WOBBLEPAINT_RUN_CLANG_TIDY_COMMAND
    "${WOBBLEPAINT_RUN_CLANG_TIDY}"
)
if(WIN32 AND WOBBLEPAINT_RUN_CLANG_TIDY)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        list(
            PREPEND
            WOBBLEPAINT_RUN_CLANG_TIDY_COMMAND
            "${Python3_EXECUTABLE}"
        )
    else()
        set(WOBBLEPAINT_RUN_CLANG_TIDY_COMMAND)
    endif()
endif()
if(WOBBLEPAINT_CLANG_TIDY AND WOBBLEPAINT_RUN_CLANG_TIDY_COMMAND)
    set(WOBBLEPAINT_TIDY_PATH_SEPARATOR "[/\\\\]")
    string(
        REPLACE
        "/"
        "${WOBBLEPAINT_TIDY_PATH_SEPARATOR}"
        WOBBLEPAINT_TIDY_SOURCE_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set(
        WOBBLEPAINT_TIDY_SOURCE_PATTERN
        "^${WOBBLEPAINT_TIDY_SOURCE_ROOT}${WOBBLEPAINT_TIDY_PATH_SEPARATOR}(src|tests|tools)${WOBBLEPAINT_TIDY_PATH_SEPARATOR}.*\\.(cpp|mm)$"
    )
    set(WOBBLEPAINT_CLANG_TIDY_ARGUMENTS)
    if(APPLE)
        execute_process(
            COMMAND xcrun --sdk macosx --show-sdk-path
            OUTPUT_VARIABLE WOBBLEPAINT_MACOS_SDK_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE WOBBLEPAINT_MACOS_SDK_RESULT
        )
        if(WOBBLEPAINT_MACOS_SDK_RESULT EQUAL 0)
            list(
                APPEND
                WOBBLEPAINT_CLANG_TIDY_ARGUMENTS
                -extra-arg=-isysroot
                "-extra-arg=${WOBBLEPAINT_MACOS_SDK_PATH}"
            )
        endif()
    endif()
    add_custom_target(
        wobblepaint_tidy
        COMMAND
        ${WOBBLEPAINT_RUN_CLANG_TIDY_COMMAND}
        -p
        "${CMAKE_BINARY_DIR}"
        -clang-tidy-binary
        "${WOBBLEPAINT_CLANG_TIDY}"
        -quiet
        ${WOBBLEPAINT_CLANG_TIDY_ARGUMENTS}
        "${WOBBLEPAINT_TIDY_SOURCE_PATTERN}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM
    )
    add_dependencies(wobblepaint_tidy WagleWaglePaint)
    if(TARGET wobblepaint_tests)
        add_dependencies(wobblepaint_tidy wobblepaint_tests)
    endif()
endif()

if(APPLE)
    add_executable(
        wobblepaint_render_release_notes
        tools/RenderReleaseNotes.cpp
    )
    target_link_libraries(
        wobblepaint_render_release_notes
        PRIVATE
        Qt6::Core
        Qt6::Gui
    )
    wobblepaint_target_defaults(wobblepaint_render_release_notes)
endif()

add_executable(
    wobblepaint_raster_asset_probe
    EXCLUDE_FROM_ALL
    tools/RasterAssetBudgetProbe.cpp
)
target_link_libraries(
    wobblepaint_raster_asset_probe
    PRIVATE
    Qt6::Core
    Qt6::Gui
)
wobblepaint_target_defaults(wobblepaint_raster_asset_probe)

add_executable(
    wobblepaint_fill_representation_probe
    EXCLUDE_FROM_ALL
    tools/FillRepresentationProbe.cpp
)
target_link_libraries(
    wobblepaint_fill_representation_probe
    PRIVATE
    wobblepaint_core
)
wobblepaint_target_defaults(wobblepaint_fill_representation_probe)

add_executable(
    wobblepaint_wawa_v10_probe
    EXCLUDE_FROM_ALL
    tools/WawaV10Probe.cpp
)
target_link_libraries(
    wobblepaint_wawa_v10_probe
    PRIVATE
    wobblepaint_core
)
wobblepaint_target_defaults(wobblepaint_wawa_v10_probe)

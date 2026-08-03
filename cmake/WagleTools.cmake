find_program(
    WOBBLEPAINT_CLANG_FORMAT
    NAMES clang-format
    HINTS
    "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin"
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
    HINTS
    "/opt/homebrew/opt/llvm/bin"
    "/usr/local/opt/llvm/bin"
)
find_program(
    WOBBLEPAINT_RUN_CLANG_TIDY
    NAMES run-clang-tidy run-clang-tidy.py
    HINTS
    "/opt/homebrew/opt/llvm/bin"
    "/usr/local/opt/llvm/bin"
)
if(WOBBLEPAINT_CLANG_TIDY AND WOBBLEPAINT_RUN_CLANG_TIDY)
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
        "${WOBBLEPAINT_RUN_CLANG_TIDY}"
        -p
        "${CMAKE_BINARY_DIR}"
        -clang-tidy-binary
        "${WOBBLEPAINT_CLANG_TIDY}"
        -quiet
        ${WOBBLEPAINT_CLANG_TIDY_ARGUMENTS}
        "^${CMAKE_CURRENT_SOURCE_DIR}/(src|tests|tools)/.*\\.(cpp|mm)$"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM
    )
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

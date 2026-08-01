if(NOT BUILD_TESTING)
    return()
endif()

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

qt_add_executable(wobblepaint_tests ${WOBBLEPAINT_TEST_SOURCES})
target_link_libraries(wobblepaint_tests PRIVATE wobblepaint_ui Qt6::Test)
target_compile_definitions(
    wobblepaint_tests
    PRIVATE
    WOBBLEPAINT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    WAGLEWAGLEPAINT_VERSION="${PROJECT_VERSION}"
)
wobblepaint_target_defaults(wobblepaint_tests)

set(WOBBLEPAINT_TEST_SUITES
    app
    document
    render
    gif
    mask
    release_notes
    stabilizer
    ui
)
foreach(suite IN LISTS WOBBLEPAINT_TEST_SUITES)
    set(test_name "wobblepaint_${suite}_tests")
    add_test(NAME ${test_name} COMMAND wobblepaint_tests)
    set_tests_properties(
        ${test_name}
        PROPERTIES
        ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;WOBBLEPAINT_TEST_SUITE=${suite}"
    )
    if(WIN32)
        set_tests_properties(
            ${test_name}
            PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endforeach()

if(APPLE)
    add_executable(wobblepaint_package_smoke tests/PackageSmoke.cpp)
    target_link_libraries(
        wobblepaint_package_smoke
        PRIVATE
        Qt6::Core
        Qt6::Gui
    )
    set_target_properties(
        wobblepaint_package_smoke
        PROPERTIES
        SKIP_BUILD_RPATH TRUE
    )
    wobblepaint_target_defaults(wobblepaint_package_smoke)
    add_custom_command(
        TARGET wobblepaint_package_smoke
        POST_BUILD
        COMMAND
        "${CMAKE_COMMAND}"
        "-DSMOKE_EXECUTABLE=$<TARGET_FILE:wobblepaint_package_smoke>"
        "-DSMOKE_BUILD_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/PreparePackageSmoke.cmake"
        VERBATIM
    )
endif()

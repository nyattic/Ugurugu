cmake_minimum_required(VERSION 3.25)

project(WobblePaint VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)
find_package(spdlog 1.13 REQUIRED CONFIG)
find_package(fmt REQUIRED CONFIG)

qt_standard_project_setup()

function(wobblepaint_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
        )
    else()
        target_compile_options(
            ${target}
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()

set(WOBBLEPAINT_CORE_SOURCES
    src/app/Logging.cpp
    src/app/Logging.hpp
    src/document/Document.cpp
    src/document/Document.hpp
    src/document/DocumentController.cpp
    src/document/DocumentController.hpp
    src/document/DocumentLimits.hpp
    src/io/DocumentSerializer.cpp
    src/io/DocumentSerializer.hpp
    src/io/GifWriter.cpp
    src/io/GifWriter.hpp
    src/render/RenderEngine.cpp
    src/render/RenderEngine.hpp
)

add_library(wobblepaint_core STATIC ${WOBBLEPAINT_CORE_SOURCES})
target_include_directories(wobblepaint_core PUBLIC src)
if(WIN32)
    target_compile_definitions(
        wobblepaint_core
        PUBLIC
        SPDLOG_WCHAR_FILENAMES
    )
endif()
target_link_libraries(
    wobblepaint_core
    PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog_header_only
)
wobblepaint_target_defaults(wobblepaint_core)

add_library(wobblepaint_ui STATIC
    src/ui/CanvasWidget.cpp
    src/ui/CanvasWidget.hpp
    src/ui/ColorSwatchRow.cpp
    src/ui/ColorSwatchRow.hpp
    src/ui/Icons.cpp
    src/ui/Icons.hpp
    src/ui/LayerDock.cpp
    src/ui/LayerDock.hpp
    src/ui/LayerItemDelegate.cpp
    src/ui/LayerItemDelegate.hpp
    src/ui/LayerListWidget.cpp
    src/ui/LayerListWidget.hpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.hpp
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.hpp
    src/ui/Theme.cpp
    src/ui/Theme.hpp
    src/ui/TimelineBar.cpp
    src/ui/TimelineBar.hpp
    src/ui/WobblePlayButton.cpp
    src/ui/WobblePlayButton.hpp
)

wobblepaint_target_defaults(wobblepaint_ui)
target_include_directories(wobblepaint_ui PUBLIC src)
target_link_libraries(wobblepaint_ui PUBLIC wobblepaint_core Qt6::Widgets)

qt_add_executable(WobblePaint src/main.cpp)

qt_add_translation(WOBBLEPAINT_QM_FILES i18n/wobblepaint_ko.ts)
qt_add_resources(WobblePaint "wobblepaint_translations"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${WOBBLEPAINT_QM_FILES}
)

target_link_libraries(WobblePaint PRIVATE wobblepaint_ui)
target_compile_definitions(
    WobblePaint
    PRIVATE
    WOBBLEPAINT_VERSION="${PROJECT_VERSION}"
)
wobblepaint_target_defaults(WobblePaint)

set_target_properties(WobblePaint PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "WobblePaint"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.wobblepaint.app"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

install(TARGETS WobblePaint
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    get_target_property(WOBBLEPAINT_FMT_TARGET_TYPE fmt::fmt TYPE)
    if(WOBBLEPAINT_FMT_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
        install(
            IMPORTED_RUNTIME_ARTIFACTS
            fmt::fmt
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
endif()

set(WOBBLEPAINT_DEPLOY_TOOL_OPTIONS)
set(WOBBLEPAINT_DEPLOY_PLUGIN_OPTIONS)
if(APPLE)
    get_filename_component(
        WOBBLEPAINT_QT_PREFIX
        "${Qt6_DIR}/../../.."
        ABSOLUTE
    )
    list(
        APPEND
        WOBBLEPAINT_DEPLOY_TOOL_OPTIONS
        "-libpath=${WOBBLEPAINT_QT_PREFIX}/lib"
    )
    set(WOBBLEPAINT_DEPLOY_PLUGIN_OPTIONS NO_PLUGINS)
    set(
        WOBBLEPAINT_QT_PLUGIN_ROOT
        "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}"
    )
    get_filename_component(
        WOBBLEPAINT_COCOA_PLUGIN
        "${WOBBLEPAINT_QT_PLUGIN_ROOT}/platforms/libqcocoa.dylib"
        REALPATH
    )
    get_filename_component(
        WOBBLEPAINT_OFFSCREEN_PLUGIN
        "${WOBBLEPAINT_QT_PLUGIN_ROOT}/platforms/libqoffscreen.dylib"
        REALPATH
    )
    get_filename_component(
        WOBBLEPAINT_MAC_STYLE_PLUGIN
        "${WOBBLEPAINT_QT_PLUGIN_ROOT}/styles/libqmacstyle.dylib"
        REALPATH
    )
    install(
        FILES
        "${WOBBLEPAINT_COCOA_PLUGIN}"
        "${WOBBLEPAINT_OFFSCREEN_PLUGIN}"
        DESTINATION
        "WobblePaint.app/Contents/PlugIns/platforms"
    )
    install(
        FILES
        "${WOBBLEPAINT_MAC_STYLE_PLUGIN}"
        DESTINATION
        "WobblePaint.app/Contents/PlugIns/styles"
    )
endif()

qt_generate_deploy_app_script(
    TARGET WobblePaint
    OUTPUT_SCRIPT wobblepaint_deploy_script
    DEPLOY_TOOL_OPTIONS ${WOBBLEPAINT_DEPLOY_TOOL_OPTIONS}
    ${WOBBLEPAINT_DEPLOY_PLUGIN_OPTIONS}
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${wobblepaint_deploy_script})

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

    qt_add_executable(wobblepaint_tests
        tests/DocumentTests.cpp
        tests/GifWriterTests.cpp
        tests/RenderEngineTests.cpp
        tests/UiTests.cpp
    )

    target_link_libraries(wobblepaint_tests PRIVATE wobblepaint_ui Qt6::Test)
    target_compile_definitions(
        wobblepaint_tests
        PRIVATE
        WOBBLEPAINT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    wobblepaint_target_defaults(wobblepaint_tests)
    add_test(NAME wobblepaint_tests COMMAND wobblepaint_tests)
    set_tests_properties(
        wobblepaint_tests
        PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    if(WIN32)
        set_tests_properties(
            wobblepaint_tests
            PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endif()

set(CPACK_PACKAGE_NAME "WobblePaint")
set(CPACK_PACKAGE_VENDOR "WobblePaint")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(
    CPACK_PACKAGE_FILE_NAME
    "WobblePaint-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}"
)
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
endif()
include(CPack)

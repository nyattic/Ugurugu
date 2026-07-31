cmake_minimum_required(VERSION 3.25)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(
        CMAKE_OSX_DEPLOYMENT_TARGET
        "14.0"
        CACHE STRING
        "Minimum supported macOS version"
        FORCE
    )
endif()

project(WagleWaglePaint VERSION 0.5.0 LANGUAGES CXX)

include(FetchContent)
if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
endif()
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(
    Qt6 6.10
    REQUIRED
    COMPONENTS
    Core
    Gui
    Widgets
    LinguistTools
)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    spdlog
    URL
    "https://github.com/gabime/spdlog/archive/refs/tags/v1.16.0.tar.gz"
    URL_HASH
    "SHA256=8741753e488a78dd0d0024c980e1fb5b5c85888447e309d9cb9d949bdb52aa3e"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(spdlog)

qt_standard_project_setup()

set(WAGLEWAGLEPAINT_UPDATE_SOURCE src/app/UpdateControllerStub.cpp)
set(WAGLEWAGLEPAINT_UPDATE_LIBRARIES)

if(APPLE)
    enable_language(OBJCXX)
    FetchContent_Declare(
        sparkle
        URL
        "https://github.com/sparkle-project/Sparkle/releases/download/2.9.4/Sparkle-2.9.4.tar.xz"
        URL_HASH
        "SHA256=ce89daf967db1e1893ed3ebd67575ed82d3902563e3191ca92aaec9164fbdef9"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(sparkle)
    find_library(
        WAGLEWAGLEPAINT_SPARKLE_FRAMEWORK
        Sparkle
        PATHS "${sparkle_SOURCE_DIR}"
        NO_DEFAULT_PATH
        REQUIRED
    )
    set(
        WAGLEWAGLEPAINT_UPDATE_SOURCE
        src/app/UpdateControllerMac.mm
    )
    list(
        APPEND
        WAGLEWAGLEPAINT_UPDATE_LIBRARIES
        "${WAGLEWAGLEPAINT_SPARKLE_FRAMEWORK}"
    )
elseif(WIN32)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Concurrent)
    FetchContent_Declare(
        velopack
        URL
        "https://github.com/velopack/velopack/releases/download/1.2.0/velopack_libc_1.2.0.zip"
        URL_HASH
        "SHA256=547262ed7a1ab1ff62f580aa53851ede2f1a451ac61b8974eb7bc01117488835"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(velopack)
    add_library(velopack_libc SHARED IMPORTED GLOBAL)
    set_target_properties(
        velopack_libc
        PROPERTIES
        IMPORTED_LOCATION
        "${velopack_SOURCE_DIR}/lib/velopack_libc_win_x64_msvc.dll"
        IMPORTED_IMPLIB
        "${velopack_SOURCE_DIR}/lib/velopack_libc_win_x64_msvc.dll.lib"
        INTERFACE_INCLUDE_DIRECTORIES
        "${velopack_SOURCE_DIR}/include"
    )
    set(
        WAGLEWAGLEPAINT_UPDATE_SOURCE
        src/app/UpdateControllerWindows.cpp
    )
    list(
        APPEND
        WAGLEWAGLEPAINT_UPDATE_LIBRARIES
        Qt6::Concurrent
        velopack_libc
    )
endif()

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
            /external:anglebrackets
            /external:W0
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
    src/brush/BrushPreset.cpp
    src/brush/BrushPreset.hpp
    src/document/Document.cpp
    src/document/Document.hpp
    src/document/DocumentController.cpp
    src/document/DocumentController.hpp
    src/document/DocumentLimits.hpp
    src/document/StrokeMask.cpp
    src/document/StrokeMask.hpp
    src/io/DocumentSerializer.cpp
    src/io/DocumentSerializer.hpp
    src/io/GifWriter.cpp
    src/io/GifWriter.hpp
    src/render/RenderEngine.cpp
    src/render/RenderEngine.hpp
)

if(MSVC)
    set_property(
        SOURCE src/app/Logging.cpp
        APPEND
        PROPERTY COMPILE_OPTIONS /wd4702
    )
endif()

add_library(wobblepaint_core STATIC ${WOBBLEPAINT_CORE_SOURCES})
target_include_directories(wobblepaint_core PUBLIC src)
target_link_libraries(
    wobblepaint_core
    PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
)
wobblepaint_target_defaults(wobblepaint_core)

add_library(wobblepaint_ui STATIC
    src/ui/BrushPopoverPanel.cpp
    src/ui/BrushPopoverPanel.hpp
    src/ui/BrushPresetButton.cpp
    src/ui/BrushPresetButton.hpp
    src/ui/BrushSizeRow.cpp
    src/ui/BrushSizeRow.hpp
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
    src/ui/PopoverToolButton.cpp
    src/ui/PopoverToolButton.hpp
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.hpp
    src/ui/Theme.cpp
    src/ui/Theme.hpp
    src/ui/TimelineBar.cpp
    src/ui/TimelineBar.hpp
    src/ui/ToolPopover.cpp
    src/ui/ToolPopover.hpp
    src/ui/WobblePlayButton.cpp
    src/ui/WobblePlayButton.hpp
)

wobblepaint_target_defaults(wobblepaint_ui)
target_include_directories(wobblepaint_ui PUBLIC src)
target_link_libraries(wobblepaint_ui PUBLIC wobblepaint_core Qt6::Widgets)

qt_add_executable(
    WagleWaglePaint
    src/app/UpdateController.hpp
    ${WAGLEWAGLEPAINT_UPDATE_SOURCE}
    src/main.cpp
)

if(APPLE)
    set_source_files_properties(
        resources/icons/WobblePaint.icns
        PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(
        WagleWaglePaint
        PRIVATE
        resources/icons/WobblePaint.icns
    )
    set_source_files_properties(
        src/app/UpdateControllerMac.mm
        PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
    configure_file(
        resources/macos/Info.plist.in
        "${CMAKE_CURRENT_BINARY_DIR}/WagleWaglePaint-Info.plist"
        @ONLY
    )
elseif(WIN32)
    configure_file(
        resources/icons/WobblePaint.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/WagleWaglePaint.rc"
        @ONLY
    )
    target_sources(
        WagleWaglePaint
        PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/WagleWaglePaint.rc"
    )
endif()

qt_add_translation(WOBBLEPAINT_QM_FILES i18n/wobblepaint_ko.ts)
qt_add_resources(WagleWaglePaint "wobblepaint_translations"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${WOBBLEPAINT_QM_FILES}
)

target_link_libraries(
    WagleWaglePaint
    PRIVATE
    wobblepaint_ui
    ${WAGLEWAGLEPAINT_UPDATE_LIBRARIES}
)
target_compile_definitions(
    WagleWaglePaint
    PRIVATE
    WAGLEWAGLEPAINT_VERSION="${PROJECT_VERSION}"
)
wobblepaint_target_defaults(WagleWaglePaint)

set_target_properties(WagleWaglePaint PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "WagleWaglePaint"
    MACOSX_BUNDLE_ICON_FILE "WobblePaint.icns"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.waglewaglepaint.app"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

if(APPLE)
    set_target_properties(
        WagleWaglePaint
        PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_BINARY_DIR}/WagleWaglePaint-Info.plist"
        INSTALL_RPATH
        "@executable_path/../Frameworks"
    )
endif()

install(TARGETS WagleWaglePaint
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    install(
        FILES
        "${velopack_SOURCE_DIR}/lib/velopack_libc_win_x64_msvc.dll"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME velopack_libc.dll
    )
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
        "-libpath=${sparkle_SOURCE_DIR}"
        "-codesign=-"
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
        "WagleWaglePaint.app/Contents/PlugIns/platforms"
    )
    install(
        FILES
        "${WOBBLEPAINT_MAC_STYLE_PLUGIN}"
        DESTINATION
        "WagleWaglePaint.app/Contents/PlugIns/styles"
    )
    install(
        DIRECTORY
        "${sparkle_SOURCE_DIR}/Sparkle.framework"
        DESTINATION
        "WagleWaglePaint.app/Contents/Frameworks"
        USE_SOURCE_PERMISSIONS
    )
endif()

qt_generate_deploy_app_script(
    TARGET WagleWaglePaint
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

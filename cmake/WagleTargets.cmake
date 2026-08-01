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

add_library(wobblepaint_ui STATIC ${WOBBLEPAINT_UI_SOURCES})
wobblepaint_target_defaults(wobblepaint_ui)
target_include_directories(wobblepaint_ui PUBLIC src)
target_link_libraries(wobblepaint_ui PUBLIC wobblepaint_core Qt6::Widgets)

if(APPLE)
    target_sources(
        wobblepaint_ui
        PRIVATE
        src/ui/MacWindowChrome.hpp
        src/ui/MacWindowChrome.mm
    )
    set_source_files_properties(
        src/ui/MacWindowChrome.mm
        PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
    target_link_libraries(wobblepaint_ui PUBLIC "-framework AppKit")
endif()

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

qt_add_translation(
    WOBBLEPAINT_QM_FILES
    i18n/wobblepaint_ko.ts
    i18n/wobblepaint_ja.ts
)
qt_add_resources(WagleWaglePaint "wobblepaint_translations"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${WOBBLEPAINT_QM_FILES}
)
qt_add_resources(WagleWaglePaint "wobblepaint_fonts"
    PREFIX "/fonts"
    BASE "resources/fonts"
    FILES resources/fonts/PretendardJP-Medium.otf
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

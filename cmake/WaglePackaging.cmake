install(TARGETS WagleWaglePaint
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(APPLE)
    set(
        WOBBLEPAINT_DOCUMENTATION_DESTINATION
        "WagleWaglePaint.app/Contents/Resources"
    )
elseif(WIN32)
    set(WOBBLEPAINT_DOCUMENTATION_DESTINATION ".")
else()
    set(
        WOBBLEPAINT_DOCUMENTATION_DESTINATION
        "${CMAKE_INSTALL_DATADIR}/WagleWaglePaint"
    )
endif()
install(
    FILES
    LICENSE
    README.md
    README.en.md
    README.ja.md
    THIRD_PARTY_NOTICES.md
    DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
)
install(
    FILES resources/licenses/LGPL-3.0.txt
    DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
)
install(
    FILES resources/fonts/OFL.txt
    DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
    RENAME Pretendard-OFL.txt
)
install(
    FILES "${spdlog_SOURCE_DIR}/LICENSE"
    DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
    RENAME spdlog-LICENSE.txt
)
install(
    FILES "${webp_SOURCE_DIR}/COPYING"
    DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
    RENAME libwebp-LICENSE.txt
)
install(
    FILES "${webp_SOURCE_DIR}/PATENTS"
    DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
    RENAME libwebp-PATENTS.txt
)

if(APPLE)
    install(
        FILES "${sparkle_SOURCE_DIR}/LICENSE"
        DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
        RENAME Sparkle-LICENSE.txt
    )
elseif(WIN32)
    install(
        FILES resources/licenses/Velopack-LICENSE.txt
        DESTINATION "${WOBBLEPAINT_DOCUMENTATION_DESTINATION}"
        RENAME Velopack-LICENSE.txt
    )
endif()

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
        WOBBLEPAINT_JPEG_PLUGIN
        "${WOBBLEPAINT_QT_PLUGIN_ROOT}/imageformats/libqjpeg.dylib"
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
        DESTINATION
        "WagleWaglePaint.app/Contents/PlugIns/platforms"
    )
    install(
        FILES
        "${WOBBLEPAINT_JPEG_PLUGIN}"
        DESTINATION
        "WagleWaglePaint.app/Contents/PlugIns/imageformats"
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

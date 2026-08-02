include(FetchContent)

find_package(
    Qt6 6.10
    REQUIRED
    COMPONENTS
    Core
    Concurrent
    Gui
    Widgets
    LinguistTools
)

set(
    WOBBLEPAINT_DISTRIBUTION_QT_VERSION
    ""
    CACHE STRING
    "Exact Qt version required for distribution builds"
)
if(WOBBLEPAINT_DISTRIBUTION_QT_VERSION
    AND NOT Qt6_VERSION VERSION_EQUAL WOBBLEPAINT_DISTRIBUTION_QT_VERSION
)
    message(
        FATAL_ERROR
        "Distribution builds require Qt "
        "${WOBBLEPAINT_DISTRIBUTION_QT_VERSION}, found ${Qt6_VERSION}"
    )
endif()

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

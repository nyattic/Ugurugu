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

project(WagleWaglePaint VERSION 1.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CTest)
include(WagleBuildSettings)
include(WagleDependencies)
include(WagleSources)
include(WagleTargets)
include(WaglePackaging)
include(WagleTests)
include(WagleTools)

if(NOT DEFINED SMOKE_EXECUTABLE OR NOT EXISTS "${SMOKE_EXECUTABLE}")
    message(FATAL_ERROR "The package smoke executable does not exist.")
endif()

execute_process(
    COMMAND /usr/bin/otool -L "${SMOKE_EXECUTABLE}"
    OUTPUT_VARIABLE SMOKE_DEPENDENCIES
    RESULT_VARIABLE OTOOL_RESULT
)
if(NOT OTOOL_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not inspect the package smoke executable.")
endif()

execute_process(
    COMMAND /usr/bin/otool -l "${SMOKE_EXECUTABLE}"
    OUTPUT_VARIABLE SMOKE_LOAD_COMMANDS
    RESULT_VARIABLE OTOOL_LOAD_COMMAND_RESULT
)
if(NOT OTOOL_LOAD_COMMAND_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Could not inspect the package smoke executable load commands."
    )
endif()
if(NOT DEFINED SMOKE_BUILD_DIRECTORY)
    message(FATAL_ERROR "The package smoke build directory is not defined.")
endif()
string(
    FIND
    "${SMOKE_LOAD_COMMANDS}"
    "path ${SMOKE_BUILD_DIRECTORY}"
    BUILD_RPATH_POSITION
)
if(NOT BUILD_RPATH_POSITION EQUAL -1)
    message(FATAL_ERROR
        "The package smoke executable must not contain a build RPATH."
    )
endif()

foreach(FRAMEWORK IN ITEMS QtCore QtGui)
    string(
        REGEX MATCH
        "[^ \t\r\n]+/${FRAMEWORK}\\.framework/Versions/[^/ \t\r\n]+/${FRAMEWORK}"
        SOURCE_DEPENDENCY
        "${SMOKE_DEPENDENCIES}"
    )
    if(NOT SOURCE_DEPENDENCY)
        message(FATAL_ERROR
            "Could not find the ${FRAMEWORK} dependency in the package smoke executable."
        )
    endif()
    string(
        REGEX REPLACE
        "^.*(${FRAMEWORK}\\.framework/Versions/[^/]+/${FRAMEWORK})$"
        "\\1"
        BUNDLE_DEPENDENCY
        "${SOURCE_DEPENDENCY}"
    )
    execute_process(
        COMMAND
        /usr/bin/install_name_tool
        -change
        "${SOURCE_DEPENDENCY}"
        "@executable_path/../Frameworks/${BUNDLE_DEPENDENCY}"
        "${SMOKE_EXECUTABLE}"
        RESULT_VARIABLE INSTALL_NAME_RESULT
    )
    if(NOT INSTALL_NAME_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Could not rewrite the ${FRAMEWORK} dependency for package smoke."
        )
    endif()
endforeach()

execute_process(
    COMMAND /usr/bin/codesign --force --sign - "${SMOKE_EXECUTABLE}"
    RESULT_VARIABLE CODESIGN_RESULT
)
if(NOT CODESIGN_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not sign the package smoke executable.")
endif()

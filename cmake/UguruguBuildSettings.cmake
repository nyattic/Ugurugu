set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(
    UGURUGU_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)
option(
    UGURUGU_ENABLE_COVERAGE
    "Enable Clang source coverage instrumentation"
    OFF
)
option(
    UGURUGU_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors"
    OFF
)

if(UGURUGU_ENABLE_SANITIZERS AND MSVC)
    message(FATAL_ERROR "UGURUGU_ENABLE_SANITIZERS requires Clang or GCC")
endif()
if(UGURUGU_ENABLE_COVERAGE
    AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"
)
    message(FATAL_ERROR "UGURUGU_ENABLE_COVERAGE requires Clang")
endif()
if(UGURUGU_ENABLE_SANITIZERS AND UGURUGU_ENABLE_COVERAGE)
    message(FATAL_ERROR "Sanitizers and coverage cannot be enabled together")
endif()

if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
endif()

function(ugurugu_target_defaults target)
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
        if(UGURUGU_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        if(UGURUGU_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(UGURUGU_ENABLE_SANITIZERS)
            target_compile_options(
                ${target}
                PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
                -fno-sanitize-recover=all
            )
            target_link_options(
                ${target}
                PRIVATE
                -fsanitize=address,undefined
                -fno-sanitize-recover=all
            )
        endif()
        if(UGURUGU_ENABLE_COVERAGE)
            target_compile_options(
                ${target}
                PRIVATE
                -fprofile-instr-generate
                -fcoverage-mapping
            )
            target_link_options(
                ${target}
                PRIVATE
                -fprofile-instr-generate
            )
        endif()
    endif()
endfunction()

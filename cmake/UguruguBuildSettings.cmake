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
# Off by default and deliberately not a CI gate: the prebuilt Qt is not
# instrumented, so the synchronisation inside QMutex, QThreadPool and
# QtConcurrent is invisible to ThreadSanitizer and every handoff across them is
# reported as a race. Reports naming only Ugurugu frames on both sides are the
# ones worth reading; anything whose two accesses meet inside Qt is not.
option(
    UGURUGU_ENABLE_THREAD_SANITIZER
    "Enable ThreadSanitizer (reports false races against an uninstrumented Qt)"
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
# Instruments every target for coverage-guided fuzzing and builds the
# libFuzzer entry points under tests/fuzz. Meant to be combined with
# UGURUGU_ENABLE_SANITIZERS so that a mis-parse becomes a crash.
option(
    UGURUGU_ENABLE_FUZZING
    "Build libFuzzer entry points for the untrusted-input parsers"
    OFF
)

if(UGURUGU_ENABLE_SANITIZERS AND MSVC)
    message(FATAL_ERROR "UGURUGU_ENABLE_SANITIZERS requires Clang or GCC")
endif()
if(UGURUGU_ENABLE_THREAD_SANITIZER AND MSVC)
    message(FATAL_ERROR "UGURUGU_ENABLE_THREAD_SANITIZER requires Clang or GCC")
endif()
# ThreadSanitizer maintains its own shadow memory and cannot share a process
# with AddressSanitizer.
if(UGURUGU_ENABLE_THREAD_SANITIZER AND UGURUGU_ENABLE_SANITIZERS)
    message(
        FATAL_ERROR
        "ThreadSanitizer and AddressSanitizer cannot be enabled together"
    )
endif()
if(UGURUGU_ENABLE_THREAD_SANITIZER AND UGURUGU_ENABLE_COVERAGE)
    message(
        FATAL_ERROR
        "ThreadSanitizer and coverage cannot be enabled together"
    )
endif()
if(UGURUGU_ENABLE_COVERAGE
    AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"
)
    message(FATAL_ERROR "UGURUGU_ENABLE_COVERAGE requires Clang")
endif()
if(UGURUGU_ENABLE_SANITIZERS AND UGURUGU_ENABLE_COVERAGE)
    message(FATAL_ERROR "Sanitizers and coverage cannot be enabled together")
endif()
# MSVC covers clang-cl too, whose driver does not accept the sanitizer flags
# below.
if(UGURUGU_ENABLE_FUZZING
    AND (MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
)
    message(FATAL_ERROR "UGURUGU_ENABLE_FUZZING requires Clang or GCC")
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
        if(UGURUGU_ENABLE_FUZZING)
            target_compile_options(
                ${target}
                PRIVATE
                -fsanitize=fuzzer-no-link
            )
        endif()
        if(UGURUGU_ENABLE_THREAD_SANITIZER)
            target_compile_options(
                ${target}
                PRIVATE
                -fsanitize=thread
                -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE -fsanitize=thread)
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

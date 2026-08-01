set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(
    WOBBLEPAINT_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

if(WOBBLEPAINT_ENABLE_SANITIZERS AND MSVC)
    message(FATAL_ERROR "WOBBLEPAINT_ENABLE_SANITIZERS requires Clang or GCC")
endif()

if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
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
        if(WOBBLEPAINT_ENABLE_SANITIZERS)
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
    endif()
endfunction()

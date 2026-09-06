cmake_minimum_required(VERSION 4.0)

project(nixie
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(NIXIE_BUILD_EXAMPLES "Build the nixie example programs" ON)
option(NIXIE_BUILD_TESTS "Build the nixie test suite" ON)
option(NIXIE_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Treat every compiler warning as an error across the whole project.
add_library(nixie_warnings INTERFACE)

if(MSVC)
    target_compile_options(nixie_warnings INTERFACE /W4 /WX)
    # The codebase already null-terminates every strncpy/sprintf call by hand;
    # silence MSVC's "unsafe function" deprecation (C4996) for the standard
    # C library calls used here and in bundled third-party headers (stb_image).
    target_compile_definitions(nixie_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(nixie_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# Opt-in ASan+UBSan instrumentation, shared by the library, the examples and
# the test suite so a single flag rebuilds everything consistently.
add_library(nixie_sanitizers INTERFACE)

if(NIXIE_ENABLE_SANITIZERS)
    if(MSVC)
        message(WARNING "NIXIE_ENABLE_SANITIZERS has no effect with MSVC; ignoring.")
    else()
        target_compile_options(nixie_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(nixie_sanitizers INTERFACE -fsanitize=address,undefined)
    endif()
endif()

add_subdirectory(src)

if(NIXIE_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(NIXIE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

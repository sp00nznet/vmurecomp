# vmurecomp - static-recompilation toolkit for Dreamcast VMU software.
#
# Builds two things:
#   * vmurecomp      - the runtime library recompiled code links against
#                      (CPU semantics, RAM/SFR map, LCD, timers, buzzer,
#                      dispatch). No external dependencies at this layer.
#   * lc8670recomp   - the recompiler CLI (decode / discover / emit), plus
#                      vmurun, the interpreter oracle used for bring-up.
#
# A per-title repo consumes this via add_subdirectory(vmurecomp) as a git
# submodule and links `vmurecomp`. See README.md.
cmake_minimum_required(VERSION 3.16)
project(vmurecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
    # The CRT deprecation warnings fire on fopen/snprintf, which are exactly
    # what portable C code should be using.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# --- runtime library -------------------------------------------------------
add_library(vmurecomp STATIC
    src/cpu.c
    src/mem.c
    src/lcd.c
    src/timer.c
    src/audio.c
    src/recomp_rt.c
)
target_include_directories(vmurecomp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- recompiler CLI --------------------------------------------------------
add_subdirectory(tools/lc8670recomp)

# --- recompiled game host (opt-in) -----------------------------------------
# Point this at the directory `lc8670recomp recomp` wrote and it builds
# `vmuhost`, which executes the generated C rather than interpreting. The
# directory holds code derived from your own dump and is never committed.
#
#   cmake -B build -DVMURECOMP_GENERATED=generated
set(VMURECOMP_GENERATED "" CACHE PATH "Directory holding generated recomp_funcs.c")
if(VMURECOMP_GENERATED)
    if(NOT EXISTS "${VMURECOMP_GENERATED}/recomp_funcs.c")
        message(FATAL_ERROR
            "VMURECOMP_GENERATED=${VMURECOMP_GENERATED} has no recomp_funcs.c. "
            "Run: lc8670recomp recomp <image> ${VMURECOMP_GENERATED}")
    endif()
    add_executable(vmuhost
        tools/lc8670recomp/host.c
        ${VMURECOMP_GENERATED}/recomp_funcs.c
        ${VMURECOMP_GENERATED}/recomp_dispatch.c
    )
    target_include_directories(vmuhost PRIVATE ${VMURECOMP_GENERATED})
    target_link_libraries(vmuhost PRIVATE vmurecomp)
    if(MSVC)
        # One function per LC8670 routine means a single translation unit with
        # thousands of labels; the default section limit is not enough.
        target_compile_options(vmuhost PRIVATE /bigobj)
    endif()
endif()

# --- tests (only when vmurecomp is the top-level project) ------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()

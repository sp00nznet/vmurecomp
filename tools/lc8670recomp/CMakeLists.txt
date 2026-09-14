# The recompiler CLI and the interpreter oracle share the decoder, container
# parser and analyzer, so those live in a small object library.
add_library(lc8670core STATIC
    decode.c
    sfr.c
    vms.c
    analyze.c
    emit.c
)
target_include_directories(lc8670core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(lc8670recomp main.c)
target_link_libraries(lc8670recomp PRIVATE lc8670core)

# vmurun interprets against the runtime, so it links the library too.
add_executable(vmurun vmurun.c)
target_link_libraries(vmurun PRIVATE lc8670core vmurecomp)

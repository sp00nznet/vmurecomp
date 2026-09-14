# Unit tests plus the conformance harness. Everything here runs on synthetic
# input; the image corpus is opt-in via VMURECOMP_CORPUS and lives outside the
# repository.

add_executable(test_decode test_decode.c)
target_link_libraries(test_decode PRIVATE lc8670core)
add_test(NAME decode COMMAND test_decode)

add_executable(test_runtime test_runtime.c)
target_link_libraries(test_runtime PRIVATE vmurecomp)
add_test(NAME runtime COMMAND test_runtime)

add_executable(test_pipeline test_pipeline.c)
target_link_libraries(test_pipeline PRIVATE lc8670core)
# Emitted C lands in the build tree, never in the source tree.
add_test(NAME pipeline
         COMMAND test_pipeline ${CMAKE_CURRENT_BINARY_DIR})

add_executable(conform conform.c)
target_link_libraries(conform PRIVATE lc8670core)
add_test(NAME conformance
         COMMAND conform "" ${CMAKE_CURRENT_SOURCE_DIR}/conformance-baseline.txt)

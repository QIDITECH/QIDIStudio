# Keep generated-fixture and exact arithmetic contracts independent of stale
# legacy test APIs. These are the actual product integration source files.
add_executable(qidi_gpu_geometry_tests
    main.cpp
    ../libslic3r/test_gpu_exact_geometry.cpp
    ../libslic3r/test_gpu_batch_preflight.cpp
    ../fff_print/test_gpu_fill.cpp
    ../fff_print/test_gpu_tree_contours.cpp)
target_compile_features(qidi_gpu_geometry_tests PRIVATE cxx_std_17)
target_include_directories(qidi_gpu_geometry_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
# libslic3r's LogSink and NSVGUtils use the app's Http/QDT encryption and
# NanoSVG implementations. Reuse their real GUI library and transitive crypto
# dependencies, as the native slic3rutils suite does; do not stub those symbols.
target_link_libraries(qidi_gpu_geometry_tests PRIVATE test_common libslic3r libslic3r_gui)
if(WIN32)
    target_link_libraries(qidi_gpu_geometry_tests PRIVATE bcrypt)
endif()
set_property(TARGET qidi_gpu_geometry_tests PROPERTY FOLDER "tests")
# Catch's default selection runs every non-hidden case, including the distinct
# [GpuBatchPreflight] tag. Physical-GPU cases retain their hidden tag.
add_test(NAME qidi_gpu_geometry_tests COMMAND qidi_gpu_geometry_tests)

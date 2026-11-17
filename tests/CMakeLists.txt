# GoogleTest is obtained via vcpkg (see vcpkg/ at the repository root), the
# same dependency manager used for OpenCV. Install it with:
#   vcpkg/vcpkg.exe install gtest:x64-windows
# and configure CMake with the vcpkg toolchain file as documented in the
# top-level README.

find_package(GTest CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Shared test-support sources (synthetic image generators etc.)
# ---------------------------------------------------------------------------
add_library(optinspect_test_support STATIC
    support/SyntheticImages.cpp
)
target_include_directories(optinspect_test_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_SOURCE_DIR}/src/application
)
target_link_libraries(optinspect_test_support PUBLIC optinspect_engine)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
add_executable(optinspect_unit_tests
    unit/test_file_image_source.cpp
    unit/test_image_processor.cpp
    unit/test_threshold_processor.cpp
    unit/test_morphological_processor.cpp
    unit/test_contour_detector.cpp
    unit/test_defect_measurement.cpp
    unit/test_inspection_decider.cpp
    unit/test_config.cpp
    unit/test_batch_discovery.cpp
    unit/test_performance_report.cpp
    unit/test_cli_arguments.cpp
)
target_link_libraries(optinspect_unit_tests PRIVATE
    optinspect_engine
    optinspect_application
    optinspect_test_support
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(optinspect_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/application)

include(GoogleTest)
gtest_discover_tests(optinspect_unit_tests)

# ---------------------------------------------------------------------------
# Integration tests
# ---------------------------------------------------------------------------
add_executable(optinspect_integration_tests
    integration/test_full_inspection.cpp
    integration/test_database_persistence.cpp
    integration/test_database_cli.cpp
)
target_link_libraries(optinspect_integration_tests PRIVATE
    optinspect_engine
    optinspect_application
    optinspect_database
    optinspect_test_support
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(optinspect_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/application ${CMAKE_CURRENT_SOURCE_DIR}/support)

gtest_discover_tests(optinspect_integration_tests)

# ---------------------------------------------------------------------------
# Database tests (Phase 2). They link only the database library (no OpenCV)
# and use temporary database files exclusively.
# ---------------------------------------------------------------------------
add_executable(optinspect_database_tests
    database/test_connection.cpp
    database/test_repositories.cpp
    database/test_analytics.cpp
)
target_link_libraries(optinspect_database_tests PRIVATE
    optinspect_database
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(optinspect_database_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)

gtest_discover_tests(optinspect_database_tests)

# Manually run synthetic benchmark (not registered with CTest); see
# docs/performance-report.md for the recorded results.
add_executable(optinspect_database_benchmark database/benchmark_database.cpp)
target_link_libraries(optinspect_database_benchmark PRIVATE optinspect_database)
target_include_directories(optinspect_database_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
# ---------------------------------------------------------------------------
# Phase 3 tests (only when the GUI is built, see OPTINSPECT_BUILD_GUI).
#  - optinspect_visualization_tests: pure logic (coordinate transforms, image formats, overlay
#    mapping, texture cache policy). No OpenGL context, no desktop session needed.
#  - optinspect_gui_integration_tests: headless GUI workflow (controller, worker, history,
#    analytics). No window, no OpenGL.
#  - optinspect_visualization_gl_smoke: needs a real OpenGL 3.3 context; every test is SKIPPED
#    when none can be created (headless CI). Registered with CTest like the others.
#  - optinspect_visualization_benchmark: manual micro-benchmark (not registered with CTest).
# ---------------------------------------------------------------------------
if(TARGET optinspect_visualization)
    add_executable(optinspect_visualization_tests
        visualization/ViewportTransformTests.cpp
        visualization/ImageFormatTests.cpp
        visualization/OverlayMappingTests.cpp
        visualization/TextureCacheTests.cpp
    )
    target_link_libraries(optinspect_visualization_tests PRIVATE
        optinspect_visualization
        GTest::gtest
        GTest::gtest_main
    )
    gtest_discover_tests(optinspect_visualization_tests)

    add_executable(optinspect_gui_integration_tests
        integration/test_gui_workflow.cpp
    )
    target_link_libraries(optinspect_gui_integration_tests PRIVATE
        optinspect_gui_core
        optinspect_test_support
        GTest::gtest
        GTest::gtest_main
    )
    target_include_directories(optinspect_gui_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
    gtest_discover_tests(optinspect_gui_integration_tests)

    add_executable(optinspect_visualization_gl_smoke visualization/GLSmokeTests.cpp)
    target_link_libraries(optinspect_visualization_gl_smoke PRIVATE
        optinspect_visualization
        GTest::gtest
        GTest::gtest_main
    )
    target_compile_definitions(optinspect_visualization_gl_smoke PRIVATE
        OPTINSPECT_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/resources/shaders")
    gtest_discover_tests(optinspect_visualization_gl_smoke)

    add_executable(optinspect_visualization_benchmark visualization/benchmark_visualization.cpp)
    target_link_libraries(optinspect_visualization_benchmark PRIVATE optinspect_visualization opencv_imgcodecs opencv_imgproc)
    target_compile_definitions(optinspect_visualization_benchmark PRIVATE
        OPTINSPECT_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/resources/shaders")
endif()

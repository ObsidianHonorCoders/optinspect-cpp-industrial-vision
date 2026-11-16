cmake_minimum_required(VERSION 3.15)

project(OptiInspect
    VERSION 1.0.0
    DESCRIPTION "OptiInspect - Industrial Optical Inspection Engine (Phase 1)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep runtime outputs together regardless of generator (single- vs
# multi-config), which simplifies running the executable and tests from a
# known location after the build.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(OUTPUTCONFIG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG})
endforeach()

if(MSVC)
    add_compile_options(/W4 /permissive- /MP)
    # Suppress noisy warnings from OpenCV headers which are outside our control.
    add_compile_options(/wd4251 /wd4275)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# NOTE: OpenCV's default vcpkg features pull in dnn/gapi -> abseil, which
# requires a C++ toolset newer than VS2017; this project uses traditional CV
# only (no dnn), so OpenCV is installed via vcpkg with an explicit, reduced
# feature set instead of the defaults:
#   vcpkg/vcpkg.exe install "opencv4[core,highgui,calib3d,png,jpeg]:x64-windows"
# (core bundles imgproc/imgcodecs/video/features2d; png/jpeg enable PNG and
# JPEG read/write support in imgcodecs, which is required for loading/saving
# the PCB reference/inspected images (DeepPCB samples are JPEG) and the
# generated report/annotation images used throughout this repo.)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui calib3d features2d video)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR
        "OpenCV was not found. Configure it via vcpkg (recommended):\n"
        "  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake\n"
        "or provide -DOpenCV_DIR=<path to OpenCVConfig.cmake>.")
endif()
message(STATUS "Found OpenCV ${OpenCV_VERSION} at ${OpenCV_DIR}")

# ---------------------------------------------------------------------------
# Core engine library (everything except the CLI entry point), so tests can
# link against the same compiled objects as the executable.
# ---------------------------------------------------------------------------
add_library(optinspect_engine STATIC
    src/common/Config.cpp
    src/acquisition/FileImageSource.cpp
    src/processing/ImageProcessor.cpp
    src/processing/ImageAligner.cpp
    src/processing/ThresholdProcessor.cpp
    src/processing/MorphologicalProcessor.cpp
    src/processing/ImageComparator.cpp
    src/detection/DefectMeasurement.cpp
    src/detection/ContourDetector.cpp
    src/detection/DefectDetector.cpp
    src/detection/InspectionDecider.cpp
    src/detection/InspectionResult.cpp
)

target_include_directories(optinspect_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(optinspect_engine PUBLIC
    opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui opencv_calib3d opencv_features2d opencv_video
)

# ---------------------------------------------------------------------------
# Application-layer library (pipeline orchestration, CLI, reporting,
# annotation). Kept separate from optinspect_engine so its headers remain
# private to the application and are not exposed as part of the public
# include/ API surface.
# ---------------------------------------------------------------------------
add_library(optinspect_application STATIC
    src/application/BatchDiscovery.cpp
    src/application/PerformanceReport.cpp
    src/application/CliArguments.cpp
    src/application/InspectionPipeline.cpp
    src/application/ReportWriter.cpp
    src/application/ResultAnnotator.cpp
)

target_include_directories(optinspect_application PUBLIC
    ${CMAKE_SOURCE_DIR}/src/application
)

target_link_libraries(optinspect_application PUBLIC optinspect_engine)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(optinspect src/application/main.cpp)
target_link_libraries(optinspect PRIVATE optinspect_application)

# ---------------------------------------------------------------------------
# Tests (GoogleTest, fetched via CMake's FetchContent if not already
# available locally; see tests/CMakeLists.txt for details).
# ---------------------------------------------------------------------------
option(OPTINSPECT_BUILD_TESTS "Build the OptiInspect test suite" ON)
if(OPTINSPECT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

cmake_minimum_required(VERSION 3.15)

project(OptiInspect
    VERSION 1.0.0
    DESCRIPTION "OptiInspect - Industrial Optical Inspection Engine with SQLite persistence and quality analytics"
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
# Database layer (Phase 2): SQLite connection management, schema/migrations,
# repositories, persistence service and quality analytics. Deliberately has
# no dependency on OpenCV or the inspection engine; it exchanges plain data
# records (include/database/Models.hpp) with the application layer.
#
# SQLite comes from vcpkg:  vcpkg/vcpkg.exe install sqlite3:x64-windows
# ---------------------------------------------------------------------------
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(optinspect_database STATIC
    src/database/DatabaseConnection.cpp
    src/database/Statement.cpp
    src/database/DatabaseTransaction.cpp
    src/database/DatabaseInitializer.cpp
    src/database/Schema.cpp
    src/database/Models.cpp
    src/database/TimeUtil.cpp
    src/database/InspectionQuery.cpp
    src/database/ProductRepository.cpp
    src/database/InspectionRepository.cpp
    src/database/DefectRepository.cpp
    src/database/InspectionImageRepository.cpp
    src/database/InspectionPersistenceService.cpp
    src/database/QualityAnalyticsService.cpp
)

target_include_directories(optinspect_database PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(optinspect_database PUBLIC unofficial::sqlite3::sqlite3)

# ---------------------------------------------------------------------------
# Application-layer library (pipeline orchestration, CLI, reporting,
# annotation). Kept separate from optinspect_engine so its headers remain
# private to the application and are not exposed as part of the public
# include/ API surface.
# ---------------------------------------------------------------------------
add_library(optinspect_application STATIC
    src/application/BatchDiscovery.cpp
    src/application/DatabaseCommands.cpp
    src/application/InspectionRecorder.cpp
    src/application/PerformanceReport.cpp
    src/application/CliArguments.cpp
    src/application/InspectionPipeline.cpp
    src/application/InspectionService.cpp
    src/application/ReportWriter.cpp
    src/application/ResultAnnotator.cpp
)

target_include_directories(optinspect_application PUBLIC
    ${CMAKE_SOURCE_DIR}/src/application
)

target_link_libraries(optinspect_application PUBLIC optinspect_engine optinspect_database)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(optinspect src/application/main.cpp)
target_link_libraries(optinspect PRIVATE optinspect_application)

# ---------------------------------------------------------------------------
# Phase 3: OpenGL desktop GUI (optional). The engine, database, application
# library and CLI above never depend on any of this; configure with
# -DOPTINSPECT_BUILD_GUI=OFF to build the CLI and tests without the GUI
# dependencies (GLFW, GLAD, Dear ImGui).
#
#   vcpkg install glfw3 "glad[gl-api-33,loader]" "imgui[glfw-binding,opengl3-binding,docking-experimental]"
# ---------------------------------------------------------------------------
option(OPTINSPECT_BUILD_GUI "Build the OpenGL desktop GUI (optinspect_gui)" ON)

if(OPTINSPECT_BUILD_GUI)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)

    # Reusable OpenGL rendering components. Depends on OpenCV (image data), GLFW
    # and GLAD only - not on the inspection pipeline and not on the database.
    add_library(optinspect_visualization STATIC
        src/visualization/GLContext.cpp
        src/visualization/Shader.cpp
        src/visualization/Texture2D.cpp
        src/visualization/ImageFormat.cpp
        src/visualization/ImageRenderer.cpp
        src/visualization/ImageViewport.cpp
        src/visualization/ViewportTransform.cpp
        src/visualization/DefectOverlayRenderer.cpp
        src/visualization/GpuTimer.cpp
    )
    target_include_directories(optinspect_visualization PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(optinspect_visualization PUBLIC opencv_core glad::glad glfw)

    # GUI logic without any window/OpenGL/ImGui code: controller, inspection
    # worker, history/analytics access, image loading, command line. Testable headless.
    add_library(optinspect_gui_core STATIC
        src/application/gui/ImageLoader.cpp
        src/application/gui/HistoryService.cpp
        src/application/gui/InspectionWorker.cpp
        src/application/gui/GuiInspectionController.cpp
        src/application/gui/GuiOptions.cpp
    )
    target_include_directories(optinspect_gui_core PUBLIC ${CMAKE_SOURCE_DIR}/src/application/gui)
    target_link_libraries(optinspect_gui_core PUBLIC optinspect_application)
    find_package(Threads REQUIRED)
    target_link_libraries(optinspect_gui_core PUBLIC Threads::Threads)

    add_executable(optinspect_gui
        src/application/gui/main_gui.cpp
        src/application/gui/GuiApplication.cpp
        src/application/gui/MainWindow.cpp
        src/application/gui/InspectionPanel.cpp
        src/application/gui/ImageComparisonPanel.cpp
        src/application/gui/ResultsPanel.cpp
        src/application/gui/DefectPanel.cpp
        src/application/gui/HistoryPanel.cpp
        src/application/gui/AnalyticsPanel.cpp
        src/application/gui/PerformancePanel.cpp
        src/application/gui/NativeFileDialog.cpp
    )
    target_link_libraries(optinspect_gui PRIVATE optinspect_gui_core optinspect_visualization imgui::imgui)
    if(WIN32)
        target_link_libraries(optinspect_gui PRIVATE ole32 shell32 uuid)
    endif()
    target_compile_definitions(optinspect_gui PRIVATE
        OPTINSPECT_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/resources/shaders")
    # Make the GLSL files available next to the executable.
    add_custom_command(TARGET optinspect_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/resources/shaders
                $<TARGET_FILE_DIR:optinspect_gui>/resources/shaders
        COMMENT "Copying shaders next to optinspect_gui")
endif()

# ---------------------------------------------------------------------------
# Tests (GoogleTest, fetched via CMake's FetchContent if not already
# available locally; see tests/CMakeLists.txt for details).
# ---------------------------------------------------------------------------
option(OPTINSPECT_BUILD_TESTS "Build the OptiInspect test suite" ON)
if(OPTINSPECT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

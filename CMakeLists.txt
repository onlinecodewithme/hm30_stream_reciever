cmake_minimum_required(VERSION 3.19)

# ---------------------------------------------------------------------------
# Project declaration — version is the single source of truth.
# CMake propagates it into version.h via configure_file.
# ---------------------------------------------------------------------------
project(hm30_rtp_receiver
    VERSION      2.0.0
    DESCRIPTION  "Production-grade H.264 RTP/UDP receiver and dashboard for the SIYI HM30."
    LANGUAGES    CXX
)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)  # Enforce ISO C++17, no GNU extensions.

# ---------------------------------------------------------------------------
# Qt automation
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------------------------------------------------------------------------
# Build type defaults
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type set — defaulting to Release.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose: Debug Release RelWithDebInfo" FORCE)
endif()

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
# Hardening and diagnostic flags applied to all targets.
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-deprecated-declarations  # FFmpeg headers trigger these on some distros.
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG -fstack-protector-strong)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Version header (generated at configure time)
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC  REQUIRED libavcodec)
pkg_check_modules(AVUTIL   REQUIRED libavutil)
pkg_check_modules(SWSCALE  REQUIRED libswscale)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
    main.cpp
    src/core/stream_decoder.cpp
    src/ui/video_widget.cpp
    src/ui/dashboard.cpp
)

set(APP_RESOURCES
    resources/resources.qrc
)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${APP_RESOURCES}
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}"        # For #include "src/..."
    "${CMAKE_BINARY_DIR}/generated"  # For version.h
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
)

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Widgets
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    pthread
)

# ---------------------------------------------------------------------------
# Data files — copy to build directory so the binary can find them.
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_SOURCE_DIR}/stream.sdp"
    "${CMAKE_BINARY_DIR}/stream.sdp"
    COPYONLY
)

# ---------------------------------------------------------------------------
# Install targets (optional — `cmake --install build --prefix /opt/hm30`)
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(FILES   stream.sdp      DESTINATION share/${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Optional: ROS2 integration
#
# Source ROS2 before configuring:
#   source /opt/ros/humble/setup.bash && cd build && cmake .. && make -j$(nproc)
# ---------------------------------------------------------------------------
find_package(ament_cmake QUIET)
find_package(rclcpp      QUIET)
find_package(sensor_msgs QUIET)

if(rclcpp_FOUND AND sensor_msgs_FOUND)
    target_sources(${PROJECT_NAME} PRIVATE 
        src/ros2/ros2_bridge.cpp 
        src/ros2/ros2_bridge.h
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_ROS2)
    
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/src/ros2"
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        rclcpp::rclcpp
        ${sensor_msgs_TARGETS}
    )

    message(STATUS "  ROS2 integration : ENABLED  (topic /hm30/image_raw)")
else()
    message(STATUS "  ROS2 integration : DISABLED (source /opt/ros/humble/setup.bash first)")
endif()

# ---------------------------------------------------------------------------
# Build summary
# ---------------------------------------------------------------------------
message(STATUS "------------------------------------------------------------")
message(STATUS "  Project  : ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "  Build    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  GUI app  : hm30_rtp_receiver")
message(STATUS "------------------------------------------------------------")

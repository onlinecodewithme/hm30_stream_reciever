cmake_minimum_required(VERSION 3.8)
project(uav_control_ui_cpp)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(map_msgs REQUIRED)
find_package(cognition_brain_interfaces REQUIRED)

# Qt5 dependencies
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt5 COMPONENTS Core Quick Gui Qml REQUIRED)

# Create Qt Resources file for QML and images
set(QRC_FILE qml.qrc)
# qml.qrc is manually managed now
add_executable(uav_control_ui
  src/main.cpp
  src/RosBackend.cpp
  src/VideoStreamItem.cpp
  ${QRC_FILE}
)

target_include_directories(uav_control_ui PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(uav_control_ui
  Qt5::Core Qt5::Quick Qt5::Gui Qt5::Qml
)

ament_target_dependencies(uav_control_ui rclcpp std_msgs sensor_msgs geometry_msgs nav_msgs map_msgs cognition_brain_interfaces)

install(TARGETS uav_control_ui
  DESTINATION lib/${PROJECT_NAME})

ament_package()

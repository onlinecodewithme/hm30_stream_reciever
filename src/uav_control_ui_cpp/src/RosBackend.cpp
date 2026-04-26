#include "RosBackend.h"
#include <iostream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <geometry_msgs/msg/point.hpp>
#include <std_msgs/msg/float32.hpp>

RosBackend::RosBackend(QObject *parent) : QObject(parent)
{
    // Start with dummy values or connected=false if we want to show "Waiting for ROS connection..."
}

RosBackend::~RosBackend()
{
}

void RosBackend::setNode(std::shared_ptr<rclcpp::Node> node)
{
    m_node = node;
    if (m_node) {
        m_connected = true;
        emit connectedChanged();
        
        m_imageSub = m_node->create_subscription<sensor_msgs::msg::Image>(
            "/hm30/image_raw", 10,
            std::bind(&RosBackend::imageCallback, this, std::placeholders::_1));

        m_padStatusSub = m_node->create_subscription<std_msgs::msg::String>(
            "/drone_pad/status", 10,
            std::bind(&RosBackend::padStatusCallback, this, std::placeholders::_1));

        m_padClient = m_node->create_client<cognition_brain_interfaces::srv::DronePadControl>("/drone_pad_control");
        
        m_gimbalPosPub = m_node->create_publisher<geometry_msgs::msg::Point>("/ugv_gimbal/position", 10);
        m_gimbalHomePub = m_node->create_publisher<std_msgs::msg::String>("/ugv_gimbal/home", 10);
        
        m_cameraPulsePub = m_node->create_publisher<std_msgs::msg::Float32>("/main_camera_pulse", 10);
        
        m_cmdVelPub = m_node->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);

        m_cmdVelTimer = new QTimer(this);
        connect(m_cmdVelTimer, &QTimer::timeout, this, &RosBackend::publishCmdVel);
        m_cmdVelTimer->start(100); // 10Hz continuous publish

        // Map & Odometry
        m_mapSub = m_node->create_subscription<nav_msgs::msg::OccupancyGrid>(
            "/global_traversability_map", rclcpp::QoS(rclcpp::KeepLast(5)).reliable().durability_volatile(),
            std::bind(&RosBackend::mapCallback, this, std::placeholders::_1));
            
        m_mapUpdateSub = m_node->create_subscription<map_msgs::msg::OccupancyGridUpdate>(
            "/global_traversability_map_updates", 10,
            std::bind(&RosBackend::mapUpdateCallback, this, std::placeholders::_1));
            
        m_odomSub = m_node->create_subscription<nav_msgs::msg::Odometry>(
            "/Odometry",
            rclcpp::QoS(rclcpp::KeepLast(5)).reliable().durability_volatile(),
            std::bind(&RosBackend::odomCallback, this, std::placeholders::_1));
    }
}

void RosBackend::padStatusCallback(const std_msgs::msg::String::SharedPtr msg)
{
    QString jsonStr = QString::fromStdString(msg->data);
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        std::cerr << "Failed to parse drone pad status JSON: " << msg->data << std::endl;
        return;
    }
    
    QJsonObject obj = doc.object();
    QString newStatus = obj["status"].toString();
    
    if (m_padStatus != newStatus) {
        m_padStatus = newStatus;
        emit padStatusChanged();
    }
}

void RosBackend::imageCallback(const sensor_msgs::msg::Image::SharedPtr msg)
{
    if (msg->encoding != "rgb8") {
        std::cerr << "Unsupported image encoding: " << msg->encoding << std::endl;
        return;
    }

    QImage image(msg->data.data(), msg->width, msg->height, msg->step, QImage::Format_RGB888);
    // The QImage uses the memory from msg, so we must clone it before the msg is destroyed
    emit newFrameReceived(image.copy());
}

void RosBackend::setIsAuto(bool isAuto)
{
    if (m_isAuto != isAuto) {
        m_isAuto = isAuto;
        emit isAutoChanged();
    }
}

void RosBackend::setMappingEnabled(bool enabled)
{
    if (m_mappingEnabled != enabled) {
        m_mappingEnabled = enabled;
        emit mappingEnabledChanged();
    }
}

void RosBackend::setMainCameraOn(bool isOn)
{
    if (m_mainCameraOn != isOn) {
        m_mainCameraOn = isOn;
        emit mainCameraOnChanged();
        
        if (m_cameraPulsePub) {
            auto msg = std_msgs::msg::Float32();
            msg.data = isOn ? 1.0f : 3.0f;
            m_cameraPulsePub->publish(msg);
            std::cout << "Published /main_camera_pulse: " << msg.data << std::endl;
        }
    }
}

void RosBackend::updateLeftJoystick(double x, double y)
{
    // Asymmetrical mapping for X (Pan)
    // Center is 2050. Min is 0, Max is 5100.
    if (x < 0) {
        m_panX = 2050 + (x * 2050); // -1 to 0 maps to 0 to 2050
    } else {
        m_panX = 2050 + (x * 3050); // 0 to 1 maps to 2050 to 5100
    }
    
    // Asymmetrical mapping for Y (Tilt)
    // Center is -4000. Range is -4100 to 0.
    // Inverted direction based on user feedback
    if (y < 0) {
        // Dragging UP (-1 to 0) maps to -4100 to -4000
        m_tiltY = -4000 + (y * 100); 
    } else {
        // Dragging DOWN (0 to 1) maps to -4000 to 0
        m_tiltY = -4000 + (y * 4000);
    }

    // Clamp values just to be perfectly safe
    if (m_panX < 0) m_panX = 0;
    if (m_panX > 5100) m_panX = 5100;
    if (m_tiltY < -4100) m_tiltY = -4100;
    if (m_tiltY > 0) m_tiltY = 0;

    emit panXChanged();
    emit tiltYChanged();
    
    std::cout << "Gimbal Joystick -> Pan: " << m_panX << ", Tilt: " << m_tiltY << std::endl;
    
    if (m_gimbalPosPub) {
        auto msg = geometry_msgs::msg::Point();
        msg.x = m_panX;
        msg.y = m_tiltY;
        msg.z = 0.0;
        m_gimbalPosPub->publish(msg);
    }
}

void RosBackend::setMaxSpeed(double speed)
{
    if (m_maxSpeed != speed) {
        m_maxSpeed = speed;
        emit maxSpeedChanged();
        
        // Re-apply current inputs with the new multiplier
        // This is especially necessary for keyboard inputs that don't trigger continuous events
        updateRightJoystick(m_rightJoyX, m_rightJoyY);
    }
}

void RosBackend::setTemperature(int temp)
{
    if (m_temperature != temp) {
        m_temperature = temp;
        emit temperatureChanged();
    }
}

void RosBackend::setBattery(int batt)
{
    if (m_battery != batt) {
        m_battery = batt;
        emit batteryChanged();
    }
}

void RosBackend::updateRightJoystick(double x, double y)
{
    m_rightJoyX = x;
    m_rightJoyY = y;

    // Linear / Angular speed mapping
    // x = angular (left/right), y = linear (forward/back)
    m_linearSpeed = -y * m_maxSpeed; // Up is -y in UI coordinates usually, so we invert
    m_angularSpeed = x * m_maxSpeed;

    emit linearSpeedChanged();
    emit angularSpeedChanged();
    
    // Immediate publish for responsiveness, timer will take over for continuous
    publishCmdVel();
}

void RosBackend::publishCmdVel()
{
    if (m_cmdVelPub && !m_isAuto) {
        auto twist = geometry_msgs::msg::Twist();
        twist.linear.x = m_linearSpeed;
        twist.angular.z = -m_angularSpeed; 
        m_cmdVelPub->publish(twist);
    }
}

void RosBackend::stopAction()
{
    std::cout << "STOP action triggered!" << std::endl;
    m_rightJoyX = 0.0;
    m_rightJoyY = 0.0;
    m_linearSpeed = 0.0;
    m_angularSpeed = 0.0;
    emit linearSpeedChanged();
    emit angularSpeedChanged();
    
    if (m_cmdVelPub) {
        auto twist = geometry_msgs::msg::Twist();
        m_cmdVelPub->publish(twist); // Immediate stop publish (ignoring isAuto check to force stop)
    }
}

void RosBackend::cameraAction()
{
    std::cout << "Camera action triggered!" << std::endl;
}

void RosBackend::targetAction()
{
    std::cout << "Target action triggered (Gimbal Default)!" << std::endl;
    m_panX = 2050;
    m_tiltY = -4000;
    emit panXChanged();
    emit tiltYChanged();

    if (m_gimbalHomePub) {
        auto msg = std_msgs::msg::String();
        msg.data = "default";
        m_gimbalHomePub->publish(msg);
    }
}

void RosBackend::gimbalHomeAction()
{
    std::cout << "Gimbal Home action triggered!" << std::endl;
    m_panX = 2050;
    m_tiltY = -4000;
    emit panXChanged();
    emit tiltYChanged();

    if (m_gimbalHomePub) {
        auto msg = std_msgs::msg::String();
        msg.data = "home";
        m_gimbalHomePub->publish(msg);
    }
}

void RosBackend::launchUAV()
{
    std::cout << "Launch UAV action triggered!" << std::endl;
    
    // Instantly update UI for responsiveness
    m_padStatus = "opening";
    emit padStatusChanged();
    
    if (!m_padClient) {
        std::cout << "Error: Pad client is null!" << std::endl;
        return;
    }

    auto request = std::make_shared<cognition_brain_interfaces::srv::DronePadControl::Request>();
    request->command = "open";

    std::cout << "Sending /drone_pad_control {command: 'open'} ..." << std::endl;

    m_padClient->async_send_request(request, [](rclcpp::Client<cognition_brain_interfaces::srv::DronePadControl>::SharedFuture future) {
        auto result = future.get();
        if (result->success) {
            std::cout << "Drone pad open command success: " << result->message << std::endl;
        } else {
            std::cout << "Drone pad open command failed: " << result->message << std::endl;
        }
    });
}

void RosBackend::cancelTakeoff()
{
    std::cout << "Cancel Takeoff action triggered!" << std::endl;

    // Instantly update UI for responsiveness
    m_padStatus = "closing";
    emit padStatusChanged();

    if (!m_padClient) {
        std::cout << "Error: Pad client is null!" << std::endl;
        return;
    }

    auto request = std::make_shared<cognition_brain_interfaces::srv::DronePadControl::Request>();
    request->command = "close";

    std::cout << "Sending /drone_pad_control {command: 'close'} ..." << std::endl;

    m_padClient->async_send_request(request, [](rclcpp::Client<cognition_brain_interfaces::srv::DronePadControl>::SharedFuture future) {
        auto result = future.get();
        if (result->success) {
            std::cout << "Drone pad close command success: " << result->message << std::endl;
        } else {
            std::cout << "Drone pad close command failed: " << result->message << std::endl;
        }
    });
}

void RosBackend::mapCallback(const nav_msgs::msg::OccupancyGrid::SharedPtr msg)
{
    m_gridWidth = msg->info.width;
    m_gridHeight = msg->info.height;
    m_mapResolution = msg->info.resolution;
    m_mapOriginX = msg->info.origin.position.x;
    m_mapOriginY = msg->info.origin.position.y;

    m_gridDataRaw.assign(msg->data.begin(), msg->data.end());
    
    QVariantList newGrid;
    newGrid.reserve(m_gridDataRaw.size());
    for (int8_t v : m_gridDataRaw) {
        newGrid.append(v);
    }
    m_gridDataQ = newGrid;

    if (!m_usingLiveData) {
        m_usingLiveData = true;
        emit usingLiveDataChanged();
    }
    emit gridWidthChanged();
    emit gridHeightChanged();
    emit gridDataChanged();
    
    computeMapBounds();
}

void RosBackend::mapUpdateCallback(const map_msgs::msg::OccupancyGridUpdate::SharedPtr msg)
{
    if (m_gridWidth == 0 || m_gridHeight == 0 || m_gridDataRaw.empty()) {
        return; // Wait for full map first
    }
    
    int start_x = msg->x;
    int start_y = msg->y;
    int update_w = msg->width;
    int update_h = msg->height;

    // Update raw array
    for (int ry = 0; ry < update_h; ++ry) {
        for (int rx = 0; rx < update_w; ++rx) {
            int map_x = start_x + rx;
            int map_y = start_y + ry;
            if (map_x >= 0 && map_x < m_gridWidth && map_y >= 0 && map_y < m_gridHeight) {
                int map_idx = map_y * m_gridWidth + map_x;
                int update_idx = ry * update_w + rx;
                m_gridDataRaw[map_idx] = msg->data[update_idx];
                m_gridDataQ[map_idx] = msg->data[update_idx]; // Update QVariantList directly
            }
        }
    }
    
    emit gridDataChanged();
    computeMapBounds();
}

void RosBackend::odomCallback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    double world_x = msg->pose.pose.position.x;
    double world_y = msg->pose.pose.position.y;

    // Extract Yaw from Quaternion
    double qx = msg->pose.pose.orientation.x;
    double qy = msg->pose.pose.orientation.y;
    double qz = msg->pose.pose.orientation.z;
    double qw = msg->pose.pose.orientation.w;
    double siny_cosp = 2 * (qw * qz + qx * qy);
    double cosy_cosp = 1 - 2 * (qy * qy + qz * qz);
    double yaw = std::atan2(siny_cosp, cosy_cosp);

    // Convert world coordinates to grid coordinates (if map is loaded)
    double gridX, gridY;
    if (m_gridWidth > 0 && m_mapResolution > 0.0) {
        gridX = (world_x - m_mapOriginX) / m_mapResolution;
        gridY = (world_y - m_mapOriginY) / m_mapResolution;
    } else {
        // No map yet — store raw world coords; QML won't show until map arrives
        gridX = world_x;
        gridY = world_y;
    }

    // Marshal property changes to Qt main thread safely
    QMetaObject::invokeMethod(this, [this, gridX, gridY, yaw]() {
        m_robotX = gridX;
        m_robotY = gridY;
        m_robotAngle = yaw;
        emit robotXChanged();
        emit robotYChanged();
        emit robotAngleChanged();
    }, Qt::QueuedConnection);
}

void RosBackend::computeMapBounds() {
    int minX = m_gridWidth;
    int minY = m_gridHeight;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < m_gridHeight; ++y) {
        for (int x = 0; x < m_gridWidth; ++x) {
            if (m_gridDataRaw[y * m_gridWidth + x] != -1) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX >= minX && maxY >= minY) {
        if (m_mapMinX != minX || m_mapMinY != minY || m_mapMaxX != maxX || m_mapMaxY != maxY) {
            m_mapMinX = minX;
            m_mapMinY = minY;
            m_mapMaxX = maxX;
            m_mapMaxY = maxY;
            emit mapBoundsChanged();
        }
    }
}

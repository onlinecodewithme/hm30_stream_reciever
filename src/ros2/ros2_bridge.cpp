#include "ros2_bridge.h"
#include <QDebug>

Ros2Bridge::Ros2Bridge(std::shared_ptr<rclcpp::Node> node, QObject *parent)
    : QObject(parent), m_node(std::move(node))
{
    // Initialize ROS publisher
    m_pub = m_node->create_publisher<sensor_msgs::msg::Image>("/hm30/image_raw", 10);
    qInfo() << "[Ros2Bridge] Initialized publisher on /hm30/image_raw";
}

void Ros2Bridge::onFrameReady(const QImage &frame)
{
    if (!m_pub || !rclcpp::ok()) {
        return;
    }

    // Ensure the frame is in RGB888 format
    QImage rgbFrame = frame.convertToFormat(QImage::Format_RGB888);
    if (rgbFrame.isNull()) {
        return;
    }

    auto msg = std::make_unique<sensor_msgs::msg::Image>();

    msg->header.stamp = m_node->now();
    msg->header.frame_id = m_frame_id;
    msg->width = static_cast<uint32_t>(rgbFrame.width());
    msg->height = static_cast<uint32_t>(rgbFrame.height());
    msg->encoding = "rgb8";
    msg->is_bigendian = false;
    msg->step = static_cast<uint32_t>(rgbFrame.bytesPerLine());

    // Copy pixel data into the message buffer.
    size_t size = msg->step * msg->height;
    msg->data.assign(rgbFrame.constBits(), rgbFrame.constBits() + size);

    m_pub->publish(std::move(msg));
}

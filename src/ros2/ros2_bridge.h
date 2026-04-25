#pragma once

#include <QObject>
#include <QImage>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

/**
 * @class Ros2Bridge
 * @brief Bridges the Qt application with ROS 2.
 *
 * Listens to the QImage frames emitted by the StreamDecoder and publishes
 * them as sensor_msgs/msg/Image on a ROS 2 topic.
 */
class Ros2Bridge : public QObject {
    Q_OBJECT

public:
    explicit Ros2Bridge(std::shared_ptr<rclcpp::Node> node, QObject *parent = nullptr);
    ~Ros2Bridge() override = default;

public slots:
    /**
     * @brief Slot: receives a newly decoded frame and publishes it to ROS 2.
     * @param frame An RGB888 QImage produced by StreamDecoder.
     */
    void onFrameReady(const QImage &frame);

private:
    std::shared_ptr<rclcpp::Node> m_node;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr m_pub;
    std::string m_frame_id{"hm30_camera"};
};

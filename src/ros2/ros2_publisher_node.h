#pragma once

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <memory>
#include <string>

#include "headless_decoder.h"

/**
 * @class Ros2PublisherNode
 * @brief ROS2 node that decodes an H.264 RTP stream and publishes frames
 *        as sensor_msgs/msg/Image on a configurable topic.
 *
 * ### Topic
 * Default: `/hm30/image_raw`
 * Encoding: `rgb8`  (matches the RGB24 output of HeadlessDecoder)
 *
 * ### Parameters
 * - `port`  (int,    default 5600)              UDP port to listen on.
 * - `topic` (string, default /hm30/image_raw)   Published topic name.
 * - `sdp`   (string, default stream.sdp)        Path to the SDP template.
 * - `qos`   (int,    default 10)                Publisher queue depth.
 */
class Ros2PublisherNode : public rclcpp::Node {
public:
    explicit Ros2PublisherNode(const rclcpp::NodeOptions& options =
                                   rclcpp::NodeOptions());

    ~Ros2PublisherNode() override;

private:
    /** @brief Called by HeadlessDecoder from its worker thread each decoded frame. */
    void onFrame(int width, int height, const uint8_t* rgb, size_t size);

    /** @brief Called by HeadlessDecoder when the stream connects / disconnects. */
    void onConnection(bool connected);

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------
    std::unique_ptr<HeadlessDecoder>                          m_decoder;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr     m_pub;
    std::string                                               m_frame_id{"hm30_camera"};
    uint32_t                                                  m_seq{0};
};

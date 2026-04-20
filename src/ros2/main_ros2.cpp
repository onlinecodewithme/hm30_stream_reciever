#include <rclcpp/rclcpp.hpp>
#include <csignal>
#include <iostream>

#include "ros2_publisher_node.h"

// ---------------------------------------------------------------------------
// Graceful Ctrl-C handling
// ---------------------------------------------------------------------------
static std::atomic<bool> g_shutdown{false};

static void sigintHandler(int /*sig*/) {
    g_shutdown.store(true, std::memory_order_release);
    rclcpp::shutdown();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    // Install SIGINT handler before rclcpp::init so we can cleanly shut down.
    std::signal(SIGINT, sigintHandler);

    rclcpp::init(argc, argv);

    std::cout << "=== SIYI HM30 RTP → ROS2 Publisher ===\n";
    std::cout << "Publish topic : configured via --ros-args -p topic:=<name>\n";
    std::cout << "UDP port      : configured via --ros-args -p port:=<N>\n";
    std::cout << "SDP template  : configured via --ros-args -p sdp:=<path>\n";
    std::cout << "Press Ctrl+C to stop.\n\n";

    try {
        auto node = std::make_shared<Ros2PublisherNode>();
        rclcpp::spin(node);
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        rclcpp::shutdown();
        return 1;
    }

    rclcpp::shutdown();
    std::cout << "Shutdown complete.\n";
    return 0;
}

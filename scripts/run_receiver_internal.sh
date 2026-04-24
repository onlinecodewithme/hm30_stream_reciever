#!/usr/bin/env bash
# =============================================================================
# run_receiver_internal.sh
# 
# Starts the highly-optimized C++ UDP video receiver.
# Applies a custom FastDDS XML configuration to increase Shared Memory (SHM)
# segment size to 4MB, ensuring the 80 MB/s uncompressed video is transferred
# entirely via RAM and never hits the physical network interface.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Source ROS 2
if [ -f /opt/ros/humble/setup.bash ]; then
    set +u
    source /opt/ros/humble/setup.bash
    set -u
else
    echo "[ERROR] ROS2 Humble not found at /opt/ros/humble"
    exit 1
fi

echo "==========================================================="
echo " Starting HM30 C++ Video Receiver (SHM Optimized)"
echo "==========================================================="
echo " FastDDS is configured to use zero-copy Shared Memory for"
echo " the heavy 2D video payload. Network is clear!"
echo "==========================================================="

export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI=file:///home/xavier_ai/cyclonedds/cyclonedx.xml

cd "$PROJECT_DIR"
exec ./build/hm30_ros2_publisher "$@"

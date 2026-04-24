#!/usr/bin/env bash
# =============================================================================
# run_rviz_shm.sh
# 
# Starts RViz2 with the custom FastDDS SHM configuration applied.
# This ensures RViz2 negotiates Shared Memory for the massive PointCloud
# and video payloads, preventing them from falling back to UDP and flooding
# the physical network via multicast/unicast.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Source ROS 2
if [ -f /opt/ros/humble/setup.bash ]; then
    set +u
    source /opt/ros/humble/setup.bash
    set -u
fi

echo "==========================================================="
echo " Starting RViz2 (SHM Optimized)"
echo "==========================================================="
echo " RViz2 is now configured to accept massive 32MB payloads"
echo " via zero-copy Shared Memory. Network is clear!"
echo "==========================================================="

export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI=file:///home/xavier_ai/cyclonedds/cyclonedx.xml

exec rviz2 -d "$PROJECT_DIR/config/hm30_slam3r.rviz"

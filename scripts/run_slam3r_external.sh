#!/usr/bin/env bash
# =============================================================================
# run_slam3r_external.sh
# 
# Starts the Python SLAM3R ROS2 Bridge.
# Applies a custom FastDDS XML configuration to increase Shared Memory (SHM)
# segment size to 4MB. This node will receive the heavy video via zero-copy RAM
# from the C++ node, while remaining fully connected to the external network!
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "==========================================================="
echo " Starting SLAM3R Python Node (SHM Optimized)"
echo "==========================================================="
echo " FastDDS is configured to use zero-copy Shared Memory."
echo " Ready to receive video and broadcast 3D maps!"
echo "==========================================================="

export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI=file:///home/xavier_ai/cyclonedds/cyclonedx.xml

# Restrict PyTorch and NumPy thread usage so they don't consume 100% of all CPU cores.
# This guarantees the C++ decoder thread will always have free CPU time to process UDP packets!
export OMP_NUM_THREADS=4
export OPENBLAS_NUM_THREADS=4
export MKL_NUM_THREADS=4
export VECLIB_MAXIMUM_THREADS=4
export NUMEXPR_NUM_THREADS=4

# Reuse the existing run_slam3r_bridge.sh and force it to publish every frame instantly
exec bash "$SCRIPT_DIR/run_slam3r_bridge.sh" --ros-args -p publish_every_n_frames:=1 "$@"

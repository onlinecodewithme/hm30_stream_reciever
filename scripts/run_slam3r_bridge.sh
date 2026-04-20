#!/usr/bin/env bash
# =============================================================================
# run_slam3r_bridge.sh
# Activates the SLAM3R conda environment and launches the ROS2 bridge node.
#
# Usage:
#   bash scripts/run_slam3r_bridge.sh [--topic /my/topic] [--skip 2]
#
# The bridge subscribes to /hm30/image_raw and publishes PointCloud2
# on /hm30/pointcloud.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
NODE_SCRIPT="$PROJECT_DIR/slam3r_ros2/slam3r_bridge_node.py"
ENV_FILE="$PROJECT_DIR/slam3r_ros2/.slam3r_env"

# ── Load environment ──────────────────────────────────────────────────────────
if [ -f "$ENV_FILE" ]; then
    # shellcheck disable=SC1090
    source "$ENV_FILE"
else
    echo "[WARN] .slam3r_env not found — run scripts/install_slam3r.sh first."
    echo "       Falling back to SLAM3R_PATH from environment (may be unset)."
fi

# ── Source ROS2 ───────────────────────────────────────────────────────────────
if [ -f /opt/ros/humble/setup.bash ]; then
    source /opt/ros/humble/setup.bash
else
    echo "[ERROR] ROS2 Humble not found at /opt/ros/humble"
    exit 1
fi

# ── Activate conda env ────────────────────────────────────────────────────────
CONDA_ENV_NAME="${CONDA_ENV_NAME:-slam3r}"
# shellcheck disable=SC1091
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate "$CONDA_ENV_NAME"

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " HM30 → SLAM3R → ROS2 PointCloud Bridge"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " SLAM3R path   : ${SLAM3R_PATH:-<not set>}"
echo " Conda env     : $CONDA_ENV_NAME"
echo " Python        : $(python3 --version)"
echo " CUDA available: $(python3 -c 'import torch; print(torch.cuda.is_available())')"
echo " Subscribing   : /hm30/image_raw"
echo " Publishing    : /hm30/pointcloud"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# ── Launch the node ───────────────────────────────────────────────────────────
# All extra args are forwarded as ROS2 parameters via --ros-args.
# Example:
#   bash scripts/run_slam3r_bridge.sh --ros-args -p frame_skip:=1 -p initial_winsize:=7
python3 "$NODE_SCRIPT" "$@"

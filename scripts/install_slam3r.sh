#!/usr/bin/env bash
# =============================================================================
# install_slam3r.sh
# Auto-installs SLAM3R and its conda environment for use with the
# hm30_ros2_publisher → SLAM3R bridge pipeline.
#
# Usage:
#   bash scripts/install_slam3r.sh
#
# What it does:
#   1. Clones SLAM3R next to this project (sibling directory)
#   2. Creates a conda environment "slam3r" with Python 3.11
#   3. Installs PyTorch 2.5.0 + CUDA 12.1 wheels
#   4. Installs all SLAM3R requirements (+ optional visualization deps)
#   5. Optionally compiles the RoPE CUDA kernel for speed
#   6. Pre-downloads SLAM3R model weights from HuggingFace
#   7. Installs the ROS2 Python packages into the conda env (read-only bridge)
# =============================================================================

set -euo pipefail

# ── Configurable paths ────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SLAM3R_DIR="$(dirname "$PROJECT_DIR")/SLAM3R"   # sibling to project root
CONDA_ENV="slam3r"
CUDA_VERSION="cu121"     # Compatible with CUDA 12.2 driver on this machine
TORCH_VERSION="2.5.0"
TORCHVISION_VERSION="0.20.0"
TORCHAUDIO_VERSION="2.5.0"

# ── Colour helpers ────────────────────────────────────────────────────────────
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
step()  { echo -e "\n${GREEN}━━━ $* ━━━${NC}"; }

# ── Sanity checks ─────────────────────────────────────────────────────────────
step "Pre-flight checks"

command -v conda >/dev/null 2>&1 || error "conda not found. Install Miniconda first."
command -v git   >/dev/null 2>&1 || error "git not found."
command -v nvcc  >/dev/null 2>&1 || warn "nvcc not found — RoPE CUDA kernel will be skipped."

DRIVER_VER=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -1 || echo "unknown")
CUDA_VER=$(nvidia-smi | grep "CUDA Version" | awk '{print $NF}' || echo "unknown")
info "NVIDIA Driver: $DRIVER_VER  |  CUDA: $CUDA_VER"
info "PyTorch will be installed with: $CUDA_VERSION wheels"
info "SLAM3R will be cloned to: $SLAM3R_DIR"
info "Conda environment name:    $CONDA_ENV"
echo ""
read -rp "Continue? [y/N] " confirm
[[ "$confirm" =~ ^[Yy]$ ]] || { info "Aborted."; exit 0; }

# ── Step 1: Clone SLAM3R ──────────────────────────────────────────────────────
step "Cloning SLAM3R"
if [ -d "$SLAM3R_DIR/.git" ]; then
    warn "SLAM3R already cloned at $SLAM3R_DIR — pulling latest."
    git -C "$SLAM3R_DIR" pull --ff-only
else
    git clone https://github.com/PKU-VCL-3DV/SLAM3R.git "$SLAM3R_DIR"
fi
info "SLAM3R source: $SLAM3R_DIR"

# ── Step 2: Create conda environment ─────────────────────────────────────────
step "Creating conda environment '$CONDA_ENV'"
# shellcheck disable=SC1091
source "$(conda info --base)/etc/profile.d/conda.sh"

if conda env list | grep -q "^${CONDA_ENV} "; then
    warn "Conda env '$CONDA_ENV' already exists — updating it."
else
    conda create -y -n "$CONDA_ENV" python=3.11 cmake=3.14.0
fi
conda activate "$CONDA_ENV"
info "Active env: $(conda info --envs | grep '*' | awk '{print $1}')"

# ── Step 3: Install PyTorch ───────────────────────────────────────────────────
step "Installing PyTorch $TORCH_VERSION ($CUDA_VERSION)"
pip install --quiet \
    "torch==${TORCH_VERSION}" \
    "torchvision==${TORCHVISION_VERSION}" \
    "torchaudio==${TORCHAUDIO_VERSION}" \
    --index-url "https://download.pytorch.org/whl/${CUDA_VERSION}"

# Verify CUDA is accessible
python3 -c "
import torch
assert torch.cuda.is_available(), 'CUDA not available!'
print(f'  PyTorch {torch.__version__}, CUDA {torch.version.cuda}, '
      f'GPU: {torch.cuda.get_device_name(0)}')" \
    || error "PyTorch CUDA check failed. Check driver/CUDA compatibility."

# ── Step 4: Install SLAM3R requirements ───────────────────────────────────────
step "Installing SLAM3R requirements"
pip install --quiet -r "$SLAM3R_DIR/requirements.txt"

# Optional visualization deps (Open3D, viser, etc.)
info "Installing optional visualization packages…"
pip install --quiet -r "$SLAM3R_DIR/requirements_optional.txt" || \
    warn "Some optional packages failed — visualization may be limited."

# ── Step 5: Install SLAM3R as a package ───────────────────────────────────────
step "Installing SLAM3R as editable package"
cd "$SLAM3R_DIR"
pip install --quiet -e . 2>/dev/null || {
    # If no setup.py, add the directory to PYTHONPATH instead
    warn "No setup.py found — will use SLAM3R_PATH env variable instead."
}
cd "$PROJECT_DIR"

# ── Step 6: Compile RoPE CUDA kernel (optional, speeds up inference ~30%) ─────
step "Compiling RoPE CUDA kernel (optional)"
ROPE_DIR="$SLAM3R_DIR/slam3r/pos_embed/curope"
if [ -d "$ROPE_DIR" ] && command -v nvcc >/dev/null 2>&1; then
    info "Compiling custom CUDA RoPE kernel…"
    cd "$ROPE_DIR"
    python3 setup.py build_ext --inplace 2>&1 | tail -5 || \
        warn "RoPE kernel compilation failed — inference will still work, just slower."
    cd "$PROJECT_DIR"
else
    warn "Skipping RoPE kernel (no nvcc or kernel dir not found)."
fi

# ── Step 7: Pre-download model weights ────────────────────────────────────────
step "Pre-downloading SLAM3R model weights from HuggingFace"
info "This downloads ~2 GB of model weights. May take a few minutes…"
python3 - <<'EOF'
import sys
try:
    from slam3r.models import Image2PointsModel, Local2WorldModel
    print("  Downloading Image2PointsModel (I2P)…")
    Image2PointsModel.from_pretrained('siyan824/slam3r_i2p')
    print("  Downloading Local2WorldModel (L2W)…")
    Local2WorldModel.from_pretrained('siyan824/slam3r_l2w')
    print("  ✓ Weights downloaded and cached.")
except Exception as e:
    print(f"  WARNING: Pre-download failed: {e}", file=sys.stderr)
    print("  Weights will be downloaded on first run.", file=sys.stderr)
EOF

# ── Step 8: Install ROS2 Python packages into the conda env ───────────────────
step "Linking ROS2 Python packages into conda env"
ROS_PYTHON_PATH="/opt/ros/humble/lib/python3.10/dist-packages"
CONDA_SITE=$(python3 -c "import site; print(site.getsitepackages()[0])")
ROS_PTH="$CONDA_SITE/ros2_humble.pth"

if [ -d "$ROS_PYTHON_PATH" ]; then
    echo "$ROS_PYTHON_PATH" > "$ROS_PTH"
    info "ROS2 Python packages linked via: $ROS_PTH"
else
    warn "ROS2 Python path not found at $ROS_PYTHON_PATH — update ROS_PYTHON_PATH in this script."
fi

# Install cv_bridge numpy/OpenCV compatibility layer
pip install --quiet opencv-python-headless 2>/dev/null || true

# ── Step 9: Write environment file ────────────────────────────────────────────
step "Writing environment config"
ENV_FILE="$PROJECT_DIR/slam3r_ros2/.slam3r_env"
cat > "$ENV_FILE" <<EOF
# Auto-generated by install_slam3r.sh — source this file before running the bridge node.
export SLAM3R_PATH="$SLAM3R_DIR"
export CONDA_ENV_NAME="$CONDA_ENV"
EOF
info "Environment file written: $ENV_FILE"

# ── Done ──────────────────────────────────────────────────────────────────────
step "Installation complete"
echo ""
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN} SLAM3R bridge is ready!${NC}"
echo ""
echo " To run the full pipeline:"
echo ""
echo "   Terminal 1 — stream publisher:"
echo "     source /opt/ros/humble/setup.bash"
echo "     ./build/hm30_ros2_publisher"
echo ""
echo "   Terminal 2 — 3D reconstruction:"
echo "     bash scripts/run_slam3r_bridge.sh"
echo ""
echo "   Terminal 3 — verify:"
echo "     source /opt/ros/humble/setup.bash"
echo "     ros2 topic hz /hm30/pointcloud"
echo "     rviz2   # add PointCloud2 display on /hm30/pointcloud"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

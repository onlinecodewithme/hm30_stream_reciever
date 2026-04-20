#!/usr/bin/env python3
"""
slam3r_bridge_node.py
─────────────────────
ROS2 node that subscribes to /hm30/image_raw, feeds decoded frames into
SLAM3R's online reconstruction pipeline, and publishes the accumulated
point cloud as sensor_msgs/PointCloud2 on /hm30/pointcloud.

Architecture:
  /hm30/image_raw (sensor_msgs/Image)
        │
        ▼
  Frame queue  ──►  SLAM3R worker thread  ──►  /hm30/pointcloud (PointCloud2)

Usage:
  # Activate the slam3r conda environment first, then:
  source /opt/ros/humble/setup.bash
  python3 slam3r_bridge_node.py

  # Override defaults via ROS2 parameters:
  python3 slam3r_bridge_node.py --ros-args \\
      -p input_topic:=/hm30/image_raw \\
      -p output_topic:=/hm30/pointcloud \\
      -p frame_skip:=2 \\
      -p publish_every_n_frames:=5 \\
      -p conf_threshold:=1.5 \\
      -p initial_winsize:=5
"""

import sys
import os
import threading
import queue
import argparse
import time
import struct

import numpy as np
import cv2

# ── ROS2 ──────────────────────────────────────────────────────────────────────
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image, PointCloud2, PointField
from std_msgs.msg import Header
from builtin_interfaces.msg import Time as RosTime

# ── SLAM3R ────────────────────────────────────────────────────────────────────
# SLAM3R_PATH must be set before import (done by the runner script).
_slam3r_path = os.environ.get("SLAM3R_PATH", "")
if _slam3r_path and _slam3r_path not in sys.path:
    sys.path.insert(0, _slam3r_path)

try:
    import torch
    from slam3r.models import Image2PointsModel, Local2WorldModel, inf
    from slam3r.utils.device import to_numpy
    from slam3r.pipeline.recon_online_pipeline import (
        get_raw_input_frame,
        process_input_frame,
        initialize_scene,
        initial_scene_for_accumulated_frames,
        recover_points_in_initial_window,
        register_initial_window_frames,
        select_ids_as_reference,
        pointmap_local_recon,
        pointmap_global_register,
        update_buffer_set,
    )
    _SLAM3R_AVAILABLE = True
except ImportError as e:
    print(f"[WARN] SLAM3R not importable: {e}")
    print("[WARN] Running in DRY-RUN mode — frames consumed but no reconstruction.")
    _SLAM3R_AVAILABLE = False


# ══════════════════════════════════════════════════════════════════════════════
# Helpers
# ══════════════════════════════════════════════════════════════════════════════

def ros_image_to_bgr(msg: Image) -> np.ndarray:
    """Convert a sensor_msgs/Image (rgb8) to an OpenCV BGR numpy array."""
    data = np.frombuffer(msg.data, dtype=np.uint8)
    if msg.encoding in ("rgb8", "RGB8"):
        img = data.reshape((msg.height, msg.width, 3))
        return cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
    elif msg.encoding in ("bgr8", "BGR8"):
        return data.reshape((msg.height, msg.width, 3)).copy()
    elif msg.encoding in ("mono8",):
        gray = data.reshape((msg.height, msg.width))
        return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    else:
        raise ValueError(f"Unsupported encoding: {msg.encoding}")


def make_pointcloud2(points_xyz: np.ndarray,
                     colors_rgb: np.ndarray,
                     frame_id: str,
                     stamp) -> PointCloud2:
    """
    Build a sensor_msgs/PointCloud2 (XYZRGB) from numpy arrays.

    Args:
        points_xyz : (N, 3) float32
        colors_rgb : (N, 3) uint8  in [0, 255]
        frame_id   : TF frame string
        stamp      : rclpy time (node.get_clock().now().to_msg())
    """
    assert len(points_xyz) == len(colors_rgb)
    N = len(points_xyz)

    # Pack RGB into a float32 (standard ROS XYZRGB convention)
    r = colors_rgb[:, 0].astype(np.uint32)
    g = colors_rgb[:, 1].astype(np.uint32)
    b = colors_rgb[:, 2].astype(np.uint32)
    rgb_packed = ((r << 16) | (g << 8) | b).astype(np.uint32)
    rgb_float = rgb_packed.view(np.float32)

    # Interleave [x, y, z, rgb] → each point = 16 bytes
    data = np.zeros(N, dtype=[
        ('x', np.float32),
        ('y', np.float32),
        ('z', np.float32),
        ('rgb', np.float32),
    ])
    data['x']   = points_xyz[:, 0].astype(np.float32)
    data['y']   = points_xyz[:, 1].astype(np.float32)
    data['z']   = points_xyz[:, 2].astype(np.float32)
    data['rgb'] = rgb_float

    fields = [
        PointField(name='x',   offset=0,  datatype=PointField.FLOAT32, count=1),
        PointField(name='y',   offset=4,  datatype=PointField.FLOAT32, count=1),
        PointField(name='z',   offset=8,  datatype=PointField.FLOAT32, count=1),
        PointField(name='rgb', offset=12, datatype=PointField.FLOAT32, count=1),
    ]

    msg = PointCloud2()
    msg.header.frame_id = frame_id
    msg.header.stamp    = stamp
    msg.height          = 1
    msg.width           = N
    msg.fields          = fields
    msg.is_bigendian    = False
    msg.point_step      = 16
    msg.row_step        = 16 * N
    msg.data            = data.tobytes()
    msg.is_dense        = False
    return msg


# ══════════════════════════════════════════════════════════════════════════════
# Fake args namespace (mirrors recon.py argparse defaults)
# ══════════════════════════════════════════════════════════════════════════════

def _make_args(device, conf_thres_i2p, conf_thres_l2w,
               keyframe_stride, initial_winsize, win_r,
               num_scene_frame, buffer_size, retrieve_freq):
    args = argparse.Namespace()
    args.device           = device
    args.conf_thres_i2p   = conf_thres_i2p
    args.conf_thres_l2w   = conf_thres_l2w
    args.keyframe_stride  = keyframe_stride
    args.initial_winsize  = initial_winsize
    args.win_r            = win_r
    args.num_scene_frame  = num_scene_frame
    args.max_num_register = 10
    args.num_points_save  = 2_000_000
    args.norm_input       = False
    args.save_frequency   = 3
    args.save_each_frame  = False
    args.retrieve_freq    = retrieve_freq
    args.update_buffer_intv = 1
    args.buffer_size      = buffer_size
    args.buffer_strategy  = 'reservoir'
    args.perframe         = 1
    args.test_name        = "ros2_live"
    args.save_preds       = False
    args.save_for_eval    = False
    args.save_online      = False
    return args


# ══════════════════════════════════════════════════════════════════════════════
# SLAM3R worker
# ══════════════════════════════════════════════════════════════════════════════

class SLAM3RWorker:
    """
    Runs SLAM3R online reconstruction in a background thread.
    Accepts BGR frames via `push_frame()`.
    Accumulated global point cloud is retrieved via `get_pointcloud()`.
    """

    def __init__(self, device: str, args: argparse.Namespace,
                 on_pointcloud_ready, logger):
        self._device   = device
        self._args     = args
        self._callback = on_pointcloud_ready
        self._log      = logger

        self._frame_queue: queue.Queue = queue.Queue(maxsize=30)
        self._stop_event  = threading.Event()
        self._thread      = threading.Thread(target=self._run, daemon=True)

        # Shared global point cloud (protected by lock)
        self._pcd_lock  = threading.Lock()
        self._pts_xyz   = np.empty((0, 3), dtype=np.float32)
        self._pts_rgb   = np.empty((0, 3), dtype=np.uint8)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop_event.set()
        # Unblock the worker if it's waiting for a frame
        try:
            self._frame_queue.put_nowait(None)
        except queue.Full:
            pass
        self._thread.join(timeout=10)

    def push_frame(self, bgr: np.ndarray):
        """Non-blocking push. Drops oldest frame if queue is full."""
        try:
            self._frame_queue.put_nowait(bgr)
        except queue.Full:
            try:
                self._frame_queue.get_nowait()  # drop oldest
                self._frame_queue.put_nowait(bgr)
            except queue.Empty:
                pass

    def get_pointcloud(self):
        """Thread-safe snapshot of the current global point cloud."""
        with self._pcd_lock:
            return self._pts_xyz.copy(), self._pts_rgb.copy()

    # ── Internal reconstruction loop ─────────────────────────────────────────

    def _run(self):
        self._log.info("[SLAM3R] Loading models from HuggingFace (first run downloads weights)…")
        i2p_model = Image2PointsModel.from_pretrained('siyan824/slam3r_i2p').to(self._device).eval()
        l2w_model = Local2WorldModel.from_pretrained('siyan824/slam3r_l2w').to(self._device).eval()
        self._log.info("[SLAM3R] Models loaded. Waiting for frames…")

        args       = self._args
        kf_stride  = args.keyframe_stride
        init_winsize = args.initial_winsize

        # State mirrors scene_recon_pipeline_online
        data_views            = []
        rgb_imgs              = []
        input_views           = []
        per_frame_res         = dict(i2p_pcds=[], i2p_confs=[], l2w_pcds=[], l2w_confs=[])
        registered_confs_mean = []
        local_confs_mean      = []
        last_ref_ids_buffer   = []
        fail_view             = {}
        buffering_set_ids     = []
        milestone             = 0
        candi_frame_id        = 0
        init_ref_id           = 0
        init_num              = 0
        initialized           = False
        num_frame_read        = 0

        class _FakeFrameReader:
            """Adapter so we can reuse get_raw_input_frame with a BGR numpy array."""
            type = "video"

        fake_reader = _FakeFrameReader()

        while not self._stop_event.is_set():
            try:
                bgr = self._frame_queue.get(timeout=0.5)
            except queue.Empty:
                continue
            if bgr is None:
                break   # stop signal

            current_frame_id = num_frame_read
            num_frame_read  += 1

            # get_raw_input_frame expects a raw OpenCV frame when type=="video"
            frame, data_views, rgb_imgs = get_raw_input_frame(
                fake_reader.type, data_views, rgb_imgs,
                current_frame_id, bgr, self._device
            )
            input_view, per_frame_res, registered_confs_mean = process_input_frame(
                per_frame_res, registered_confs_mean,
                data_views, current_frame_id, i2p_model
            )
            input_views.append(input_view)

            # ── Wait for enough frames to initialize ──────────────────────────
            if current_frame_id < (init_winsize - 1) * kf_stride:
                self._log.info(f"[SLAM3R] Buffering frame {current_frame_id+1}"
                               f" / {(init_winsize-1)*kf_stride+1} for init…")
                continue

            # ── Scene initialization ──────────────────────────────────────────
            if not initialized and current_frame_id == (init_winsize - 1) * kf_stride:
                self._log.info("[SLAM3R] Initializing scene…")
                out = initial_scene_for_accumulated_frames(
                    input_views, init_winsize, kf_stride, i2p_model,
                    per_frame_res, registered_confs_mean,
                    args.buffer_size, args.conf_thres_i2p
                )
                buffering_set_ids     = out[0]
                init_ref_id           = out[1]
                init_num              = out[2]
                input_views           = out[3]
                per_frame_res         = out[4]
                registered_confs_mean = out[5]

                local_confs_mean, per_frame_res, input_views = recover_points_in_initial_window(
                    current_frame_id, buffering_set_ids, kf_stride,
                    init_ref_id, per_frame_res, input_views, i2p_model,
                    args.conf_thres_i2p
                )
                if kf_stride > 1:
                    _, input_views, per_frame_res = register_initial_window_frames(
                        init_num, kf_stride, buffering_set_ids, input_views,
                        l2w_model, per_frame_res, registered_confs_mean,
                        self._device, args.norm_input
                    )
                milestone      = init_num * kf_stride + 1
                candi_frame_id = len(buffering_set_ids)
                initialized    = True
                self._log.info("[SLAM3R] Scene initialized — starting incremental reconstruction.")
                self._publish_current_cloud(input_views, rgb_imgs, per_frame_res,
                                            args.conf_thres_i2p, current_frame_id)
                continue

            # ── Incremental reconstruction ────────────────────────────────────
            if not initialized:
                continue

            ref_ids, ref_ids_buffer = select_ids_as_reference(
                buffering_set_ids, current_frame_id, input_views,
                i2p_model, args.num_scene_frame, args.win_r,
                kf_stride, args.retrieve_freq, last_ref_ids_buffer
            )
            last_ref_ids_buffer = ref_ids_buffer

            local_views = [input_views[current_frame_id]] + [input_views[i] for i in ref_ids]
            local_confs_mean, per_frame_res, input_views = pointmap_local_recon(
                local_views, i2p_model, current_frame_id, 0,
                per_frame_res, input_views, args.conf_thres_i2p, local_confs_mean
            )

            ref_views = [input_views[i] for i in ref_ids]
            input_views, per_frame_res, registered_confs_mean = pointmap_global_register(
                ref_views, input_views, l2w_model, per_frame_res,
                registered_confs_mean, current_frame_id,
                device=self._device, norm_input=args.norm_input
            )

            next_frame_id = current_frame_id + 1
            update_intv   = kf_stride * args.update_buffer_intv
            if next_frame_id - milestone >= update_intv:
                milestone, candi_frame_id, buffering_set_ids = update_buffer_set(
                    next_frame_id, args.buffer_size, kf_stride,
                    buffering_set_ids, args.buffer_strategy,
                    registered_confs_mean, local_confs_mean,
                    candi_frame_id, milestone
                )

            conf = registered_confs_mean[current_frame_id]
            if conf < 10:
                fail_view[current_frame_id] = conf.item() if hasattr(conf, 'item') else float(conf)

            self._log.debug(f"[SLAM3R] Frame {current_frame_id} done, conf={float(conf):.2f}")
            self._publish_current_cloud(input_views, rgb_imgs, per_frame_res,
                                        args.conf_thres_i2p, current_frame_id)

        self._log.info("[SLAM3R] Worker thread exiting.")

    def _publish_current_cloud(self, input_views, rgb_imgs, per_frame_res,
                                conf_thres, up_to_frame):
        """Collect all registered point clouds and pass them to the callback."""
        try:
            pcds  = []
            rgbs  = []
            for i, view in enumerate(input_views):
                if 'pts3d_world' not in view:
                    continue
                pcd = to_numpy(view['pts3d_world'][0])          # (224, 224, 3)
                if pcd.shape[0] == 3:
                    pcd = pcd.transpose(1, 2, 0)
                pcd = pcd.reshape(-1, 3).astype(np.float32)

                # Corresponding RGB image
                if i < len(rgb_imgs):
                    rgb = rgb_imgs[i].reshape(-1, 3)
                    # rgb_imgs stores BGR — convert to RGB for the message
                    rgb = rgb[:, ::-1].copy()
                else:
                    rgb = np.ones((pcd.shape[0], 3), dtype=np.uint8) * 128

                # Confidence filter
                if per_frame_res['l2w_confs'][i] is not None:
                    conf_map = per_frame_res['l2w_confs'][i]
                    if hasattr(conf_map, 'cpu'):
                        conf_map = conf_map.cpu().numpy()
                    mask = conf_map.reshape(-1) > conf_thres
                    pcd  = pcd[mask]
                    rgb  = rgb[mask]

                if len(pcd):
                    pcds.append(pcd)
                    rgbs.append(rgb)

            if not pcds:
                return

            all_pts = np.concatenate(pcds, axis=0)
            all_rgb = np.concatenate(rgbs, axis=0)

            # Down-sample if the cloud is massive (keep ≤ 500k points for ROS2)
            MAX_PTS = 500_000
            if len(all_pts) > MAX_PTS:
                idx     = np.random.choice(len(all_pts), MAX_PTS, replace=False)
                all_pts = all_pts[idx]
                all_rgb = all_rgb[idx]

            with self._pcd_lock:
                self._pts_xyz = all_pts
                self._pts_rgb = all_rgb.astype(np.uint8)

            self._callback(all_pts, all_rgb.astype(np.uint8))
        except Exception as exc:
            self._log.warn(f"[SLAM3R] Point cloud assembly error: {exc}")


# ══════════════════════════════════════════════════════════════════════════════
# ROS2 Node
# ══════════════════════════════════════════════════════════════════════════════

class SLAM3RBridgeNode(Node):

    def __init__(self):
        super().__init__('slam3r_bridge')

        # ── Parameters ───────────────────────────────────────────────────────
        self.declare_parameter('input_topic',         '/hm30/image_raw')
        self.declare_parameter('output_topic',        '/hm30/pointcloud')
        self.declare_parameter('frame_id',            'hm30_camera')
        self.declare_parameter('device',              'cuda')
        self.declare_parameter('frame_skip',          2)        # process every Nth frame
        self.declare_parameter('publish_every_n_frames', 5)    # publish cloud every N processed frames
        self.declare_parameter('conf_threshold',      1.5)
        self.declare_parameter('initial_winsize',     5)
        self.declare_parameter('keyframe_stride',     3)
        self.declare_parameter('win_r',               3)
        self.declare_parameter('num_scene_frame',     10)
        self.declare_parameter('buffer_size',         100)
        self.declare_parameter('retrieve_freq',       1)

        in_topic    = self.get_parameter('input_topic').value
        out_topic   = self.get_parameter('output_topic').value
        self._frame_id     = self.get_parameter('frame_id').value
        device             = self.get_parameter('device').value
        self._frame_skip   = max(1, self.get_parameter('frame_skip').value)
        self._pub_every    = max(1, self.get_parameter('publish_every_n_frames').value)
        conf_thres         = self.get_parameter('conf_threshold').value
        init_winsize       = self.get_parameter('initial_winsize').value
        kf_stride          = self.get_parameter('keyframe_stride').value
        win_r              = self.get_parameter('win_r').value
        num_scene_frame    = self.get_parameter('num_scene_frame').value
        buffer_size        = self.get_parameter('buffer_size').value
        retrieve_freq      = self.get_parameter('retrieve_freq').value

        self.get_logger().info(
            f"SLAM3R Bridge: {in_topic} → {out_topic} | "
            f"device={device} skip={self._frame_skip}"
        )

        # ── Publisher / Subscriber ────────────────────────────────────────────
        self._pub = self.create_publisher(PointCloud2, out_topic, 10)
        self._sub = self.create_subscription(Image, in_topic,
                                             self._on_image, 10)

        # ── Frame counter ─────────────────────────────────────────────────────
        self._recv_count    = 0
        self._proc_count    = 0

        # ── SLAM3R worker ─────────────────────────────────────────────────────
        if _SLAM3R_AVAILABLE:
            args = _make_args(
                device=device,
                conf_thres_i2p=conf_thres,
                conf_thres_l2w=conf_thres * 8,   # tighter for final filter
                keyframe_stride=kf_stride,
                initial_winsize=init_winsize,
                win_r=win_r,
                num_scene_frame=num_scene_frame,
                buffer_size=buffer_size,
                retrieve_freq=retrieve_freq,
            )
            self._worker = SLAM3RWorker(
                device=device,
                args=args,
                on_pointcloud_ready=self._on_cloud_ready,
                logger=self.get_logger(),
            )
            self._worker.start()
        else:
            self._worker = None
            self.get_logger().warn("SLAM3R unavailable — no point clouds will be published.")

    def destroy_node(self):
        if self._worker:
            self.get_logger().info("Stopping SLAM3R worker…")
            self._worker.stop()
        super().destroy_node()

    # ── Image callback ────────────────────────────────────────────────────────

    def _on_image(self, msg: Image):
        self._recv_count += 1
        if self._recv_count % self._frame_skip != 0:
            return
        if self._worker is None:
            return

        try:
            bgr = ros_image_to_bgr(msg)
        except Exception as exc:
            self.get_logger().warn(f"Image conversion failed: {exc}")
            return

        self._worker.push_frame(bgr)
        self._proc_count += 1
        if self._proc_count % 30 == 0:
            self.get_logger().info(
                f"[SLAM3R Bridge] Received {self._recv_count} frames, "
                f"pushed {self._proc_count} for reconstruction."
            )

    # ── Point cloud callback (called from SLAM3R worker thread) ───────────────

    def _on_cloud_ready(self, pts_xyz: np.ndarray, pts_rgb: np.ndarray):
        self._proc_count += 1
        if self._proc_count % self._pub_every != 0:
            return

        stamp = self.get_clock().now().to_msg()
        msg   = make_pointcloud2(pts_xyz, pts_rgb, self._frame_id, stamp)
        self._pub.publish(msg)
        self.get_logger().info(
            f"[SLAM3R] Published cloud with {len(pts_xyz)} points "
            f"→ {self.get_parameter('output_topic').value}"
        )


# ══════════════════════════════════════════════════════════════════════════════
# Entry point
# ══════════════════════════════════════════════════════════════════════════════

def main():
    rclpy.init()
    node = SLAM3RBridgeNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()

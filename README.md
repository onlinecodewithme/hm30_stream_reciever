# SIYI HM30 — RTP H.264 Receiver Dashboard (C++)

A low-latency video dashboard that receives raw **RTP/UDP H.264** streams — designed to work with the `stream_to_siyi.sh` GStreamer pipeline and the DJI Osmo Action 5 Pro.

## Data Flow

```
DJI Osmo Action 5 Pro → GStreamer (stream_to_siyi.sh)
  → RTP/UDP H.264 → HM30 Air (.11) → Radio → HM30 Ground (.12)
  → Ethernet → This Receiver (listens on UDP :5600)
```

## How It Works

Unlike the RTSP dashboard (which connects to a server), this receiver **listens passively** for incoming RTP packets. An SDP file tells FFmpeg the codec parameters that would normally be negotiated via RTSP.

## Prerequisites

```bash
sudo apt install -y \
    build-essential cmake \
    qtbase5-dev \
    libavformat-dev libavcodec-dev libswscale-dev libavutil-dev
```

## Build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Run

```bash
# Default: listen on UDP port 5600
./build/hm30_rtp_receiver

# Custom port
./build/hm30_rtp_receiver 5700
```

Then start your stream on the source device:
```bash
./stream_to_siyi.sh 192.168.144.11 5600       # 720p
./stream_to_siyi.sh 192.168.144.11 5600 1080p  # 1080p
```

## Project Structure

```
.
├── CMakeLists.txt         # Build config (Qt5 + FFmpeg)
├── stream.sdp             # RTP stream descriptor (codec/port info)
├── main.cpp               # Entry point (optional port argument)
├── dashboard.h/cpp        # Dark-themed UI with status panels
├── video_widget.h/cpp     # FFmpeg RTP decode + Qt display
├── build/
│   └── hm30_rtp_receiver  # Compiled binary
└── README.md              # This file
```

## Key Differences from RTSP Dashboard

| Feature | RTSP Dashboard (`hm_30_py/`) | RTP Receiver (this project) |
|---------|-----------------------------|-----------------------------|
| Transport | RTSP over UDP (client connects) | Raw RTP/UDP (passive listen) |
| Stream source | GStreamer RTSP server on Pi | GStreamer `udpsink` from camera |
| Negotiation | RTSP + SDP automatic | SDP file (manual) |
| Direction | Pull (client requests) | Push (sender fires) |
| Latency | Low | Lower (no RTSP handshake) |

#pragma once

/**
 * @file app_config.h
 * @brief Compile-time application constants and configuration defaults.
 *
 * All "magic numbers" and global string literals are centralized here so that
 * they can be changed from a single location without hunting through source.
 */

namespace AppConfig {

// ---------------------------------------------------------------------------
// Application identity
// ---------------------------------------------------------------------------

/// Human-readable application name used by QApplication and the window title.
constexpr char kAppName[]    = "hm30_rtp_receiver";

/// Semantic version string injected from CMake via version.h at build time.
/// Fallback value used when the generated header is unavailable.
constexpr char kAppVersion[] = "2.0.0";

/// Window title prefix (port number is appended at runtime).
constexpr char kWindowTitle[] = "SIYI HM30 — RTP Receiver";

// ---------------------------------------------------------------------------
// Network defaults
// ---------------------------------------------------------------------------

/// Default stream URL. Overridable via --url flag.
constexpr char kDefaultUrl[] = "rtsp://192.168.144.25:8554/stream";

// ---------------------------------------------------------------------------
// UI / timing
// ---------------------------------------------------------------------------

/// How often (ms) the status bar and info panel are refreshed.
constexpr int kStatusIntervalMs = 500;

/// Delay (ms) between reconnection attempts when the stream drops.
constexpr int kReconnectDelayMs = 100;

/// Minimum widget size for the main window.
constexpr int kMinWindowWidth  = 1000;
constexpr int kMinWindowHeight = 600;

/// Minimum size (px) for the video canvas.
constexpr int kMinVideoWidth  = 640;
constexpr int kMinVideoHeight = 360;

/// Width (px) reserved for the right-hand info panel.
constexpr int kInfoPanelMinWidth = 360;

// ---------------------------------------------------------------------------
// FFmpeg decoder tuning
// ---------------------------------------------------------------------------

/// Maximum stream analysis duration (µs) passed to avformat.
constexpr int kMaxAnalyzeDuration = 5'000'000;

/// avformat probesize limit (bytes).
constexpr int kProbeSize = 5'000'000;

/// Number of codec threads allocated for H.264 decode.
constexpr int kDecoderThreads = 2;

/// FFmpeg SWS_FAST_BILINEAR algorithm flag (value = 4).
/// Kept as a plain int to avoid pulling libswscale headers into this header.
constexpr int kSwsAlgorithm = 4; // SWS_FAST_BILINEAR

// ---------------------------------------------------------------------------
// Resource paths (Qt Resource System)
// ---------------------------------------------------------------------------

/// Embedded QSS stylesheet path inside the Qt resource bundle.
constexpr char kStylesheetResource[] = ":/style.qss";

} // namespace AppConfig

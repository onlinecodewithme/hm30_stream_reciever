#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

/**
 * @class HeadlessDecoder
 * @brief Qt-free FFmpeg H.264/RTP decode worker running on a std::thread.
 *
 * This class mirrors the logic of StreamDecoder but has zero Qt dependencies,
 * making it suitable for use inside a pure ROS2 node binary that has no
 * QApplication or event loop.
 *
 * ### Usage
 * ```cpp
 * HeadlessDecoder dec(5600, "/path/to/stream.sdp");
 * dec.setFrameCallback([](int w, int h, const uint8_t* rgb, size_t sz) {
 *     // publish to ROS2 here
 * });
 * dec.start();
 * // ... when shutting down:
 * dec.stop();
 * ```
 *
 * ### Thread Safety
 * - `fps()`, `videoWidth()`, `videoHeight()` are safe from any thread.
 * - The frame callback is invoked from the decode worker thread.
 *   Do NOT call `stop()` from inside the callback (deadlock).
 */
class HeadlessDecoder {
public:
    /**
     * @brief Frame callback type.
     * @param width   Frame width in pixels.
     * @param height  Frame height in pixels.
     * @param rgb     Pointer to packed RGB24 pixel data (width * height * 3 bytes).
     * @param size    Total byte size of the rgb buffer.
     */
    using FrameCallback = std::function<void(int width, int height,
                                             const uint8_t* rgb, size_t size)>;

    /**
     * @brief Construct the decoder. Does not start the decode thread.
     * @param port            UDP port to listen on.
     * @param sdpTemplatePath Absolute path to the SDP template file.
     */
    explicit HeadlessDecoder(int port, const std::string& sdpTemplatePath);

    /** @brief Stops the decode thread if running, then destroys the object. */
    ~HeadlessDecoder();

    // Non-copyable, non-movable.
    HeadlessDecoder(const HeadlessDecoder&)            = delete;
    HeadlessDecoder& operator=(const HeadlessDecoder&) = delete;

    /**
     * @brief Register a callback invoked once per decoded RGB frame.
     *
     * Must be called before `start()`. The callback is executed on the
     * decode worker thread — keep it fast and non-blocking.
     */
    void setFrameCallback(FrameCallback cb);

    /**
     * @brief Register a callback invoked whenever the connection state changes.
     * @param cb  Receives `true` when stream connects, `false` when it drops.
     */
    void setConnectionCallback(std::function<void(bool connected)> cb);

    /** @brief Start the decode worker thread. */
    void start();

    /**
     * @brief Gracefully stop the decode thread and block until it exits.
     * Safe to call from any thread except from inside the frame callback.
     */
    void stop();

    /** @brief Current measured FPS (thread-safe). */
    [[nodiscard]] double fps()         const { return m_fps.load(std::memory_order_relaxed); }

    /** @brief Last decoded frame width in pixels, 0 until first frame (thread-safe). */
    [[nodiscard]] int    videoWidth()  const { return m_width.load(std::memory_order_relaxed); }

    /** @brief Last decoded frame height in pixels, 0 until first frame (thread-safe). */
    [[nodiscard]] int    videoHeight() const { return m_height.load(std::memory_order_relaxed); }

private:
    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /** @brief Build the patched SDP content from the template, replacing the port. */
    [[nodiscard]] std::string buildSdpContent() const;

    /** @brief Write patched SDP to a temp file and open the FFmpeg pipeline. */
    [[nodiscard]] bool openStream();

    /** @brief Release all FFmpeg resources and reset state. */
    void closeStream();

    /** @brief Main decode loop — runs on m_thread. */
    void decodeLoop();

    // -------------------------------------------------------------------------
    // Configuration (set at construction, read-only afterwards)
    // -------------------------------------------------------------------------
    int         m_port;
    std::string m_sdpTemplatePath;
    std::string m_tempSdpPath;   ///< Path of the temporary SDP file in use.

    // -------------------------------------------------------------------------
    // Runtime state
    // -------------------------------------------------------------------------
    std::thread         m_thread;
    std::atomic<bool>   m_running{false};
    std::atomic<bool>   m_connected{false};

    // -------------------------------------------------------------------------
    // FFmpeg pipeline handles
    // -------------------------------------------------------------------------
    AVFormatContext* m_fmtCtx         = nullptr;
    AVCodecContext*  m_codecCtx       = nullptr;
    SwsContext*      m_swsCtx         = nullptr;
    int              m_videoStreamIdx = -1;

    // -------------------------------------------------------------------------
    // Live statistics (atomic for cross-thread reads)
    // -------------------------------------------------------------------------
    std::atomic<double> m_fps{0.0};
    std::atomic<int>    m_width{0};
    std::atomic<int>    m_height{0};

    // -------------------------------------------------------------------------
    // Frame conversion back-buffer
    // -------------------------------------------------------------------------
    std::vector<uint8_t> m_rgbBuf;
    std::mutex           m_bufMutex;

    // -------------------------------------------------------------------------
    // User-supplied callbacks
    // -------------------------------------------------------------------------
    FrameCallback                      m_frameCb;
    std::function<void(bool)>          m_connCb;
};

#pragma once

#include <QThread>
#include <QImage>

#include <atomic>
#include <array>
#include <mutex>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

/**
 * @class StreamDecoder
 * @brief Asynchronous H.264 / RTP decode worker running on a dedicated QThread.
 *
 * StreamDecoder owns the entire FFmpeg pipeline:
 *   URL → avformat (demux) → avcodec (H.264) → sws_scale (YUV→RGB) → QImage
 *
 * Decoded frames are emitted via the Qt signal/slot mechanism (queued connection)
 * so that VideoWidget can safely repaint from the UI thread without any explicit
 * locking on the consumer side.
 *
 * ### Lifecycle
 * 1. Construct with desired stream URL.
 * 2. Call `start()` — the thread begins and internally calls `openStream()`.
 * 3. If the stream drops, the worker reconnects automatically.
 * 4. Call `stop()` from any thread; the worker exits gracefully and this call blocks
 *    until the thread has fully terminated (`QThread::wait()`).
 *
 * ### Thread Safety
 * - `fps()`, `videoWidth()`, `videoHeight()` are safe to call from any thread
 *   (backed by `std::atomic`).
 * - Signals (`frameReady`, `connectionChanged`) are emitted from the worker thread
 *   and delivered to the UI thread via Qt's queued-connection mechanism.
 */
class StreamDecoder : public QThread {
    Q_OBJECT

public:
    /**
     * @brief Construct the decoder. Does not start the background thread.
     * @param url             Stream URL (e.g. rtsp://... or udp://...).
     * @param parent          Optional Qt parent object.
     */
    explicit StreamDecoder(const QString &url, QObject *parent = nullptr);

    /** @brief Stops the decode thread if still running, then destroys the object. */
    ~StreamDecoder() override;

    /**
     * @brief Gracefully requests the decode thread to stop and blocks until it exits.
     *
     * Safe to call from the main thread at any time, including from the
     * parent window's destructor.
     */
    void stop();

    /** @brief Current measured frame rate (frames/second). Thread-safe. */
    [[nodiscard]] double fps() const { return m_fps.load(std::memory_order_relaxed); }

    /** @brief Last decoded frame width in pixels. 0 until the first frame. Thread-safe. */
    [[nodiscard]] int videoWidth()  const { return m_width.load(std::memory_order_relaxed); }

    /** @brief Last decoded frame height in pixels. 0 until the first frame. Thread-safe. */
    [[nodiscard]] int videoHeight() const { return m_height.load(std::memory_order_relaxed); }

signals:
    /**
     * @brief Emitted once per decoded video frame.
     * @param frame A deep-copied RGB888 QImage ready for display. Ownership is
     *              transferred to the connected slot via Qt's queued connection.
     */
    void frameReady(const QImage &frame);

    /**
     * @brief Emitted whenever the stream connect/disconnect state changes.
     * @param connected @c true when a stream is active, @c false when it drops.
     */
    void connectionChanged(bool connected);

protected:
    /** @brief Main decode loop executed on the worker thread. */
    void run() override;

private:
    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------



    /**
     * @brief Opens the FFmpeg format/codec pipeline.
     * @return @c true on success, @c false if any stage fails.
     */
    [[nodiscard]] bool openStream();

    /** @brief Releases all FFmpeg resources and resets state. */
    void closeStream();

    // -------------------------------------------------------------------------
    // Configuration (set at construction, read-only afterwards)
    // -------------------------------------------------------------------------
    QString m_url;

    // -------------------------------------------------------------------------
    // Runtime state
    // -------------------------------------------------------------------------
    std::atomic<bool>       m_running{false};
    std::atomic<bool>       m_connected{false};

    // -------------------------------------------------------------------------
    // FFmpeg pipeline handles
    // -------------------------------------------------------------------------
    AVFormatContext *m_fmtCtx        = nullptr;
    AVCodecContext  *m_codecCtx      = nullptr;
    SwsContext      *m_swsCtx        = nullptr;
    int              m_videoStreamIdx = -1;

    // -------------------------------------------------------------------------
    // Live statistics (atomic for cross-thread reads)
    // -------------------------------------------------------------------------
    std::atomic<double> m_fps{0.0};
    std::atomic<int>    m_width{0};
    std::atomic<int>    m_height{0};

    // -------------------------------------------------------------------------
    // Back-buffer for YUV→RGB conversion (protected by mutex)
    // -------------------------------------------------------------------------
    QImage m_backBuf;
    std::mutex m_bufMutex;
};

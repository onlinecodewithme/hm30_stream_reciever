#pragma once

#include <QThread>
#include <QImage>
#include <QTemporaryFile>
#include <atomic>
#include <string>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

/**
 * Worker thread for decoding the RTP/UDP H.264 stream.
 * Converts decoded frames directly into QImage and emits them securely via queued signals.
 */
class StreamDecoder : public QThread {
    Q_OBJECT

public:
    explicit StreamDecoder(int port, const QString &sdpTemplatePath, QObject *parent = nullptr);
    ~StreamDecoder() override;

    void stop();

    double fps() const { return m_fps.load(); }
    int videoWidth() const { return m_width.load(); }
    int videoHeight() const { return m_height.load(); }

signals:
    void frameReady(const QImage &frame);
    void connectionChanged(bool connected);

protected:
    void run() override;

private:
    bool openStream();
    void closeStream();

    int m_port;
    QString m_sdpTemplatePath;
    QTemporaryFile m_tempSdp;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};

    // FFmpeg state
    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext  *m_codecCtx = nullptr;
    SwsContext      *m_swsCtx = nullptr;
    int              m_videoStreamIdx = -1;

    // Stats
    std::atomic<double> m_fps{0.0};
    std::atomic<int>    m_width{0};
    std::atomic<int>    m_height{0};

    // Buffer handling
    QImage m_backBuf;
    std::mutex m_bufMutex;
};

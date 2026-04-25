#include "stream_decoder.h"
#include "app_config.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QUrl>

#include <chrono>
#include <array>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

StreamDecoder::StreamDecoder(const QString &url, QObject *parent)
    : QThread(parent)
    , m_url(url)
{
}

StreamDecoder::~StreamDecoder()
{
    stop();
}

// ---------------------------------------------------------------------------
// Public control
// ---------------------------------------------------------------------------

void StreamDecoder::stop()
{
    m_running.store(false, std::memory_order_release);
    wait(); // Block until QThread::run() returns.
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool StreamDecoder::openStream()
{
    qInfo().nospace() << "[StreamDecoder] Opening stream — " << m_url;

    // -- Allocate format context ----------------------------------------------
    m_fmtCtx = avformat_alloc_context();
    if (!m_fmtCtx) {
        qCritical() << "[StreamDecoder] avformat_alloc_context() failed.";
        return false;
    }

    // -- Low-latency AVDictionary options -------------------------------------
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp,tcp,rtsp", 0);
    av_dict_set(&opts, "fflags",             "nobuffer",      0);
    av_dict_set(&opts, "flags",              "low_delay",     0);
    av_dict_set(&opts, "framedrop",          "1",             0);
    av_dict_set(&opts, "max_delay",          "0",             0);
    av_dict_set(&opts, "reorder_queue_size", "0",             0);
    av_dict_set(&opts, "rtsp_transport", "tcp", 0); // Optional, but usually more reliable
    // Request a 10MB socket buffer from the OS to prevent packet drops during CPU spikes
    av_dict_set(&opts, "buffer_size",        "10485760",      0);
    av_dict_set(&opts, "fifo_size",          "10485760",      0);

    QString inputUrl = m_url;
    if (m_url.startsWith(QLatin1String("udp://"), Qt::CaseInsensitive) || 
        m_url.startsWith(QLatin1String("rtp://"), Qt::CaseInsensitive)) {
        QUrl url(m_url);
        int port = url.port();
        if (port == -1) {
            port = 5600;
        }

        QString sdpPath = QCoreApplication::applicationDirPath() + QLatin1String("/stream.sdp");
        QFile file(sdpPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "v=0\n";
            out << "o=- 0 0 IN IP4 127.0.0.1\n";
            out << "s=No Name\n";
            out << "c=IN IP4 0.0.0.0\n";
            out << "t=0 0\n";
            out << "a=tool:libavformat 58.76.100\n";
            out << "m=video " << port << " RTP/AVP 96\n";
            out << "a=rtpmap:96 H264/90000\n";
            out << "a=fmtp:96 packetization-mode=1\n";
            file.close();
            inputUrl = sdpPath;
            qInfo() << "[StreamDecoder] Generated SDP for raw RTP on port" << port;
        } else {
            qWarning() << "[StreamDecoder] Failed to write SDP file at" << sdpPath;
        }
    }

    int ret = avformat_open_input(&m_fmtCtx,
                                  inputUrl.toUtf8().constData(),
                                  nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        std::array<char, 256> errBuf{};
        av_strerror(ret, errBuf.data(), errBuf.size());
        qWarning() << "[StreamDecoder] avformat_open_input failed:" << errBuf.data();
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
        return false;
    }

    m_fmtCtx->max_analyze_duration = AppConfig::kMaxAnalyzeDuration;
    m_fmtCtx->probesize            = AppConfig::kProbeSize;

    avformat_find_stream_info(m_fmtCtx, nullptr);

    // -- Locate the first video stream ----------------------------------------
    m_videoStreamIdx = -1;
    for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIdx = static_cast<int>(i);
            break;
        }
    }
    if (m_videoStreamIdx < 0) {
        qWarning() << "[StreamDecoder] No video stream found in the stream.";
        closeStream();
        return false;
    }

    // -- Open codec -----------------------------------------------------------
    const AVCodecParameters *codecPar = m_fmtCtx->streams[m_videoStreamIdx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        qWarning() << "[StreamDecoder] Unsupported codec id:" << codecPar->codec_id;
        closeStream();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical() << "[StreamDecoder] avcodec_alloc_context3() failed.";
        closeStream();
        return false;
    }

    avcodec_parameters_to_context(m_codecCtx, codecPar);
    m_codecCtx->flags        |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2       |= AV_CODEC_FLAG2_FAST;
    m_codecCtx->thread_count  = AppConfig::kDecoderThreads;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qWarning() << "[StreamDecoder] avcodec_open2() failed.";
        closeStream();
        return false;
    }

    // Report initial resolution if available from SDP parameters.
    if (m_codecCtx->width > 0 && m_codecCtx->height > 0) {
        m_width.store(m_codecCtx->width,   std::memory_order_relaxed);
        m_height.store(m_codecCtx->height, std::memory_order_relaxed);
        qInfo() << "[StreamDecoder] Stream connected:"
                << m_codecCtx->width << "x" << m_codecCtx->height
                << "codec:" << codec->name;
    } else {
        qInfo() << "[StreamDecoder] Stream opened — resolution pending first frame.";
    }

    m_connected.store(true, std::memory_order_release);
    emit connectionChanged(true);
    return true;
}

void StreamDecoder::closeStream()
{
    m_connected.store(false, std::memory_order_release);

    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
    }

    m_videoStreamIdx = -1;
}

// ---------------------------------------------------------------------------
// Main decode loop (runs on the worker thread)
// ---------------------------------------------------------------------------

void StreamDecoder::run()
{
    m_running.store(true, std::memory_order_release);
    qInfo() << "[StreamDecoder] Decode thread started —" << m_url;

    while (m_running.load(std::memory_order_acquire)) {

        // Attempt to (re)connect if currently disconnected.
        if (!m_connected.load(std::memory_order_acquire)) {
            if (!openStream()) {
                QThread::msleep(AppConfig::kReconnectDelayMs);
                continue;
            }
        }

        AVPacket *pkt   = av_packet_alloc();
        AVFrame  *frame = av_frame_alloc();

        int  frameCount = 0;
        auto fpsStart   = std::chrono::steady_clock::now();

        // Inner loop: read and decode frames until the stream drops or stop() is called.
        while (m_running.load(std::memory_order_acquire) &&
               m_connected.load(std::memory_order_acquire))
        {
            int ret = av_read_frame(m_fmtCtx, pkt);
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN)) {
                    QThread::msleep(1);
                    continue;
                }
                std::array<char, 256> errBuf{};
                av_strerror(ret, errBuf.data(), errBuf.size());
                qWarning() << "[StreamDecoder] av_read_frame failed:"
                           << errBuf.data() << "— reconnecting...";
                m_connected.store(false, std::memory_order_release);
                emit connectionChanged(false);
                break;
            }

            // Drop non-video packets.
            if (pkt->stream_index != m_videoStreamIdx) {
                av_packet_unref(pkt);
                continue;
            }

            // Send encoded packet to codec.
            ret = avcodec_send_packet(m_codecCtx, pkt);
            av_packet_unref(pkt);
            if (ret < 0) {
                continue;
            }

            // Drain decoded frames.
            while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
                const int w = frame->width;
                const int h = frame->height;

                // (Re)create the scaler if it doesn't exist or the resolution has changed.
                if (!m_swsCtx ||
                    w != m_width.load(std::memory_order_relaxed) ||
                    h != m_height.load(std::memory_order_relaxed))
                {
                    m_width.store(w,  std::memory_order_relaxed);
                    m_height.store(h, std::memory_order_relaxed);

                    if (m_swsCtx) {
                        sws_freeContext(m_swsCtx);
                    }
                    m_swsCtx = sws_getContext(
                        w, h, static_cast<AVPixelFormat>(frame->format),
                        w, h, AV_PIX_FMT_RGB24,
                        AppConfig::kSwsAlgorithm, nullptr, nullptr, nullptr);

                    qInfo() << "[StreamDecoder] Scaler initialized for"
                            << w << "x" << h;
                }

                if (m_swsCtx) {
                    // Convert YUV frame to RGB QImage in the back-buffer,
                    // then make a deep copy for the signal emission.
                    QImage finalImage;
                    {
                        std::lock_guard<std::mutex> lock(m_bufMutex);
                        if (m_backBuf.width() != w || m_backBuf.height() != h) {
                            m_backBuf = QImage(w, h, QImage::Format_RGB888);
                        }
                        uint8_t *dst[4]   = { m_backBuf.bits(), nullptr, nullptr, nullptr };
                        int dstStride[4]  = { static_cast<int>(m_backBuf.bytesPerLine()), 0, 0, 0 };
                        sws_scale(m_swsCtx,
                                  frame->data, frame->linesize, 0, h,
                                  dst, dstStride);
                        finalImage = m_backBuf.copy();
                    }

                    emit frameReady(std::move(finalImage));
                }

                // Update FPS counter once per second.
                ++frameCount;
                const auto now     = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(now - fpsStart).count();
                if (elapsed >= 1.0) {
                    m_fps.store(frameCount / elapsed, std::memory_order_relaxed);
                    frameCount = 0;
                    fpsStart   = now;
                }

                av_frame_unref(frame);
            } // avcodec_receive_frame loop
        } // inner read loop

        av_packet_free(&pkt);
        av_frame_free(&frame);
        closeStream();
    } // outer reconnect loop

    qInfo() << "[StreamDecoder] Decode thread finished cleanly.";
}

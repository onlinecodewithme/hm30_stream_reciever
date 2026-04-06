#include "stream_decoder.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <chrono>

StreamDecoder::StreamDecoder(int port, const QString &sdpTemplatePath, QObject *parent)
    : QThread(parent), m_port(port), m_sdpTemplatePath(sdpTemplatePath)
{
    // Configure QTemporaryFile
    m_tempSdp.setFileTemplate(QDir::tempPath() + "/hm30_rtp_XXXXXX.sdp");
}

StreamDecoder::~StreamDecoder()
{
    stop();
}

void StreamDecoder::stop()
{
    m_running.store(false);
    wait(); // Wait safely for QThread to finish execution
}

bool StreamDecoder::openStream()
{
    // Read the SDP template and patch the port
    QFile sdpFile(m_sdpTemplatePath);
    if (!sdpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open SDP file:" << m_sdpTemplatePath;
        return false;
    }

    QString sdpContent = QTextStream(&sdpFile).readAll();
    sdpFile.close();

    // Replace port in the m= line with our configured port
    sdpContent.replace(QRegularExpression(R"(m=video\s+\d+)"), 
                       QString("m=video %1").arg(m_port));

    // Write patched SDP to a safe temp file
    if (!m_tempSdp.open()) {
        qWarning() << "Cannot create temporary SDP file!";
        return false;
    }
    QTextStream out(&m_tempSdp);
    out << sdpContent;
    m_tempSdp.flush(); // ensure it is written to disk

    qInfo().nospace() << "Opening RTP stream via SDP on port " << m_port 
                      << " (" << m_tempSdp.fileName() << ")";

    m_fmtCtx = avformat_alloc_context();
    if (!m_fmtCtx) return false;

    // ── Low-latency RTP options ──
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp", 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "flags", "low_delay", 0);
    av_dict_set(&opts, "framedrop", "1", 0);
    av_dict_set(&opts, "max_delay", "0", 0);
    av_dict_set(&opts, "reorder_queue_size", "0", 0);

    AVInputFormat *sdpFmt = const_cast<AVInputFormat*>(av_find_input_format("sdp"));
    if (!sdpFmt) {
        qWarning() << "SDP input format not available in this FFmpeg build";
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
        av_dict_free(&opts);
        return false;
    }

    int ret = avformat_open_input(&m_fmtCtx, m_tempSdp.fileName().toUtf8().constData(), sdpFmt, &opts);
    av_dict_free(&opts);
    
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        qWarning() << "Failed to open RTP stream:" << err;
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
        m_tempSdp.close(); // Clean up temp file connection immediately upon fail
        return false;
    }

    m_fmtCtx->max_analyze_duration = 5000000;
    m_fmtCtx->probesize = 5000000;

    avformat_find_stream_info(m_fmtCtx, nullptr);

    m_videoStreamIdx = -1;
    for (unsigned i = 0; i < m_fmtCtx->nb_streams; i++) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIdx = i;
            break;
        }
    }
    if (m_videoStreamIdx < 0) {
        qWarning() << "No video stream found in SDP";
        closeStream();
        return false;
    }

    AVCodecParameters *codecPar = m_fmtCtx->streams[m_videoStreamIdx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        qWarning() << "Unsupported codec";
        closeStream();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, codecPar);

    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;
    m_codecCtx->thread_count = 2;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qWarning() << "Failed to open codec";
        closeStream();
        return false;
    }

    if (m_codecCtx->width > 0 && m_codecCtx->height > 0) {
        m_width.store(m_codecCtx->width);
        m_height.store(m_codecCtx->height);
        qInfo() << "RTP stream connected:" << m_codecCtx->width << "x" << m_codecCtx->height << "codec:" << codec->name;
    } else {
        qInfo() << "RTP stream opened (resolution pending first frame)";
    }

    m_connected.store(true);
    emit connectionChanged(true);
    return true;
}

void StreamDecoder::closeStream()
{
    m_connected.store(false);

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
    m_tempSdp.close(); // Clean up QTemporaryFile
}

void StreamDecoder::run()
{
    m_running.store(true);
    qInfo() << "[StreamDecoder] Decode thread started — listening for RTP on port" << m_port;

    while (m_running.load()) {
        if (!m_connected.load()) {
            if (!openStream()) {
                QThread::msleep(100);
                continue;
            }
        }

        AVPacket *pkt = av_packet_alloc();
        AVFrame  *frame = av_frame_alloc();
        int frameCount = 0;
        auto fpsStart = std::chrono::steady_clock::now();

        while (m_running.load() && m_connected.load()) {
            int ret = av_read_frame(m_fmtCtx, pkt);
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN)) {
                    QThread::msleep(1);
                    continue;
                }
                char errBuf[256];
                av_strerror(ret, errBuf, sizeof(errBuf));
                qWarning() << "Read frame failed:" << errBuf << "(" << ret << ") - reconnecting...";
                m_connected.store(false);
                emit connectionChanged(false);
                break;
            }

            if (pkt->stream_index != m_videoStreamIdx) {
                av_packet_unref(pkt);
                continue;
            }

            ret = avcodec_send_packet(m_codecCtx, pkt);
            av_packet_unref(pkt);
            if (ret < 0) continue;

            while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
                int w = frame->width;
                int h = frame->height;

                if (w != m_width.load() || h != m_height.load()) {
                    m_width.store(w);
                    m_height.store(h);

                    if (m_swsCtx) sws_freeContext(m_swsCtx);
                    m_swsCtx = sws_getContext(
                        w, h, (AVPixelFormat)frame->format,
                        w, h, AV_PIX_FMT_RGB24,
                        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
                    );
                    qInfo() << "Resolution changed to" << w << "x" << h;
                }

                // Copy directly into local buffer explicitly sized appropriately
                QImage finalImage; 
                {
                    // Ensure thread-safe modification of back-buffer map
                    std::lock_guard<std::mutex> lock(m_bufMutex);
                    if (m_backBuf.width() != w || m_backBuf.height() != h) {
                        m_backBuf = QImage(w, h, QImage::Format_RGB888);
                    }

                    uint8_t *dst[4] = { m_backBuf.bits(), nullptr, nullptr, nullptr };
                    int dstStride[4] = { static_cast<int>(m_backBuf.bytesPerLine()), 0, 0, 0 };

                    sws_scale(m_swsCtx,
                              frame->data, frame->linesize, 0, h,
                              dst, dstStride);
                    
                    finalImage = m_backBuf.copy(); // Extremely fast deep copy guaranteeing uncorrupted state passed via cross-thread signal queue
                }
                
                emit frameReady(std::move(finalImage)); // Transfer perfectly-valid image

                frameCount++;
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - fpsStart).count();
                if (elapsed >= 1.0) {
                    m_fps.store(frameCount / elapsed);
                    frameCount = 0;
                    fpsStart = now;
                }

                av_frame_unref(frame);
            }
        }

        av_packet_free(&pkt);
        av_frame_free(&frame);
        closeStream();
    }

    qInfo() << "[StreamDecoder] Decode thread finished gracefully";
}

#include "headless_decoder.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <unistd.h>   // close()
#include <sstream>

// ---------------------------------------------------------------------------
// Internal constants (mirrors AppConfig values)
// ---------------------------------------------------------------------------
static constexpr int kMaxAnalyzeDuration = 5'000'000;
static constexpr int kProbeSize          = 5'000'000;
static constexpr int kDecoderThreads     = 2;
static constexpr int kReconnectDelayMs   = 100;
static constexpr int kSwsAlgorithm       = 4; // SWS_FAST_BILINEAR

// ---------------------------------------------------------------------------
// Helper: millisecond sleep
// ---------------------------------------------------------------------------
static void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// Helper: format av error as string
// ---------------------------------------------------------------------------
static std::string avErr(int ret) {
    std::array<char, 256> buf{};
    av_strerror(ret, buf.data(), buf.size());
    return std::string(buf.data());
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

HeadlessDecoder::HeadlessDecoder(int port, const std::string& sdpTemplatePath)
    : m_port(port)
    , m_sdpTemplatePath(sdpTemplatePath)
{}

HeadlessDecoder::~HeadlessDecoder() {
    stop();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void HeadlessDecoder::setFrameCallback(FrameCallback cb) {
    m_frameCb = std::move(cb);
}

void HeadlessDecoder::setConnectionCallback(std::function<void(bool)> cb) {
    m_connCb = std::move(cb);
}

void HeadlessDecoder::start() {
    if (m_running.load(std::memory_order_acquire)) return;
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&HeadlessDecoder::decodeLoop, this);
}

void HeadlessDecoder::stop() {
    m_running.store(false, std::memory_order_release);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string HeadlessDecoder::buildSdpContent() const {
    std::ifstream f(m_sdpTemplatePath);
    if (!f.is_open()) {
        std::cerr << "[HeadlessDecoder] Cannot open SDP template: "
                  << m_sdpTemplatePath << "\n";
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    // Patch the port number in the  m=video  line.
    std::regex portRe(R"(m=video\s+\d+)");
    content = std::regex_replace(content, portRe, "m=video " + std::to_string(m_port));
    return content;
}

bool HeadlessDecoder::openStream() {
    // -- Build and write patched SDP to a temp file --------------------------
    const std::string sdpContent = buildSdpContent();
    if (sdpContent.empty()) return false;

    // Create a unique temp file name.
    char tmplBuf[] = "/tmp/hm30_hl_XXXXXX.sdp";
    int fd = mkstemps(tmplBuf, 4); // 4 = length of ".sdp"
    if (fd < 0) {
        std::cerr << "[HeadlessDecoder] mkstemps() failed.\n";
        return false;
    }
    {
        // Write SDP content then close the fd (FFmpeg will open by path).
        std::ofstream out(tmplBuf);
        out << sdpContent;
    }
    close(fd);
    m_tempSdpPath = tmplBuf;

    std::cout << "[HeadlessDecoder] Opening RTP stream — port " << m_port
              << " via " << m_tempSdpPath << "\n";

    // -- Allocate format context ---------------------------------------------
    m_fmtCtx = avformat_alloc_context();
    if (!m_fmtCtx) {
        std::cerr << "[HeadlessDecoder] avformat_alloc_context() failed.\n";
        return false;
    }

    // -- Low-latency AVDictionary options ------------------------------------
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp", 0);
    av_dict_set(&opts, "fflags",             "nobuffer",      0);
    av_dict_set(&opts, "flags",              "low_delay",     0);
    av_dict_set(&opts, "framedrop",          "1",             0);
    av_dict_set(&opts, "max_delay",          "0",             0);
    av_dict_set(&opts, "reorder_queue_size", "0",             0);

    // -- Find SDP demuxer ----------------------------------------------------
    const AVInputFormat* sdpFmt = av_find_input_format("sdp");
    if (!sdpFmt) {
        std::cerr << "[HeadlessDecoder] SDP input format unavailable.\n";
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
        av_dict_free(&opts);
        return false;
    }

    // -- Open the stream -----------------------------------------------------
    int ret = avformat_open_input(&m_fmtCtx,
                                  m_tempSdpPath.c_str(),
                                  const_cast<AVInputFormat*>(sdpFmt), &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        std::cerr << "[HeadlessDecoder] avformat_open_input failed: "
                  << avErr(ret) << "\n";
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
        std::remove(m_tempSdpPath.c_str());
        return false;
    }

    m_fmtCtx->max_analyze_duration = kMaxAnalyzeDuration;
    m_fmtCtx->probesize            = kProbeSize;

    avformat_find_stream_info(m_fmtCtx, nullptr);

    // -- Locate the first video stream ---------------------------------------
    m_videoStreamIdx = -1;
    for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIdx = static_cast<int>(i);
            break;
        }
    }
    if (m_videoStreamIdx < 0) {
        std::cerr << "[HeadlessDecoder] No video stream found in SDP.\n";
        closeStream();
        return false;
    }

    // -- Open codec ----------------------------------------------------------
    const AVCodecParameters* codecPar =
        m_fmtCtx->streams[m_videoStreamIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        std::cerr << "[HeadlessDecoder] Unsupported codec id: "
                  << codecPar->codec_id << "\n";
        closeStream();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        std::cerr << "[HeadlessDecoder] avcodec_alloc_context3() failed.\n";
        closeStream();
        return false;
    }

    avcodec_parameters_to_context(m_codecCtx, codecPar);
    m_codecCtx->flags        |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2       |= AV_CODEC_FLAG2_FAST;
    m_codecCtx->thread_count  = kDecoderThreads;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        std::cerr << "[HeadlessDecoder] avcodec_open2() failed.\n";
        closeStream();
        return false;
    }

    if (m_codecCtx->width > 0 && m_codecCtx->height > 0) {
        m_width.store(m_codecCtx->width,   std::memory_order_relaxed);
        m_height.store(m_codecCtx->height, std::memory_order_relaxed);
        std::cout << "[HeadlessDecoder] Stream connected: "
                  << m_codecCtx->width << "x" << m_codecCtx->height
                  << " codec:" << codec->name << "\n";
    } else {
        std::cout << "[HeadlessDecoder] Stream opened — resolution pending first frame.\n";
    }

    m_connected.store(true, std::memory_order_release);
    if (m_connCb) m_connCb(true);
    return true;
}

void HeadlessDecoder::closeStream() {
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

    if (!m_tempSdpPath.empty()) {
        std::remove(m_tempSdpPath.c_str());
        m_tempSdpPath.clear();
    }
}

// ---------------------------------------------------------------------------
// Main decode loop (runs on m_thread)
// ---------------------------------------------------------------------------

void HeadlessDecoder::decodeLoop() {
    std::cout << "[HeadlessDecoder] Decode thread started — UDP port "
              << m_port << "\n";

    while (m_running.load(std::memory_order_acquire)) {

        // Attempt to (re)connect if currently disconnected.
        if (!m_connected.load(std::memory_order_acquire)) {
            if (!openStream()) {
                sleepMs(kReconnectDelayMs);
                continue;
            }
        }

        AVPacket* pkt   = av_packet_alloc();
        AVFrame*  frame = av_frame_alloc();

        int  frameCount = 0;
        auto fpsStart   = std::chrono::steady_clock::now();

        // Inner loop: read and decode frames until stream drops or stop().
        while (m_running.load(std::memory_order_acquire) &&
               m_connected.load(std::memory_order_acquire))
        {
            int ret = av_read_frame(m_fmtCtx, pkt);
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN)) {
                    sleepMs(1);
                    continue;
                }
                std::cerr << "[HeadlessDecoder] av_read_frame failed: "
                          << avErr(ret) << " — reconnecting...\n";
                m_connected.store(false, std::memory_order_release);
                if (m_connCb) m_connCb(false);
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
            if (ret < 0) continue;

            // Drain decoded frames.
            while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
                const int w = frame->width;
                const int h = frame->height;

                // (Re)create scaler if resolution changed.
                if (!m_swsCtx ||
                    w != m_width.load(std::memory_order_relaxed) ||
                    h != m_height.load(std::memory_order_relaxed))
                {
                    m_width.store(w,  std::memory_order_relaxed);
                    m_height.store(h, std::memory_order_relaxed);

                    if (m_swsCtx) sws_freeContext(m_swsCtx);
                    m_swsCtx = sws_getContext(
                        w, h, static_cast<AVPixelFormat>(frame->format),
                        w, h, AV_PIX_FMT_RGB24,
                        kSwsAlgorithm, nullptr, nullptr, nullptr);

                    std::cout << "[HeadlessDecoder] Scaler initialized for "
                              << w << "x" << h << "\n";
                }

                if (m_swsCtx && m_frameCb) {
                    const size_t bufSize = static_cast<size_t>(w) * h * 3;
                    {
                        std::lock_guard<std::mutex> lock(m_bufMutex);
                        if (m_rgbBuf.size() != bufSize) {
                            m_rgbBuf.resize(bufSize);
                        }
                        uint8_t* dst[4]  = { m_rgbBuf.data(), nullptr, nullptr, nullptr };
                        int dstStride[4] = { w * 3, 0, 0, 0 };
                        sws_scale(m_swsCtx,
                                  frame->data, frame->linesize, 0, h,
                                  dst, dstStride);
                    }
                    m_frameCb(w, h, m_rgbBuf.data(), bufSize);
                }

                // Update FPS counter once per second.
                ++frameCount;
                const auto now = std::chrono::steady_clock::now();
                const double elapsed =
                    std::chrono::duration<double>(now - fpsStart).count();
                if (elapsed >= 1.0) {
                    m_fps.store(frameCount / elapsed, std::memory_order_relaxed);
                    std::cout << "[HeadlessDecoder] FPS: "
                              << static_cast<int>(m_fps.load()) << "\n";
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

    std::cout << "[HeadlessDecoder] Decode thread finished cleanly.\n";
}

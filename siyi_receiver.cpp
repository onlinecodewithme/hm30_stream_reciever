/**
 * SIYI HM30 Ground Unit — H.264 RTP/UDP Stream Receiver & Display
 *
 * Receives an H.264 RTP/UDP video stream (or RTSP) and displays it
 * in a window using SDL2.
 *
 * Usage:
 *   # Listen for RTP/UDP on a local port (needs SDP file):
 *   ./siyi_receiver --udp 5600
 *
 *   # Pull from RTSP (ground unit):
 *   ./siyi_receiver --rtsp rtsp://192.168.144.12:8554/stream
 *
 *   # Direct RTP/UDP with inline SDP (most common):
 *   ./siyi_receiver --udp 5600
 *
 * Build:
 *   mkdir -p build && cd build && cmake .. && make -j$(nproc)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <getopt.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

#include <SDL2/SDL.h>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static volatile sig_atomic_t g_running = 1;
static void signal_handler(int) { g_running = 0; }

// ---------------------------------------------------------------------------
// SDL Display Context
// ---------------------------------------------------------------------------
struct DisplayCtx {
    SDL_Window   *window   = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture  *texture  = nullptr;
    int width  = 0;
    int height = 0;
};

static bool display_init(DisplayCtx &dctx, int w, int h)
{
    dctx.width  = w;
    dctx.height = h;

    dctx.window = SDL_CreateWindow(
        "SIYI HM30 — Stream Receiver",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!dctx.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    dctx.renderer = SDL_CreateRenderer(dctx.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!dctx.renderer) {
        // Fall back to software renderer
        dctx.renderer = SDL_CreateRenderer(dctx.window, -1, 0);
    }
    if (!dctx.renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    dctx.texture = SDL_CreateTexture(dctx.renderer,
        SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!dctx.texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

static void display_frame(DisplayCtx &dctx, AVFrame *frame)
{
    SDL_UpdateYUVTexture(dctx.texture, nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1],
        frame->data[2], frame->linesize[2]);

    SDL_RenderClear(dctx.renderer);
    SDL_RenderCopy(dctx.renderer, dctx.texture, nullptr, nullptr);
    SDL_RenderPresent(dctx.renderer);
}

static void display_cleanup(DisplayCtx &dctx)
{
    if (dctx.texture)  SDL_DestroyTexture(dctx.texture);
    if (dctx.renderer) SDL_DestroyRenderer(dctx.renderer);
    if (dctx.window)   SDL_DestroyWindow(dctx.window);
}

// ---------------------------------------------------------------------------
// Generate SDP file for raw RTP/UDP reception
// ---------------------------------------------------------------------------
static const char* generate_sdp(int port, const char *tmppath)
{
    FILE *f = fopen(tmppath, "w");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot create SDP file %s\n", tmppath);
        return nullptr;
    }
    fprintf(f,
        "v=0\n"
        "o=- 0 0 IN IP4 0.0.0.0\n"
        "s=SIYI Stream\n"
        "c=IN IP4 0.0.0.0\n"
        "t=0 0\n"
        "m=video %d RTP/AVP 96\n"
        "a=rtpmap:96 H264/90000\n"
        "a=fmtp:96 packetization-mode=1\n",
        port);
    fclose(f);
    return tmppath;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    const char *rtsp_url = nullptr;
    int udp_port         = 0;

    static struct option long_opts[] = {
        {"rtsp",  required_argument, nullptr, 'r'},
        {"udp",   required_argument, nullptr, 'u'},
        {"help",  no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:u:h", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'r': rtsp_url = optarg;        break;
            case 'u': udp_port = atoi(optarg);  break;
            case 'h':
            default:
                fprintf(stderr,
                    "Usage: %s [options]\n"
                    "  --rtsp <url>   RTSP stream URL\n"
                    "                 e.g. rtsp://192.168.144.12:8554/stream\n"
                    "  --udp  <port>  Listen on UDP port for RTP H.264\n"
                    "                 e.g. --udp 5600\n",
                    argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (!rtsp_url && udp_port == 0) {
        fprintf(stderr, "ERROR: Specify --rtsp <url> or --udp <port>.\n"
                        "  Example: %s --udp 5600\n"
                        "  Example: %s --rtsp rtsp://192.168.144.12:8554/stream\n",
                        argv[0], argv[0]);
        return 1;
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // --- Init SDL ---
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // --- Build input URL ---
    char input_url[512];
    char sdp_path[] = "/tmp/siyi_receiver.sdp";

    if (rtsp_url) {
        snprintf(input_url, sizeof(input_url), "%s", rtsp_url);
        printf("=== SIYI HM30 Stream Receiver ===\n");
        printf("Source: RTSP %s\n", input_url);
    } else {
        generate_sdp(udp_port, sdp_path);
        snprintf(input_url, sizeof(input_url), "%s", sdp_path);
        printf("=== SIYI HM30 Stream Receiver ===\n");
        printf("Source: RTP/UDP port %d\n", udp_port);
    }
    printf("Waiting for stream...\n\n");

    // --- Open input ---
    AVFormatContext *fmt_ctx = nullptr;
    AVDictionary *opts = nullptr;

    if (rtsp_url) {
        // RTSP options: prefer UDP transport, reduce buffer/latency
        av_dict_set(&opts, "rtsp_transport", "udp", 0);
        av_dict_set(&opts, "stimeout", "5000000", 0);   // 5s timeout
        av_dict_set(&opts, "buffer_size", "512000", 0);
    } else {
        // SDP file input
        av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp", 0);
    }

    int ret = avformat_open_input(&fmt_ctx, input_url, nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "ERROR: Could not open input '%s': %s\n",
                input_url, errbuf);
        SDL_Quit();
        return 1;
    }

    // --- Find stream info ---
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        fprintf(stderr, "ERROR: Could not find stream info.\n");
        avformat_close_input(&fmt_ctx);
        SDL_Quit();
        return 1;
    }

    av_dump_format(fmt_ctx, 0, input_url, 0);

    // --- Find video stream ---
    int video_idx = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = (int)i;
            break;
        }
    }
    if (video_idx < 0) {
        fprintf(stderr, "ERROR: No video stream found.\n");
        avformat_close_input(&fmt_ctx);
        SDL_Quit();
        return 1;
    }

    AVCodecParameters *codecpar = fmt_ctx->streams[video_idx]->codecpar;
    printf("\nVideo: %dx%d, codec_id=%d\n",
           codecpar->width, codecpar->height, codecpar->codec_id);

    // --- Open decoder ---
    const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        fprintf(stderr, "ERROR: Decoder not found for codec_id %d.\n",
                codecpar->codec_id);
        avformat_close_input(&fmt_ctx);
        SDL_Quit();
        return 1;
    }

    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, codecpar);

    // Low-latency decoding flags
    dec_ctx->flags  |= AV_CODEC_FLAG_LOW_DELAY;
    dec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        fprintf(stderr, "ERROR: Could not open decoder.\n");
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        SDL_Quit();
        return 1;
    }

    printf("Using decoder: %s\n\n", decoder->name);

    // --- Init display (deferred until first frame with known size) ---
    DisplayCtx dctx;
    bool display_ready = false;

    // --- Conversion context for non-YUV420P formats ---
    SwsContext *sws_ctx = nullptr;
    AVFrame *rgb_frame  = nullptr;

    AVFrame  *frame = av_frame_alloc();
    AVPacket *pkt   = av_packet_alloc();

    int64_t frame_count  = 0;
    int64_t start_time   = av_gettime_relative();

    printf("Receiving stream...\n");

    // --- Main receive loop ---
    while (g_running) {
        // Poll SDL events (must do this to keep window responsive)
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                g_running = 0;
                break;
            }
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_ESCAPE) {
                g_running = 0;
                break;
            }
        }
        if (!g_running) break;

        // Read packet
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                printf("End of stream.\n");
                break;
            }
            // Timeout or transient error — retry
            av_usleep(10000);
            continue;
        }

        if (pkt->stream_index != video_idx) {
            av_packet_unref(pkt);
            continue;
        }

        // Decode
        ret = avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        while (avcodec_receive_frame(dec_ctx, frame) == 0) {
            // Init display on first decoded frame
            if (!display_ready) {
                int w = frame->width;
                int h = frame->height;
                printf("Stream active: %dx%d\n", w, h);

                if (!display_init(dctx, w, h)) {
                    fprintf(stderr, "ERROR: Display init failed.\n");
                    g_running = 0;
                    break;
                }
                display_ready = true;

                // If frame isn't YUV420P, set up conversion
                if (frame->format != AV_PIX_FMT_YUV420P) {
                    sws_ctx = sws_getContext(w, h,
                        (AVPixelFormat)frame->format,
                        w, h, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    rgb_frame = av_frame_alloc();
                    rgb_frame->format = AV_PIX_FMT_YUV420P;
                    rgb_frame->width  = w;
                    rgb_frame->height = h;
                    av_frame_get_buffer(rgb_frame, 0);
                }
            }

            // Convert if needed, then display
            if (sws_ctx) {
                sws_scale(sws_ctx,
                    frame->data, frame->linesize, 0, frame->height,
                    rgb_frame->data, rgb_frame->linesize);
                display_frame(dctx, rgb_frame);
            } else {
                display_frame(dctx, frame);
            }

            frame_count++;

            // Stats every 2 seconds
            if (frame_count % 60 == 0) {
                double elapsed = (av_gettime_relative() - start_time) / 1e6;
                printf("\r  Frames: %ld | Time: %.1fs | FPS: %.1f   ",
                       (long)frame_count, elapsed, frame_count / elapsed);
                fflush(stdout);
            }
        }
    }

    printf("\n\nShutting down... (received %ld frames)\n", (long)frame_count);

    // Cleanup
    if (sws_ctx)   sws_freeContext(sws_ctx);
    if (rgb_frame) av_frame_free(&rgb_frame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    display_cleanup(dctx);
    SDL_Quit();

    // Cleanup temp SDP file
    if (udp_port > 0) remove(sdp_path);

    printf("Done.\n");
    return 0;
}

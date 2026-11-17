#include "rafstream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <io/pad.h>
#include <tiny3d.h>
#include <audio/audio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

/* Pemutar Video Streaming Native PS3 (FFmpeg libavformat + RSX Texture + AudioOut) */
int play_stream_video(const char *stream_url, const char *video_title) {
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *opts = NULL;

    av_dict_set(&opts, "user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)", 0);
    av_dict_set(&opts, "referer", "https://vidy.st/", 0);
    av_dict_set(&opts, "tls_verify", "0", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "5", 0);

    if (avformat_open_input(&fmt_ctx, stream_url, NULL, &opts) != 0) {
        av_dict_free(&opts);
        return -1;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        avformat_close_input(&fmt_ctx);
        return -2;
    }

    int video_idx = -1;
    int audio_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_idx < 0) {
            video_idx = (int)i;
        } else if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_idx < 0) {
            audio_idx = (int)i;
        }
    }

    if (video_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return -3;
    }

    const AVCodec *vcodec = avcodec_find_decoder(fmt_ctx->streams[video_idx]->codecpar->codec_id);
    if (!vcodec) {
        avformat_close_input(&fmt_ctx);
        return -4;
    }

    AVCodecContext *vctx = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(vctx, fmt_ctx->streams[video_idx]->codecpar);
    vctx->thread_count = 2;
    if (avcodec_open2(vctx, vcodec, NULL) < 0) {
        avcodec_free_context(&vctx);
        avformat_close_input(&fmt_ctx);
        return -5;
    }

    /* Setup Audio Decoder & PS3 Hardware AudioOut */
    AVCodecContext *actx = NULL;
    SwrContext *swr = NULL;
    u32 audio_port = 0;
    audioPortParam a_param;
    audioPortConfig a_config;
    int audio_ready = 0;

    if (audio_idx >= 0) {
        const AVCodec *acodec = avcodec_find_decoder(fmt_ctx->streams[audio_idx]->codecpar->codec_id);
        if (acodec) {
            actx = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(actx, fmt_ctx->streams[audio_idx]->codecpar);
            if (avcodec_open2(actx, acodec, NULL) == 0) {
                memset(&a_param, 0, sizeof(a_param));
                a_param.numChannels = AUDIO_PORT_2CH;
                a_param.numBlocks = AUDIO_BLOCK_8;
                a_param.attrib = 0;
                a_param.level = 1.0f;
                if (audioPortOpen(&a_param, &audio_port) == 0) {
                    audioGetPortConfig(audio_port, &a_config);
                    audioPortStart(audio_port);
                    swr = swr_alloc_set_opts(NULL,
                        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, 48000,
                        actx->channel_layout ? (int64_t)actx->channel_layout : AV_CH_LAYOUT_STEREO,
                        actx->sample_fmt, actx->sample_rate, 0, NULL);
                    if (swr && swr_init(swr) >= 0) {
                        audio_ready = 1;
                    }
                }
            }
        }
    }

    /* Konversi frame video ke ARGB 32-bit untuk ditampilkan via RSX Tiny3D */
    int dst_w = (vctx->width > 1280) ? 1280 : vctx->width;
    int dst_h = (vctx->height > 720) ? 720 : vctx->height;
    struct SwsContext *sws = sws_getContext(
        vctx->width, vctx->height, vctx->pix_fmt,
        dst_w, dst_h, AV_PIX_FMT_ARGB,
        SWS_FAST_BILINEAR, NULL, NULL, NULL
    );

    u32 *rsx_frame = (u32 *)tiny3d_AllocTexture(dst_w * dst_h * 4);
    u32 rsx_offset = tiny3d_TextureOffset(rsx_frame);

    AVFrame *frame = av_frame_alloc();
    AVFrame *argb_frame = av_frame_alloc();
    argb_frame->data[0] = (uint8_t *)rsx_frame;
    argb_frame->linesize[0] = dst_w * 4;

    AVPacket *pkt = av_packet_alloc();
    padInfo padinfo;
    padData paddata;
    u32 prev_btn = 0;
    int paused = 0;
    int running = 1;

    (void)video_title;

    while (running) {
        ioPadGetInfo(&padinfo);
        if (padinfo.status[0]) {
            ioPadGetData(0, &paddata);
            u32 pressed = paddata.button[2] & ~prev_btn;
            prev_btn = paddata.button[2];

            if (paddata.BTN_CIRCLE) {
                running = 0; /* Tombol O: Stop & kembali ke detail */
                break;
            }
            if (pressed & (1 << 14)) { /* Tombol X: Pause / Resume */
                paused = !paused;
            }
            if (paddata.BTN_RIGHT || paddata.BTN_LEFT) {
                int64_t step = paddata.BTN_RIGHT ? (10 * AV_TIME_BASE) : (-10 * AV_TIME_BASE);
                int64_t target = av_gettime() + step;
                (void)target;
                av_seek_frame(fmt_ctx, -1, (paddata.BTN_RIGHT ? 15 : -15) * AV_TIME_BASE, AVSEEK_FLAG_ANY);
            }
        }

        if (paused) {
            usleep(30000);
            continue;
        }

        int r = av_read_frame(fmt_ctx, pkt);
        if (r < 0) {
            break; /* Video selesai atau terputus */
        }

        if (pkt->stream_index == video_idx) {
            if (avcodec_send_packet(vctx, pkt) == 0) {
                while (avcodec_receive_frame(vctx, frame) == 0) {
                    sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize,
                              0, vctx->height, argb_frame->data, argb_frame->linesize);

                    tiny3d_Clear(0xFF000000, TINY3D_CLEAR_ALL);
                    tiny3d_SetTexture(0, rsx_offset, dst_w, dst_h, dst_w * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);

                    float scale_x = 848.0f;
                    float scale_y = 512.0f;
                    tiny3d_SetPolygon(TINY3D_QUADS);
                    tiny3d_VertexPos(0.0f, 0.0f, 0.0f);
                    tiny3d_VertexTexture(0.0f, 0.0f);
                    tiny3d_VertexPos(scale_x, 0.0f, 0.0f);
                    tiny3d_VertexTexture(1.0f, 0.0f);
                    tiny3d_VertexPos(scale_x, scale_y, 0.0f);
                    tiny3d_VertexTexture(1.0f, 1.0f);
                    tiny3d_VertexPos(0.0f, scale_y, 0.0f);
                    tiny3d_VertexTexture(0.0f, 1.0f);
                    tiny3d_End();
                    tiny3d_Flip();
                }
            }
        } else if (audio_ready && pkt->stream_index == audio_idx) {
            if (avcodec_send_packet(actx, pkt) == 0) {
                while (avcodec_receive_frame(actx, frame) == 0) {
                    float out_samples[4096 * 2];
                    uint8_t *out_ptr = (uint8_t *)out_samples;
                    int converted = swr_convert(swr, &out_ptr, 4096,
                                                (const uint8_t **)frame->data, frame->nb_samples);
                    if (converted > 0) {
                        u32 *ring = (u32 *)(uintptr_t)a_config.audioDataStart;
                        size_t copy_bytes = (size_t)converted * 2 * sizeof(float);
                        if (copy_bytes > 256 * 2 * sizeof(float)) copy_bytes = 256 * 2 * sizeof(float);
                        memcpy(ring, out_samples, copy_bytes);
                    }
                }
            }
        }

        av_packet_unref(pkt);
    }

    if (audio_ready) {
        audioPortStop(audio_port);
        audioPortClose(audio_port);
        if (swr) swr_free(&swr);
    }
    if (actx) avcodec_free_context(&actx);
    av_packet_free(&pkt);
    av_frame_free(&argb_frame);
    av_frame_free(&frame);
    sws_freeContext(sws);
    avcodec_free_context(&vctx);
    avformat_close_input(&fmt_ctx);
    return 0;
}

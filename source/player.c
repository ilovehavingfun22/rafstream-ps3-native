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

/* Kompatibel dengan FFmpeg 2.x/3.0 maupun FFmpeg 3.1+ di ps3libraries */
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
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
        enum AVMediaType mtype = fmt_ctx->streams[i]->codecpar->codec_type;
#else
        enum AVMediaType mtype = fmt_ctx->streams[i]->codec->codec_type;
#endif
        if (mtype == AVMEDIA_TYPE_VIDEO && video_idx < 0) {
            video_idx = (int)i;
        } else if (mtype == AVMEDIA_TYPE_AUDIO && audio_idx < 0) {
            audio_idx = (int)i;
        }
    }

    if (video_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return -3;
    }

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
    const AVCodec *vcodec = avcodec_find_decoder(fmt_ctx->streams[video_idx]->codecpar->codec_id);
    if (!vcodec) {
        avformat_close_input(&fmt_ctx);
        return -4;
    }
    AVCodecContext *vctx = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(vctx, fmt_ctx->streams[video_idx]->codecpar);
#else
    AVCodecContext *vctx = fmt_ctx->streams[video_idx]->codec;
    AVCodec *vcodec = avcodec_find_decoder(vctx->codec_id);
    if (!vcodec) {
        avformat_close_input(&fmt_ctx);
        return -4;
    }
#endif

    vctx->thread_count = 2;
    if (avcodec_open2(vctx, vcodec, NULL) < 0) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
        avcodec_free_context(&vctx);
#endif
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
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
        const AVCodec *acodec = avcodec_find_decoder(fmt_ctx->streams[audio_idx]->codecpar->codec_id);
        if (acodec) {
            actx = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(actx, fmt_ctx->streams[audio_idx]->codecpar);
        }
#else
        actx = fmt_ctx->streams[audio_idx]->codec;
        AVCodec *acodec = avcodec_find_decoder(actx->codec_id);
#endif
        if (acodec && actx && avcodec_open2(actx, acodec, NULL) == 0) {
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

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

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
                running = 0;
                break;
            }
            if (pressed & (1 << 14)) {
                paused = !paused;
            }
            if (paddata.BTN_RIGHT || paddata.BTN_LEFT) {
                av_seek_frame(fmt_ctx, -1, (paddata.BTN_RIGHT ? 15 : -15) * AV_TIME_BASE, AVSEEK_FLAG_ANY);
            }
        }

        if (paused) {
            usleep(30000);
            continue;
        }

        if (av_read_frame(fmt_ctx, &pkt) < 0) {
            break;
        }

        if (pkt.stream_index == video_idx) {
            int got_frame = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
            if (avcodec_send_packet(vctx, &pkt) == 0 && avcodec_receive_frame(vctx, frame) == 0) {
                got_frame = 1;
            }
#else
            avcodec_decode_video2(vctx, frame, &got_frame, &pkt);
#endif
            if (got_frame) {
                sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize,
                          0, vctx->height, argb_frame->data, argb_frame->linesize);

                tiny3d_Clear(0xFF000000, TINY3D_CLEAR_ALL);
                tiny3d_SetTexture(0, rsx_offset, dst_w, dst_h, dst_w * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);

                tiny3d_SetPolygon(TINY3D_QUADS);
                tiny3d_VertexPos(0.0f, 0.0f, 0.0f);
                tiny3d_VertexTexture(0.0f, 0.0f);
                tiny3d_VertexPos(848.0f, 0.0f, 0.0f);
                tiny3d_VertexTexture(1.0f, 0.0f);
                tiny3d_VertexPos(848.0f, 512.0f, 0.0f);
                tiny3d_VertexTexture(1.0f, 1.0f);
                tiny3d_VertexPos(0.0f, 512.0f, 0.0f);
                tiny3d_VertexTexture(0.0f, 1.0f);
                tiny3d_End();
                tiny3d_Flip();
            }
        } else if (audio_ready && pkt.stream_index == audio_idx) {
            int got_audio = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
            if (avcodec_send_packet(actx, &pkt) == 0 && avcodec_receive_frame(actx, frame) == 0) {
                got_audio = 1;
            }
#else
            avcodec_decode_audio4(actx, frame, &got_audio, &pkt);
#endif
            if (got_audio) {
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

        av_packet_unref(&pkt);
    }

    if (audio_ready) {
        audioPortStop(audio_port);
        audioPortClose(audio_port);
        if (swr) swr_free(&swr);
    }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
    if (actx) avcodec_free_context(&actx);
    avcodec_free_context(&vctx);
#else
    if (actx) avcodec_close(actx);
    avcodec_close(vctx);
#endif
    av_frame_free(&argb_frame);
    av_frame_free(&frame);
    sws_freeContext(sws);
    avformat_close_input(&fmt_ctx);
    return 0;
}

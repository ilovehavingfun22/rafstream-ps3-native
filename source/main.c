/*
 * RAFSTREAM PS3 NATIVE APPLICATION (100% STANDALONE - TANPA MOVIAN)
 * UI Hardware RSX 720p + DualShock 3 Navigation + Direct HTTPS ke Vercel + FFmpeg Video Player
 */
#include "rafstream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/process.h>
#include <io/pad.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sysmodule/sysmodule.h>
#include <audio/audio.h>
#include <curl/curl.h>
#include <libavformat/avformat.h>
#include <tiny3d.h>

/* 256 KB stack cukup karena array besar sudah di BSS, menyisakan memori 1M-page untuk RSX */
SYS_PROCESS_PARAM(1001, 0x40000);

static void boot_log(const char *msg) {
    FILE *f = fopen("/dev_hdd0/tmp/rafstream_boot.log", "a");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

/* Bitmap font 8x8 bawaan agar teks tajam tanpa tergantung file .ttf eksternal */
static const uint8_t FONT8X8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},{0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},{0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},{0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},{0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},{0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},{0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},{0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},{0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},{0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},{0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},{0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},{0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},{0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},{0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},{0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},{0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},{0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},{0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},{0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},{0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},{0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},{0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},{0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},{0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},{0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},{0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},{0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},{0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},{0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00},{0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00},{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},{0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},{0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},{0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},{0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},{0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},{0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},{0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},{0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},{0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},{0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

/* Simpan di BSS (RAM utama), bukan di stack */
static MediaItem items[MAX_ITEMS];
static EpisodeItem episodes[MAX_EPISODES];

enum {
    PAD_BIT_SELECT   = (1 << 0),
    PAD_BIT_START    = (1 << 3),
    PAD_BIT_UP       = (1 << 4),
    PAD_BIT_RIGHT    = (1 << 5),
    PAD_BIT_DOWN     = (1 << 6),
    PAD_BIT_LEFT     = (1 << 7),
    PAD_BIT_L2       = (1 << 8),
    PAD_BIT_R2       = (1 << 9),
    PAD_BIT_L1       = (1 << 10),
    PAD_BIT_R1       = (1 << 11),
    PAD_BIT_TRIANGLE = (1 << 12),
    PAD_BIT_CIRCLE   = (1 << 13),
    PAD_BIT_CROSS    = (1 << 14),
    PAD_BIT_SQUARE   = (1 << 15)
};

static void begin_frame_2d(u32 clear_color) {
    tiny3d_Clear(clear_color, TINY3D_CLEAR_ALL);
    tiny3d_Project2D();
}

static void draw_rect(float x, float y, float w, float h, u32 color) {
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexPos(x + w, y, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexPos(x + w, y + h, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexPos(x, y + h, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_End();
}

static void draw_text(float x, float y, float scale, u32 color, const char *str) {
    if (!str) return;
    float cx = x;
    while (*str) {
        unsigned char ch = (unsigned char)*str++;
        if (ch < 32 || ch > 127) ch = '?';
        const uint8_t *glyph = FONT8X8[ch - 32];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << col)) {
                    draw_rect(cx + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        cx += 8.0f * scale + 1.0f;
    }
}

static const char *CAT_KEYS[]   = {"trending", "movie", "tv", "kdrama", "anime", "indo"};
static const char *CAT_LABELS[] = {"TRENDING", "MOVIES", "TV SHOWS", "K-DRAMA", "ANIME", "INDONESIA"};
#define NUM_CATS 6

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    FILE *f_reset = fopen("/dev_hdd0/tmp/rafstream_boot.log", "w");
    if (f_reset) {
        fprintf(f_reset, "1. main() started\n");
        fclose(f_reset);
    }

    sysModuleLoad(SYSMODULE_NET);
    sysModuleLoad(SYSMODULE_NETCTL);
    sysModuleLoad(SYSMODULE_JPGDEC);
    sysModuleLoad(SYSMODULE_PNGDEC);
    sysModuleLoad(SYSMODULE_AUDIO);
    boot_log("2. sysmodules loaded");

    tiny3d_Init(1024 * 1024);
    boot_log("3. tiny3d initialized");

    begin_frame_2d(0xFF0B0B0F);
    draw_text(40.0f, 36.0f, 2.2f, 0xFFE50914, "RAFSTREAM PS3 NATIVE");
    draw_text(40.0f, 80.0f, 1.5f, 0xFFFFFFFF, "Menyiapkan jaringan PS3 & decoder...");
    tiny3d_Flip();
    usleep(300000);

    netInitialize();
    netCtlInit();
    boot_log("4. net initialized");

    audioInit();
    ioPadInit(7);
    boot_log("5. audio & pad initialized");

    curl_global_init(CURL_GLOBAL_ALL);
    avformat_network_init();
    boot_log("6. curl & ffmpeg net initialized");

    int item_count = 0;
    int ep_count = 0;
    int cat_idx = 0;
    int page = 1;
    int sel_idx = 0;
    int view_mode = 0; /* 0 = Grid Katalog, 1 = Detail & Episode */
    int sel_season = 1;
    int sel_ep_idx = 0;
    char status_msg[160] = "Menghubungkan ke server Vercel RAFSTREAM...";
    char search_query[64] = "";

    begin_frame_2d(0xFF0B0B0F);
    draw_text(40.0f, 36.0f, 2.2f, 0xFFE50914, "RAFSTREAM PS3 NATIVE");
    draw_text(40.0f, 80.0f, 1.5f, 0xFFFFFFFF, status_msg);
    tiny3d_Flip();

    boot_log("7. fetching catalog...");
    item_count = api_fetch_catalog(CAT_KEYS[cat_idx], search_query, page, items, 8);
    boot_log("8. catalog fetched");

    if (item_count < 0) {
        snprintf(status_msg, sizeof(status_msg), "Gagal terhubung ke server (kode %d). Tekan [Select] untuk coba lagi.", item_count);
        item_count = 0;
    } else if (item_count == 0) {
        snprintf(status_msg, sizeof(status_msg), "Katalog kosong. Tekan [R1] ganti kategori atau [Select] muat ulang.");
    } else {
        snprintf(status_msg, sizeof(status_msg), "D-Pad: Navigasi | [X]: Pilih/Play | [O]: Kembali | [L1/R1]: Kategori");
        for (int i = 0; i < item_count; i++) {
            load_item_poster_texture(&items[i]);
        }
    }
    boot_log("9. entering main UI loop");

    padInfo padinfo;
    padData paddata;
    u32 prev_btn = 0;

    while (1) {
        ioPadGetInfo(&padinfo);
        if (padinfo.status[0]) {
            ioPadGetData(0, &paddata);

            u32 btn = 0;
            if (paddata.BTN_SELECT)   btn |= PAD_BIT_SELECT;
            if (paddata.BTN_START)    btn |= PAD_BIT_START;
            if (paddata.BTN_UP)       btn |= PAD_BIT_UP;
            if (paddata.BTN_RIGHT)    btn |= PAD_BIT_RIGHT;
            if (paddata.BTN_DOWN)     btn |= PAD_BIT_DOWN;
            if (paddata.BTN_LEFT)     btn |= PAD_BIT_LEFT;
            if (paddata.BTN_L2)       btn |= PAD_BIT_L2;
            if (paddata.BTN_R2)       btn |= PAD_BIT_R2;
            if (paddata.BTN_L1)       btn |= PAD_BIT_L1;
            if (paddata.BTN_R1)       btn |= PAD_BIT_R1;
            if (paddata.BTN_TRIANGLE) btn |= PAD_BIT_TRIANGLE;
            if (paddata.BTN_CIRCLE)   btn |= PAD_BIT_CIRCLE;
            if (paddata.BTN_CROSS)    btn |= PAD_BIT_CROSS;
            if (paddata.BTN_SQUARE)   btn |= PAD_BIT_SQUARE;

            u32 pressed = btn & ~prev_btn;
            prev_btn = btn;

            if (view_mode == 0) {
                /* Navigasi Grid 4x2 */
                if ((pressed & PAD_BIT_RIGHT) && sel_idx + 1 < item_count) sel_idx++;
                if ((pressed & PAD_BIT_LEFT)  && sel_idx > 0) sel_idx--;
                if ((pressed & PAD_BIT_DOWN)  && sel_idx + 4 < item_count) sel_idx += 4;
                if ((pressed & PAD_BIT_UP)    && sel_idx - 4 >= 0) sel_idx -= 4;

                /* L1 / R1 Ganti Kategori */
                int reload = 0;
                if (pressed & PAD_BIT_R1) {
                    cat_idx = (cat_idx + 1) % NUM_CATS;
                    page = 1;
                    search_query[0] = '\0';
                    reload = 1;
                }
                if (pressed & PAD_BIT_L1) {
                    cat_idx = (cat_idx - 1 + NUM_CATS) % NUM_CATS;
                    page = 1;
                    search_query[0] = '\0';
                    reload = 1;
                }
                /* R2 / L2 Ganti Halaman */
                if (pressed & PAD_BIT_R2) {
                    page++;
                    reload = 1;
                }
                if ((pressed & PAD_BIT_L2) && page > 1) {
                    page--;
                    reload = 1;
                }
                /* Select: Refresh */
                if (pressed & PAD_BIT_SELECT) {
                    reload = 1;
                }

                if (reload) {
                    strcpy(status_msg, "Memuat katalog...");
                    begin_frame_2d(0xFF0B0B0F);
                    draw_text(40.0f, 240.0f, 1.8f, 0xFFFFFFFF, status_msg);
                    tiny3d_Flip();

                    item_count = api_fetch_catalog(CAT_KEYS[cat_idx], search_query, page, items, 8);
                    if (item_count < 0) {
                        snprintf(status_msg, sizeof(status_msg), "Gagal memuat katalog (kode %d). Tekan [Select] coba lagi.", item_count);
                        item_count = 0;
                    } else {
                        sel_idx = 0;
                        for (int i = 0; i < item_count; i++) load_item_poster_texture(&items[i]);
                        snprintf(status_msg, sizeof(status_msg), "Halaman %d | [L2/R2]: Hal | [L1/R1]: Kategori | [X]: Buka", page);
                    }
                }

                /* Tombol X: Buka Detail Tayangan */
                if ((pressed & PAD_BIT_CROSS) && item_count > 0) {
                    strcpy(status_msg, "Mengambil detail & episode...");
                    begin_frame_2d(0xFF0B0B0F);
                    draw_text(40.0f, 240.0f, 1.8f, 0xFFFFFFFF, status_msg);
                    tiny3d_Flip();

                    api_fetch_detail(&items[sel_idx]);
                    sel_season = 1;
                    sel_ep_idx = 0;
                    ep_count = 0;
                    if (strcmp(items[sel_idx].media_type, "tv") == 0) {
                        ep_count = api_fetch_episodes(items[sel_idx].id, sel_season, episodes, MAX_EPISODES);
                        if (ep_count < 0) ep_count = 0;
                    }
                    view_mode = 1;
                }
            } else if (view_mode == 1) {
                /* Halaman Detail & Episode */
                MediaItem *cur = &items[sel_idx];
                int is_tv = (strcmp(cur->media_type, "tv") == 0);

                if (pressed & PAD_BIT_CIRCLE) {
                    view_mode = 0;
                }
                if (is_tv && ep_count > 0) {
                    if ((pressed & PAD_BIT_DOWN) && sel_ep_idx + 1 < ep_count) sel_ep_idx++;
                    if ((pressed & PAD_BIT_UP)   && sel_ep_idx > 0) sel_ep_idx--;
                    if ((pressed & PAD_BIT_R1)   && sel_season < cur->num_seasons) {
                        sel_season++;
                        sel_ep_idx = 0;
                        ep_count = api_fetch_episodes(cur->id, sel_season, episodes, MAX_EPISODES);
                        if (ep_count < 0) ep_count = 0;
                    }
                    if ((pressed & PAD_BIT_L1) && sel_season > 1) {
                        sel_season--;
                        sel_ep_idx = 0;
                        ep_count = api_fetch_episodes(cur->id, sel_season, episodes, MAX_EPISODES);
                        if (ep_count < 0) ep_count = 0;
                    }
                }

                /* Tombol X: Ekstrak Stream & Putar Video di PS3 */
                if (pressed & PAD_BIT_CROSS) {
                    int ep_num = (is_tv && ep_count > 0) ? episodes[sel_ep_idx].episode_number : 1;
                    begin_frame_2d(0xFF0B0B0F);
                    draw_text(40.0f, 220.0f, 1.8f, 0xFFE50914, "MENGAMBIL STREAM VIDEO...");
                    draw_text(40.0f, 255.0f, 1.3f, 0xFFCCCCCC, "Menghubungi ekstraktor server...");
                    tiny3d_Flip();

                    char stream_url[1024];
                    int s_res = api_fetch_stream_url(cur, sel_season, ep_num, stream_url, sizeof(stream_url));
                    if (s_res == 0) {
                        int p_res = play_stream_video(stream_url, cur->title);
                        if (p_res < 0) {
                            snprintf(status_msg, sizeof(status_msg), "Decoder gagal membuka format video (%d).", p_res);
                        } else {
                            strcpy(status_msg, "Pemutaran selesai.");
                        }
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "Stream belum tersedia untuk judul ini (kode %d).", s_res);
                    }
                }
            }
        }

        /* RENDER UI 720P DI RSX */
        begin_frame_2d(0xFF0B0B0F);

        /* Header Bar */
        draw_rect(0.0f, 0.0f, 848.0f, 44.0f, 0xFF14141A);
        draw_text(28.0f, 14.0f, 1.8f, 0xFFE50914, "RAFSTREAM");

        float tab_x = 195.0f;
        for (int c = 0; c < NUM_CATS; c++) {
            u32 tcol = (c == cat_idx) ? 0xFFFFFFFF : 0xFF888888;
            if (c == cat_idx) draw_rect(tab_x - 4.0f, 38.0f, 78.0f, 3.0f, 0xFFE50914);
            draw_text(tab_x, 16.0f, 1.1f, tcol, CAT_LABELS[c]);
            tab_x += 95.0f;
        }

        if (view_mode == 0) {
            /* Grid 4 Kolom x 2 Baris */
            for (int i = 0; i < item_count; i++) {
                int col = i % 4;
                int row = i / 4;
                float gx = 34.0f + col * 198.0f;
                float gy = 58.0f + row * 206.0f;

                if (i == sel_idx) {
                    draw_rect(gx - 4.0f, gy - 4.0f, 186.0f, 196.0f, 0xFFE50914);
                }
                draw_rect(gx, gy, 178.0f, 188.0f, 0xFF1A1A22);

                if (items[i].tex_offset != 0) {
                    tiny3d_SetTexture(0, items[i].tex_offset, items[i].tex_w, items[i].tex_h,
                                      items[i].tex_w * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);
                    tiny3d_SetPolygon(TINY3D_QUADS);
                    tiny3d_VertexPos(gx + 6.0f, gy + 6.0f, 0.0f);
                    tiny3d_VertexTexture(0.0f, 0.0f);
                    tiny3d_VertexPos(gx + 172.0f, gy + 6.0f, 0.0f);
                    tiny3d_VertexTexture(1.0f, 0.0f);
                    tiny3d_VertexPos(gx + 172.0f, gy + 142.0f, 0.0f);
                    tiny3d_VertexTexture(1.0f, 1.0f);
                    tiny3d_VertexPos(gx + 6.0f, gy + 142.0f, 0.0f);
                    tiny3d_VertexTexture(0.0f, 1.0f);
                    tiny3d_End();
                } else {
                    draw_rect(gx + 6.0f, gy + 6.0f, 166.0f, 136.0f, 0xFF282834);
                }

                char short_title[20];
                strncpy(short_title, items[i].title, 18);
                short_title[18] = '\0';
                draw_text(gx + 8.0f, gy + 150.0f, 1.0f, 0xFFFFFFFF, short_title);

                char meta[32];
                snprintf(meta, sizeof(meta), "* %s | %s", items[i].rating, items[i].year);
                draw_text(gx + 8.0f, gy + 168.0f, 0.95f, 0xFFF5C518, meta);
            }
        } else {
            /* Detail View */
            MediaItem *cur = &items[sel_idx];
            draw_rect(28.0f, 56.0f, 792.0f, 412.0f, 0xFF14141C);
            draw_text(46.0f, 72.0f, 1.8f, 0xFFFFFFFF, cur->title);

            char dmeta[64];
            snprintf(dmeta, sizeof(dmeta), "Rating: %s/10  |  Tahun: %s  |  Tipe: %s",
                     cur->rating, cur->year, cur->media_type);
            draw_text(46.0f, 102.0f, 1.15f, 0xFFF5C518, dmeta);

            char line1[85], line2[85];
            strncpy(line1, cur->overview, 82);
            line1[82] = '\0';
            draw_text(46.0f, 130.0f, 1.0f, 0xFFCCCCCC, line1);
            if (strlen(cur->overview) > 82) {
                strncpy(line2, cur->overview + 82, 82);
                line2[82] = '\0';
                draw_text(46.0f, 146.0f, 1.0f, 0xFFCCCCCC, line2);
            }

            draw_rect(46.0f, 174.0f, 230.0f, 34.0f, 0xFFE50914);
            draw_text(60.0f, 185.0f, 1.25f, 0xFFFFFFFF, "[X] PUTAR VIDEO SEKARANG");

            if (strcmp(cur->media_type, "tv") == 0) {
                char s_hdr[64];
                snprintf(s_hdr, sizeof(s_hdr), "Season %d / %d (Tekan L1/R1 ganti Season):", sel_season, cur->num_seasons);
                draw_text(46.0f, 224.0f, 1.1f, 0xFFFFFFFF, s_hdr);

                int start_ep = (sel_ep_idx / 8) * 8;
                for (int e = start_ep; e < ep_count && e < start_ep + 8; e++) {
                    float ey = 246.0f + (e - start_ep) * 24.0f;
                    if (e == sel_ep_idx) draw_rect(44.0f, ey - 2.0f, 740.0f, 20.0f, 0xFF331115);
                    char ep_line[96];
                    snprintf(ep_line, sizeof(ep_line), "%s E%d: %s", (e == sel_ep_idx ? ">" : " "),
                             episodes[e].episode_number, episodes[e].name);
                    draw_text(50.0f, ey + 2.0f, 1.0f, (e == sel_ep_idx ? 0xFFFFFFFF : 0xFFAAAAAA), ep_line);
                }
            }
        }

        /* Footer Status Bar */
        draw_rect(0.0f, 478.0f, 848.0f, 34.0f, 0xFF101016);
        draw_text(28.0f, 488.0f, 1.0f, 0xFFCCCCCC, status_msg);

        tiny3d_Flip();
        usleep(16000);
    }

    avformat_network_deinit();
    curl_global_cleanup();
    ioPadEnd();
    netCtlTerm();
    netDeinitialize();
    return 0;
}

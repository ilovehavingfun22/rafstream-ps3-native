#ifndef RAFSTREAM_H
#define RAFSTREAM_H

#include <stdint.h>
#include <stddef.h>

#define VERCEL_API_BASE "https://rafstream-puce.vercel.app/api/ps3"
#define TMDB_IMG_BASE   "https://image.tmdb.org/t/p/w300"

#define MAX_ITEMS       16
#define MAX_EPISODES    50

typedef struct {
    int id;
    char title[128];
    char media_type[16];     /* "movie" atau "tv" */
    char year[8];
    char rating[8];
    char poster_path[128];
    char overview[512];
    int num_seasons;

    /* Texture poster di memori RSX (0 jika belum dimuat) */
    uint32_t tex_offset;
    int tex_w;
    int tex_h;
    int tex_loaded;
} MediaItem;

typedef struct {
    int episode_number;
    char name[128];
} EpisodeItem;

/* net_api.c */
char *http_get_string(const char *url);
int api_fetch_catalog(const char *category, const char *query, int page, MediaItem *out_items, int max_items);
int api_fetch_detail(MediaItem *item);
int api_fetch_episodes(int tv_id, int season, EpisodeItem *out_eps, int max_eps);
int api_fetch_stream_url(const MediaItem *item, int season, int episode, char *out_url, size_t out_url_size);
void load_item_poster_texture(MediaItem *item);

/* player.c */
int play_stream_video(const char *stream_url, const char *video_title);

#endif

#include "rafstream.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <tiny3d.h>
#include <jpgdec/jpgdec.h>

struct MemBuf {
    char *data;
    size_t size;
};

static size_t write_mem_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct MemBuf *mem = (struct MemBuf *)userp;
    char *ptr = (char *)realloc(mem->data, mem->size + total + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, total);
    mem->size += total;
    mem->data[mem->size] = 0;
    return total;
}

static void url_encode(const char *src, char *dst, size_t dst_len) {
    const char *hex = "0123456789ABCDEF";
    size_t pos = 0;
    while (*src && pos + 4 < dst_len) {
        unsigned char c = (unsigned char)*src++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[pos++] = (char)c;
        } else {
            dst[pos++] = '%';
            dst[pos++] = hex[(c >> 4) & 0x0F];
            dst[pos++] = hex[c & 0x0F];
        }
    }
    dst[pos] = '\0';
}

char *http_get_string(const char *url) {
    CURL *curl;
    CURLcode res;
    struct MemBuf chunk;
    chunk.data = (char *)malloc(1);
    chunk.size = 0;
    if (!chunk.data) return NULL;
    chunk.data[0] = '\0';

    curl = curl_easy_init();
    if (!curl) {
        free(chunk.data);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RAFSTREAM-PS3-Native/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || chunk.size == 0) {
        free(chunk.data);
        return NULL;
    }
    return chunk.data;
}

int api_fetch_catalog(const char *category, const char *query, int page, MediaItem *out_items, int max_items) {
    char url[512];
    char enc_q[256];

    if (query && strlen(query) > 0) {
        url_encode(query, enc_q, sizeof(enc_q));
        snprintf(url, sizeof(url), "%s?action=search&q=%s&page=%d", VERCEL_API_BASE, enc_q, page);
    } else if (strcmp(category, "trending") == 0) {
        snprintf(url, sizeof(url), "%s?action=trending&page=%d", VERCEL_API_BASE, page);
    } else {
        snprintf(url, sizeof(url), "%s?action=discover&cat=%s&page=%d", VERCEL_API_BASE, category, page);
    }

    char *raw = http_get_string(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -2;

    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    int count = 0;
    int arr_size = cJSON_GetArraySize(results);

    for (int i = 0; i < arr_size && count < max_items; i++) {
        cJSON *obj = cJSON_GetArrayItem(results, i);
        cJSON *id_j = cJSON_GetObjectItemCaseSensitive(obj, "id");
        cJSON *title_j = cJSON_GetObjectItemCaseSensitive(obj, "title");
        cJSON *name_j = cJSON_GetObjectItemCaseSensitive(obj, "name");
        cJSON *mtype_j = cJSON_GetObjectItemCaseSensitive(obj, "media_type");
        cJSON *rdate_j = cJSON_GetObjectItemCaseSensitive(obj, "release_date");
        cJSON *fdate_j = cJSON_GetObjectItemCaseSensitive(obj, "first_air_date");
        cJSON *vote_j = cJSON_GetObjectItemCaseSensitive(obj, "vote_average");
        cJSON *poster_j = cJSON_GetObjectItemCaseSensitive(obj, "poster_path");
        cJSON *over_j = cJSON_GetObjectItemCaseSensitive(obj, "overview");

        if (!id_j || id_j->valueint <= 0) continue;

        MediaItem *m = &out_items[count];
        memset(m, 0, sizeof(MediaItem));
        m->id = id_j->valueint;

        const char *t_str = (title_j && title_j->valuestring) ? title_j->valuestring :
                            ((name_j && name_j->valuestring) ? name_j->valuestring : "Tanpa Judul");
        strncpy(m->title, t_str, sizeof(m->title) - 1);

        if (mtype_j && mtype_j->valuestring) {
            strncpy(m->media_type, mtype_j->valuestring, sizeof(m->media_type) - 1);
        } else if (fdate_j && fdate_j->valuestring && (!rdate_j || !rdate_j->valuestring)) {
            strcpy(m->media_type, "tv");
        } else if (strcmp(category, "tv") == 0 || strcmp(category, "kdrama") == 0 || strcmp(category, "anime") == 0) {
            strcpy(m->media_type, "tv");
        } else {
            strcpy(m->media_type, "movie");
        }

        const char *d_str = (rdate_j && rdate_j->valuestring && strlen(rdate_j->valuestring) >= 4) ? rdate_j->valuestring :
                            ((fdate_j && fdate_j->valuestring && strlen(fdate_j->valuestring) >= 4) ? fdate_j->valuestring : "2025");
        strncpy(m->year, d_str, 4);
        m->year[4] = '\0';

        double v = vote_j ? vote_j->valuedouble : 8.0;
        snprintf(m->rating, sizeof(m->rating), "%.1f", v);

        if (poster_j && poster_j->valuestring) {
            strncpy(m->poster_path, poster_j->valuestring, sizeof(m->poster_path) - 1);
        }
        if (over_j && over_j->valuestring) {
            strncpy(m->overview, over_j->valuestring, sizeof(m->overview) - 1);
        } else {
            strcpy(m->overview, "Tonton langsung di konsol PS3 Anda.");
        }
        m->num_seasons = 1;
        count++;
    }

    cJSON_Delete(root);
    return count;
}

int api_fetch_detail(MediaItem *item) {
    char url[512];
    snprintf(url, sizeof(url), "%s?action=detail&type=%s&id=%d", VERCEL_API_BASE, item->media_type, item->id);
    char *raw = http_get_string(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -2;

    cJSON *seasons_j = cJSON_GetObjectItemCaseSensitive(root, "number_of_seasons");
    if (seasons_j && seasons_j->valueint > 0) {
        item->num_seasons = seasons_j->valueint;
        if (item->num_seasons > 20) item->num_seasons = 20;
    }

    cJSON *over_j = cJSON_GetObjectItemCaseSensitive(root, "overview");
    if (over_j && over_j->valuestring && strlen(over_j->valuestring) > 5) {
        strncpy(item->overview, over_j->valuestring, sizeof(item->overview) - 1);
    }

    cJSON_Delete(root);
    return 0;
}

int api_fetch_episodes(int tv_id, int season, EpisodeItem *out_eps, int max_eps) {
    char url[512];
    snprintf(url, sizeof(url), "%s?action=season&id=%d&season=%d", VERCEL_API_BASE, tv_id, season);
    char *raw = http_get_string(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -2;

    cJSON *eps = cJSON_GetObjectItemCaseSensitive(root, "episodes");
    int count = 0;
    int arr_size = cJSON_GetArraySize(eps);

    for (int i = 0; i < arr_size && count < max_eps; i++) {
        cJSON *ep = cJSON_GetArrayItem(eps, i);
        cJSON *num_j = cJSON_GetObjectItemCaseSensitive(ep, "episode_number");
        cJSON *name_j = cJSON_GetObjectItemCaseSensitive(ep, "name");
        out_eps[count].episode_number = num_j ? num_j->valueint : (i + 1);
        if (name_j && name_j->valuestring) {
            strncpy(out_eps[count].name, name_j->valuestring, sizeof(out_eps[count].name) - 1);
        } else {
            snprintf(out_eps[count].name, sizeof(out_eps[count].name), "Episode %d", out_eps[count].episode_number);
        }
        count++;
    }

    cJSON_Delete(root);
    return count;
}

int api_fetch_stream_url(const MediaItem *item, int season, int episode, char *out_url, size_t out_url_size) {
    char url[768];
    char enc_title[256];
    url_encode(item->title, enc_title, sizeof(enc_title));

    snprintf(url, sizeof(url), "%s?action=stream&id=%d&type=%s&title=%s&year=%s&season=%d&episode=%d",
             VERCEL_API_BASE, item->id, item->media_type, enc_title, item->year, season, episode);

    char *raw = http_get_string(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -2;

    cJSON *stream_j = cJSON_GetObjectItemCaseSensitive(root, "streamUrl");
    if (stream_j && stream_j->valuestring && strlen(stream_j->valuestring) > 10) {
        strncpy(out_url, stream_j->valuestring, out_url_size - 1);
        out_url[out_url_size - 1] = '\0';
        cJSON_Delete(root);
        return 0;
    }

    cJSON_Delete(root);
    return -3;
}

void load_item_poster_texture(MediaItem *item) {
    if (!item || item->tex_loaded || strlen(item->poster_path) < 4) return;
    item->tex_loaded = 1;

    char url[512];
    snprintf(url, sizeof(url), "%s%s", TMDB_IMG_BASE, item->poster_path);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    struct MemBuf img_buf;
    img_buf.data = (char *)malloc(1);
    img_buf.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&img_buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && img_buf.size > 128) {
        jpgData jpg;
        if (jpgLoadFromBuffer(img_buf.data, (u32)img_buf.size, &jpg) == 0 && jpg.bmp_out) {
            u32 *tex_mem = (u32 *)tiny3d_AllocTexture(jpg.width * jpg.height * 4);
            if (tex_mem) {
                memcpy(tex_mem, jpg.bmp_out, jpg.width * jpg.height * 4);
                item->tex_offset = tiny3d_TextureOffset(tex_mem);
                item->tex_w = jpg.width;
                item->tex_h = jpg.height;
            }
            free(jpg.bmp_out);
        }
    }
    free(img_buf.data);
}

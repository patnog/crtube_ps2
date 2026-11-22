#ifndef CRTUBE_PS2_YOUTUBE_SEARCH_H
#define CRTUBE_PS2_YOUTUBE_SEARCH_H

#include <stddef.h>

#define CRTUBE_YOUTUBE_MAX_HOME_RESULTS 4
#define CRTUBE_YOUTUBE_VIDEO_ID_SIZE 32
#define CRTUBE_YOUTUBE_TITLE_SIZE 192
#define CRTUBE_YOUTUBE_CHANNEL_SIZE 128
#define CRTUBE_YOUTUBE_THUMBNAIL_URL_SIZE 512

typedef struct crtube_youtube_video {
    char video_id[CRTUBE_YOUTUBE_VIDEO_ID_SIZE];
    char title[CRTUBE_YOUTUBE_TITLE_SIZE];
    char channel[CRTUBE_YOUTUBE_CHANNEL_SIZE];
    char thumbnail_url[CRTUBE_YOUTUBE_THUMBNAIL_URL_SIZE];
} crtube_youtube_video_t;

/* Parses YouTube Innertube search JSON and extracts up to capacity videos.
   Returns the number of results, or a negative value for invalid JSON/args. */
int crtube_youtube_parse_search_results(const char *json, size_t json_size,
                                        crtube_youtube_video_t *videos,
                                        size_t capacity);

/* Performs an Innertube search and extracts up to capacity video entries. */
int crtube_youtube_search(const char *query, crtube_youtube_video_t *videos,
                          size_t capacity);

/* Loads the four home-screen entries and keeps them cached for the UI. */
int crtube_youtube_load_home_results(const char *query);
const crtube_youtube_video_t *crtube_youtube_get_home_results(size_t *count);

#endif

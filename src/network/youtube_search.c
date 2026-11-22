#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "youtube_search.h"
#include "http.h"

typedef struct json_parser {
    const char *data;
    size_t size;
    size_t pos;
    int error;
} json_parser_t;

typedef struct json_span {
    size_t start;
    size_t end;
} json_span_t;

static void json_ws(json_parser_t *p)
{
    while (p->pos < p->size) {
        char c = p->data[p->pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        p->pos++;
    }
}

static int json_hex4(const char *s, unsigned *value)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = (v << 4) | d;
    }
    *value = v;
    return 0;
}

static int json_string_end(json_parser_t *p, json_span_t *span)
{
    size_t start;
    if (p->pos >= p->size || p->data[p->pos] != '"')
        return -1;
    start = p->pos++;
    while (p->pos < p->size) {
        unsigned char c = (unsigned char)p->data[p->pos++];
        if (c == '"') {
            if (span) { span->start = start; span->end = p->pos; }
            return 0;
        }
        if (c < 0x20) return -1;
        if (c == '\\') {
            unsigned char e;
            if (p->pos >= p->size) return -1;
            e = (unsigned char)p->data[p->pos++];
            if (e == 'u') {
                unsigned code;
                if (p->size - p->pos < 4 || json_hex4(p->data + p->pos, &code) != 0)
                    return -1;
                p->pos += 4;
            } else if (e != '"' && e != '\\' && e != '/' && e != 'b' &&
                       e != 'f' && e != 'n' && e != 'r' && e != 't') {
                return -1;
            }
        }
    }
    return -1;
}

static int json_value_end(json_parser_t *p, json_span_t *span, unsigned depth)
{
    size_t start;
    char c;
    json_ws(p);
    if (depth > 64 || p->pos >= p->size) return -1;
    start = p->pos;
    c = p->data[p->pos];
    if (c == '"') {
        if (json_string_end(p, NULL) != 0) return -1;
    } else if (c == '{') {
        p->pos++;
        json_ws(p);
        if (p->pos < p->size && p->data[p->pos] == '}') p->pos++;
        else {
            for (;;) {
                if (json_string_end(p, NULL) != 0) return -1;
                json_ws(p);
                if (p->pos >= p->size || p->data[p->pos++] != ':') return -1;
                if (json_value_end(p, NULL, depth + 1) != 0) return -1;
                json_ws(p);
                if (p->pos >= p->size) return -1;
                if (p->data[p->pos] == '}') { p->pos++; break; }
                if (p->data[p->pos++] != ',') return -1;
                json_ws(p);
            }
        }
    } else if (c == '[') {
        p->pos++;
        json_ws(p);
        if (p->pos < p->size && p->data[p->pos] == ']') p->pos++;
        else {
            for (;;) {
                if (json_value_end(p, NULL, depth + 1) != 0) return -1;
                json_ws(p);
                if (p->pos >= p->size) return -1;
                if (p->data[p->pos] == ']') { p->pos++; break; }
                if (p->data[p->pos++] != ',') return -1;
                json_ws(p);
            }
        }
    } else {
        while (p->pos < p->size) {
            c = p->data[p->pos];
            if (c == ',' || c == ']' || c == '}' || c == ' ' || c == '\t' || c == '\r' || c == '\n')
                break;
            p->pos++;
        }
        if (p->pos == start) return -1;
        if (!((p->pos - start == 4 && memcmp(p->data + start, "true", 4) == 0) ||
              (p->pos - start == 5 && memcmp(p->data + start, "false", 5) == 0) ||
              (p->pos - start == 4 && memcmp(p->data + start, "null", 4) == 0) ||
              (p->data[start] == '-' || (p->data[start] >= '0' && p->data[start] <= '9'))))
            return -1;
    }
    if (span) { span->start = start; span->end = p->pos; }
    return 0;
}

static int json_string_decode(const char *json, size_t size, char *out, size_t out_size)
{
    size_t i = 1, used = 0;
    if (!json || size < 2 || json[0] != '"' || json[size - 1] != '"' || !out || out_size == 0)
        return -1;
    while (i < size - 1) {
        unsigned char c = (unsigned char)json[i++];
        if (c == '\\') {
            unsigned code;
            if (i >= size - 1) return -1;
            c = (unsigned char)json[i++];
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u':
                if (size - 1 - i < 4 || json_hex4(json + i, &code) != 0) return -1;
                i += 4;
                if (code >= 0xd800 && code <= 0xdbff && size - 1 - i >= 6 &&
                    json[i] == '\\' && json[i + 1] == 'u') {
                    unsigned low;
                    if (json_hex4(json + i + 2, &low) == 0 && low >= 0xdc00 && low <= 0xdfff) {
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                        i += 6;
                    }
                }
                if (code < 0x80) c = (unsigned char)code;
                else {
                    unsigned char bytes[4]; size_t n;
                    if (code < 0x800) { bytes[0] = 0xc0 | (code >> 6); bytes[1] = 0x80 | (code & 63); n = 2; }
                    else if (code < 0x10000) { bytes[0] = 0xe0 | (code >> 12); bytes[1] = 0x80 | ((code >> 6) & 63); bytes[2] = 0x80 | (code & 63); n = 3; }
                    else { bytes[0] = 0xf0 | (code >> 18); bytes[1] = 0x80 | ((code >> 12) & 63); bytes[2] = 0x80 | ((code >> 6) & 63); bytes[3] = 0x80 | (code & 63); n = 4; }
                    if (used + n >= out_size) break;
                    memcpy(out + used, bytes, n); used += n; continue;
                }
                break;
            default: return -1;
            }
        }
        if (used + 1 >= out_size) break;
        out[used++] = (char)c;
    }
    out[used] = '\0';
    return (int)used;
}

static int json_key_is(const char *json, json_span_t span, const char *key)
{
    char decoded[96];
    size_t n = span.end - span.start;
    if (n >= sizeof(decoded)) return 0;
    if (json_string_decode(json + span.start, n, decoded, sizeof(decoded)) < 0) return 0;
    return strcmp(decoded, key) == 0;
}

static int json_object_get(const char *json, size_t size, json_span_t object,
                           const char *key, json_span_t *value)
{
    json_parser_t p;
    if (object.start >= size || json[object.start] != '{') return 0;
    p.data = json; p.size = object.end; p.pos = object.start + 1; p.error = 0;
    while (p.pos < p.size) {
        json_span_t k, v;
        json_ws(&p);
        if (p.pos < p.size && json[p.pos] == '}') break;
        if (json_string_end(&p, &k) != 0) return -1;
        json_ws(&p);
        if (p.pos >= p.size || json[p.pos++] != ':') return -1;
        if (json_value_end(&p, &v, 0) != 0) return -1;
        if (json_key_is(json, k, key)) { *value = v; return 1; }
        json_ws(&p);
        if (p.pos < p.size && json[p.pos] == ',') p.pos++;
        else break;
    }
    return 0;
}

static int json_array_first(const char *json, size_t size, json_span_t array, json_span_t *first)
{
    json_parser_t p;
    if (array.start >= size || json[array.start] != '[') return 0;
    p.data = json; p.size = array.end; p.pos = array.start + 1; p.error = 0;
    json_ws(&p);
    if (p.pos >= p.size || json[p.pos] == ']') return 0;
    return json_value_end(&p, first, 0) == 0 ? 1 : -1;
}

static void json_copy_value_string(const char *json, size_t size, json_span_t value,
                                   char *out, size_t out_size)
{
    if (value.end <= size && value.start < value.end && json[value.start] == '"')
        (void)json_string_decode(json + value.start, value.end - value.start, out, out_size);
}

static void json_extract_text(const char *json, size_t size, json_span_t text_object,
                              char *out, size_t out_size)
{
    json_span_t runs, item, text;
    size_t used = 0;
    int found;
    out[0] = '\0';
    found = json_object_get(json, size, text_object, "simpleText", &text);
    if (found == 1) { json_copy_value_string(json, size, text, out, out_size); if (out[0]) return; }
    found = json_object_get(json, size, text_object, "runs", &runs);
    if (found != 1 || json_array_first(json, size, runs, &item) != 1) return;
    /* Concatenate all run text fragments, preserving their original spacing. */
    {
        json_parser_t p; json_span_t current;
        p.data = json; p.size = runs.end; p.pos = runs.start + 1; p.error = 0;
        for (;;) {
            int got;
            json_ws(&p);
            if (p.pos >= p.size || json[p.pos] == ']') break;
            if (json_value_end(&p, &current, 0) != 0) break;
            if (json_object_get(json, size, current, "text", &text) == 1 &&
                text.start < text.end && json[text.start] == '"' && used + 1 < out_size) {
                char fragment[256];
                int n = json_string_decode(json + text.start, text.end - text.start, fragment, sizeof(fragment));
                if (n > 0) {
                    size_t copy = (size_t)n;
                    if (copy > out_size - used - 1) copy = out_size - used - 1;
                    memcpy(out + used, fragment, copy); used += copy; out[used] = '\0';
                }
            }
            json_ws(&p);
            if (p.pos >= p.size || json[p.pos] != ',') break;
            p.pos++;
            got = 1; (void)got;
        }
    }
}

static int json_parse_video(const char *json, size_t size, json_span_t renderer,
                            crtube_youtube_video_t *video)
{
    json_span_t value, title, owner = {0, 0}, thumbnail, thumbs, url = {0, 0};
    int rc;
    memset(video, 0, sizeof(*video));
    rc = json_object_get(json, size, renderer, "videoId", &value);
    if (rc != 1) return 0;
    json_copy_value_string(json, size, value, video->video_id, sizeof(video->video_id));
    if (!video->video_id[0]) return 0;
    if (json_object_get(json, size, renderer, "title", &title) == 1)
        json_extract_text(json, size, title, video->title, sizeof(video->title));
    if (json_object_get(json, size, renderer, "ownerText", &owner) != 1)
        (void)json_object_get(json, size, renderer, "shortBylineText", &owner);
    if (owner.end > owner.start)
        json_extract_text(json, size, owner, video->channel, sizeof(video->channel));
    if (json_object_get(json, size, renderer, "thumbnail", &thumbnail) == 1 &&
        json_object_get(json, size, thumbnail, "thumbnails", &thumbs) == 1 &&
        thumbs.start < size && json[thumbs.start] == '[') {
        json_parser_t p; json_span_t current, candidate;
        p.data = json; p.size = thumbs.end; p.pos = thumbs.start + 1; p.error = 0;
        while (p.pos < p.size) {
            json_ws(&p);
            if (json[p.pos] == ']') break;
            if (json_value_end(&p, &current, 0) != 0) break;
            if (json_object_get(json, size, current, "url", &candidate) == 1 &&
                candidate.start < candidate.end && json[candidate.start] == '"')
                url = candidate;
            json_ws(&p);
            if (p.pos < p.size && json[p.pos] == ',') p.pos++; else break;
        }
        if (url.end > url.start)
            json_copy_value_string(json, size, url, video->thumbnail_url, sizeof(video->thumbnail_url));
    }
    return 1;
}

static void json_find_videos(const char *json, size_t size, json_span_t value,
                             crtube_youtube_video_t *videos, size_t capacity,
                             size_t *count, unsigned depth)
{
    json_parser_t p;
    if (*count >= capacity || depth > 64 || value.start >= value.end || value.end > size) return;
    if (json[value.start] == '{') {
        p.data = json; p.size = value.end; p.pos = value.start + 1; p.error = 0;
        while (p.pos < p.size && *count < capacity) {
            json_span_t key, child;
            json_ws(&p);
            if (p.pos >= p.size || json[p.pos] == '}') break;
            if (json_string_end(&p, &key) != 0) return;
            json_ws(&p);
            if (p.pos >= p.size || json[p.pos++] != ':') return;
            if (json_value_end(&p, &child, 0) != 0) return;
            if (json_key_is(json, key, "videoRenderer") && json[child.start] == '{') {
                crtube_youtube_video_t candidate;
                if (json_parse_video(json, size, child, &candidate)) {
                    size_t i; int duplicate = 0;
                    for (i = 0; i < *count; i++)
                        if (strcmp(videos[i].video_id, candidate.video_id) == 0) duplicate = 1;
                    if (!duplicate) videos[(*count)++] = candidate;
                }
            } else if (json[child.start] == '{' || json[child.start] == '[') {
                json_find_videos(json, size, child, videos, capacity, count, depth + 1);
            }
            json_ws(&p);
            if (p.pos < p.size && json[p.pos] == ',') p.pos++;
            else break;
        }
    } else if (json[value.start] == '[') {
        p.data = json; p.size = value.end; p.pos = value.start + 1; p.error = 0;
        while (p.pos < p.size && *count < capacity) {
            json_span_t child;
            json_ws(&p);
            if (p.pos >= p.size || json[p.pos] == ']') break;
            if (json_value_end(&p, &child, 0) != 0) return;
            if (json[child.start] == '{' || json[child.start] == '[')
                json_find_videos(json, size, child, videos, capacity, count, depth + 1);
            json_ws(&p);
            if (p.pos < p.size && json[p.pos] == ',') p.pos++;
            else break;
        }
    }
}

static crtube_youtube_video_t g_home_videos[CRTUBE_YOUTUBE_MAX_HOME_RESULTS];
static size_t g_home_video_count;
static int g_home_videos_valid;

static int append_json_escaped(char *out, size_t capacity, size_t *used,
                               const char *input)
{
    const unsigned char *p = (const unsigned char *)input;
    while (*p) {
        const char *escape = NULL;
        char encoded[7];
        size_t n;
        switch (*p) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (*p < 0x20) {
                (void)snprintf(encoded, sizeof(encoded), "\\u%04x", *p);
                escape = encoded;
            }
            break;
        }
        if (escape) {
            n = strlen(escape);
            if (*used + n >= capacity) return -1;
            memcpy(out + *used, escape, n);
            *used += n;
        } else {
            if (*used + 1 >= capacity) return -1;
            out[(*used)++] = (char)*p;
        }
        p++;
    }
    out[*used] = '\0';
    return 0;
}

int crtube_youtube_search(const char *query, crtube_youtube_video_t *videos,
                          size_t capacity)
{
    char request_body[1024];
    char escaped_query[512];
    char *response = NULL;
    size_t response_size = 0, used = 0;
    char *body;
    size_t body_size;
    int result;
    if (!query || !videos || capacity == 0 || capacity > 32) return -1;
    escaped_query[0] = '\0';
    if (append_json_escaped(escaped_query, sizeof(escaped_query), &used, query) != 0)
        return -1;
    if (snprintf(request_body, sizeof(request_body),
        "{\"context\":{\"client\":{\"clientName\":\"WEB\","
        "\"clientVersion\":\"2.20261002.01.00\",\"hl\":\"pt-BR\",\"gl\":\"BR\"}},"
        "\"query\":\"%s\"}", escaped_query) >= (int)sizeof(request_body))
        return -1;
    result = crtube_http_post_url_alloc(
        "https://www.youtube.com/youtubei/v1/search?prettyPrint=false",
        "application/json", request_body, &response, &response_size);
    if (result != CRTUBE_HTTP_OK || response == NULL) {
        free(response);
        return result != CRTUBE_HTTP_OK ? result : -2;
    }
    if (crtube_http_last_status() != 200) { free(response); return -3; }
    body = strstr(response, "\r\n\r\n");
    if (body == NULL) { free(response); return -4; }
    body += 4;
    if ((size_t)(body - response) > response_size) { free(response); return -4; }
    body_size = response_size - (size_t)(body - response);
    result = crtube_youtube_parse_search_results(body, body_size, videos, capacity);
    free(response);
    return result;
}

int crtube_youtube_load_home_results(const char *query)
{
    int count;
    memset(g_home_videos, 0, sizeof(g_home_videos));
    g_home_video_count = 0;
    g_home_videos_valid = 0;
    count = crtube_youtube_search(query, g_home_videos,
                                  CRTUBE_YOUTUBE_MAX_HOME_RESULTS);
    if (count < 0) return count;
    g_home_video_count = (size_t)count;
    g_home_videos_valid = 1;
    return count;
}

const crtube_youtube_video_t *crtube_youtube_get_home_results(size_t *count)
{
    if (count) *count = g_home_videos_valid ? g_home_video_count : 0;
    return g_home_videos_valid ? g_home_videos : NULL;
}

int crtube_youtube_parse_search_results(const char *json, size_t json_size,
                                        crtube_youtube_video_t *videos,
                                        size_t capacity)
{
    json_parser_t p;
    json_span_t root;
    size_t count = 0;
    if (!json || !videos || capacity == 0) return -1;
    p.data = json; p.size = json_size; p.pos = 0; p.error = 0;
    if (json_value_end(&p, &root, 0) != 0) return -2;
    json_ws(&p);
    if (p.pos != json_size || json[root.start] != '{') return -2;
    json_find_videos(json, json_size, root, videos, capacity, &count, 0);
    return (int)count;
}

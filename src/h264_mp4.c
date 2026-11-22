#include "h264_mp4.h"

#include <string.h>

typedef struct box_view {
    const uint8_t *p;
    size_t size;
    size_t header;
    uint32_t type;
} box_view_t;

#define BOX(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static int next_box(const uint8_t *data, size_t size, size_t *offset,
                    box_view_t *box)
{
    uint64_t length;
    size_t header = 8;
    if (!data || !offset || !box || *offset > size || size - *offset < 8)
        return -1;
    length = be32(data + *offset);
    box->type = be32(data + *offset + 4);
    if (length == 1) {
        if (size - *offset < 16) return -1;
        length = be64(data + *offset + 8);
        header = 16;
    } else if (length == 0) {
        length = size - *offset;
    }
    if (length < header || length > size - *offset || length > SIZE_MAX)
        return -1;
    box->p = data + *offset + header;
    box->size = (size_t)length - header;
    box->header = header;
    *offset += (size_t)length;
    return 0;
}

static int find_child(const uint8_t *data, size_t size, uint32_t type,
                      box_view_t *found)
{
    size_t off = 0;
    box_view_t b;
    while (off < size) {
        if (next_box(data, size, &off, &b) < 0) return -1;
        if (b.type == type) {
            *found = b;
            return 0;
        }
    }
    return -1;
}

static int parse_avcc(crtube_mp4_t *m, const box_view_t *b)
{
    const uint8_t *p = b->p;
    size_t n = b->size, off;
    unsigned count, i;
    if (n < 7 || p[0] != 1) return -1;
    m->nal_length_size = (p[4] & 3u) + 1u;
    if (m->nal_length_size == 3u) return -2;
    count = p[5] & 31u;
    off = 6;
    for (i = 0; i < count; ++i) {
        unsigned len;
        if (off + 2 > n) return -1;
        len = ((unsigned)p[off] << 8) | p[off + 1];
        off += 2;
        if (!len || len > n - off) return -1;
        off += len;
    }
    if (off >= n) return -1;
    count = p[off++];
    for (i = 0; i < count; ++i) {
        unsigned len;
        if (off + 2 > n) return -1;
        len = ((unsigned)p[off] << 8) | p[off + 1];
        off += 2;
        if (!len || len > n - off) return -1;
        off += len;
    }
    m->avcc = p;
    m->avcc_size = n;
    return 0;
}

static int parse_stsd(crtube_mp4_t *m, const box_view_t *stsd)
{
    const uint8_t *p = stsd->p;
    uint32_t count;
    size_t off = 8;
    box_view_t entry, child;
    if (stsd->size < 8) return -1;
    count = be32(p + 4);
    while (count--) {
        size_t entry_start = off;
        if (next_box(p, stsd->size, &off, &entry) < 0) return -1;
        if (entry.type != BOX('a','v','c','1') &&
            entry.type != BOX('a','v','c','3')) continue;
        if (entry.size < 78) return -1;
        m->width = ((uint32_t)entry.p[24] << 8) | entry.p[25];
        m->height = ((uint32_t)entry.p[26] << 8) | entry.p[27];
        if (find_child(entry.p + 78, entry.size - 78,
                       BOX('a','v','c','C'), &child) < 0) return -1;
        (void)entry_start;
        return parse_avcc(m, &child);
    }
    return -1;
}

static int parse_stbl(crtube_mp4_t *m, const box_view_t *stbl)
{
    box_view_t b;
    if (find_child(stbl->p, stbl->size, BOX('s','t','s','d'), &b) < 0 ||
        parse_stsd(m, &b) < 0) return -1;
    if (find_child(stbl->p, stbl->size, BOX('s','t','s','z'), &b) < 0 ||
        b.size < 12 || be32(b.p) != 0) return -1;
    m->default_sample_size = be32(b.p + 4);
    m->sample_count = be32(b.p + 8);
    if (!m->sample_count || m->sample_count > 1000000u) return -1;
    m->stsz = b.p + 12;
    if (!m->default_sample_size &&
        (uint64_t)m->sample_count * 4u > b.size - 12u) return -1;

    if (find_child(stbl->p, stbl->size, BOX('s','t','s','c'), &b) < 0 ||
        b.size < 8 || be32(b.p) != 0) return -1;
    m->stsc_count = be32(b.p + 4);
    m->stsc = b.p + 8;
    if (!m->stsc_count || (uint64_t)m->stsc_count * 12u > b.size - 8u)
        return -1;

    if (find_child(stbl->p, stbl->size, BOX('s','t','c','o'), &b) == 0) {
        if (b.size < 8 || be32(b.p) != 0) return -1;
        m->chunk_count = be32(b.p + 4);
        m->chunk_offsets = b.p + 8;
        m->chunk_offset_width = 4;
        if ((uint64_t)m->chunk_count * 4u > b.size - 8u) return -1;
    } else if (find_child(stbl->p, stbl->size, BOX('c','o','6','4'), &b) == 0) {
        if (b.size < 8 || be32(b.p) != 0) return -1;
        m->chunk_count = be32(b.p + 4);
        m->chunk_offsets = b.p + 8;
        m->chunk_offset_width = 8;
        if ((uint64_t)m->chunk_count * 8u > b.size - 8u) return -1;
    } else return -1;
    if (!m->chunk_count) return -1;
    return 0;
}

static int parse_trak(crtube_mp4_t *m, const box_view_t *trak)
{
    box_view_t mdia, hdlr, minf, stbl;
    if (find_child(trak->p, trak->size, BOX('m','d','i','a'), &mdia) < 0 ||
        find_child(mdia.p, mdia.size, BOX('h','d','l','r'), &hdlr) < 0 ||
        hdlr.size < 12) return -1;
    if (be32(hdlr.p + 8) != BOX('v','i','d','e')) return 1;
    if (find_child(mdia.p, mdia.size, BOX('m','i','n','f'), &minf) < 0 ||
        find_child(minf.p, minf.size, BOX('s','t','b','l'), &stbl) < 0)
        return -1;
    return parse_stbl(m, &stbl);
}

int crtube_mp4_open_memory(crtube_mp4_t *mp4, const uint8_t *data, size_t size)
{
    box_view_t top, moov, trak;
    size_t off = 0, moov_off = 0;
    int found = 0;
    if (!mp4 || !data || size < 16) return -1;
    memset(mp4, 0, sizeof(*mp4));
    mp4->data = data;
    mp4->size = size;
    while (off < size) {
        if (next_box(data, size, &off, &top) < 0) return -1;
        if (top.type == BOX('m','o','o','v')) {
            moov = top;
            moov_off = 1;
            break;
        }
    }
    if (!moov_off) return -1;
    off = 0;
    while (off < moov.size) {
        int r;
        if (next_box(moov.p, moov.size, &off, &trak) < 0) return -1;
        if (trak.type != BOX('t','r','a','k')) continue;
        {
            crtube_mp4_t candidate = *mp4;
            r = parse_trak(&candidate, &trak);
            if (r == 0) {
                *mp4 = candidate;
                found = 1;
                break;
            }
            if (r < 0 && r != -1) return r;
        }
    }
    if (!found) return -1;
    mp4->video_track_found = 1;
    return 0;
}

static uint32_t sample_size_at(const crtube_mp4_t *m, uint32_t index)
{
    return m->default_sample_size ? m->default_sample_size :
           be32(m->stsz + (size_t)index * 4u);
}

int crtube_mp4_get_sample(const crtube_mp4_t *m, uint32_t index,
                          crtube_mp4_sample_t *sample)
{
    uint32_t chunk, sample_in_chunk, first_sample = 0, stsc_i;
    uint64_t chunk_offset;
    if (!m || !sample || index >= m->sample_count) return -1;
    first_sample = 0;
    for (chunk = 1; chunk <= m->chunk_count; ++chunk) {
        uint32_t spc = 0;
        for (stsc_i = 0; stsc_i < m->stsc_count; ++stsc_i) {
            const uint8_t *e = m->stsc + (size_t)stsc_i * 12u;
            uint32_t first = be32(e);
            if (first <= chunk) spc = be32(e + 4);
            else break;
        }
        if (!spc) return -1;
        if (index < first_sample + spc) {
            uint32_t i;
            sample_in_chunk = index - first_sample;
            chunk_offset = m->chunk_offset_width == 4 ?
                be32(m->chunk_offsets + (size_t)(chunk - 1u) * 4u) :
                be64(m->chunk_offsets + (size_t)(chunk - 1u) * 8u);
            for (i = 0; i < sample_in_chunk; ++i)
                chunk_offset += sample_size_at(m, first_sample + i);
            sample->size = sample_size_at(m, index);
            if (chunk_offset > m->size || sample->size > m->size - (size_t)chunk_offset)
                return -1;
            sample->data = m->data + (size_t)chunk_offset;
            sample->index = index;
            return 0;
        }
        first_sample += spc;
    }
    return -1;
}

static int avcc_nal_at(const crtube_mp4_t *m, int want_pps, unsigned wanted,
                       const uint8_t **nal, size_t *size)
{
    const uint8_t *p;
    size_t n, off;
    unsigned count, i;
    if (!m || !m->avcc || !nal || !size) return -1;
    p = m->avcc; n = m->avcc_size;
    count = p[5] & 31u;
    off = 6;
    if (want_pps) {
        for (i = 0; i < count; ++i) {
            unsigned len = ((unsigned)p[off] << 8) | p[off + 1];
            off += 2;
            if (len > n - off) return -1;
            off += len;
        }
        if (off >= n) return -1;
        count = p[off++];
    }
    if (wanted >= count) return -1;
    for (i = 0; i < count; ++i) {
        unsigned len;
        if (off + 2 > n) return -1;
        len = ((unsigned)p[off] << 8) | p[off + 1];
        off += 2;
        if (!len || len > n - off) return -1;
        if (i == wanted) {
            *nal = p + off;
            *size = len;
            return 0;
        }
        off += len;
    }
    return -1;
}

int crtube_mp4_get_sps(const crtube_mp4_t *m, unsigned index,
                       const uint8_t **nal, size_t *size)
{
    return avcc_nal_at(m, 0, index, nal, size);
}

int crtube_mp4_get_pps(const crtube_mp4_t *m, unsigned index,
                       const uint8_t **nal, size_t *size)
{
    return avcc_nal_at(m, 1, index, nal, size);
}


int crtube_mp4_write_parameter_sets_annexb(const crtube_mp4_t *m,
                                           uint8_t *output, size_t capacity,
                                           size_t *output_size)
{
    size_t out = 0, n;
    unsigned i;
    const uint8_t *nal;
    if (!m || !output || !output_size) return -1;
    for (i = 0; crtube_mp4_get_sps(m, i, &nal, &n) == 0; ++i) {
        if (capacity - out < n + 4u) return -1;
        output[out++] = 0; output[out++] = 0;
        output[out++] = 0; output[out++] = 1;
        memcpy(output + out, nal, n);
        out += n;
    }
    for (i = 0; crtube_mp4_get_pps(m, i, &nal, &n) == 0; ++i) {
        if (capacity - out < n + 4u) return -1;
        output[out++] = 0; output[out++] = 0;
        output[out++] = 0; output[out++] = 1;
        memcpy(output + out, nal, n);
        out += n;
    }
    if (!out) return -1;
    *output_size = out;
    return 0;
}

int crtube_mp4_sample_to_annexb(const crtube_mp4_t *m,
                                const crtube_mp4_sample_t *sample,
                                uint8_t *output, size_t capacity,
                                size_t *output_size)
{
    size_t in = 0, out = 0;
    if (!m || !sample || !output || !output_size ||
        (m->nal_length_size != 1 && m->nal_length_size != 2 &&
         m->nal_length_size != 4)) return -1;
    while (in < sample->size) {
        uint32_t nal_size = 0;
        unsigned i;
        if (sample->size - in < m->nal_length_size) return -1;
        for (i = 0; i < m->nal_length_size; ++i)
            nal_size = (nal_size << 8) | sample->data[in++];
        if (!nal_size || nal_size > sample->size - in ||
            capacity - out < 4u + nal_size) return -1;
        output[out++] = 0; output[out++] = 0;
        output[out++] = 0; output[out++] = 1;
        memcpy(output + out, sample->data + in, nal_size);
        out += nal_size;
        in += nal_size;
    }
    *output_size = out;
    return 0;
}

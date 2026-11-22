#include "h264_decoder.h"

#include <string.h>

static int append_partition(crtube_h264_p_partition_t *out,
                            unsigned *count, unsigned x, unsigned y,
                            unsigned w, unsigned h)
{
    crtube_h264_p_partition_t *p;
    if (!out || !count || *count >= 16u || !w || !h ||
        x + w > 4u || y + h > 4u)
        return -1;
    p = &out[(*count)++];
    memset(p, 0, sizeof(*p));
    p->x4 = (uint8_t)x;
    p->y4 = (uint8_t)y;
    p->width4 = (uint8_t)w;
    p->height4 = (uint8_t)h;
    return 0;
}

int crtube_h264_p_partition_layout(unsigned mb_type,
    const uint8_t sub_mb_types[4], crtube_h264_p_partition_t partitions[16],
    unsigned *partition_count)
{
    unsigned count = 0, i;
    if (!partitions || !partition_count || mb_type > 4u)
        return -1;
    memset(partitions, 0, 16u * sizeof(*partitions));
    if (mb_type == 0u) {
        if (append_partition(partitions, &count, 0, 0, 4, 4) < 0) return -1;
    } else if (mb_type == 1u) {
        if (append_partition(partitions, &count, 0, 0, 4, 2) < 0 ||
            append_partition(partitions, &count, 0, 2, 4, 2) < 0) return -1;
    } else if (mb_type == 2u) {
        if (append_partition(partitions, &count, 0, 0, 2, 4) < 0 ||
            append_partition(partitions, &count, 2, 0, 2, 4) < 0) return -1;
    } else {
        if (!sub_mb_types) return -1;
        for (i = 0; i < 4u; ++i) {
            unsigned sx = (i & 1u) * 2u;
            unsigned sy = (i >> 1u) * 2u;
            unsigned sub = sub_mb_types[i];
            unsigned dx, dy, nx, ny, x, y;
            if (sub > 3u) return -1;
            if (sub == 0u) { dx = 2; dy = 2; nx = ny = 1; }
            else if (sub == 1u) { dx = 2; dy = 1; nx = 1; ny = 2; }
            else if (sub == 2u) { dx = 1; dy = 2; nx = 2; ny = 1; }
            else { dx = dy = 1; nx = ny = 2; }
            for (y = 0; y < ny; ++y)
                for (x = 0; x < nx; ++x)
                    if (append_partition(partitions, &count,
                            sx + x * dx, sy + y * dy, dx, dy) < 0)
                        return -1;
        }
    }
    /* A complete P macroblock must cover each 4x4 luma cell exactly once. */
    {
        uint8_t coverage[16] = {0};
        unsigned p, x, y;
        for (p = 0; p < count; ++p) {
            const crtube_h264_p_partition_t *part = &partitions[p];
            for (y = part->y4; y < (unsigned)part->y4 + part->height4; ++y)
                for (x = part->x4; x < (unsigned)part->x4 + part->width4; ++x) {
                    unsigned cell = y * 4u + x;
                    if (coverage[cell]++) return -1;
                }
        }
        for (i = 0; i < 16u; ++i) if (coverage[i] != 1u) return -1;
    }
    *partition_count = count;
    return 0;
}

#include "h264_mp4.h"
#include "h264_decoder.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    uint8_t *d, *out;
    crtube_mp4_t m;
    crtube_mp4_sample_t s;
    size_t os = 0, ps = 0;
    const uint8_t *nal;
    size_t ns;
    crtube_h264_pps_info_t pps;
    crtube_h264_stream_info_t sps;
    unsigned i;

    if (argc < 3) return 2;
    f = fopen(argv[1], "rb");
    if (!f) return 3;
    if (fseek(f, 0, SEEK_END) != 0) return 4;
    n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) return 5;
    d = (uint8_t *)malloc((size_t)n);
    if (!d) return 6;
    if (fread(d, 1, (size_t)n, f) != (size_t)n) return 7;
    fclose(f);

    if (crtube_mp4_open_memory(&m, d, (size_t)n) < 0) {
        fprintf(stderr, "open fail\n");
        return 8;
    }
    printf("video %ux%u samples=%u nal_length=%u avcc=%lu\n",
           m.width, m.height, m.sample_count, m.nal_length_size,
           (unsigned long)m.avcc_size);
    for (i = 0; crtube_mp4_get_sps(&m, i, &nal, &ns) == 0; ++i) {
        int sr = crtube_h264_parse_sps(nal, ns, &sps);
        printf("SPS %u bytes=%lu profile=%u parse=%d dimensions=%ux%u\n",
               i, (unsigned long)ns, nal[1], sr,
               sr == 0 ? sps.width : 0u, sr == 0 ? sps.height : 0u);
    }
    for (i = 0; crtube_mp4_get_pps(&m, i, &nal, &ns) == 0; ++i) {
        int pr = crtube_h264_parse_pps(nal, ns, &pps);
        printf("PPS %u bytes=%lu parse=%d entropy_coding_mode=%u\n",
               i, (unsigned long)ns, pr,
               pr == 0 ? pps.entropy_coding_mode : 99u);
    }
    if (crtube_mp4_get_sample(&m, 0, &s) < 0) return 9;
    out = (uint8_t *)malloc(s.size + 4096u);
    if (!out) return 10;
    if (crtube_mp4_write_parameter_sets_annexb(&m, out, s.size + 4096u, &ps) < 0)
        return 11;
    if (crtube_mp4_sample_to_annexb(&m, &s, out + ps, s.size + 4096u - ps, &os) < 0)
        return 12;
    os += ps;
    f = fopen(argv[2], "wb");
    if (!f) return 13;
    if (fwrite(out, 1, os, f) != os) return 14;
    fclose(f);
    printf("sample0=%lu annexb=%lu\n", (unsigned long)s.size,
           (unsigned long)os);
    free(out);
    free(d);
    return 0;
}

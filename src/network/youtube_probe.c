#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "bearssl_transport.h"
#include "http.h"
#include "youtube_probe.h"
#include "youtube_search.h"
#include "network.h"
#include "sntp.h"
#include "debug_log.h"

void crtube_youtube_run_probe(void)
{
    int result;
    time_t rtc_time;
    const crtube_youtube_video_t *videos;
    size_t video_count = 0, i;

    rtc_time = time(NULL);
    crtube_log("YouTube BearSSL probe: starting verified TLS 1.2 POST.\n");
    crtube_log("YouTube BearSSL RTC: time_t=%lld valid=%d (accepted Unix range: 1577836800..2147483647).\n",
               (long long)rtc_time, crtube_bearssl_clock_available());
    crtube_network_report_progress(CRTUBE_TEXT_NETWORK_PROGRESS_YOUTUBE_DNS);

    {
        int entropy_sources = crtube_bearssl_entropy_probe();
        crtube_log("YouTube BearSSL entropy probe: getrandom_read=%d dev_urandom_read=%d.\n",
                   (entropy_sources & CRTUBE_BEARSSL_ENTROPY_GETRANDOM_READ) != 0,
                   (entropy_sources & CRTUBE_BEARSSL_ENTROPY_URANDOM_DEVICE) != 0);
        if (!crtube_bearssl_entropy_available()) {
            crtube_log("YouTube BearSSL: blocked; no approved cryptographic entropy provider is configured. rand() is not a secure substitute.\n");
            return;
        }
    }
    if (!crtube_bearssl_clock_available()) {
        crtube_log("YouTube BearSSL: console clock invalid; attempting SNTP synchronization.\n");
        if (crtube_sntp_sync() != 0) {
            crtube_log("YouTube BearSSL: blocked; SNTP synchronization failed.\n");
            return;
        }
        crtube_log("YouTube BearSSL: clock after SNTP time_t=%lu.\n",
                   (unsigned long)time(NULL));
    }
    if (!crtube_bearssl_clock_available()) {
        crtube_log("YouTube BearSSL: blocked; synchronized RTC is still outside the accepted certificate-validation range.\n");
        return;
    }

    videos = crtube_youtube_get_home_results(&video_count);
    if (videos == NULL) {
        crtube_log("YouTube home search: requesting four results.\n");
        result = crtube_youtube_load_home_results("playstation 2 homebrew");
        if (result < 0) {
            crtube_log("YouTube home search: failed result=%d status=%d stage=%d errno=%d tls_error=%d.\n",
                       result, crtube_http_last_status(), crtube_http_last_stage(),
                       crtube_http_last_errno(), crtube_bearssl_last_error());
            return;
        }
        videos = crtube_youtube_get_home_results(&video_count);
    }
    crtube_log("YouTube home search: parsed=%lu status=%d.\n",
               (unsigned long)video_count, crtube_http_last_status());
    for (i = 0; videos != NULL && i < video_count; i++) {
        crtube_log("YouTube home item %lu: id=%s title=%s channel=%s\n",
                   (unsigned long)i, videos[i].video_id,
                   videos[i].title, videos[i].channel);
        crtube_log("YouTube home thumbnail %lu: %s\n",
                   (unsigned long)i, videos[i].thumbnail_url);
    }
}

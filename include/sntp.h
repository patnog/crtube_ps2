#ifndef CRTUBE_PS2_SNTP_H
#define CRTUBE_PS2_SNTP_H

/* Synchronize the PS2 RTC from two independent SNTP servers.
   Returns 0 on success, negative on failure. */
int crtube_sntp_sync(void);

#endif

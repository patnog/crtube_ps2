#ifndef CRTUBE_H264_MP4_SMB_H
#define CRTUBE_H264_MP4_SMB_H

#include <stddef.h>
#include <stdint.h>

/* Reads one SMB file into caller-owned memory. Path is relative to smb0:,
   e.g. "APPS/CRTUBE_PS2/240p-baseline.mp4". Returns bytes read or negative. */
long crtube_h264_read_smb_file(const char *path, uint8_t *buffer,
                               size_t capacity);

#endif

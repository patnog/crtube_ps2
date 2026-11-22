#include "h264_mp4_smb.h"

#include <stdio.h>
#include <string.h>
#include "debug_log.h"

long crtube_h264_read_smb_file(const char *path, uint8_t *buffer,
                               size_t capacity)
{
    char device_path[256];
    size_t path_len, used = 0;
    FILE *file;
    int read_error = 0;
    int close_result;

    if (!path || !buffer || capacity == 0)
        return -1;
    path_len = strlen(path);
    if (path_len == 0 || path_len + 6u > sizeof(device_path))
        return -1;
    memcpy(device_path, "smb0:/", 6);
    memcpy(device_path + 6, path, path_len + 1u);

    /*
     * Use the same stdio/IOMAN path used by the SMB debug log. On the
     * installed SMBMAN + FILEXIO stack, fileXioOpen() returned a valid fd
     * for this file but fileXioRead() returned zero bytes immediately.
     * stdio avoids that direct-RPC read path and uses the registered device
     * driver through newlib.
     */
    crtube_log("H.264 SMB IO: fopen path=%s\n", device_path);
    file = fopen(device_path, "rb");
    if (!file) {
        crtube_log("H.264 SMB IO: fopen failed\n");
        return -2;
    }

    while (used < capacity) {
        size_t request = capacity - used;
        size_t n;

        if (request > 4096u)
            request = 4096u;
        n = fread(buffer + used, 1, request, file);
        crtube_log("H.264 SMB IO: fread offset=%lu request=%lu result=%lu "
                   "eof=%d error=%d\n",
                   (unsigned long)used, (unsigned long)request,
                   (unsigned long)n, feof(file) ? 1 : 0,
                   ferror(file) ? 1 : 0);
        used += n;

        if (n == 0) {
            if (ferror(file))
                read_error = 1;
            else if (!feof(file))
                read_error = 1; /* no progress without EOF is not success */
            break;
        }
    }

    if (used == capacity && !read_error) {
        int extra = fgetc(file);
        if (extra != EOF) {
            crtube_log("H.264 SMB IO: file exceeds capacity=%lu\n",
                       (unsigned long)capacity);
            read_error = 2;
        } else if (ferror(file)) {
            read_error = 1;
        }
    }

    close_result = fclose(file);
    crtube_log("H.264 SMB IO: fclose=%d bytes=%lu capacity=%lu "
               "read_error=%d\n",
               close_result, (unsigned long)used,
               (unsigned long)capacity, read_error);

    if (close_result != 0 && !read_error)
        read_error = 1;
    if (read_error == 2)
        return -4;
    if (read_error)
        return -3;
    if (used == 0)
        return -3;
    return (long)used;
}

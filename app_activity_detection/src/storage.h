#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

#define STORAGE_MNT     "/lfs"
#define STORAGE_ACC_DIR "/lfs/acc"

/* Mount LittleFS at /lfs (never formats) and create /lfs/acc. 0 on success. */
int storage_init(void);

/* Free bytes on the volume, or <0 on error */
int64_t storage_free_bytes(void);

#endif
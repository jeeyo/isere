#ifndef ISERE_FS_H_

#include "isere.h"

#define ISERE_FS_H_

#ifdef __cplusplus
extern "C" {
#endif

#define ISERE_FS_LOG_TAG "fs"

typedef int fs_file_t;

int fs_init(isere_t *isere, isere_fs_t *fs);
void fs_deinit(isere_fs_t *fs);

int fs_open(isere_fs_t *fs, fs_file_t *file, const char *path, int flags);
int fs_close(isere_fs_t *fs, fs_file_t *file);

int fs_read(isere_fs_t *fs, fs_file_t *file, uint8_t *buf, size_t size);
int fs_write(isere_fs_t *fs, fs_file_t *file, uint8_t *buf, size_t size);

int fs_rewind(isere_fs_t *fs, fs_file_t *file);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_FS_H_ */

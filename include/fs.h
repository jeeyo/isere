#ifndef ISERE_FS_H_
#define ISERE_FS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "logger.h"

#define ISERE_FS_LOG_TAG "fs"

#define ISERE_FS_MAX_PATH 64

typedef int fs_file_t;

typedef struct {
  isere_logger_t *logger;
} isere_fs_t;

int fs_init(isere_fs_t *fs, isere_logger_t *logger);
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

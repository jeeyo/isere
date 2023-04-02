#include "fs.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static isere_t *__isere = NULL;

int fs_init(isere_t *isere, isere_fs_t *fs)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  return 0;
}

void fs_deinit(isere_fs_t *fs)
{
  if (__isere) {
    __isere = NULL;
  }

  return;
}

int fs_open(isere_fs_t *fs, fs_file_t *file, const char *path, int flags)
{
  if (file == NULL) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  *file = open(path, flags, 0666);
  if (*file < 0) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "open() error: %s", strerror(errno));
    return -1;
  }

  return 0;
}

int fs_close(isere_fs_t *fs, fs_file_t *file)
{
  if (file == NULL) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  int ret = close(*file);
  if (ret != 0) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "close() error: %s", strerror(errno));
    return -1;
  }

  *file = -1;
  return 0;
}

int fs_read(isere_fs_t *fs, fs_file_t *file, uint8_t *buf, size_t size)
{
  if (file == NULL) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  if (buf == NULL) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "buf is NULL");
    return -1;
  }

  ssize_t ret = read(*file, buf, size);
  if (ret < 0) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "read() error: %s", strerror(errno));
    return -1;
  }

  return ret;
}

int fs_write(isere_fs_t *fs, fs_file_t *file, uint8_t *buf, size_t size)
{
  if (file == NULL) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  if (buf == NULL) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "buf is NULL");
    return -1;
  }

  ssize_t ret = write(*file, buf, size);
  if (ret < 0) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "write() error: %s", strerror(errno));
    return -1;
  }

  return ret;
}

int fs_rewind(isere_fs_t *fs, fs_file_t *file)
{
  if (file == NULL) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  off_t ret = lseek(*file, 0, SEEK_SET);
  if (ret < 0) {
    __isere->logger->error(ISERE_FS_LOG_TAG, "lseek() error: %s", strerror(errno));
    return -1;
  }

  return 0;
}

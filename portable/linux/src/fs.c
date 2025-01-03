#include "fs.h"

// #include <fcntl.h>
// #include <unistd.h>
#include <string.h>
#include <errno.h>

#define FS_ROOT_DIR "romfs/"

static isere_fs_t *__fs = NULL;

int fs_init(isere_fs_t *fs, isere_logger_t *logger)
{
  fs->logger = logger;
  __fs = fs;

  return 0;
}

void fs_deinit(isere_fs_t *fs)
{
  if (__fs) {
    __fs = NULL;
  }

  return;
}

int fs_open(isere_fs_t *fs, fs_file_t *file, const char *path, int flags)
{
  if (file == NULL) {
    __fs->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  char filepath[256];
  memset(filepath, 0, sizeof(filepath));
  strncpy(filepath, FS_ROOT_DIR, sizeof(filepath) - 1);
  strncat(filepath, path, sizeof(filepath) - 1);

  // *file = open(filepath, flags, 0666);
  // if (*file < 0) {
  //   __fs->logger->error(ISERE_FS_LOG_TAG, "open() error: %s", strerror(errno));
  //   return -1;
  // }

  return 0;
}

int fs_close(isere_fs_t *fs, fs_file_t *file)
{
  if (file == NULL) {
    __fs->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  // int ret = close(*file);
  // if (ret != 0) {
  //   // __fs->logger->error(ISERE_FS_LOG_TAG, "close() error: %s", strerror(errno));
  //   return -1;
  // }

  *file = -1;
  return 0;
}

int fs_read(isere_fs_t *fs, fs_file_t *file, uint8_t *buf, size_t size)
{
  if (file == NULL) {
    __fs->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  if (buf == NULL) {
    __fs->logger->error(ISERE_FS_LOG_TAG, "buf is NULL");
    return -1;
  }

  // ssize_t ret = read(*file, buf, size);
  // if (ret <= 0) {
  //   __fs->logger->error(ISERE_FS_LOG_TAG, "read() error: %s", strerror(errno));
  //   return -1;
  // }

  // return ret;
  return 0;
}

int fs_write(isere_fs_t *fs, fs_file_t *file, uint8_t *buf, size_t size)
{
  if (file == NULL) {
    __fs->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  if (buf == NULL) {
    __fs->logger->error(ISERE_FS_LOG_TAG, "buf is NULL");
    return -1;
  }

  // ssize_t ret = write(*file, buf, size);
  // if (ret < 0) {
  //   __fs->logger->error(ISERE_FS_LOG_TAG, "write() error: %s", strerror(errno));
  //   return -1;
  // }

  // return ret;
  return 0;
}

int fs_rewind(isere_fs_t *fs, fs_file_t *file)
{
  if (file == NULL) {
    __fs->logger->error(ISERE_FS_LOG_TAG, "file is NULL");
    return -1;
  }

  // off_t ret = lseek(*file, 0, SEEK_SET);
  // if (ret < 0) {
  //   __fs->logger->error(ISERE_FS_LOG_TAG, "lseek() error: %s", strerror(errno));
  //   return -1;
  // }

  return 0;
}

#include "ini.h"

#include "fs.h"
#include <fcntl.h>

// static capn_text chars_to_text(const char *chars) {
//   return {
//     .len = (int) strlen(chars),
//     .str = chars,
//     .seg = NULL,
//   };
// }

static isere_t *__isere = NULL;
static struct Config config;

static int ini_write_default(isere_t *isere)
{
  struct capn c;
  capn_init_malloc(&c);
  capn_ptr cr = capn_root(&c);
  struct capn_segment *cs = cr.seg;

  // Set initial object in `p`.
  struct Config p = {
    .timeout = 30000,
  };

  Config_ptr pp = new_Config(cs);
  write_Config(&p, pp);
  int setp_ret = capn_setp(capn_root(&c), 0, pp.p);

  uint8_t buf[INI_MAX_CONFIG_FILE_SIZE];
  int sz = capn_write_mem(&c, buf, sizeof(buf), 0 /* packed */);
  capn_free(&c);

  fs_file_t fp = -1;
  int fd = fs_open(isere->fs, &fp, INI_FILENAME, O_WRONLY | O_CREAT);
  if (fd < 0) {
    return -1;
  }

  int ret = fs_write(isere->fs, &fp, buf, sz);
  if (ret < 0) {
    return -1;
  }

  if (fs_close(isere->fs, &fp) < 0) {
    return -1;
  }

  return 0;
}

int ini_init(isere_t *isere, isere_ini_t *ini)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  if (isere->fs == NULL) {
    return -1;
  }

  fs_file_t fp = -1;
  int fd = fs_open(isere->fs, &fp, INI_FILENAME, O_RDONLY);
  if (fd < 0) {
    return -1;
  }

  uint8_t buf[INI_MAX_CONFIG_FILE_SIZE];
  int sz = fs_read(isere->fs, &fp, buf, INI_MAX_CONFIG_FILE_SIZE);
  if (sz < 0) {
    return -1;
  }

  if (fs_close(isere->fs, &fp) < 0) {
    return -1;
  }

  struct capn rc;
  int init_mem_ret = capn_init_mem(&rc, buf, sz, 0 /* packed */);
  if (init_mem_ret < 0) {
    return -1;
  }

  Config_ptr rroot;
  rroot.p = capn_getp(capn_root(&rc), 0 /* off */, 1 /* resolve */);
  read_Config(&config, rroot);

  capn_free(&rc);
  return 0;
}

int ini_get_timeout(isere_ini_t *ini)
{
  return config.timeout;
}

void ini_deinit(isere_ini_t *ini)
{
  return;
}

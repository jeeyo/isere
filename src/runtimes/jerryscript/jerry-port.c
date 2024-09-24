#include "isere.h"

#include "runtime.h"
#include "polyfills.h"

#include "jerryscript-port.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdint.h>

void jerry_port_fatal(jerry_fatal_code_t code)
{
  exit((int) code);
}

int32_t jerry_port_local_tza(double unix_ms)
{
  (void) unix_ms;

  /* We live in UTC. */
  return 0;
}

double jerry_port_current_time(void)
{
  return (double) 0;
}

void jerry_port_sleep(uint32_t sleep_time)
{
  vTaskDelay(sleep_time);
}

void jerry_port_log(const char *message_p)
{
  // fputs (message_p, stderr);
}

jerry_char_t *jerry_port_path_normalize(const jerry_char_t *path_p, /**< input path */
                                        jerry_size_t path_size) /**< size of the path */
{
  (void) path_size;
  return (jerry_char_t *) realpath ((char *) path_p, NULL);
}

jerry_size_t jerry_port_path_base(const jerry_char_t *path_p)
{
  const jerry_char_t *basename_p = (jerry_char_t *) strrchr ((char *) path_p, '/') + 1;
  return (jerry_size_t) (basename_p - path_p);
}

void jerry_port_path_free(jerry_char_t *path_p)
{
  free (path_p);
}

static jerry_size_t jerry_port_get_file_size (FILE *file_p) /**< opened file */
{
  fseek (file_p, 0, SEEK_END);
  long size = ftell (file_p);
  fseek (file_p, 0, SEEK_SET);

  return (jerry_size_t) size;
}

jerry_char_t *jerry_port_source_read(const char *file_name_p, /**< file name */
                                    jerry_size_t *out_size_p) /**< [out] read bytes */
{
  /* TODO(dbatyai): Temporary workaround for nuttx target
   * The nuttx target builds and copies the jerryscript libraries as a separate build step, which causes linking issues
   * later due to different libc libraries. It should incorporate the amalgam sources into the main nuttx build so that
   * the correct libraries are used, then this guard should be removed from here and also from the includes. */
#if defined(__GLIBC__) || defined(_WIN32)
  struct stat stat_buffer;
  if (stat (file_name_p, &stat_buffer) == -1 || S_ISDIR (stat_buffer.st_mode))
  {
    return NULL;
  }
#endif /* __GLIBC__ */

  FILE *file_p = fopen (file_name_p, "rb");

  if (file_p == NULL)
  {
    return NULL;
  }

  jerry_size_t file_size = jerry_port_get_file_size (file_p);
  jerry_char_t *buffer_p = (jerry_char_t *) malloc (file_size);

  if (buffer_p == NULL)
  {
    fclose (file_p);
    return NULL;
  }

  size_t bytes_read = fread (buffer_p, 1u, file_size, file_p);

  if (bytes_read != file_size)
  {
    fclose (file_p);
    free (buffer_p);
    return NULL;
  }

  fclose (file_p);
  *out_size_p = (jerry_size_t) bytes_read;

  return buffer_p;
}

void jerry_port_source_free(uint8_t *buffer_p) /**< buffer to free */
{
  free(buffer_p);
}

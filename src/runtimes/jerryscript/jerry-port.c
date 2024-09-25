#include "isere.h"

#include "runtime.h"
#include "polyfills.h"

#include "jerryscript-port.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
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
  puts(message_p);
}

jerry_char_t *jerry_port_path_normalize(const jerry_char_t *path_p, /**< input path */
                                        jerry_size_t path_size) /**< size of the path */
{
  (void) path_size;
  jerry_char_t *path = (jerry_char_t *)pvPortMalloc(8);
  strncpy((char *)path, "handler", 8);
  return path;
}

jerry_size_t jerry_port_path_base(const jerry_char_t *path_p)
{
  const jerry_char_t *basename_p = (jerry_char_t *) strrchr ((char *) path_p, '/') + 1;
  return (jerry_size_t) (basename_p - path_p);
}

void jerry_port_path_free(jerry_char_t *path_p)
{
  vPortFree(path_p);
}

jerry_char_t *jerry_port_source_read(const char *file_name_p, /**< file name */
                                    jerry_size_t *out_size_p) /**< [out] read bytes */
{
  if (strcmp(file_name_p, "handler") != 0) {
    return NULL;
  }

  // *out_size_p = (jerry_size_t) bytes_read;
  // return buffer_p;

  return NULL;
}

void jerry_port_source_free(uint8_t *buffer_p) /**< buffer to free */
{
  // we store handler code in static memory, so don't have to free() it
  // vPortFree(buffer_p);
}

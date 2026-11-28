#include "isere.h"
#include "platform.h"

#include "jerryscript-port.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int32_t jerry_port_local_tza(double unix_ms)
{
  (void) unix_ms;

  /* We live in UTC. */
  return 0;
}

double jerry_port_current_time(void)
{
  // Date.now(): JWT expiry checks depend on it
  return (double)platform_time_ms();
}

void jerry_port_sleep(uint32_t sleep_time)
{
  vTaskDelay(sleep_time);
}

void jerry_port_log(const char *message_p)
{
  fputs(message_p, stderr);
}

jerry_char_t *jerry_port_path_normalize(const jerry_char_t *path_p, /**< input path */
                                        jerry_size_t path_size) /**< size of the path */
{
  return strdup(path_p);
}

jerry_size_t jerry_port_path_base(const jerry_char_t *path_p)
{
  const jerry_char_t *basename_p = (jerry_char_t *) strrchr ((char *) path_p, '/') + 1;
  return (jerry_size_t) (basename_p - path_p);
}

void jerry_port_path_free(jerry_char_t *path_p)
{
  free(path_p);
}

jerry_char_t *jerry_port_source_read(const char *file_name_p, /**< file name */
                                    jerry_size_t *out_size_p) /**< [out] read bytes */
{
  return NULL;
}

void jerry_port_source_free(uint8_t *buffer_p) /**< buffer to free */
{
  free(buffer_p);
}

void jerry_port_print_byte (jerry_char_t byte) /**< the character to print */
{
  putchar(byte);
}

void jerry_port_print_buffer (const jerry_char_t *buffer_p, /**< string buffer */
                              jerry_size_t buffer_size) /**< string size*/
{
  for (jerry_size_t i = 0; i < buffer_size; i++)
  {
    jerry_port_print_byte(buffer_p[i]);
  }
}

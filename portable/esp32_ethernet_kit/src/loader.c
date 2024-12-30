#include "loader.h"

#include <string.h>

static uint8_t *loader_get_fn(isere_loader_t *loader, uint32_t *size)
{
  *size = handler_len;
  return handler;
}

int isere_loader_init(isere_loader_t *loader, isere_logger_t *logger)
{
  if (loader->fn != NULL) {
    logger->error(ISERE_LOADER_LOG_TAG, "loader already initialized");
    return -1;
  }

  // load javascript source code from the shared library
  loader->fn = loader_get_fn(loader, &loader->fn_size);

  return 0;
}

int isere_loader_deinit(isere_loader_t *loader)
{
  return 0;
}

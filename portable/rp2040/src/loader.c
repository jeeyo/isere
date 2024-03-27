#include "loader.h"

#include <string.h>

static isere_t *__isere = NULL;

static uint8_t *loader_get_fn(isere_loader_t *loader, uint32_t *size)
{
  *size = handler_len;
  return handler;
}

int loader_init(isere_t *isere, isere_loader_t *loader)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  if (loader->fn != NULL) {
    __isere->logger->error(ISERE_LOADER_LOG_TAG, "loader already initialized");
    return -1;
  }

  // load javascript source code from the shared library
  loader->fn = loader_get_fn(loader, &loader->fn_size);

  return 0;
}

int loader_deinit(isere_loader_t *loader)
{
  if (__isere) {
    __isere = NULL;
  }

  return 0;
}

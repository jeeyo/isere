#include "loader.h"

#include <string.h>
#include <dlfcn.h>

static isere_t *__isere = NULL;

int loader_init(isere_t *isere)
{
  __isere = isere;
  return 0;
}

void *loader_open(const char *filename)
{
  void *dl = dlopen(filename, RTLD_LAZY);
  if (!dl) {
    __isere->logger->error("[%s] dlopen() error: %s", ISERE_LOADER_LOG_TAG, dlerror());
    return NULL;
  }

  return dl;
}

int loader_close(void *dl)
{
  if (__isere) {
    __isere = NULL;
  }

  if (dl) {
    dlclose(dl);
  }

  return 0;
}

uint8_t *loader_get_fn(void *dl, uint32_t *size)
{
  *size = *(uint32_t *)(dlsym(dl, ISERE_LOADER_HANDLER_SIZE_FUNCTION));
  return (uint8_t *)(dlsym(dl, ISERE_LOADER_HANDLER_FUNCTION));
}

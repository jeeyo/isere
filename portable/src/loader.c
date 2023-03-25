#include "loader.h"

#include <string.h>
#include <dlfcn.h>

static isere_t *__isere = NULL;

static uint8_t *loader_get_fn(isere_loader_t *loader, uint32_t *size)
{
  *size = *(uint32_t *)(dlsym(loader->dll, ISERE_LOADER_HANDLER_SIZE_FUNCTION));
  return (uint8_t *)(dlsym(loader->dll, ISERE_LOADER_HANDLER_FUNCTION));
}

int loader_init(isere_t *isere, isere_loader_t *loader, const char *dll_path)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  if (loader->dll != NULL || loader->fn != NULL) {
    __isere->logger->error("[%s] loader already initialized", ISERE_LOADER_LOG_TAG);
    return -1;
  }

  if (dll_path == NULL) {
    __isere->logger->error("[%s] dll_path is NULL", ISERE_LOADER_LOG_TAG);
    return -1;
  }

  loader->dll = dlopen(dll_path, RTLD_LAZY);
  if (!loader->dll) {
    loader->dll = NULL;
    __isere->logger->error("[%s] dlopen() error: %s", ISERE_LOADER_LOG_TAG, dlerror());
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

  if (loader->dll) {
    dlclose(loader->dll);
  }

  return 0;
}

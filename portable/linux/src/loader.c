#include "loader.h"

#include <string.h>

#ifdef ISERE_USE_DYNLINK
#include <dlfcn.h>
#endif /* ISERE_USE_DYNLINK */

static uint8_t *loader_get_fn(isere_loader_t *loader, uint32_t *size)
{
#ifdef ISERE_USE_DYNLINK
  *size = *(uint32_t *)(dlsym(loader->dll, ISERE_LOADER_HANDLER_SIZE_FUNCTION));
  return (uint8_t *)(dlsym(loader->dll, ISERE_LOADER_HANDLER_FUNCTION));
#else /* ISERE_USE_DYNLINK */
  *size = handler_len;
  return handler;
#endif /* ISERE_USE_DYNLINK */
}

int isere_loader_init(isere_loader_t *loader, isere_logger_t *logger)
{
#ifdef ISERE_USE_DYNLINK
  if (loader->dll != NULL || loader->fn != NULL) {
#else
  if (loader->fn != NULL) {
#endif /* ISERE_USE_DYNLINK */
    logger->error(ISERE_LOADER_LOG_TAG, "loader already initialized");
    return -1;
  }

#ifdef ISERE_USE_DYNLINK
  loader->dll = dlopen(ISERE_LOADER_HANDLER_FUNCTION_DLL_PATH, RTLD_LAZY);
  if (!loader->dll) {
    loader->dll = NULL;
    logger->error(ISERE_LOADER_LOG_TAG, "dlopen() error: %s", dlerror());
    return -1;
  }
#endif /* ISERE_USE_DYNLINK */

  // load javascript source code from the shared library
  loader->fn = loader_get_fn(loader, &loader->fn_size);

  return 0;
}

int isere_loader_deinit(isere_loader_t *loader)
{
#ifdef ISERE_USE_DYNLINK
  if (loader->dll) {
    dlclose(loader->dll);
  }
#endif /* ISERE_USE_DYNLINK */

  return 0;
}

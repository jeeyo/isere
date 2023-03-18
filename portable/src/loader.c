#include "loader.h"

#include <string.h>

#include <dlfcn.h>
static isere_t *__isere = NULL;
static void *__dynlnk = NULL;

int loader_init(isere_t *isere)
{
  __isere = isere;
  return 0;
}

int loader_open(const char *filename)
{
  __dynlnk = dlopen(filename, RTLD_LAZY);
  if (!__dynlnk) {
    return -1;
  }

  return 0;
}

int loader_close()
{
  if (__isere) {
    __isere = NULL;
  }

  if (__dynlnk) {
    dlclose(__dynlnk);
    __dynlnk = NULL;
  }

  return 0;
}

uint8_t *loader_get_fn(uint32_t *size)
{
  *size = *(uint32_t *)(dlsym(__dynlnk, ISERE_LOADER_HANDLER_SIZE_FUNCTION));
  return (uint8_t *)(dlsym(__dynlnk, ISERE_LOADER_HANDLER_FUNCTION));
}

char *loader_last_error()
{
  return dlerror();
}

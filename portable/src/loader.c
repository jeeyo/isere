#include "loader.h"

#include <dlfcn.h>

void *loader_open(const char *filename)
{
  return dlopen(filename, RTLD_LAZY);
}

int loader_close(void *handle)
{
  return dlclose(handle);
}

loader_fn_t *loader_get_fn(void *handle, const char *fn)
{
  return (loader_fn_t *)(dlsym(handle, fn));
}

char *loader_last_error()
{
  return dlerror();
}

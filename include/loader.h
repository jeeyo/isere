#ifndef LOADER_H_

#define LOADER_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef int (loader_fn_t)();

void *loader_open(const char *filename);
int loader_close(void *handle);
loader_fn_t *loader_get_fn(void *handle, const char *fn);
char *loader_last_error();

#ifdef __cplusplus
}
#endif

#endif /* LOADER_H_ */

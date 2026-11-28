#ifndef ISERE_MEM_H_
#define ISERE_MEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// Heap used by isère itself: the JS engines, connection buffers, TLS and fetch.
// Every block carries a small size header, so the heap in use is known exactly
// and can be attributed to whichever request is running when a block is
// allocated or freed. Only the httpd task allocates through it.

// what one request allocated and freed while it was the current account
typedef struct {
  uint64_t allocated;   // bytes allocated
  int64_t net;          // bytes allocated minus bytes freed
  size_t peak_in_use;   // highest heap in use seen while this request ran
} isere_mem_account_t;

typedef struct {
  size_t in_use;        // bytes currently allocated
  size_t peak_in_use;   // highest in_use since boot
  size_t heap_size;     // budget, 0 = unbounded
  uint64_t allocations; // number of allocations since boot
} isere_mem_stats_t;

void isere_mem_init(size_t heap_size);

void *isere_malloc(size_t size);
void *isere_calloc(size_t count, size_t size);
void *isere_realloc(void *ptr, size_t size);
void isere_free(void *ptr);
size_t isere_malloc_usable_size(const void *ptr);

// attribute allocations to `account` until the next call; NULL stops attributing
void isere_mem_set_account(isere_mem_account_t *account);
isere_mem_account_t *isere_mem_get_account(void);

// counts memory an engine manages inside its own arena (JerryScript) against the current account
void isere_mem_account_external(int64_t delta);

void isere_mem_get_stats(isere_mem_stats_t *stats);
// free heap and the lowest it has been since boot (budget minus in-use)
size_t isere_mem_free(void);
size_t isere_mem_min_free(void);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_MEM_H_ */

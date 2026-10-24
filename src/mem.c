#include "mem.h"

#include <string.h>

#include "FreeRTOS.h"

// 16 bytes keeps the payload as aligned as the underlying allocator's blocks
#define HEADER_SIZE 16

static isere_mem_stats_t __stats = { 0 };
static isere_mem_account_t *__account = NULL;

void isere_mem_init(size_t heap_size)
{
  memset(&__stats, 0, sizeof(__stats));
  __stats.heap_size = heap_size;
}

static inline size_t *__header(const void *ptr)
{
  return (size_t *)((uint8_t *)ptr - HEADER_SIZE);
}

static inline void __on_alloc(size_t size)
{
  __stats.in_use += size;
  __stats.allocations++;
  if (__stats.in_use > __stats.peak_in_use) {
    __stats.peak_in_use = __stats.in_use;
  }
  if (__account != NULL) {
    __account->allocated += size;
    __account->net += (int64_t)size;
    if (__stats.in_use > __account->peak_in_use) {
      __account->peak_in_use = __stats.in_use;
    }
  }
}

static inline void __on_free(size_t size)
{
  __stats.in_use -= size;
  if (__account != NULL) {
    __account->net -= (int64_t)size;
  }
}

void *isere_malloc(size_t size)
{
  if (size == 0) {
    return NULL;
  }
  // the budget makes the Linux build run out of memory where the board would
  if (__stats.heap_size != 0 && __stats.in_use + size > __stats.heap_size) {
    return NULL;
  }
  if (size > (size_t)-1 - HEADER_SIZE) {
    return NULL;
  }

  uint8_t *block = pvPortMalloc(size + HEADER_SIZE);
  if (block == NULL) {
    return NULL;
  }

  *(size_t *)block = size;
  __on_alloc(size);
  return block + HEADER_SIZE;
}

void *isere_calloc(size_t count, size_t size)
{
  if (count != 0 && size > (size_t)-1 / count) {
    return NULL;
  }

  void *ptr = isere_malloc(count * size);
  if (ptr != NULL) {
    memset(ptr, 0, count * size);
  }
  return ptr;
}

void isere_free(void *ptr)
{
  if (ptr == NULL) {
    return;
  }

  size_t *header = __header(ptr);
  __on_free(*header);
  vPortFree(header);
}

void *isere_realloc(void *ptr, size_t size)
{
  if (ptr == NULL) {
    return isere_malloc(size);
  }
  if (size == 0) {
    isere_free(ptr);
    return NULL;
  }

  size_t old_size = *__header(ptr);
  if (size <= old_size && size >= old_size / 2) {
    return ptr;
  }

  // heap_4 on the board has no realloc, so do it the portable way
  void *new_ptr = isere_malloc(size);
  if (new_ptr == NULL) {
    return NULL;
  }
  memcpy(new_ptr, ptr, old_size < size ? old_size : size);
  isere_free(ptr);
  return new_ptr;
}

size_t isere_malloc_usable_size(const void *ptr)
{
  return ptr != NULL ? *__header(ptr) : 0;
}

void isere_mem_set_account(isere_mem_account_t *account)
{
  __account = account;
  if (account != NULL && __stats.in_use > account->peak_in_use) {
    account->peak_in_use = __stats.in_use;
  }
}

isere_mem_account_t *isere_mem_get_account(void)
{
  return __account;
}

void isere_mem_account_external(int64_t delta)
{
  if (__account == NULL) {
    return;
  }
  if (delta > 0) {
    __account->allocated += (uint64_t)delta;
  }
  __account->net += delta;
}

void isere_mem_get_stats(isere_mem_stats_t *stats)
{
  *stats = __stats;
}

size_t isere_mem_free(void)
{
  if (__stats.heap_size == 0) {
    return 0;
  }
  return __stats.heap_size - __stats.in_use;
}

size_t isere_mem_min_free(void)
{
  if (__stats.heap_size == 0) {
    return 0;
  }
  return __stats.heap_size - __stats.peak_in_use;
}

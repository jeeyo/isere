#ifndef ISERE_PVPORTREALLOC_H_
#define ISERE_PVPORTREALLOC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

void *pvPortRealloc(void *pv, size_t xWantedSize);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_PVPORTREALLOC_H_ */

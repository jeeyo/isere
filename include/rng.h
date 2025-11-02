#ifndef ISERE_RNG_H_
#define ISERE_RNG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef void * isere_rng_t;

int isere_rng_init(isere_rng_t *rng);
int isere_rng_get_random_fast(isere_rng_t *rng, void *buf, uint32_t buflen);
int isere_rng_get_random(isere_rng_t *rng, void *buf, uint32_t buflen);
void isere_rng_deinit(isere_rng_t *rng);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_RNG_H_ */

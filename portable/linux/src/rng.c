#include "rng.h"

#include <sys/random.h>

int isere_rng_init(isere_rng_t *rng)
{
  return 0;
}

void isere_rng_deinit(isere_rng_t *rng)
{
  return 0;
}

uint32_t isere_rng_get_random_fast(isere_rng_t *rng, void *buf, uint32_t buflen)
{
  return getrandom(buf, buflen, 0);
}

uint32_t isere_rng_get_random(isere_rng_t *rng, void *buf, uint32_t buflen)
{
  return getrandom(buf, buflen, GRND_RANDOM);
}

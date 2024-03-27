#include "isere.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static isere_t *__isere = NULL;

int rtc_init(isere_t *isere, isere_rtc_t *rtc)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  return 0;
}

void rtc_deinit(isere_rtc_t *rtc)
{
  if (__isere) {
    __isere = NULL;
  }

  return;
}

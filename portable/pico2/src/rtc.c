#include "rtc.h"

static isere_t *__isere = NULL;

int isere_rtc_init(isere_t *isere, isere_rtc_t *rtc)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  return 0;
}

void isere_rtc_deinit(isere_rtc_t *rtc)
{
  if (__isere) {
    __isere = NULL;
  }

  return;
}


int isere_rtc_get_datetime(isere_rtc_t *rtc, rtc_datetime_t *datetime)
{
  return 0;
}

int isere_rtc_set_datetime(isere_rtc_t *rtc, rtc_datetime_t *datetime)
{
  return 0;
}

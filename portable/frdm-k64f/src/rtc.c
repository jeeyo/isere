#include "rtc.h"

int isere_rtc_init(isere_rtc_t *rtc)
{
  return 0;
}

void isere_rtc_deinit(isere_rtc_t *rtc)
{
  return;
}

int isere_rtc_unix_to_datetime(isere_rtc_t *rtc, uint64_t unixtimestamp, rtc_datetime_t *datetime)
{
  return 0;
}

uint64_t isere_rtc_get_unix_timestamp(isere_rtc_t *rtc)
{
  return 0;
}

int isere_rtc_set_datetime(isere_rtc_t *rtc, const rtc_datetime_t *datetime)
{
  return 0;
}

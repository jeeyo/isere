#include "rtc.h"

#include <time.h>

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
  struct tm *timeinfo;

  timeinfo = localtime(&unixtimestamp);

  datetime->year = timeinfo->tm_year + 1900;
  datetime->month = timeinfo->tm_mon + 1;
  datetime->day = timeinfo->tm_mday;
  datetime->dotw = timeinfo->tm_wday;
  datetime->hour = timeinfo->tm_hour;
  datetime->min = timeinfo->tm_min;
  datetime->sec = timeinfo->tm_sec;
  return 0;
}

uint64_t isere_rtc_get_unix_timestamp(isere_rtc_t *rtc)
{
  time_t rawtime;
  time(&rawtime);
  return rawtime;
}

int isere_rtc_set_datetime(isere_rtc_t *rtc, const rtc_datetime_t *datetime)
{
  return 0;
}

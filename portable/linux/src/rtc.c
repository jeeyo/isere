#include "rtc.h"

#include "time.h"

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

int rtc_get_datetime(isere_rtc_t *rtc, rtc_datetime_t *datetime)
{
  time_t rawtime;
  struct tm *timeinfo;

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  datetime->year = timeinfo->tm_year + 1900;
  datetime->month = timeinfo->tm_mon + 1;
  datetime->day = timeinfo->tm_mday;
  datetime->dotw = timeinfo->tm_wday;
  datetime->hour = timeinfo->tm_hour;
  datetime->min = timeinfo->tm_min;
  datetime->sec = timeinfo->tm_sec;
  return 0;
}

int rtc_set_datetime(isere_rtc_t *rtc, rtc_datetime_t *datetime)
{
  __isere->logger->warning(ISERE_RTC_LOG_TAG, "rtc_set_datetime() is not implemented");
  return 0;
}

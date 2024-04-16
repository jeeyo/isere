#include "rtc.h"

#include "hardware/rtc.h"
#include "pico/util/datetime.h"

static isere_t *__isere = NULL;

int isere_rtc_init(isere_t *isere, isere_rtc_t *rtc)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  rtc_init();

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
  datetime_t t;
  rtc_get_datetime(&t);

  datetime->year = t.year;
  datetime->month = t.month;
  datetime->day = t.day;
  datetime->dotw = t.dotw;
  datetime->hour = t.hour;
  datetime->min = t.min;
  datetime->sec = t.sec;

  return 0;
}

int isere_rtc_set_datetime(isere_rtc_t *rtc, rtc_datetime_t *datetime)
{
  datetime_t t = {
    .year  = datetime->year,
    .month = datetime->month,
    .day   = datetime->day,
    .dotw  = datetime->dotw,
    .hour  = datetime->hour,
    .min   = datetime->min,
    .sec   = datetime->sec
  };

  rtc_set_datetime(&t);

  // clk_sys is >2000x faster than clk_rtc, so datetime is not updated immediately when rtc_get_datetime() is called.
  // tbe delay is up to 3 RTC clock cycles (which is 64us with the default clock settings)
  sleep_us(64);

  return 0;
}

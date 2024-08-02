#ifndef ISERE_RTC_H_
#define ISERE_RTC_H_

#include "isere.h"

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ISERE_RTC_LOG_TAG "rtc"

typedef struct {
  int16_t year;    ///< 0..4095
  int8_t month;    ///< 1..12, 1 is January
  int8_t day;      ///< 1..28,29,30,31 depending on month
  int8_t dotw;     ///< 0..6, 0 is Sunday
  int8_t hour;     ///< 0..23
  int8_t min;      ///< 0..59
  int8_t sec;      ///< 0..59
} rtc_datetime_t;

int isere_rtc_init(isere_t *isere, isere_rtc_t *rtc);
int isere_rtc_get_datetime(isere_rtc_t *rtc, rtc_datetime_t *datetime);
int isere_rtc_set_datetime(isere_rtc_t *rtc, rtc_datetime_t *datetime);
void isere_rtc_deinit(isere_rtc_t *rtc);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_RTC_H_ */

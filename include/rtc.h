#ifndef ISERE_RTC_H_
#define ISERE_RTC_H_

#include "isere.h"

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ISERE_RTC_LOG_TAG "rtc"

int rtc_init(isere_t *isere, isere_rtc_t *rtc);
void rtc_deinit(isere_rtc_t *rtc);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_RTC_H_ */

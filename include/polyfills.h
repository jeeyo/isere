#ifndef ISERE_POLYFILLS_H
#define ISERE_POLYFILLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

void isere_js_polyfill_timer_init(isere_js_t *js);
void isere_js_polyfill_timer_deinit(isere_js_t *js);
int isere_js_polyfill_timer_poll(isere_js_t *js);

void isere_js_polyfill_fetch_init(isere_js_t *js);
void isere_js_polyfill_fetch_deinit(isere_js_t *js);
int isere_js_polyfill_fetch_poll(isere_js_t *js);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_POLYFILLS_H */

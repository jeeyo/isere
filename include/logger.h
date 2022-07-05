#ifndef LOGGER_H_

#define LOGGER_H_

#ifdef __cplusplus
extern "C" {
#endif

void logger_init(void);
void logger_debug(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H_ */

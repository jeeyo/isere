#ifndef ISERE_CONFIG_H_
#define ISERE_CONFIG_H_

// lwIP sockets and TCP PCBs are sized for this (lwipopts.h)
#define ISERE_TCP_MAX_CONNECTIONS           4

// in words (FreeRTOS stack depth); 20 KB, of which ISERE_JS_STACK_KB (14) may go to the
// engine. Endless recursion, stopped at a 16 KB JS limit, peaked at 17.5 KB.
#define ISERE_HTTPD_TASK_STACK_SIZE         (1024 * 5)
#define ISERE_OTEL_TASK_STACK_SIZE          (1024 * 2)

#endif /* ISERE_CONFIG_H_ */

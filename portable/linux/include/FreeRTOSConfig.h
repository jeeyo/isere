#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS configuration for isère's Linux build (the FreeRTOS POSIX port).
 *
 * Only what isère needs, and only where it differs from FreeRTOS's defaults
 * (include/FreeRTOS.h). PORTING.md (FreeRTOS configuration) classifies every setting as
 * essential or effective, says what breaks without it, and gives the
 * measurements behind the tunable ones.
 *
 * isère uses very little of the kernel: two tasks (httpd, and FreeRTOS's own
 * idle task), vTaskDelay(), one mutex in the logger, and pvPortMalloc(). All
 * networking, timers and JS run on the httpd task's event loop. */

#include <assert.h>

#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 16384
#endif /* PTHREAD_STACK_MIN */

/* --- essential: required by FreeRTOS.h, or isère does not work without them --- */

/* required; the httpd task must be preempted for the idle task to clean up after
 * deleted tasks, and on the board for the USB task to run while JS is busy */
#ifndef configUSE_PREEMPTION
#define configUSE_PREEMPTION                    1
#endif

/* required; isère has no use for either hook */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0

/* required; isère's tasks use priorities up to tskIDLE_PRIORITY + 3 */
#define configMAX_PRIORITIES                    4

/* required; the POSIX port gives every task a pthread stack of at least
 * PTHREAD_STACK_MIN bytes whatever is asked for, so this only sizes the idle task */
#define configMINIMAL_STACK_SIZE                ( ( configSTACK_DEPTH_TYPE ) PTHREAD_STACK_MIN )
#define configSTACK_DEPTH_TYPE                  uint32_t

/* FreeRTOS defaults to 16-bit ticks, which wrap after 65 s at 1 kHz */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* the logger serialises output with a mutex */
#define configUSE_MUTEXES                       1

/* httpd waits for the network with vTaskDelay() and deletes itself at shutdown */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1

/* kernel misuse fails loudly in every build */
#define configASSERT( x )                       assert( x )

/* --- effective: tunable, measured in PORTING.md (FreeRTOS configuration) --- */

/* The tick is a SIGALRM that interrupts the httpd task, also while it waits in
 * poll(). JS timers wait in poll(), not on ticks, so here the tick only costs
 * idle CPU: 1.5% of a core at 100 Hz against 4.1% at 1000 Hz, same throughput. */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      ( 100 )
#endif

#endif /* FREERTOS_CONFIG_H */

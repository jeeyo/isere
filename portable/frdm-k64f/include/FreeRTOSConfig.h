#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS configuration for isère on the NXP FRDM-K64F (MK64FN1M0VLL12,
 * Cortex-M4F at 120 MHz, the GCC ARM_CM4F port, single core).
 *
 * PORTING.md (FreeRTOS configuration) classifies every setting as essential or
 * effective. Settings at FreeRTOS's defaults are left out. Besides isère's httpd
 * task this configuration serves lwIP's tcpip thread and its FreeRTOS sys_arch,
 * and the Ethernet receive task (src/ethernetif.c). */

#include <assert.h>
#include <stdint.h>

/* --- essential: the Cortex-M4F port --- */

extern uint32_t SystemCoreClock;
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )

/* the K64F implements 4 priority bits; interrupts at 5..15 may call FreeRTOS
 * FromISR APIs (the ENET interrupts run at 6), 0..4 are never masked */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* the vector table (NXP's startup_MK64F12.S) names the handlers the CMSIS way */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

/* --- essential: what isère and its libraries use --- */

/* required; lwIP's tcpip thread and the Ethernet task must run while the httpd task is busy in JS */
#define configUSE_PREEMPTION                    1

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0

/* required; lwIP's tcpip thread runs at tskIDLE_PRIORITY + 4, the Ethernet task at + 5 */
#define configMAX_PRIORITIES                    7

/* sizes the idle task, in words */
#define configMINIMAL_STACK_SIZE                ( ( configSTACK_DEPTH_TYPE ) 128 )

#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* the logger and lwIP's sys_arch take mutexes; lwIP's semaphores may count */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1

/* heap_5: SRAM_L and the rest of SRAM_U, registered by platform_init() */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   0

/* run-time stats in microseconds from the PIT's 64-bit lifetime timer, for the
 * device CPU utilisation metric (platform_busy_cpu_ns(): everything but idle) */
extern uint64_t platform_now_us( void );
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()        platform_now_us()
#define INCLUDE_xTaskGetIdleTaskHandle          1

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* kernel misuse fails loudly in every build */
#define configASSERT( x )                       assert( x )
#define configCHECK_FOR_STACK_OVERFLOW          2

/* --- effective: tunable --- */

/* lwIP's timeouts, and so the event loop's poll() timeout, count ticks:
 * 1 ms resolution keeps a 1 ms JS timer at 1 ms */
#define configTICK_RATE_HZ                      ( 1000 )

/* stack high-water marks, to size stacks from measurements */
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#endif /* FREERTOS_CONFIG_H */

#include "isere.h"

#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#if (configSUPPORT_DYNAMIC_ALLOCATION == 0)
  #error This file must not be used if configSUPPORT_DYNAMIC_ALLOCATION is 0
#endif

void *pvPortRealloc(void * pv, size_t xWantedSize)
{
  vTaskSuspendAll();
  {
    pv = realloc(pv, xWantedSize);
    traceFREE(pv, 0);
    traceMALLOC(pv, xWantedSize);
  }
  (void)xTaskResumeAll();

#if configUSE_MALLOC_FAILED_HOOK == 1
  if (pv == NULL) {
    vApplicationMallocFailedHook();
  }
#endif /* configUSE_MALLOC_FAILED_HOOK */

  return pv;
}

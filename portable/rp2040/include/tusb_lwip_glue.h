#ifndef _TUSB_LWIP_GLUE_H_
#define _TUSB_LWIP_GLUE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "tusb.h"
#include "dhserver.h"
#include "dnserver.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"

void rndis_tusb_init();
void lwip_freertos_init(void);
void lwip_add_netif();
void wait_for_netif_is_up();
void dhcpd_init();

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_LWIP_GLUE_H_ */

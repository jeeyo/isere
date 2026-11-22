#ifndef LWIP_ARCH_CC_H_
#define LWIP_ARCH_CC_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

#define LWIP_PLATFORM_DIAG(x)    do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x)  do { printf("lwIP assertion \"%s\" failed at %s:%d\n", x, __FILE__, __LINE__); abort(); } while (0)

// random numbers from the K64F's RNGA (board.c)
uint32_t board_rand32(void);
#define LWIP_RAND() board_rand32()

#endif /* LWIP_ARCH_CC_H_ */

#ifndef ISERE_QUICKJS_COMPAT_H_
#define ISERE_QUICKJS_COMPAT_H_

// force-included into quickjs-ng sources on RP2350 (see platform_extras.cmake)
// quickjs-ng assumes a POSIX host, newlib leaves a few gaps

#include <inttypes.h>

// some arm-none-eabi toolchains ship a freestanding stdint.h that hides newlib's format macros
#ifndef PRId64
#define PRId64 "lld"
#endif
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRIx64
#define PRIx64 "llx"
#endif

#endif /* ISERE_QUICKJS_COMPAT_H_ */

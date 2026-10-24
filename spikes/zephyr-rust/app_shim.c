/*
 * Shim for Zephyr kernel functions that Rust can't link to directly.
 *
 * In Zephyr, __syscall functions expand to static inline wrappers when
 * CONFIG_USERSPACE is disabled (the common embedded case). These thin
 * non-inline wrappers give Rust FFI a real linkable symbol.
 */

#include <zephyr/kernel.h>

int64_t isere_k_uptime_get(void)
{
    return k_uptime_get();
}

int32_t isere_k_msleep(int32_t ms)
{
    return k_msleep(ms);
}

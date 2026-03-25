// Real-time clock abstraction over Zephyr's kernel timing.
//
// Uses k_uptime_get() for monotonic time (milliseconds since boot).

extern "C" {
    fn k_uptime_get() -> i64;
}

/// Get milliseconds since system boot.
pub fn uptime_ms() -> u64 {
    unsafe { k_uptime_get() as u64 }
}

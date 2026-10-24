// Logging abstraction over Zephyr's printk / LOG_MODULE.
//
// For the initial implementation, we use printk directly.
// A future refinement would use Zephyr's LOG_MODULE macros via FFI.

use core::ffi::c_char;

extern "C" {
    fn printk(fmt: *const c_char, ...);
}

pub fn info(tag: &str, msg: &str) {
    // We use a simple format since variadic FFI is tricky from Rust.
    // For production, consider using the zephyr crate's logging facilities.
    unsafe {
        printk(
            b"[%s] %s\n\0".as_ptr() as *const c_char,
            tag.as_ptr() as *const c_char,
            msg.as_ptr() as *const c_char,
        );
    }
}

pub fn error(tag: &str, msg: &str) {
    unsafe {
        printk(
            b"[%s] ERROR: %s\n\0".as_ptr() as *const c_char,
            tag.as_ptr() as *const c_char,
            msg.as_ptr() as *const c_char,
        );
    }
}

/// Log using Zephyr printk with a null-terminated format string.
/// Safety: format string must be null-terminated.
pub fn printk_str(s: &[u8]) {
    unsafe {
        printk(b"%s\0".as_ptr() as *const c_char, s.as_ptr() as *const c_char);
    }
}

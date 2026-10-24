// JavaScript polyfills for the Zephyr environment.
//
// Provides setTimeout/clearTimeout using a cooperative timer model.
// Timers are tracked with expiry timestamps and fired during the JS poll loop
// using Zephyr's k_uptime_get() for monotonic time.
//
// This mirrors src/polyfills/quickjs/timer.c from the C codebase,
// adapted from FreeRTOS xTimerCreate to a poll-based model.

use core::ffi::{c_char, c_int, c_void};
use core::ptr;

use super::quickjs_ffi as qjs;

const MAX_TIMERS: usize = 8;

/// A pending setTimeout timer.
struct Timer {
    active: bool,
    /// Expiry time in milliseconds (from k_uptime_get).
    expiry_ms: i64,
    /// The JS callback function (ref-counted via JS_DupValue).
    func: qjs::JSValue,
    /// The JS context this timer belongs to.
    ctx: *mut qjs::JSContext,
}

impl Timer {
    const fn empty() -> Self {
        Self {
            active: false,
            expiry_ms: 0,
            func: qjs::JSValue::UNDEFINED,
            ctx: ptr::null_mut(),
        }
    }
}

/// Timer state embedded in the JS context opaque data.
/// Must be allocated and accessible for the lifetime of the JsContext.
pub struct TimerState {
    timers: [Timer; MAX_TIMERS],
    class_id: qjs::JSClassID,
}

impl TimerState {
    pub const fn new() -> Self {
        Self {
            timers: [
                Timer::empty(),
                Timer::empty(),
                Timer::empty(),
                Timer::empty(),
                Timer::empty(),
                Timer::empty(),
                Timer::empty(),
                Timer::empty(),
            ],
            class_id: 0,
        }
    }

    /// Register setTimeout/clearTimeout on the global object.
    /// Must be called after the JS context is created.
    pub fn init(&mut self, rt: *mut qjs::JSRuntime, ctx: *mut qjs::JSContext) {
        unsafe {
            // Create a custom class for timer handles
            qjs::JS_NewClassID(&mut self.class_id);

            let class_def = qjs::JSClassDef {
                class_name: b"Timer\0".as_ptr() as *const c_char,
                finalizer: None,
                gc_mark: None,
                call: None,
                exotic: ptr::null(),
            };
            qjs::JS_NewClass(rt, self.class_id, &class_def);

            let global = qjs::JS_GetGlobalObject(ctx);

            qjs::JS_SetPropertyStr(
                ctx,
                global,
                b"setTimeout\0".as_ptr() as *const c_char,
                qjs::JS_NewCFunction(ctx, js_set_timeout, b"setTimeout\0".as_ptr() as *const c_char, 2),
            );
            qjs::JS_SetPropertyStr(
                ctx,
                global,
                b"clearTimeout\0".as_ptr() as *const c_char,
                qjs::JS_NewCFunction(ctx, js_clear_timeout, b"clearTimeout\0".as_ptr() as *const c_char, 1),
            );

            qjs::JS_FreeValue(ctx, global);
        }
    }

    /// Poll all timers, firing any that have expired.
    /// Returns true if any timers are still active (caller should keep polling).
    pub fn poll(&mut self) -> bool {
        let now = uptime_ms();
        let mut any_active = false;

        for i in 0..MAX_TIMERS {
            if !self.timers[i].active {
                continue;
            }

            if now >= self.timers[i].expiry_ms {
                // Timer expired — fire the callback
                let ctx = self.timers[i].ctx;
                let func = self.timers[i].func;

                // Mark inactive before calling (callback may free handler)
                self.timers[i].active = false;

                unsafe {
                    let func_dup = qjs::isere_JS_DupValue(ctx, func);
                    let ret = qjs::JS_Call(ctx, func_dup, qjs::JSValue::UNDEFINED, 0, ptr::null());
                    qjs::JS_FreeValue(ctx, func_dup);
                    if ret.is_exception() {
                        // Log but don't crash
                        qjs::JS_FreeValue(ctx, ret);
                    } else {
                        qjs::JS_FreeValue(ctx, ret);
                    }

                    // Release the stored reference
                    qjs::JS_FreeValue(ctx, func);
                }
                self.timers[i].func = qjs::JSValue::UNDEFINED;
            } else {
                any_active = true;
            }
        }

        any_active
    }

    /// Clean up all active timers.
    pub fn deinit(&mut self, ctx: *mut qjs::JSContext) {
        for i in 0..MAX_TIMERS {
            if self.timers[i].active {
                unsafe {
                    qjs::JS_FreeValue(ctx, self.timers[i].func);
                }
                self.timers[i].active = false;
                self.timers[i].func = qjs::JSValue::UNDEFINED;
            }
        }
    }

    /// Allocate a free timer slot. Returns index or None.
    fn alloc_slot(&mut self) -> Option<usize> {
        for i in 0..MAX_TIMERS {
            if !self.timers[i].active {
                return Some(i);
            }
        }
        None
    }
}

extern "C" {
    fn isere_k_uptime_get() -> i64;
}

fn uptime_ms() -> i64 {
    unsafe { isere_k_uptime_get() }
}

/// Global timer state pointer — set by JsContext before eval, read by callbacks.
/// Safe because we run single-threaded on Zephyr.
static mut TIMER_STATE: *mut TimerState = ptr::null_mut();

/// Set the global timer state pointer (called by JsContext).
pub unsafe fn set_global_timer_state(state: *mut TimerState) {
    TIMER_STATE = state;
}

/// setTimeout(callback, delay_ms) -> Timer handle
unsafe extern "C" fn js_set_timeout(
    ctx: *mut qjs::JSContext,
    _this_val: qjs::JSValue,
    argc: c_int,
    argv: *const qjs::JSValue,
) -> qjs::JSValue {
    if argc < 2 {
        return qjs::JS_ThrowTypeError(ctx, b"setTimeout requires 2 arguments\0".as_ptr() as *const c_char);
    }

    let func = *argv;
    if qjs::JS_IsFunction(ctx, func) == 0 {
        return qjs::JS_ThrowTypeError(ctx, b"not a function\0".as_ptr() as *const c_char);
    }

    let mut delay: i64 = 0;
    if qjs::JS_ToInt64(ctx, &mut delay, *argv.add(1)) < 0 {
        return qjs::JS_ThrowTypeError(ctx, b"not a number\0".as_ptr() as *const c_char);
    }

    let state = &mut *TIMER_STATE;

    let slot = match state.alloc_slot() {
        Some(s) => s,
        None => {
            return qjs::JS_ThrowInternalError(ctx, b"exceeded timer limit\0".as_ptr() as *const c_char);
        }
    };

    state.timers[slot].active = true;
    state.timers[slot].expiry_ms = uptime_ms() + delay;
    state.timers[slot].func = qjs::isere_JS_DupValue(ctx, func);
    state.timers[slot].ctx = ctx;

    // Return a Timer object with the slot index as opaque data
    let obj = qjs::JS_NewObjectClass(ctx, state.class_id as c_int);
    if obj.is_exception() {
        // Clean up
        qjs::JS_FreeValue(ctx, state.timers[slot].func);
        state.timers[slot].active = false;
        state.timers[slot].func = qjs::JSValue::UNDEFINED;
        return obj;
    }

    qjs::JS_SetOpaque(obj, slot as *mut c_void);
    obj
}

/// clearTimeout(timerHandle)
unsafe extern "C" fn js_clear_timeout(
    ctx: *mut qjs::JSContext,
    _this_val: qjs::JSValue,
    argc: c_int,
    argv: *const qjs::JSValue,
) -> qjs::JSValue {
    if argc < 1 {
        return qjs::JSValue::UNDEFINED;
    }

    let state = &mut *TIMER_STATE;
    let slot_ptr = qjs::JS_GetOpaque2(ctx, *argv, state.class_id);
    if slot_ptr.is_null() {
        return qjs::JSValue::UNDEFINED;
    }

    let slot = slot_ptr as usize;
    if slot < MAX_TIMERS && state.timers[slot].active {
        qjs::JS_FreeValue(ctx, state.timers[slot].func);
        state.timers[slot].active = false;
        state.timers[slot].func = qjs::JSValue::UNDEFINED;
    }

    qjs::JSValue::UNDEFINED
}

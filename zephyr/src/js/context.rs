// Safe Rust wrapper around QuickJS runtime and context.
//
// Provides lifecycle management, handler evaluation, and response extraction.
// Mirrors the logic in src/runtimes/quickjs/quickjs.c from the C codebase.

use core::ffi::{c_char, c_int, c_void};
use core::ptr;

use super::quickjs_ffi as qjs;
use crate::httpd::{HttpRequest, HttpResponse, MAX_HEADERS, MAX_RESPONSE_BODY_LEN};

/// Custom allocator that bridges QuickJS to Zephyr's k_malloc/k_free.
mod alloc {
    use core::ffi::c_void;
    use super::qjs;

    extern "C" {
        fn k_malloc(size: usize) -> *mut c_void;
        fn k_free(ptr: *mut c_void);
    }

    // Simple realloc using k_malloc + copy + k_free.
    // Zephyr doesn't provide a native realloc, so we implement one.
    unsafe extern "C" fn js_zephyr_malloc(s: *mut qjs::JSMallocState, size: usize) -> *mut c_void {
        if size == 0 {
            return ptr::null_mut();
        }
        let st = &mut *s;
        if st.malloc_size + size > st.malloc_limit {
            return ptr::null_mut();
        }
        // Allocate extra space to store the size for realloc
        let ptr = k_malloc(size + core::mem::size_of::<usize>());
        if ptr.is_null() {
            return ptr::null_mut();
        }
        // Store size at the beginning
        *(ptr as *mut usize) = size;
        st.malloc_count += 1;
        st.malloc_size += size;
        (ptr as *mut u8).add(core::mem::size_of::<usize>()) as *mut c_void
    }

    unsafe extern "C" fn js_zephyr_free(s: *mut qjs::JSMallocState, ptr: *mut c_void) {
        if ptr.is_null() {
            return;
        }
        let real_ptr = (ptr as *mut u8).sub(core::mem::size_of::<usize>()) as *mut c_void;
        let size = *(real_ptr as *const usize);
        let st = &mut *s;
        st.malloc_count -= 1;
        st.malloc_size -= size;
        k_free(real_ptr);
    }

    unsafe extern "C" fn js_zephyr_realloc(
        s: *mut qjs::JSMallocState,
        ptr: *mut c_void,
        size: usize,
    ) -> *mut c_void {
        if ptr.is_null() {
            return js_zephyr_malloc(s, size);
        }
        if size == 0 {
            js_zephyr_free(s, ptr);
            return ptr::null_mut();
        }

        let real_ptr = (ptr as *mut u8).sub(core::mem::size_of::<usize>()) as *mut c_void;
        let old_size = *(real_ptr as *const usize);

        let new_ptr = js_zephyr_malloc(s, size);
        if new_ptr.is_null() {
            return ptr::null_mut();
        }

        let copy_size = if old_size < size { old_size } else { size };
        core::ptr::copy_nonoverlapping(ptr as *const u8, new_ptr as *mut u8, copy_size);

        js_zephyr_free(s, ptr);
        new_ptr
    }

    use core::ptr;

    pub static MALLOC_FUNCS: qjs::JSMallocFunctions = qjs::JSMallocFunctions {
        js_malloc: Some(js_zephyr_malloc),
        js_free: Some(js_zephyr_free),
        js_realloc: Some(js_zephyr_realloc),
        js_malloc_usable_size: None,
    };
}

/// A QuickJS context bound to a single HTTP request.
/// Each request gets its own runtime+context (same pattern as the C code).
pub struct JsContext {
    runtime: *mut qjs::JSRuntime,
    context: *mut qjs::JSContext,
    future: qjs::JSValue,
    pub response_ready: bool,
}

/// Opaque data attached to a JS context for the handler callback.
/// Points back to the response object to fill in.
#[repr(C)]
struct ContextOpaque {
    response: *mut HttpResponse,
}

impl JsContext {
    /// Create a new JS runtime + context with Zephyr allocator.
    pub fn new() -> Option<Self> {
        let runtime = unsafe { qjs::JS_NewRuntime2(&alloc::MALLOC_FUNCS, ptr::null_mut()) };
        if runtime.is_null() {
            return None;
        }

        let context = unsafe { qjs::JS_NewContextRaw(runtime) };
        if context.is_null() {
            unsafe { qjs::JS_FreeRuntime(runtime) };
            return None;
        }

        // Add standard intrinsics
        unsafe {
            qjs::JS_AddIntrinsicBaseObjects(context);
            qjs::JS_AddIntrinsicDate(context);
            qjs::JS_AddIntrinsicEval(context);
            qjs::JS_AddIntrinsicJSON(context);
            qjs::JS_AddIntrinsicPromise(context);
        }

        Some(Self {
            runtime,
            context,
            future: qjs::JSValue::UNDEFINED,
            response_ready: false,
        })
    }

    /// Set up the global environment (console, process.env, event, context, cb).
    pub fn setup_globals(&mut self, request: &HttpRequest, response: &mut HttpResponse) {
        unsafe {
            let ctx = self.context;
            let global = qjs::JS_GetGlobalObject(ctx);

            // console.log/warn/error
            let console = qjs::JS_NewObject(ctx);
            qjs::JS_SetPropertyStr(
                ctx,
                console,
                b"log\0".as_ptr() as *const c_char,
                qjs::JS_NewCFunction(ctx, console_log, b"log\0".as_ptr() as *const c_char, 1),
            );
            qjs::JS_SetPropertyStr(
                ctx,
                console,
                b"warn\0".as_ptr() as *const c_char,
                qjs::JS_NewCFunction(ctx, console_warn, b"warn\0".as_ptr() as *const c_char, 1),
            );
            qjs::JS_SetPropertyStr(
                ctx,
                console,
                b"error\0".as_ptr() as *const c_char,
                qjs::JS_NewCFunction(ctx, console_error, b"error\0".as_ptr() as *const c_char, 1),
            );
            qjs::JS_SetPropertyStr(ctx, global, b"console\0".as_ptr() as *const c_char, console);

            // process.env
            let process = qjs::JS_NewObject(ctx);
            let env = qjs::JS_NewObject(ctx);
            qjs::JS_SetPropertyStr(
                ctx,
                env,
                b"NODE_ENV\0".as_ptr() as *const c_char,
                qjs::JS_NewString(ctx, b"production\0".as_ptr() as *const c_char),
            );
            qjs::JS_SetPropertyStr(ctx, process, b"env\0".as_ptr() as *const c_char, env);
            qjs::JS_SetPropertyStr(ctx, global, b"process\0".as_ptr() as *const c_char, process);

            // event object (HTTP request)
            let event = qjs::JS_NewObject(ctx);

            // Method - need null-terminated string
            let method_str = request.method_str();
            let mut method_buf = [0u8; 16];
            let mlen = method_str.len().min(15);
            method_buf[..mlen].copy_from_slice(&method_str.as_bytes()[..mlen]);
            qjs::JS_SetPropertyStr(
                ctx,
                event,
                b"httpMethod\0".as_ptr() as *const c_char,
                qjs::JS_NewString(ctx, method_buf.as_ptr() as *const c_char),
            );

            // Path
            let mut path_buf = [0u8; 258];
            let plen = request.path_len.min(257);
            path_buf[..plen].copy_from_slice(&request.path[..plen]);
            qjs::JS_SetPropertyStr(
                ctx,
                event,
                b"path\0".as_ptr() as *const c_char,
                qjs::JS_NewString(ctx, path_buf.as_ptr() as *const c_char),
            );

            // Headers
            let headers = qjs::JS_NewObject(ctx);
            for i in 0..request.num_headers {
                let h = &request.headers[i];
                let mut name_buf = [0u8; 66];
                let nlen = h.name_len.min(65);
                name_buf[..nlen].copy_from_slice(&h.name[..nlen]);

                let mut val_buf = [0u8; 514];
                let vlen = h.value_len.min(513);
                val_buf[..vlen].copy_from_slice(&h.value[..vlen]);

                qjs::JS_SetPropertyStr(
                    ctx,
                    headers,
                    name_buf.as_ptr() as *const c_char,
                    qjs::JS_NewString(ctx, val_buf.as_ptr() as *const c_char),
                );
            }
            qjs::JS_SetPropertyStr(ctx, event, b"headers\0".as_ptr() as *const c_char, headers);

            // Query
            let mut query_buf = [0u8; 258];
            let qlen = request.query_len.min(257);
            query_buf[..qlen].copy_from_slice(&request.query[..qlen]);
            qjs::JS_SetPropertyStr(
                ctx,
                event,
                b"query\0".as_ptr() as *const c_char,
                qjs::JS_NewString(ctx, query_buf.as_ptr() as *const c_char),
            );

            // Body
            let body_str = request.body_str();
            let mut body_buf = [0u8; 514];
            let blen = body_str.len().min(513);
            body_buf[..blen].copy_from_slice(&body_str.as_bytes()[..blen]);

            // Try parsing body as JSON first
            let parsed_body = qjs::JS_ParseJSON(
                ctx,
                body_buf.as_ptr() as *const c_char,
                blen,
                b"<input>\0".as_ptr() as *const c_char,
            );
            if parsed_body.is_object() {
                qjs::JS_SetPropertyStr(ctx, event, b"body\0".as_ptr() as *const c_char, parsed_body);
            } else {
                qjs::JS_FreeValue(ctx, parsed_body);
                qjs::JS_SetPropertyStr(
                    ctx,
                    event,
                    b"body\0".as_ptr() as *const c_char,
                    qjs::JS_NewString(ctx, body_buf.as_ptr() as *const c_char),
                );
            }

            qjs::JS_SetPropertyStr(
                ctx,
                event,
                b"isBase64Encoded\0".as_ptr() as *const c_char,
                qjs::JSValue::FALSE,
            );
            qjs::JS_SetPropertyStr(ctx, global, b"event\0".as_ptr() as *const c_char, event);

            // Lambda context object
            let context_obj = qjs::JS_NewObject(ctx);
            qjs::JS_SetPropertyStr(
                ctx,
                context_obj,
                b"functionName\0".as_ptr() as *const c_char,
                qjs::JS_NewString(ctx, b"handler\0".as_ptr() as *const c_char),
            );
            qjs::JS_SetPropertyStr(
                ctx,
                context_obj,
                b"functionVersion\0".as_ptr() as *const c_char,
                qjs::JS_NewString(ctx, b"0.1.0\0".as_ptr() as *const c_char),
            );
            qjs::JS_SetPropertyStr(
                ctx,
                context_obj,
                b"memoryLimitInMB\0".as_ptr() as *const c_char,
                qjs::JS_NewInt32(ctx, 128),
            );
            qjs::JS_SetPropertyStr(ctx, global, b"context\0".as_ptr() as *const c_char, context_obj);

            // Store response pointer in context opaque for the callback
            let opaque = &mut ContextOpaque {
                response: response as *mut HttpResponse,
            };
            qjs::JS_SetContextOpaque(ctx, opaque as *mut ContextOpaque as *mut c_void);

            // Handler callback function
            qjs::JS_SetPropertyStr(
                ctx,
                global,
                b"cb\0".as_ptr() as *const c_char,
                qjs::JS_NewCFunction(ctx, handler_callback, b"cb\0".as_ptr() as *const c_char, 1),
            );

            qjs::JS_FreeValue(ctx, global);
        }
    }

    /// Evaluate the handler module and set up the promise chain.
    pub fn eval_handler(&mut self, handler_code: &[u8]) -> Result<(), ()> {
        unsafe {
            let ctx = self.context;

            // Compile handler as module
            let h = qjs::JS_Eval(
                ctx,
                handler_code.as_ptr() as *const c_char,
                handler_code.len(),
                b"handler\0".as_ptr() as *const c_char,
                qjs::JS_EVAL_TYPE_MODULE | qjs::JS_EVAL_FLAG_COMPILE_ONLY,
            );
            if h.is_exception() {
                qjs::JS_FreeValue(ctx, h);
                return Err(());
            }

            qjs::js_module_set_import_meta(ctx, h, 0, 0);
            qjs::JS_FreeValue(ctx, h);

            // Evaluate the import + promise chain
            let eval_code = b"import { handler } from 'handler';\
                const handler1 = new Promise(resolve => handler(event, context, resolve).then(resolve));\
                Promise.resolve(handler1).then(cb)\0";

            self.future = qjs::JS_Eval(
                ctx,
                eval_code.as_ptr() as *const c_char,
                eval_code.len() - 1, // exclude null terminator from length
                b"<isere>\0".as_ptr() as *const c_char,
                qjs::JS_EVAL_TYPE_MODULE,
            );

            if self.future.is_exception() {
                return Err(());
            }

            Ok(())
        }
    }

    /// Poll pending JS jobs (promises, timers). Returns true if jobs remain.
    pub fn poll(&mut self) -> Result<bool, ()> {
        unsafe {
            let rt = qjs::JS_GetRuntime(self.context);
            let mut ctx1: *mut qjs::JSContext = ptr::null_mut();

            let err = qjs::JS_ExecutePendingJob(rt, &mut ctx1);
            if err < 0 {
                return Err(());
            }

            Ok(err > 0)
        }
    }
}

impl Drop for JsContext {
    fn drop(&mut self) {
        unsafe {
            if !self.context.is_null() {
                qjs::JS_FreeValue(self.context, self.future);
                qjs::JS_FreeContext(self.context);
            }
            if !self.runtime.is_null() {
                qjs::JS_FreeRuntime(self.runtime);
            }
        }
    }
}

// --- C callback functions registered into JS ---

/// Handler callback: called when the JS handler returns/resolves with a response object.
/// Extracts statusCode, headers, and body from the JS response object.
unsafe extern "C" fn handler_callback(
    ctx: *mut qjs::JSContext,
    _this_val: qjs::JSValue,
    argc: c_int,
    argv: *const qjs::JSValue,
) -> qjs::JSValue {
    if argc < 1 {
        return qjs::JSValue::UNDEFINED;
    }

    let opaque = qjs::JS_GetContextOpaque(ctx) as *mut ContextOpaque;
    if opaque.is_null() {
        return qjs::JSValue::UNDEFINED;
    }
    let response = &mut *(*opaque).response;

    let response_obj = *argv;

    // statusCode
    let status_code = qjs::JS_GetPropertyStr(ctx, response_obj, b"statusCode\0".as_ptr() as *const c_char);
    if status_code.is_number() {
        let mut code: i32 = 200;
        qjs::JS_ToInt32(ctx, &mut code, status_code);
        response.status_code = code as u16;
    }
    qjs::JS_FreeValue(ctx, status_code);

    // headers
    let headers = qjs::JS_GetPropertyStr(ctx, response_obj, b"headers\0".as_ptr() as *const c_char);
    if headers.is_object() {
        let mut props: *mut qjs::JSPropertyEnum = ptr::null_mut();
        let mut props_len: u32 = 0;

        if qjs::JS_GetOwnPropertyNames(
            ctx,
            &mut props,
            &mut props_len,
            headers,
            qjs::JS_GPN_STRING_MASK | qjs::JS_GPN_ENUM_ONLY,
        ) == 0
        {
            let count = (props_len as usize).min(MAX_HEADERS);
            response.num_headers = count;

            for i in 0..count {
                let prop = &*props.add(i);

                // Header name
                let name_ptr = qjs::JS_AtomToCString(ctx, prop.atom);
                if !name_ptr.is_null() {
                    let name_bytes = cstr_to_bytes(name_ptr);
                    let n = name_bytes.len().min(response.headers[i].name.len());
                    response.headers[i].name[..n].copy_from_slice(&name_bytes[..n]);
                    response.headers[i].name_len = n;
                    qjs::JS_FreeCString(ctx, name_ptr);
                }

                // Header value
                let val = qjs::JS_GetProperty(ctx, headers, prop.atom);
                let mut val_len: usize = 0;
                let val_ptr = qjs::JS_ToCStringLen(ctx, &mut val_len, val);
                if !val_ptr.is_null() {
                    let val_bytes = core::slice::from_raw_parts(val_ptr as *const u8, val_len);
                    let v = val_len.min(response.headers[i].value.len());
                    response.headers[i].value[..v].copy_from_slice(&val_bytes[..v]);
                    response.headers[i].value_len = v;
                    qjs::JS_FreeCString(ctx, val_ptr);
                }
                qjs::JS_FreeValue(ctx, val);

                qjs::JS_FreeAtom(ctx, prop.atom);
            }
        }
    }
    qjs::JS_FreeValue(ctx, headers);

    // body
    let body = qjs::JS_GetPropertyStr(ctx, response_obj, b"body\0".as_ptr() as *const c_char);
    if body.is_object() {
        // Stringify JSON objects
        let stringified = qjs::JS_JSONStringify(ctx, body, qjs::JSValue::UNDEFINED, qjs::JSValue::UNDEFINED);
        let mut len: usize = 0;
        let ptr = qjs::JS_ToCStringLen(ctx, &mut len, stringified);
        if !ptr.is_null() {
            let n = len.min(MAX_RESPONSE_BODY_LEN);
            let bytes = core::slice::from_raw_parts(ptr as *const u8, n);
            response.body[..n].copy_from_slice(bytes);
            response.body_len = n;
            qjs::JS_FreeCString(ctx, ptr);
        }
        qjs::JS_FreeValue(ctx, stringified);
    } else if body.is_string() {
        let mut len: usize = 0;
        let ptr = qjs::JS_ToCStringLen(ctx, &mut len, body);
        if !ptr.is_null() {
            let n = len.min(MAX_RESPONSE_BODY_LEN);
            let bytes = core::slice::from_raw_parts(ptr as *const u8, n);
            response.body[..n].copy_from_slice(bytes);
            response.body_len = n;
            qjs::JS_FreeCString(ctx, ptr);
        }
    }
    qjs::JS_FreeValue(ctx, body);

    response.completed = true;
    qjs::JSValue::UNDEFINED
}

/// console.log
unsafe extern "C" fn console_log(
    ctx: *mut qjs::JSContext,
    _this_val: qjs::JSValue,
    argc: c_int,
    argv: *const qjs::JSValue,
) -> qjs::JSValue {
    log_args(ctx, argc, argv);
    qjs::JSValue::UNDEFINED
}

/// console.warn
unsafe extern "C" fn console_warn(
    ctx: *mut qjs::JSContext,
    _this_val: qjs::JSValue,
    argc: c_int,
    argv: *const qjs::JSValue,
) -> qjs::JSValue {
    log_args(ctx, argc, argv);
    qjs::JSValue::UNDEFINED
}

/// console.error
unsafe extern "C" fn console_error(
    ctx: *mut qjs::JSContext,
    _this_val: qjs::JSValue,
    argc: c_int,
    argv: *const qjs::JSValue,
) -> qjs::JSValue {
    log_args(ctx, argc, argv);
    qjs::JSValue::UNDEFINED
}

/// Log JS arguments using Zephyr printk.
unsafe fn log_args(ctx: *mut qjs::JSContext, argc: c_int, argv: *const qjs::JSValue) {
    extern "C" {
        fn printk(fmt: *const c_char, ...);
    }

    for i in 0..argc {
        if i > 0 {
            printk(b" \0".as_ptr() as *const c_char);
        }

        let val = *argv.add(i as usize);
        if val.is_object() {
            let stringified = qjs::JS_JSONStringify(ctx, val, qjs::JSValue::UNDEFINED, qjs::JSValue::UNDEFINED);
            let mut len: usize = 0;
            let ptr = qjs::JS_ToCStringLen(ctx, &mut len, stringified);
            if !ptr.is_null() {
                printk(b"%s\0".as_ptr() as *const c_char, ptr);
                qjs::JS_FreeCString(ctx, ptr);
            } else {
                printk(b"[object]\0".as_ptr() as *const c_char);
            }
            qjs::JS_FreeValue(ctx, stringified);
        } else if val.is_string() {
            let mut len: usize = 0;
            let ptr = qjs::JS_ToCStringLen(ctx, &mut len, val);
            if !ptr.is_null() {
                printk(b"%s\0".as_ptr() as *const c_char, ptr);
                qjs::JS_FreeCString(ctx, ptr);
            }
        } else if val.is_number() {
            let mut n: i32 = 0;
            qjs::JS_ToInt32(ctx, &mut n, val);
            printk(b"%d\0".as_ptr() as *const c_char, n);
        } else {
            printk(b"(unknown)\0".as_ptr() as *const c_char);
        }
    }
    printk(b"\n\0".as_ptr() as *const c_char);
}

/// Convert a C string pointer to a byte slice (without the null terminator).
unsafe fn cstr_to_bytes<'a>(ptr: *const c_char) -> &'a [u8] {
    let mut len = 0;
    while *ptr.add(len) != 0 {
        len += 1;
    }
    core::slice::from_raw_parts(ptr as *const u8, len)
}

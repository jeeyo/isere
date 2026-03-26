/*
 * Shim for QuickJS static inline functions that Rust can't link to directly.
 *
 * QuickJS defines JS_DupValue, JS_FreeValue, etc. as static inline
 * in quickjs.h. Since Rust FFI can only link to actual symbols, we
 * provide thin non-inline wrappers here.
 */

#include "quickjs.h"

JSValue isere_JS_DupValue(JSContext *ctx, JSValueConst v)
{
    return JS_DupValue(ctx, v);
}

/* JS_FreeValue is static inline in quickjs.h — provide a linkable symbol
 * for Rust FFI. Using the real name so existing extern "C" declarations work. */
void isere_JS_FreeValue(JSContext *ctx, JSValue v)
{
    JS_FreeValue(ctx, v);
}

void isere_JS_FreeValueRT(JSRuntime *rt, JSValue v)
{
    JS_FreeValueRT(rt, v);
}

/* JS_NewBool and JS_NewInt32 may also be static inline in some versions */
JSValue isere_JS_NewBool(JSContext *ctx, int val)
{
    return JS_NewBool(ctx, val);
}

JSValue isere_JS_NewInt32(JSContext *ctx, int32_t val)
{
    return JS_NewInt32(ctx, val);
}

/* JS_NewString is static inline in quickjs.h (wraps JS_NewStringLen) */
JSValue isere_JS_NewString(JSContext *ctx, const char *str)
{
    return JS_NewString(ctx, str);
}

/* JS_NewCFunction is static inline in quickjs.h (wraps JS_NewCFunction2) */
JSValue isere_JS_NewCFunction(JSContext *ctx, JSCFunction *func, const char *name, int length)
{
    return JS_NewCFunction(ctx, func, name, length);
}

/* JS_ToCStringLen is static inline in quickjs.h (wraps JS_ToCStringLen2) */
const char *isere_JS_ToCStringLen(JSContext *ctx, size_t *plen, JSValueConst val)
{
    return JS_ToCStringLen(ctx, plen, val);
}

/* JS_AtomToCString is static inline in quickjs.h (wraps JS_AtomToCStringLen) */
const char *isere_JS_AtomToCString(JSContext *ctx, JSAtom atom)
{
    return JS_AtomToCString(ctx, atom);
}

/* JS_GetProperty is static inline in quickjs.h (wraps JS_GetPropertyInternal) */
JSValue isere_JS_GetProperty(JSContext *ctx, JSValueConst this_obj, JSAtom prop)
{
    return JS_GetProperty(ctx, this_obj, prop);
}

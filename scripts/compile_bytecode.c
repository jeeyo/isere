/*
 * QuickJS bytecode compiler for isere handlers.
 *
 * Compiles a JavaScript ES module into QuickJS bytecode that can be
 * loaded at runtime with JS_ReadObject() + JS_EvalFunction(), skipping
 * the parser entirely.
 *
 * Build (on host):
 *   cc -o compile_bytecode scripts/compile_bytecode.c \
 *      c_libs/quickjs/quickjs.c c_libs/quickjs/libregexp.c \
 *      c_libs/quickjs/libunicode.c c_libs/quickjs/cutils.c \
 *      -Ic_libs/quickjs -DCONFIG_VERSION=\"0.1.0\" \
 *      -DCONFIG_BIGNUM -D_GNU_SOURCE -lm -lpthread
 *
 * For 32-bit bytecode (required for RP2350):
 *   cc -m32 -o compile_bytecode32 scripts/compile_bytecode.c \
 *      c_libs/quickjs/quickjs.c c_libs/quickjs/libregexp.c \
 *      c_libs/quickjs/libunicode.c c_libs/quickjs/cutils.c \
 *      -Ic_libs/quickjs -DCONFIG_VERSION=\"0.1.0\" \
 *      -DCONFIG_BIGNUM -D_GNU_SOURCE -lm -lpthread
 *
 * Usage:
 *   ./compile_bytecode js/handler.js js/handler.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"

static uint8_t *read_file(const char *filename, size_t *psize)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[size] = '\0';
    fclose(f);
    *psize = size;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.js> <output.bin>\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];

    /* Read input JS source */
    size_t source_len;
    uint8_t *source = read_file(input_file, &source_len);
    if (!source)
        return 1;

    /* Create QuickJS runtime and context */
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        fprintf(stderr, "Failed to create JS runtime\n");
        free(source);
        return 1;
    }

    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, "Failed to create JS context\n");
        JS_FreeRuntime(rt);
        free(source);
        return 1;
    }

    /* Compile the module (parse only, no execution) */
    JSValue obj = JS_Eval(ctx, (const char *)source, source_len,
                          "handler",
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(source);

    if (JS_IsException(obj)) {
        JSValue exc = JS_GetException(ctx);
        const char *str = JS_ToCString(ctx, exc);
        fprintf(stderr, "Compilation error: %s\n", str ? str : "(unknown)");
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    /* Serialize to bytecode */
    size_t bytecode_len;
    uint8_t *bytecode = JS_WriteObject(ctx, &bytecode_len, obj,
                                       JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);

    if (!bytecode) {
        fprintf(stderr, "Failed to serialize bytecode\n");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    /* Write bytecode to output file */
    FILE *f = fopen(output_file, "wb");
    if (!f) {
        perror(output_file);
        js_free(ctx, bytecode);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    fwrite(bytecode, 1, bytecode_len, f);
    fclose(f);

    printf("Compiled %s -> %s (%zu bytes source -> %zu bytes bytecode)\n",
           input_file, output_file, source_len, bytecode_len);

    js_free(ctx, bytecode);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}

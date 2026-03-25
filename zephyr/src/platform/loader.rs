// Handler code loader.
//
// Supports two modes:
// 1. Raw JS source (default): embeds handler.js via include_bytes!()
// 2. Pre-compiled bytecode: embeds handler.bin via include_bytes!()
//
// Bytecode mode skips the QuickJS parser at runtime, reducing startup
// latency and allowing the linker to strip parser code (~30-50KB savings).
//
// To use bytecode mode:
// 1. Build the `qjsc_compile` host tool (see scripts/compile_bytecode.sh)
// 2. Run: qjsc_compile js/handler.js zephyr/js/handler.bin
// 3. Build with: -DHANDLER_BYTECODE=1 (or set the feature flag)

/// The JavaScript handler source code, embedded at compile time.
static HANDLER_JS: &[u8] = include_bytes!("../../js/handler.js");

/// Pre-compiled handler bytecode, if available.
/// To generate: use scripts/compile_bytecode.sh or the qjsc_compile tool.
///
/// When this file exists and is non-empty, the runtime will use it
/// instead of parsing handler.js on every request.
#[cfg(feature = "bytecode")]
static HANDLER_BYTECODE: &[u8] = include_bytes!("../../js/handler.bin");

/// Handler loading result.
pub enum HandlerCode {
    /// Raw JavaScript source — needs parsing via JS_Eval + COMPILE_ONLY.
    Source(&'static [u8]),
    /// Pre-compiled QuickJS bytecode — load via JS_ReadObject.
    Bytecode(&'static [u8]),
}

/// Get the handler code (bytecode if available, else source).
pub fn handler_code() -> HandlerCode {
    #[cfg(feature = "bytecode")]
    {
        if !HANDLER_BYTECODE.is_empty() {
            return HandlerCode::Bytecode(HANDLER_BYTECODE);
        }
    }
    HandlerCode::Source(HANDLER_JS)
}

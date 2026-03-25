// Handler code loader.
//
// Embeds handler.js at compile time using Rust's include_bytes!() macro.
// This replaces the xxd-based approach used in the C codebase.

/// The JavaScript handler code, embedded at compile time.
/// This is the contents of js/handler.js.
static HANDLER_JS: &[u8] = include_bytes!("../../js/handler.js");

/// Get the embedded handler JavaScript source code.
pub fn handler_code() -> &'static [u8] {
    HANDLER_JS
}

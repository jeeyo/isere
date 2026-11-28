// JavaScript runtime integration for isere.
//
// Wraps QuickJS (C library) via FFI, providing a safe Rust interface
// for creating JS contexts, evaluating handler code, and extracting
// response objects.

pub mod quickjs_ffi;
pub mod context;
pub mod polyfills;

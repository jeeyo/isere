// JavaScript polyfills for the Zephyr environment.
//
// Provides setTimeout/clearTimeout using Zephyr kernel timers.
// This mirrors src/polyfills/quickjs/timer.c from the C codebase.

// TODO: Implement setTimeout/clearTimeout using Zephyr k_timer.
// For the initial implementation, setTimeout is registered but
// timer firing is deferred to Phase 7 polish.
//
// The QuickJS Promise-based handler flow works without timers
// for the basic case (handler returns a value or resolves a promise).
// Timers are only needed for setTimeout() calls within handlers.

// HTTP request handler.
//
// Bridges the HTTP server to the JavaScript runtime:
// 1. Creates a JS context for the request
// 2. Sets up the Lambda-compatible event/context objects
// 3. Loads the pre-compiled handler bytecode
// 4. Polls until the response promise resolves
// 5. Returns the response to the HTTP server for writeback

use crate::httpd::{HttpRequest, HttpResponse};
use crate::js::context::{JsContext, PollStatus};
use crate::platform::loader;

/// Process a parsed HTTP request through the JavaScript handler.
///
/// This creates a JS context, loads the pre-compiled bytecode handler,
/// and polls until the response is ready.
///
/// Returns Ok(()) if the handler completed, Err(()) on JS error.
pub fn handle_request(request: &HttpRequest<'_>, response: &mut HttpResponse) -> Result<(), ()> {
    let bytecode = loader::handler_bytecode();

    let mut js_ctx = JsContext::new().ok_or(())?;

    js_ctx.setup_globals(request, response);

    js_ctx.eval_handler(bytecode)?;

    // Poll pending jobs until the response callback fires
    let max_iterations = 1000; // Safety limit
    for _ in 0..max_iterations {
        if response.completed {
            break;
        }

        match js_ctx.poll() {
            Ok(PollStatus::JobsExecuted) => continue,  // More jobs pending, execute immediately
            Ok(PollStatus::WaitingForTimers) => {
                // Wait for timers, yield the thread
                extern "C" { fn isere_k_msleep(ms: i32) -> i32; }
                unsafe { isere_k_msleep(5); }
                continue;
            },
            Ok(PollStatus::Idle) => break,    // No more jobs
            Err(()) => return Err(()),
        }
    }

    // If handler didn't produce a response, send default 200
    if !response.completed {
        response.status_code = 200;
        response.completed = true;
    }

    Ok(())
}

// HTTP request handler.
//
// Bridges the HTTP server to the JavaScript runtime:
// 1. Creates a JS context for the request
// 2. Sets up the Lambda-compatible event/context objects
// 3. Evaluates the handler module
// 4. Polls until the response promise resolves
// 5. Returns the response to the HTTP server for writeback
//
// This mirrors src/http_handler.c + the JS queue logic from httpd.c.

use crate::httpd::{Connection, HttpResponse};
use crate::js::context::JsContext;
use crate::platform::loader::{self, HandlerCode};

/// Process a parsed HTTP request through the JavaScript handler.
///
/// This creates a JS context, evaluates the handler, and polls until
/// the response is ready. The response is written into conn.response.
///
/// Returns Ok(()) if the handler completed, Err(()) on JS error.
pub fn handle_request(conn: &mut Connection) -> Result<(), ()> {
    // Load handler code (bytecode or source)
    let handler = loader::handler_code();

    // Create JS context
    let mut js_ctx = JsContext::new().ok_or(())?;

    // Set up global objects (event, context, console, process.env, cb)
    js_ctx.setup_globals(&conn.request, &mut conn.response);

    // Evaluate the handler module — bytecode path skips the JS parser
    match handler {
        HandlerCode::Bytecode(bytecode) => js_ctx.eval_handler_bytecode(bytecode)?,
        HandlerCode::Source(source) => js_ctx.eval_handler(source)?,
    }

    // Poll pending jobs until the response callback fires
    // This is the same loop as in httpd.c's __httpd_task
    let max_iterations = 1000; // Safety limit
    for _ in 0..max_iterations {
        if conn.response.completed {
            break;
        }

        match js_ctx.poll() {
            Ok(true) => continue,  // More jobs pending
            Ok(false) => break,    // No more jobs
            Err(()) => return Err(()),
        }
    }

    // If handler didn't produce a response, send default 200
    if !conn.response.completed {
        conn.response.status_code = 200;
        conn.response.completed = true;
    }

    Ok(())
}

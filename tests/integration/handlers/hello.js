// Minimal well-behaved handler: no timers, no logging, deterministic body.
// Used as the baseline for smoke, concurrency and memory tests.
export const handler = async function (event, context, done) {
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'application/json' },
    body: { ok: true, path: event.path, method: event.httpMethod }
  }
}

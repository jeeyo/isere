// Never resolves, and keeps a timer pending the whole time.
//
// isere_js_poll() keeps reporting outstanding work, and the drain loop
// re-queues the connection onto the queue it is iterating, so the event loop
// cannot get back to accept(). ISERE_HTTPD_HANDLER_TIMEOUT_MS is declared but
// never enforced, so nothing cuts this short.
export const handler = async function (event, context, done) {
  setTimeout(() => {}, 2000)
  return new Promise(() => {})
}

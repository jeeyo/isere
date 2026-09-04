// Allocates several MB of JS objects per request.
//
// JS_SetMemoryLimit is effectively inert (js_def_malloc_usable_size() returns
// a hardcoded 0), so nothing currently bounds how much heap one handler can
// take. This should still be reclaimed when the context is freed -- if RSS
// climbs across requests, the per-request runtime is not being released.
export const handler = async function (event, context, done) {
  const junk = []
  for (let i = 0; i < 20000; i++) {
    junk.push({ i: i, s: 'x'.repeat(64) })
  }
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'text/plain' },
    body: 'allocated ' + junk.length
  }
}

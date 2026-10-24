// What the prelude leaves on the global object for handlers.
export default {
  fetch() {
    const names = ['__isere', '__isere_dispatch', '__isere_register', 'fetch', 'crypto', 'Response', 'Request', 'Headers', 'URL', 'TextEncoder', 'setTimeout', 'console']
    const present = {}
    for (const n of names) present[n] = typeof globalThis[n] !== 'undefined'
    return Response.json(present)
  },
}

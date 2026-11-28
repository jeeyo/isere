// Header injection attempts: none of these may reach the wire as written.
export default {
  fetch() {
    const r = new Response('bad header')
    r.headers.set('X-Injected', 'a\r\nSet-Cookie: evil=1')
    r.headers.set('X-Nul', 'a' + String.fromCharCode(0) + 'b')
    r.headers.set('Bad Name', 'x')
    r.headers.set('Content-Length', '999')
    r.headers.set('X-Fine', 'kept')
    return r
  },
}

// Echoes the request as JSON, so tests can check what reached the handler.
export default {
  async fetch(request) {
    const headers = {}
    request.headers.forEach((v, k) => { headers[k] = v })
    const url = new URL(request.url)
    return Response.json({
      method: request.method,
      path: url.pathname,
      query: url.search,
      headers,
      body: await request.text(),
    })
  },
}

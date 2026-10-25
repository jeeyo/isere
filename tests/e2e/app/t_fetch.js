// Fetches ?url= and reports what came back.
export default {
  async fetch(request) {
    const url = new URL(request.url).searchParams.get('url')
    try {
      const res = await fetch(url, {
        method: request.method === 'POST' ? 'POST' : 'GET',
        headers: { 'x-from': 'isere' },
        body: request.method === 'POST' ? await request.text() : undefined,
      })
      const body = await res.text()
      return Response.json({ status: res.status, contentType: res.headers.get('content-type'), body })
    } catch (e) {
      return Response.json({ error: String(e.message || e) }, { status: 502 })
    }
  },
}

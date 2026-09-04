// Echoes the parsed request back, so tests can assert on request-side
// parsing: header count, value truncation, empty header names, oversized
// fields, query strings.
export const handler = async function (event, context, done) {
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'application/json' },
    body: {
      method: event.httpMethod,
      path: event.path,
      query: event.query,
      headers: event.headers,
      body: event.body
    }
  }
}

// The Lambda-style handler signature, kept for compatibility.
export const handler = async (event) => ({
  statusCode: 200,
  headers: { 'Content-Type': 'application/json' },
  body: { method: event.httpMethod, path: event.path, query: event.query, headers: event.headers },
})

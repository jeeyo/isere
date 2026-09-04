// Far more response headers than ISERE_HTTPD_MAX_HTTP_HEADERS (16).
//
// The written count is clamped to 16, but the property-name atoms beyond the
// clamp are never freed, so this is expected to leak on every request.
export const handler = async function (event, context, done) {
  const headers = { 'Content-Type': 'text/plain' }
  for (let i = 0; i < 40; i++) {
    headers['X-Filler-' + i] = 'v'.repeat(32)
  }
  return { statusCode: 200, headers: headers, body: 'many headers' }
}

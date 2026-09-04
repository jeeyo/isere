// Response body far larger than any request-side buffer, to exercise the
// blocking writeback loop and the response arena.
export const handler = async function (event, context, done) {
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'text/plain' },
    body: 'Z'.repeat(64 * 1024)
  }
}

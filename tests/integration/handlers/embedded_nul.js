// Body containing an embedded NUL byte followed by filler.
//
// The C side takes body_len from the JS string length (which counts the NUL)
// but copies the buffer with strdup(), which stops at the NUL. Writeback then
// sends body_len bytes from a shorter allocation -- a heap over-read that puts
// adjacent process memory on the wire.
//
// String.fromCharCode(0) is used rather than a literal so this file stays
// plain ASCII.
export const handler = async function (event, context, done) {
  const body = 'A' + String.fromCharCode(0) + 'B'.repeat(200)
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'text/plain' },
    body: body
  }
}

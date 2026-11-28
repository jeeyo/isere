// A body with a NUL in it. isère used to strdup() it, sending heap memory after the NUL.
export const handler = async () => ({
  statusCode: 200,
  headers: { 'Content-Type': 'text/plain' },
  body: 'A' + String.fromCharCode(0) + 'B'.repeat(200),
})

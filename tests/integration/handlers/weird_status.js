// Status code outside the 3-digit range the writeback buffer assumes
// (char statusCode[4], snprintf bound 4).
export const handler = async function (event, context, done) {
  return {
    statusCode: 12345,
    headers: { 'Content-Type': 'text/plain' },
    body: 'weird status'
  }
}

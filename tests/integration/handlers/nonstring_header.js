// Non-string header values.
//
// num_header_fields counts every enumerated property, but the C side only
// assigns the pointer when the value is a string -- so writeback can end up
// calling strlen() on a NULL pointer.
export const handler = async function (event, context, done) {
  return {
    statusCode: 200,
    headers: {
      'Content-Type': 'text/plain',
      'X-Number': 42,
      'X-Object': { a: 1 },
      'X-Null': null
    },
    body: 'nonstring header'
  }
}

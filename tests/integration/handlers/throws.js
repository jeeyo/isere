// Handler that throws. The runtime should report the error; the server should
// still answer the connection and stay up for the next request.
export const handler = async function (event, context, done) {
  throw new Error('deliberate failure from integration test')
}

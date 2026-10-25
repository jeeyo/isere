// GET / : the smallest JS endpoint, in the AWS Lambda style isère started with.
export const handler = async function (event, context) {
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'text/plain' },
    body: 'hello from isère\n',
  }
}

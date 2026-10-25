export const handler = async () => ({
  statusCode: 200,
  headers: { 'Content-Type': 'text/plain', 'X-Number': 42, 'X-Object': { a: 1 }, 'X-Null': null, 'X-Undefined': undefined },
  body: 'nonstring header',
})

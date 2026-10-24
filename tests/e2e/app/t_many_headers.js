export const handler = async () => {
  const headers = { 'Content-Type': 'text/plain' }
  for (let i = 0; i < 40; i++) headers['X-Filler-' + i] = 'v'.repeat(32)
  return { statusCode: 200, headers, body: 'many headers' }
}

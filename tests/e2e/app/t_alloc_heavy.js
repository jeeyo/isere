// Allocates a few MB of short-lived objects.
export const handler = async () => {
  const junk = []
  for (let i = 0; i < 20000; i++) junk.push({ i, s: 'x'.repeat(64) })
  return { statusCode: 200, body: 'allocated ' + junk.length }
}

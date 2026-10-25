export const handler = async () => ({ statusCode: 200, headers: { 'Content-Type': 'text/plain' }, body: 'Z'.repeat(64 * 1024) })

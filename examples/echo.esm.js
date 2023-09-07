export const handler = async function(event, context, callback) {
  // console.log('globalThis.__event', globalThis.__event)
  // console.log('Test ESM')
  // console.log('## ENVIRONMENT VARIABLES: ', process.env)
  // console.log('## CONTEXT: ', context)
  // console.log('## EVENT: ', event)

  await new Promise(resolve => setTimeout(resolve, 10000));
  return {
    statusCode: 404,
    headers: { 'Content-Type': 'text/plain' },
    body: { k: 'v' }
  }
}

console.log('ESM Outside')

export const handler = async function(event, context, done) {
  console.log('Test ESM')
  console.log('## ENVIRONMENT VARIABLES: ', process.env)
  console.log('## CONTEXT: ', context)
  console.log('## EVENT: ', event)

  await Promise.resolve(resolve => setTimeout(resolve, 15000))
  return {
    statusCode: 404,
    headers: { 'Content-Type': 'text/plain' },
    body: { k: 'v' }
  }
}

console.log('ESM Outside')

export const handler = async function(event, context) {
  console.log('Test ESM')
  console.log('## ENVIRONMENT VARIABLES: ', process.env)
  console.log('## CONTEXT: ', context)
  console.log('## EVENT: ', event)

  return {
    statusCode: 404,
    headers: { 'Content-Type': 'text/plain' },
    body: { k: 'v' }
  }
}

console.log('ESM Outside')

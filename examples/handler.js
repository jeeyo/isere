export const handler = async function(event, context, done) {
  console.log('Test ESM')
  console.log('## ENVIRONMENT VARIABLES: ', process.env)
  console.log('## CONTEXT: ', context)
  console.log('## EVENT: ', event)

  setTimeout(() => {
    console.log('ESM Inside')
  }, 1000)

  // done({
  //   statusCode: 200,
  //   headers: { 'Content-Type': 'text/plain' },
  //   body: { k: 'j' }
  // })

  const a = await new Promise(resolve => resolve('555'));
  console.log('a', a)

  return {
    statusCode: 404,
    headers: { 'Content-Type': 'text/plain' },
    body: { k: 'v' }
  }
}

console.log('ESM Outside')

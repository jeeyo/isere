export const handler = async function(event, context, callback) {
  console.log('Test ESM')
  console.log('## ENVIRONMENT VARIABLES: ' + serialize(process.env))
  console.log('## CONTEXT: ' + serialize(context))
  console.log('## EVENT: ' + serialize(event))

  return { hello: 'world', body: "me" }
}

var serialize = function(object) {
  return JSON.stringify(object, null, 2)
}

console.log('ESM Outside')

// Handler
exports.handler = async function(event, context) {
  console.log('Test CJS')
  console.log('## ENVIRONMENT VARIABLES: ' + serialize(process.env))
  console.log('## CONTEXT: ' + serialize(context))
  console.log('## EVENT: ' + serialize(event))

  await new Promise(resolve => setTimeout(() => { console.log('awaited'); resolve(); }, 1000))

  console.log('first')

  return { hello: 'world' }
};

var serialize = function(object) {
  return JSON.stringify(object, null, 2)
};

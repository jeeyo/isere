// Answers, but leaves a promise chain that never settles.
export default {
  fetch() {
    const never = new Promise(() => {})
    never.then(() => console.log('unreachable'))
    return new Response('left a promise pending')
  },
}

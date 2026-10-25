// Answers, and leaves a rejected promise nobody handles.
export default {
  fetch() {
    Promise.reject(new Error('nobody catches this'))
    return new Response('left a rejection')
  },
}

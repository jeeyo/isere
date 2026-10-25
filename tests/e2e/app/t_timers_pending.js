// Timers still pending when the response goes out: isère cancels them.
export default {
  fetch() {
    for (let i = 0; i < 4; i++) setTimeout(() => console.log('should never run'), 30000)
    return new Response('timers scheduled')
  },
}

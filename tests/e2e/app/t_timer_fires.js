// A timer that fires before the response.
export default {
  async fetch() {
    await new Promise((resolve) => setTimeout(resolve, 5))
    return new Response('timer fired')
  },
}
